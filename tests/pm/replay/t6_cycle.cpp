// PR #124 — bet/cancel + LP add/withdraw cycle analysis.
//
// Replays pm_evaluator.cpp's binary-CPMM arithmetic VERBATIM (line refs below) for the
// 9-step scenario:
//   1. bet 100 YES        2. record odds        3. cancel that bet
//   4. bet 50 YES         5. add 50 liquidity   6. withdraw 50 liquidity
//   7. bet 50 YES         8. record             9. cancel both 50s -> compare to start
//
// Sources replicated:
//   pm_place_bet          :1051-1073
//   pm_cancel_bet         :1235-1247
//   pm_add_liquidity      :1298-1309
//   pm_withdraw_liquidity :1343-1352
#include <fc/uint128_t.hpp>
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>

static fc::uint128_t U(int64_t v) { return fc::uint128_t((uint64_t)v); }
static std::string u128s(const fc::uint128_t& v) {
    char buf[64];
    if (v.hi == 0) { snprintf(buf, sizeof buf, "%llu", (unsigned long long)v.lo); return buf; }
    snprintf(buf, sizeof buf, "%llu*2^64+%llu", (unsigned long long)v.hi, (unsigned long long)v.lo);
    return buf;
}

struct bet { int64_t amount = 0, weight = 0; int side = 0; bool open = false; };

struct market {
    int64_t reserve_a = 0, reserve_b = 0;
    fc::uint128_t k = 0;
    int64_t a_bets_sum = 0, b_bets_sum = 0, bets_sum = 0, liquidity_sum = 0;

    // real VIZ the market is holding (frozen stakes + live LP principal)
    int64_t held() const { return bets_sum + liquidity_sum; }
    // CPMM implied probability of A: price_A = reserve_b / (reserve_a + reserve_b)
    double curve_pct_a() const { return 100.0 * reserve_b / double(reserve_a + reserve_b); }
    // what prediction_market_api reports (kline weights = a_bets_sum / b_bets_sum, :390)
    double api_pct_a() const {
        int64_t t = a_bets_sum + b_bets_sum;
        return t ? 100.0 * a_bets_sum / double(t) : 50.0;
    }
};

static void create(market& m, int64_t liquidity) {
    m.reserve_a = liquidity / 2;
    m.reserve_b = liquidity - m.reserve_a;
    m.k = U(m.reserve_a) * U(m.reserve_b);
    m.liquidity_sum = liquidity;
}

// pm_place_bet_evaluator::do_apply, :1051-1073
static bet place_bet(market& m, int side, int64_t amount) {
    int64_t reserve_in  = (side == 0) ? m.reserve_a : m.reserve_b;
    int64_t reserve_out = (side == 0) ? m.reserve_b : m.reserve_a;
    fc::uint128_t denom = U(reserve_in + amount);
    int64_t new_out = (int64_t)(m.k / denom).lo;
    int64_t tokens  = reserve_out - new_out;
    if (side == 0) { m.reserve_a += amount; m.reserve_b = new_out; m.a_bets_sum += amount; }
    else           { m.reserve_b += amount; m.reserve_a = new_out; m.b_bets_sum += amount; }
    m.bets_sum += amount;
    // NOTE: k is deliberately NOT recomputed here.
    bet b; b.amount = amount; b.weight = tokens; b.side = side; b.open = true;
    return b;
}

// pm_cancel_bet_evaluator::do_apply, :1235-1247 (refund is the full nominal stake, :1231)
static int64_t cancel_bet(market& m, bet& b) {
    if (b.side == 0) { m.reserve_a -= b.amount; m.reserve_b += b.weight; m.a_bets_sum -= b.amount; }
    else             { m.reserve_b -= b.amount; m.reserve_a += b.weight; m.b_bets_sum -= b.amount; }
    m.bets_sum -= b.amount;
    m.k = U(m.reserve_a) * U(m.reserve_b);   // <-- k RESET to the reserve product
    b.open = false;
    return b.amount;
}

// pm_add_liquidity_evaluator::do_apply, :1298-1309
static void add_liquidity(market& m, int64_t amount) {
    m.liquidity_sum += amount;
    int64_t half = amount / 2;
    m.reserve_a += half;
    m.reserve_b += amount - half;
    m.k = U(m.reserve_a) * U(m.reserve_b);
}

// pm_withdraw_liquidity_evaluator::do_apply, :1343-1352 (binary path touches liquidity_sum ONLY)
static int64_t withdraw_liquidity(market& m, int64_t amount) {
    m.liquidity_sum -= amount;
    return amount;
}

static void row(const char* step, const market& m) {
    printf("%-26s ra=%-10lld rb=%-10lld k=%-22s curveA=%6.2f%%  apiA=%6.2f%%  held=%-9lld ra*rb==k:%s\n",
           step, (long long)m.reserve_a, (long long)m.reserve_b, u128s(m.k).c_str(),
           m.curve_pct_a(), m.api_pct_a(), (long long)m.held(),
           (U(m.reserve_a) * U(m.reserve_b) == m.k) ? "yes" : "NO");
}

int main() {
    const int64_t L    =  400000;   // market seed liquidity: unit*4 = 400 VIZ (pm_min_liquidity=100 VIZ)
    const int64_t BET   = 100000;   // 100 VIZ
    const int64_t HALF  =  50000;   //  50 VIZ
    const int64_t LP    =  50000;   //  50 VIZ

    printf("=== Baseline A: single 100 VIZ bet on YES, then cancel ===\n");
    market a; create(a, L);
    market a0 = a;
    row("0. created", a);
    bet b1 = place_bet(a, 0, BET);
    row("1. bet 100 YES", a);
    printf("   -> weight %lld  (%.4f weight per mVIZ)\n", (long long)b1.weight, b1.weight / double(BET));
    int64_t r1 = cancel_bet(a, b1);
    row("3. cancel the 100 bet", a);
    printf("   refund=%lld   reserves restored: %s   k restored: %s\n\n",
           (long long)r1,
           (a.reserve_a == a0.reserve_a && a.reserve_b == a0.reserve_b) ? "yes" : "NO",
           (a.k == a0.k) ? "yes" : "NO");

    printf("=== Path B: 2 x 50 VIZ with a free LP add/withdraw in the middle ===\n");
    market m; create(m, L);
    market m0 = m;
    row("0. created", m);

    bet s1 = place_bet(m, 0, HALF);
    row("4. bet 50 YES", m);
    printf("   -> weight %lld\n", (long long)s1.weight);

    add_liquidity(m, LP);
    row("5. add 50 liquidity", m);

    int64_t back = withdraw_liquidity(m, LP);
    row("6. withdraw 50 liquidity", m);
    printf("   got back %lld of %lld (%.0f%%), market keeps the reserves\n", (long long)back, (long long)LP, 100.0 * back / LP);

    bet s2 = place_bet(m, 0, HALF);
    row("7. bet 50 YES", m);
    printf("   -> weight %lld\n", (long long)s2.weight);

    int64_t wB = s1.weight + s2.weight;
    printf("\n7b. RESULT for the same 100 VIZ:\n");
    printf("    single 100 bet (honest)      : weight %lld\n", (long long)b1.weight);
    printf("    2x50 with free LP cycle      : weight %lld  (%+.2f%%)\n",
           (long long)wB, 100.0 * (wB - b1.weight) / double(b1.weight));

    int64_t c1 = cancel_bet(m, s1);
    row("8a. cancel first 50", m);
    int64_t c2 = cancel_bet(m, s2);
    row("8b. cancel second 50", m);
    printf("    refunds %lld + %lld = %lld for 100 staked\n", (long long)c1, (long long)c2, (long long)(c1 + c2));

    printf("\n8c. back at the start?\n");
    printf("    reserve_a %lld -> %lld  (%+lld)\n", (long long)m0.reserve_a, (long long)m.reserve_a, (long long)(m.reserve_a - m0.reserve_a));
    printf("    reserve_b %lld -> %lld  (%+lld)\n", (long long)m0.reserve_b, (long long)m.reserve_b, (long long)(m.reserve_b - m0.reserve_b));
    printf("    k         %s -> %s\n", u128s(m0.k).c_str(), u128s(m.k).c_str());
    printf("    curve A   %.2f%% -> %.2f%%\n", m0.curve_pct_a(), m.curve_pct_a());
    printf("    VIZ held  %lld -> %lld\n", (long long)m0.held(), (long long)m.held());
    printf("    reserve sum %lld vs VIZ held %lld  (phantom depth %+lld)\n",
           (long long)(m.reserve_a + m.reserve_b), (long long)m.held(),
           (long long)(m.reserve_a + m.reserve_b - m.held()));

    printf("\n=== Step 9: repeat the whole cycle on the same market ===\n");
    printf("%-6s %-11s %-11s %-9s %-9s %-11s %s\n", "cycle", "reserve_a", "reserve_b", "curveA%", "held", "phantom", "weight for 100 VIZ");
    market c; create(c, L);
    for (int i = 1; i <= 10; ++i) {
        bet x = place_bet(c, 0, HALF);
        add_liquidity(c, LP);
        withdraw_liquidity(c, LP);
        bet y = place_bet(c, 0, HALF);
        int64_t w = x.weight + y.weight;
        cancel_bet(c, x);
        cancel_bet(c, y);
        printf("%-6d %-11lld %-11lld %8.2f%% %-9lld %+-11lld %lld\n", i,
               (long long)c.reserve_a, (long long)c.reserve_b, c.curve_pct_a(),
               (long long)c.held(), (long long)(c.reserve_a + c.reserve_b - c.held()),
               (long long)w);
    }

    printf("\n=== Control: same cycle WITHOUT the LP add/withdraw ===\n");
    printf("%-6s %-11s %-11s %-9s %s\n", "cycle", "reserve_a", "reserve_b", "curveA%", "weight for 100 VIZ");
    market d; create(d, L);
    for (int i = 1; i <= 5; ++i) {
        bet x = place_bet(d, 0, HALF);
        bet y = place_bet(d, 0, HALF);
        int64_t w = x.weight + y.weight;
        cancel_bet(d, y);
        cancel_bet(d, x);
        printf("%-6d %-11lld %-11lld %8.2f%% %lld\n", i,
               (long long)d.reserve_a, (long long)d.reserve_b, d.curve_pct_a(), (long long)w);
    }
    return 0;
}
