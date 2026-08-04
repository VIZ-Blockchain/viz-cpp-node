#include <graphene/chain/pm/parimutuel.hpp>

#include <fc/uint128_t.hpp>

namespace graphene { namespace chain { namespace pm {

    settle_result compute_settlement(const settle_params& p,
                                     const std::vector<winner_in>& winners) {
        settle_result r;

        // Fees are bp OF losers_sum (10000 = 100%); oracle fixed fee is taken from the
        // remainder (capped) so it is never minted.
        int64_t oracle_fee  = p.losers_sum * (int64_t)p.oracle_fee_percent   / 10000;
        int64_t creator_fee = p.losers_sum * (int64_t)p.creator_fee_percent  / 10000;
        int64_t liq_fee     = p.losers_sum * (int64_t)p.liquidity_fee_percent / 10000;
        int64_t avail = p.losers_sum - oracle_fee - creator_fee - liq_fee;
        if (avail < 0) avail = 0;
        int64_t fixed_paid = (p.oracle_fixed_fee < avail) ? p.oracle_fixed_fee : avail;
        // forfeit_pool is a SIGNED accumulator: leverage positions that close in profit route
        // a negative residual (cv > total_bet — the surplus was paid to the winner out of the
        // pool, so it must reduce what parimutuel winners can share). If leverage wins exceed
        // the losers' pool the sum goes negative; floor at 0 so winners simply get their
        // principal back (line 44) and the negative NEVER reaches the uint64 casts below — an
        // unclamped negative would wrap to ~1.8e19 and mint. (B3: PR #124 review.)
        int64_t winners_pool = avail - fixed_paid + p.forfeit_pool;
        if (winners_pool < 0) winners_pool = 0;

        r.oracle_take  = oracle_fee + fixed_paid;
        r.creator_take = creator_fee;
        r.lp_bonus     = liq_fee;

        fc::uint128_t total_weight = 0;
        for (const auto& w : winners)
            total_weight += fc::uint128_t((uint64_t)w.weight);

        r.winner_payout.reserve(winners.size());

        if (total_weight == 0) {
            // No winning tokens: the whole pool is undistributed → LP.
            r.lp_bonus += winners_pool;
            return r;
        }

        int64_t distributed = 0;
        for (const auto& w : winners) {
            int64_t profit = (int64_t)(fc::uint128_t((uint64_t)winners_pool)
                              * fc::uint128_t((uint64_t)w.weight) / total_weight).lo;
            int64_t penalty = (int64_t)(fc::uint128_t((uint64_t)profit)
                              * fc::uint128_t((uint64_t)w.time_penalty)
                              / fc::uint128_t((uint64_t)1000000)).lo;
            // F3 defensive clamp: validate() bounds pm_max_time_penalty <= 1e6 so penalty <= profit
            // already, but never let a (mis-configured / legacy) median push penalty past profit —
            // that would make payout < principal and settle_market silently drop the winner's stake.
            if (penalty > profit) penalty = profit;
            r.winner_payout.push_back(w.amount + profit - penalty);
            distributed += profit;
            r.lp_bonus  += penalty;
        }
        // Rounding dust from the weight split → LP (keeps Σ exact).
        int64_t dust = winners_pool - distributed;
        if (dust > 0) r.lp_bonus += dust;

        return r;
    }

    std::vector<int64_t> distribute_lp(const std::vector<lp_in>& lps, int64_t bonus) {
        std::vector<int64_t> out(lps.size(), 0);
        if (lps.empty() || bonus <= 0) return out;

        std::vector<fc::uint128_t> w(lps.size());
        fc::uint128_t total = 0;
        for (size_t i = 0; i < lps.size(); ++i) {
            int64_t sec = lps[i].seconds_active < 0 ? 0 : lps[i].seconds_active;
            // Minute granularity keeps principal×time within 128 bits; the +1 floor
            // makes equal-time LPs reduce to a plain principal-pro-rata split.
            w[i] = fc::uint128_t((uint64_t)lps[i].principal)
                 * fc::uint128_t((uint64_t)(sec / 60 + 1));
            total += w[i];
        }
        if (total == 0) return out; // all-zero principal

        int64_t left = bonus;
        for (size_t i = 0; i < lps.size(); ++i) {
            if (i + 1 == lps.size()) { out[i] = left; break; } // last absorbs remainder
            int64_t s = (int64_t)(fc::uint128_t((uint64_t)bonus) * w[i] / total).lo;
            out[i] = s;
            left -= s;
        }
        return out;
    }

}}} // graphene::chain::pm
