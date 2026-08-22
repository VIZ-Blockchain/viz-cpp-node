// t17_leverage_math_invariants.cpp — replay the REAL leverage/margin math
// (libraries/chain/pm/leverage.cpp, linked, not modelled) and assert the safety
// properties the margin subsystem relies on, across adversarial + fuzzed inputs.
//
// These are the invariants that make the pool solvent and the round-trip non-profitable;
// if any breaks, a bettor could extract from the curve/pool or the open-time safety check
// would admit an under-collateralized position.
//
//   I.   bounded k on buy: the stored k is NOT reassigned on a bet (pm_place_bet keeps the
//        original k), and flooring the divided reserve gives new_reserve_a × new_reserve_b
//        in (k − divisor, k] — i.e. the product never EXCEEDS k (the pool can't gain phantom
//        constant-product) and loses at most one divisor-unit (so tokens_out isn't unboundedly
//        inflated in the bettor's favour). Combined with II this bounds any rounding edge.
//   II.  round-trip non-profit: buy `amount`, immediately cancel the tokens at the SAME
//        reserves → cancel_value <= amount. A self-sandwich cannot mint against the curve.
//   III. monotonicity: cancel_value_after_opposing is non-increasing in the opposing bet
//        `m`. This is the assumption behind worst_opposing_bet being the worst case; if it
//        failed, the open-time solvency check would use the wrong bound.
//   IV.  solvency of max_leverage_loan: the loan it returns actually satisfies the
//        worst-case safety inequality cvw >= threshold_safe (never hands out an
//        under-margined loan), and 0 is always safe/returned when nothing qualifies.
//   V.   no negative / no wrap: every output >= 0 on the full reachable reserve range.
//
// Build: part of tests/pm/replay (links libraries/chain/pm/leverage.cpp). See build.sh.
#include <graphene/chain/pm/leverage.hpp>
#include <fc/uint128_t.hpp>
#include <cstdio>
#include <cstdint>
using namespace graphene::chain::pm::leverage;

static long long failures = 0;
static fc::uint128_t K(int64_t ra, int64_t rb) {
    return fc::uint128_t((uint64_t)ra) * fc::uint128_t((uint64_t)rb);
}
// product of post-fill reserves as 128-bit (for the k >= invariant)
static fc::uint128_t prod(int64_t a, int64_t b) {
    return fc::uint128_t((uint64_t)a) * fc::uint128_t((uint64_t)b);
}

int main() {
    printf("=== I. bounded k on buy: k-divisor < new_ra x new_rb <= k (no phantom k, bounded dust) ===\n");
    {
        struct C { int64_t ra, rb, amt; int oc; };
        C cs[] = {
            {3000000, 3000000, 100000, 0}, {3000000, 3000000, 100000, 1},
            {9000000, 1000000, 500000, 0}, {1000000, 9000000, 500000, 1},
            {1000000000000LL, 1000000, 999999, 0}, {100, 100, 1, 0},
        };
        for (auto& c : cs) {
            fc::uint128_t k = K(c.ra, c.rb);
            cpmm_fill f = cpmm_buy(c.ra, c.rb, k, c.amt, c.oc);
            fc::uint128_t p = prod(f.new_reserve_a, f.new_reserve_b);
            // the reserve that was RECOMPUTED via floor(k/x) is the "out" reserve
            int64_t divisor = (c.oc == 0) ? f.new_reserve_a : f.new_reserve_b;
            bool le_k   = (p <= k);                                   // never exceeds k
            bool bounded = (p + fc::uint128_t((uint64_t)divisor) >= k); // loses < 1 divisor-unit
            bool tok_ok = f.tokens >= 0;
            if (!le_k || !bounded || !tok_ok) ++failures;
            printf("  ra=%-13lld rb=%-13lld amt=%-8lld oc=%d tokens=%-10lld  %s%s\n",
                   (long long)c.ra, (long long)c.rb, (long long)c.amt, c.oc, (long long)f.tokens,
                   (le_k && bounded) ? "k bounded" : "*** K OUT OF BOUNDS ***", tok_ok ? "" : " *** NEG TOKENS ***");
        }
    }

    printf("\n=== II. round-trip non-profit: buy then cancel at same reserves <= amount ===\n");
    {
        struct C { int64_t ra, rb, amt; int oc; };
        C cs[] = {
            {3000000, 3000000, 100000, 0}, {3000000, 3000000, 900000, 1},
            {5000000, 5000000, 4999999, 0}, {2000000, 8000000, 100000, 1},
            {100, 100, 50, 0}, {1000000000LL, 1000000000LL, 1, 0},
        };
        for (auto& c : cs) {
            fc::uint128_t k = K(c.ra, c.rb);
            cpmm_fill f = cpmm_buy(c.ra, c.rb, k, c.amt, c.oc);
            // cancel the freshly-bought tokens at the POST-buy reserves (immediate round-trip)
            int64_t cv = cancel_value(f.new_reserve_a, f.new_reserve_b, k, f.tokens, c.oc);
            bool ok = (cv <= c.amt) && (cv >= 0);
            if (!ok) ++failures;
            printf("  amt=%-9lld tokens=%-10lld cancel=%-9lld  %s\n",
                   (long long)c.amt, (long long)f.tokens, (long long)cv,
                   ok ? "no profit" : "*** ROUND-TRIP PROFIT ***");
        }
    }

    printf("\n=== III. monotonicity: cancel_value_after_opposing non-increasing in m ===\n");
    {
        int64_t ra = 4000000, rb = 4000000; fc::uint128_t k = K(ra, rb);
        cpmm_fill f = cpmm_buy(ra, rb, k, 500000, 0); // a position on A
        int64_t prev = -1; bool mono = true; long long steps = 0;
        for (int64_t m = 0; m <= 2000000; m += 1000) {
            int64_t cv = cancel_value_after_opposing(f.new_reserve_a, f.new_reserve_b, k, f.tokens, 0, m);
            if (cv < 0) { mono = false; }
            if (prev >= 0 && cv > prev) { mono = false; }
            prev = cv; ++steps;
        }
        if (!mono) ++failures;
        printf("  swept %lld opposing-bet sizes; cancel_value monotonic non-increasing & >=0: %s\n",
               steps, mono ? "YES" : "*** NO ***");
    }

    printf("\n=== IV. max_leverage_loan returns a SOLVENT loan (cvw >= threshold_safe) ===\n");
    {
        // realistic-ish governance params: r=10%, s=20% safety, sl=10% max slippage, m_factor=50%
        const uint16_t r = 10, s = 20, sl = 10, mf = 50;
        struct C { int64_t ra, rb, coll; int oc; };
        C cs[] = {
            {3000000, 3000000, 200000, 0}, {3000000, 3000000, 200000, 1},
            {9000000, 1000000, 200000, 0}, {5000000, 5000000, 1000000, 0},
            {1000000, 1000000, 10, 0},
        };
        for (auto& c : cs) {
            fc::uint128_t k = K(c.ra, c.rb);
            int64_t hi = c.ra + c.rb; // generous upper bound
            int64_t L = max_leverage_loan(c.ra, c.rb, k, c.coll, c.oc, hi, r, s, sl, mf);
            bool solvent = true;
            if (L > 0) {
                cpmm_fill f = cpmm_buy(c.ra, c.rb, k, c.coll + L, c.oc);
                int64_t m = worst_opposing_bet(f.new_reserve_a, f.new_reserve_b, sl, mf);
                int64_t cvw = cancel_value_after_opposing(f.new_reserve_a, f.new_reserve_b, k, f.tokens, c.oc, m);
                int64_t thr = liquidation_threshold(L, r);
                int64_t thr_safe = (int64_t)(fc::uint128_t((uint64_t)thr) * fc::uint128_t(100u + s) / fc::uint128_t(100u)).lo;
                solvent = (cvw >= thr_safe);
                if (!solvent) ++failures;
                printf("  coll=%-8lld oc=%d -> loan=%-9lld cvw=%-10lld thr_safe=%-10lld  %s\n",
                       (long long)c.coll, c.oc, (long long)L, (long long)cvw, (long long)thr_safe,
                       solvent ? "solvent" : "*** UNDER-MARGINED ***");
            } else {
                printf("  coll=%-8lld oc=%d -> loan=0 (nothing qualifies; safe)\n", (long long)c.coll, c.oc);
            }
        }
    }

    printf("\n=== V. randomized fuzz over reachable reserves: no negative, k-preserved, no wrap ===\n");
    {
        uint64_t seed = 0xD1B54A32D192ED03ULL;
        auto rnd = [&]() { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; };
        long long checked = 0, bad = 0;
        for (int i = 0; i < 400000; ++i) {
            int64_t ra = (int64_t)(rnd() % 100000000ULL) + 1;
            int64_t rb = (int64_t)(rnd() % 100000000ULL) + 1;
            fc::uint128_t k = K(ra, rb);
            int oc = (int)(rnd() & 1);
            int64_t amt = (int64_t)(rnd() % 100000000ULL) + 1;
            cpmm_fill f = cpmm_buy(ra, rb, k, amt, oc);
            bool ok = true;
            if (f.tokens < 0 || f.new_reserve_a < 0 || f.new_reserve_b < 0) ok = false;
            // bounded k: product never exceeds k, and loses < 1 divisor-unit (no phantom k, bounded dust)
            fc::uint128_t p = prod(f.new_reserve_a, f.new_reserve_b);
            int64_t divisor = (oc == 0) ? f.new_reserve_a : f.new_reserve_b;
            if (p > k) ok = false;
            if (!(p + fc::uint128_t((uint64_t)divisor) >= k)) ok = false;
            // round-trip: immediate cancel never exceeds the amount paid
            int64_t cv = cancel_value(f.new_reserve_a, f.new_reserve_b, k, f.tokens, oc);
            if (cv < 0 || cv > amt) ok = false;
            // worst-case + threshold never negative
            int64_t m = worst_opposing_bet(f.new_reserve_a, f.new_reserve_b, 10, 50);
            int64_t cvw = cancel_value_after_opposing(f.new_reserve_a, f.new_reserve_b, k, f.tokens, oc, m);
            if (m < 0 || cvw < 0) ok = false;
            if (liquidation_threshold(amt, 10) < 0) ok = false;
            if (!ok) ++bad;
            ++checked;
        }
        if (bad) failures += bad;
        printf("  fuzzed %lld positions; invariant violations = %lld\n", checked, bad);
    }

    printf("\n%s (%lld failing assertions)\n",
           failures == 0 ? "ALL LEVERAGE-MATH INVARIANTS HOLD — no curve leak, no round-trip profit, solvent loans"
                         : "*** FAILURES ***",
           failures);
    return failures == 0 ? 0 : 1;
}
