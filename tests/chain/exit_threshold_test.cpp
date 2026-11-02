// Exit-from-emergency threshold: the number of real (keyed) validators at which the chain
// leaves emergency consensus must be the quorum the chain will then require — computed from
// the schedule size measured ON ENTRY, not from CHAIN_MAX_VALIDATORS.
//
// The defect this pins: the old expression was 21*75/100 = 15 unconditionally. With 18
// slots scheduled (mainnet) that asks 15 of 18 = 83% while the quorum the network needs is
// 13; with fewer than 15 scheduled it is unreachable at all.
//
// Expected numbers are literals taken from the rule, not a re-implementation of it: the
// test must call the library function, otherwise it proves only that the copy compiles.
#define BOOST_TEST_MODULE chain_exit_threshold
#include <boost/test/unit_test.hpp>

#include <graphene/chain/emergency_exit.hpp>

using graphene::chain::emergency_exit_threshold;
using graphene::chain::emergency_exit_threshold_effective;
using graphene::chain::emergency_exit_floor;
using graphene::chain::emergency_quorum_for;

BOOST_AUTO_TEST_CASE(threshold_follows_the_schedule_size_on_entry) {
    BOOST_CHECK_EQUAL(emergency_exit_threshold(9), 7u);
    BOOST_CHECK_EQUAL(emergency_exit_threshold(14), 10u);
    BOOST_CHECK_EQUAL(emergency_exit_threshold(18), 13u);
    BOOST_CHECK_EQUAL(emergency_exit_threshold(21), 15u);
}

BOOST_AUTO_TEST_CASE(small_schedules_are_measured_from_integer_division_not_rounded_up) {
    // 1 slot: 1*2/3+1 = 0+1 = 1, and 1% of 75% truncates to 0, so the quorum is 1.
    // (An earlier hand note in the goal claimed 2 here; the function says 1. The test
    // records the function, not the note.)
    BOOST_CHECK_EQUAL(emergency_exit_threshold(1), 1u);
    BOOST_CHECK_EQUAL(emergency_exit_threshold(2), 2u);
    BOOST_CHECK_EQUAL(emergency_exit_threshold(3), 3u);
}

BOOST_AUTO_TEST_CASE(unmeasured_entry_size_falls_back_to_a_full_max_schedule) {
    // 0 = not measured -> assume the schedule was full. Stated as a RELATION, so the leg is
    // config-independent: the testnet build has CHAIN_MAX_VALIDATORS = 1, the mainnet one 21.
    BOOST_CHECK_EQUAL(emergency_exit_threshold(0), emergency_quorum_for(CHAIN_MAX_VALIDATORS));
    // On mainnet that is exactly the legacy constant, so the fallback reproduces today's
    // behaviour (21 slots: max(21*2/3+1, 21*75%) = max(15, 15) = 15).
#ifndef BUILD_TESTNET
    BOOST_CHECK_EQUAL(emergency_exit_threshold(0), 15u);
#endif
    // An unmeasured value must not become the cheapest possible threshold: with a real
    // schedule (>1 slot allowed) it must differ from the quorum of an empty schedule.
    if (CHAIN_MAX_VALIDATORS > 1)
        BOOST_CHECK_NE(emergency_exit_threshold(0), emergency_quorum_for(0));
}

BOOST_AUTO_TEST_CASE(exit_threshold_equals_the_quorum_the_network_requires_after_exit) {
    // The whole point of the fix: ask for exactly what the chain will enforce next block,
    // for every plausible schedule size. If these ever diverge, exit either asks too much
    // (stuck at 83%) or too little (leaves emergency without a workable quorum).
    for (uint32_t n = 1; n <= 64; ++n)
        BOOST_CHECK_EQUAL(emergency_exit_threshold(n), emergency_quorum_for(n));
}

BOOST_AUTO_TEST_CASE(threshold_never_decreases_as_more_validators_return) {
    uint32_t prev = emergency_exit_threshold(1);
    for (uint32_t n = 2; n <= 64; ++n) {
        const uint32_t cur = emergency_exit_threshold(n);
        BOOST_CHECK_GE(cur, prev);
        prev = cur;
    }
}

BOOST_AUTO_TEST_CASE(effective_threshold_is_the_quorum_raised_to_the_floor) {
    // HF17 (owner question #3396 option A): the number the exit check compares against is the
    // quorum of the entry size, but never below half the maximum schedule.
    BOOST_CHECK_EQUAL(emergency_exit_floor(), CHAIN_MAX_VALIDATORS / 2);
    for (uint32_t n = 1; n <= 64; ++n) {
        const uint32_t expected = std::max(emergency_quorum_for(n), uint32_t(CHAIN_MAX_VALIDATORS / 2));
        BOOST_CHECK_EQUAL(emergency_exit_threshold_effective(n), expected);
    }
    // Not measured (0) is the legacy full-schedule quorum, and the floor cannot change it.
    BOOST_CHECK_EQUAL(emergency_exit_threshold_effective(0), emergency_quorum_for(CHAIN_MAX_VALIDATORS));
#ifndef BUILD_TESTNET
    // Mainnet literals: 21 slots on entry -> max(15, 10) = 15 (today's constant, unchanged);
    // 14 -> max(10, 10) = 10 (the case the old constant made unreachable); 9 -> max(7, 10) = 10.
    BOOST_CHECK_EQUAL(emergency_exit_threshold_effective(21), 15u);
    BOOST_CHECK_EQUAL(emergency_exit_threshold_effective(14), 10u);
    BOOST_CHECK_EQUAL(emergency_exit_threshold_effective(9), 10u);
    // The floor's price, stated: a schedule that was tiny on entry now needs more validators
    // than it had, so an episode with an unchanged holder set never ends. That is the owner's
    // option A trade-off; option B is emergency_exit_floor() returning 0.
    BOOST_CHECK_GT(emergency_exit_threshold_effective(1), emergency_exit_threshold(1));
#endif
}

BOOST_AUTO_TEST_CASE(effective_threshold_never_falls_below_the_floor_and_never_decreases) {
    // Only REAL entry sizes are checked: the schedule never has more than CHAIN_MAX_VALIDATORS
    // slots, and the unmeasured 0 is a separate case (it answers with the quorum of the maximum,
    // which on mainnet is 15 — above the floor, so it cannot start this monotonic run).
    uint32_t prev = emergency_exit_threshold_effective(1);
    for (uint32_t n = 1; n <= CHAIN_MAX_VALIDATORS; ++n) {
        const uint32_t cur = emergency_exit_threshold_effective(n);
        BOOST_CHECK_GE(cur, emergency_exit_floor());
        BOOST_CHECK_GE(cur, prev);
        BOOST_CHECK_LE(cur, CHAIN_MAX_VALIDATORS);
        prev = cur;
    }
}

BOOST_AUTO_TEST_CASE(threshold_stays_reachable_within_the_schedule) {
    // A threshold above the schedule size would make exit impossible by construction.
    for (uint32_t n = 1; n <= 64; ++n)
        BOOST_CHECK_LE(emergency_exit_threshold(n), n);
}
