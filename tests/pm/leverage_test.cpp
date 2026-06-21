// HF14 leverage CPMM math test — frozen against leverage-risk-off-strategy.md §4.
// Pure integer math used by the leverage evaluators / atomic-liquidation hook. The
// reserve convention matches the chain's pm_place_bet (outcome 0 adds to reserve_a).

#define BOOST_TEST_MODULE pm_leverage
#include <boost/test/unit_test.hpp>

#include <graphene/chain/pm/leverage.hpp>

using namespace graphene::chain::pm::leverage;

namespace {
    // Equal-reserve deep market: ra = rb = 10,000,000 mVIZ, k = ra*rb.
    const int64_t RA = 10000000, RB = 10000000;
    const fc::uint128_t K = fc::uint128_t((uint64_t)RA) * fc::uint128_t((uint64_t)RB);
}

// §4.4 — liquidation threshold = loan × (1 + R%/100); safety adds S%.
BOOST_AUTO_TEST_CASE(threshold_and_safety) {
    BOOST_CHECK_EQUAL(liquidation_threshold(1000000, 10), 1100000); // 1000 × 1.10
    int64_t safe = (int64_t)(fc::uint128_t((uint64_t)liquidation_threshold(1000000, 10))
                   * fc::uint128_t(101u) / fc::uint128_t(100u)).lo;
    BOOST_CHECK_EQUAL(safe, 1111000); // 1100 × 1.01
}

// §4.1 — cpmm_buy returns positive tokens and moves reserves in the right direction.
BOOST_AUTO_TEST_CASE(cpmm_buy_direction) {
    auto a = cpmm_buy(RA, RB, K, 1000000, 0); // bet on A
    BOOST_CHECK_GT(a.tokens, 0);
    BOOST_CHECK_GT(a.new_reserve_a, RA);   // A side grows
    BOOST_CHECK_LT(a.new_reserve_b, RB);   // B side shrinks
    auto b = cpmm_buy(RA, RB, K, 1000000, 1); // bet on B (symmetric)
    BOOST_CHECK_EQUAL(a.tokens, b.tokens);
}

// §4.2 — cancel_value > 0, symmetric on equal reserves, sub-linear (slippage);
// and cancel_value_after_opposing(m=0) reduces to cancel_value.
BOOST_AUTO_TEST_CASE(cancel_value_properties) {
    int64_t cv_a = cancel_value(RA, RB, K, 1000000, 0);
    int64_t cv_b = cancel_value(RA, RB, K, 1000000, 1);
    BOOST_CHECK_GT(cv_a, 0);
    BOOST_CHECK_EQUAL(cv_a, cv_b);                       // symmetric

    int64_t cv_5 = cancel_value(RA, RB, K, 5000000, 0);
    BOOST_CHECK_GT(cv_5, cv_a);                          // more tokens → more VIZ
    BOOST_CHECK_LT(cv_5, cv_a * 5);                      // but sub-linear

    BOOST_CHECK_EQUAL(cancel_value_after_opposing(RA, RB, K, 1000000, 0, 0), cv_a);
}

// §4.3 — an opposing bet lowers the position's cancel value (monotone in M).
BOOST_AUTO_TEST_CASE(opposing_bet_lowers_value) {
    int64_t base  = cancel_value_after_opposing(RA, RB, K, 1000000, 0, 0);
    int64_t small = cancel_value_after_opposing(RA, RB, K, 1000000, 0, 500000);
    int64_t big   = cancel_value_after_opposing(RA, RB, K, 1000000, 0, 2000000);
    BOOST_CHECK_LT(small, base);
    BOOST_CHECK_LT(big, small);
    int64_t m = worst_opposing_bet(RA, RB, 10, 100); // 10% slippage, full M_max
    BOOST_CHECK_EQUAL(m, RA / 10);                   // min(ra,rb) × 10% × 100%
}

// §4.6 Constraint 2 — max leverage binary search returns a bounded loan whose
// worst-case cancel value satisfies the safety threshold after the bet is placed.
BOOST_AUTO_TEST_CASE(max_leverage_constraint) {
    const int64_t collateral = 1000000; // 1000 VIZ
    const int outcome = 0;
    const uint16_t R = 10, S = 1, SL = 10, MF = 100;

    int64_t loan = max_leverage_loan(RA, RB, K, collateral, outcome,
                                     collateral * 10, R, S, SL, MF);
    BOOST_CHECK_GT(loan, 0);
    BOOST_CHECK_LE(loan, collateral * 10);

    // Re-derive the constraint at the chosen loan: place (C+L), worst opposing bet, check.
    auto f = cpmm_buy(RA, RB, K, collateral + loan, outcome);
    int64_t m = worst_opposing_bet(f.new_reserve_a, f.new_reserve_b, SL, MF);
    int64_t cvw = cancel_value_after_opposing(f.new_reserve_a, f.new_reserve_b, K, f.tokens, outcome, m);
    int64_t thr_safe = (int64_t)(fc::uint128_t((uint64_t)liquidation_threshold(loan, R))
                       * fc::uint128_t(100u + S) / fc::uint128_t(100u)).lo;
    BOOST_CHECK_GE(cvw, thr_safe);
}
