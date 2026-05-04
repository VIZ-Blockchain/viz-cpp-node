#include <graphene/network/dlt_p2p_node.hpp>
#include <graphene/network/config.hpp>

#include <fc/network/resolve.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/io/raw.hpp>
#include <fc/log/log_message.hpp>
#include <fc/thread/thread.hpp>

#include <algorithm>
#include <random>
#include <chrono>

namespace graphene {
namespace network {

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

// ── Lifecycle ────────────────────────────────────────────────────────

void dlt_p2p_node::start() {
    _running = true;

    try {
        if (_listen_endpoint.port() != 0) {
            _tcp_server.listen(_listen_endpoint);
            ilog("DLT P2P listening on ${ep}", ("ep", _listen_endpoint));
        }
    } catch (const fc::exception& e) {
        wlog("DLT P2P failed to listen on ${ep}: ${e}", ("ep", _listen_endpoint)("e", e.to_detail_string()));
    }

    // Add seed nodes to known peers
    for (const auto& ep : _seed_nodes) {
        dlt_known_peer kp;
        kp.endpoint = ep;
        kp.node_id = node_id_t();
        _known_peers.insert(kp);
    }

    // Connect to seed nodes
    for (const auto& ep : _seed_nodes) {
        connect_to_peer(ep);
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
            auto& id = _fib_item.first;
            auto& fiber = _fib_item.second;
        try { if (fiber.valid()) fiber.cancel_and_wait(__FUNCTION__); } catch (...) {}
    }
    _read_fibers.clear();

    try {
        _tcp_server.close();
    } catch (...) {}

    for (auto& _conn_item : _connections) {
            auto& id = _conn_item.first;
            auto& conn = _conn_item.second;
        try { if (conn) conn->close(); } catch (...) {}
    }
    _connections.clear();
    _peer_states.clear();
    ilog("DLT P2P node closed");
}

// ── Connection management ────────────────────────────────────────────

void dlt_p2p_node::connect_to_peer(const fc::ip::endpoint& ep) {
    if (_connections.size() >= _max_connections) return;

    // Don't connect to already-connected peer
    for (auto& _peer_item : _peer_states) {
            auto& id = _peer_item.first;
            auto& state = _peer_item.second;
        if (state.endpoint == ep && state.lifecycle_state != DLT_PEER_LIFECYCLE_DISCONNECTED
            && state.lifecycle_state != DLT_PEER_LIFECYCLE_BANNED) {
            return;
        }
    }

    peer_id pid = _next_peer_id++;
    auto& state = _peer_states[pid];
    state.endpoint = ep;
    state.lifecycle_state = DLT_PEER_LIFECYCLE_CONNECTING;
    state.state_entered_time = fc::time_point::now();

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
        wlog("Failed to connect to ${ep}: ${e}", ("ep", ep)("e", e.to_detail_string()));
        state.lifecycle_state = DLT_PEER_LIFECYCLE_DISCONNECTED;
        state.disconnected_since = fc::time_point::now();
        state.next_reconnect_attempt = fc::time_point::now() + fc::seconds(30);
    }
}

void dlt_p2p_node::handle_disconnect(peer_id peer, const std::string& reason) {
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

    state.lifecycle_state = DLT_PEER_LIFECYCLE_DISCONNECTED;
    state.disconnected_since = fc::time_point::now();
    state.expected_next_block = 0;

    // Double backoff, cap at max
    state.reconnect_backoff_sec = std::min(state.reconnect_backoff_sec * 2,
                                            dlt_peer_state::MAX_RECONNECT_BACKOFF_SEC);

    // Add jitter (±25%)
    uint32_t jitter_range = state.reconnect_backoff_sec / 2;
    uint32_t jitter = (jitter_range > 0) ? (rand() % jitter_range) - (jitter_range / 2) : 0;
    state.next_reconnect_attempt = fc::time_point::now() + fc::seconds(state.reconnect_backoff_sec + jitter);

    // Cancel read fiber
    auto fiber_it = _read_fibers.find(peer);
    if (fiber_it != _read_fibers.end()) {
        try { if (fiber_it->second.valid()) fiber_it->second.cancel_and_wait(__FUNCTION__); } catch (...) {}
        _read_fibers.erase(fiber_it);
    }

    // Close connection
    auto conn_it = _connections.find(peer);
    if (conn_it != _connections.end()) {
        try { if (conn_it->second) conn_it->second->close(); } catch (...) {}
        _connections.erase(conn_it);
    }

    wlog("Disconnected from peer ${ep}: ${reason} (backoff=${b}s)",
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
                ilog("Reconnecting to peer ${ep} (backoff=${b}s)",
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
}

void dlt_p2p_node::periodic_lifecycle_timeout_check() {
    for (auto& _peer_item : _peer_states) {
            auto& id = _peer_item.first;
            auto& state = _peer_item.second;
        if (state.has_lifecycle_timeout()) {
            wlog("Peer ${ep} timed out in state ${s}",
                 ("ep", state.endpoint)("s", (int)state.lifecycle_state));
            handle_disconnect(id, "lifecycle timeout");
        }
    }
}

// ── Message send/receive ─────────────────────────────────────────────

void dlt_p2p_node::send_message(peer_id peer, const message& msg) {
    auto it = _connections.find(peer);
    if (it == _connections.end() || !it->second) return;

    try {
        // Write wire format: [8-byte header][data bytes]
        // Do NOT use fc::raw::pack(msg) which adds varint length prefix to data vector
        message_header hdr;
        hdr.size = static_cast<uint32_t>(msg.data.size());
        hdr.msg_type = msg.msg_type;

        it->second->writesome(reinterpret_cast<const char*>(&hdr), sizeof(message_header));
        if (!msg.data.empty()) {
            it->second->writesome(msg.data.data(), msg.data.size());
        }
        it->second->flush();
    } catch (const fc::exception& e) {
        wlog("Failed to send to peer ${p}: ${e}", ("p", peer)("e", e.to_detail_string()));
        handle_disconnect(peer, "send failed");
    }
}

void dlt_p2p_node::send_to_all_our_fork_peers(const message& msg, peer_id exclude) {
    for (auto& _peer_item : _peer_states) {
            auto& id = _peer_item.first;
            auto& state = _peer_item.second;
        if (id == exclude) continue;
        if (state.exchange_enabled && state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE) {
            send_message(id, msg);
        }
    }
}

void dlt_p2p_node::on_message(peer_id peer, const message& msg) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;

    // Block processing pause check
    if (_block_processing_paused) {
        // Only allow hello/hello_reply during pause
        if (msg.msg_type != dlt_hello_message_type && msg.msg_type != dlt_hello_reply_message_type) {
            return;
        }
    }

    try {
        switch (msg.msg_type) {
            case dlt_hello_message_type:
                on_dlt_hello(peer, msg.as<dlt_hello_message>());
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
                on_dlt_block_range_reply(peer, msg.as<dlt_block_range_reply_message>());
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
            default:
                wlog("Unknown DLT message type ${t} from peer ${p}", ("t", msg.msg_type)("p", peer));
                record_packet_result(peer, false);
                break;
        }
    } catch (const fc::exception& e) {
        wlog("Error processing message type ${t} from peer ${p}: ${e}",
             ("t", msg.msg_type)("p", peer)("e", e.to_detail_string()));
        record_packet_result(peer, false);
    }
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
    return hello;
}

dlt_hello_reply_message dlt_p2p_node::build_hello_reply(peer_id peer, const dlt_hello_message& hello) const {
    dlt_hello_reply_message reply;
    reply.our_dlt_earliest = _delegate->get_dlt_earliest_block();
    reply.our_dlt_latest = _delegate->get_dlt_latest_block();
    reply.our_fork_status = _fork_status;
    reply.our_node_status = _node_status;

    // Check fork alignment
    block_id_type recognized_head, recognized_lib;
    reply.fork_alignment = check_fork_alignment(hello.head_block_id, hello.lib_block_id,
                                                  recognized_head, recognized_lib);
    reply.initiator_head_seen = recognized_head;
    reply.initiator_lib_seen = recognized_lib;
    reply.exchange_enabled = reply.fork_alignment; // exchange enabled if fork-aligned

    return reply;
}

bool dlt_p2p_node::check_fork_alignment(const block_id_type& head_id, const block_id_type& lib_id,
                                          block_id_type& recognized_head_out, block_id_type& recognized_lib_out) const {
    if (!_delegate) return false;

    // Check if peer's head is known to us
    if (_delegate->is_block_known(head_id)) {
        recognized_head_out = head_id;
    }
    // Check if peer's LIB is known to us
    if (_delegate->is_block_known(lib_id)) {
        recognized_lib_out = lib_id;
    }

    // Fork alignment: at least one of head/LIB must be known
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
        if (reply.exchange_enabled || _node_status == DLT_NODE_STATUS_SYNC) {
            state.lifecycle_state = DLT_PEER_LIFECYCLE_ACTIVE;
            state.state_entered_time = fc::time_point::now();
        }
    }

    record_packet_result(peer, true);

    // If we're in SYNC mode, request blocks
    if (_node_status == DLT_NODE_STATUS_SYNC && reply.exchange_enabled) {
        request_blocks_from_peer(peer);
    }

    ilog(DLT_LOG_WHITE "Received DLT hello from ${ep}: head=#${hn} lib=#${ln} fork=${f} node=${ns} exchange=${ex}" DLT_LOG_RESET,
         ("ep", state.endpoint)("hn", hello.head_block_num)("ln", hello.lib_block_num)
         ("f", (int)hello.fork_status)("ns", (int)hello.node_status)("ex", reply.exchange_enabled));
}

void dlt_p2p_node::on_dlt_hello_reply(peer_id peer, const dlt_hello_reply_message& reply) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;
    auto& state = it->second;

    state.exchange_enabled = reply.exchange_enabled;
    state.fork_alignment = reply.fork_alignment;
    state.recognized_head = reply.initiator_head_seen;
    state.recognized_lib = reply.initiator_lib_seen;

    // Transition to active
    if (state.lifecycle_state == DLT_PEER_LIFECYCLE_HANDSHAKING ||
        state.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) {
        state.lifecycle_state = DLT_PEER_LIFECYCLE_ACTIVE;
        state.state_entered_time = fc::time_point::now();
    }

    record_packet_result(peer, true);

    // If we're in SYNC mode and exchange is enabled, start fetching blocks
    if (_node_status == DLT_NODE_STATUS_SYNC && reply.exchange_enabled) {
        request_blocks_from_peer(peer);
    } else if (!reply.exchange_enabled) {
        ilog(DLT_LOG_ORANGE "Peer ${ep} is NOT on our fork (fork_alignment=false)" DLT_LOG_RESET,
             ("ep", state.endpoint));
    }
}

// ── Block request/reply handlers ─────────────────────────────────────

void dlt_p2p_node::request_blocks_from_peer(peer_id peer) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;

    uint32_t our_head = _delegate->get_head_block_num();
    uint32_t peer_latest = it->second.peer_dlt_latest;

    if (our_head >= peer_latest) {
        // We're caught up with this peer
        if (_node_status == DLT_NODE_STATUS_SYNC) {
            // Check if ALL peers are caught up
            bool all_caught_up = true;
            for (auto& _peer_item : _peer_states) {
            auto& id = _peer_item.first;
            auto& s = _peer_item.second;
                if (s.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE && s.peer_dlt_latest > our_head) {
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

    uint32_t start = our_head + 1;
    uint32_t end = std::min(start + 200 - 1, peer_latest); // max 200 blocks per request

    dlt_get_block_range_message req;
    req.start_block_num = start;
    req.end_block_num = end;
    req.prev_block_id = _delegate->get_head_block_id();

    it->second.pending_sync_start = start;
    it->second.pending_sync_end = end;
    it->second.lifecycle_state = DLT_PEER_LIFECYCLE_SYNCING;
    it->second.state_entered_time = fc::time_point::now();

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

    for (uint32_t n = req.start_block_num; n <= req.end_block_num && n <= our_latest; ++n) {
        auto block = _delegate->read_block_by_num(n);
        if (block.valid()) {
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
    auto& state = it->second;
    state.pending_block_batch_time = fc::time_point::now();
    for (const auto& block : reply.blocks) {
        if (_block_processing_paused) break;

        // Validate block ordering
        if (state.expected_next_block != 0 && block.block_num() != state.expected_next_block) {
            wlog(DLT_LOG_RED "Block #${n} from ${ep} out of order (expected #${e})" DLT_LOG_RESET,
                 ("n", block.block_num())("ep", state.endpoint)("e", state.expected_next_block));
            record_packet_result(peer, false);
            continue;
        }

        bool caused_fork = _delegate->accept_block(block, /*sync_mode=*/(_node_status == DLT_NODE_STATUS_SYNC));
        any_block_applied = true;
        _last_block_received_time = fc::time_point::now();
        _last_network_block_time = fc::time_point::now();

        state.expected_next_block = block.block_num() + 1;

        on_block_applied(block, caused_fork);
    }
    state.pending_block_batch_time = fc::time_point();

    record_packet_result(peer, any_block_applied);

    // If SYNC and last block, transition to FORWARD
    if (_node_status == DLT_NODE_STATUS_SYNC) {
        if (reply.is_last) {
            transition_to_forward();
        } else if (reply.last_block_next_available > 0) {
            // Continue fetching
            it->second.peer_dlt_earliest = reply.last_block_next_available;
            request_blocks_from_peer(peer);
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

    // Validate block ordering
    if (state.expected_next_block != 0 && reply.block.block_num() != state.expected_next_block) {
        wlog(DLT_LOG_RED "Block #${n} from ${ep} out of order (expected #${e})" DLT_LOG_RESET,
             ("n", reply.block.block_num())("ep", state.endpoint)("e", state.expected_next_block));
        record_packet_result(peer, false);
        return;
    }

    bool caused_fork = _delegate->accept_block(reply.block, false);
    _last_network_block_time = fc::time_point::now();
    _last_block_received_time = fc::time_point::now();

    state.expected_next_block = reply.block.block_num() + 1;

    on_block_applied(reply.block, caused_fork);
    record_packet_result(peer, true);

    // Retranslate to our-fork peers
    send_to_all_our_fork_peers(message(dlt_block_reply_message(reply)), peer);
}

void dlt_p2p_node::on_dlt_not_available(peer_id peer, const dlt_not_available_message& msg) {
    ilog(DLT_LOG_ORANGE "Peer ${p} doesn't have block #${n}" DLT_LOG_RESET,
         ("p", peer)("n", msg.block_num));
    record_packet_result(peer, true);
}

void dlt_p2p_node::on_dlt_fork_status(peer_id peer, const dlt_fork_status_message& msg) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;

    it->second.peer_fork_status = msg.fork_status;
    it->second.peer_head_id = msg.head_block_id;
    it->second.peer_head_num = msg.head_block_num;
    record_packet_result(peer, true);
}

// ── Peer exchange handlers ───────────────────────────────────────────

void dlt_p2p_node::on_dlt_peer_exchange_request(peer_id peer, const dlt_peer_exchange_request&) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;
    auto& state = it->second;

    // Rate-limit check
    if (state.is_peer_exchange_rate_limited()) {
        auto elapsed = fc::time_point::now() - state.last_peer_exchange_request_time;
        uint32_t wait = dlt_peer_state::PEER_EXCHANGE_COOLDOWN_SEC -
                        static_cast<uint32_t>(elapsed.count() / 1000000);
        send_message(peer, message(dlt_peer_exchange_rate_limited{wait}));
        return;
    }

    state.last_peer_exchange_request_time = fc::time_point::now();

    // Collect "our fork" peers (exchange_enabled, active, min uptime)
    dlt_peer_exchange_reply reply;
    auto now = fc::time_point::now();
    for (auto& _peer_item : _peer_states) {
            auto& id = _peer_item.first;
            auto& s = _peer_item.second;
        if (id == peer) continue;
        if (!s.exchange_enabled) continue;
        if (s.lifecycle_state != DLT_PEER_LIFECYCLE_ACTIVE) continue;
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
    ilog(DLT_LOG_DGRAY "Peer ${p} rate-limited our exchange request, wait ${w}s" DLT_LOG_RESET,
         ("p", peer)("w", msg.wait_seconds));
    record_packet_result(peer, true);
}

// ── Transaction handler ──────────────────────────────────────────────

void dlt_p2p_node::on_dlt_transaction(peer_id peer, const dlt_transaction_message& msg) {
    bool accepted = add_to_mempool(msg.trx, /*from_peer=*/true, peer);
    if (accepted) {
        ilog(DLT_LOG_DGRAY "Got transaction ${id} from peer ${p}" DLT_LOG_RESET,
             ("id", msg.trx.id())("p", peer));
    }
}

// ── Broadcast methods ────────────────────────────────────────────────

void dlt_p2p_node::broadcast_block(const signed_block& block) {
    dlt_block_reply_message reply;
    reply.block = block;
    reply.next_available = 0;
    reply.is_last = true;
    send_to_all_our_fork_peers(message(reply));
}

void dlt_p2p_node::broadcast_block_post_validation(
    const block_id_type& block_id,
    const std::string& witness_account,
    const signature_type& witness_signature) {
    // For now, send as fork_status message with block_id
    dlt_fork_status_message msg;
    msg.fork_status = _fork_status;
    msg.head_block_id = block_id;
    msg.head_block_num = graphene::protocol::block_header::num_from_id(block_id);
    send_to_all_our_fork_peers(message(msg));
}

void dlt_p2p_node::broadcast_transaction(const signed_transaction& trx) {
    add_to_mempool(trx, /*from_peer=*/false, INVALID_PEER_ID);
    dlt_transaction_message msg;
    msg.trx = trx;
    send_to_all_our_fork_peers(message(msg));
}

void dlt_p2p_node::broadcast_chain_status() {
    auto hello = build_hello_message();
    // Send as fork_status for lightweight status update
    dlt_fork_status_message msg;
    msg.fork_status = _fork_status;
    msg.head_block_id = hello.head_block_id;
    msg.head_block_num = hello.head_block_num;
    send_to_all_our_fork_peers(message(msg));
}

// ── State queries ────────────────────────────────────────────────────

uint32_t dlt_p2p_node::get_connection_count() const {
    uint32_t count = 0;
    for (auto& _peer_item : _peer_states) {
            auto& id = _peer_item.first;
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
    auto hello = build_hello_message();
    for (auto& _peer_item : _peer_states) {
            auto& id = _peer_item.first;
            auto& state = _peer_item.second;
        if (state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE ||
            state.lifecycle_state == DLT_PEER_LIFECYCLE_SYNCING) {
            send_message(id, message(hello));
            request_blocks_from_peer(id);
        }
    }
}

void dlt_p2p_node::trigger_resync() {
    ilog(DLT_LOG_GREEN "DLT P2P: resync triggered" DLT_LOG_RESET);
    // Re-send hello to all active peers
    auto hello = build_hello_message();
    for (auto& _peer_item : _peer_states) {
            auto& id = _peer_item.first;
            auto& state = _peer_item.second;
        if (state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE) {
            send_message(id, message(hello));
        }
    }
}

void dlt_p2p_node::reconnect_seeds() {
    ilog(DLT_LOG_GREEN "DLT P2P: reconnecting seed nodes" DLT_LOG_RESET);
    for (const auto& ep : _seed_nodes) {
        // Reset backoff for seeds
        for (auto& _peer_item : _peer_states) {
            auto& id = _peer_item.first;
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
    ilog("DLT P2P: block processing paused");
}

void dlt_p2p_node::resume_block_processing() {
    _block_processing_paused = false;
    ilog("DLT P2P: block processing resumed");
}

bool dlt_p2p_node::is_on_majority_fork() const {
    return _fork_status != DLT_FORK_STATUS_MINORITY;
}

// ── Node status transitions ──────────────────────────────────────────

void dlt_p2p_node::transition_to_forward() {
    if (_node_status == DLT_NODE_STATUS_FORWARD) return;
    _node_status = DLT_NODE_STATUS_FORWARD;
    _sync_stagnation_retries = 0;
    ilog(DLT_LOG_GREEN "=== DLT P2P: transitioning to FORWARD mode ===" DLT_LOG_RESET);

    // Revalidate provisional mempool entries
    for (auto it = _mempool_by_id.begin(); it != _mempool_by_id.end(); ) {
        if (it->second.is_provisional) {
            if (!is_tapos_valid(it->second.trx)) {
                _mempool_total_bytes -= it->second.estimated_size();
                _mempool_by_expiry.erase(
                    std::make_pair(it->second.trx.expiration, it->first));
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

    // Check TaPoS validity
    if (!is_tapos_valid(trx)) {
        if (from_peer && sender != INVALID_PEER_ID) record_packet_result(sender, false);
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
            _mempool_by_expiry.erase(std::make_pair(it->second.trx.expiration, trx_id));
            _mempool_by_id.erase(it);
        }
    }
}

void dlt_p2p_node::prune_mempool_on_fork_switch() {
    for (auto it = _mempool_by_id.begin(); it != _mempool_by_id.end(); ) {
        if (!is_tapos_valid(it->second.trx)) {
            _mempool_total_bytes -= it->second.estimated_size();
            _mempool_by_expiry.erase(
                std::make_pair(it->second.trx.expiration, it->first));
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
    return _delegate->is_tapos_block_known(trx.ref_block_num, trx.ref_block_prefix);
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
        soft_ban_peer(peer);
        return false;
    }
    return true;
}

void dlt_p2p_node::soft_ban_peer(peer_id peer) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;

    wlog(DLT_LOG_RED "Soft-banning peer ${ep} for ${d}s" DLT_LOG_RESET,
         ("ep", it->second.endpoint)("d", BAN_DURATION_SEC));

    it->second.lifecycle_state = DLT_PEER_LIFECYCLE_BANNED;
    it->second.state_entered_time = fc::time_point::now();
    it->second.reconnect_backoff_sec = BAN_DURATION_SEC;
    it->second.next_reconnect_attempt = fc::time_point::now() + fc::seconds(BAN_DURATION_SEC);

    auto conn_it = _connections.find(peer);
    if (conn_it != _connections.end()) {
        try { if (conn_it->second) conn_it->second->close(); } catch (...) {}
        _connections.erase(conn_it);
    }
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
    if (_node_status != DLT_NODE_STATUS_FORWARD) return;

    // Pick a random active peer to request exchange from
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

    size_t idx = rand() % candidates.size();
    send_message(candidates[idx], message(dlt_peer_exchange_request()));
}

// ── Subnet diversity ─────────────────────────────────────────────────

uint32_t dlt_p2p_node::count_peers_in_subnet(const fc::ip::address& addr) const {
    uint32_t count = 0;
    for (auto& _peer_item : _peer_states) {
            auto& id = _peer_item.first;
            auto& state = _peer_item.second;
        if (is_same_subnet(state.endpoint.get_address(), addr)) {
            count++;
        }
    }
    return count;
}

bool dlt_p2p_node::is_same_subnet(const fc::ip::address& a, const fc::ip::address& b) const {
    // /24 subnet check — compare first 3 bytes
    auto a_data = a.data();
    auto b_data = b.data();
    return a_data[0] == b_data[0] && a_data[1] == b_data[1] && a_data[2] == b_data[2];
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
    periodic_reconnect_check();
    periodic_lifecycle_timeout_check();
    sync_stagnation_check();
    block_validation_timeout();
    periodic_mempool_cleanup();
    periodic_peer_exchange();

    // Check banned peers for unban
    for (auto& _peer_item : _peer_states) {
            auto& id = _peer_item.first;
            auto& state = _peer_item.second;
        if (state.lifecycle_state == DLT_PEER_LIFECYCLE_BANNED) {
            auto elapsed = fc::time_point::now() - state.state_entered_time;
            if (elapsed.count() > BAN_DURATION_SEC * 1000000) {
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
            state.lifecycle_state = DLT_PEER_LIFECYCLE_HANDSHAKING;
            state.state_entered_time = fc::time_point::now();
            state.connected_since = fc::time_point::now();

            // Add to known peers
            dlt_known_peer kp;
            kp.endpoint = state.endpoint;
            kp.node_id = node_id_t();
            _known_peers.insert(kp);

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
            if (_running) fc::usleep(fc::seconds(1));
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
        ilog("Read loop started for peer ${p}", ("p", peer));

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
                    wlog("Oversized message (${s} bytes) from peer ${p}, disconnecting",
                         ("s", hdr.size)("p", peer));
                    handle_disconnect(peer, "oversized message");
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

                on_message(peer, msg);
            }
        } catch (const fc::canceled_exception&) {
            // Normal cancellation during shutdown
        } catch (const fc::exception& e) {
            wlog("Read loop error for peer ${p}: ${e}",
                 ("p", peer)("e", e.to_detail_string()));
            handle_disconnect(peer, "read error");
        }

        ilog("Read loop ended for peer ${p}", ("p", peer));
    }, "dlt read_loop");
}

} // namespace network
} // namespace graphene
