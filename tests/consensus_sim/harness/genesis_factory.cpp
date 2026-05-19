#include "genesis_factory.hpp"

#include <graphene/protocol/config.hpp>
#include <fc/crypto/sha256.hpp>

#include <cstdio>
#include <cstring>

namespace consensus_sim {

static fc::ecc::private_key derive_key(uint64_t seed, uint32_t idx) {
    // Deterministic key derivation: sha256(seed || idx) -> private key.
    char buf[16];
    std::memcpy(buf, &seed, 8);
    std::memcpy(buf + 8, &idx, 4);
    std::memset(buf + 12, 0, 4);
    fc::sha256 digest = fc::sha256::hash(buf, sizeof(buf));
    return fc::ecc::private_key::regenerate(digest);
}

genesis_params make_genesis_params(uint64_t seed, uint32_t num_witnesses) {
    genesis_params p;
    p.initial_supply = CHAIN_INIT_SUPPLY;
    p.num_witnesses = num_witnesses;
    p.witness_keys.reserve(num_witnesses);
    for (uint32_t i = 0; i < num_witnesses; ++i) {
        char name[32];
        std::snprintf(name, sizeof(name), "witness-%02u", i);
        p.witness_keys.emplace_back(
            graphene::protocol::account_name_type(name),
            derive_key(seed, i));
    }
    return p;
}

} // namespace consensus_sim
