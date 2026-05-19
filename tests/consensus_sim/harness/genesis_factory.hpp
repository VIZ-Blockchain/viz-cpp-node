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
    std::vector<std::pair<graphene::protocol::account_name_type,
                          fc::ecc::private_key>> witness_keys;
};

/// Deterministically build genesis parameters from a seed.
/// Same (seed, num_witnesses) -> bit-identical params.
genesis_params make_genesis_params(uint64_t seed, uint32_t num_witnesses);

} // namespace consensus_sim
