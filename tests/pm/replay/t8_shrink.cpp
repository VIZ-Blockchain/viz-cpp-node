// t8_shrink.cpp — two follow-ups on 172d87c:
//  (A) attribute the "split beats single" excess: LP cycle, or plain bet-splitting rounding?
//  (B) NEW: withdraw now SHRINKS reserves, but pm_cancel_bet still subtracts the bet amount
//      recorded at bet time. Can a legitimate LP withdrawal between bet and cancel drive a
//      reserve negative -> k detonation (B6's failure mode, newly reachable)?
#include <cstdio>
#include <cstdint>
#include <initializer_list>
typedef unsigned __int128 u128;
static u128 U(int64_t v) { return (u128)(uint64_t)v; }
struct market { int64_t reserve_a=0, reserve_b=0, bets_sum=0, liquidity_sum=0; u128 k=0; };
struct bet { int64_t amount=0, weight=0; int side=0; };

static market make_market(int64_t liq) {
    market m; int64_t h = liq/2; m.reserve_a=h; m.reserve_b=liq-h;
    m.liquidity_sum=liq; m.k=U(m.reserve_a)*U(m.reserve_b); return m;
}
static bet place_bet(market& m, int side, int64_t amount) {
    int64_t rin = side==0?m.reserve_a:m.reserve_b, rout = side==0?m.reserve_b:m.reserve_a;
    int64_t nout = (int64_t)(uint64_t)(m.k / U(rin+amount));
    bet b; b.amount=amount; b.weight=rout-nout; b.side=side;
    if (side==0) { m.reserve_a+=amount; m.reserve_b=nout; } else { m.reserve_b+=amount; m.reserve_a=nout; }
    m.bets_sum+=amount; return b;
}
static void cancel_bet(market& m, const bet& b) {
    if (b.side==0) { m.reserve_a-=b.amount; m.reserve_b+=b.weight; }
    else           { m.reserve_b-=b.amount; m.reserve_a+=b.weight; }
    m.bets_sum-=b.amount;
    m.k = U(m.reserve_a)*U(m.reserve_b);   // negative int64 -> huge uint64
}
static void add_liq(market& m, int64_t a) {
    const int64_t L=m.liquidity_sum;
    if (L>0) { u128 n=U(L+a),d=U(L);
        m.reserve_a=(int64_t)(uint64_t)(U(m.reserve_a)*n/d);
        m.reserve_b=(int64_t)(uint64_t)(U(m.reserve_b)*n/d); }
    else { int64_t h=a/2; m.reserve_a+=h; m.reserve_b+=a-h; }
    m.liquidity_sum+=a; m.k=U(m.reserve_a)*U(m.reserve_b);
}
static bool wd_liq(market& m, int64_t w, int64_t minliq) {
    if (m.liquidity_sum - w < minliq) return false;      // the new FC_ASSERT
    const int64_t L=m.liquidity_sum; m.liquidity_sum-=w;
    if (L>0) { u128 n=U(L-w),d=U(L);
        m.reserve_a=(int64_t)(uint64_t)(U(m.reserve_a)*n/d);
        m.reserve_b=(int64_t)(uint64_t)(U(m.reserve_b)*n/d);
        m.k=U(m.reserve_a)*U(m.reserve_b); }
    return true;
}

int main() {
    const int64_t MINLIQ = 100000;

    printf("=== (A) attribution: split-vs-single, with and without the LP round trip ===\n");
    int lp_wins=0, plain_wins=0, tot=0; double lp_best=0, plain_best=0;
    for (int64_t liq=100000; liq<=700000; liq+=37000)
    for (int64_t amt=2000; amt<=200000; amt+=7331)
    for (int64_t lp=1000; lp<=300000; lp+=9973) {
        market one=make_market(liq), withlp=make_market(liq), nolp=make_market(liq);
        int64_t h=amt/2;
        bet s = place_bet(one,0,amt);
        bet a1=place_bet(withlp,0,h);
        add_liq(withlp,lp);
        if (!wd_liq(withlp,lp,MINLIQ)) continue;
        bet a2=place_bet(withlp,0,amt-h);
        bet b1=place_bet(nolp,0,h), b2=place_bet(nolp,0,amt-h);   // control: no LP ops at all
        ++tot;
        int64_t w1=a1.weight+a2.weight, w2=b1.weight+b2.weight;
        if (w1>s.weight) { ++lp_wins;    double p=100.0*(w1-s.weight)/s.weight; if(p>lp_best) lp_best=p; }
        if (w2>s.weight) { ++plain_wins; double p=100.0*(w2-s.weight)/s.weight; if(p>plain_best) plain_best=p; }
    }
    printf("  with LP round trip : %d/%d beat single, worst %+.4f%%\n", lp_wins, tot, lp_best);
    printf("  plain split, no LP : %d/%d beat single, worst %+.4f%%\n", plain_wins, tot, plain_best);
    printf("  -> excess is %s\n", lp_wins==plain_wins && lp_best==plain_best
           ? "pure bet-splitting truncation, NOT the LP cycle" : "LP-attributable (investigate)");

    printf("\n=== (B) LP withdrawal between bet and cancel ===\n");
    {
        market m = make_market(400000);
        printf("  created                ra=%lld rb=%lld L=%lld\n",
               (long long)m.reserve_a,(long long)m.reserve_b,(long long)m.liquidity_sum);
        bet alice = place_bet(m,0,100000);
        printf("  alice bets 100k on A   ra=%lld rb=%lld  weight=%lld\n",
               (long long)m.reserve_a,(long long)m.reserve_b,(long long)alice.weight);
        bool ok = wd_liq(m, 300000, MINLIQ);   // legal: leaves L = 100000 == pm_min_liquidity
        printf("  LP withdraws 300k      %s  ra=%lld rb=%lld L=%lld\n",
               ok?"ALLOWED":"blocked",(long long)m.reserve_a,(long long)m.reserve_b,
               (long long)m.liquidity_sum);
        cancel_bet(m, alice);
        printf("  alice cancels          ra=%lld rb=%lld\n",
               (long long)m.reserve_a,(long long)m.reserve_b);
        printf("  k = %llu%s\n", (unsigned long long)m.k,
               m.reserve_a<0||m.reserve_b<0 ? "   <-- NEGATIVE RESERVE, k detonated" : "");
        // what the next bettor now gets out of the detonated curve
        if (m.reserve_a > 0 || m.reserve_b > 0) {
            market probe = m; bet nxt = place_bet(probe,1,1000);
            printf("  next 1000 bet on B gets weight %lld (reserve_a was %lld)\n",
                   (long long)nxt.weight,(long long)m.reserve_a);
        }
    }

    printf("\n  minimum LP withdrawal that makes a cancel underflow, by bet size:\n");
    for (int64_t amt : {10000, 50000, 100000, 200000}) {
        int64_t liq=400000, found=-1;
        for (int64_t w=1000; w<=liq-MINLIQ; w+=1000) {
            market m=make_market(liq);
            bet b=place_bet(m,0,amt);
            if (!wd_liq(m,w,MINLIQ)) continue;
            if (m.reserve_a - b.amount < 0) { found=w; break; }
        }
        printf("    bet %-7lld on a %lld market: %s\n", (long long)amt, (long long)liq,
               found<0 ? "no single withdrawal suffices"
                       : (printf("withdraw %lld (%.0f%% of pool)",(long long)found,100.0*found/liq), ""));
        printf("\n");
    }
    return 0;
}
