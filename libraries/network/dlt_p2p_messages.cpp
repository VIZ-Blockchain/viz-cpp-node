#include <graphene/network/dlt_p2p_messages.hpp>

namespace graphene {
namespace network {

// Static type constants for each DLT message struct
const dlt_message_type_enum dlt_hello_message::type               = dlt_hello_message_type;
const dlt_message_type_enum dlt_hello_reply_message::type         = dlt_hello_reply_message_type;
const dlt_message_type_enum dlt_range_request_message::type       = dlt_range_request_message_type;
const dlt_message_type_enum dlt_range_reply_message::type         = dlt_range_reply_message_type;
const dlt_message_type_enum dlt_get_block_range_message::type     = dlt_get_block_range_message_type;
const dlt_message_type_enum dlt_block_range_reply_message::type   = dlt_block_range_reply_message_type;
const dlt_message_type_enum dlt_get_block_message::type           = dlt_get_block_message_type;
const dlt_message_type_enum dlt_block_reply_message::type         = dlt_block_reply_message_type;
const dlt_message_type_enum dlt_not_available_message::type       = dlt_not_available_message_type;
const dlt_message_type_enum dlt_fork_status_message::type         = dlt_fork_status_message_type;
const dlt_message_type_enum dlt_peer_exchange_request::type       = dlt_peer_exchange_request_type;
const dlt_message_type_enum dlt_peer_exchange_reply::type         = dlt_peer_exchange_reply_type;
const dlt_message_type_enum dlt_peer_exchange_rate_limited::type  = dlt_peer_exchange_rate_limited_type;
const dlt_message_type_enum dlt_transaction_message::type         = dlt_transaction_message_type;
const dlt_message_type_enum dlt_soft_ban_message::type            = dlt_soft_ban_message_type;

} // namespace network
} // namespace graphene
