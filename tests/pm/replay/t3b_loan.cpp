// T3b — what the un-shrunk reserves actually buy an attacker: phantom curve depth.
#include <graphene/chain/pm/leverage.hpp>
#include <cstdio>
using namespace graphene::chain::pm;
struct mkt { int64_t ra, rb; fc::uint128_t k; int64_t liquidity_sum; };
static void add_withdraw(mkt& m, int64_t amount) {   // deposit then immediately withdraw
    m.liquidity_sum += amount;
    int64_t half = amount / 2; m.ra += half; m.rb += amount - half;
    m.k = fc::uint128_t((uint64_t)m.ra) * fc::uint128_t((uint64_t)m.rb);
    m.liquidity_sum -= amount;                        // reserves + k stay inflated
}
int main() {
    const int64_t collateral = 100000;
    printf("collateral=%lld, r=10%%, s=20%%, sl=50%%, m_factor=50%%\n\n", (long long)collateral);
    printf("%-22s %-12s %-12s %-14s %s\n", "market", "reserve_a", "reserve_b", "real VIZ held", "max loan");
    mkt m{1000000, 1000000, 0, 2000000};
    m.k = fc::uint128_t((uint64_t)m.ra) * fc::uint128_t((uint64_t)m.rb);
    for (int cycle = 0; cycle <= 3; ++cycle) {
        if (cycle) add_withdraw(m, 10000000);
        int64_t L = leverage::max_leverage_loan(m.ra, m.rb, m.k, collateral, 0,
                                               collateral * 20, 10, 20, 50, 50);
        char tag[32]; snprintf(tag, sizeof tag, cycle ? "after %d add+withdraw" : "untouched", cycle);
        printf("%-22s %-12lld %-12lld %-14lld %lld\n", tag, (long long)m.ra, (long long)m.rb,
               (long long)m.liquidity_sum, (long long)L);
    }
    return 0;
}
