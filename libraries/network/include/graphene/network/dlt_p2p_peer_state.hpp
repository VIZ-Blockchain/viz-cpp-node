#pragma once

#include <graphene/network/dlt_p2p_messages.hpp>
#include <graphene/network/message.hpp>

#include <fc/time.hpp>
#include <fc/io/raw.hpp>

#include <cstdint>
#include <set>

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

    // Peer's chain state (from hello)
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

    // Anti-spam
    uint32_t                     spam_strikes = 0;
    fc::time_point               last_good_packet_time;

    // Peer exchange rate-limiting
    fc::time_point               last_peer_exchange_request_time;
    static constexpr uint32_t    PEER_EXCHANGE_COOLDOWN_SEC = 600; // 10 min

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

    // ── Helper: check if peer exchange is rate-limited ───────────
    bool is_peer_exchange_rate_limited() const {
        if (last_peer_exchange_request_time == fc::time_point()) return false;
        return (fc::time_point::now() - last_peer_exchange_request_time).count()
               < PEER_EXCHANGE_COOLDOWN_SEC * 1000000;
    }

    // ── Helper: check if pending block batch has timed out ───────
    bool has_pending_batch_timeout() const {
        if (pending_block_batch_time == fc::time_point()) return false;
        return (fc::time_point::now() - pending_block_batch_time).count()
               > PENDING_BATCH_TIMEOUT_SEC * 1000000;
    }
};

// ── Known peer entry (persistent across reconnects) ─────────────────
struct dlt_known_peer {
    fc::ip::endpoint  endpoint;
    node_id_t         node_id;

    bool operator<(const dlt_known_peer& other) const {
        if (endpoint != other.endpoint) return endpoint < other.endpoint;
        return node_id < other.node_id;
    }

    bool operator==(const dlt_known_peer& other) const {
        return endpoint == other.endpoint && node_id == other.node_id;
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
