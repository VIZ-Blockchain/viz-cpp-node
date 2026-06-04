#pragma once

#include "genesis_factory.hpp"

#include <graphene/protocol/block.hpp>
#include <graphene/protocol/transaction.hpp>
#include <graphene/protocol/types.hpp>

#include <fc/time.hpp>

#include <string>

namespace consensus_sim {

/// Builds a tiny, valid, no-effect signed_transaction usable to force divergence
/// between two simulated_nodes producing for the same (witness, slot).
///
/// Op: account_metadata_operation { account = params.initiator_name,
///                                  json_metadata = payload }.
/// The op requires only the initiator's regular authority. The initiator
/// account ("viz") is created at genesis with CHAIN_INITIATOR_PUBLIC_KEY in
/// master/active/regular, so a signature with params.initiator_key satisfies it.
///
/// ref_block_num / ref_block_prefix are bound to `reference_block`, the
/// expiration is set 60s after `reference_time` (CHAIN_MAX_TIME_UNTIL_EXPIRATION
/// is 1 hour, so 60s is well inside the window).
///
/// Same (params, reference_block, reference_time, payload) -> byte-identical
/// transaction id, by construction. The harness uses distinct payloads to
/// guarantee block_a's pool (empty) and block_b's pool (this tx) produce
/// different transaction_merkle_root values.
graphene::protocol::signed_transaction make_noop_metadata_tx(
    const genesis_params& params,
    const graphene::protocol::block_id_type& reference_block,
    fc::time_point_sec reference_time,
    const graphene::protocol::chain_id_type& chain_id,
    const std::string& payload);

} // namespace consensus_sim
