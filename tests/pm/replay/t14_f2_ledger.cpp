// t14_f2_ledger.cpp — verify f3ff915 (F2, curve-priced cancel) and its interaction with
// 5ff694e (F1, "charge the shortfall to LP principal").
//
// Models pm_cancel_bet as rewritten at pm_evaluator.cpp:1332-1358 (f3ff915) and pm_place_bet's
// CPMM buy at :1139-1160, then settles through the REAL compute_settlement.
//
//   A. does the t12 weight-inflation chain still pay?  (F2's own claim)
//   B. full-market VIZ ledger: stakes in, refunds out, settlement out.
#include <graphene/chain/pm/parimutuel.hpp>
#include <cstdio>
#include <cstdint>
#include <vector>
using namespace graphene::chain::pm;

typedef unsigned __int128 u128;
static u128 U(int64_t v) { return (u128)(uint64_t)v; }
static int64_t D(u128 v) { return (int64_t)(uint64_t)v; }

struct market { int64_t ra=0, rb=0, bets=0, a_bets=0, b_bets=0, L=0, forfeit=0; u128 k=0; };
struct bet    { int64_t amount=0, weight=0; int side=0; };

static market make_market(int64_t liq) {
    market m; int64_t h = liq/2;
    m.ra=h; m.rb=liq-h; m.L=liq; m.k=U(m.ra)*U(m.rb); return m;
}
// pm_place_bet, market_type 0 (:1139-1160). k is NOT recomputed — reserve_out truncates down.
static bet place(market& m, int side, int64_t amount) {
    int64_t rin = side==0 ? m.ra : m.rb, rout = side==0 ? m.rb : m.ra;
    int64_t nout = D(m.k / U(rin + amount));
    bet b; b.amount=amount; b.weight=rout-nout; b.side=side;
    if (side==0) { m.ra+=amount; m.rb=nout; m.a_bets+=amount; }
    else         { m.rb+=amount; m.ra=nout; m.b_bets+=amount; }
    m.bets+=amount; return b;
}
// pm_cancel_bet after f3ff915: sell bet.weight back at the CURRENT reserves. Returns the refund.
static int64_t cancel_f2(market& m, const bet& b) {
    bool a = (b.side==0);
    int64_t rin = a ? m.ra : m.rb, rout = a ? m.rb : m.ra;
    int64_t nrin = D(m.k / U(rout + b.weight));
    int64_t refund = rin - nrin;
    int64_t residual = b.amount - refund;
    if (a) { m.ra=nrin; m.rb+=b.weight; m.a_bets-=b.amount; }
    else   { m.rb=nrin; m.ra+=b.weight; m.b_bets-=b.amount; }
    m.bets -= b.amount;
    m.forfeit += residual;
    return refund;
}

int main() {
    printf("=== A. t12's chain under the NEW curve-priced cancel (400000 market, X=50000) ===\n");
    {
        const int64_t X = 50000;
        market honest = make_market(400000);
        bet base = place(honest, 0, X);
        market m = make_market(400000);
        bet b1 = place(m, 0, X);
        bet b2 = place(m, 1, X);
        int64_t r1 = cancel_f2(m, b1);
        bet b4 = place(m, 0, X);
        int64_t r2 = cancel_f2(m, b2);
        printf("  honest 50000 on A -> weight %lld\n", (long long)base.weight);
        printf("  chained  retained A bet weight %lld   (%+.2f%%)\n",
               (long long)b4.weight, 100.0*(b4.weight-base.weight)/base.weight);
        printf("  refunds: cancel(1)=%lld cancel(2)=%lld  (stake was %lld each)\n",
               (long long)r1, (long long)r2, (long long)X);
        printf("  cash cost of the chain = %lld  (was 0 under nominal reversal)\n",
               (long long)(2*X - r1 - r2));
        printf("  k invariant: k=%llu  ra*rb=%llu  drift=%lld (buys truncate reserve_out down; same as place_bet)\n",
               (unsigned long long)m.k, (unsigned long long)D(U(m.ra)*U(m.rb)),
               (long long)(D(m.k) - D(U(m.ra)*U(m.rb))));
        printf("  forfeit_pool after the chain = %+lld\n", (long long)m.forfeit);
    }

    printf("\n=== B. VIZ ledger: cancel AFTER a same-side follow-on bet, settled for real ===\n");
    printf("  (no leverage, no LP op — one cancellation-enabled binary market)\n");
    for (int64_t follow : {0LL, 10000LL, 50000LL, 200000LL}) {
        market m = make_market(200000);
        int64_t staked = 0, refunded = 0;
        bet alice = place(m, 0, 50000); staked += 50000;
        bet bob;
        bool has_bob = follow > 0;
        if (has_bob) { bob = place(m, 0, follow); staked += follow; }
        int64_t r = cancel_f2(m, alice); refunded += r;

        // Side A wins. Remaining winners: bob (if any). No B bets -> losers_sum = 0.
        settle_params p;
        p.losers_sum = 0;
        p.forfeit_pool = m.forfeit;
        p.oracle_fee_percent = 100; p.creator_fee_percent = 100; p.liquidity_fee_percent = 200;
        std::vector<winner_in> w;
        if (has_bob) w.push_back({bob.amount, bob.weight, 0});
        auto res = compute_settlement(p, w);

        int64_t held = m.L + staked - refunded;              // real VIZ the market holds
        int64_t out  = res.oracle_take + res.creator_take + res.lp_bonus + m.L; // LP principal back in full
        for (size_t i=0;i<res.winner_payout.size();++i) out += res.winner_payout[i];
        // 5ff694e would charge res.uncovered against LP principal; measure what it actually reports.
        int64_t out_after_charge = out - res.uncovered;

        printf("  follow-on=%-7lld refund=%-7lld forfeit=%+-8lld  held=%-8lld out=%-8lld delta=%+-8lld"
               "  uncovered=%lld %s\n",
               (long long)follow, (long long)r, (long long)m.forfeit,
               (long long)held, (long long)out, (long long)(out-held),
               (long long)res.uncovered,
               out_after_charge==held ? "" : (out>held ? "*** EMITTED ***" : ""));
    }
    printf("\n  `uncovered` is what 5ff694e's settle_liquidity charges to LP principal.\n");
    printf("  compute_settlement never assigns it (grep libraries/chain/pm/parimutuel.cpp), so it is\n");
    printf("  always 0 and the charge is dead code — the shortfall is still emitted.\n");
    return 0;
}
