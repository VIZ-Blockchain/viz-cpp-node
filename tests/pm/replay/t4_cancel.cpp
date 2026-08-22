// T4 — pm_cancel_bet reserve underflow → k corruption.
// Replays pm_place_bet (pm_evaluator.cpp:1052-1072) then pm_cancel_bet's binary branch
// (:1234-1247) verbatim, with the same int64 share_type / (uint64_t) cast semantics.
#include <fc/uint128_t.hpp>
#include <cstdio>

struct mkt { int64_t ra, rb; fc::uint128_t k; };

static int64_t place_bet(mkt& m, int64_t amount, int side) {      // returns weight (tokens_out)
    int64_t rin = side == 0 ? m.ra : m.rb, rout = side == 0 ? m.rb : m.ra;
    int64_t new_out = (int64_t)(m.k / fc::uint128_t((uint64_t)(rin + amount))).lo;
    int64_t tokens  = rout - new_out;
    if (side == 0) { m.ra += amount; m.rb = new_out; } else { m.rb += amount; m.ra = new_out; }
    return tokens;
}

static void cancel_bet(mkt& m, int64_t amount, int64_t weight, int side) {
    if (side == 0) { m.ra -= amount; m.rb += weight; } else { m.rb -= amount; m.ra += weight; }
    m.k = fc::uint128_t((uint64_t)m.ra) * fc::uint128_t((uint64_t)m.rb);   // :1246
}

static void show(const char* tag, const mkt& m) {
    printf("%-34s reserve_a=%-22lld reserve_b=%-12lld k.hi=%llu k.lo=%llu\n",
           tag, (long long)m.ra, (long long)m.rb,
           (unsigned long long)m.k.high_bits(), (unsigned long long)m.k.low_bits());
}

int main() {
    mkt m; m.ra = 1000000; m.rb = 1000000;
    m.k = fc::uint128_t((uint64_t)m.ra) * fc::uint128_t((uint64_t)m.rb);
    show("initial (liquidity 2,000,000)", m);

    const int64_t stake = 500000;
    int64_t weight = place_bet(m, stake, 0);                  // victim bets 500k on side 0
    printf("%-34s weight(A-tokens)=%lld\n", "after own bet on side 0:", (long long)weight);
    show("  reserves", m);

    for (int i = 0; i < 4; ++i) place_bet(m, 400000, 1);      // 4 x 400k opposing bets on side 1
    show("after 1,600,000 of side-1 bets", m);
    printf("%-34s reserve_a(%lld) < refund(%lld) ? %s\n\n", "underflow precondition:",
           (long long)m.ra, (long long)stake, m.ra < stake ? "YES" : "no");

    cancel_bet(m, stake, weight, 0);                          // full refund of 500k, no floor
    show("AFTER pm_cancel_bet", m);
    printf("\nreserve_a is %s; k went from 1e12 to %s (~2^%d)\n",
           m.ra < 0 ? "NEGATIVE (share_type int64)" : "ok",
           m.k.high_bits() ? "a 128-bit value" : "a 64-bit value", 128 - 0);
    return 0;
}
