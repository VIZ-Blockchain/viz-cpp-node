// t10_conserve.cpp — does b06bdbf's `if (winners_pool < 0) winners_pool = 0;` restore the
// conservation identity that pm_evaluator.cpp:395 documents?
//
//   "Money is conserved exactly:  Σ outputs == Σ all bet amounts + LP principal + forfeit_pool"
//
// Σ in  = Σ winner principals + losers_sum + forfeit_pool      (LP principal cancels: returned 1:1)
// Σ out = Σ winner payouts + oracle_take + creator_take + lp_bonus
//
// The catastrophic uint64 wrap (B3) is gone. This asks the narrower question: with the floor,
// is the remaining shortfall absorbed anywhere, or emitted?
#include <graphene/chain/pm/parimutuel.hpp>
#include <cstdio>
using namespace graphene::chain::pm;

static long long run(const char* tag, int64_t losers, int64_t fp,
                     std::vector<winner_in> w, bool verbose) {
    settle_params p;
    p.losers_sum = losers;
    p.forfeit_pool = fp;
    p.oracle_fee_percent = 100;    // 1%
    p.creator_fee_percent = 100;   // 1%
    p.liquidity_fee_percent = 200; // 2%
    auto r = compute_settlement(p, w);

    long long in = losers + fp, out = r.oracle_take + r.creator_take + r.lp_bonus;
    for (size_t i = 0; i < w.size(); ++i) { in += w[i].amount; out += r.winner_payout[i]; }

    int64_t oracle_fee  = losers * 100 / 10000;
    int64_t creator_fee = losers * 100 / 10000;
    int64_t liq_fee     = losers * 200 / 10000;
    int64_t avail = losers - oracle_fee - creator_fee - liq_fee; if (avail < 0) avail = 0;
    long long wp_raw = avail + fp;   // pre-floor winners_pool (oracle_fixed_fee = 0 here)

    if (verbose)
        printf("  %-28s losers=%-8lld fp=%-9lld winners_pool(raw)=%-9lld  Sin=%-9lld Sout=%-9lld  delta=%+lld %s\n",
               tag, (long long)losers, (long long)fp, wp_raw, in, out, out - in,
               out == in ? "conserved" : "*** EMITTED ***");
    return out - in;
}

int main() {
    printf("=== 1. floor engaged (winners_pool would be negative) ===\n");
    run("2 winners, no losers",      0,    -1000, {{1000,1000,0},{3000,3000,0}}, true);
    run("2 winners, small losers",   500,  -1000, {{1000,1000,0},{3000,3000,0}}, true);
    run("2 winners, fp == -avail",   1042, -1021, {{1000,1000,0},{3000,3000,0}}, true);
    run("single winner",             0,    -5000, {{4000,4000,0}}, true);
    run("no winning tokens",         0,    -5000, {{4000,0,0}}, true);

    printf("\n=== 2. floor NOT engaged (control) ===\n");
    run("positive forfeit_pool",     0,     1000, {{1000,1000,0},{3000,3000,0}}, true);
    run("negative but covered",      100000,-1000, {{1000,1000,0},{3000,3000,0}}, true);
    run("fp == 0",                   10000, 0,     {{1000,1000,0},{3000,3000,0}}, true);

    printf("\n=== 3. is the emission exactly |winners_pool|? sweep ===\n");
    long long bad = 0, tot = 0, worst = 0; bool law = true;
    for (int64_t losers = 0; losers <= 200000; losers += 7331)
    for (int64_t fp = -300000; fp <= 50000; fp += 4999) {
        int64_t of = losers*100/10000, cf = losers*100/10000, lf = losers*200/10000;
        int64_t avail = losers - of - cf - lf; if (avail < 0) avail = 0;
        long long wp = avail + fp;
        long long d = run("", losers, fp, {{1000,1000,0},{3000,3000,0}}, false);
        ++tot;
        if (d != 0) { ++bad; if (d > worst) worst = d; if (d != -wp) law = false; }
        else if (wp < 0) law = false;   // floor engaged but nothing emitted → law wrong
    }
    printf("  %lld/%lld settlements emit tokens; worst single emission = %+lld\n", bad, tot, worst);
    printf("  emission == |negative winners_pool| in every case: %s\n", law ? "YES" : "no");

    printf("\n=== 4. reachability, anchored on live testnet market 19 (forfeit_pool = -69227) ===\n");
    // Emission = |avail + forfeit_pool| when negative, avail = losers_sum*(1 - fee_bp/10000).
    // With the 4%% of fees used above, a market carrying market 19's residual emits for every
    // losers_sum below 69227/0.96 = 72111 raw.
    for (int64_t losers : {0LL, 20000LL, 50000LL, 72111LL, 100000LL})
        run("mkt-19 residual", losers, -69227, {{50000,50000,0}}, true);
    printf("  -> break-even losers_sum = %lld raw (%.3f VIZ); below it the settlement emits\n",
           (long long)(69227 * 10000 / 9600), 69227 * 10000 / 9600 / 1000.0);
    printf("  (forfeit_pool is the NET of every leverage close on the market, so the residual\n"
           "   grows with leverage volume while losers_sum does not.)\n");
    return 0;
}
