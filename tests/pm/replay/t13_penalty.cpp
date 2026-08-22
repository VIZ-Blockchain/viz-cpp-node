// t13_penalty.cpp — B9 wires time_penalty live (26fd26a). compute_time_penalty() clamps its
// result to the median `pm_max_time_penalty`, and compute_settlement() spends it as
//     penalty = profit * time_penalty / 1e6
// so the whole thing is only safe while pm_max_time_penalty <= 1e6. chain_properties_pm::validate()
// bounds every other ratio field (pm_lazy_*, pm_leverage_*, *_percent <= 10000 / <= 100) but has
// NO assert on pm_max_time_penalty. Before 26fd26a the field was inert (time_penalty was always 0),
// so the missing bound was harmless. Now it is live.
//
// Checks the documented header contract:
//   Sigma winner_payout + oracle_take + creator_take + lp_bonus == Sigma amount + losers_sum + forfeit_pool
#include <graphene/chain/pm/parimutuel.hpp>
#include <cstdio>
using namespace graphene::chain::pm;

static void run(const char* tag, uint32_t tp) {
    settle_params p;
    p.losers_sum = 100000;      // 100 VIZ of losing stakes
    p.forfeit_pool = 0;
    p.oracle_fee_percent = 0; p.creator_fee_percent = 0; p.liquidity_fee_percent = 0;
    std::vector<winner_in> w = { {50000, 50000, tp} };   // one winner, 50 VIZ staked
    auto r = compute_settlement(p, w);

    long long in = p.losers_sum + p.forfeit_pool + w[0].amount;
    long long out = r.oracle_take + r.creator_take + r.lp_bonus + r.winner_payout[0];
    printf("  %-30s time_penalty=%-9u payout=%-10lld lp_bonus=%-9lld  Sin=%lld Sout=%lld  %s\n",
           tag, tp, (long long)r.winner_payout[0], (long long)r.lp_bonus, in, out,
           out == in ? "conserved" : "*** VIOLATES THE HEADER CONTRACT ***");
}

int main() {
    printf("pm_max_time_penalty default = 1000000 (1e6 = 100%% of profit)\n");
    printf("chain_properties_pm::validate() does not bound it; median-voted, so any uint32 passes.\n\n");
    run("no penalty",                        0);
    run("50%% of profit",                     500000);
    run("100%% of profit (the cap's intent)", 1000000);
    printf("\n  --- median voted above 1e6 (validate() accepts) ---\n");
    run("2e6",                               2000000);
    run("4e6",                               4000000);
    run("uint32 max",                        4294967295u);
    printf("\n  winner staked 50000 and won; at 4e6 the payout is what?\n");
    printf("  (settle_market credits only `if (payout.value > 0)`, so a negative payout means the\n"
           "   winner's principal is simply never returned, while lp_bonus keeps the phantom penalty)\n");
    return 0;
}
