// t7_fixed.cpp — replay the 9-step cycle against On1x's 172d87c (price-neutral CPMM LP ops).
// Models pm_add_liquidity / pm_withdraw_liquidity / pm_place_bet / pm_cancel_bet verbatim,
// including fc::uint128_t truncating division.
#include <cstdio>
#include <cstdint>
#include <vector>
typedef unsigned __int128 u128;
static u128 U(int64_t v) { return (u128)(uint64_t)v; }

struct market {
    int64_t reserve_a = 0, reserve_b = 0;
    u128    k = 0;
    int64_t bets_sum = 0, a_bets_sum = 0, b_bets_sum = 0, liquidity_sum = 0;
    int64_t reserve_sum() const { return reserve_a + reserve_b; }
    int64_t viz_held()    const { return liquidity_sum + bets_sum; }
    double  curve_a()     const { return 100.0 * reserve_b / (reserve_a + reserve_b); }
};
struct bet { int64_t amount = 0, weight = 0; int side = 0; bool open = false; };

static market make_market(int64_t liq) {
    market m;
    int64_t half = liq / 2;
    m.reserve_a = half; m.reserve_b = liq - half;
    m.liquidity_sum = liq;
    m.k = U(m.reserve_a) * U(m.reserve_b);
    return m;
}
// pm_place_bet_evaluator::do_apply :1051-1073 — UNCHANGED by the fix
static bet place_bet(market& m, int side, int64_t amount) {
    int64_t reserve_in  = (side == 0) ? m.reserve_a : m.reserve_b;
    int64_t reserve_out = (side == 0) ? m.reserve_b : m.reserve_a;
    int64_t new_out = (int64_t)(uint64_t)(m.k / U(reserve_in + amount));
    int64_t tokens  = reserve_out - new_out;
    if (side == 0) { m.reserve_a += amount; m.reserve_b = new_out; m.a_bets_sum += amount; }
    else           { m.reserve_b += amount; m.reserve_a = new_out; m.b_bets_sum += amount; }
    m.bets_sum += amount;
    bet b; b.amount = amount; b.weight = tokens; b.side = side; b.open = true;
    return b;
}
// pm_cancel_bet :1235-1247 — UNCHANGED by the fix
static void cancel_bet(market& m, bet& b) {
    if (b.side == 0) { m.reserve_a -= b.amount; m.reserve_b += b.weight; m.a_bets_sum -= b.amount; }
    else             { m.reserve_b -= b.amount; m.reserve_a += b.weight; m.b_bets_sum -= b.amount; }
    m.bets_sum -= b.amount;
    m.k = U(m.reserve_a) * U(m.reserve_b);
    b.open = false;
}
// NEW pm_add_liquidity (172d87c): scale both reserves by (L + amount) / L
static void add_liquidity(market& m, int64_t amount) {
    const int64_t L = m.liquidity_sum;
    if (L > 0) {
        u128 num = U(L + amount), den = U(L);
        m.reserve_a = (int64_t)(uint64_t)(U(m.reserve_a) * num / den);
        m.reserve_b = (int64_t)(uint64_t)(U(m.reserve_b) * num / den);
    } else {
        int64_t half = amount / 2;
        m.reserve_a += half; m.reserve_b += amount - half;
    }
    m.liquidity_sum += amount;
    m.k = U(m.reserve_a) * U(m.reserve_b);
}
// NEW pm_withdraw_liquidity (172d87c): scale both reserves by (L - withdraw) / L
static void withdraw_liquidity(market& m, int64_t amount, int64_t min_liquidity) {
    if (m.liquidity_sum - amount < min_liquidity) {
        printf("   !! FC_ASSERT: withdrawal would drop liquidity below minimum (%lld - %lld < %lld)\n",
               (long long)m.liquidity_sum, (long long)amount, (long long)min_liquidity);
        return;
    }
    const int64_t L = m.liquidity_sum;
    m.liquidity_sum -= amount;
    if (L > 0) {
        u128 num = U(L - amount), den = U(L);
        m.reserve_a = (int64_t)(uint64_t)(U(m.reserve_a) * num / den);
        m.reserve_b = (int64_t)(uint64_t)(U(m.reserve_b) * num / den);
        m.k = U(m.reserve_a) * U(m.reserve_b);
    }
}
static void show(const char* tag, const market& m) {
    printf("%-24s ra=%-8lld rb=%-8lld curveA=%6.2f%%  rsum=%-8lld held=%-8lld L=%lld\n",
           tag, (long long)m.reserve_a, (long long)m.reserve_b, m.curve_a(),
           (long long)m.reserve_sum(), (long long)m.viz_held(), (long long)m.liquidity_sum);
}

int main() {
    const int64_t L = 400000, BET = 100000, HALF = 50000, LP = 50000, MINLIQ = 100000;

    printf("=== 9-step cycle against the FIX (172d87c) ===\n");
    market m = make_market(L);
    show("0. created", m);

    bet b100 = place_bet(m, 0, BET);
    show("1. bet 100 YES", m);
    printf("   weight %lld  (curve A %.2f%%)\n", (long long)b100.weight, m.curve_a());

    market at_create = m; // placeholder, overwritten below
    cancel_bet(m, b100);
    show("3. cancel the 100 bet", m);

    bet b50a = place_bet(m, 0, HALF);
    show("4. bet 50 YES", m);
    market before_lp = m;
    add_liquidity(m, LP);
    show("5. add 50 liquidity", m);
    withdraw_liquidity(m, LP, MINLIQ);
    show("6. withdraw 50 liq.", m);
    printf("   round-trip exact? ra %s (%lld vs %lld)  rb %s (%lld vs %lld)  k %s\n",
           m.reserve_a == before_lp.reserve_a ? "YES" : "NO",
           (long long)m.reserve_a, (long long)before_lp.reserve_a,
           m.reserve_b == before_lp.reserve_b ? "YES" : "NO",
           (long long)m.reserve_b, (long long)before_lp.reserve_b,
           m.k == before_lp.k ? "YES" : "NO");

    bet b50b = place_bet(m, 0, HALF);
    show("7. bet 50 YES", m);
    printf("   single 100 weight %lld  |  2x50 around LP cycle %lld  (%+.2f%%)\n",
           (long long)b100.weight, (long long)(b50a.weight + b50b.weight),
           100.0 * (b50a.weight + b50b.weight - b100.weight) / b100.weight);

    cancel_bet(m, b50b);
    cancel_bet(m, b50a);
    show("8. cancel both 50s", m);
    printf("   phantom depth: rsum %lld vs VIZ held %lld  (%+lld)\n",
           (long long)m.reserve_sum(), (long long)m.viz_held(),
           (long long)(m.reserve_sum() - m.viz_held()));

    printf("\n=== step 9: repeat the cycle 10x ===\n");
    market c = make_market(L);
    for (int i = 1; i <= 10; ++i) {
        bet x = place_bet(c, 0, HALF);
        add_liquidity(c, LP);
        withdraw_liquidity(c, LP, MINLIQ);
        bet y = place_bet(c, 0, HALF);
        int64_t w = x.weight + y.weight;
        cancel_bet(c, y); cancel_bet(c, x);
        if (i <= 3 || i == 10)
            printf("  cycle %-2d  ra=%-7lld rb=%-7lld rsum=%-7lld held=%-7lld phantom=%+lld  weight=%lld\n",
                   i, (long long)c.reserve_a, (long long)c.reserve_b, (long long)c.reserve_sum(),
                   (long long)c.viz_held(), (long long)(c.reserve_sum() - c.viz_held()), (long long)w);
    }

    printf("\n=== round-trip exactness sweep (add then withdraw the same amount) ===\n");
    int inexact = 0, total = 0; int64_t worst = 0;
    for (int64_t liq = 100000; liq <= 700000; liq += 37000)
    for (int64_t bet_amt = 1000; bet_amt <= 200000; bet_amt += 7331)
    for (int64_t lp = 1000; lp <= 300000; lp += 9973) {
        market s = make_market(liq);
        place_bet(s, 0, bet_amt);
        market pre = s;
        add_liquidity(s, lp);
        if (s.liquidity_sum - lp < MINLIQ) continue;
        withdraw_liquidity(s, lp, MINLIQ);
        ++total;
        int64_t da = s.reserve_a - pre.reserve_a, db_ = s.reserve_b - pre.reserve_b;
        if (da || db_) {
            ++inexact;
            if (da > worst) worst = da;
            if (db_ > worst) worst = db_;
            if (inexact <= 5)
                printf("  inexact: liq=%lld bet=%lld lp=%lld  dra=%+lld drb=%+lld\n",
                       (long long)liq, (long long)bet_amt, (long long)lp, (long long)da, (long long)db_);
        }
    }
    printf("  %d/%d round-trips inexact; worst signed drift = %+lld (positive = reserves GREW)\n",
           inexact, total, (long long)worst);

    printf("\n=== does an add->withdraw round-trip ever beat the honest single bet? ===\n");
    int wins = 0; total = 0; double best = 0;
    for (int64_t liq = 100000; liq <= 700000; liq += 37000)
    for (int64_t bet_amt = 2000; bet_amt <= 200000; bet_amt += 7331)
    for (int64_t lp = 1000; lp <= 300000; lp += 9973) {
        market single = make_market(liq), split = make_market(liq);
        bet s1 = place_bet(single, 0, bet_amt);
        int64_t h = bet_amt / 2;
        bet p1 = place_bet(split, 0, h);
        add_liquidity(split, lp);
        if (split.liquidity_sum - lp < MINLIQ) continue;
        withdraw_liquidity(split, lp, MINLIQ);
        bet p2 = place_bet(split, 0, bet_amt - h);
        ++total;
        int64_t ws = s1.weight, wp = p1.weight + p2.weight;
        if (wp > ws) {
            ++wins;
            double pct = 100.0 * (wp - ws) / ws;
            if (pct > best) best = pct;
            if (wins <= 5)
                printf("  split wins: liq=%lld bet=%lld lp=%lld  single=%lld split=%lld (%+.4f%%)\n",
                       (long long)liq, (long long)bet_amt, (long long)lp,
                       (long long)ws, (long long)wp, pct);
        }
    }
    printf("  split beat single in %d/%d cases; worst excess %+.4f%%\n", wins, total, best);
    return 0;
}
