// t11_cancel_move.cpp — b06bdbf's B6 guard vs. the k corruption it claims to fix.
//
// The commit message says the assert fixes "underflowing share_type and corrupting
// k = reserve_a x reserve_b". The assert only rejects the case where the reversal would make a
// reserve NEGATIVE. It says nothing about the case where the reversal succeeds on a curve that
// has moved: pm_cancel_bet still reverses the NOMINAL pair (amount, weight) recorded at bet time,
// so whenever any other operation touched the curve in between, the post-cancel state is not the
// state the market would have had without the bet — and k is rebuilt from the wrong reserves.
//
// Counterfactual = the identical market with the same intervening op but no bet/cancel pair.
// Every transition mirrors pm_evaluator.cpp on origin/pm (f697e5c) incl. truncating uint128 div.
#include <cstdio>
#include <cstdint>
typedef unsigned __int128 u128;
static u128 U(int64_t v) { return (u128)(uint64_t)v; }
static int64_t D(u128 v) { return (int64_t)(uint64_t)v; }

struct market { int64_t ra = 0, rb = 0, bets = 0, L = 0; u128 k = 0; };
struct bet    { int64_t amount = 0, weight = 0; int side = 0; };

static market make_market(int64_t liq) {
    market m; int64_t h = liq / 2;
    m.ra = h; m.rb = liq - h; m.L = liq; m.k = U(m.ra) * U(m.rb); return m;
}
// pm_place_bet, binary CPMM (:1110-1140) — k preserved across the fill
static bet place(market& m, int side, int64_t amount) {
    int64_t rin = side == 0 ? m.ra : m.rb, rout = side == 0 ? m.rb : m.ra;
    int64_t nout = D(m.k / U(rin + amount));
    bet b; b.amount = amount; b.weight = rout - nout; b.side = side;
    if (side == 0) { m.ra += amount; m.rb = nout; } else { m.rb += amount; m.ra = nout; }
    m.bets += amount; return b;
}
// pm_cancel_bet (:1305-1322) with b06bdbf's B6 assert. Returns false = cancel refused.
static bool cancel(market& m, const bet& b) {
    int64_t rin = b.side == 0 ? m.ra : m.rb;
    if (rin < b.amount) return false;                       // <-- b06bdbf FC_ASSERT
    if (b.side == 0) { m.ra -= b.amount; m.rb += b.weight; }
    else             { m.rb -= b.amount; m.ra += b.weight; }
    m.bets -= b.amount;
    m.k = U(m.ra) * U(m.rb);                                // k rebuilt from whatever is there
    return true;
}
// pm_add_liquidity / pm_withdraw_liquidity after 172d87c — proportional, price-neutral
static void add_liq(market& m, int64_t a) {
    if (m.L > 0) { u128 n = U(m.L + a), d = U(m.L);
        m.ra = D(U(m.ra) * n / d); m.rb = D(U(m.rb) * n / d); }
    m.L += a; m.k = U(m.ra) * U(m.rb);
}
static bool wd_liq(market& m, int64_t w, int64_t minliq) {
    if (m.L - w < minliq) return false;
    u128 n = U(m.L - w), d = U(m.L);
    m.ra = D(U(m.ra) * n / d); m.rb = D(U(m.rb) * n / d);
    m.L -= w; m.k = U(m.ra) * U(m.rb); return true;
}
// implied probability of side A, x1e4, as the CPMM curve prices it
static double pA(const market& m) { return 100.0 * m.rb / (double)(m.ra + m.rb); }

// weight a fresh `stake` on side A buys from a given state — the settlement claim
static int64_t probe(market m, int64_t stake) { return place(m, 0, stake).weight; }

static void report(const char* tag, market got, market want, int64_t probe_stake) {
    int64_t wg = probe(got, probe_stake), ww = probe(want, probe_stake);
    printf("  %-34s ra=%-9lld rb=%-9lld  P(A)=%6.2f%%  k=%-14llu next-%lld buys %lld\n",
           tag, (long long)got.ra, (long long)got.rb, pA(got),
           (unsigned long long)got.k, (long long)probe_stake, (long long)wg);
    printf("  %-34s ra=%-9lld rb=%-9lld  P(A)=%6.2f%%  k=%-14llu             %lld   %+.2f%%\n",
           "  counterfactual (no bet/cancel)", (long long)want.ra, (long long)want.rb, pA(want),
           (unsigned long long)want.k, (long long)ww, 100.0 * (wg - ww) / (double)ww);
}

int main() {
    const int64_t MINLIQ = 100000;

    printf("=== A. the case b06bdbf DOES stop (t8's detonation) ===\n");
    {
        market m = make_market(400000);
        bet a = place(m, 0, 100000);
        wd_liq(m, 300000, MINLIQ);
        printf("  after legal 300k withdrawal: ra=%lld  bet nominal=%lld -> cancel %s\n",
               (long long)m.ra, (long long)a.amount, cancel(m, a) ? "APPLIED" : "REFUSED (guard holds)");
    }

    printf("\n=== B. same shape, withdrawal small enough to pass the guard ===\n");
    {
        market got = make_market(400000), want = make_market(400000);
        bet a = place(got, 0, 50000);
        wd_liq(got, 100000, MINLIQ);
        wd_liq(want, 100000, MINLIQ);                 // counterfactual: same withdrawal, no bet
        printf("  cancel %s\n", cancel(got, a) ? "APPLIED" : "REFUSED");
        report("after bet+withdraw+cancel", got, want, 50000);
    }

    printf("\n=== C. no LP op at all — just an opposing bet in between ===\n");
    {
        market got = make_market(400000), want = make_market(400000);
        bet a = place(got, 0, 50000);
        place(got, 1, 50000);
        place(want, 1, 50000);                        // counterfactual: only the opposing bet
        printf("  cancel %s\n", cancel(got, a) ? "APPLIED" : "REFUSED");
        report("after bet+opposing bet+cancel", got, want, 50000);
    }

    printf("\n=== D. LP ADD in between ===\n");
    {
        market got = make_market(400000), want = make_market(400000);
        bet a = place(got, 0, 50000);
        add_liq(got, 200000);
        add_liq(want, 200000);
        printf("  cancel %s\n", cancel(got, a) ? "APPLIED" : "REFUSED");
        report("after bet+add+cancel", got, want, 50000);
    }

    printf("\n=== E. repeat the C cycle — does it ratchet? ===\n");
    {
        market m = make_market(400000);
        for (int i = 1; i <= 10; ++i) {
            bet a = place(m, 0, 50000);
            bet opp = place(m, 1, 50000);
            if (!cancel(m, a)) { printf("  cycle %d: cancel refused\n", i); break; }
            if (!cancel(m, opp)) { printf("  cycle %2d: opposing cancel refused; ra=%lld rb=%lld\n", i, (long long)m.ra, (long long)m.rb); break; }
            if (i == 1 || i == 5 || i == 10)
                printf("  after cycle %2d: ra=%-8lld rb=%-8lld k=%-14llu depth=%lld (started 400000)\n",
                       i, (long long)m.ra, (long long)m.rb, (unsigned long long)m.k,
                       (long long)(m.ra + m.rb));
        }
    }

    printf("\n=== F. sweep: how often, and how large, is the claim-weight excess? ===\n");
    {
        long long tot = 0, refused = 0, favourable = 0, adverse = 0;
        double best = 0, worst = 0;
        for (int64_t liq = 200000; liq <= 900000; liq += 47000)
        for (int64_t amt = 5000;  amt <= 200000; amt += 9973)
        for (int64_t opp = 5000;  opp <= 200000; opp += 11117) {
            market got = make_market(liq), want = make_market(liq);
            bet a = place(got, 0, amt);
            place(got, 1, opp);
            place(want, 1, opp);
            ++tot;
            if (!cancel(got, a)) { ++refused; continue; }
            int64_t wg = probe(got, amt), ww = probe(want, amt);
            if (ww <= 0) continue;
            double p = 100.0 * (wg - ww) / (double)ww;
            if (p > 0)      { ++favourable; if (p > best)  best  = p; }
            else if (p < 0) { ++adverse;    if (p < worst) worst = p; }
        }
        printf("  %lld triples: %lld refused by the guard, %lld inflate the next claim (worst %+.2f%%),\n"
               "  %lld deflate it (worst %+.2f%%)\n", tot, refused, favourable, best, adverse, worst);
    }
    return 0;
}
