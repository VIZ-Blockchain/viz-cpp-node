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

BOOST_AUTO_TEST_CASE(unmeasured_entry_size_falls_back_to_the_legacy_threshold) {
    // 0 = not measured. Legacy threshold over CHAIN_MAX_VALIDATORS = 21 -> 15.
    BOOST_CHECK_EQUAL(emergency_exit_threshold(0), 15u);
    // And explicitly NOT the cheapest possible threshold: an unmeasured value must not
    // end emergency at the first schedule update.
    BOOST_CHECK_NE(emergency_exit_threshold(0), emergency_quorum_for(0));
    BOOST_CHECK_EQUAL(emergency_quorum_for(0), 1u);
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

BOOST_AUTO_TEST_CASE(threshold_stays_reachable_within_the_schedule) {
    // A threshold above the schedule size would make exit impossible by construction.
    for (uint32_t n = 1; n <= 64; ++n)
        BOOST_CHECK_LE(emergency_exit_threshold(n), n);
}
