#include <boost/test/unit_test.hpp>
#include "simulated_node.hpp"
#include "genesis_factory.hpp"
#include "virtual_clock.hpp"

#include <graphene/protocol/config.hpp>

using namespace consensus_sim;

BOOST_AUTO_TEST_SUITE(simulated_node_smoke_suite)

// Milestone 1 produces blocks as the single witness VIZ's init_genesis
// actually creates (CHAIN_INITIATOR_NAME, "viz"). genesis_factory's
// witness_keys vector is reserved for Milestone 3 — see the comment in
// simulated_node::register_witness_keys_.

BOOST_AUTO_TEST_CASE(single_node_produces_one_block) {
    auto params = make_genesis_params(0xCAFE, 1);
    auto t0 = fc::time_point_sec::from_iso_string("2026-01-01T00:00:00");
    virtual_clock clk(t0);

    simulated_node node("node-0", params, clk);

    auto block = node.produce_block(params.genesis_witness_name,
                                    params.genesis_witness_key,
                                    t0 + CHAIN_BLOCK_INTERVAL);

    BOOST_CHECK_EQUAL(node.head_block_num(), 1u);
    BOOST_CHECK(node.head_block_id() == block.id());
    BOOST_CHECK(node.head_block_time() == t0 + fc::seconds(CHAIN_BLOCK_INTERVAL));
}

BOOST_AUTO_TEST_CASE(single_node_produces_100_blocks) {
    auto params = make_genesis_params(0xBEEF, 1);
    auto t0 = fc::time_point_sec::from_iso_string("2026-01-01T00:00:00");
    virtual_clock clk(t0);

    simulated_node node("node-0", params, clk);

    fc::time_point_sec t = t0;
    for (int i = 0; i < 100; ++i) {
        t += CHAIN_BLOCK_INTERVAL;
        clk.advance_to(t);
        node.produce_block(params.genesis_witness_name, params.genesis_witness_key, t);
    }
    BOOST_CHECK_EQUAL(node.head_block_num(), 100u);
}

BOOST_AUTO_TEST_SUITE_END()
