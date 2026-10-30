#include <boost/test/unit_test.hpp>
#include <boost/test/data/test_case.hpp>
#include <boost/test/data/monomorphic.hpp>

#include "fault_injector.hpp"
#include "failure_log.hpp"
#include "invariants.hpp"
#include "scenario_driver.hpp"

using namespace consensus_sim;

BOOST_AUTO_TEST_SUITE(equivocation_suite)

// fault_injector::instruct_equivocation produces two distinct, validly-signed
// blocks for the same (witness, slot) and partitions the network so each side
// adopts a different block. chains_consistent therefore fires at the
// equivocation slot, surfacing the divergence. The tests below assert the
// expected violation rather than its absence — silent passing here means the
// shadow-node + tx-injection mechanism stopped producing real equivocation.

BOOST_AUTO_TEST_CASE(seed_deadbeef_fires_chains_consistent) {
    scenario_config cfg;
    cfg.seed = 0xDEADBEEF;
    cfg.num_witnesses = 7;
    cfg.max_slots = 30;

    scenario_driver d(cfg);
    fault_injector fi(d);

    d.add_invariant(chains_consistent);
    d.add_invariant(no_double_signed_in_canonical);
    lib_monotone_checker lib_check;
    d.add_invariant([&](const auto& ns) { return lib_check(ns); });

    // Single-witness genesis (CHAIN_NUM_INITIATORS=0): every slot is signed by
    // CHAIN_COMMITTEE_ACCOUNT. Equivocation fires on the first matching slot
    // at height >= 2 (slot 1 has no canonical block to reference for the
    // shadow's no-op tx).
    fi.instruct_equivocation(d.params().genesis_witness_name);

    auto result = d.run();
    if (!result) {
        write_failure_log("equivocation_seed_deadbeef_no_violation", d);
        BOOST_FAIL("expected chains_consistent violation, run completed clean");
    }
    BOOST_CHECK_EQUAL(result->invariant_name, "chains_consistent");
    // The split happens at the equivocation slot (block height 2) and is
    // detected at the same slot's invariant check.
    BOOST_CHECK_EQUAL(result->block_num, 2u);
}

// 100 seeds, 9 s each under ASan, used to be one ~15 min test case. Split into
// kSweepChunks data-driven cases (seed_sweep_one_hundred_all_fire/_0 .. /_9),
// each covering a contiguous block of seeds, so run_parallel.py can spread
// them across cores. The union is still seeds 0..kSweepCount-1.
namespace {
constexpr uint64_t kSweepCount = 100;
constexpr uint64_t kSweepChunks = 10;
static_assert(kSweepCount % kSweepChunks == 0, "chunks must cover every seed exactly once");
}

BOOST_DATA_TEST_CASE(seed_sweep_one_hundred_all_fire,
                     boost::unit_test::data::xrange(kSweepChunks), chunk) {
    constexpr uint64_t per_chunk = kSweepCount / kSweepChunks;
    const uint64_t first = static_cast<uint64_t>(chunk) * per_chunk;
    uint32_t missed = 0;

    // Every seed should produce the same equivocation outcome: chains_consistent
    // fires at slot 2. The sweep proves the mechanism is robust across seeds,
    // and that the violation is reproducible per-seed.
    for (uint64_t seed = first; seed < first + per_chunk; ++seed) {
        scenario_config cfg;
        cfg.seed = seed;
        cfg.num_witnesses = 7;
        cfg.max_slots = 5;  // Equivocation fires at slot 2; 5 is ample.

        scenario_driver d(cfg);
        fault_injector fi(d);

        d.add_invariant(chains_consistent);

        fi.instruct_equivocation(d.params().genesis_witness_name);

        auto result = d.run();
        if (!result || result->invariant_name != "chains_consistent") {
            ++missed;
            write_failure_log("equivocation_seed_" + std::to_string(seed), d);
            if (result) {
                BOOST_TEST_MESSAGE("seed " << seed << " unexpected violation: "
                                           << result->invariant_name);
            } else {
                BOOST_TEST_MESSAGE("seed " << seed << " no violation (expected one)");
            }
        }
    }

    BOOST_CHECK_EQUAL(missed, 0u);
    if (missed > 0) {
        BOOST_TEST_MESSAGE("failure logs in tests/consensus_sim/failures/");
    }
}

BOOST_AUTO_TEST_SUITE_END()
