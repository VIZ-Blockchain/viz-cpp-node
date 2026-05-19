#include <boost/test/unit_test.hpp>
#include "genesis_factory.hpp"

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

BOOST_AUTO_TEST_SUITE_END()
