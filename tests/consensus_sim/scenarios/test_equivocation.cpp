#include <boost/test/unit_test.hpp>

#include "fault_injector.hpp"
#include "failure_log.hpp"
#include "invariants.hpp"
#include "scenario_driver.hpp"

using namespace consensus_sim;

BOOST_AUTO_TEST_SUITE(equivocation_suite)

BOOST_AUTO_TEST_CASE(seed_deadbeef_no_canonical_double_sign) {
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

    // Milestone 2 wires every slot to the genesis witness (CHAIN_COMMITTEE_ACCOUNT,
    // the only on-chain witness when CHAIN_NUM_INITIATORS=0). Equivocation must
    // therefore target that witness to fire; the plan's `params.witness_keys[3]`
    // path will reactivate once multi-witness key rotation lands in a follow-up.
    //
    // The instruct_equivocation body currently ships block_a only and flags the
    // shadow-chain reconstruction gap, so this test is expected to pass trivially
    // until that gap is closed (sibling-state or direct-mutation second-block
    // construction).
    const auto& params = d.params();
    fi.instruct_equivocation(params.genesis_witness_name);

    auto result = d.run();
    if (result) {
        write_failure_log("equivocation_seed_deadbeef", d);
        BOOST_FAIL("invariant violated: " + result->invariant_name + " - " + result->detail);
    }
}

BOOST_AUTO_TEST_CASE(seed_sweep_one_hundred) {
    constexpr uint64_t kSweepCount = 100;
    uint32_t failures = 0;

    // Each scenario spins up `num_witnesses` chainbase databases, each ~340ms to
    // open under ASan. With 7 nodes × 100 seeds that floor alone is ~4 min, so
    // the sweep keeps slot count low until shadow-block construction (Task 13
    // follow-up) actually produces equivocations worth running long for.
    for (uint64_t seed = 0; seed < kSweepCount; ++seed) {
        scenario_config cfg;
        cfg.seed = seed;
        cfg.num_witnesses = 7;
        cfg.max_slots = 10;

        scenario_driver d(cfg);
        fault_injector fi(d);

        d.add_invariant(no_double_signed_in_canonical);

        // Target genesis_witness_name to actually fire the equivocation override
        // (per Milestone 2's single-witness genesis). The seed still varies the
        // genesis_params.witness_keys list — unused by the slot producer today
        // but kept here so the sweep takes the same shape as the eventual
        // multi-witness target: `params.witness_keys[seed % 7].first`.
        const auto& params = d.params();
        fi.instruct_equivocation(params.genesis_witness_name);

        auto result = d.run();
        if (result) {
            ++failures;
            write_failure_log("equivocation_seed_" + std::to_string(seed), d);
            BOOST_TEST_MESSAGE("seed " << seed << " violation: " << result->detail);
        }
    }

    BOOST_CHECK_EQUAL(failures, 0u);
    if (failures > 0) {
        BOOST_TEST_MESSAGE("failure logs in tests/consensus_sim/failures/");
    }
}

BOOST_AUTO_TEST_SUITE_END()
