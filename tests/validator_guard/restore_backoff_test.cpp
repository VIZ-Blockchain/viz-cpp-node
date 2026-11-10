// When is restoring a blanked signing key futile, and when is it the guard's job?
//
// The chain blanks a key that is more than CHAIN_EMERGENCY_MAX_VALIDATOR_MISSED_BLOCKS behind
// last_confirmed (database.cpp). For a validator this node does not produce blocks for,
// last_confirmed never advances, so every restoration is discarded on the next block and the
// guard would broadcast a validator_update every round — measured on testnet 2026-11-09: four
// cycles in three minutes, each a real transaction signed by the operator's hot key.
//
// Both directions of a wrong verdict cost something real: a false "stop" leaves a validator that
// a human must re-key by hand, and a false "keep restoring" is a stream of transactions. So the
// state machine is driven here rather than only observed live.
#define BOOST_TEST_MODULE validator_guard_restore_backoff
#include <boost/test/unit_test.hpp>

#include <graphene/plugins/validator_guard/restore_backoff.hpp>

using graphene::plugins::validator_guard::restore_backoff;

namespace {

constexpr int64_t kWindow = 315;   // one blanking threshold in seconds (post-HF16, 1 s blocks)
constexpr int64_t kHold = 3600;

/// Play the observed cycle: restore confirmed, chain blanks the key `age` seconds later.
void blanked_after(restore_backoff& b, const std::string& name, int64_t& t, int64_t age) {
    b.on_restore_confirmed(name, t);
    t += age;
}

}  // namespace

BOOST_AUTO_TEST_CASE(first_null_key_is_restored_without_evidence) {
    restore_backoff b;
    const auto d = b.on_null_key("faucet3", 1000, kWindow, kHold);
    BOOST_CHECK(d.restore);
    BOOST_CHECK(!d.warn);
    BOOST_CHECK_EQUAL(d.cycles, 0u);
}

BOOST_AUTO_TEST_CASE(one_blanked_restoration_is_a_warning_shot_not_a_hold) {
    restore_backoff b;
    int64_t t = 1000;
    blanked_after(b, "faucet3", t, 20);  // confirmed at 1000, blanked at 1020

    const auto d = b.on_null_key("faucet3", t, kWindow, kHold);
    BOOST_CHECK_MESSAGE(d.restore, "a single blank is not proof: the next slot may land the key");
    BOOST_CHECK(!d.warn);
    BOOST_CHECK_EQUAL(d.cycles, 1u);
    BOOST_CHECK_EQUAL(d.held_until, 0);
}

BOOST_AUTO_TEST_CASE(second_blanked_restoration_holds_and_warns_once) {
    restore_backoff b;
    int64_t t = 1000;
    blanked_after(b, "faucet3", t, 20);
    BOOST_CHECK(b.on_null_key("faucet3", t, kWindow, kHold).restore);  // cycle 1 -> restore again

    blanked_after(b, "faucet3", t, 31);
    const auto second = b.on_null_key("faucet3", t, kWindow, kHold);
    BOOST_CHECK_MESSAGE(!second.restore, "two consecutive blanks mean the chain discards it");
    BOOST_CHECK(second.warn);
    BOOST_CHECK_EQUAL(second.cycles, 2u);
    BOOST_CHECK_EQUAL(second.held_until, t + kHold);

    // While the hold runs: no broadcast, and the warning is not repeated.
    const auto during = b.on_null_key("faucet3", t + 600, kWindow, kHold);
    BOOST_CHECK(!during.restore);
    BOOST_CHECK(!during.warn);
    BOOST_CHECK_EQUAL(during.held_until, t + kHold);

    // After it expires the guard tries again rather than latching the validator off for good.
    const auto after = b.on_null_key("faucet3", t + kHold + 1, kWindow, kHold);
    BOOST_CHECK(after.restore);
    BOOST_CHECK(!after.warn);
    BOOST_CHECK_EQUAL(after.cycles, 0u);
}

BOOST_AUTO_TEST_CASE(restoration_that_held_past_the_window_resets_the_evidence) {
    restore_backoff b;
    int64_t t = 1000;
    b.on_restore_confirmed("faucet3", t);
    t += kWindow + 1;  // the key survived longer than one blanking threshold -> the restore worked

    const auto d = b.on_null_key("faucet3", t, kWindow, kHold);
    BOOST_CHECK(d.restore);
    BOOST_CHECK_EQUAL(d.cycles, 0u);
    BOOST_CHECK_EQUAL(d.held_until, 0);

    // And the next blanked restore counts as the FIRST cycle, not the second.
    blanked_after(b, "faucet3", t, 10);
    const auto again = b.on_null_key("faucet3", t, kWindow, kHold);
    BOOST_CHECK_MESSAGE(again.restore, "a working restore must not leave the counter primed");
    BOOST_CHECK_EQUAL(again.cycles, 1u);
}

BOOST_AUTO_TEST_CASE(a_restore_that_later_holds_wipes_the_accumulated_cycles) {
    // Cycle 1 already counted, then a restore that survives the window: the count must be wiped,
    // otherwise the NEXT blank would look like cycle 2 and hold a validator that is recovering.
    restore_backoff b;
    int64_t t = 1000;
    blanked_after(b, "faucet3", t, 20);
    BOOST_CHECK_EQUAL(b.on_null_key("faucet3", t, kWindow, kHold).cycles, 1u);

    b.on_restore_confirmed("faucet3", t);
    t += kWindow + 1;  // it held this time
    const auto after_hold = b.on_null_key("faucet3", t, kWindow, kHold);
    BOOST_CHECK(after_hold.restore);
    BOOST_CHECK_EQUAL(after_hold.cycles, 0u);

    blanked_after(b, "faucet3", t, 15);
    const auto next = b.on_null_key("faucet3", t, kWindow, kHold);
    BOOST_CHECK_MESSAGE(next.restore, "a recovered validator must not be held on its next blank");
    BOOST_CHECK_EQUAL(next.cycles, 1u);
}

BOOST_AUTO_TEST_CASE(window_boundary_counts_as_blanked) {    restore_backoff b;
    b.on_restore_confirmed("faucet3", 1000);
    BOOST_CHECK_EQUAL(b.on_null_key("faucet3", 1000 + kWindow, kWindow, kHold).cycles, 1u);
}

BOOST_AUTO_TEST_CASE(a_key_that_is_present_does_not_reset_the_counter) {
    // Between the restore and the blanking the key IS present; dropping the count there would
    // reset it on every attempt and the futility would never be detected.
    restore_backoff b;
    int64_t t = 1000;
    blanked_after(b, "faucet3", t, 20);
    BOOST_CHECK_EQUAL(b.on_null_key("faucet3", t, kWindow, kHold).cycles, 1u);

    b.on_key_present("faucet3");  // the second attempt landed
    blanked_after(b, "faucet3", t, 25);
    BOOST_CHECK(!b.on_null_key("faucet3", t, kWindow, kHold).restore);
}

BOOST_AUTO_TEST_CASE(operator_action_ends_a_hold_early) {
    restore_backoff b;
    int64_t t = 1000;
    blanked_after(b, "faucet3", t, 20);
    b.on_null_key("faucet3", t, kWindow, kHold);
    blanked_after(b, "faucet3", t, 20);
    BOOST_CHECK(!b.on_null_key("faucet3", t, kWindow, kHold).restore);

    // The operator re-keys it by hand: the key appears, so the hold is over.
    b.on_key_present("faucet3");
    BOOST_CHECK(!b.is_held("faucet3", t + 10));
}

BOOST_AUTO_TEST_CASE(validators_are_tracked_independently) {
    restore_backoff b;
    int64_t t = 1000;
    blanked_after(b, "faucet3", t, 20);
    b.on_null_key("faucet3", t, kWindow, kHold);
    blanked_after(b, "faucet3", t, 20);
    BOOST_CHECK(!b.on_null_key("faucet3", t, kWindow, kHold).restore);

    // A different validator on the same node is not punished for its neighbour's cycles.
    const auto other = b.on_null_key("other-validator", t, kWindow, kHold);
    BOOST_CHECK(other.restore);
    BOOST_CHECK_EQUAL(other.cycles, 0u);
}
