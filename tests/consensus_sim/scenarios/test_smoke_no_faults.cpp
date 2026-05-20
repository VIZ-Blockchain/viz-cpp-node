#include <boost/test/unit_test.hpp>
#include "scenario_driver.hpp"

using namespace consensus_sim;

BOOST_AUTO_TEST_SUITE(smoke_no_faults_suite)

BOOST_AUTO_TEST_CASE(seven_nodes_one_hundred_slots) {
    scenario_config cfg;
    cfg.seed = 0xDEADBEEF;
    cfg.num_witnesses = 7;
    cfg.max_slots = 100;
    cfg.start_time = fc::time_point_sec::from_iso_string("2026-01-01T00:00:00");

    scenario_driver d(cfg);

    d.add_invariant(chains_consistent);
    lib_monotone_checker lib_check;
    d.add_invariant([&lib_check](const std::vector<simulated_node*>& ns) {
        return lib_check(ns);
    });

    auto result = d.run();
    if (result) {
        BOOST_FAIL("invariant violated: " + result->invariant_name + " - " + result->detail);
    }

    for (auto* n : d.nodes()) {
        BOOST_CHECK_GE(n->head_block_num(), 90u);
    }
    auto head_id = d.nodes()[0]->head_block_id();
    for (auto* n : d.nodes()) {
        BOOST_CHECK(n->head_block_id() == head_id);
    }
}

BOOST_AUTO_TEST_SUITE_END()
