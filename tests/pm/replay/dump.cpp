#include <graphene/chain/pm/parimutuel.hpp>
#include <cstdio>
using namespace graphene::chain::pm;
static void run(const char* tag, int64_t fp, std::vector<winner_in> w) {
    settle_params p; p.losers_sum = 0; p.forfeit_pool = fp;
    auto r = compute_settlement(p, w);
    long long in = p.losers_sum + p.forfeit_pool, out = r.oracle_take + r.creator_take + r.lp_bonus;
    printf("%s  forfeit_pool=%lld  winners=%zu\n", tag, (long long)fp, w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        in += w[i].amount; out += r.winner_payout[i];
        printf("   winner[%zu] amount=%-6lld weight=%-6lld  payout=%lld\n",
               i, (long long)w[i].amount, (long long)w[i].weight, (long long)r.winner_payout[i]);
    }
    printf("   lp_bonus=%lld oracle=%lld creator=%lld\n   Sigma_in=%lld  Sigma_out=%lld (int64-wrapped)\n\n",
           (long long)r.lp_bonus, (long long)r.oracle_take, (long long)r.creator_take, in, out);
}
int main() {
    run("A two unequal winners:", -1000, { {1000,1000,0}, {3000,3000,0} });
    run("B two equal winners:  ", -1000, { {1000,1000,0}, {1000,1000,0} });
    run("C single winner:      ", -1000, { {4000,4000,0} });
    run("D control, positive:  ",  1000, { {1000,1000,0}, {3000,3000,0} });
    return 0;
}
