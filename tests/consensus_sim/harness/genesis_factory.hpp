#pragma once

#include <graphene/protocol/types.hpp>
#include <fc/crypto/elliptic.hpp>

#include <utility>
#include <vector>
#include <cstdint>

namespace consensus_sim {

struct genesis_params {
    uint64_t initial_supply;
    uint32_t num_witnesses;
    /// Pairs of (witness_account_name, signing_private_key).
    /// Names are derived as "witness-NN" where NN is the zero-padded index.
    /// These are reserved for Milestone 3+ (equivocation testing).
    /// They are NOT registered on chain by init_genesis — simulated_node has to
    /// either rotate keys via witness_update or use the genesis initiator below.
    std::vector<std::pair<graphene::protocol::account_name_type,
                          fc::ecc::private_key>> witness_keys;

    /// The witness VIZ's init_genesis actually creates and schedules in slot 0
    /// (CHAIN_COMMITTEE_ACCOUNT, signed by CHAIN_COMMITTEE_PUBLIC_KEY). With
    /// CHAIN_NUM_INITIATORS=0, "committee" is the only witness — so Milestone 1
    /// produces blocks as this account.
    graphene::protocol::account_name_type genesis_witness_name;
    /// Private key matching CHAIN_COMMITTEE_PUBLIC_KEY_STR.
    fc::ecc::private_key genesis_witness_key;

    /// The account VIZ's init_genesis funds with the entire initial supply
    /// (CHAIN_INITIATOR_NAME = "viz"). Unlike the committee account, "viz" has
    /// the initiator public key wired into all three authorities, so it can
    /// sign transactions. The harness uses this account to craft no-op
    /// transactions that force shadow/canonical divergence at equivocation
    /// scenarios — see harness/tx_factory.hpp.
    graphene::protocol::account_name_type initiator_name;
    fc::ecc::private_key initiator_key;
};

/// Deterministically build genesis parameters from a seed.
/// Same (seed, num_witnesses) -> bit-identical params.
genesis_params make_genesis_params(uint64_t seed, uint32_t num_witnesses);

} // namespace consensus_sim
