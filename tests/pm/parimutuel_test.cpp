// HF14 parimutuel settlement test — verifies the pure money-split math used by
// settle_market() (pm_evaluator.cpp). The single invariant that MUST always hold
// is ZERO-SUM conservation (no token emission, no leak):
//
//   Σ winner_payout + oracle_take + creator_take + lp_bonus
//     == Σ winner.amount + losers_sum + forfeit_pool
//
// (oracle_fixed_fee is funded FROM the pool, so it is NOT an input term — it is
// part of oracle_take which is drawn out of losers_sum.) Plus the spec edge
// cases from parimutuel-settlement.md §5.

#define BOOST_TEST_MODULE pm_parimutuel
#include <boost/test/unit_test.hpp>

#include <graphene/chain/pm/parimutuel.hpp>

#include <numeric>
#include <vector>

using namespace graphene::chain::pm;

namespace {

    int64_t sum_payout(const settle_result& r) {
        return std::accumulate(r.winner_payout.begin(), r.winner_payout.end(), (int64_t)0);
    }

    // Σ outputs must equal Σ inputs (losers_sum + forfeit_pool + winner principals).
    void check_conservation(const settle_params& p, const std::vector<winner_in>& w,
                            const settle_result& r) {
        int64_t in = p.losers_sum + p.forfeit_pool;
        for (const auto& x : w) in += x.amount;
        int64_t out = sum_payout(r) + r.oracle_take + r.creator_take + r.lp_bonus;
        BOOST_CHECK_EQUAL(in, out);
        BOOST_CHECK(r.oracle_take  >= 0);
        BOOST_CHECK(r.creator_take >= 0);
        BOOST_CHECK(r.lp_bonus     >= 0);
    }

} // namespace

// Two winners split the losers' pool strictly by weight (no fees/penalty/forfeit).
BOOST_AUTO_TEST_CASE(weight_proportional_split) {
    settle_params p;
    p.losers_sum = 400;
    std::vector<winner_in> w = { {100, 100, 0}, {200, 300, 0} };
    auto r = compute_settlement(p, w);

    // winners_pool = 400; profit = 400 * weight / 400.
    BOOST_CHECK_EQUAL(r.winner_payout[0], 100 + 100); // 100 principal + 100 profit
    BOOST_CHECK_EQUAL(r.winner_payout[1], 200 + 300); // 200 principal + 300 profit
    BOOST_CHECK_EQUAL(r.lp_bonus, 0);
    BOOST_CHECK_EQUAL(r.oracle_take, 0);
    check_conservation(p, w, r);
}

// Fees are bp OF losers_sum (10000 = 100%) and route to oracle/creator/LP.
BOOST_AUTO_TEST_CASE(fees_from_losers_sum) {
    settle_params p;
    p.losers_sum = 1000;
    p.oracle_fee_percent = 500;   // 5% → 50
    p.creator_fee_percent = 200;  // 2% → 20
    p.liquidity_fee_percent = 300; // 3% → 30 → LP
    std::vector<winner_in> w = { {500, 100, 0} };
    auto r = compute_settlement(p, w);

    BOOST_CHECK_EQUAL(r.oracle_take, 50);
    BOOST_CHECK_EQUAL(r.creator_take, 20);
    BOOST_CHECK_EQUAL(r.lp_bonus, 30);
    BOOST_CHECK_EQUAL(r.winner_payout[0], 500 + 900); // winners_pool = 1000-100
    check_conservation(p, w, r);
}

// No winning tokens → the whole pool becomes an LP bonus (spec §5).
BOOST_AUTO_TEST_CASE(no_winners_pool_to_lp) {
    settle_params p;
    p.losers_sum = 500;
    auto r = compute_settlement(p, {});
    BOOST_CHECK(r.winner_payout.empty());
    BOOST_CHECK_EQUAL(r.lp_bonus, 500);
    check_conservation(p, {}, r);
}

// Everyone won (losers_sum = 0): winners share only the forfeit_pool, never lose.
BOOST_AUTO_TEST_CASE(all_winners_only_forfeit) {
    settle_params p;
    p.losers_sum = 0;
    p.forfeit_pool = 100;
    std::vector<winner_in> w = { {200, 50, 0}, {200, 50, 0} };
    auto r = compute_settlement(p, w);
    BOOST_CHECK_EQUAL(r.winner_payout[0], 200 + 50);
    BOOST_CHECK_EQUAL(r.winner_payout[1], 200 + 50);
    check_conservation(p, w, r);
}

// Single winner takes the entire winners_pool.
BOOST_AUTO_TEST_CASE(single_winner_takes_pool) {
    settle_params p;
    p.losers_sum = 777;
    std::vector<winner_in> w = { {123, 9, 0} };
    auto r = compute_settlement(p, w);
    BOOST_CHECK_EQUAL(r.winner_payout[0], 123 + 777);
    BOOST_CHECK_EQUAL(r.lp_bonus, 0);
    check_conservation(p, w, r);
}

// time_penalty docks PROFIT only; the docked amount accrues to the LP bonus.
BOOST_AUTO_TEST_CASE(time_penalty_to_lp) {
    settle_params p;
    p.losers_sum = 1000;
    std::vector<winner_in> w = { {300, 100, 500000} }; // 50% of profit (1e6 scale)
    auto r = compute_settlement(p, w);
    // profit = 1000, penalty = 1000 * 0.5 = 500.
    BOOST_CHECK_EQUAL(r.winner_payout[0], 300 + 1000 - 500);
    BOOST_CHECK_EQUAL(r.lp_bonus, 500);
    check_conservation(p, w, r);
}

// oracle_fixed_fee is capped at the available pool and funded from it (never minted).
BOOST_AUTO_TEST_CASE(oracle_fixed_fee_capped) {
    settle_params p;
    p.losers_sum = 100;
    p.oracle_fixed_fee = 1000; // exceeds the pool
    std::vector<winner_in> w = { {50, 10, 0} };
    auto r = compute_settlement(p, w);
    BOOST_CHECK_EQUAL(r.oracle_take, 100);     // takes the whole pool, capped
    BOOST_CHECK_EQUAL(r.winner_payout[0], 50); // winner gets only principal back
    check_conservation(p, w, r);
}

// Rounding dust from the integer weight split is swept into the LP bonus.
BOOST_AUTO_TEST_CASE(rounding_dust_to_lp) {
    settle_params p;
    p.losers_sum = 10;
    std::vector<winner_in> w = { {5, 1, 0}, {5, 2, 0} }; // total weight 3
    auto r = compute_settlement(p, w);
    // profit = floor(10*1/3)=3, floor(10*2/3)=6 → distributed 9, dust 1 → LP.
    BOOST_CHECK_EQUAL(r.winner_payout[0], 5 + 3);
    BOOST_CHECK_EQUAL(r.winner_payout[1], 5 + 6);
    BOOST_CHECK_EQUAL(r.lp_bonus, 1);
    check_conservation(p, w, r);
}

// ── Time-weighted LP fee split (distribute_lp) ──────────────────────────────────

// Equal principal, longer time-in-market earns a larger fee share; Σ == bonus.
BOOST_AUTO_TEST_CASE(lp_time_weight_early_earns_more) {
    std::vector<lp_in> lps = { {1000, 3600 * 10}, {1000, 0} }; // 10h vs 0
    auto s = distribute_lp(lps, 600);
    BOOST_CHECK_EQUAL(s[0] + s[1], 600);      // exact conservation
    BOOST_CHECK_GT(s[0], s[1]);               // earlier LP earns more
}

// Equal time reduces to a plain principal-pro-rata split.
BOOST_AUTO_TEST_CASE(lp_equal_time_pro_rata) {
    std::vector<lp_in> lps = { {300, 0}, {100, 0} };
    auto s = distribute_lp(lps, 400);
    BOOST_CHECK_EQUAL(s[0] + s[1], 400);
    BOOST_CHECK_EQUAL(s[0], 300);             // 400 * 300/400
    BOOST_CHECK_EQUAL(s[1], 100);
}

// Single LP takes the whole bonus; zero bonus pays nothing.
BOOST_AUTO_TEST_CASE(lp_single_and_zero_bonus) {
    auto one = distribute_lp({ {500, 1234} }, 250);
    BOOST_CHECK_EQUAL(one[0], 250);
    auto none = distribute_lp({ {500, 10}, {500, 20} }, 0);
    BOOST_CHECK_EQUAL(none[0], 0);
    BOOST_CHECK_EQUAL(none[1], 0);
}
