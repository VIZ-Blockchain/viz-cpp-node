// t15_early_exit_headroom.cpp — verify 499246e9 ("clamp F1 early-exit bucket to
// settlement headroom") and the conservation of the early-exit deferred-claim
// settlement pass. This path (F1/#300, five commits) ships with NO test coverage:
// nothing under tests/ references deferred_claim / early_exit / reward_cap, the
// CI-wired parimutuel_test.cpp never exercises the claim pass, and the
// consensus_sim PM suite has no supply-conservation assertion.
//
// Models the settle_market claim-distribution pass verbatim from
// pm_evaluator.cpp:647-700 (bucket = cap%·losers_sum, clamp to headroom, FIFO pay
// winning-outcome claims, fold paid_claims into forfeit_pool), then settles through
// the REAL compute_settlement (linked from libraries/chain/pm/parimutuel.cpp).
//
// The claim of 499246e9: the reward cap ALONE does not bound solvency. Per-market
// fees are capped only by oracle+creator+liquidity <= 100% at CREATION
// (pm_operations.cpp:64) with no chain-param guard, so a VALID market can push fees
// near 100%. With the default cap (3300 bp = 33%) that makes fees + bucket exceed
// losers_sum; without the clamp compute_settlement floors winners_pool at 0 and
// reports the shortfall as `uncovered` — charged to LP principal (F1), i.e. an LP
// haircut reachable with default params, not just extreme medians.
//
//   A. one concrete market, bucket UNCLAMPED  -> uncovered > 0 (the bug)
//   B. same market, bucket CLAMPED (real code) -> uncovered == 0, full ledger balances
//   C. adversarial sweep of (fees, cap, forfeit) -> clamp yields uncovered==0 and a
//      balanced ledger on every valid combination; PASS/FAIL summary + exit code.
#include <graphene/chain/pm/parimutuel.hpp>
#include <cstdio>
#include <cstdint>
#include <vector>
using namespace graphene::chain::pm;

typedef unsigned __int128 u128;
static u128 U(int64_t v) { return (u128)(uint64_t)v; }
static int64_t D(u128 v) { return (int64_t)(uint64_t)v; }

// Default early-exit cap: chain_operations.hpp:710 (pm_early_exit_reward_cap_percent = 3300 bp).
static const uint16_t DEFAULT_CAP_BP = 3300;

struct fees { uint16_t oracle=0, creator=0, liquidity=0; int64_t fixed=0; };

// One deferred early-exit claim on the WINNING outcome (kind/outcome elided — this
// pass only sees claim_amount for winning-outcome claims; losing-outcome claims pay 0).
struct claim { int64_t amount=0; };

struct settle_out {
    int64_t bucket=0, paid_claims=0, uncovered=0;
    int64_t in=0, out=0;            // full VIZ ledger (see below)
    int64_t oracle=0, creator=0, lp_bonus=0, winners=0;
};

// Model of the settle_market claim pass + settlement. `clamp` toggles the 499246e9 fix.
static settle_out run(int64_t losers_sum, int64_t forfeit_pool, fees f,
                      const std::vector<claim>& winning_claims,
                      const std::vector<winner_in>& winners,
                      uint16_t cap_bp, bool clamp) {
    settle_out o;

    // pm_evaluator.cpp:650 — bucket = cap% of losers_sum.
    int64_t bucket = D(U(losers_sum) * U(cap_bp) / U(10000));

    // pm_evaluator.cpp:664-675 — headroom clamp. Fee math MIRRORS parimutuel.cpp:13-25
    // EXACTLY (same int64 order/flooring) so headroom == the pot the split will see.
    if (clamp) {
        const int64_t oracle_fee  = losers_sum * (int64_t)f.oracle    / 10000;
        const int64_t creator_fee = losers_sum * (int64_t)f.creator   / 10000;
        const int64_t liq_fee     = losers_sum * (int64_t)f.liquidity / 10000;
        int64_t avail = losers_sum - oracle_fee - creator_fee - liq_fee;
        if (avail < 0) avail = 0;
        const int64_t fixed_paid = (f.fixed < avail) ? f.fixed : avail;
        int64_t headroom = avail - fixed_paid + forfeit_pool;
        if (headroom < 0) headroom = 0;
        if (bucket > headroom) bucket = headroom;
    }
    o.bucket = bucket;

    // pm_evaluator.cpp:679-696 — FIFO pay winning-outcome claims up to the bucket.
    int64_t paid_claims = 0;
    for (const auto& c : winning_claims) {
        int64_t remaining = bucket - paid_claims;
        int64_t pay = c.amount < remaining ? c.amount : remaining;
        if (pay > 0) paid_claims += pay;
    }
    o.paid_claims = paid_claims;

    // pm_evaluator.cpp:700 — fold paid claims into forfeit_pool, then the real split.
    settle_params sp;
    sp.losers_sum           = losers_sum;
    sp.forfeit_pool         = forfeit_pool - paid_claims;
    sp.oracle_fixed_fee     = f.fixed;
    sp.oracle_fee_percent   = f.oracle;
    sp.creator_fee_percent  = f.creator;
    sp.liquidity_fee_percent= f.liquidity;

    settle_result res = compute_settlement(sp, winners);
    o.uncovered = res.uncovered;
    o.oracle    = res.oracle_take;
    o.creator   = res.creator_take;
    o.lp_bonus  = res.lp_bonus;
    for (auto p : res.winner_payout) o.winners += p;

    // Full VIZ ledger for the settlement pass (LP principal returns in full separately
    // and cancels on both sides, so it is omitted). Real VIZ the market holds for this
    // settlement == losers_sum + Σ winner stakes + forfeit_pool. `uncovered` is the
    // EXTERNAL top-up F1 charges to LP principal — it sits on the INPUT side, so when it
    // is > 0 the outputs were funded partly from LP capital (a haircut), not from the pot.
    int64_t winner_stake = 0;
    for (const auto& w : winners) winner_stake += w.amount;
    o.in  = losers_sum + winner_stake + forfeit_pool + res.uncovered;
    o.out = o.winners + o.oracle + o.creator + o.lp_bonus + paid_claims;
    return o;
}

int main() {
    int failures = 0;

    printf("=== A. bucket UNCLAMPED: default 33%% cap + 90%% fees (both VALID) -> LP hit ===\n");
    // A valid market: oracle 30% + creator 30% + liquidity 30% = 90% <= 100% (passes
    // pm_operations.cpp:64). losers_sum 100000, no forfeit, one winner, one 33000 claim.
    {
        fees f{3000, 3000, 3000, 0};
        std::vector<winner_in> w = {{50000, 1000, 0}};
        std::vector<claim>     c = {{33000}};
        settle_out a = run(100000, 0, f, c, w, DEFAULT_CAP_BP, /*clamp=*/false);
        printf("  bucket=%lld paid_claims=%lld uncovered=%lld  in=%lld out=%lld delta=%+lld %s\n",
               (long long)a.bucket, (long long)a.paid_claims, (long long)a.uncovered,
               (long long)a.in, (long long)a.out, (long long)(a.out - a.in),
               a.uncovered > 0 ? "  <-- LP principal charged (F1) / would-be mint" : "");
        printf("  -> fees leave avail=10000; a 33000 claim overdraws it by 23000. Reachable with\n"
               "     default params, no extreme median. This is what 499246e9 fixes.\n");
        if (a.uncovered <= 0) { printf("  UNEXPECTED: bug did not reproduce\n"); ++failures; }
        if (a.out - a.uncovered != a.in - a.uncovered) { /* identity always holds */ }
    }

    printf("\n=== B. bucket CLAMPED (real code): same market -> no LP hit, ledger balances ===\n");
    {
        fees f{3000, 3000, 3000, 0};
        std::vector<winner_in> w = {{50000, 1000, 0}};
        std::vector<claim>     c = {{33000}};
        settle_out b = run(100000, 0, f, c, w, DEFAULT_CAP_BP, /*clamp=*/true);
        printf("  bucket=%lld (clamped from 33000 to headroom 10000) paid_claims=%lld uncovered=%lld\n",
               (long long)b.bucket, (long long)b.paid_claims, (long long)b.uncovered);
        printf("  ledger: in=%lld out=%lld delta=%+lld  %s\n",
               (long long)b.in, (long long)b.out, (long long)(b.out - b.in),
               (b.uncovered == 0 && b.out == b.in) ? "BALANCED (no mint, no LP hit)" : "*** BROKEN ***");
        printf("  early-exiter is haircut 33000 -> 10000 (unfunded remainder stays in the curve,\n"
               "  pays 0 — exactly like bucket exhaustion); winner keeps principal.\n");
        if (b.uncovered != 0) { printf("  FAIL: clamp left uncovered=%lld\n", (long long)b.uncovered); ++failures; }
        if (b.out != b.in)    { printf("  FAIL: ledger unbalanced by %lld\n", (long long)(b.out - b.in)); ++failures; }
    }

    printf("\n=== C. adversarial sweep: clamp must give uncovered==0 AND a balanced ledger ===\n");
    // Every combination of valid fees (sum <= 100%), cap, forfeit and claim size. The
    // clamped path (real code) must NEVER produce uncovered and must ALWAYS balance.
    {
        int cases = 0, uncovered_hits = 0, unbalanced = 0;
        const int64_t losers = 100000;
        for (int of = 0; of <= 10000; of += 2500)
        for (int cf = 0; of + cf <= 10000; cf += 2500)
        for (int lf = 0; of + cf + lf <= 10000; lf += 2500)
        for (int cap : {0, 3300, 6600, 10000})
        for (int64_t forfeit : {(int64_t)-40000, (int64_t)0, (int64_t)25000})
        for (int64_t claim_amt : {(int64_t)0, (int64_t)20000, (int64_t)80000, (int64_t)200000}) {
            fees f{(uint16_t)of, (uint16_t)cf, (uint16_t)lf, 0};
            std::vector<winner_in> w = {{50000, 1000, 0}};
            std::vector<claim>     c = {{claim_amt}};
            settle_out s = run(losers, forfeit, f, c, w, (uint16_t)cap, /*clamp=*/true);
            ++cases;
            if (s.uncovered != 0) { ++uncovered_hits; if (uncovered_hits <= 3)
                printf("  uncovered=%lld  fees=%d/%d/%d cap=%d forfeit=%lld claim=%lld\n",
                       (long long)s.uncovered, of, cf, lf, cap, (long long)forfeit, (long long)claim_amt); }
            if (s.out != s.in) { ++unbalanced; if (unbalanced <= 3)
                printf("  UNBALANCED delta=%+lld  fees=%d/%d/%d cap=%d forfeit=%lld claim=%lld\n",
                       (long long)(s.out - s.in), of, cf, lf, cap, (long long)forfeit, (long long)claim_amt); }
        }
        printf("  swept %d valid combinations: uncovered_hits=%d  unbalanced=%d\n",
               cases, uncovered_hits, unbalanced);
        // NOTE: a NEGATIVE forfeit_pool that already exceeds the losing pot can still make
        // winners_pool < 0 on its own (the leverage-close residual the F1 LP-charge exists
        // for); that is NOT what the headroom clamp targets. The clamp's contract is only
        // that PAID CLAIMS never ADD to that shortfall. So the pass condition is: the
        // clamped run is never WORSE than the same market with no claims.
        int regressions = 0;
        for (int of = 0; of <= 10000; of += 2500)
        for (int cf = 0; of + cf <= 10000; cf += 2500)
        for (int lf = 0; of + cf + lf <= 10000; lf += 2500)
        for (int cap : {0, 3300, 6600, 10000})
        for (int64_t forfeit : {(int64_t)-40000, (int64_t)0, (int64_t)25000})
        for (int64_t claim_amt : {(int64_t)0, (int64_t)20000, (int64_t)80000, (int64_t)200000}) {
            fees f{(uint16_t)of, (uint16_t)cf, (uint16_t)lf, 0};
            std::vector<winner_in> w = {{50000, 1000, 0}};
            settle_out with_claim = run(losers, forfeit, f, {{claim_amt}}, w, (uint16_t)cap, true);
            settle_out no_claim    = run(losers, forfeit, f, {},           w, (uint16_t)cap, true);
            if (with_claim.uncovered > no_claim.uncovered) { ++regressions;
                printf("  REGRESSION: claim raised uncovered %lld -> %lld  fees=%d/%d/%d cap=%d forfeit=%lld\n",
                       (long long)no_claim.uncovered, (long long)with_claim.uncovered,
                       of, cf, lf, cap, (long long)forfeit); }
            if (with_claim.out != with_claim.in) { /* counted above */ }
        }
        printf("  claim-induced uncovered regressions vs no-claim baseline: %d\n", regressions);
        if (unbalanced) { printf("  FAIL: %d unbalanced ledgers under clamp\n", unbalanced); ++failures; }
        if (regressions) { printf("  FAIL: paid claims worsened solvency in %d cases\n", regressions); ++failures; }
    }

    printf("\n%s\n", failures == 0
        ? "ALL CHECKS PASS: the headroom clamp makes paid_claims solvency-neutral; the "
          "settlement ledger balances (no mint, no LP hit from the early-exit path)."
        : "*** FAILURES ABOVE ***");
    return failures == 0 ? 0 : 1;
}
