#include <graphene/network/dlt_p2p_node.hpp>
#include <graphene/network/config.hpp>
#include <graphene/network/exceptions.hpp>

#include <fc/network/resolve.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/io/raw.hpp>
#include <fc/log/log_message.hpp>
#include <fc/thread/thread.hpp>

#include <algorithm>
#include <cstring>
#include <random>
#include <chrono>
#include <thread>
#include <functional>

namespace graphene {
namespace network {

// ── Static constexpr out-of-line definitions (required for C++14 ODR-use) ─
constexpr uint32_t dlt_peer_state::PEER_EXCHANGE_MAX_REQUESTS;
constexpr uint32_t dlt_peer_state::PEER_EXCHANGE_WINDOW_SEC;
constexpr uint32_t dlt_peer_state::PENDING_BATCH_TIMEOUT_SEC;
constexpr uint32_t dlt_peer_state::INITIAL_RECONNECT_BACKOFF_SEC;
constexpr uint32_t dlt_peer_state::MAX_RECONNECT_BACKOFF_SEC;

constexpr uint32_t dlt_p2p_node::GAP_FILL_MAX_BLOCKS;
constexpr uint32_t dlt_p2p_node::GAP_FILL_COOLDOWN_SEC;
constexpr uint32_t dlt_p2p_node::GAP_FILL_TIMEOUT_SEC;
constexpr uint32_t dlt_p2p_node::FORWARD_STAGNATION_SEC;
constexpr uint32_t dlt_p2p_node::ISOLATION_RESET_SEC;
constexpr uint32_t dlt_p2p_node::GAP_REJECT_BLACKLIST_SEC;
constexpr uint32_t dlt_p2p_node::BLOCKED_IP_DURATION_SEC;

// ── Construction / destruction ───────────────────────────────────────

dlt_p2p_node::dlt_p2p_node(const std::string& user_agent)
    : _user_agent(user_agent),
      _node_id(fc::ecc::private_key::generate().get_public_key()) {}

dlt_p2p_node::~dlt_p2p_node() {
    close();
}

// ── Configuration ────────────────────────────────────────────────────

void dlt_p2p_node::set_delegate(dlt_p2p_delegate* del) { _delegate = del; }
void dlt_p2p_node::set_listen_endpoint(const fc::ip::endpoint& ep, bool wait) {
    _listen_endpoint = ep;
    _wait_if_busy = wait;
}
void dlt_p2p_node::add_seed_node(const fc::ip::endpoint& ep) { _seed_nodes.push_back(ep); }
void dlt_p2p_node::set_max_connections(uint32_t max_conn) { _max_connections = max_conn; }
void dlt_p2p_node::set_thread(fc::thread& t) { _thread = &t; }
void dlt_p2p_node::set_dlt_block_log_max_blocks(uint32_t max_blocks) { _dlt_block_log_max_blocks = max_blocks; }
void dlt_p2p_node::set_peer_max_disconnect_hours(uint32_t hours) { _peer_max_disconnect_hours = hours; }
void dlt_p2p_node::set_mempool_limits(uint32_t max_tx, uint32_t max_bytes, uint32_t max_tx_size, uint32_t max_expiration_hours) {
    _mempool_max_tx = max_tx;
    _mempool_max_bytes = max_bytes;
    _mempool_max_tx_size = max_tx_size;
    _mempool_max_expiration_hours = max_expiration_hours;
}
void dlt_p2p_node::set_peer_exchange_limits(uint32_t max_per_reply, uint32_t max_per_subnet, uint32_t min_uptime_sec) {
    _peer_exchange_max_per_reply = max_per_reply;
    _peer_exchange_max_per_subnet = max_per_subnet;
    _peer_exchange_min_uptime_sec = min_uptime_sec;
}

void dlt_p2p_node::set_stats_log_interval(uint32_t seconds) {
    _stats_log_interval_sec = std::max(seconds, uint32_t(30));  // minimum 30s
}

void dlt_p2p_node::set_isolated_peers(bool isolated) {
    _isolated_peers = isolated;
    if (isolated)
        ilog("DLT P2P: isolated-peers mode enabled — only seed connections allowed");
}

void dlt_p2p_node::set_witness_diag_provider(std::function<std::string()> fn) {
    _witness_diag_provider = std::move(fn);
}

void dlt_p2p_node::block_incoming_ip(uint32_t ip, const std::string& reason) {
     // NAT safety: if multiple active peers share this IP (nodes behind the same NAT),
    // blocking the IP would kill all of them.  Only block when a single peer is using
    // this IP — that's the typical single-machine attacker scenario.
    uint32_t peers_with_ip = 0;
    for (const auto& item : _peer_states) {
        const auto& s = item.second;
        if ((uint32_t)s.endpoint.get_address() == ip &&
            (s.lifecycle_state == DLT_PEER_LIFECYCLE_CONNECTING  ||
             s.lifecycle_state == DLT_PEER_LIFECYCLE_HANDSHAKING ||
             s.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING     ||
             s.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE)) {
            ++peers_with_ip;
        }
    }
    if (peers_with_ip > 1) {
        wlog(DLT_LOG_ORANGE "NAT: NOT blocking IP ${ip} (${n} active peers share this IP) — reason was: ${r}" DLT_LOG_RESET,
             ("ip", std::string(fc::ip::address(ip)))("n", peers_with_ip)("r", reason));
        return;  // Don't punish NAT peers for one misbehaving connection
    }
    
    fc::time_point unblock_at = fc::time_point::now() + fc::seconds(BLOCKED_IP_DURATION_SEC);
    _blocked_ips[ip] = unblock_at;
    wlog(DLT_LOG_ORANGE "Blocking IP ${ip} for ${d}s: ${r}" DLT_LOG_RESET,
         ("ip", std::string(fc::ip::address(ip)))("d", BLOCKED_IP_DURATION_SEC)("r", reason));
}

bool dlt_p2p_node::is_ip_blocked(uint32_t ip) {
    auto it = _blocked_ips.find(ip);
    if (it == _blocked_ips.end()) return false;
    if (fc::time_point::now() >= it->second) {
        _blocked_ips.erase(it);
        return false;
    }
    return true;
}

// ── Lifecycle ────────────────────────────────────────────────────────

void dlt_p2p_node::start() {
    _running = true;
    _node_start_time = fc::time_point::now();

    try {
        if (_listen_endpoint.port() != 0) {
            _tcp_server.listen(_listen_endpoint);
            ilog("DLT P2P listening on ${ep}", ("ep", _listen_endpoint));
        }
    } catch (const fc::exception& e) {
        wlog("DLT P2P failed to listen on ${ep}: ${e}", ("ep", _listen_endpoint)("e", e.to_detail_string()));
    }

    // Add seed nodes to known peers and register for immediate async reconnection.
    // Do NOT connect synchronously — an unreachable seed can block startup
    // for 2+ minutes on TCP SYN timeout, preventing witness block production.
    for (const auto& ep : _seed_nodes) {
        dlt_known_peer kp;
        kp.endpoint = ep;
        kp.node_id = node_id_t();
        _known_peers.insert(kp);

        // Register as DISCONNECTED with immediate reconnect so periodic_reconnect_check()
        // picks them up on the first cycle (within 5 seconds).
        peer_id pid = _next_peer_id++;
        auto& state = _peer_states[pid];
        state.endpoint = ep;
        state.lifecycle_state = DLT_PEER_LIFECYCLE_DISCONNECTED;
        state.disconnected_since = fc::time_point::now();
        state.next_reconnect_attempt = fc::time_point::now();  // immediate
        state.reconnect_backoff_sec = dlt_peer_state::INITIAL_RECONNECT_BACKOFF_SEC;
    }

    // Start accept loop as a fiber on the p2p thread
    if (_thread && _listen_endpoint.port() != 0) {
        _accept_fiber = _thread->async([this]() {
            accept_loop();
        }, "dlt accept_loop");
    }

    // Start periodic task fiber
    if (_thread) {
        _periodic_fiber = _thread->async([this]() {
            while (_running) {
                try {
                    fc::usleep(fc::seconds(5));
                    if (!_running) break;
                    periodic_task();
                } catch (const fc::exception& e) {
                    elog("Error in DLT P2P periodic task: ${e}", ("e", e.to_detail_string()));
                }
            }
        }, "dlt periodic_task");
    }

    ilog("DLT P2P node started, connecting to ${n} seed nodes", ("n", _seed_nodes.size()));
}

void dlt_p2p_node::close() {
    _running = false;

    // Cancel the accept loop fiber
    try {
        if (_accept_fiber.valid()) _accept_fiber.cancel_and_wait(__FUNCTION__);
    } catch (...) {}

    // Cancel the periodic task fiber
    try {
        if (_periodic_fiber.valid()) _periodic_fiber.cancel_and_wait(__FUNCTION__);
    } catch (...) {}

    // Cancel all read fibers
    for (auto& _fib_item : _read_fibers) {
            auto& fiber = _fib_item.second;
        try { if (fiber.valid()) fiber.cancel_and_wait(__FUNCTION__); } catch (...) {}
    }
    _read_fibers.clear();

    try {
        _tcp_server.close();
    } catch (...) {}

    for (auto& _conn_item : _connections) {
            auto& conn = _conn_item.second;
        try { if (conn) conn->close(); } catch (...) {}
    }
    _connections.clear();
    _peer_states.clear();
    _peer_sending.clear();
    ilog("DLT P2P node closed");
}

// ── Connection management ────────────────────────────────────────────
// ── Backward-compatible hello deserializer ───────────────────────────────────
// FC_REFLECT-based deserialization (msg.as<dlt_hello_message>()) expects ALL
// fields to be present in the byte stream.  Old nodes (protocol_version=1,
// 62-byte payload) do NOT include the node_id field (added in v2, 33 bytes).
// Calling msg.as<>() on such a message throws out_of_range_exception ("over by 1"
// — the first byte of the missing node_id cannot be read).
//
// This helper deserializes field-by-field and treats node_id as OPTIONAL:
// it is read only when the stream still has >= sizeof(node_id_t) bytes left.
// Unknown trailing bytes (future fields) are silently ignored.
static dlt_hello_message unpack_hello_compat(const message& msg) {
    FC_ASSERT(msg.msg_type == dlt_hello_message_type);
    dlt_hello_message hello;
    if (msg.data.empty()) return hello;
    fc::datastream<const char*> ds(msg.data.data(), msg.data.size());
    fc::raw::unpack(ds, hello.protocol_version);
    fc::raw::unpack(ds, hello.head_block_id);
    fc::raw::unpack(ds, hello.head_block_num);
    fc::raw::unpack(ds, hello.lib_block_id);
    fc::raw::unpack(ds, hello.lib_block_num);
    fc::raw::unpack(ds, hello.dlt_earliest_block);
    fc::raw::unpack(ds, hello.dlt_latest_block);
    fc::raw::unpack(ds, hello.emergency_active);
    fc::raw::unpack(ds, hello.has_emergency_key);
    fc::raw::unpack(ds, hello.fork_status);
    fc::raw::unpack(ds, hello.node_status);
    // node_id (sizeof = 33 bytes for compressed secp256k1 key) is optional:
    //   - absent   → old protocol (v1 peer, 62-byte hello) — treat as zero_id
    //   - present  → new protocol (v2+ peer, 95-byte hello) — use for NAT dedup
    if (ds.remaining() >= sizeof(node_id_t)) {
        fc::raw::unpack(ds, hello.node_id);
    }
    // Any remaining bytes are future protocol fields — ignored for forward compat.
    return hello;
}

// ── Per-node-id dedup: find any existing active connection to the same node ─
// We identify nodes by the node_id they advertise in their hello message.
// This correctly handles multiple nodes behind the same NAT (same IP, different
// ports) — each node has a unique keypair, so only true duplicates are rejected.
// Returns INVALID_PEER_ID for zero node_id (old peer that didn't send one).
dlt_p2p_node::peer_id dlt_p2p_node::find_active_peer_by_node_id(const node_id_t& nid) const {
    static const node_id_t zero_id;
    if (nid == zero_id) return INVALID_PEER_ID;
    for (const auto& item : _peer_states) {
        const auto& state = item.second;
        if (state.node_id == nid &&
            (state.lifecycle_state == DLT_PEER_LIFECYCLE_CONNECTING ||
             state.lifecycle_state == DLT_PEER_LIFECYCLE_HANDSHAKING ||
             state.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING ||
             state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE)) {
            return item.first;
        }
    }
    return INVALID_PEER_ID;
}

void dlt_p2p_node::connect_to_peer(const fc::ip::endpoint& ep) {
    if (_connections.size() >= _max_connections) return;

    // Check for existing entry — reuse DISCONNECTED, skip if active/handshaking
    peer_id pid = 0;
    bool found_existing = false;
    for (auto& item : _peer_states) {
        auto& state = item.second;
        if (state.endpoint == ep) {
            if (state.lifecycle_state != DLT_PEER_LIFECYCLE_DISCONNECTED
                && state.lifecycle_state != DLT_PEER_LIFECYCLE_BANNED) {
                return;  // already connected or connecting
            }
            // Reuse existing DISCONNECTED entry instead of creating a duplicate
            pid = item.first;
            found_existing = true;
            break;
        }
    }

    // Per-IP dedup: skip if we already have an active connection to this IP.
    // This prevents cross-direction duplication (outbound + inbound to same node)
    // which causes broadcast amplification.
    // EXCEPTION: Allow reconnect if the target peer itself is DISCONNECTED,
    // even if another connection to the same IP exists (different port).
     /* if (!found_existing) {
        fc::ip::address target_ip = ep.get_address();
        peer_id existing_ip_conn = find_active_peer_by_ip(target_ip);
        if (existing_ip_conn != INVALID_PEER_ID) {
            dlog(DLT_LOG_DGRAY "Skipping connect to ${ep} (already connected to this IP as peer ${pid})" DLT_LOG_RESET,
                 ("ep", ep)("pid", existing_ip_conn));
            return;
        }
    }*/
   // NOTE: We no longer skip outbound connections based on IP address alone.
    // Multiple nodes behind the same NAT share the same public IP but have
    // different P2P ports and unique node_ids.  Deduplication happens post-hello
    // in on_dlt_hello() where we compare node_id values.

    if (!found_existing) {
        pid = _next_peer_id++;
    }

    // Preserve only cross-session data (backoff, node_id, endpoint)
    // and reset all per-session fields that are stale from the old connection.
    // Without this, exchange_enabled=true leaks into the new session,
    // peer_head_num is stale, spam_strikes are unfair, etc.
    auto& state = _peer_states[pid];
    uint32_t saved_backoff = state.reconnect_backoff_sec;
    fc::microseconds saved_duration = state.last_connection_duration;
    node_id_t saved_node_id = state.node_id;

    state = dlt_peer_state();           // zero-init everything
    state.endpoint = ep;
    state.reconnect_backoff_sec = saved_backoff;
    state.last_connection_duration = saved_duration;
    state.node_id = saved_node_id;
    state.lifecycle_state = DLT_PEER_LIFECYCLE_CONNECTING;
    state.state_entered_time = fc::time_point::now();

    // Run the TCP connect asynchronously in a fiber so an unreachable
    // seed node doesn't block the periodic task or startup.
    if (_thread) {
        _thread->async([this, pid, ep]() {
            // Re-lookup state — the map may have rehashed since we started.
            auto it = _peer_states.find(pid);
            if (it == _peer_states.end()) return;
            auto& state = it->second;

            try {
                auto sock = std::make_shared<fc::tcp_socket>();
                sock->connect_to(ep);
                _connections[pid] = sock;

                state.lifecycle_state = DLT_PEER_LIFECYCLE_HANDSHAKING;
                state.state_entered_time = fc::time_point::now();
                state.connected_since = fc::time_point::now();

                // Send hello
                send_message(pid, message(build_hello_message()));
                ilog(DLT_LOG_GREEN "Connected to peer ${ep}, sent DLT hello" DLT_LOG_RESET, ("ep", ep));

                // Start read loop as a fiber on the p2p thread
                start_read_loop(pid);

            } catch (const fc::exception& e) {
                // Connection refused / timeout are expected transient conditions — debug level.
                // Only warn on unexpected errors.
                // Note: e.what() returns "0 exception: unspecified" for ASIO errors;
                // the actual error text (e.g. "Connection refused") is in to_detail_string().
                std::string detail = e.to_detail_string();
                bool is_expected = (detail.find("Connection refused") != std::string::npos)
                               || (detail.find("connection refused") != std::string::npos)
                               || (detail.find("actively refused") != std::string::npos)       // Windows WSA 10061
                               || (detail.find("Connection timed out") != std::string::npos)
                               || (detail.find("timed out") != std::string::npos)
                               || (detail.find("Host unreachable") != std::string::npos)
                               || (detail.find("host unreachable") != std::string::npos)       // Windows WSA 10065
                               || (detail.find("No route to host") != std::string::npos)
                               || (detail.find("network is unreachable") != std::string::npos) // Windows WSA 10051
                               || (detail.find("End of file") != std::string::npos)
                               || (detail.find("end of file") != std::string::npos)
                               || (detail.find("Operation aborted") != std::string::npos)
                               || (detail.find("operation aborted") != std::string::npos);     // Windows WSA 10004
                if (is_expected)
                    dlog(DLT_LOG_DGRAY "Connect to ${ep} failed: ${w}" DLT_LOG_RESET, ("ep", ep)("w", e.what()));
                else
                    wlog("Failed to connect to ${ep}: ${e}", ("ep", ep)("e", e.to_detail_string()));
                state.lifecycle_state = DLT_PEER_LIFECYCLE_DISCONNECTED;
                state.disconnected_since = fc::time_point::now();
                state.next_reconnect_attempt = fc::time_point::now() + fc::seconds(30);
            }
        }, "dlt connect_to_peer");
    }
}

void dlt_p2p_node::handle_disconnect(peer_id peer, const std::string& reason, bool skip_backoff_increase) {
    // cancel_and_wait below yields the current fiber.  During that yield,
    // drain_send_queue may resume from a canceled writesome() and call
    // handle_disconnect again for the same peer.  Without this guard the
    // reentrant call erases the peer from _peer_states, leaving the first
    // call's iterator and state reference dangling → UB → silent crash.
    if (!_disconnect_in_progress.insert(peer).second)
        return;
    struct Guard {
        std::set<peer_id>& s; peer_id p;
        ~Guard() { s.erase(p); }
    } _guard{_disconnect_in_progress, peer};

    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;

    auto& state = it->second;

    // Calculate connection duration for backoff reset
    if (state.connected_since != fc::time_point()) {
        state.last_connection_duration = fc::time_point::now() - state.connected_since;
        // Reset backoff if connection was stable > 5 min
        if (state.last_connection_duration.count() > 300000000) { // 5 min in microseconds
            state.reconnect_backoff_sec = dlt_peer_state::INITIAL_RECONNECT_BACKOFF_SEC;
        }
    }

    // Cancel read fiber — cancel_and_wait yields, allowing drain_send_queue
    // to resume on this thread.  The reentrancy guard above ensures that
    // reentrant handle_disconnect call returns immediately without touching
    // _peer_states, so state/it remain valid when we resume here.
    auto fiber_it = _read_fibers.find(peer);
    if (fiber_it != _read_fibers.end()) {
        if (std::current_exception() != std::exception_ptr()) {
            // Suntem în catch block — amânăm cancel_and_wait pentru periodic_task
            _dead_fibers.push_back(std::move(fiber_it->second));
        } else {
            try { if (fiber_it->second.valid()) fiber_it->second.cancel_and_wait(__FUNCTION__); } catch (...) {}
        }
        _read_fibers.erase(fiber_it);
    }

    // Close connection
    auto conn_it = _connections.find(peer);
    if (conn_it != _connections.end()) {
        try { if (conn_it->second) conn_it->second->close(); } catch (...) {}
        _connections.erase(conn_it);
    }

    // Clear send guard and drain any queued messages
    _peer_sending.erase(peer);
    state.send_queue.clear();

    // Incoming peers (from accept_loop) have ephemeral random ports —
    // there is no P2P server to reconnect to. Remove them immediately
    // instead of putting them in the DISCONNECTED reconnection cycle.
    if (state.is_incoming) {
        wlog(DLT_LOG_DGRAY "Incoming peer ${ep} disconnected: ${reason} (removed, no reconnect)" DLT_LOG_RESET,
             ("ep", state.endpoint)("reason", reason));
        _peer_states.erase(it);
        return;
    }

    state.lifecycle_state = DLT_PEER_LIFECYCLE_DISCONNECTED;
    state.disconnected_since = fc::time_point::now();
    state.expected_next_block = 0;

    // Double backoff, cap at max (skip for non-DLT peers sending garbage)
    if (!skip_backoff_increase) {
        state.reconnect_backoff_sec = std::min(state.reconnect_backoff_sec * 2,
                                                dlt_peer_state::MAX_RECONNECT_BACKOFF_SEC);
    }

    // Add jitter (±25%)
    uint32_t jitter_range = state.reconnect_backoff_sec / 2;
    thread_local std::mt19937 rng(std::hash<std::thread::id>{}(std::this_thread::get_id()) ^ uint32_t(fc::time_point::now().sec_since_epoch()));
    uint32_t jitter = (jitter_range > 0) ? (rng() % jitter_range) - (jitter_range / 2) : 0;
    state.next_reconnect_attempt = fc::time_point::now() + fc::seconds(state.reconnect_backoff_sec + jitter);

    wlog(DLT_LOG_DGRAY "Disconnected from peer ${ep}: ${reason} (backoff=${b}s)" DLT_LOG_RESET,
         ("ep", state.endpoint)("reason", reason)("b", state.reconnect_backoff_sec));
}

void dlt_p2p_node::periodic_reconnect_check() {
    auto now = fc::time_point::now();
    auto expire_threshold = now - fc::hours(_peer_max_disconnect_hours);

    for (auto it = _known_peers.begin(); it != _known_peers.end(); ) {
        auto state_it = std::find_if(_peer_states.begin(), _peer_states.end(),
            [&it](const auto& p) { return p.second.endpoint == it->endpoint; });

        if (state_it != _peer_states.end() &&
            state_it->second.lifecycle_state == DLT_PEER_LIFECYCLE_DISCONNECTED) {
            auto& state = state_it->second;

            // Permanently remove after max disconnect hours
            if (state.disconnected_since < expire_threshold) {
                wlog("Removing peer ${ep} after ${h}h of non-response",
                     ("ep", it->endpoint)("h", _peer_max_disconnect_hours));
                _peer_states.erase(state_it);
                it = _known_peers.erase(it);
                continue;
            }

            // Attempt reconnection if backoff elapsed
            if (now >= state.next_reconnect_attempt && _connections.size() < _max_connections) {
                dlog(DLT_LOG_DGRAY "Reconnecting to peer ${ep} (backoff=${b}s)" DLT_LOG_RESET,
                     ("ep", it->endpoint)("b", state.reconnect_backoff_sec));
                connect_to_peer(it->endpoint);
            }
        }
        ++it;
    }

    // Also check directly-tracked peer states
    for (auto it = _peer_states.begin(); it != _peer_states.end(); ) {
        if (it->second.lifecycle_state == DLT_PEER_LIFECYCLE_DISCONNECTED) {
            if (it->second.disconnected_since < expire_threshold) {
                // Remove from known peers too
                dlt_known_peer kp;
                kp.endpoint = it->second.endpoint;
                kp.node_id = it->second.node_id;
                _known_peers.erase(kp);
                it = _peer_states.erase(it);
                continue;
            }
        }
        ++it;
    }

    // Hard cap: if _peer_states is too large, evict oldest DISCONNECTED entries.
    // This is a safety net against any edge case that causes accumulation.
    static constexpr uint32_t MAX_PEER_STATES = 200;
    if (_peer_states.size() > MAX_PEER_STATES) {
        uint32_t excess = static_cast<uint32_t>(_peer_states.size()) - MAX_PEER_STATES;
        uint32_t removed = 0;
        // Collect DISCONNECTED peers sorted by disconnect time (oldest first)
        std::vector<std::pair<fc::time_point, peer_id>> disc_peers;
        for (const auto& item : _peer_states) {
            if (item.second.lifecycle_state == DLT_PEER_LIFECYCLE_DISCONNECTED) {
                disc_peers.emplace_back(item.second.disconnected_since, item.first);
            }
        }
        std::sort(disc_peers.begin(), disc_peers.end());
        for (uint32_t i = 0; i < std::min(excess, static_cast<uint32_t>(disc_peers.size())) && removed < excess; ++i) {
            wlog("Evicting stale peer ${ep} (peer_states cap exceeded, ${n} entries)",
                 ("ep", _peer_states[disc_peers[i].second].endpoint)
                 ("n", _peer_states.size()));
            _peer_states.erase(disc_peers[i].second);
            removed++;
        }
    }
}

void dlt_p2p_node::periodic_lifecycle_timeout_check() {
    // P40 fix: collect timed-out peers first to avoid iterator invalidation
    // when handle_disconnect erases incoming peers from _peer_states.
    std::vector<peer_id> timed_out;
    for (const auto& _peer_item : _peer_states) {
        if (_peer_item.second.has_lifecycle_timeout()) {
            dlog(DLT_LOG_DGRAY "Peer ${ep} timed out in state ${s}" DLT_LOG_RESET,
                 ("ep", _peer_item.second.endpoint)("s", (int)_peer_item.second.lifecycle_state));
            timed_out.push_back(_peer_item.first);
        }
    }
    for (auto id : timed_out) {
        handle_disconnect(id, "lifecycle timeout");
    }
}

// ── Message send/receive ─────────────────────────────────────────────

void dlt_p2p_node::send_message(peer_id peer, const message& msg) {
    auto it = _connections.find(peer);
    if (it == _connections.end() || !it->second) return;

    // Serialize the complete wire frame into a single contiguous buffer.
    // Wire format: [8-byte header (size + msg_type)][payload bytes]
    // Do NOT use fc::raw::pack(msg) which adds a varint length prefix.
    static constexpr size_t HDR_SIZE = sizeof(message_header); // 8 bytes
    const size_t total = HDR_SIZE + msg.data.size();
    std::vector<char> buf(total);
    {
        message_header hdr;
        hdr.size     = static_cast<uint32_t>(msg.data.size());
        hdr.msg_type = msg.msg_type;
        std::memcpy(buf.data(), &hdr, HDR_SIZE);
        if (!msg.data.empty()) {
            std::memcpy(buf.data() + HDR_SIZE, msg.data.data(), msg.data.size());
        }
    }

    // Per-peer send serialization: if a fiber is already writing to this
    // peer's socket (_peer_sending is set), enqueue the buffer instead.
    // The active writer will drain the queue after finishing its write.
    if (_peer_sending.count(peer)) {
        auto state_it = _peer_states.find(peer);
        if (state_it != _peer_states.end()) {
            auto& state = state_it->second;
            if (state.send_queue.size() < dlt_peer_state::SEND_QUEUE_MAX_DEPTH) {
                state.send_queue.push_back(std::move(buf));
                ++state.send_queue_total;
            } else {
                // Queue is at max depth — peer can't consume data fast enough.
                // Capture info before handle_disconnect potentially erases the state.
                std::string ep = std::string(state.endpoint);
                uint32_t dropped = state.send_queue_dropped;
                wlog("Send queue full (depth=${d}, previously dropped=${n}, msg_type=${t}), "
                     "disconnecting slow peer ${ep}",
                     ("d", dlt_peer_state::SEND_QUEUE_MAX_DEPTH)("n", dropped)
                     ("t", msg.msg_type)("ep", ep));
                handle_disconnect(peer, "send queue full");
            }
        }
        return;
    }

    // No send in progress — claim the peer and start draining.
    _peer_sending.insert(peer);
    drain_send_queue(peer, std::move(buf));
}

void dlt_p2p_node::drain_send_queue(peer_id peer, std::vector<char> buf) {
    auto conn_it = _connections.find(peer);
    if (conn_it == _connections.end() || !conn_it->second) {
        _peer_sending.erase(peer);
        return;
    }
    auto& sock = conn_it->second;

    // Cache endpoint before entering the try block — handle_disconnect may
    // remove the peer from _peer_states before the catch block runs, making
    // the endpoint lookup fail and falling back to the numeric peer id.
    std::string peer_ep;
    {
        auto ep_it = _peer_states.find(peer);
        peer_ep = (ep_it != _peer_states.end()) ? std::string(ep_it->second.endpoint) : std::to_string(peer);
    }

    try {
        while (true) {
            // Write the current buffer to the socket in a loop —
            // writesome() may return after writing only a subset of
            // the requested bytes (partial write).
            const char* ptr = buf.data();
            size_t remaining = buf.size();
            while (remaining > 0) {
                size_t written = sock->writesome(ptr, remaining);
                if (written == 0) {
                    FC_THROW_EXCEPTION(fc::exception,
                        "writesome returned 0 bytes — peer connection stalled");
                }
                ptr       += written;
                remaining -= written;
            }

            // Check for queued messages
            auto state_it = _peer_states.find(peer);
            if (state_it == _peer_states.end() || state_it->second.send_queue.empty()) {
                break; // nothing more to send
            }
            buf = std::move(state_it->second.send_queue.front());
            state_it->second.send_queue.pop_front();
        }
    } catch (const fc::exception& e) {
        wlog("Failed to send to peer ${ep}: ${e}", ("ep", peer_ep)("e", e.to_detail_string()));
        _peer_sending.erase(peer);
        handle_disconnect(peer, "send failed");
        return;
    }
    _peer_sending.erase(peer);
}

void dlt_p2p_node::send_to_all_our_fork_peers(const message& msg, peer_id exclude, const block_id_type& block_id) {
    // Per-IP dedup: send to each unique IP only once, even if multiple
    // peer entries exist for the same IP (belt-and-suspenders safety net).
    // Dedup by node_id: send to each unique node only once, even if multiple
    // peer entries exist for the same node (e.g. duplicate connections still
    // being cleaned up).  We do NOT dedup by IP address — multiple distinct
    // nodes can share the same NAT IP and each deserves its own copy.
    // Falls back to endpoint dedup for peers without a node_id (old protocol).
    std::set<node_id_t>        sent_to_node_ids;
    std::set<fc::ip::endpoint> sent_to_endpoints;
    static const node_id_t zero_id;

    // Diagnostic: count eligible vs skipped peers
    uint32_t eligible = 0, skipped_not_exchange = 0, skipped_not_active = 0, skipped_echo = 0, skipped_peer_syncing = 0;
    const bool is_block_broadcast = (block_id != block_id_type());
    const bool is_tx_broadcast = (msg.msg_type == dlt_transaction_message_type);

    // fix: collect target peers first to avoid iterator invalidation
    // when send_message -> handle_disconnect erases incoming peers.
    std::vector<peer_id> targets;
    for (const auto& _peer_item : _peer_states) {
        const auto& id = _peer_item.first;
        const auto& state = _peer_item.second;
        if (id == exclude) continue;
        if (state.lifecycle_state != DLT_PEER_LIFECYCLE_ACTIVE) {
            if (state.lifecycle_state == DLT_PEER_LIFECYCLE_HANDSHAKING ||
                state.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING ||
                state.lifecycle_state == DLT_PEER_LIFECYCLE_CONNECTING)
                skipped_not_active++;
            continue;
        }
        if (!state.exchange_enabled) {
            skipped_not_exchange++;
            continue;
        }
        // Skip transaction broadcast to peers still in SYNC mode: they cannot
        // validate TaPoS for recent txs (their block_summary buffer holds
        // older blocks at the referenced slots), so the trx is wasted
        // bandwidth. Blocks and other message types still flow normally so
        // the peer can finish catching up. peer_node_status is set from the
        // peer's hello (line 843) and refreshed via fork_status (line 1543).
        if (is_tx_broadcast && state.peer_node_status == DLT_NODE_STATUS_SYNC) {
            skipped_peer_syncing++;
            continue;
        }
        // Echo suppression: skip peers already known to have this block
        if (is_block_broadcast && state.has_block(block_id)) {
            skipped_echo++;
            continue;
        }
        // Dedup: skip if we already queued a send to the same node.
        // Use node_id when available (correctly handles NAT), fall back to
        // full endpoint (IP:port) for old peers without a node_id.
        if (state.node_id != zero_id) {
            if (sent_to_node_ids.count(state.node_id)) continue;
            sent_to_node_ids.insert(state.node_id);
        } else {
            if (sent_to_endpoints.count(state.endpoint)) continue;
            sent_to_endpoints.insert(state.endpoint);
        }
        targets.push_back(id);
        eligible++;
    }
    for (auto id : targets) {
        send_message(id, msg);
        // Record that this peer now has this block
        if (is_block_broadcast) {
            auto it = _peer_states.find(id);
            if (it != _peer_states.end()) {
                it->second.record_known_block(block_id);
            }
        }
    }

    // diagnostic: log relay stats for block messages
    if (msg.msg_type == dlt_block_reply_message_type) {
        dlog(DLT_LOG_DGRAY "Relay block_reply to ${e} peers (${nx} skipped: no_exchange, ${na} skipped: not_active, ${ne} skipped: echo)" DLT_LOG_RESET,
             ("e", eligible)("nx", skipped_not_exchange)("na", skipped_not_active)("ne", skipped_echo));
    }
    if (msg.msg_type == dlt_transaction_message_type) {
        dlog(DLT_LOG_DGRAY "Relay transaction to ${e} peers (${nx} skipped: no_exchange, ${na} skipped: not_active, ${ns} skipped: peer_syncing)" DLT_LOG_RESET,
             ("e", eligible)("nx", skipped_not_exchange)("na", skipped_not_active)("ns", skipped_peer_syncing));
    }
}

bool dlt_p2p_node::on_message(peer_id peer, const message& msg) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return true;  // peer already gone, not a desync issue

    // Block processing pause check
    if (_block_processing_paused) {
        switch (msg.msg_type) {
            // Always allow hello messages for peer state maintenance
            case dlt_hello_message_type:
            case dlt_hello_reply_message_type:
                break; // fall through to normal processing

            // Allow fork status to keep peer_head_num up to date
            case dlt_fork_status_message_type:
                break; // fall through to normal processing

            // Buffer block-carrying messages for processing after pause
            case dlt_block_reply_message_type:
                try {
                    auto reply = msg.as<dlt_block_reply_message>();
                    if (_paused_block_queue.size() < PAUSED_QUEUE_MAX)
                        _paused_block_queue.push_back(std::move(reply.block));
                } catch (const fc::exception& e) {
                    wlog("Failed to buffer block during pause: ${e}", ("e", e.what()));
                }
                return true;

            case dlt_block_range_reply_message_type:
                try {
                    auto reply = msg.as<dlt_block_range_reply_message>();
                    for (auto& b : reply.blocks) {
                        if (_paused_block_queue.size() >= PAUSED_QUEUE_MAX)
                            break;
                        _paused_block_queue.push_back(std::move(b));
                    }
                } catch (const fc::exception& e) {
                    wlog("Failed to buffer block range during pause: ${e}", ("e", e.what()));
                }
                return true;

            case dlt_gap_fill_reply_type:
                try {
                    auto reply = msg.as<dlt_gap_fill_reply>();
                    for (auto& b : reply.blocks) {
                        if (_paused_block_queue.size() >= PAUSED_QUEUE_MAX)
                            break;
                        _paused_block_queue.push_back(std::move(b));
                    }
                } catch (const fc::exception& e) {
                    wlog("Failed to buffer gap-fill block during pause: ${e}", ("e", e.what()));
                }
                return true;

            // Drop everything else (transactions, peer exchange, etc.)
            default:
                return true;
        }
    }

    try {
        switch (msg.msg_type) {
            case dlt_hello_message_type:
              // unpack_hello_compat handles both v1 (no node_id, 62 bytes) and
                // v2+ (with node_id, 95 bytes) — avoids out_of_range_exception.
                on_dlt_hello(peer, unpack_hello_compat(msg));
                break;
            case dlt_hello_reply_message_type:
                on_dlt_hello_reply(peer, msg.as<dlt_hello_reply_message>());
                break;
            case dlt_range_request_message_type:
                on_dlt_range_request(peer, msg.as<dlt_range_request_message>());
                break;
            case dlt_range_reply_message_type:
                on_dlt_range_reply(peer, msg.as<dlt_range_reply_message>());
                break;
            case dlt_get_block_range_message_type:
                on_dlt_get_block_range(peer, msg.as<dlt_get_block_range_message>());
                break;
            case dlt_block_range_reply_message_type:
                try {
                    on_dlt_block_range_reply(peer, msg.as<dlt_block_range_reply_message>());
                } catch (const fc::exception& e) {
                    // Deserialization of range reply failed (corrupted block data
                    // from peer's dlt_block_log, protocol mismatch, etc.)
                    // The message was fully read from TCP — stream is still aligned.
                    // Fall back to single-block requests to isolate the bad block.
                    auto ep_it_rr = _peer_states.find(peer);
                    if (ep_it_rr != _peer_states.end()) {
                        auto& rr_state = ep_it_rr->second;
                        rr_state.range_fallback_mode = true;
                        wlog(DLT_LOG_ORANGE "Range reply deserialization failed from ${ep}, switching to single-block mode" DLT_LOG_RESET,
                             ("ep", rr_state.endpoint));
                        // Start single-block fetch from our head
                        if (_node_status == DLT_NODE_STATUS_SYNC) {
                            uint32_t fallback_start = _delegate->get_head_block_num();
                            uint32_t our_lib = _delegate->get_lib_block_num();
                            if (our_lib > 0 && our_lib < fallback_start) {
                                fallback_start = our_lib;
                            }
                            dlt_get_block_message single_req;
                            single_req.block_num = fallback_start;
                            single_req.prev_block_id = _delegate->get_head_block_id();
                            send_message(peer, message(single_req));
                        }
                    }
                }
                break;
            case dlt_get_block_message_type:
                on_dlt_get_block(peer, msg.as<dlt_get_block_message>());
                break;
            case dlt_block_reply_message_type:
                on_dlt_block_reply(peer, msg.as<dlt_block_reply_message>());
                break;
            case dlt_not_available_message_type:
                on_dlt_not_available(peer, msg.as<dlt_not_available_message>());
                break;
            case dlt_fork_status_message_type:
                on_dlt_fork_status(peer, msg.as<dlt_fork_status_message>());
                break;
            case dlt_peer_exchange_request_type:
                on_dlt_peer_exchange_request(peer, msg.as<dlt_peer_exchange_request>());
                break;
            case dlt_peer_exchange_reply_type:
                on_dlt_peer_exchange_reply(peer, msg.as<dlt_peer_exchange_reply>());
                break;
            case dlt_peer_exchange_rate_limited_type:
                on_dlt_peer_exchange_rate_limited(peer, msg.as<dlt_peer_exchange_rate_limited>());
                break;
            case dlt_transaction_message_type:
                on_dlt_transaction(peer, msg.as<dlt_transaction_message>());
                break;
            case dlt_soft_ban_message_type:
                on_dlt_soft_ban(peer, msg.as<dlt_soft_ban_message>());
                break;
            case dlt_gap_fill_request_type:
                on_dlt_gap_fill_request(peer, msg.as<dlt_gap_fill_request>());
                break;
            case dlt_gap_fill_reply_type:
                on_dlt_gap_fill_reply(peer, msg.as<dlt_gap_fill_reply>());
                break;
            default:
                auto ep_it_unk = _peer_states.find(peer);
                auto ep_unk = (ep_it_unk != _peer_states.end()) ? std::string(ep_it_unk->second.endpoint) : std::to_string(peer);
                wlog("Unknown DLT message type ${t} from peer ${ep}", ("t", msg.msg_type)("ep", ep_unk));
                record_packet_result(peer, false);
                break;
        }
    } catch (const fc::exception& e) {
        auto ep_it_msg = _peer_states.find(peer);
        auto ep_msg = (ep_it_msg != _peer_states.end()) ? std::string(ep_it_msg->second.endpoint) : std::to_string(peer);
        wlog("Error processing message type ${t} from peer ${ep}: ${e}",
             ("t", msg.msg_type)("ep", ep_msg)("e", e.to_detail_string()));
        // Deserialization failures leave the TCP stream desynchronized —
        // the read cursor is at an unknown offset relative to the next
        // real message boundary.  Continuing would interpret payload
        // bytes as headers, producing garbage (e.g. oversized-message)
        // and eventual cascading disconnects.  Close immediately.
        return false;
    }
    return true;
}

// ── Hello handlers ───────────────────────────────────────────────────

dlt_hello_message dlt_p2p_node::build_hello_message() const {
    dlt_hello_message hello;
    hello.protocol_version = 1;
    hello.head_block_id = _delegate->get_head_block_id();
    hello.head_block_num = _delegate->get_head_block_num();
    hello.lib_block_id = _delegate->get_lib_block_id();
    hello.lib_block_num = _delegate->get_lib_block_num();
    hello.dlt_earliest_block = _delegate->get_dlt_earliest_block();
    hello.dlt_latest_block = _delegate->get_dlt_latest_block();
    hello.emergency_active = _delegate->is_emergency_consensus_active();
    hello.has_emergency_key = _delegate->has_emergency_private_key();
    hello.fork_status = _fork_status;
    hello.node_status = _node_status;
    hello.node_id = _node_id;  // identify ourselves so NAT peers can dedup by node, not IP
    return hello;
}

dlt_hello_reply_message dlt_p2p_node::build_hello_reply(peer_id peer, const dlt_hello_message& hello) const {
    dlt_hello_reply_message reply;
    reply.our_dlt_earliest = _delegate->get_dlt_earliest_block();
    reply.our_dlt_latest = _delegate->get_dlt_latest_block();
    reply.our_fork_status = _fork_status;
    reply.our_node_status = _node_status;

    // Check fork alignment using DLT-range-aware logic
    block_id_type recognized_head, recognized_lib;
    reply.fork_alignment = check_fork_alignment(hello, recognized_head, recognized_lib);
    reply.initiator_head_seen = recognized_head;
    reply.initiator_lib_seen = recognized_lib;
    reply.exchange_enabled = reply.fork_alignment; // exchange enabled if fork-aligned

    return reply;
}

bool dlt_p2p_node::check_fork_alignment(const dlt_hello_message& hello,
                                          block_id_type& recognized_head_out, block_id_type& recognized_lib_out) const {
    if (!_delegate) return false;

    // ── Empty peer (no blocks at all) ──────────────────────────
    // An empty peer has no fork to be on — accept it so it stays connected
    // and can eventually receive a snapshot. Don't mark any block as
    // recognized since it has none.
    if (hello.head_block_num == 0) {
        return true;
    }

    uint32_t our_earliest = _delegate->get_dlt_earliest_block();
    uint32_t our_latest = _delegate->get_dlt_latest_block();

    // ── Range overlap: peer's head is within our DLT range ─────
    if (hello.head_block_num >= our_earliest && hello.head_block_num <= our_latest) {
        if (_delegate->is_block_known(hello.head_block_id)) {
            recognized_head_out = hello.head_block_id;
        }
    }

    // ── Boundary link: peer's head is just before our earliest ─
    // In DLT mode, blocks below our_earliest are pruned so is_block_known()
    // returns false. But if our earliest block's previous links to the
    // peer's head, the peer IS on our chain.
    if (hello.head_block_num + 1 == our_earliest && our_earliest > 0) {
        auto our_earliest_block = _delegate->read_block_by_num(our_earliest);
        if (our_earliest_block.valid() && our_earliest_block->previous == hello.head_block_id) {
            recognized_head_out = hello.head_block_id;
        }
    }

    // ── LIB-based fallback ─────────────────────────────────────
    // If head check didn't match, try the LIB — it may be within our range
    // even when the head is on a competing tip.
    if (_delegate->is_block_known(hello.lib_block_id)) {
        recognized_lib_out = hello.lib_block_id;
    }

    return (recognized_head_out != block_id_type() || recognized_lib_out != block_id_type());
}

void dlt_p2p_node::on_dlt_hello(peer_id peer, const dlt_hello_message& hello) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;
    auto& state = it->second;

    // Protocol version check
    uint16_t our_major = 1; // current protocol version
    uint16_t their_major = hello.protocol_version;
    if (their_major != our_major) {
        wlog("Peer ${ep} has different protocol version (${theirs} vs ${ours}), disabling exchange",
             ("ep", state.endpoint)("theirs", their_major)("ours", our_major));
    }
    // Persist node_id — used for dedup and peer-exchange identity.
    state.node_id = hello.node_id;

    // ── Post-hello node_id dedup ────────────────────────────────────────────
    // Now that we know the remote node's identity, check if we already have an
    // active connection to the exact same node.  This correctly handles:
    //   • A node reconnecting before the old connection was cleaned up
    //   • Simultaneous inbound + outbound to the same node
    // It does NOT fire for two different nodes sharing the same NAT IP, because
    // each node generates a unique keypair (node_id).
    static const node_id_t zero_id;
    if (hello.node_id != zero_id) {
        peer_id dup = find_active_peer_by_node_id(hello.node_id);
        if (dup != INVALID_PEER_ID && dup != peer) {
            auto dup_it = _peer_states.find(dup);
            auto dup_ep = (dup_it != _peer_states.end()) ? dup_it->second.endpoint : fc::ip::endpoint();
            dlog(DLT_LOG_DGRAY "Closing duplicate connection from ${ep} "
                 "(same node_id already active as peer ${dup} at ${dep})" DLT_LOG_RESET,
                 ("ep", state.endpoint)("dup", dup)("dep", dup_ep));
            handle_disconnect(peer, "duplicate node_id");
            return;
        }
    }

    // Store peer's chain state
    state.peer_head_id = hello.head_block_id;
    state.peer_head_num = hello.head_block_num;
    state.peer_lib_id = hello.lib_block_id;
    state.peer_lib_num = hello.lib_block_num;
    state.peer_dlt_earliest = hello.dlt_earliest_block;
    state.peer_dlt_latest = hello.dlt_latest_block;
    state.peer_emergency_active = hello.emergency_active;
    state.peer_has_emergency_key = hello.has_emergency_key;
    state.peer_fork_status = hello.fork_status;
    state.peer_node_status = hello.node_status;

    // Build and send reply
    auto reply = build_hello_reply(peer, hello);
    state.exchange_enabled = reply.exchange_enabled;
    state.fork_alignment = reply.fork_alignment;
    state.recognized_head = reply.initiator_head_seen;
    state.recognized_lib = reply.initiator_lib_seen;
    send_message(peer, message(reply));

    // Transition to active
    if (state.lifecycle_state == DLT_PEER_LIFECYCLE_HANDSHAKING) {
        if (reply.exchange_enabled || _node_status == DLT_NODE_STATUS_SYNC
            || hello.node_status == DLT_NODE_STATUS_SYNC) {
            state.lifecycle_state = DLT_PEER_LIFECYCLE_ACTIVE;
            state.state_entered_time = fc::time_point::now();
        }
    }

    record_packet_result(peer, true);

    // If we're in SYNC mode and exchange is enabled, start fetching blocks.
    if (_node_status == DLT_NODE_STATUS_SYNC && reply.exchange_enabled) {
        request_blocks_from_peer(peer);
    }

    if (!reply.exchange_enabled && hello.node_status == DLT_NODE_STATUS_SYNC) {
        ilog(DLT_LOG_ORANGE "SYNC peer ${ep} at head #${hn} not yet fork-aligned (needs snapshot or boundary catch-up)" DLT_LOG_RESET,
             ("ep", state.endpoint)("hn", hello.head_block_num));
    }

    dlog(DLT_LOG_DGRAY "Received DLT hello from ${ep}: head=#${hn} lib=#${ln} fork=${f} node=${ns} exchange=${ex}" DLT_LOG_RESET,
         ("ep", state.endpoint)("hn", hello.head_block_num)("ln", hello.lib_block_num)
         ("f", (int)hello.fork_status)("ns", (int)hello.node_status)("ex", reply.exchange_enabled));
}

void dlt_p2p_node::on_dlt_hello_reply(peer_id peer, const dlt_hello_reply_message& reply) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;
    auto& state = it->second;

    // P27 fix: Use OR to combine local and remote exchange_enabled determinations.
    //
    // The local determination (set in on_dlt_hello) reflects whether WE can
    // verify the peer's chain (their head/LIB is known to us). The remote
    // determination (in reply) reflects whether THEY can verify OUR chain.
    //
    // Without OR, a slave that can't verify the master's ahead-of-us chain
    // sends hello_reply with exchange_enabled=false. The master then
    // overwrites its own exchange_enabled=true with false, which stops ALL
    // block broadcasts to the slave — even though the master knows the slave
    // IS on its fork.
    //
    // With OR: if EITHER side considers the peer fork-aligned, exchange is
    // enabled. This is correct because:
    //  - If we think the peer is on our fork → we should send blocks to them
    //  - If they think we're on their fork → we should request blocks from them
    //  - If both are false → truly different forks, no exchange
    state.exchange_enabled = state.exchange_enabled || reply.exchange_enabled;
    state.fork_alignment = state.fork_alignment || reply.fork_alignment;
    state.recognized_head = reply.initiator_head_seen;
    state.recognized_lib = reply.initiator_lib_seen;

    // Transition to active
    if (state.lifecycle_state == DLT_PEER_LIFECYCLE_HANDSHAKING ||
        state.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) {
        state.lifecycle_state = DLT_PEER_LIFECYCLE_ACTIVE;
        state.state_entered_time = fc::time_point::now();
    }

    record_packet_result(peer, true);

    // If exchange is enabled, request blocks when appropriate
    if (state.exchange_enabled) {
        if (_node_status == DLT_NODE_STATUS_SYNC) {
            // SYNC mode: always request blocks from exchange-enabled peers
            request_blocks_from_peer(peer);
        } else if (_node_status == DLT_NODE_STATUS_FORWARD) {
            // FORWARD mode: if the peer is significantly ahead of us, we've
            // fallen behind (missed broadcast blocks) — transition to SYNC
            // and request the missing range.
            uint32_t our_head = _delegate->get_head_block_num();
            if (state.peer_head_num > our_head + FORWARD_FALLBEHIND_THRESHOLD) {
                ilog(DLT_LOG_ORANGE "Falling behind in FORWARD mode: peer ${ep} at #${pn}, our head #${hn} — transitioning to SYNC" DLT_LOG_RESET,
                     ("ep", state.endpoint)("pn", state.peer_head_num)("hn", our_head));
                transition_to_sync();
                request_blocks_from_peer(peer);
            }
        }
    } else {
        ilog(DLT_LOG_ORANGE "Peer ${ep} says we are NOT on its fork (fork_alignment=false)" DLT_LOG_RESET,
             ("ep", state.endpoint));
    }
}

// ── Block request/reply handlers ─────────────────────────────────────

void dlt_p2p_node::request_blocks_from_peer(peer_id peer) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;

    uint32_t our_head = _delegate->get_head_block_num();
    // P42 fix: Use the freshest view of the peer's highest block.
    // peer_dlt_latest is set during hello exchange and goes stale
    // as blocks arrive.  peer_head_num is updated from received
    // blocks (P37) and reflects the real chain tip.
    uint32_t peer_latest = std::max(it->second.peer_dlt_latest, it->second.peer_head_num);

    if (our_head >= peer_latest) {
        // We're caught up with this peer
        if (_node_status == DLT_NODE_STATUS_SYNC) {
            // Check if ALL peers are caught up
            bool all_caught_up = true;
            for (auto& _peer_item : _peer_states) {
            auto& s = _peer_item.second;
                uint32_t s_latest = std::max(s.peer_dlt_latest, s.peer_head_num);
                if ((s.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE ||
                     s.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) && s_latest > our_head) {
                    all_caught_up = false;
                    break;
                }
            }
            if (all_caught_up) {
                transition_to_forward();
            }
        }
        return;
    }

    // P49 fix: Start from our_head (not our_head+1) so the peer's version
    // of our head block is fetched. If two witnesses signed different blocks
    // at the same height, the peer may have the competing version. Without
    // this, the sync range skips the divergence point and blocks from the
    // competing fork accumulate as unlinkable in fork_db forever.
    // If the block is the same as ours, accept_block returns ALREADY_KNOWN
    // with no side effects.
    //
    // LIB fallback: When significantly behind a peer (gap > threshold),
    // start from LIB instead of head. If our head diverged from the
    // network (e.g. our witness produced blocks the network rejected),
    // requesting from head only gets ALREADY_KNOWN for head, then all
    // subsequent blocks go to fork_db as unlinkable. LIB is guaranteed
    // on the majority fork, giving the chain a chance to switch forks.
    uint32_t start = our_head;
    uint32_t gap = (peer_latest > our_head) ? (peer_latest - our_head) : 0;
    if (gap > FORWARD_FALLBEHIND_THRESHOLD) {
        uint32_t our_lib = _delegate->get_lib_block_num();
        if (our_lib > 0 && our_lib < our_head) {
            ilog(DLT_LOG_ORANGE "Large gap (${g} blocks behind peer ${ep}), starting sync from LIB #${lib} instead of head #${head}" DLT_LOG_RESET,
                 ("g", gap)("ep", it->second.endpoint)("lib", our_lib)("head", our_head));
            start = our_lib;
        }
    }

    // Don't request blocks below the peer's DLT range — those blocks
    // are pruned and the peer can't serve them.  Clamp start to the
    // peer's earliest available block.
    uint32_t peer_earliest = it->second.peer_dlt_earliest;
    if (start < peer_earliest && peer_earliest > 0) {
        // P19 fix: Detect unbridgeable gap. If no peer has the missing
        // blocks, we need a snapshot. Try to find a peer that can bridge.
        ilog(DLT_LOG_ORANGE "Gap detected: blocks ${a}-${b} missing (our head=#${h}, peer ${ep} DLT starts at ${c})" DLT_LOG_RESET,
             ("a", our_head + 1)("b", peer_earliest - 1)("h", our_head)("ep", it->second.endpoint)("c", peer_earliest));

        // Check if any other peer can serve the missing blocks
        peer_id bridging_peer = peer_id();
        for (const auto& _pi : _peer_states) {
            if (_pi.first == peer) continue;
            const auto& ps = _pi.second;
            if (ps.lifecycle_state != DLT_PEER_LIFECYCLE_ACTIVE &&
                ps.lifecycle_state != DLT_PEER_LIFECYCLE_SYNCING) continue;
            uint32_t ps_latest = std::max(ps.peer_dlt_latest, ps.peer_head_num);
            if (ps.peer_dlt_earliest > 0 && ps.peer_dlt_earliest <= start &&
                ps_latest > our_head) {
                bridging_peer = _pi.first;
                ilog(DLT_LOG_GREEN "Peer ${ep2} can bridge gap (DLT range ${e}-${l})" DLT_LOG_RESET,
                     ("ep2", ps.endpoint)("e", ps.peer_dlt_earliest)("l", ps.peer_dlt_latest));
                break;
            }
        }

        if (bridging_peer != peer_id()) {
            // Another peer can fill the gap — actively request from it.
            ilog(DLT_LOG_ORANGE "Deferring sync from ${ep} — requesting from bridge peer ${ep2} instead" DLT_LOG_RESET,
                 ("ep", it->second.endpoint)("ep2", _peer_states[bridging_peer].endpoint));
            request_blocks_from_peer(bridging_peer);
            return;
        }

        // No peer can bridge the gap — we may need a snapshot.
        wlog(DLT_LOG_RED "No peer has blocks ${a}-${b}. Snapshot may be required to continue sync." DLT_LOG_RESET,
             ("a", our_head + 1)("b", peer_earliest - 1));

        // Still attempt clamped request — the blocks might be linkable
        // via fork_db or boundary link, even if they don't directly
        // follow our head.
        start = peer_earliest;
    }

    // After clamping, start may now be beyond peer_latest (gap case).
    if (start > peer_latest) {
        return;
    }

    uint32_t end = std::min(start + 200 - 1, peer_latest); // max 200 blocks per request

    dlt_get_block_range_message req;
    req.start_block_num = start;
    req.end_block_num = end;
    req.prev_block_id = _delegate->get_head_block_id();

    it->second.pending_sync_start = start;
    it->second.pending_sync_end = end;
    it->second.lifecycle_state = DLT_PEER_LIFECYCLE_SYNCING;
    it->second.state_entered_time = fc::time_point::now();

    // Reset expected_next_block for the new range request to prevent
    // false "out of order" errors when the response arrives. The
    // expected_next_block may be stale from previous sync attempts
    // or from blocks received from other peers.
    it->second.expected_next_block = 0;

    send_message(peer, message(req));
    ilog(DLT_LOG_GREEN "Requesting blocks ${s}-${e} from ${ep}" DLT_LOG_RESET,
         ("s", start)("e", end)("ep", it->second.endpoint));
}

void dlt_p2p_node::on_dlt_range_request(peer_id peer, const dlt_range_request_message& req) {
    dlt_range_reply_message reply;
    block_id_type found_id;

    if (_delegate->block_exists_in_log_or_fork_db(req.block_num, found_id)) {
        reply.has_blocks = true;
        reply.range_start = _delegate->get_dlt_earliest_block();
        reply.range_end = _delegate->get_dlt_latest_block();
    } else {
        reply.has_blocks = false;
        reply.range_start = 0;
        reply.range_end = 0;
    }

    send_message(peer, message(reply));
    record_packet_result(peer, true);
}

void dlt_p2p_node::on_dlt_range_reply(peer_id peer, const dlt_range_reply_message& reply) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;

    if (reply.has_blocks) {
        it->second.peer_dlt_earliest = reply.range_start;
        it->second.peer_dlt_latest = reply.range_end;
        // Now request the actual blocks
        request_blocks_from_peer(peer);
    } else {
        record_packet_result(peer, true);
    }
}

void dlt_p2p_node::on_dlt_get_block_range(peer_id peer, const dlt_get_block_range_message& req) {
    dlt_block_range_reply_message reply;
    uint32_t our_latest = _delegate->get_dlt_latest_block();

    // Leave 64KB headroom for the reply wrapper fields
    // (last_block_next_available, is_last, vector overhead)
    static constexpr uint32_t REPLY_SIZE_HEADROOM = 64 * 1024;
    uint32_t budget = (MAX_MESSAGE_SIZE > REPLY_SIZE_HEADROOM)
                     ? MAX_MESSAGE_SIZE - REPLY_SIZE_HEADROOM : MAX_MESSAGE_SIZE;

    for (uint32_t n = req.start_block_num; n <= req.end_block_num && n <= our_latest; ++n) {
        auto block = _delegate->read_block_by_num(n);
        if (block.valid()) {
            // Check if adding this block would exceed the message size budget.
            // pack_size is expensive but we only call it per-block here.
            uint32_t block_size = static_cast<uint32_t>(fc::raw::pack_size(*block));
            uint32_t current_size = static_cast<uint32_t>(fc::raw::pack_size(reply));
            if (current_size + block_size > budget && !reply.blocks.empty()) {
                // This block would push us over the limit — stop here and
                // report that more blocks are available.
                uint32_t last_num = reply.blocks.back().block_num();
                reply.last_block_next_available = n;
                reply.is_last = false;
                ilog(DLT_LOG_DGRAY "Range reply size limit reached after ${c} blocks (#${f}-#${l}), continuing from #${n}" DLT_LOG_RESET,
                     ("c", reply.blocks.size())("f", reply.blocks.front().block_num())
                     ("l", last_num)("n", n));
                send_message(peer, message(reply));
                record_packet_result(peer, true);
                return;
            }
            reply.blocks.push_back(std::move(*block));
        }
    }

    if (!reply.blocks.empty()) {
        // Check if there are more blocks after the range
        uint32_t last_num = reply.blocks.back().block_num();
        reply.last_block_next_available = (last_num < our_latest) ? last_num + 1 : 0;
        reply.is_last = (last_num >= our_latest);
    }

    send_message(peer, message(reply));
    record_packet_result(peer, true);
}

// Fiber-aware serialization wrapper around accept_block.
// Per-peer read loops run as separate FC fibers on the same OS thread.
// If fiber A holds the chainbase write lock (inside push_block) and yields
// at a cooperative point (e.g. fc::thread::async in update_lib's signal),
// fiber B wakes up and tries accept_block → push_block → write lock →
// waiter_tid == writer_tid deadlock.  Spinning here with fc::usleep yields
// fiber B back to the scheduler so fiber A can finish and release the lock.
dlt_block_accept_result dlt_p2p_node::call_accept_block(
        const graphene::protocol::signed_block& block, bool sync_mode) {
    while (_accept_block_in_progress) fc::usleep(fc::microseconds(100));
    _accept_block_in_progress = true;
    try {
        auto result = _delegate->accept_block(block, sync_mode);
        _accept_block_in_progress = false;
        return result;
    } catch (...) {
        _accept_block_in_progress = false;
        throw;
    }
}

void dlt_p2p_node::on_dlt_block_range_reply(peer_id peer, const dlt_block_range_reply_message& reply) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;

    if (reply.blocks.empty()) {
        record_packet_result(peer, true);
        return;
    }

    // Validate first block's prev_hash link
    const auto& first = reply.blocks.front();
    if (first.previous != _delegate->get_head_block_id()) {
        wlog(DLT_LOG_RED "Block #${n} from ${ep} does NOT link to our head — possible fork" DLT_LOG_RESET,
             ("n", first.block_num())("ep", it->second.endpoint));
        // Still process — fork_db will handle it
    }

    bool any_block_applied = false;
    bool any_fork_db_only = false;
    auto& state = it->second;

    ilog(DLT_LOG_GREEN "Received block range #${first}-#${last} (${count} blocks) from ${ep}" DLT_LOG_RESET,
         ("first", reply.blocks.front().block_num())
         ("last", reply.blocks.back().block_num())("count", reply.blocks.size())
         ("ep", state.endpoint));

    // P37 fix: Update peer head from the range's last block.
    // The peer can serve blocks up to at least this number.
    uint32_t range_last = reply.blocks.back().block_num();
    if (range_last > state.peer_head_num) {
        state.peer_head_num = range_last;
    }
    if (range_last > _highest_seen_block_num) {
        _highest_seen_block_num = range_last;
    }

    state.pending_block_batch_time = fc::time_point::now();

    // If this range starts at head+1 but expected_next_block was advanced
    // ahead by single-block broadcasts from the same peer, reset tracking so
    // we don't generate spurious "stale tracking" messages for every block in
    // the batch.  Single-block broadcasts can race with range replies and push
    // expected_next_block to a value that doesn't match the range's start.
    if (!reply.blocks.empty() && state.expected_next_block != 0) {
        uint32_t first_num = reply.blocks.front().block_num();
        uint32_t head_at_start = _delegate->get_head_block_num();
        if (first_num == head_at_start + 1 && first_num != state.expected_next_block) {
            state.expected_next_block = 0;
        }
    }

    for (const auto& block : reply.blocks) {
        if (_block_processing_paused) break;

        // Out-of-order check: duplicate blocks from another peer are fine
        if (state.expected_next_block != 0 && block.block_num() != state.expected_next_block) {
            if (block.block_num() < state.expected_next_block && _delegate->is_block_known(block.id())) {
                dlog("Skipping duplicate block #${n} in range from ${ep} (already applied)",
                     ("n", block.block_num())("ep", state.endpoint));
                continue;
            }
            // Out-of-order check: when multiple peers send us the same blocks,
            // one peer's reply may arrive after we already advanced past that block.
            // After fix 8.1+8.2, expected_next_block is advanced by on_block_applied()
            // for ALL block sources, so stale tracking should be rare.  When it still
            // happens (narrow race), demote to debug if the block links directly to
            // our head (block_num == head + 1) — it's not a real gap or fork issue.
            uint32_t head_num = _delegate->get_head_block_num();
            if (block.block_num() <= head_num) {
                dlog(DLT_LOG_DGRAY "Block #${n} from ${ep} at or behind head #${h} (expected #${e}) — stale tracking" DLT_LOG_RESET,
                     ("n", block.block_num())("ep", state.endpoint)("h", head_num)("e", state.expected_next_block));
            } else if (block.block_num() == head_num + 1) {
                dlog(DLT_LOG_DGRAY "Block #${n} from ${ep} matches head+1 but expected #${e} — stale tracking" DLT_LOG_RESET,
                     ("n", block.block_num())("ep", state.endpoint)("e", state.expected_next_block));
            } else {
                wlog(DLT_LOG_RED "Block #${n} from ${ep} out of order (expected #${e}, head=#${h})" DLT_LOG_RESET,
                     ("n", block.block_num())("ep", state.endpoint)("e", state.expected_next_block)("h", head_num));
            }
            // Fall through — fork_db or push_block will handle it
        }

        dlt_block_accept_result result;
        try {
            result = call_accept_block(block, /*sync_mode=*/(_node_status == DLT_NODE_STATUS_SYNC));
        } catch (const graphene::network::deferred_resize_exception&) {
            // Transient local out-of-memory: stop processing this range,
            // don't punish the peer, and trigger resync after resize.
            wlog("Deferred resize on block #${n} from ${ep}, stopping range processing",
                 ("n", block.block_num())("ep", state.endpoint));
            record_packet_result(peer, true);
            break;
        }

        if (result == dlt_block_accept_result::ACCEPTED) {
            any_block_applied = true;
            _last_block_received_time = fc::time_point::now();
            _last_network_block_time = fc::time_point::now();

            ilog(DLT_LOG_BWHITE "Got block #${n} with ${tx} transaction(s) validated by " DLT_LOG_YELLOW "${w}" DLT_LOG_BWHITE " [${ep}]" DLT_LOG_RESET,
                 ("n", block.block_num())("tx", block.transactions.size())
                 ("w", block.validator)("ep", state.endpoint));

            on_block_applied(block, /*caused_fork_switch=*/false);

            // P25 fix: If this peer has exchange_enabled=false but its block was
            // accepted, it must be on our chain — enable exchange so we receive
            // future blocks from it.
            if (!state.exchange_enabled) {
                ilog(DLT_LOG_GREEN "Enabling exchange for peer ${ep} after accepting its block #${n}" DLT_LOG_RESET,
                     ("ep", state.endpoint)("n", block.block_num()));
                state.exchange_enabled = true;
                state.fork_alignment = true;
            }
        } else if (result == dlt_block_accept_result::ALREADY_KNOWN) {
            dlog(DLT_LOG_DGRAY "Block #${n} from ${ep} is already on our chain (duplicate)" DLT_LOG_RESET,
                 ("n", block.block_num())("ep", state.endpoint));
        } else if (result == dlt_block_accept_result::FORK_DB_ONLY) {
            any_fork_db_only = true;
            dlog("Stored block #${n} in fork_db (not yet applied) from ${ep}",
                 ("n", block.block_num())("ep", state.endpoint));
        } else if (result == dlt_block_accept_result::DEAD_FORK) {
            // Peer sent a block from a dead fork (parent not in fork_db,
            // block at or below our head).  This peer is on a competing
            // fork that diverged before our fork_db window — soft-ban it
            // immediately and stop processing blocks from it.
            wlog(DLT_LOG_RED "Peer ${ep} sent dead-fork block #${n} (parent not in fork_db, head=${h}) — soft-banning" DLT_LOG_RESET,
                 ("ep", state.endpoint)("n", block.block_num())("h", _delegate->get_head_block_num()));
            soft_ban_peer(peer, "dead-fork block #" + std::to_string(block.block_num()));
            break;
        } else {
            wlog(DLT_LOG_RED "Rejected block #${n} from ${ep}" DLT_LOG_RESET,
                 ("n", block.block_num())("ep", state.endpoint));
            continue; // skip updating expected_next_block for rejected blocks
        }

        state.expected_next_block = std::max(state.expected_next_block, block.block_num() + 1);
    }
    state.pending_block_batch_time = fc::time_point();

    // If the peer was banned during the loop (e.g. dead-fork block),
    // skip all post-loop state updates and block requests.
    if (state.lifecycle_state == DLT_PEER_LIFECYCLE_BANNED) {
        return;
    }

    // fork_db-only means the peer sent valid blocks we can't apply yet
    // (competing fork accumulating, or large-gap sync from LIB) — not spam.
    record_packet_result(peer, any_block_applied || any_fork_db_only);

    // If SYNC, continue fetching or transition to FORWARD.
    // When blocks were applied, follow the normal transition logic.
    // When blocks only went to fork_db (competing fork case), continue
    // fetching so fork_db accumulates the full competing chain and can
    // evaluate a fork switch.  A range full of dead-fork rejects
    // (no applied, no fork_db) should NOT end sync mode.
    if (_node_status == DLT_NODE_STATUS_SYNC) {
        if (any_block_applied) {
            state.fork_only_batch_count = 0;
            if (reply.is_last) {
                transition_to_forward();
            } else if (reply.last_block_next_available > 0) {
                // Continue fetching
                request_blocks_from_peer(peer);
            }
        } else if (any_fork_db_only && reply.last_block_next_available > 0) {
            // Blocks went to fork_db but weren't applied — competing fork.
            // Continue fetching so fork_db accumulates the full competing
            // chain and can evaluate a fork switch.
            // P77 fix: if after FORK_ONLY_BATCH_LIMIT consecutive batches
            // nothing is applied, this peer is on a permanently diverging
            // fork — soft-ban to break the infinite fetch loop.
            state.fork_only_batch_count++;
            if (state.fork_only_batch_count >= dlt_peer_state::FORK_ONLY_BATCH_LIMIT) {
                wlog(DLT_LOG_RED "Peer ${ep}: ${n} consecutive fork-only batches with no blocks applied — soft-banning" DLT_LOG_RESET,
                     ("ep", state.endpoint)("n", state.fork_only_batch_count));
                soft_ban_peer(peer, "fork-only spam: " + std::to_string(state.fork_only_batch_count) + " batches with no progress");
            } else {
                ilog(DLT_LOG_ORANGE "Range stored in fork_db only (competing fork?), continuing fetch from #${n}" DLT_LOG_RESET,
                     ("n", reply.last_block_next_available));
                request_blocks_from_peer(peer);
            }
        }
    }
}

void dlt_p2p_node::on_dlt_get_block(peer_id peer, const dlt_get_block_message& req) {
    auto block = _delegate->read_block_by_num(req.block_num);
    if (block.valid()) {
        dlt_block_reply_message reply;
        reply.block = std::move(*block);
        uint32_t our_latest = _delegate->get_dlt_latest_block();
        reply.next_available = (block->block_num() < our_latest) ? block->block_num() + 1 : 0;
        reply.is_last = (block->block_num() >= our_latest);
        send_message(peer, message(reply));
    } else {
        send_message(peer, message(dlt_not_available_message{req.block_num}));
    }
    record_packet_result(peer, true);
}

void dlt_p2p_node::on_dlt_block_reply(peer_id peer, const dlt_block_reply_message& reply) {
    if (_block_processing_paused) return;

    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;
    auto& state = it->second;

    uint32_t block_num = reply.block.block_num();

    // P37 fix: Update peer's head from the block it sent us.
    // If a peer can send us block #N, its chain head must be >= N.
    // Without this, peer_head_num stays stale (only set from hello
    // exchange), which breaks gap fill and fallbehind detection.
    if (block_num > state.peer_head_num) {
        state.peer_head_num = block_num;
        dlog(DLT_LOG_DGRAY "Updated peer ${ep} head to #${n} from received block" DLT_LOG_RESET, ("ep", state.endpoint)("n", block_num));
    }
    if (block_num > _highest_seen_block_num) {
        _highest_seen_block_num = block_num;
    }

    // Out-of-order check: when multiple peers send us the same blocks,
    // one peer's reply may arrive after we already advanced past that block.
    if (state.expected_next_block != 0 && block_num != state.expected_next_block) {
        // Block we already have (duplicate from another peer): don't punish
        if (block_num < state.expected_next_block && _delegate->is_block_known(reply.block.id())) {
            dlog(DLT_LOG_DGRAY "Ignoring duplicate block #${n} from ${ep} (already applied, head=${h})" DLT_LOG_RESET,
                 ("n", block_num)("ep", state.endpoint)("h", _delegate->get_head_block_num()));
            record_packet_result(peer, true);
            return;
        }

        // Out-of-order log: after fix 8.1+8.2, expected_next_block is advanced
        // by on_block_applied() for ALL block sources.  When still stale (narrow
        // race), demote to debug if block_num <= head or block_num == head + 1 —
        // the block is not truly out of order, just the per-peer tracker lagging.
        // Only warn at wlog level for genuine gaps (block_num > head + 1).
        uint32_t head_num = _delegate->get_head_block_num();
        if (block_num <= head_num) {
            dlog(DLT_LOG_DGRAY "Block #${n} from ${ep} at or behind head #${h} (expected #${e}) — stale tracking" DLT_LOG_RESET,
                 ("n", block_num)("ep", state.endpoint)("h", head_num)("e", state.expected_next_block));
        } else if (block_num == head_num + 1) {
            dlog(DLT_LOG_DGRAY "Block #${n} from ${ep} matches head+1 but expected #${e} — stale tracking" DLT_LOG_RESET,
                 ("n", block_num)("ep", state.endpoint)("e", state.expected_next_block));
        } else {
            wlog(DLT_LOG_RED "Block #${n} from ${ep} out of order (expected #${e}, head=#${h})" DLT_LOG_RESET,
                 ("n", block_num)("ep", state.endpoint)("e", state.expected_next_block)("h", head_num));
        }
        // P36: An out-of-order block indicates a gap. Trigger gap fill
        // to quickly request the missing blocks from exchange-enabled peers
        // instead of oscillating SYNC↔FORWARD.
        // P40 fix: Only request gap fill when there's a REAL gap (at least
        // one block missing between our head and the received block).
        // When block_num == head + 1, there's no gap — the block links
        // directly to our head. The "out of order" is just stale
        // expected_next_block tracking from receiving blocks via other peers.
        if (block_num > _delegate->get_head_block_num() + 1) {
            request_gap_fill();
        }

        // Detect competing fork parent at our head height.
        // When a received block's previous hash points to a block at the
        // same height as our head but with a DIFFERENT ID, the peer is on
        // a competing fork. We must request that competing parent block
        // so fork_db can evaluate both forks and potentially switch.
        //
        // Example: our head is social's #79693402, but the received
        // #79693403 links to committee's #79693402 (different block).
        // Without this fix, we'd only request gap fill by block number
        // which returns the same committee block we already can't link —
        // leaving the competing fork unevaluated.
        if (block_num > _delegate->get_head_block_num()) {
            uint32_t parent_num = protocol::block_header::num_from_id(reply.block.previous);
            block_id_type our_head_id = _delegate->get_head_block_id();
            if (parent_num == _delegate->get_head_block_num() &&
                reply.block.previous != our_head_id &&
                reply.block.previous != block_id_type()) {
                wlog(DLT_LOG_ORANGE "Competing fork parent detected: block #${n} links to "
                     "a different #${pn} than our head (our=${ours}, theirs=${theirs}). "
                     "Requesting competing block from ${ep}" DLT_LOG_RESET,
                     ("n", block_num)("pn", parent_num)
                     ("ours", our_head_id)("theirs", reply.block.previous)("ep", state.endpoint));
                dlt_get_block_message req;
                req.block_num = parent_num;
                send_message(peer, message(req));
            }
        }

        // Fall through and try to apply — fork_db or push_block will handle it
    }

    dlt_block_accept_result result;
    try {
        result = call_accept_block(reply.block, false);
    } catch (const graphene::network::deferred_resize_exception&) {
        // Transient local out-of-memory: not the peer's fault.
        // The missed block will be re-fetched after the deferred resize
        // completes.  Don't punish the peer, don't update sync state.
        wlog("Deferred resize on block #${n} from ${ep}, will retry after resize",
             ("n", block_num)("ep", state.endpoint));
        record_packet_result(peer, true);
        return;
    }

    if (result == dlt_block_accept_result::REJECTED) {
        wlog(DLT_LOG_RED "Rejected block #${n} from ${ep}" DLT_LOG_RESET,
             ("n", block_num)("ep", state.endpoint));
        // Track rejected blocks to prevent gap fill infinite retry loop
        if (block_num == _gap_rejected_block_num) {
            _gap_rejected_count++;
        } else {
            _gap_rejected_block_num = block_num;
            _gap_rejected_count = 1;
        }
        if (_gap_rejected_count >= GAP_REJECT_MAX_RETRIES) {
            elog(DLT_LOG_RED "Block #${n} rejected ${c} times from peers — blacklisting gap fill for ${t}s "
                 "(validator key likely invalid on this fork)" DLT_LOG_RESET,
                 ("n", block_num)("c", _gap_rejected_count)("t", GAP_REJECT_BLACKLIST_SEC));
            _gap_rejected_blacklist_until = fc::time_point::now() + fc::seconds(GAP_REJECT_BLACKLIST_SEC);
            _gap_rejected_count = 0;
            _gap_rejected_block_num = 0;
        }
        record_packet_result(peer, false);
        return;
    }

    if (result == dlt_block_accept_result::DEAD_FORK) {
        // Peer sent a block from a dead fork that diverged before our
        // fork_db window — soft-ban immediately.
        wlog(DLT_LOG_RED "Peer ${ep} sent dead-fork block #${n} (parent not in fork_db, head=${h}) — soft-banning" DLT_LOG_RESET,
             ("ep", state.endpoint)("n", block_num)("h", _delegate->get_head_block_num()));
        soft_ban_peer(peer, "dead-fork block #" + std::to_string(block_num));
        return;
    }

    if (result == dlt_block_accept_result::ACCEPTED) {
        ilog(DLT_LOG_BWHITE "Got block #${n} with ${tx} transaction(s) validated by " DLT_LOG_YELLOW "${w}" DLT_LOG_BWHITE " [${ep}]" DLT_LOG_RESET,
             ("n", reply.block.block_num())("tx", reply.block.transactions.size())
             ("w", reply.block.validator)("ep", state.endpoint));

        _last_network_block_time = fc::time_point::now();
        _last_block_received_time = fc::time_point::now();

        on_block_applied(reply.block, /*caused_fork_switch=*/false);

        // If this peer has exchange_enabled=false but its block was
        // accepted, it must be on our chain — enable exchange.
        if (!state.exchange_enabled) {
            ilog(DLT_LOG_GREEN "Enabling exchange for peer ${ep} after accepting its block #${n}" DLT_LOG_RESET,
                 ("ep", state.endpoint)("n", reply.block.block_num()));
            state.exchange_enabled = true;
            state.fork_alignment = true;
        }

        // Record that the sender has this block (for echo suppression).
        // If we later receive this same block from another peer, we won't
        // retransmit it back to the original sender.
        state.record_known_block(reply.block.id());

        // Retransmit to our-fork peers
        dlog(DLT_LOG_DGRAY "Retransmitting block #${n} by ${w} to fork peers (excluding ${ep})" DLT_LOG_RESET,
             ("n", reply.block.block_num())("w", reply.block.validator)("ep", state.endpoint));
        send_to_all_our_fork_peers(message(dlt_block_reply_message(reply)), peer, reply.block.id());

        // P26 fix: Check if we've caught up to all peers via single-block replies
        check_sync_catchup();
    } else if (result == dlt_block_accept_result::ALREADY_KNOWN) {
        dlog(DLT_LOG_DGRAY "Block #${n} from ${ep} is already on our chain (duplicate)" DLT_LOG_RESET,
             ("n", block_num)("ep", state.endpoint));
    } else {
        // FORK_DB_ONLY: block stored in fork_db but not applied to chain.
        // Do NOT call on_block_applied (which would corrupt mempool),
        // do NOT retransmit (block is not on our main chain).
        dlog(DLT_LOG_ORANGE "Stored block #${n} by validator ${w} in fork_db (not applied, head=${h}) from ${ep}" DLT_LOG_RESET,
             ("n", block_num)("w", reply.block.validator)("h", _delegate->get_head_block_num())("ep", state.endpoint));
    }

    // Update peer's expected_next_block regardless of outcome so the
    // sync state stays consistent with this peer's view.
    state.expected_next_block = std::max(state.expected_next_block, block_num + 1);

    record_packet_result(peer, true);

    // Single-block fallback continuation: when range_fallback_mode is
    // set (range deserialization failed), keep requesting blocks one
    // at a time instead of switching to range requests.
    if (state.range_fallback_mode && _node_status == DLT_NODE_STATUS_SYNC) {
        if (reply.next_available > 0) {
            dlt_get_block_message next_req;
            next_req.block_num = reply.next_available;
            next_req.prev_block_id = _delegate->get_head_block_id();
            send_message(peer, message(next_req));
        } else if (reply.is_last) {
            // Caught up to this peer — use check_sync_catchup() instead of
            // transition_to_forward() directly so we don't enter FORWARD when
            // no block was actually applied (e.g. all ALREADY_KNOWN replies).
            state.range_fallback_mode = false;
            check_sync_catchup();
        }
    }
}

void dlt_p2p_node::on_dlt_not_available(peer_id peer, const dlt_not_available_message& msg) {
    auto it = _peer_states.find(peer);
    auto ep = (it != _peer_states.end()) ? std::string(it->second.endpoint) : std::to_string(peer);
    ilog(DLT_LOG_ORANGE "Peer ${ep} doesn't have block #${n}" DLT_LOG_RESET,
         ("ep", ep)("n", msg.block_num));
    record_packet_result(peer, true);
}

void dlt_p2p_node::on_dlt_fork_status(peer_id peer, const dlt_fork_status_message& msg) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;
    auto& state = it->second;

    // Track what the peer's node_status was before this update, so we can
    // detect a SYNC→FORWARD transition and react to it.
    uint8_t prev_node_status = state.peer_node_status;

    state.peer_fork_status = msg.fork_status;
    state.peer_head_id = msg.head_block_id;
    state.peer_head_num = msg.head_block_num;
    state.peer_lib_id = msg.lib_block_id;
    state.peer_lib_num = msg.lib_block_num;
    state.peer_dlt_earliest = msg.dlt_earliest_block;
    state.peer_dlt_latest = msg.dlt_latest_block;
    state.peer_node_status = msg.node_status;

    // When a peer transitions from SYNC to FORWARD (or is already FORWARD)
    // and we haven't enabled exchange with it yet, re-evaluate. The peer
    // just finished syncing — its head block may now be within our known
    // chain, meaning we should enable block/transaction exchange.
    if (!state.exchange_enabled &&
        msg.node_status == DLT_NODE_STATUS_FORWARD &&
        prev_node_status == DLT_NODE_STATUS_SYNC &&
        state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE &&
        msg.head_block_num > 0 && _delegate) {
        if (_delegate->is_block_known(msg.head_block_id)) {
            ilog(DLT_LOG_GREEN "Peer ${ep} transitioned to FORWARD, re-enabling exchange (head #${hn} now recognized)" DLT_LOG_RESET,
                 ("ep", state.endpoint)("hn", msg.head_block_num));
            state.exchange_enabled = true;
            state.fork_alignment = true;
        }
    }

    // Also update _highest_seen_block_num for gap detection (P37)
    if (msg.head_block_num > _highest_seen_block_num) {
        _highest_seen_block_num = msg.head_block_num;
    }

    record_packet_result(peer, true);
}

// ── Peer exchange handlers ───────────────────────────────────────────

void dlt_p2p_node::on_dlt_peer_exchange_request(peer_id peer, const dlt_peer_exchange_request&) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;
    auto& state = it->second;

    if (_isolated_peers) {
        send_message(peer, message(dlt_peer_exchange_reply{}));
        return;
    }

    // Rate-limit check (sliding window: 3 requests per 5 min)
    if (state.is_peer_exchange_rate_limited()) {
        send_message(peer, message(dlt_peer_exchange_rate_limited{state.peer_exchange_wait_seconds()}));
        return;
    }

    state.record_peer_exchange_request();

    // Collect "our fork" peers (exchange_enabled, active, min uptime)
    dlt_peer_exchange_reply reply;
    auto now = fc::time_point::now();
    for (auto& _peer_item : _peer_states) {
            auto& id = _peer_item.first;
            auto& s = _peer_item.second;
        if (id == peer) continue;
        if (!s.exchange_enabled) continue;
        if (s.lifecycle_state != DLT_PEER_LIFECYCLE_ACTIVE) continue;
        if (s.is_incoming) continue;  // Don't share ephemeral ports from incoming connections
        if (s.connected_since == fc::time_point()) continue;
        auto uptime = (now - s.connected_since).count() / 1000000;
        if (uptime < _peer_exchange_min_uptime_sec) continue;

        // Subnet diversity check
        if (count_peers_in_subnet(s.endpoint.get_address()) > _peer_exchange_max_per_subnet) continue;

        dlt_peer_endpoint_info info;
        info.endpoint = s.endpoint;
        info.node_id = s.node_id;
        reply.peers.push_back(info);

        if (reply.peers.size() >= _peer_exchange_max_per_reply) break;
    }

    send_message(peer, message(reply));
    record_packet_result(peer, true);
}

void dlt_p2p_node::on_dlt_peer_exchange_reply(peer_id peer, const dlt_peer_exchange_reply& reply) {
    if (_isolated_peers) return;
    for (auto& info : reply.peers) {
        // Filter out self
        if (info.node_id == _node_id) continue;

        // Filter out already connected/known
        dlt_known_peer kp;
        kp.endpoint = info.endpoint;
        kp.node_id = info.node_id;

        if (_known_peers.count(kp)) continue;

        // Subnet diversity check
        if (count_peers_in_subnet(info.endpoint.get_address()) >= _peer_exchange_max_per_subnet) continue;

        _known_peers.insert(kp);

        if (_connections.size() < _max_connections) {
            connect_to_peer(info.endpoint);
        }
    }
    record_packet_result(peer, true);
}

void dlt_p2p_node::on_dlt_peer_exchange_rate_limited(peer_id peer, const dlt_peer_exchange_rate_limited& msg) {
    auto it = _peer_states.find(peer);
    auto ep = (it != _peer_states.end()) ? std::string(it->second.endpoint) : std::to_string(peer);
    ilog(DLT_LOG_DGRAY "Peer ${ep} rate-limited our exchange request, wait ${w}s" DLT_LOG_RESET,
         ("ep", ep)("w", msg.wait_seconds));

    // Record the rate-limit locally so periodic_peer_exchange() stops
    // sending requests to this peer until the window expires.
    if (it != _peer_states.end()) {
        it->second.peer_exchange_request_count = dlt_peer_state::PEER_EXCHANGE_MAX_REQUESTS;
        it->second.peer_exchange_window_start = fc::time_point::now();
    }

    record_packet_result(peer, true);
}

// ── Transaction handler ──────────────────────────────────────────────

void dlt_p2p_node::on_dlt_transaction(peer_id peer, const dlt_transaction_message& msg) {
    auto it = _peer_states.find(peer);
    auto ep = (it != _peer_states.end()) ? std::string(it->second.endpoint) : std::to_string(peer);

    bool accepted = add_to_mempool(msg.trx, /*from_peer=*/true, peer);
    if (!accepted) {
        // Dedup or validation failure — already in mempool or invalid TaPoS/expiry/size
        dlog(DLT_LOG_DGRAY "Transaction ${id} from peer ${ep} rejected by mempool" DLT_LOG_RESET,
             ("id", msg.trx.id())("ep", ep));
        return;
    }

    dlog(DLT_LOG_DGRAY "Got transaction ${id} from peer ${ep}" DLT_LOG_RESET,
         ("id", msg.trx.id())("ep", ep));

    // Push to chain database so the local witness can include it in a block.
    // Without this, P2P-received transactions only exist in the P2P mempool
    // and never enter the chain's pending transaction queue.
    if (_delegate) {
        bool chain_accepted = _delegate->accept_transaction(msg.trx);
        if (!chain_accepted) {
            dlog(DLT_LOG_DGRAY "Transaction ${id} from peer ${ep} rejected by chain (already known or invalid)" DLT_LOG_RESET,
                 ("id", msg.trx.id())("ep", ep));
        }
    }
}

void dlt_p2p_node::on_dlt_soft_ban(peer_id peer, const dlt_soft_ban_message& msg) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;

    ilog(DLT_LOG_ORANGE "Peer ${ep} soft-banned us for ${d}s (reason: ${r})" DLT_LOG_RESET,
         ("ep", it->second.endpoint)("d", msg.ban_duration_sec)("r", msg.reason));

    // Enter BANNED state with the duration specified by the remote peer
    it->second.lifecycle_state = DLT_PEER_LIFECYCLE_BANNED;
    it->second.state_entered_time = fc::time_point::now();
    it->second.ban_reason = "remote: " + msg.reason;
    it->second.ban_duration_sec = msg.ban_duration_sec;
    it->second.reconnect_backoff_sec = msg.ban_duration_sec;
    it->second.next_reconnect_attempt = fc::time_point::now() + fc::seconds(msg.ban_duration_sec);

    // Close connection — stop sending data to the peer that banned us
    auto conn_it = _connections.find(peer);
    if (conn_it != _connections.end()) {
        try { if (conn_it->second) conn_it->second->close(); } catch (...) {}
        _connections.erase(conn_it);
    }
}

// ── Gap fill handlers (exchange-only) ─────────────────────────────────

void dlt_p2p_node::on_dlt_gap_fill_request(peer_id peer, const dlt_gap_fill_request& req) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;

    // P39 fix: Accept gap fill requests from any active peer, not just
    // exchange-enabled.  Gap fill is a lightweight read from block_log
    // (same as SYNC range requests), so there's no security reason to
    // restrict it.  This allows newly-FORWARD peers (whose exchange
    // status may not be set yet) to fill gaps immediately.

    // Validate request: limit number of blocks requested
    if (req.block_nums.empty() || req.block_nums.size() > GAP_FILL_MAX_BLOCKS) {
        wlog("Gap fill request from ${ep} has invalid block count (${n})",
             ("ep", it->second.endpoint)("n", req.block_nums.size()));
        record_packet_result(peer, false);
        return;
    }

    // Verify requested blocks are within reasonable range of our head
    uint32_t our_head = _delegate->get_head_block_num();
    uint32_t our_earliest = _delegate->get_dlt_earliest_block();
    dlt_gap_fill_reply reply;

    for (uint32_t num : req.block_nums) {
        // Only serve blocks within our DLT log range and not too far from head
        if (num < our_earliest || num > our_head + GAP_FILL_MAX_BLOCKS) continue;
        auto block = _delegate->read_block_by_num(num);
        if (block.valid()) {
            reply.blocks.push_back(std::move(*block));
        }
    }

    if (!reply.blocks.empty()) {
        ilog(DLT_LOG_GREEN "Serving gap fill: ${n} blocks to ${ep}" DLT_LOG_RESET,
             ("n", reply.blocks.size())("ep", it->second.endpoint));
    } else {
        ilog(DLT_LOG_ORANGE "Gap fill request from ${ep}: no blocks available" DLT_LOG_RESET,
             ("ep", it->second.endpoint));
    }
    // Always send the reply, even when empty: the requester clears
    // _gap_fill_in_progress on receipt and can switch peer/strategy
    // immediately, instead of waiting GAP_FILL_TIMEOUT_SEC for a stuck
    // request to time out and then retrying the same peer in a loop.
    send_message(peer, message(reply));

    record_packet_result(peer, true);
}

void dlt_p2p_node::on_dlt_gap_fill_reply(peer_id peer, const dlt_gap_fill_reply& reply) {
    if (_block_processing_paused) return;

    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;

    _gap_fill_in_progress = false;
    _gap_fill_start_time = fc::time_point();

    if (reply.blocks.empty()) {
        ilog(DLT_LOG_ORANGE "Gap fill reply from ${ep}: no blocks" DLT_LOG_RESET,
             ("ep", it->second.endpoint));
        record_packet_result(peer, true);
        return;
    }

    ilog(DLT_LOG_GREEN "Gap fill: received ${n} blocks from ${ep}" DLT_LOG_RESET,
         ("n", reply.blocks.size())("ep", it->second.endpoint));

    // Apply received blocks via normal accept_block flow
    bool any_applied = false;
    for (const auto& block : reply.blocks) {
        if (_block_processing_paused) break;

        dlt_block_accept_result result;
        try {
            result = call_accept_block(block, /*sync_mode=*/false);
        } catch (const graphene::network::deferred_resize_exception&) {
            wlog("Deferred resize during gap fill, stopping");
            break;
        }

        if (result == dlt_block_accept_result::ACCEPTED) {
            any_applied = true;
            _last_block_received_time = fc::time_point::now();
            _last_network_block_time = fc::time_point::now();

            ilog(DLT_LOG_BWHITE "Got block #${n} with ${tx} transaction(s) validated by " DLT_LOG_YELLOW "${w}" DLT_LOG_BWHITE " [${ep}] (gap fill)" DLT_LOG_RESET,
                 ("n", block.block_num())("tx", block.transactions.size())
                 ("w", block.validator)("ep", it->second.endpoint));

            on_block_applied(block, /*caused_fork_switch=*/false);

            // P25 fix: enable exchange after accepting block from non-exchange peer
            if (!it->second.exchange_enabled) {
                ilog(DLT_LOG_GREEN "Enabling exchange for peer ${ep} after gap fill block #${n}" DLT_LOG_RESET,
                     ("ep", it->second.endpoint)("n", block.block_num()));
                it->second.exchange_enabled = true;
                it->second.fork_alignment = true;
            }
        } else if (result == dlt_block_accept_result::ALREADY_KNOWN) {
            dlog(DLT_LOG_DGRAY "Gap fill block #${n} already on chain" DLT_LOG_RESET,
                 ("n", block.block_num()));
        } else if (result == dlt_block_accept_result::FORK_DB_ONLY) {
            dlog("Gap fill block #${n} stored in fork_db", ("n", block.block_num()));
        } else if (result == dlt_block_accept_result::DEAD_FORK) {
            // P69 fix: If the peer has a longer chain our locally produced blocks
            // (e.g. emergency witness blocks) are on the losing fork — the dead-fork
            // block is the peer's legitimate version of that slot.  Re-sync from LIB
            // so fork_db can reconnect the winning chain.  Only ban when the peer's
            // chain is shorter-or-equal (the genuine misbehavior case).
            uint32_t our_head_now   = _delegate->get_head_block_num();
            uint32_t peer_latest    = std::max(it->second.peer_dlt_latest, it->second.peer_head_num);
            if (peer_latest > our_head_now) {
                wlog(DLT_LOG_ORANGE "Gap fill: dead-fork block #${n} from peer ${ep} (peer=#${p} > our head #${h})"
                     " — our fork is losing, re-syncing from LIB instead of banning" DLT_LOG_RESET,
                     ("n", block.block_num())("ep", it->second.endpoint)("p", peer_latest)("h", our_head_now));
                transition_to_sync();
                request_blocks_from_peer(peer);
            } else {
                wlog(DLT_LOG_RED "Gap fill: peer ${ep} sent dead-fork block #${n}" DLT_LOG_RESET,
                     ("ep", it->second.endpoint)("n", block.block_num()));
                soft_ban_peer(peer, "dead-fork block in gap fill");
            }
            break;
        } else {
            wlog(DLT_LOG_RED "Gap fill: rejected block #${n} from ${ep}" DLT_LOG_RESET,
                 ("n", block.block_num())("ep", it->second.endpoint));
            // Track rejected blocks to prevent infinite retry loop
            if (block.block_num() == _gap_rejected_block_num) {
                _gap_rejected_count++;
            } else {
                _gap_rejected_block_num = block.block_num();
                _gap_rejected_count = 1;
            }
            if (_gap_rejected_count >= GAP_REJECT_MAX_RETRIES) {
                elog(DLT_LOG_RED "Gap fill: block #${n} rejected ${c} times — blacklisting for ${t}s "
                     "(validator key likely invalid on this fork)" DLT_LOG_RESET,
                     ("n", block.block_num())("c", _gap_rejected_count)("t", GAP_REJECT_BLACKLIST_SEC));
                _gap_rejected_blacklist_until = fc::time_point::now() + fc::seconds(GAP_REJECT_BLACKLIST_SEC);
                _gap_rejected_count = 0;
                _gap_rejected_block_num = 0;
            }
        }
    }

    // A non-empty gap fill reply means the peer served us faithfully.
    // DEAD_FORK already calls soft_ban_peer() directly (above).
    // REJECTED blocks are tracked by _gap_rejected_count / blacklist.
    // Penalising here for FORK_DB_ONLY/ALREADY_KNOWN is a false positive
    // that causes the slave to ban a good peer after 10 rounds where all
    // blocks land in fork_db or are already known (p57 mutual-ban fix).
    record_packet_result(peer, true);
}

void dlt_p2p_node::request_gap_fill() {
    if (!_delegate) return;

    // P37 fix: timeout stale _gap_fill_in_progress.  If the peer
    // we asked disconnected or is slow, the flag gets stuck and
    // blocks all future gap fill attempts forever.
    if (_gap_fill_in_progress) {
        if (_gap_fill_start_time != fc::time_point()) {
            auto elapsed = fc::time_point::now() - _gap_fill_start_time;
            if (elapsed.count() > GAP_FILL_TIMEOUT_SEC * 1000000) {
                wlog("Gap fill timed out after ${t}s, resetting", ("t", GAP_FILL_TIMEOUT_SEC));
                _gap_fill_in_progress = false;
            } else {
                return;  // still waiting for reply
            }
        } else {
            return;  // in progress but no start time (shouldn't happen)
        }
    }

    // Cooldown: don't spam gap fill requests
    if (_last_gap_fill_time != fc::time_point()) {
        auto elapsed = fc::time_point::now() - _last_gap_fill_time;
        if (elapsed.count() < GAP_FILL_COOLDOWN_SEC * 1000000) return;
    }

    // Blacklist: if a block was permanently rejected (e.g. null witness key),
    // don't keep requesting it in a tight loop.
    if (_gap_rejected_blacklist_until != fc::time_point()) {
        if (fc::time_point::now() < _gap_rejected_blacklist_until) {
            return;
        }
        _gap_rejected_blacklist_until = fc::time_point();  // blacklist expired
    }

    uint32_t our_head = _delegate->get_head_block_num();
    if (our_head == 0) return;

    // Gap fill works in both FORWARD and SYNC modes.
    // In SYNC mode, when request_blocks_from_peer() can't bridge a gap
    // (blocks below the syncing peer's DLT range), gap fill provides an
    // alternative path by asking exchange-enabled peers for specific blocks.

    // P37 fix: Use _highest_seen_block_num as the upper bound for
    // gap detection, NOT just peer_head_num.  When broadcast blocks
    // arrive from peers whose peer_head_num is stale (not updated
    // since the last hello exchange), the old code saw no peer with
    // head > our_head and silently returned — the gap grew forever.
    //
    // _highest_seen_block_num is updated whenever we receive any
    // block with a higher number, so it reflects reality even when
    // peer_head_num hasn't caught up.
    //
    // P39 fix: Prefer exchange-enabled peers (they're confirmed on our
    // fork), but fall back to ANY active or syncing peer.  The serving side
    // validates the request independently — refusing if not exchange-enabled.
    // Requiring exchange_enabled on the requesting side was too strict:
    // right after SYNC→FORWARD transition, no peer may have exchange=true
    // yet, causing gap fill to silently fail and the gap to grow forever.
    uint32_t max_peer_head = our_head;
    peer_id best_peer = INVALID_PEER_ID;
    peer_id any_active_peer = INVALID_PEER_ID;  // P39 fallback
    for (const auto& _pi : _peer_states) {
        const auto& state = _pi.second;
        // Include SYNCING peers — in SYNC mode the best candidate
        // may be in SYNCING lifecycle (set by request_blocks_from_peer).
        if (state.lifecycle_state != DLT_PEER_LIFECYCLE_ACTIVE &&
            state.lifecycle_state != DLT_PEER_LIFECYCLE_SYNCING) continue;
        // Skip peers whose DLT log explicitly starts after the first block we
        // need (our_head + 1). They will always return an empty reply, wasting
        // the 15s gap-fill timeout each round.
        // peer_dlt_earliest == 0 means unknown — still worth trying (p57 fix).
        bool can_serve_gap = (state.peer_dlt_earliest == 0 ||
                              state.peer_dlt_earliest <= our_head + 1);
        // Track any active/syncing peer that can serve our gap as fallback
        if (can_serve_gap && any_active_peer == INVALID_PEER_ID &&
            state.peer_head_num > our_head) {
            any_active_peer = _pi.first;
        }
        if (!state.exchange_enabled) continue;
        if (can_serve_gap && state.peer_head_num > max_peer_head) {
            max_peer_head = state.peer_head_num;
            best_peer = _pi.first;
        }
    }

    // If no peer reports a higher head, use _highest_seen_block_num
    // as the gap ceiling.  This covers the case where we're receiving
    // broadcast blocks from peers with stale peer_head_num.
    uint32_t gap_ceiling = std::max(max_peer_head, _highest_seen_block_num);

    if (gap_ceiling <= our_head) return;  // truly no gap

    uint32_t gap = gap_ceiling - our_head;

    // For large gaps, request only the first GAP_FILL_MAX_BLOCKS blocks.
    // Subsequent chunks will be requested on the next periodic call
    // after this chunk completes or times out.
    //
    // Include our_head to detect competing fork at our tip (same logic as
    // P49 fix in request_blocks_from_peer). If our witness produced a block
    // at our_head that the network rejected, the peer's version is needed
    // so fork_db can link subsequent blocks. If it's the same as ours,
    // accept_block returns ALREADY_KNOWN with no side effects.
    uint32_t request_ceiling = std::min(gap_ceiling, our_head + GAP_FILL_MAX_BLOCKS - 1);

    // Build request for the missing blocks (capped to chunk size)
    dlt_gap_fill_request req;
    for (uint32_t n = our_head; n <= request_ceiling; ++n) {
        req.block_nums.push_back(n);
    }

    ilog(DLT_LOG_GREEN "Requesting gap fill for blocks ${s}-${e} (${n} blocks, total_gap=${g}) (highest_seen=${hs})" DLT_LOG_RESET,
         ("s", our_head)("e", request_ceiling)("n", req.block_nums.size())("g", gap)("hs", _highest_seen_block_num));

    _gap_fill_in_progress = true;
    _gap_fill_start_time = fc::time_point::now();
    _last_gap_fill_time = fc::time_point::now();

    // Send to the best exchange-enabled peer (the one with highest head)
    if (best_peer != INVALID_PEER_ID) {
        send_message(best_peer, message(req));
    } else if (any_active_peer != INVALID_PEER_ID) {
        // P39 fix: No exchange-enabled peer, but we have an active peer
        // with a higher head.  Try it — the serving side will validate
        // and refuse if not allowed.
        auto& peer_state = _peer_states[any_active_peer];
        ilog(DLT_LOG_ORANGE "Gap fill: no exchange-enabled peer, trying active peer ${ep} (peer_head=#${ph}, exchange=${ex})" DLT_LOG_RESET,
             ("ep", peer_state.endpoint)("ph", peer_state.peer_head_num)("ex", peer_state.exchange_enabled));
        send_message(any_active_peer, message(req));
    } else {
        // P39 fix: No peer at all with a higher head — gap fill
        // can't help.  Transition to SYNC immediately instead of
        // waiting for stagnation detection.
        wlog("Gap fill: no peer available — transitioning to SYNC");
        _gap_fill_in_progress = false;
        _gap_fill_start_time = fc::time_point();
        transition_to_sync();
        // Request blocks from all active peers
        for (const auto& _pi : _peer_states) {
            const auto& state = _pi.second;
            if (state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE ||
                state.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) {
                request_blocks_from_peer(_pi.first);
            }
        }
    }
}

// ── Broadcast methods ────────────────────────────────────────────────

void dlt_p2p_node::broadcast_block(const signed_block& block) {
    dlt_block_reply_message reply;
    reply.block = block;
    reply.next_available = 0;
    reply.is_last = true;
    send_to_all_our_fork_peers(message(reply), INVALID_PEER_ID, block.id());

    // Track our own block application: clean mempool of included
    // transactions, advance fork state, and update all peers'
    // expected_next_block so the next incoming block from any peer
    // is not falsely flagged as "out of order".
    on_block_applied(block, /*caused_fork_switch=*/false);
}

void dlt_p2p_node::broadcast_block_post_validation(
    const block_id_type& block_id,
    const std::string& witness_account,
    const signature_type& validator_signature) {
    // For now, send as fork_status message with block_id
    dlt_fork_status_message msg;
    msg.fork_status = _fork_status;
    msg.head_block_id = block_id;
    msg.head_block_num = graphene::protocol::block_header::num_from_id(block_id);
    if (_delegate) {
        msg.lib_block_id = _delegate->get_lib_block_id();
        msg.lib_block_num = _delegate->get_lib_block_num();
        msg.dlt_earliest_block = _delegate->get_dlt_earliest_block();
        msg.dlt_latest_block = _delegate->get_dlt_latest_block();
    }
    msg.node_status = _node_status;
    send_to_all_our_fork_peers(message(msg));
}

void dlt_p2p_node::broadcast_transaction(const signed_transaction& trx) {
    add_to_mempool(trx, /*from_peer=*/false, INVALID_PEER_ID);
    dlt_transaction_message msg;
    msg.trx = trx;
    dlog(DLT_LOG_DGRAY "Broadcasting transaction ${id} to fork peers" DLT_LOG_RESET,
         ("id", trx.id()));
    send_to_all_our_fork_peers(message(msg));
}

void dlt_p2p_node::broadcast_chain_status() {
    auto hello = build_hello_message();
    dlt_fork_status_message msg;
    msg.fork_status = _fork_status;
    msg.head_block_id = hello.head_block_id;
    msg.head_block_num = hello.head_block_num;
    msg.lib_block_id = hello.lib_block_id;
    msg.lib_block_num = hello.lib_block_num;
    msg.dlt_earliest_block = hello.dlt_earliest_block;
    msg.dlt_latest_block = hello.dlt_latest_block;
    msg.node_status = hello.node_status;
    send_to_all_our_fork_peers(message(msg));
}

// ── State queries ────────────────────────────────────────────────────

uint32_t dlt_p2p_node::get_connection_count() const {
    uint32_t count = 0;
    for (auto& _peer_item : _peer_states) {
            auto& state = _peer_item.second;
        if (state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE ||
            state.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) {
            count++;
        }
    }
    return count;
}

fc::time_point dlt_p2p_node::get_last_network_block_time() const {
    return _last_network_block_time;
}

void dlt_p2p_node::set_block_production(bool producing) {
    _producing_blocks = producing;
}

void dlt_p2p_node::resync_from_lib(bool force_emergency) {
    ilog(DLT_LOG_GREEN "DLT P2P: resync from LIB requested (force_emergency=${f})" DLT_LOG_RESET,
         ("f", force_emergency));

    // Reset fork tracking
    _fork_detected = false;
    _fork_detection_block_num = 0;
    _fork_resolution_state = dlt_fork_resolution_state();
    _fork_status = DLT_FORK_STATUS_NORMAL;

    transition_to_sync();

    // Re-send hello to all peers to get updated chain state
    // P40 fix: collect peers first to avoid iterator invalidation
    // when send_message -> handle_disconnect erases incoming peers.
    auto hello = build_hello_message();
    std::vector<peer_id> targets;
    for (const auto& _peer_item : _peer_states) {
        const auto& state = _peer_item.second;
        if (state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE ||
            state.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) {
            targets.push_back(_peer_item.first);
        }
    }
    for (auto id : targets) {
        send_message(id, message(hello));
        request_blocks_from_peer(id);
    }
}

void dlt_p2p_node::trigger_resync() {
    ilog(DLT_LOG_GREEN "DLT P2P: resync triggered" DLT_LOG_RESET);
    // Re-send hello to all active peers
    // P40 fix: collect peers first to avoid iterator invalidation
    // when send_message -> handle_disconnect erases incoming peers.
    auto hello = build_hello_message();
    std::vector<peer_id> targets;
    for (const auto& _peer_item : _peer_states) {
        const auto& state = _peer_item.second;
        if (state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE) {
            targets.push_back(_peer_item.first);
        }
    }
    for (auto id : targets) {
        send_message(id, message(hello));
    }
}

void dlt_p2p_node::reconnect_seeds() {
    ilog(DLT_LOG_GREEN "DLT P2P: reconnecting seed nodes" DLT_LOG_RESET);
    for (const auto& ep : _seed_nodes) {
        // Reset backoff for seeds
        for (auto& _peer_item : _peer_states) {
            auto& state = _peer_item.second;
            if (state.endpoint == ep) {
                state.reconnect_backoff_sec = dlt_peer_state::INITIAL_RECONNECT_BACKOFF_SEC;
                state.next_reconnect_attempt = fc::time_point();
                break;
            }
        }
        connect_to_peer(ep);
    }
}

void dlt_p2p_node::pause_block_processing() {
    _block_processing_paused = true;
    ilog(DLT_LOG_ORANGE "DLT P2P: block processing paused" DLT_LOG_RESET);
}

void dlt_p2p_node::drain_paused_block_queue() {
    if (_paused_block_queue.empty()) return;

    // Sort by block number so blocks are applied in chain order.
    // They may arrive out of order from multiple peers.
    std::sort(_paused_block_queue.begin(), _paused_block_queue.end(),
        [](const graphene::protocol::signed_block& a,
           const graphene::protocol::signed_block& b) {
            return a.block_num() < b.block_num();
        });

    ilog(DLT_LOG_GREEN "Draining ${n} queued blocks from pause" DLT_LOG_RESET,
         ("n", _paused_block_queue.size()));

    for (auto& block : _paused_block_queue) {
        if (_block_processing_paused) break; // re-paused during drain

        dlt_block_accept_result result;
        try {
            result = call_accept_block(block, false);
        } catch (const graphene::network::deferred_resize_exception&) {
            wlog("Deferred resize on queued block #${n}, stopping drain", ("n", block.block_num()));
            break;
        } catch (...) {
            continue;
        }

        if (result == dlt_block_accept_result::ACCEPTED) {
            _last_block_received_time = fc::time_point::now();
            _last_network_block_time = fc::time_point::now();

            ilog(DLT_LOG_BWHITE "Got queued block #${n} with ${tx} transaction(s) by validator ${w}" DLT_LOG_RESET,
                 ("n", block.block_num())("tx", block.transactions.size())("w", block.validator));

            on_block_applied(block, /*caused_fork_switch=*/false);
        }
    }

    _paused_block_queue.clear();
}

void dlt_p2p_node::run_resume_on_p2p_thread() {
    ilog("DLT P2P: block processing resumed");

    if (!_delegate) return;

    // Drain queued blocks on the P2P thread.
    // _paused_block_queue is P2P-thread-only; accept_block/push_block
    // must run here.
    if (!_paused_block_queue.empty()) {
        if (_thread) {
            _thread->async([this]() {
                drain_paused_block_queue();

                // After draining, check if we are still behind peers.
                // If not, clear the catchup flag so production resumes.
                uint32_t our_head = _delegate->get_head_block_num();
                bool any_ahead = false;
                for (const auto& _pi : _peer_states) {
                    const auto& s = _pi.second;
                    if ((s.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE ||
                         s.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) &&
                        s.peer_head_num > our_head) {
                        any_ahead = true;
                        break;
                    }
                }
                if (!any_ahead) {
                    _catchup_after_pause = false;
                    ilog(DLT_LOG_GREEN "Post-pause queue drain complete, no gap remaining (head=#${h})" DLT_LOG_RESET,
                         ("h", our_head));
                } else {
                    ilog(DLT_LOG_ORANGE "Post-pause queue drain complete, but peers still ahead (head=#${h}). Requesting gap fill." DLT_LOG_RESET,
                         ("h", our_head));
                    // Gap fill / sync will be triggered by periodic_task()
                }
            });
        }
    } else {
        // No queued blocks — send hello to refresh peer info and let
        // periodic_task() decide if a gap exists.
        dlt_hello_message hello = build_hello_message();
        message hello_msg(hello);
        for (auto& _peer_item : _peer_states) {
            auto& state = _peer_item.second;
            if (state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE ||
                state.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) {
                send_message(_peer_item.first, hello_msg);
            }
        }
    }
}

void dlt_p2p_node::resume_block_processing() {
    set_resume_flags();
    run_resume_on_p2p_thread();
}

bool dlt_p2p_node::is_on_majority_fork() const {
    return _fork_status != DLT_FORK_STATUS_MINORITY;
}

// ── Node status transitions ──────────────────────────────────────────

void dlt_p2p_node::transition_to_forward() {
    // Clear the post-pause catchup flag BEFORE the early return check.
    // This ensures witness production can resume even if we're already
    // in FORWARD mode when this is called (e.g., from periodic sync checks).
    // Without this, _catchup_after_pause stays true forever after the first
    // transition, blocking witness production indefinitely.
    if (_catchup_after_pause) {
        _catchup_after_pause = false;
        ilog(DLT_LOG_GREEN "Post-pause catchup complete, clearing flag (validator production may resume)" DLT_LOG_RESET);
    }

    // Clear chain's currently_syncing flag so the witness plugin can produce.
    // call_accept_block(sync_mode=true) during SYNC sets currently_syncing=true;
    // it only self-clears when the next accept_block(sync_mode=false) runs.
    // If our witnesses are the only producers and they're blocked by
    // is_syncing()→not_synced, no FORWARD block ever arrives to clear it —
    // indefinite deadlock.  Must clear here on every SYNC→FORWARD transition.
    if (_delegate) _delegate->clear_syncing();

    if (_node_status == DLT_NODE_STATUS_FORWARD) return;
    _node_status = DLT_NODE_STATUS_FORWARD;
    _sync_stagnation_retries = 0;
    _last_forward_head_num = 0;   // P37: reset so check_forward_stagnation initializes
    _last_forward_progress_time = fc::time_point();

    ilog(DLT_LOG_GREEN "=== DLT P2P: transitioning to FORWARD mode ===" DLT_LOG_RESET);

    // Emit peer stats immediately upon entering FORWARD mode
    log_peer_stats();
    _stats_log_counter = 0;  // reset interval timer from this point

    // P42 fix: Reset SYNCING peers to ACTIVE.  After transition_to_forward(),
    // any peer still in SYNCING lifecycle is one we just finished syncing from.
    // Without this reset, request_gap_fill() filters for ACTIVE peers only
    // and skips SYNCING ones — so it finds no eligible peer and falls back
    // to transition_to_sync(), causing the SYNC↔FORWARD oscillation.
    for (auto& _peer_item : _peer_states) {
        auto& state = _peer_item.second;
        if (state.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) {
            state.lifecycle_state = DLT_PEER_LIFECYCLE_ACTIVE;
            state.state_entered_time = fc::time_point::now();
        }
    }

    // P25 fix: Re-evaluate exchange_enabled for all peers now that
    // we're in FORWARD mode. Peers that were SYNC when we first
    // handshaked may now be fork-aligned (their blocks are in our
    // chain after sync). Re-run fork alignment check for peers
    // that currently have exchange_enabled=false.
    for (auto& _peer_item : _peer_states) {
        auto& state = _peer_item.second;
        if (!state.exchange_enabled &&
            state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE &&
            state.peer_head_num > 0) {
            // Re-check: if the peer's head block is now known to us,
            // enable exchange. The peer is on our chain.
            if (_delegate->is_block_known(state.peer_head_id)) {
                ilog(DLT_LOG_GREEN "Re-enabling exchange for peer ${ep} (head #${hn} now recognized)" DLT_LOG_RESET,
                     ("ep", state.endpoint)("hn", state.peer_head_num));
                state.exchange_enabled = true;
                state.fork_alignment = true;
            }
        }
    }

    // Notify ALL connected peers (not just exchange-enabled) that we are
    // now in FORWARD mode. Peers that still have us listed as SYNC need
    // this update to re-evaluate their exchange_enabled flag for us.
    // Without this, a peer that connected while we were SYNC may still
    // have exchange_enabled=false for us and won't send us blocks/txs.
    {
        dlt_fork_status_message status_msg;
        if (_delegate) {
            dlt_hello_message hello = build_hello_message();
            status_msg.fork_status = _fork_status;
            status_msg.head_block_id = hello.head_block_id;
            status_msg.head_block_num = hello.head_block_num;
            status_msg.lib_block_id = hello.lib_block_id;
            status_msg.lib_block_num = hello.lib_block_num;
            status_msg.dlt_earliest_block = hello.dlt_earliest_block;
            status_msg.dlt_latest_block = hello.dlt_latest_block;
        }
        status_msg.node_status = DLT_NODE_STATUS_FORWARD;

        message fwd_msg(status_msg);
        for (auto& _peer_item : _peer_states) {
            auto& state = _peer_item.second;
            if (state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE ||
                state.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) {
                send_message(_peer_item.first, fwd_msg);
            }
        }
        ilog(DLT_LOG_GREEN "Sent FORWARD status notification to all connected peers" DLT_LOG_RESET);
    }

    // Revalidate provisional mempool entries
    for (auto it = _mempool_by_id.begin(); it != _mempool_by_id.end(); ) {
        if (it->second.is_provisional) {
            if (!is_tapos_valid(it->second.trx)) {
                _mempool_total_bytes -= it->second.estimated_size();
                auto range = _mempool_by_expiry.equal_range(it->second.trx.expiration);
                for (auto exp_it = range.first; exp_it != range.second; ++exp_it) {
                    if (exp_it->second == it->first) {
                        _mempool_by_expiry.erase(exp_it);
                        break;
                    }
                }
                it = _mempool_by_id.erase(it);
                continue;
            }
            it->second.is_provisional = false;
        }
        ++it;
    }
}

void dlt_p2p_node::transition_to_sync() {
    if (_node_status == DLT_NODE_STATUS_SYNC) return;
    _node_status = DLT_NODE_STATUS_SYNC;
    _sync_stagnation_retries = 0;
    _last_block_received_time = fc::time_point::now();
    ilog(DLT_LOG_ORANGE "=== DLT P2P: transitioning to SYNC mode ===" DLT_LOG_RESET);
}

// ── Sync stagnation ──────────────────────────────────────────────────

void dlt_p2p_node::sync_stagnation_check() {
    if (_node_status != DLT_NODE_STATUS_SYNC) return;
    if (_last_block_received_time == fc::time_point()) return;

    auto elapsed = fc::time_point::now() - _last_block_received_time;
    if (elapsed.count() < SYNC_STAGNATION_SEC * 1000000) return;

    _sync_stagnation_retries++;
    ilog(DLT_LOG_ORANGE "Sync stagnation: no block for ${s}s (retry ${r}/${m})" DLT_LOG_RESET,
         ("s", SYNC_STAGNATION_SEC)("r", _sync_stagnation_retries)("m", SYNC_STAGNATION_MAX_RETRIES));

    if (_sync_stagnation_retries >= SYNC_STAGNATION_MAX_RETRIES) {
        wlog("Sync stagnation max retries reached, transitioning to FORWARD");
        transition_to_forward();
        return;
    }

    // Re-request from all active peers
    for (auto& _peer_item : _peer_states) {
            auto& id = _peer_item.first;
            auto& state = _peer_item.second;
        if (state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE ||
            state.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) {
            request_blocks_from_peer(id);
        }
    }
    _last_block_received_time = fc::time_point::now(); // reset timer
}

void dlt_p2p_node::check_sync_catchup() {
    if (_node_status != DLT_NODE_STATUS_SYNC) return;
    if (!_delegate) return;

    uint32_t our_head = _delegate->get_head_block_num();
    if (our_head == 0) return;  // nothing to catch up to

    // Count active peers and check if our head is at or ahead of them.
    // known_head_peers counts only peers that have reported a non-zero head.
    // Empty peers (head=0 from hello, e.g. new nodes with no blocks) are
    // skipped for the "ahead" check but still count toward active_peer_count
    // so isolation logic works correctly.
    uint32_t active_peer_count = 0;
    uint32_t known_head_peers = 0;
    bool has_peer_ahead = false;
    for (const auto& _peer_item : _peer_states) {
        const auto& state = _peer_item.second;
        if (state.lifecycle_state != DLT_PEER_LIFECYCLE_ACTIVE &&
            state.lifecycle_state != DLT_PEER_LIFECYCLE_SYNCING) continue;
        active_peer_count++;
        if (state.peer_head_num == 0) continue;  // empty peer — no head info
        known_head_peers++;
        if (our_head < state.peer_head_num) {
            has_peer_ahead = true;
            break;
        }
    }

    // Isolation: no active peers at all — cannot claim "caught up".
    // Track when isolation started and reset peers after ISOLATION_RESET_SEC.
    if (active_peer_count == 0) {
        if (_isolation_detected_time == fc::time_point()) {
            _isolation_detected_time = fc::time_point::now();
            wlog(DLT_LOG_ORANGE "Sync isolation: no active peers, head=#${h}. Will reset peers in ${t}s" DLT_LOG_RESET,
                 ("h", our_head)("t", ISOLATION_RESET_SEC));
        } else {
            auto iso_elapsed = fc::time_point::now() - _isolation_detected_time;
            if (iso_elapsed.count() > ISOLATION_RESET_SEC * 1000000) {
                emergency_peer_reset();
            }
        }
        return;  // never claim caught up while isolated
    }

    // We have active peers — clear isolation tracking
    _isolation_detected_time = fc::time_point();

    // Only transition to FORWARD if at least one peer reported its head AND
    // none of those peers is ahead of us.  Requiring known_head_peers > 0
    // prevents a false "caught up" when all connected peers are empty nodes
    // (e.g. slaveC with head=0) — they give us no evidence about network state.
    if (known_head_peers > 0 && !has_peer_ahead) {
        // Don't transition to FORWARD when we have blocks in fork_db that we
        // haven't been able to apply yet (e.g. we're on a wrong fork and the
        // canonical chain blocks are accumulating in fork_db ahead of us).
        // In that state the peers' known heads happen to be <= our fork head,
        // but _highest_seen_block_num reflects the real network tip.
        // Transitioning to FWD here would cause request_gap_fill to fail with
        // "no peer available" and immediately cycle back to SYNC, resetting
        // the stagnation timer each time and delaying fork-switch recovery by
        // many minutes (p68 scenario).  Stay in SYNC so the stagnation check
        // can fire at the right time and request the full alternative chain.
        if (_highest_seen_block_num > our_head + FORWARD_FALLBEHIND_THRESHOLD) {
            dlog("Staying in SYNC: highest seen block #${h} is ${d} blocks ahead of our head #${o} — unlinked fork_db blocks pending",
                 ("h", _highest_seen_block_num)("d", _highest_seen_block_num - our_head)("o", our_head));
            return;
        }
        ilog(DLT_LOG_GREEN "Sync catchup detected: our head (#${h}) >= all ${k} known-head peers, transitioning to FORWARD" DLT_LOG_RESET,
             ("h", our_head)("k", known_head_peers));
        transition_to_forward();
    }
}

// ── Emergency peer reset ─────────────────────────────────────────────

void dlt_p2p_node::emergency_peer_reset() {
    wlog(DLT_LOG_RED "Emergency peer reset: all peers disconnected/banned, resetting backoffs and clearing bans" DLT_LOG_RESET);

    for (auto& _peer_item : _peer_states) {
        auto& state = _peer_item.second;

        if (state.lifecycle_state == DLT_PEER_LIFECYCLE_BANNED) {
            ilog(DLT_LOG_GREEN "  Clearing soft-ban for ${ep} (was: ${r})" DLT_LOG_RESET,
                 ("ep", state.endpoint)("r", state.ban_reason));
            state.ban_reason.clear();
            state.ban_duration_sec = 0;
            state.spam_strikes = 0;
            state.lifecycle_state = DLT_PEER_LIFECYCLE_DISCONNECTED;
            state.disconnected_since = fc::time_point::now();
        }

        if (state.lifecycle_state == DLT_PEER_LIFECYCLE_DISCONNECTED) {
            state.reconnect_backoff_sec = dlt_peer_state::INITIAL_RECONNECT_BACKOFF_SEC;
            state.next_reconnect_attempt = fc::time_point::now();  // immediate
            state.spam_strikes = 0;
            ilog(DLT_LOG_GREEN "  Reset backoff for ${ep} -> ${b}s, immediate reconnect" DLT_LOG_RESET,
                 ("ep", state.endpoint)("b", state.reconnect_backoff_sec));
        }
    }

    _sync_stagnation_retries = 0;
    _isolation_detected_time = fc::time_point();  // reset so it can trigger again later
}

void dlt_p2p_node::check_forward_behind() {
    if (_node_status != DLT_NODE_STATUS_FORWARD) return;
    if (!_delegate) return;

    uint32_t our_head = _delegate->get_head_block_num();

    // Check if any active peer is significantly ahead of us.
    // In FORWARD mode, blocks arrive via broadcast. If we've fallen
    // behind by more than FORWARD_FALLBEHIND_THRESHOLD blocks, we likely
    // missed broadcast blocks (e.g. connection dropped during production)
    // and need to catch up via SYNC range requests.
    for (const auto& _peer_item : _peer_states) {
        const auto& state = _peer_item.second;
        if (state.lifecycle_state != DLT_PEER_LIFECYCLE_ACTIVE) continue;
        if (state.peer_head_num == 0) continue;
        if (state.peer_head_num > our_head + FORWARD_FALLBEHIND_THRESHOLD) {
            ilog(DLT_LOG_ORANGE "Falling behind in FORWARD mode: our head #${h}, peer ${ep} at #${p} — transitioning to SYNC" DLT_LOG_RESET,
                 ("h", our_head)("ep", state.endpoint)("p", state.peer_head_num));
            transition_to_sync();
            // Request blocks from all exchange-enabled peers that are ahead
            for (auto& pi : _peer_states) {
                auto& s = pi.second;
                if (s.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE &&
                    s.exchange_enabled && s.peer_head_num > our_head) {
                    request_blocks_from_peer(pi.first);
                }
            }
            return;
        }
    }
}

void dlt_p2p_node::check_forward_stagnation() {
    if (_node_status != DLT_NODE_STATUS_FORWARD) return;
    if (!_delegate) return;

    uint32_t our_head = _delegate->get_head_block_num();

    // Initialize on first call in FORWARD mode
    if (_last_forward_head_num == 0 || _last_forward_progress_time == fc::time_point()) {
        _last_forward_head_num = our_head;
        _last_forward_progress_time = fc::time_point::now();
        return;
    }

    // If our head has advanced, reset the timer
    if (our_head > _last_forward_head_num) {
        _last_forward_head_num = our_head;
        _last_forward_progress_time = fc::time_point::now();
        return;
    }

    // Head hasn't advanced — check how long we've been stuck
    auto elapsed = fc::time_point::now() - _last_forward_progress_time;
    if (elapsed.count() > FORWARD_STAGNATION_SEC * 1000000) {
        // Check if we're isolated (no active connections)
        uint32_t active_count = 0;
        for (const auto& pi : _peer_states) {
            const auto& s = pi.second;
            if (s.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE ||
                s.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) {
                active_count++;
            }
        }

        if (active_count == 0) {
            // Isolated: track when isolation started and reset after ISOLATION_RESET_SEC
            if (_isolation_detected_time == fc::time_point()) {
                _isolation_detected_time = fc::time_point::now();
                wlog(DLT_LOG_ORANGE "FORWARD isolation: head stuck at #${h} for ${s}s, no active peers. Will reset in ${t}s" DLT_LOG_RESET,
                     ("h", our_head)("s", FORWARD_STAGNATION_SEC)("t", ISOLATION_RESET_SEC));
            } else {
                auto iso_elapsed = fc::time_point::now() - _isolation_detected_time;
                if (iso_elapsed.count() > ISOLATION_RESET_SEC * 1000000) {
                    transition_to_sync();
                    emergency_peer_reset();
                }
            }
            return;  // don't oscillate to SYNC without a plan
        }

        // We have active peers but head is stuck — normal stagnation path
        _isolation_detected_time = fc::time_point();  // clear isolation if peers are back

        // Only transition to SYNC if at least one peer is ahead of us.
        // If no peer is ahead, SYNC mode has nothing to offer — we'd just
        // oscillate back to FORWARD on the next tick via check_sync_catchup().
        bool has_peer_ahead = false;
        for (const auto& pi : _peer_states) {
            const auto& s = pi.second;
            if ((s.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE ||
                 s.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) &&
                s.peer_head_num > our_head) {
                has_peer_ahead = true;
                break;
            }
        }

        if (!has_peer_ahead) {
            std::string witness_diag;
            if (_witness_diag_provider) {
                try { witness_diag = " | " + _witness_diag_provider(); } catch (...) {}
            }
            ilog(DLT_LOG_ORANGE "FORWARD stagnation: head stuck at #${h} for ${s}s, but no peer ahead — resetting stagnation timer${wd}" DLT_LOG_RESET,
                 ("h", our_head)("s", FORWARD_STAGNATION_SEC)("wd", witness_diag));
            _last_forward_head_num = our_head;
            _last_forward_progress_time = fc::time_point::now();
            return;
        }

        wlog(DLT_LOG_ORANGE "FORWARD stagnation: head stuck at #${h} for ${s}s — transitioning to SYNC" DLT_LOG_RESET,
             ("h", our_head)("s", FORWARD_STAGNATION_SEC));
        transition_to_sync();
        // Request blocks from all exchange-enabled peers that are ahead
        for (auto& pi : _peer_states) {
            auto& s = pi.second;
            if (s.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE &&
                s.exchange_enabled && s.peer_head_num > our_head) {
                request_blocks_from_peer(pi.first);
            }
        }
    }
}

// ── Mempool ──────────────────────────────────────────────────────────

bool dlt_p2p_node::add_to_mempool(const signed_transaction& trx, bool from_peer, peer_id sender) {
    auto trx_id = trx.id();

    // Dedup
    if (_mempool_by_id.count(trx_id)) return false;

    // Check expiry
    if (trx.expiration < fc::time_point_sec(fc::time_point::now())) {
        if (from_peer && sender != INVALID_PEER_ID) record_packet_result(sender, false);
        return false;
    }

    // Check expiration headroom
    auto max_exp = fc::time_point_sec(fc::time_point::now()) + fc::hours(_mempool_max_expiration_hours);
    if (trx.expiration > max_exp) {
        if (from_peer && sender != INVALID_PEER_ID) record_packet_result(sender, false);
        return false;
    }

    // Check size
    uint32_t tx_size = static_cast<uint32_t>(fc::raw::pack_size(trx));
    if (tx_size > _mempool_max_tx_size) {
        if (from_peer && sender != INVALID_PEER_ID) record_packet_result(sender, false);
        return false;
    }

    // Check TaPoS validity.
    // Do NOT strike the sender on TaPoS failure: TaPoS validity is a function
    // of OUR chain state (block_summary_object circular buffer), not the
    // sender's behavior. When we are behind, recently-issued txs reference
    // blocks past our head whose ref_block_num slot still holds an older
    // block — the prefix check fails through no fault of the sender. Striking
    // here caused a feedback loop where a slave node soft-banned the very peer
    // that had the blocks it needed to catch up. Genuinely-bad packets
    // (expiry past, oversized, far-future expiration) still strike above.
    if (!is_tapos_valid(trx)) {
        dlog(DLT_LOG_DGRAY "TaPoS-invalid trx ${id} from peer ${s} (we may be behind) — not striking" DLT_LOG_RESET,
             ("id", trx_id)("s", sender));
        return false;
    }

    // Enforce mempool size limits
    if (is_mempool_full()) {
        evict_oldest_expiry_mempool_entries(1);
        if (is_mempool_full()) return false; // still full after eviction
    }

    // Add entry
    dlt_mempool_entry entry;
    entry.trx_id = trx_id;
    entry.trx = trx;
    entry.received_time = fc::time_point::now();
    entry.is_provisional = (_node_status == DLT_NODE_STATUS_SYNC);
    entry.expected_head = _delegate->get_head_block_id();

    _mempool_by_id[trx_id] = entry;
    _mempool_by_expiry.insert({trx.expiration, trx_id});
    _mempool_total_bytes += tx_size;

    // Retranslate to our-fork peers (if from peer)
    if (from_peer && sender != INVALID_PEER_ID) {
        dlog(DLT_LOG_DGRAY "Relaying transaction ${id} to fork peers (excluding sender)" DLT_LOG_RESET,
             ("id", trx_id));
        dlt_transaction_message msg;
        msg.trx = trx;
        send_to_all_our_fork_peers(message(msg), sender);
    }

    return true;
}

void dlt_p2p_node::remove_transactions_in_block(const signed_block& block) {
    for (const auto& trx : block.transactions) {
        auto trx_id = trx.id();
        auto it = _mempool_by_id.find(trx_id);
        if (it != _mempool_by_id.end()) {
            _mempool_total_bytes -= it->second.estimated_size();
            auto range = _mempool_by_expiry.equal_range(it->second.trx.expiration);
            for (auto exp_it = range.first; exp_it != range.second; ++exp_it) {
                if (exp_it->second == trx_id) {
                    _mempool_by_expiry.erase(exp_it);
                    break;
                }
            }
            _mempool_by_id.erase(it);
        }
    }
}

void dlt_p2p_node::prune_mempool_on_fork_switch() {
    for (auto it = _mempool_by_id.begin(); it != _mempool_by_id.end(); ) {
        if (!is_tapos_valid(it->second.trx)) {
            _mempool_total_bytes -= it->second.estimated_size();
            auto range = _mempool_by_expiry.equal_range(it->second.trx.expiration);
            for (auto exp_it = range.first; exp_it != range.second; ++exp_it) {
                if (exp_it->second == it->first) {
                    _mempool_by_expiry.erase(exp_it);
                    break;
                }
            }
            it = _mempool_by_id.erase(it);
        } else {
            ++it;
        }
    }
}

void dlt_p2p_node::periodic_mempool_cleanup() {
    auto now = fc::time_point_sec(fc::time_point::now());

    // Prune expired
    while (!_mempool_by_expiry.empty()) {
        auto it = _mempool_by_expiry.begin();
        if (it->first >= now) break;
        auto trx_id = it->second;
        auto map_it = _mempool_by_id.find(trx_id);
        if (map_it != _mempool_by_id.end()) {
            _mempool_total_bytes -= map_it->second.estimated_size();
            _mempool_by_id.erase(map_it);
        }
        _mempool_by_expiry.erase(it);
    }

    // Prune TaPoS-invalid (if in FORWARD mode)
    if (_node_status == DLT_NODE_STATUS_FORWARD) {
        prune_mempool_on_fork_switch();
    }
}

bool dlt_p2p_node::is_tapos_valid(const signed_transaction& trx) const {
    if (trx.ref_block_num == 0) return true; // no TaPoS
    // Use the same block_summary_object circular buffer check as the chain.
    // The chain stores block IDs in a 65536-slot circular buffer
    // (block_num & 0xFFFF) and validates TaPoS by comparing
    // ref_block_prefix against the stored block_id._hash[1].
    // Previously this used find_block_id_for_num() which fails for
    // blocks that have been pruned from the DLT block log (causing
    // transactions from cli_wallet to be rejected by peer mempools).
    return _delegate->check_tapos_block_summary(trx.ref_block_num, trx.ref_block_prefix);
}

bool dlt_p2p_node::is_mempool_full() const {
    return _mempool_by_id.size() >= _mempool_max_tx ||
           _mempool_total_bytes >= _mempool_max_bytes;
}

void dlt_p2p_node::evict_oldest_expiry_mempool_entries(uint32_t count) {
    for (uint32_t i = 0; i < count && !_mempool_by_expiry.empty(); ++i) {
        auto it = _mempool_by_expiry.begin();
        auto trx_id = it->second;
        auto map_it = _mempool_by_id.find(trx_id);
        if (map_it != _mempool_by_id.end()) {
            _mempool_total_bytes -= map_it->second.estimated_size();
            _mempool_by_id.erase(map_it);
        }
        _mempool_by_expiry.erase(it);
    }
}

// ── Fork resolution ──────────────────────────────────────────────────

void dlt_p2p_node::on_block_applied(const signed_block& block, bool caused_fork_switch) {
    // Remove transactions in this block from mempool
    remove_transactions_in_block(block);

    // Track fork state
    track_fork_state(block);

    // If fork switch, prune mempool
    if (caused_fork_switch) {
        prune_mempool_on_fork_switch();
    }

    // DLT block log pruning check
    periodic_dlt_prune_check();

    // Advance stale expected_next_block for all peers.
    // When a block is applied (from any source: own production, network
    // peer A, gap fill), other peers' expected_next_block may still be
    // at the old value, causing false "out of order" warnings when those
    // peers send the next block. Advancing here keeps per-peer tracking
    // consistent with our actual chain head.
    uint32_t next = block.block_num() + 1;
    for (auto& item : _peer_states) {
        if (item.second.expected_next_block != 0 &&
            item.second.expected_next_block < next) {
            item.second.expected_next_block = next;
        }
    }
}

void dlt_p2p_node::track_fork_state(const signed_block& block) {
    if (!_delegate) return;

    auto tips = _delegate->get_fork_branch_tips();
    if (tips.size() > 1) {
        if (!_fork_detected) {
            _fork_detected = true;
            _fork_detection_block_num = block.block_num();
            _fork_status = DLT_FORK_STATUS_LOOKING_RESOLUTION;
            ilog(DLT_LOG_RED "Fork detected at block #${n}, ${t} competing branches" DLT_LOG_RESET,
                 ("n", block.block_num())("t", tips.size()));
        }
    }

    if (_fork_detected &&
        block.block_num() - _fork_detection_block_num >= FORK_RESOLUTION_BLOCK_THRESHOLD) {
        resolve_fork();
        // _fork_detected is cleared inside resolve_fork() only when resolution completes
    }
}

void dlt_p2p_node::resolve_fork() {
    auto tips = _delegate->get_fork_branch_tips();
    if (tips.size() < 2) {
        _fork_status = DLT_FORK_STATUS_NORMAL;
        _fork_detected = false;  // fork resolved: only 1 branch remains
        return;
    }

    // Find the heaviest branch using vote-weighted comparison
    block_id_type winner_tip = tips[0];
    for (size_t i = 1; i < tips.size(); ++i) {
        if (_delegate->compare_fork_branches(tips[i], winner_tip) > 0) {
            winner_tip = tips[i];
        }
    }
    dlt_fork_branch_info winner;
    winner.tip = winner_tip;

    // Hysteresis check
    if (winner.tip == _fork_resolution_state.current_winner_tip) {
        _fork_resolution_state.consecutive_blocks_as_winner++;
    } else {
        _fork_resolution_state.current_winner_tip = winner.tip;
        _fork_resolution_state.consecutive_blocks_as_winner = 1;
    }

    if (!_fork_resolution_state.is_confirmed()) {
        ilog(DLT_LOG_ORANGE "Fork resolution: candidate has ${n}/${c} confirmations" DLT_LOG_RESET,
             ("n", _fork_resolution_state.consecutive_blocks_as_winner)
             ("c", dlt_fork_resolution_state::CONFIRMATION_BLOCKS));
        return;
    }

    // We have a confirmed winner
    if (_delegate->is_head_on_branch(winner.tip)) {
        _fork_status = DLT_FORK_STATUS_NORMAL;
        ilog(DLT_LOG_GREEN "Fork resolved: we are on majority fork (weight=${w})" DLT_LOG_RESET,
             ("w", winner.total_vote_weight));
    } else {
        _fork_status = DLT_FORK_STATUS_MINORITY;
        wlog(DLT_LOG_RED "We are on MINORITY fork! Switching to majority." DLT_LOG_RESET);
        _delegate->switch_to_fork(winner.tip);
        prune_mempool_on_fork_switch();
    }

    // Reset hysteresis
    _fork_resolution_state = dlt_fork_resolution_state();
    _fork_detected = false;  // fork resolution completed
}

dlt_fork_branch_info dlt_p2p_node::compute_branch_info(const block_id_type& tip) const {
    dlt_fork_branch_info info;
    info.tip = tip;
    // Detailed computation requires fork_db traversal — delegate provides comparison
    // For now, use a simplified approach: just set the tip
    // The actual vote-weight computation is done by the delegate's compare_fork_branches
    info.block_count = 1;
    info.total_vote_weight = 0;
    return info;
}

// ── Anti-spam ────────────────────────────────────────────────────────

bool dlt_p2p_node::record_packet_result(peer_id peer, bool is_good) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return is_good;

    if (is_good) {
        it->second.spam_strikes = 0;
        it->second.last_good_packet_time = fc::time_point::now();
        return true;
    }

    it->second.spam_strikes++;
    if (it->second.spam_strikes >= SPAM_STRIKE_THRESHOLD) {
        soft_ban_peer(peer, "spam strike threshold exceeded");
        return false;
    }
    return true;
}

void dlt_p2p_node::soft_ban_peer(peer_id peer, const std::string& reason) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;

    wlog(DLT_LOG_RED "Soft-banning peer ${ep} for ${d}s (reason: ${r})" DLT_LOG_RESET,
         ("ep", it->second.endpoint)("d", BAN_DURATION_SEC)("r", reason));

    // Notify the peer before disconnecting so they can stop spamming us
    try {
        dlt_soft_ban_message ban_msg;
        ban_msg.ban_duration_sec = BAN_DURATION_SEC;
        ban_msg.reason = reason;
        send_message(peer, message(ban_msg));
    } catch (...) {
        // Best-effort send — peer may already be disconnected
    }

    it->second.lifecycle_state = DLT_PEER_LIFECYCLE_BANNED;
    it->second.state_entered_time = fc::time_point::now();
    it->second.ban_reason = reason;
    it->second.ban_duration_sec = BAN_DURATION_SEC;
    it->second.reconnect_backoff_sec = BAN_DURATION_SEC;
    it->second.next_reconnect_attempt = fc::time_point::now() + fc::seconds(BAN_DURATION_SEC);

    auto conn_it = _connections.find(peer);
    if (conn_it != _connections.end()) {
        try { if (conn_it->second) conn_it->second->close(); } catch (...) {}
        _connections.erase(conn_it);
    }
}

// ── Diagnostics ──────────────────────────────────────────────────────

void dlt_p2p_node::log_node_status() {
    const char* G = DLT_LOG_GREEN;
    const char* R = DLT_LOG_RESET;

    const char* status_str = (_node_status == DLT_NODE_STATUS_SYNC) ? "SYNC" : "FWD";
    const char* fork_str;
    switch (_fork_status) {
        case DLT_FORK_STATUS_NORMAL: fork_str = "NORMAL"; break;
        case DLT_FORK_STATUS_LOOKING_RESOLUTION: fork_str = "LOOKING"; break;
        case DLT_FORK_STATUS_MINORITY: fork_str = "MINORITY"; break;
        default: fork_str = "?"; break;
    }
    uint32_t our_head = _delegate ? _delegate->get_head_block_num() : 0;
    uint32_t our_lib  = _delegate ? _delegate->get_lib_block_num() : 0;
    uint32_t dlt_earliest = _delegate ? _delegate->get_dlt_earliest_block() : 0;
    uint32_t dlt_latest   = _delegate ? _delegate->get_dlt_latest_block() : 0;

    // Count connected / active peers
    uint32_t connected = 0, active = 0;
    for (const auto& item : _peer_states) {
        const auto& s = item.second;
        if (s.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE ||
            s.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) { active++; connected++; }
        else if (s.lifecycle_state == DLT_PEER_LIFECYCLE_HANDSHAKING) { connected++; }
    }

    std::string flags;
    if (_block_processing_paused) flags += "PAUSED ";
    if (_fork_status != DLT_FORK_STATUS_NORMAL) flags += "FORK:" + std::string(fork_str) + " ";
    if (flags.empty()) flags = "ok";

    int64_t uptime_sec = (_node_start_time.sec_since_epoch() > 0)
        ? (fc::time_point::now() - _node_start_time).count() / 1000000
        : 0;
    int up_h = (int)(uptime_sec / 3600);
    int up_m = (int)((uptime_sec % 3600) / 60);
    int up_s = (int)(uptime_sec % 60);

    ilog("${G}DLT Status | ${st} | head=#${h} lib=#${lib} | dlt_range=${de}-${dl} | peers=${a}active/${c}conn | uptime=${uh}h${um}m${us}s | ${fl}${R}",
         ("G", G)("st", status_str)("h", our_head)("lib", our_lib)
         ("de", dlt_earliest)("dl", dlt_latest)
         ("a", active)("c", connected)
         ("uh", up_h)("um", up_m)("us", up_s)
         ("fl", flags)("R", R));

    // Emit peer stats on the first node status log (1 min after startup)
    if (!_first_node_status_logged) {
        _first_node_status_logged = true;
        log_peer_stats();
    }
}

void dlt_p2p_node::log_peer_stats() {
    const char* C = DLT_LOG_CYAN;
    const char* R = DLT_LOG_RESET;

    // Node-level summary
    const char* status_str = (_node_status == DLT_NODE_STATUS_SYNC) ? "SYNC" : "FWD";
    const char* fork_str;
    switch (_fork_status) {
        case DLT_FORK_STATUS_NORMAL: fork_str = "NORMAL"; break;
        case DLT_FORK_STATUS_LOOKING_RESOLUTION: fork_str = "LOOKING"; break;
        case DLT_FORK_STATUS_MINORITY: fork_str = "MINORITY"; break;
        default: fork_str = "?"; break;
    }
    uint32_t our_head = _delegate ? _delegate->get_head_block_num() : 0;
    uint32_t our_lib  = _delegate ? _delegate->get_lib_block_num() : 0;

    int64_t uptime_sec = (_node_start_time.sec_since_epoch() > 0)
        ? (fc::time_point::now() - _node_start_time).count() / 1000000
        : 0;
    int up_h = (int)(uptime_sec / 3600);
    int up_m = (int)((uptime_sec % 3600) / 60);
    int up_s = (int)(uptime_sec % 60);
    ilog("${C}=== DLT P2P Stats | status=${st} fork=${fk} head=${h} lib=${lib} peers=${n} conn=${c} paused=${p} uptime=${uh}h${um}m${us}s ===${R}",
         ("C", C)("st", status_str)("fk", fork_str)("h", our_head)("lib", our_lib)
         ("n", _peer_states.size())("c", _connections.size())
         ("p", _block_processing_paused ? "YES" : "no")
         ("uh", up_h)("um", up_m)("us", up_s)("R", R));

    // Per-peer details
    // NOTE: peer_head_num is from the last hello/fork_status exchange or
    // block relay — it is NOT real-time. The peer's actual head may be
    // higher. It should not be used as an authoritative measure of the
    // peer's current chain state.
    for (auto& _peer_item : _peer_states) {
        auto& state = _peer_item.second;
        auto ep = std::string(state.endpoint);

        // Lifecycle state label
        const char* ls;
        switch (state.lifecycle_state) {
            case DLT_PEER_LIFECYCLE_CONNECTING:   ls = "CONNECT"; break;
            case DLT_PEER_LIFECYCLE_HANDSHAKING:  ls = "HANDSHAKE"; break;
            case DLT_PEER_LIFECYCLE_SYNCING:      ls = "SYNCING"; break;
            case DLT_PEER_LIFECYCLE_ACTIVE:       ls = "ACTIVE"; break;
            case DLT_PEER_LIFECYCLE_DISCONNECTED: ls = "DISC"; break;
            case DLT_PEER_LIFECYCLE_BANNED:       ls = "BANNED"; break;
            default:                              ls = "?"; break;
        }

        if (state.lifecycle_state == DLT_PEER_LIFECYCLE_BANNED) {
            auto ban_elapsed = (fc::time_point::now() - state.state_entered_time).count() / 1000000;
            auto ban_dur = (state.ban_duration_sec > 0) ? state.ban_duration_sec : BAN_DURATION_SEC;
            auto ban_remaining = (ban_dur > ban_elapsed) ? (ban_dur - ban_elapsed) : 0;
            ilog("${C}  ${ep} | ${ls} | ban_remaining=${br}s | reason=${reason}${R}",
                 ("C", C)("ep", ep)("ls", ls)("br", ban_remaining)("reason", state.ban_reason)("R", R));
            continue;
        }

        if (state.lifecycle_state == DLT_PEER_LIFECYCLE_DISCONNECTED) {
            auto disc_sec = (fc::time_point::now() - state.disconnected_since).count() / 1000000;
            auto recon_sec = (state.next_reconnect_attempt != fc::time_point())
                             ? (state.next_reconnect_attempt - fc::time_point::now()).count() / 1000000 : 0;
            ilog("${C}  ${ep} | ${ls} | disconnected=${ds}s | backoff=${bo}s | reconnect_in=${ri}s | spam=${s}${R}",
                 ("C", C)("ep", ep)("ls", ls)("ds", disc_sec)("bo", state.reconnect_backoff_sec)
                 ("ri", (recon_sec > 0 ? recon_sec : 0))("s", state.spam_strikes)("R", R));
            continue;
        }

        // Active/handshaking/syncing peers — show exchange status, fork alignment, ranges, flags
        std::string flags;
        if (state.fork_alignment) flags += "+align";
        if (state.peer_emergency_active) flags += "+emrg";
        if (state.peer_has_emergency_key) flags += "+ekey";
        if (state.pending_sync_start > 0) flags += "+sync";
        if (flags.empty()) flags = "-";

        const char* exch_str = state.exchange_enabled ? "YES" : "no";

        const char* peer_fork_str;
        switch (state.peer_fork_status) {
            case DLT_FORK_STATUS_NORMAL: peer_fork_str = "NORM"; break;
            case DLT_FORK_STATUS_LOOKING_RESOLUTION: peer_fork_str = "LOOK"; break;
            case DLT_FORK_STATUS_MINORITY: peer_fork_str = "MINO"; break;
            default: peer_fork_str = "?"; break;
        }
        const char* peer_node_str = (state.peer_node_status == DLT_NODE_STATUS_SYNC) ? "SYNC" : "FWD";

        ilog("${C}  ${ep} | ${ls} | exch=${ex} | head=${ph} lib=${pl} | range=${pe}-${pt} | peer_fork=${pf} peer_node=${pn} | spam=${s} | ${fl}${sq}${R}",
             ("C", C)("ep", ep)("ls", ls)("ex", exch_str)
             ("ph", state.peer_head_num)("pl", state.peer_lib_num)
             ("pe", state.peer_dlt_earliest)("pt", state.peer_dlt_latest)
             ("pf", peer_fork_str)("pn", peer_node_str)
             ("s", state.spam_strikes)("fl", flags)
             ("sq", (state.send_queue.empty() && state.send_queue_dropped == 0) ? std::string() : " sq=" + std::to_string(state.send_queue.size()) + "/qd=" + std::to_string(state.send_queue_dropped))
             ("R", R));
    }

    ilog("${C}=== End DLT P2P Stats ===${R}", ("C", C)("R", R));
}

// ── DLT block log pruning ────────────────────────────────────────────

void dlt_p2p_node::periodic_dlt_prune_check() {
    if (!_delegate) return;
    uint32_t earliest = _delegate->get_dlt_earliest_block();
    uint32_t latest = _delegate->get_dlt_latest_block();
    if (latest == 0 || earliest == 0) return;

    uint32_t current_range = latest - earliest + 1;
    if (current_range <= _dlt_block_log_max_blocks) return;

    // Only prune in batches of DLT_PRUNE_BATCH_SIZE (10000)
    if (latest - _last_prune_block_num < DLT_PRUNE_BATCH_SIZE) return;

    ilog(DLT_LOG_GREEN "DLT block log exceeds max (${r} > ${m}), pruning ${b} blocks" DLT_LOG_RESET,
         ("r", current_range)("m", _dlt_block_log_max_blocks)("b", DLT_PRUNE_BATCH_SIZE));

    // The actual pruning is done at the chain level via truncate_before()
    // We signal the delegate to prune, passing the new start block number
    uint32_t new_start = earliest + DLT_PRUNE_BATCH_SIZE;
    // TODO: Add dlt_p2p_delegate::prune_dlt_block_log(uint32_t new_start)
    (void)new_start;  // suppress unused variable warning until delegate method is added
    _last_prune_block_num = latest;
}

// ── Peer exchange periodic ───────────────────────────────────────────

void dlt_p2p_node::periodic_peer_exchange() {
    if (_isolated_peers) return;
    if (_node_status != DLT_NODE_STATUS_FORWARD) return;

   
    // Pick a random active peer to request exchange from.
    // When only one exchange-enabled peer exists (common for nodes behind NAT
    // or freshly started nodes), all requests go to that single peer and hit
    // the 3/300s rate-limit quickly.  Back off to one request per 90s in that
    // case so we never exceed the limit (3 requests / 300s = 1 per 100s max).

    std::vector<peer_id> candidates;
    for (auto& _peer_item : _peer_states) {
            auto& id = _peer_item.first;
            auto& state = _peer_item.second;
        if (state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE && state.exchange_enabled) {
            if (!state.is_peer_exchange_rate_limited()) {
                candidates.push_back(id);
            }
        }
    }

    if (candidates.empty()) return;
// Dynamic throttle: ensure no single peer is asked more than 3 times per 300s.
    //
    // With N exchange-enabled peers and a random pick each loop (5s interval):
    //   requests per peer per 300s ≈ 300s / 5s / N = 60 / N
    //   rate-limit threshold        = 3 requests / 300s
    //   safe minimum loop interval  = 300s / (3 × N) = 100s / N
    //
    // Examples:
    //   N=1  → min interval 100s  (was hardcoded 90s, now exact)
    //   N=2  → min interval  50s
    //   N=5  → min interval  20s
    //   N=20 → min interval   5s  (≥ loop tick, no extra throttle needed)
    //
    // We track _last_peer_exchange globally; the random peer pick spreads
    // load evenly across candidates so this global gate is sufficient.
    {
        size_t n = candidates.size();
        int64_t min_interval_us = (n >= 20)
            ? 0LL
            : static_cast<int64_t>(100'000'000LL / static_cast<int64_t>(n)); // 100s / N in microseconds
        if (min_interval_us > 0) {
            auto now = fc::time_point::now();
            if ((now - _last_peer_exchange_time).count() < min_interval_us) return;
            _last_peer_exchange_time = now;
        }
    }
    
    thread_local std::mt19937 peer_rng(std::hash<std::thread::id>{}(std::this_thread::get_id()) ^ uint32_t(fc::time_point::now().sec_since_epoch()));
    size_t idx = peer_rng() % candidates.size();
    send_message(candidates[idx], message(dlt_peer_exchange_request()));
}

// ── Subnet diversity ─────────────────────────────────────────────────

uint32_t dlt_p2p_node::count_peers_in_subnet(const fc::ip::address& addr) const {
    uint32_t count = 0;
    for (auto& _peer_item : _peer_states) {
            auto& state = _peer_item.second;
        if (is_same_subnet(state.endpoint.get_address(), addr)) {
            count++;
        }
    }
    return count;
}

bool dlt_p2p_node::is_same_subnet(const fc::ip::address& a, const fc::ip::address& b) const {
    // /24 subnet check — compare first 3 bytes
    uint32_t a_ip = static_cast<uint32_t>(a);
    uint32_t b_ip = static_cast<uint32_t>(b);
    return (a_ip >> 8) == (b_ip >> 8);
}

// ── Block validation timeout ─────────────────────────────────────────

void dlt_p2p_node::block_validation_timeout() {
    for (auto& _peer_item : _peer_states) {
            auto& id = _peer_item.first;
            auto& state = _peer_item.second;
        if (state.has_pending_batch_timeout()) {
            wlog(DLT_LOG_RED "Block validation timeout for peer ${ep} (30s)" DLT_LOG_RESET,
                 ("ep", state.endpoint));
            record_packet_result(id, false);
            state.pending_block_batch_time = fc::time_point();
            // If spam threshold reached, soft_ban will happen via record_packet_result
        }
    }
}

// ── Periodic task ────────────────────────────────────────────────────

void dlt_p2p_node::periodic_task() {
    if (!_dead_fibers.empty()) {
    std::vector<fc::future<void>> to_clean;
    to_clean.swap(_dead_fibers);
    for (auto& f : to_clean) {
        try {
            // Nu apela ready() — poate crapa dacă promise e distrus
            // cancel_and_wait are acum garda valid() după fix-ul din future.hpp
            f.cancel_and_wait(__FUNCTION__);
        } catch (...) {}
        // Eliberează explicit promise-ul imediat după
        f = fc::future<void>();
    }
}
    // Non-DB-access housekeeping always runs.
    periodic_reconnect_check();
    periodic_lifecycle_timeout_check();
    block_validation_timeout();
    periodic_mempool_cleanup();

    // When block processing is paused (snapshot creation in progress),
    // skip periodic operations that need database read locks.  The snapshot
    // holds a strong read lock for 30-120s; trying to acquire another read
    // lock from this fiber would time out and cascade into peer disconnections.
    if (_block_processing_paused) {
        // Still check banned peers for unban -- no DB access needed.
        for (auto& _peer_item : _peer_states) {
                auto& state = _peer_item.second;
            if (state.lifecycle_state == DLT_PEER_LIFECYCLE_BANNED) {
                auto ban_dur = (state.ban_duration_sec > 0) ? state.ban_duration_sec : BAN_DURATION_SEC;
                auto elapsed = fc::time_point::now() - state.state_entered_time;
                if (elapsed.count() > ban_dur * 1000000) {
                    state.lifecycle_state = DLT_PEER_LIFECYCLE_DISCONNECTED;
                    state.disconnected_since = fc::time_point::now();
                    state.next_reconnect_attempt = fc::time_point::now() + fc::seconds(30);
                    ilog("Unbanning peer ${ep}", ("ep", state.endpoint));
                }
            }
        }
        return;
    }

    // Normal path: all periodic operations run.
    sync_stagnation_check();
    check_sync_catchup();   // P26 fix: periodic catch-up detection
    check_forward_behind(); // P27 fix: detect falling behind in FORWARD mode
    check_forward_stagnation(); // P37 fix: detect head stuck in FORWARD mode
    request_gap_fill();     // P36 fix: fill gaps via exchange-enabled peers
    periodic_peer_exchange();

    // Post-pause catchup: drain queued blocks and/or clear the flag
    // when caught up.
    if (_catchup_after_pause && _delegate) {
        // If there are still queued blocks, drain them first
        if (!_paused_block_queue.empty()) {
            drain_paused_block_queue();
        }

        // After drain (or if queue was empty), check if we're still behind
        uint32_t our_head = _delegate->get_head_block_num();
        bool any_ahead = false;
        for (const auto& _pi : _peer_states) {
            const auto& s = _pi.second;
            if ((s.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE ||
                 s.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) &&
                s.peer_head_num > our_head) {
                any_ahead = true;
                break;
            }
        }
        if (!any_ahead) {
            _catchup_after_pause = false;
            ilog(DLT_LOG_GREEN "Post-pause catchup complete, no gap remaining (head=#${h})" DLT_LOG_RESET,
                 ("h", our_head));
        }
    }

    // Log node status every 1 minute (12 cycles at 5s)
    _status_log_counter++;
    if (_status_log_counter >= 12) {
        _status_log_counter = 0;
        log_node_status();
    }

    // Log peer stats at configured interval (counter tracks seconds, ticks are 5s)
    _stats_log_counter += 5;
    if (_stats_log_counter >= _stats_log_interval_sec) {
        _stats_log_counter = 0;
        log_peer_stats();
    }

    // Check banned peers for unban
    for (auto& _peer_item : _peer_states) {
            auto& state = _peer_item.second;
        if (state.lifecycle_state == DLT_PEER_LIFECYCLE_BANNED) {
            auto ban_dur = (state.ban_duration_sec > 0) ? state.ban_duration_sec : BAN_DURATION_SEC;
            auto elapsed = fc::time_point::now() - state.state_entered_time;
            if (elapsed.count() > ban_dur * 1000000) {
                state.lifecycle_state = DLT_PEER_LIFECYCLE_DISCONNECTED;
                state.disconnected_since = fc::time_point::now();
                state.next_reconnect_attempt = fc::time_point::now() + fc::seconds(30);
                ilog("Unbanning peer ${ep}", ("ep", state.endpoint));
            }
        }
    }
}

// ── Accept loop ─────────────────────────────────────────────────

void dlt_p2p_node::accept_loop() {
    ilog("DLT P2P accept loop started");

    while (_running) {
        try {
            auto sock = std::make_shared<fc::tcp_socket>();
            _tcp_server.accept(*sock);

            if (!_running) {
                sock->close();
                return;
            }

            // Check max connections
            if (_connections.size() >= _max_connections) {
                wlog("Rejecting incoming connection: max connections reached (${m})",
                     ("m", _max_connections));
                sock->close();
                continue;
            }

            peer_id pid = _next_peer_id++;
            _connections[pid] = sock;

            auto& state = _peer_states[pid];
            try {
                state.endpoint = sock->remote_endpoint();
            } catch (...) {
                state.endpoint = fc::ip::endpoint();
            }

            // Per-IP dedup: if we already have an active connection from this
            // IP address, reject the duplicate to prevent broadcast amplification.
            // The existing connection may be outbound (port 2001) or a prior
            // incoming connection on a different ephemeral port.
            fc::ip::address incoming_ip = state.endpoint.get_address();

            // Blocklist check: reject IPs that recently sent oversized/malformed data.
            if (is_ip_blocked((uint32_t)incoming_ip)) {
                dlog("Rejecting blocked IP ${ip} (sent oversized/malformed message)",
                     ("ip", incoming_ip));
                _peer_states.erase(pid);
                _connections.erase(pid);
                sock->close();
                continue;
            }

             /* peer_id existing = find_active_peer_by_ip(incoming_ip);
            if (existing != INVALID_PEER_ID) {
                auto ex_it = _peer_states.find(existing);
                auto ex_ep = (ex_it != _peer_states.end()) ? ex_it->second.endpoint : fc::ip::endpoint();
                dlog(DLT_LOG_DGRAY "Rejecting duplicate incoming connection from ${ip} "
                     "(already connected as peer ${existing} at ${ex_ep})" DLT_LOG_RESET,
                     ("ip", state.endpoint)("existing", existing)("ex_ep", ex_ep));
                _peer_states.erase(pid);
                _connections.erase(pid);
                sock->close();
                continue;
            }*/
             // NOTE: We do NOT reject here based on IP address alone.
            // Multiple nodes behind the same NAT share the same public IP but
            // have different P2P ports and unique node_ids.  Deduplication of
            // truly-duplicate connections (same node reconnecting) is done
            // post-hello in on_dlt_hello() by comparing node_id values.
            
            // Isolated-peers: only accept inbound from configured seed IPs.
            if (_isolated_peers) {
                bool is_seed = false;
                for (const auto& seed_ep : _seed_nodes) {
                    if (seed_ep.get_address() == incoming_ip) { is_seed = true; break; }
                }
                if (!is_seed) {
                    dlog(DLT_LOG_DGRAY "Isolated-peers: rejecting inbound from non-seed ${ip}" DLT_LOG_RESET,
                         ("ip", state.endpoint));
                    _peer_states.erase(pid);
                    _connections.erase(pid);
                    sock->close();
                    continue;
                }
            }

            state.lifecycle_state = DLT_PEER_LIFECYCLE_HANDSHAKING;
            state.state_entered_time = fc::time_point::now();
            state.connected_since = fc::time_point::now();
            state.is_incoming = true;

            // Do NOT add incoming random-port endpoints to _known_peers.
            // Ephemeral client ports (e.g. 35048) are not listening P2P servers
            // and should never be reconnected to or shared in peer exchange.

            // Send hello
            send_message(pid, message(build_hello_message()));
            ilog(DLT_LOG_GREEN "Accepted incoming connection from ${ep}" DLT_LOG_RESET,
                 ("ep", state.endpoint));

            // Start read loop as a fiber
            start_read_loop(pid);

        } catch (const fc::canceled_exception&) {
            ilog("DLT P2P accept loop canceled");
            return;
        } catch (const fc::exception& e) {
            elog("Error in accept loop: ${e}", ("e", e.to_detail_string()));

             // NOTE: do NOT call fc::usleep here — fc::usleep yields the fiber and
           // FC_ASSERT(current_exception == nullptr) fires if called while an
            // exception is still active (Windows x64 SEH / ucrtbase abort).
            // The sleep happens AFTER this catch block, below.
         }
        // Sleep OUTSIDE the catch block so no exception is active when we yield.
        if (_running)
            fc::usleep(fc::seconds(1));
		 
    }
}
        }
    }
}

// ── Read loop (runs as fiber on p2p thread) ──────────────────────────

void dlt_p2p_node::start_read_loop(peer_id peer) {
    auto it = _connections.find(peer);
    if (it == _connections.end() || !it->second) return;

    // Capture socket by value for the fiber
    auto sock = it->second;

    _read_fibers[peer] = _thread->async([this, peer, sock]() -> void {
        auto ep_it_rl = _peer_states.find(peer);
        auto ep_str_rl = (ep_it_rl != _peer_states.end()) ? std::string(ep_it_rl->second.endpoint) : std::to_string(peer);
        ilog(DLT_LOG_DGRAY "Read loop started for peer ${ep}" DLT_LOG_RESET, ("ep", ep_str_rl));

        try {
            while (_running) {
                // Read message header (8 bytes: size + msg_type)
                message_header hdr;
                try {
                    size_t total_read = 0;
                    while (total_read < sizeof(message_header)) {
                        auto r = sock->readsome(
                            reinterpret_cast<char*>(&hdr) + total_read,
                            sizeof(message_header) - total_read);
                        if (r == 0) throw fc::eof_exception();
                        total_read += r;
                    }
                } catch (const fc::eof_exception&) {
                    handle_disconnect(peer, "connection closed");
                    return;
                }

                // Validate message size
                if (hdr.size > MAX_MESSAGE_SIZE) {
                    auto ep_it = _peer_states.find(peer);
                    std::string ep_str;
                    uint32_t peer_ip = 0;
                    if (ep_it != _peer_states.end()) {
                        ep_str = std::string(ep_it->second.endpoint);
                        peer_ip = (uint32_t)ep_it->second.endpoint.get_address();
                    } else {
                        ep_str = std::to_string(peer);
                    }
                    wlog(DLT_LOG_ORANGE "Oversized message (${s} bytes, max=${m}) from peer ${ep} — blocking IP for ${d}s" DLT_LOG_RESET,
                         ("s", hdr.size)("m", MAX_MESSAGE_SIZE)("ep", ep_str)("d", BLOCKED_IP_DURATION_SEC));
                    if (peer_ip) block_incoming_ip(peer_ip, "oversized message (" + std::to_string(hdr.size) + " bytes)");
                    handle_disconnect(peer, "oversized message", true);
                    return;
                }

                // Read message data
                std::vector<char> data(hdr.size);
                if (hdr.size > 0) {
                    try {
                        size_t total_read = 0;
                        while (total_read < hdr.size) {
                            auto r = sock->readsome(
                                data.data() + total_read,
                                hdr.size - total_read);
                            if (r == 0) throw fc::eof_exception();
                            total_read += r;
                        }
                    } catch (const fc::eof_exception&) {
                        handle_disconnect(peer, "connection closed during read");
                        return;
                    }
                }

                // Construct message and dispatch
                message msg;
                msg.size = hdr.size;
                msg.msg_type = hdr.msg_type;
                msg.data = std::move(data);

                // Dispatch and check for stream desync.
                // on_message returns false when a deserialization error
                // left the TCP read cursor at an unknown offset —
                // no recovery possible, disconnect immediately.
                bool msg_ok = on_message(peer, msg);
                if (!msg_ok || !_running) {
                    handle_disconnect(peer, "deserialization error");
                    return;
                }
            }
        } catch (const fc::canceled_exception&) {
            // Normal cancellation during shutdown
        } catch (const fc::exception& e) {
            // Detect common transient TCP errors and log them at info level
            // with a short, human-friendly message instead of the full stack trace.
            const auto& detail = e.to_detail_string();
            bool is_transient =
                detail.find("Connection reset by peer") != std::string::npos ||
                detail.find("forcibly closed") != std::string::npos ||           // Windows WSA 10054
                detail.find("Connection refused") != std::string::npos ||
                detail.find("actively refused") != std::string::npos ||          // Windows WSA 10061
                detail.find("Broken pipe") != std::string::npos ||
                detail.find("connection was aborted") != std::string::npos ||    // Windows WSA 10053
                detail.find("end of stream") != std::string::npos ||
                detail.find("End of file") != std::string::npos ||
                detail.find("Operation aborted") != std::string::npos ||
                detail.find("operation aborted") != std::string::npos ||         // Windows WSA 10004
                detail.find("Network is unreachable") != std::string::npos ||
                detail.find("network is unreachable") != std::string::npos ||    // Windows WSA 10051
                detail.find("No route to host") != std::string::npos ||
                detail.find("Connection timed out") != std::string::npos ||
                detail.find("timed out") != std::string::npos ||
                detail.find("Host is unreachable") != std::string::npos ||
                detail.find("host unreachable") != std::string::npos;            // Windows WSA 10065
            bool is_benign_close =
                detail.find("Bad file descriptor") != std::string::npos ||
                detail.find("bad file descriptor") != std::string::npos ||
                detail.find("invalid argument") != std::string::npos;            // Windows: closed socket reuse

            if (is_benign_close) {
                dlog(DLT_LOG_DGRAY "Peer ${ep} read canceled (socket already closed)" DLT_LOG_RESET,
                     ("ep", ep_str_rl));
            } else if (is_transient) {
                ilog(DLT_LOG_DGRAY "Peer ${ep} disconnected: ${msg}" DLT_LOG_RESET,
                     ("ep", ep_str_rl)("msg", std::string(e.what())));
            } else {
                wlog("Read loop error for peer ${ep}: ${e}",
                     ("ep", ep_str_rl)("e", detail));
            }
            handle_disconnect(peer, "read error");
        }

        ilog("Read loop ended for peer ${ep}", ("ep", ep_str_rl));
    }, "dlt read_loop");
}

} // namespace network
} // namespace graphene
