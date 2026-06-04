#include <boost/test/unit_test.hpp>
#include "genesis_factory.hpp"

#include <graphene/protocol/config.hpp>

#include <set>
#include <string>

using namespace consensus_sim;

BOOST_AUTO_TEST_SUITE(genesis_factory_suite)

BOOST_AUTO_TEST_CASE(same_seed_same_keys) {
    auto a = make_genesis_params(42, /*num_witnesses=*/7);
    auto b = make_genesis_params(42, /*num_witnesses=*/7);
    BOOST_REQUIRE_EQUAL(a.witness_keys.size(), 7u);
    BOOST_REQUIRE_EQUAL(b.witness_keys.size(), 7u);
    for (size_t i = 0; i < 7; ++i) {
        BOOST_CHECK(a.witness_keys[i].first == b.witness_keys[i].first);
        BOOST_CHECK(a.witness_keys[i].second.get_secret() == b.witness_keys[i].second.get_secret());
    }
}

BOOST_AUTO_TEST_CASE(different_seed_different_keys) {
    auto a = make_genesis_params(42, 7);
    auto b = make_genesis_params(43, 7);
    BOOST_CHECK(a.witness_keys[0].second.get_secret() != b.witness_keys[0].second.get_secret());
}

BOOST_AUTO_TEST_CASE(witness_names_are_distinct) {
    auto p = make_genesis_params(7, 7);
    std::set<std::string> names;
    for (auto& [name, _] : p.witness_keys) names.insert(std::string(name));
    BOOST_CHECK_EQUAL(names.size(), 7u);
}

BOOST_AUTO_TEST_CASE(initiator_key_matches_genesis_constant) {
    // The initiator account is funded with the full initial supply at genesis
    // and is the only signable account on a CHAIN_NUM_INITIATORS=0 chain.
    // Anyone touching make_genesis_params should know that changing the WIF
    // here invalidates the tx_factory's signing assumption.
    auto p = make_genesis_params(0, 1);
    BOOST_CHECK_EQUAL(std::string(p.initiator_name),
                      std::string(CHAIN_INITIATOR_NAME));
    graphene::protocol::public_key_type derived(p.initiator_key.get_public_key());
    BOOST_CHECK_EQUAL(static_cast<std::string>(derived),
                      std::string(CHAIN_INITIATOR_PUBLIC_KEY_STR));
}

BOOST_AUTO_TEST_SUITE_END()
