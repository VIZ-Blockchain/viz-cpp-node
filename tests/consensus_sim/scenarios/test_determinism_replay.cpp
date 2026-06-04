#include <boost/test/unit_test.hpp>
#include "scenario_driver.hpp"

using namespace consensus_sim;

BOOST_AUTO_TEST_SUITE(determinism_replay_suite)

BOOST_AUTO_TEST_CASE(same_seed_byte_identical_event_log) {
    auto run_once = [](uint64_t seed) {
        scenario_config cfg;
        cfg.seed = seed;
        cfg.num_witnesses = 7;
        cfg.max_slots = 50;
        cfg.start_time = fc::time_point_sec::from_iso_string("2026-01-01T00:00:00");
        scenario_driver d(cfg);
        d.add_invariant(chains_consistent);
        auto v = d.run();
        BOOST_REQUIRE(!v);
        return d.event_log();
    };

    auto a = run_once(0x12345);
    auto b = run_once(0x12345);
    BOOST_REQUIRE_EQUAL(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        BOOST_CHECK_EQUAL(a[i], b[i]);
    }
}

// The plan's second case (different_seed_diverges) is deferred to
// Milestone 3. With the current Milestone 2 producer every block is
// signed by the genesis witness, so the per-index witness_keys that
// `seed` feeds into are never used — different seeds produce identical
// event logs. The divergence sanity check only becomes meaningful
// once register_witness_keys_ rotates per-witness keys via
// witness_update.

BOOST_AUTO_TEST_SUITE_END()
