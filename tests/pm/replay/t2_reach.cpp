// T2 — reachability of `curve_residual < 0` (and thus a negative pm_market.forfeit_pool).
// Replays pm_leverage_open → pm_place_bet(same side) → pm_leverage_close using the PR's
// own pure math (pm::leverage) and the verbatim reserve update from
// pm_evaluator.cpp:1052-1072 (pm_place_bet) / :1930-1943 (pm_leverage_close).
#include <graphene/chain/pm/leverage.hpp>
#include <cstdio>
using namespace graphene::chain::pm;

struct mkt { int64_t ra, rb; fc::uint128_t k; };

// pm_place_bet_evaluator::do_apply, binary branch (pm_evaluator.cpp:1052-1072)
static void place_bet(mkt& m, int64_t amount, int side) {
    int64_t rin = side == 0 ? m.ra : m.rb, rout = side == 0 ? m.rb : m.ra;
    int64_t new_out = (int64_t)(m.k / fc::uint128_t((uint64_t)(rin + amount))).lo;
    (void)rout;
    if (side == 0) { m.ra += amount; m.rb = new_out; }
    else           { m.rb += amount; m.ra = new_out; }
}

static void scenario(int64_t liquidity, int64_t collateral, int64_t loan,
                     int64_t same_side_bet, uint16_t r_percent) {
    mkt m; m.ra = liquidity / 2; m.rb = liquidity - m.ra;
    m.k = fc::uint128_t((uint64_t)m.ra) * fc::uint128_t((uint64_t)m.rb);

    const int64_t total_bet = collateral + loan;
    auto fill = leverage::cpmm_buy(m.ra, m.rb, m.k, total_bet, 0);   // pm_leverage_open, side A
    m.ra = fill.new_reserve_a; m.rb = fill.new_reserve_b;
    const int64_t tokens    = fill.tokens;
    const int64_t threshold = leverage::liquidation_threshold(loan, r_percent);

    place_bet(m, same_side_bet, 0);                                  // others pile onto side A

    const int64_t cv         = leverage::cancel_value(m.ra, m.rb, m.k, tokens, 0);
    const int64_t obligation = threshold;                            // + funding_paid (0 same block)
    const int64_t curve_residual = total_bet - cv;                   // pm_evaluator.cpp:1930

    printf("liq=%-9lld C=%-7lld L=%-7lld same-side bets=%-9lld | tokens=%-9lld total_bet=%-7lld "
           "cv=%-9lld obligation=%-7lld close_allowed=%s curve_residual=%lld%s\n",
           (long long)liquidity, (long long)collateral, (long long)loan, (long long)same_side_bet,
           (long long)tokens, (long long)total_bet, (long long)cv, (long long)obligation,
           cv >= obligation ? "yes" : "NO ", (long long)curve_residual,
           curve_residual < 0 ? "   <-- NEGATIVE -> forfeit_pool goes negative" : "");
}

int main() {
    for (int64_t bet : { 0LL, 50000LL, 200000LL, 500000LL, 1000000LL, 3000000LL })
        scenario(2000000, 100000, 200000, bet, 10);
    return 0;
}
