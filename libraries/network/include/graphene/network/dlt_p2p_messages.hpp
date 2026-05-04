#pragma once

#include <graphene/protocol/block.hpp>
#include <graphene/protocol/transaction.hpp>

#include <fc/crypto/elliptic.hpp>
#include <fc/network/ip.hpp>
#include <fc/reflect/reflect.hpp>
#include <fc/time.hpp>

#include <vector>
#include <string>

namespace graphene {
namespace network {

using graphene::protocol::signed_transaction;
using graphene::protocol::signed_block;
using graphene::protocol::block_id_type;
using graphene::protocol::signature_type;
using fc::ecc::public_key_data;

typedef public_key_data node_id_t;

// ── DLT P2P message type IDs ────────────────────────────────────────
enum dlt_message_type_enum {
    dlt_hello_message_type               = 5100,
    dlt_hello_reply_message_type         = 5101,
    dlt_range_request_message_type       = 5102,
    dlt_range_reply_message_type         = 5103,
    dlt_get_block_range_message_type     = 5104,
    dlt_block_range_reply_message_type   = 5105,
    dlt_get_block_message_type           = 5106,
    dlt_block_reply_message_type         = 5107,
    dlt_not_available_message_type       = 5108,
    dlt_fork_status_message_type         = 5109,
    dlt_peer_exchange_request_type       = 5110,
    dlt_peer_exchange_reply_type         = 5111,
    dlt_peer_exchange_rate_limited_type  = 5112,
    dlt_transaction_message_type         = 5113,
};

// ── DLT node status ─────────────────────────────────────────────────
enum dlt_node_status {
    DLT_NODE_STATUS_SYNC    = 0,  // catching up
    DLT_NODE_STATUS_FORWARD = 1   // caught up, exchanging
};

// ── DLT fork status ─────────────────────────────────────────────────
enum dlt_fork_status {
    DLT_FORK_STATUS_NORMAL            = 0,
    DLT_FORK_STATUS_LOOKING_RESOLUTION = 1,
    DLT_FORK_STATUS_MINORITY          = 2
};

// ── DLT peer lifecycle states ───────────────────────────────────────
enum dlt_peer_lifecycle_state {
    DLT_PEER_LIFECYCLE_CONNECTING    = 0,
    DLT_PEER_LIFECYCLE_HANDSHAKING   = 1,
    DLT_PEER_LIFECYCLE_SYNCING       = 2,
    DLT_PEER_LIFECYCLE_ACTIVE        = 3,
    DLT_PEER_LIFECYCLE_DISCONNECTED  = 4,
    DLT_PEER_LIFECYCLE_BANNED        = 5
};

// ── DLT Hello — sent on connection ──────────────────────────────────
struct dlt_hello_message {
    static const dlt_message_type_enum type;

    uint16_t      protocol_version = 1;
    block_id_type head_block_id;
    uint32_t      head_block_num = 0;
    block_id_type lib_block_id;
    uint32_t      lib_block_num = 0;
    uint32_t      dlt_earliest_block = 0;
    uint32_t      dlt_latest_block = 0;
    bool          emergency_active = false;
    bool          has_emergency_key = false;
    uint8_t       fork_status = DLT_FORK_STATUS_NORMAL;
    uint8_t       node_status = DLT_NODE_STATUS_SYNC;
};

// ── DLT Hello Reply ─────────────────────────────────────────────────
struct dlt_hello_reply_message {
    static const dlt_message_type_enum type;

    bool          exchange_enabled = false;
    bool          fork_alignment = false;
    block_id_type initiator_head_seen;
    block_id_type initiator_lib_seen;
    uint32_t      our_dlt_earliest = 0;
    uint32_t      our_dlt_latest = 0;
    uint8_t       our_fork_status = DLT_FORK_STATUS_NORMAL;
    uint8_t       our_node_status = DLT_NODE_STATUS_SYNC;
};

// ── Range request ───────────────────────────────────────────────────
struct dlt_range_request_message {
    static const dlt_message_type_enum type;

    uint32_t      block_num = 0;
    block_id_type block_id;   // hash of block we're asking about
};

// ── Range reply ─────────────────────────────────────────────────────
struct dlt_range_reply_message {
    static const dlt_message_type_enum type;

    uint32_t      range_start = 0;
    uint32_t      range_end = 0;
    bool          has_blocks = false;
};

// ── Bulk block fetch ────────────────────────────────────────────────
struct dlt_get_block_range_message {
    static const dlt_message_type_enum type;

    uint32_t      start_block_num = 0;
    uint32_t      end_block_num = 0;
    block_id_type prev_block_id;   // hash of block (start-1) for chain link
};

// ── Bulk block reply ────────────────────────────────────────────────
struct dlt_block_range_reply_message {
    static const dlt_message_type_enum type;

    std::vector<signed_block> blocks;
    uint32_t      last_block_next_available = 0;
    bool          is_last = false;
};

// ── Single block request ────────────────────────────────────────────
struct dlt_get_block_message {
    static const dlt_message_type_enum type;

    uint32_t      block_num = 0;
    block_id_type prev_block_id;
};

// ── Single block reply ──────────────────────────────────────────────
struct dlt_block_reply_message {
    static const dlt_message_type_enum type;

    signed_block  block;
    uint32_t      next_available = 0;
    bool          is_last = false;
};

// ── Not available ───────────────────────────────────────────────────
struct dlt_not_available_message {
    static const dlt_message_type_enum type;

    uint32_t      block_num = 0;
};

// ── Fork status update ──────────────────────────────────────────────
struct dlt_fork_status_message {
    static const dlt_message_type_enum type;

    uint8_t       fork_status = DLT_FORK_STATUS_NORMAL;
    block_id_type head_block_id;
    uint32_t      head_block_num = 0;
};

// ── Peer exchange request ───────────────────────────────────────────
struct dlt_peer_exchange_request {
    static const dlt_message_type_enum type;
    // empty — the request itself implies intent
};

// ── Peer endpoint info (used in exchange reply) ─────────────────────
struct dlt_peer_endpoint_info {
    fc::ip::endpoint  endpoint;
    node_id_t         node_id;
};

// ── Peer exchange reply ─────────────────────────────────────────────
struct dlt_peer_exchange_reply {
    static const dlt_message_type_enum type;

    std::vector<dlt_peer_endpoint_info> peers;
};

// ── Rate-limit response ─────────────────────────────────────────────
struct dlt_peer_exchange_rate_limited {
    static const dlt_message_type_enum type;

    uint32_t      wait_seconds = 0;
};

// ── Transaction broadcast ───────────────────────────────────────────
struct dlt_transaction_message {
    static const dlt_message_type_enum type;

    signed_transaction trx;
};

} // namespace network
} // namespace graphene

// ── FC_REFLECT macros ───────────────────────────────────────────────

FC_REFLECT_ENUM(graphene::network::dlt_message_type_enum,
    (dlt_hello_message_type)
    (dlt_hello_reply_message_type)
    (dlt_range_request_message_type)
    (dlt_range_reply_message_type)
    (dlt_get_block_range_message_type)
    (dlt_block_range_reply_message_type)
    (dlt_get_block_message_type)
    (dlt_block_reply_message_type)
    (dlt_not_available_message_type)
    (dlt_fork_status_message_type)
    (dlt_peer_exchange_request_type)
    (dlt_peer_exchange_reply_type)
    (dlt_peer_exchange_rate_limited_type)
    (dlt_transaction_message_type))

FC_REFLECT_ENUM(graphene::network::dlt_node_status,
    (DLT_NODE_STATUS_SYNC)(DLT_NODE_STATUS_FORWARD))

FC_REFLECT_ENUM(graphene::network::dlt_fork_status,
    (DLT_FORK_STATUS_NORMAL)(DLT_FORK_STATUS_LOOKING_RESOLUTION)(DLT_FORK_STATUS_MINORITY))

FC_REFLECT_ENUM(graphene::network::dlt_peer_lifecycle_state,
    (DLT_PEER_LIFECYCLE_CONNECTING)(DLT_PEER_LIFECYCLE_HANDSHAKING)
    (DLT_PEER_LIFECYCLE_SYNCING)(DLT_PEER_LIFECYCLE_ACTIVE)
    (DLT_PEER_LIFECYCLE_DISCONNECTED)(DLT_PEER_LIFECYCLE_BANNED))

FC_REFLECT((graphene::network::dlt_hello_message),
    (protocol_version)(head_block_id)(head_block_num)(lib_block_id)(lib_block_num)
    (dlt_earliest_block)(dlt_latest_block)(emergency_active)(has_emergency_key)
    (fork_status)(node_status))

FC_REFLECT((graphene::network::dlt_hello_reply_message),
    (exchange_enabled)(fork_alignment)(initiator_head_seen)(initiator_lib_seen)
    (our_dlt_earliest)(our_dlt_latest)(our_fork_status)(our_node_status))

FC_REFLECT((graphene::network::dlt_range_request_message),
    (block_num)(block_id))

FC_REFLECT((graphene::network::dlt_range_reply_message),
    (range_start)(range_end)(has_blocks))

FC_REFLECT((graphene::network::dlt_get_block_range_message),
    (start_block_num)(end_block_num)(prev_block_id))

FC_REFLECT((graphene::network::dlt_block_range_reply_message),
    (blocks)(last_block_next_available)(is_last))

FC_REFLECT((graphene::network::dlt_get_block_message),
    (block_num)(prev_block_id))

FC_REFLECT((graphene::network::dlt_block_reply_message),
    (block)(next_available)(is_last))

FC_REFLECT((graphene::network::dlt_not_available_message),
    (block_num))

FC_REFLECT((graphene::network::dlt_fork_status_message),
    (fork_status)(head_block_id)(head_block_num))

FC_REFLECT_EMPTY((graphene::network::dlt_peer_exchange_request))

FC_REFLECT((graphene::network::dlt_peer_endpoint_info),
    (endpoint)(node_id))

FC_REFLECT((graphene::network::dlt_peer_exchange_reply),
    (peers))

FC_REFLECT((graphene::network::dlt_peer_exchange_rate_limited),
    (wait_seconds))

FC_REFLECT((graphene::network::dlt_transaction_message),
    (trx))
