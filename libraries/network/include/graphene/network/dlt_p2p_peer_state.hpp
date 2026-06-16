#pragma once

#include <graphene/network/dlt_p2p_messages.hpp>
#include <graphene/network/message.hpp>

#include <fc/time.hpp>
#include <fc/io/raw.hpp>

#include <cstdint>
#include <algorithm>
#include <deque>
#include <set>
#include <string>
#include <vector>

namespace graphene {
namespace network {

// ── Per-peer state for DLT P2P ──────────────────────────────────────
struct dlt_peer_state {
    // Identity
    fc::ip::endpoint             endpoint;
    node_id_t                    node_id;

    // Lifecycle
    dlt_peer_lifecycle_state     lifecycle_state = DLT_PEER_LIFECYCLE_DISCONNECTED;
    fc::time_point               state_entered_time;       // when we entered current lifecycle state
    fc::time_point               connected_since;          // when connection was established

    // Peer's chain state (from hello, fork_status, or block relay)
    // IMPORTANT: These values are NOT real-time. They are snapshots from the
    // last communication (hello handshake, fork_status message, or block
    // relay). The peer's actual chain head may be significantly higher.
    // Do not treat peer_head_num as authoritative current state.
    block_id_type                peer_head_id;
    uint32_t                     peer_head_num = 0;
    block_id_type                peer_lib_id;
    uint32_t                     peer_lib_num = 0;
    uint32_t                     peer_dlt_earliest = 0;
    uint32_t                     peer_dlt_latest = 0;
    bool                         peer_emergency_active = false;
    bool                         peer_has_emergency_key = false;
    uint8_t                      peer_fork_status = DLT_FORK_STATUS_NORMAL;
    uint8_t                      peer_node_status = DLT_NODE_STATUS_SYNC;

    // Our view of this peer
    bool                         exchange_enabled = false;  // "our fork"
    bool                         fork_alignment = false;    // true = blocks link to ours
    block_id_type                recognized_head;           // which of peer's blocks we recognize
    block_id_type                recognized_lib;
    bool                         is_incoming = false;       // true = accepted in accept_loop (random port)

    // Anti-spam
    uint32_t                     spam_strikes = 0;
    fc::time_point               last_good_packet_time;

    // Ban tracking
    std::string                  ban_reason;              // why we (or they) were banned
    uint32_t                     ban_duration_sec = 0;    // actual ban duration (may differ from BAN_DURATION_SEC for remote bans)

    // Peer exchange rate-limiting (sliding window)
    uint32_t                     peer_exchange_request_count = 0;
    fc::time_point               peer_exchange_window_start;
    static constexpr uint32_t    PEER_EXCHANGE_MAX_REQUESTS = 3;
    static constexpr uint32_t    PEER_EXCHANGE_WINDOW_SEC = 300; // 5 min

    // Block ordering validation
    uint32_t                     expected_next_block = 0;
    fc::time_point               pending_block_batch_time;  // when batch was received
    static constexpr uint32_t    PENDING_BATCH_TIMEOUT_SEC = 30;

    // Reconnection tracking
    fc::time_point               disconnected_since;
    fc::time_point               next_reconnect_attempt;
    uint32_t                     reconnect_backoff_sec = 30;
    static constexpr uint32_t    INITIAL_RECONNECT_BACKOFF_SEC = 30;
    static constexpr uint32_t    MAX_RECONNECT_BACKOFF_SEC = 3600;
    fc::microseconds             last_connection_duration;  // for backoff reset on stable >5min

    // Misc
    uint32_t                     pending_sync_start = 0;   // what block range we asked for
    uint32_t                     pending_sync_end = 0;
    bool                         range_fallback_mode = false; // single-block mode after range deserialization error
    uint32_t                     fork_only_batch_count = 0;  // consecutive range replies with no blocks applied
    static constexpr uint32_t   FORK_ONLY_BATCH_LIMIT = 5;

    // Block echo suppression: ring buffer of recent block IDs this peer
    // is known to have.  Updated when we send a block to the peer or when
    // the peer sends us a block.  Prevents retransmitting a block to a
    // peer that already has it.
    static constexpr size_t        KNOWN_BLOCKS_WINDOW = 20;
    std::vector<block_id_type>     known_blocks;

    // Send queue: serialized wire frames waiting to be written to this
    // peer's socket.  When a fiber is already writing to the socket
    // (tracked by _peer_sending in dlt_p2p_node), new messages are
    // appended here.  The active writer drains the queue after each
    // successful write.
    std::deque<std::vector<char>>  send_queue;
    static constexpr size_t        SEND_QUEUE_MAX_DEPTH = 150;
    uint32_t                       send_queue_total = 0;   // lifetime queued (stats)
    uint32_t                       send_queue_dropped = 0; // lifetime dropped (stats)

    bool has_block(const block_id_type& id) const {
        return std::find(known_blocks.begin(), known_blocks.end(), id) != known_blocks.end();
    }

    void record_known_block(const block_id_type& id) {
        if (known_blocks.size() >= KNOWN_BLOCKS_WINDOW) {
            known_blocks.erase(known_blocks.begin());
        }
        known_blocks.push_back(id);
    }

    // ── Helper: check if lifecycle state has timed out ───────────
    bool has_lifecycle_timeout() const {
        auto elapsed = fc::time_point::now() - state_entered_time;
        switch (lifecycle_state) {
            case DLT_PEER_LIFECYCLE_CONNECTING:
                return elapsed.count() > 5000000; // 5s
            case DLT_PEER_LIFECYCLE_HANDSHAKING:
                return elapsed.count() > 10000000; // 10s
            default:
                return false;
        }
    }

    // ── Helper: check if peer exchange is rate-limited (sliding window) ──
    bool is_peer_exchange_rate_limited() const {
        if (peer_exchange_window_start == fc::time_point()) return false;
        auto elapsed = (fc::time_point::now() - peer_exchange_window_start).count();
        if (elapsed >= PEER_EXCHANGE_WINDOW_SEC * 1000000) return false;
        return peer_exchange_request_count >= PEER_EXCHANGE_MAX_REQUESTS;
    }

    // ── Helper: record a peer exchange request (increments window counter) ──
    void record_peer_exchange_request() {
        auto now = fc::time_point::now();
        if (peer_exchange_window_start == fc::time_point() ||
            (now - peer_exchange_window_start).count() >= PEER_EXCHANGE_WINDOW_SEC * 1000000) {
            peer_exchange_request_count = 1;
            peer_exchange_window_start = now;
        } else {
            peer_exchange_request_count++;
        }
    }

    // ── Helper: remaining seconds in rate-limit window ──────────────
    uint32_t peer_exchange_wait_seconds() const {
        if (peer_exchange_window_start == fc::time_point()) return 0;
        auto elapsed = static_cast<uint32_t>(
            (fc::time_point::now() - peer_exchange_window_start).count() / 1000000);
        return (elapsed < PEER_EXCHANGE_WINDOW_SEC) ? (PEER_EXCHANGE_WINDOW_SEC - elapsed) : 0;
    }

    // ── Helper: check if pending block batch has timed out ───────
    bool has_pending_batch_timeout() const {
        if (pending_block_batch_time == fc::time_point()) return false;
        return (fc::time_point::now() - pending_block_batch_time).count()
               > PENDING_BATCH_TIMEOUT_SEC * 1000000;
    }
};

// ── Known peer entry (persistent across reconnects) ─────────────────
// Identity is the endpoint only.  node_id is kept as best-effort metadata
// (last seen identity) but MUST NOT participate in set membership: with
// random per-session node_ids (NAT support), keying on (endpoint, node_id)
// causes stale entries to accumulate after every peer restart and silently
// suppresses re-discovery via peer-exchange replies.  last_seen is used by
// periodic_known_peers_cleanup() to evict long-unreferenced entries.
// node_id and last_seen are mutable so they can be refreshed on the existing
// std::set entry without re-keying.
struct dlt_known_peer {
    fc::ip::endpoint   endpoint;
    mutable node_id_t  node_id;
    mutable fc::time_point last_seen;

    bool operator<(const dlt_known_peer& other) const {
        return endpoint < other.endpoint;
    }

    bool operator==(const dlt_known_peer& other) const {
        return endpoint == other.endpoint;
    }
};

// ── Mempool entry ───────────────────────────────────────────────────
struct dlt_mempool_entry {
    graphene::protocol::transaction_id_type          trx_id;
    signed_transaction          trx;
    fc::time_point              received_time;
    bool                        is_provisional = false;  // true if received during SYNC
    block_id_type               expected_head;           // our head at time of receipt

    uint32_t                    estimated_size() const {
        return static_cast<uint32_t>(fc::raw::pack_size(trx));
    }
};

// ── Fork resolution state ───────────────────────────────────────────
struct dlt_fork_resolution_state {
    block_id_type current_winner_tip;
    uint32_t      consecutive_blocks_as_winner = 0;
    static constexpr uint32_t CONFIRMATION_BLOCKS = 6;

    bool is_confirmed() const {
        return consecutive_blocks_as_winner >= CONFIRMATION_BLOCKS;
    }
};

// ── Fork branch info (for resolution) ───────────────────────────────
struct dlt_fork_branch_info {
    block_id_type                    tip;
    std::vector<block_id_type>       blocks;
    std::set<std::string>            witnesses;  // account names
    uint64_t                         total_vote_weight = 0;
    bool                             has_emergency_blocks = false;
    uint32_t                         block_count = 0;
};

} // namespace network
} // namespace graphene

FC_REFLECT((graphene::network::dlt_known_peer), (endpoint)(node_id))
