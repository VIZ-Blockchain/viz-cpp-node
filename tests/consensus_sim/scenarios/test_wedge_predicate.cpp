// Truth-table regression guard for dlt_p2p_node::is_wedged().
//
// is_wedged() is the pure, I/O-free heart of the self-healing
// wedged-behind-network watchdog (PR #125).  The watchdog only takes the
// destructive wipe+resync action when this predicate returns true, so a
// silent change to its boolean composition — or to the WEDGE_BEHIND_THRESHOLD
// / WEDGE_CONFIRM_SEC boundaries — could either wedge a healthy node or fail
// to heal a genuinely stuck one.  The predicate's *semantics* changed in the
// review-fix round (rejects_climbed is now recency-based, behind is now
// corroborated-tip-based) even though its signature did not; this table pins
// the contract those inputs must satisfy.
//
// CONFIRMED only when ALL of:
//   behind > WEDGE_BEHIND_THRESHOLD  (far behind the corroborated network tip)
//   !head_advanced                   (our head never moved in the window)
//   rejects_climbed                  (still actively rejecting the chain now)
//   elapsed_sec >= WEDGE_CONFIRM_SEC (sustained long enough)

#include <boost/test/unit_test.hpp>

#include <graphene/network/dlt_p2p_node.hpp>

using graphene::network::dlt_p2p_node;

namespace {
constexpr uint32_t BEHIND = dlt_p2p_node::WEDGE_BEHIND_THRESHOLD;
constexpr uint32_t CONFIRM = dlt_p2p_node::WEDGE_CONFIRM_SEC;

// Values comfortably satisfying / violating each dimension.
constexpr uint32_t FAR_BEHIND   = BEHIND + 1;   // strictly greater ⇒ "behind"
constexpr uint32_t NOT_BEHIND   = BEHIND;       // NOT strictly greater ⇒ not behind
constexpr uint32_t LONG_ENOUGH  = CONFIRM;      // >= confirm ⇒ sustained
constexpr uint32_t TOO_SHORT    = CONFIRM - 1;  // < confirm ⇒ not yet
} // namespace

BOOST_AUTO_TEST_SUITE(wedge_predicate)

// The one and only combination that must confirm a wedge.
BOOST_AUTO_TEST_CASE(confirms_only_when_all_conditions_hold) {
    BOOST_CHECK(dlt_p2p_node::is_wedged(FAR_BEHIND, /*head_advanced=*/false,
                                        /*rejects_climbed=*/true, LONG_ENOUGH));
}

// Each single dimension flipped must veto the verdict.
BOOST_AUTO_TEST_CASE(not_wedged_when_head_advanced) {
    // A syncing node advances its head — never a wedge, however far behind.
    BOOST_CHECK(!dlt_p2p_node::is_wedged(FAR_BEHIND, /*head_advanced=*/true,
                                         /*rejects_climbed=*/true, LONG_ENOUGH));
}

BOOST_AUTO_TEST_CASE(not_wedged_when_rejects_not_climbing) {
    // A partitioned/silent node stops rejecting — recency vetoes the verdict.
    BOOST_CHECK(!dlt_p2p_node::is_wedged(FAR_BEHIND, /*head_advanced=*/false,
                                         /*rejects_climbed=*/false, LONG_ENOUGH));
}

BOOST_AUTO_TEST_CASE(not_wedged_when_not_far_behind) {
    // At/under the threshold we are not "behind" enough to be wedged.
    BOOST_CHECK(!dlt_p2p_node::is_wedged(NOT_BEHIND, /*head_advanced=*/false,
                                         /*rejects_climbed=*/true, LONG_ENOUGH));
}

BOOST_AUTO_TEST_CASE(not_wedged_before_confirm_window) {
    // All spatial conditions met but not yet sustained long enough.
    BOOST_CHECK(!dlt_p2p_node::is_wedged(FAR_BEHIND, /*head_advanced=*/false,
                                         /*rejects_climbed=*/true, TOO_SHORT));
}

// ── Boundary conditions ──────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(behind_boundary_is_strictly_greater) {
    // Exactly at the threshold does NOT count (predicate uses `>`, not `>=`).
    BOOST_CHECK(!dlt_p2p_node::is_wedged(BEHIND, false, true, LONG_ENOUGH));
    // One block past the threshold does.
    BOOST_CHECK(dlt_p2p_node::is_wedged(BEHIND + 1, false, true, LONG_ENOUGH));
}

BOOST_AUTO_TEST_CASE(confirm_boundary_is_inclusive) {
    // Exactly at the confirm window DOES count (predicate uses `>=`).
    BOOST_CHECK(dlt_p2p_node::is_wedged(FAR_BEHIND, false, true, CONFIRM));
    // One second short does not.
    BOOST_CHECK(!dlt_p2p_node::is_wedged(FAR_BEHIND, false, true, CONFIRM - 1));
}

// Nothing set — the trivial safe default.
BOOST_AUTO_TEST_CASE(not_wedged_when_nothing_holds) {
    BOOST_CHECK(!dlt_p2p_node::is_wedged(0, /*head_advanced=*/true,
                                         /*rejects_climbed=*/false, 0));
}

BOOST_AUTO_TEST_SUITE_END()
