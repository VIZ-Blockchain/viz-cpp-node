#include "tx_factory.hpp"

#include <graphene/protocol/chain_operations.hpp>

namespace consensus_sim {

graphene::protocol::signed_transaction make_noop_metadata_tx(
    const genesis_params& params,
    const graphene::protocol::block_id_type& reference_block,
    fc::time_point_sec reference_time,
    const graphene::protocol::chain_id_type& chain_id,
    const std::string& payload) {
    graphene::protocol::account_metadata_operation op;
    op.account = params.initiator_name;
    op.json_metadata = payload;

    graphene::protocol::signed_transaction tx;
    tx.set_reference_block(reference_block);
    tx.set_expiration(reference_time + fc::seconds(60));
    tx.operations.emplace_back(op);
    tx.sign(params.initiator_key, chain_id);
    return tx;
}

} // namespace consensus_sim
