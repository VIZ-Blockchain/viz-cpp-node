// Cross-check of the investigation report's "exact threshold" claim.
#include <graphene/chain/pm/parimutuel.hpp>
#include <cstdio>
using namespace graphene::chain::pm;
int main() {
    for (int64_t fp : { -9599LL, -9600LL, -9601LL }) {
        settle_params p; p.losers_sum = 10000; p.forfeit_pool = fp;
        p.oracle_fee_percent = 100; p.creator_fee_percent = 100; p.liquidity_fee_percent = 200;
        std::vector<winner_in> w = { {100000, 1000, 0}, {100000, 3000, 0} };
        auto r = compute_settlement(p, w);
        int64_t avail = p.losers_sum - 100 - 100 - 200;
        int64_t mx = r.winner_payout[0] > r.winner_payout[1] ? r.winner_payout[0] : r.winner_payout[1];
        printf("forfeit_pool=%-7lld winners_pool=%-7lld max_payout=%-22lld %s\n",
               (long long)fp, (long long)(avail + fp), (long long)mx,
               mx > 200000 ? "CATASTROPHIC" : "ok");
    }
    return 0;
}
