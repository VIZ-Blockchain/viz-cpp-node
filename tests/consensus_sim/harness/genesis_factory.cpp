#include "genesis_factory.hpp"

#include <graphene/protocol/config.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/exception/exception.hpp>

#include <cstdio>
#include <cstring>

namespace consensus_sim {

// Matches CHAIN_COMMITTEE_PUBLIC_KEY_STR in libraries/protocol/include/graphene/protocol/config.hpp.
// With CHAIN_NUM_INITIATORS=0, init_genesis does NOT create a witness for the
// CHAIN_INITIATOR_NAME ("viz") account — it only creates a witness for
// CHAIN_COMMITTEE_ACCOUNT signed by CHAIN_COMMITTEE_PUBLIC_KEY. The committee
// account is scheduled in slot 0, so block production has to use this identity.
static const char* kGenesisWitnessPrivateKeyWif =
    "5Hw9YPABaFxa2LooiANLrhUK5TPryy8f7v9Y1rk923PuYqbYdfC";

// Matches CHAIN_INITIATOR_PUBLIC_KEY_STR. The "viz" account holds the entire
// initial supply at genesis and is the only account with a signable authority
// (committee/anonymous/invite are special; see database.cpp init_genesis path).
// The private key is documented as a comment in config.hpp:35 but is not
// exposed as a macro, so the harness has to hard-code the WIF here.
static const char* kInitiatorPrivateKeyWif =
    "5JabcrvaLnBTCkCVFX5r4rmeGGfuJuVp4NAKRNLTey6pxhRQmf4";

static fc::ecc::private_key derive_key(uint64_t seed, uint32_t idx) {
    // Deterministic key derivation: sha256(seed || idx) -> private key.
    char buf[16];
    std::memcpy(buf, &seed, 8);
    std::memcpy(buf + 8, &idx, 4);
    std::memset(buf + 12, 0, 4);
    fc::sha256 digest = fc::sha256::hash(buf, sizeof(buf));
    return fc::ecc::private_key::regenerate(digest);
}

static fc::ecc::private_key load_wif_(const char* wif, const char* role) {
    auto k = graphene::utilities::wif_to_key(wif);
    FC_ASSERT(k.valid(), "consensus_sim: failed to parse ${role} WIF",
              ("role", std::string(role)));
    return *k;
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
    p.genesis_witness_name = graphene::protocol::account_name_type(CHAIN_COMMITTEE_ACCOUNT);
    p.genesis_witness_key = load_wif_(kGenesisWitnessPrivateKeyWif, "genesis witness");
    p.initiator_name = graphene::protocol::account_name_type(CHAIN_INITIATOR_NAME);
    p.initiator_key = load_wif_(kInitiatorPrivateKeyWif, "initiator");
    return p;
}

} // namespace consensus_sim
