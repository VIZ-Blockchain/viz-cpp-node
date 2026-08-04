// t9 — magnitude of the residual LP-round-trip rounding leak, and whether it accumulates.
#include <cstdio>
#include <cstdint>
#include <initializer_list>
typedef unsigned __int128 u128;
static u128 U(int64_t v){return (u128)(uint64_t)v;}
struct market{int64_t ra=0,rb=0,bs=0,L=0;u128 k=0;};
struct bet{int64_t amount=0,weight=0;int side=0;};
static market mk(int64_t l){market m;int64_t h=l/2;m.ra=h;m.rb=l-h;m.L=l;m.k=U(m.ra)*U(m.rb);return m;}
static bet pb(market&m,int s,int64_t a){int64_t ri=s==0?m.ra:m.rb,ro=s==0?m.rb:m.ra;
 int64_t no=(int64_t)(uint64_t)(m.k/U(ri+a));bet b;b.amount=a;b.weight=ro-no;b.side=s;
 if(s==0){m.ra+=a;m.rb=no;}else{m.rb+=a;m.ra=no;}m.bs+=a;return b;}
static void cb(market&m,const bet&b){if(b.side==0){m.ra-=b.amount;m.rb+=b.weight;}else{m.rb-=b.amount;m.ra+=b.weight;}
 m.bs-=b.amount;m.k=U(m.ra)*U(m.rb);}
static void al(market&m,int64_t a){const int64_t L=m.L;if(L>0){u128 n=U(L+a),d=U(L);
 m.ra=(int64_t)(uint64_t)(U(m.ra)*n/d);m.rb=(int64_t)(uint64_t)(U(m.rb)*n/d);}
 else{int64_t h=a/2;m.ra+=h;m.rb+=a-h;}m.L+=a;m.k=U(m.ra)*U(m.rb);}
static bool wl(market&m,int64_t w,int64_t mn){if(m.L-w<mn)return false;const int64_t L=m.L;m.L-=w;
 if(L>0){u128 n=U(L-w),d=U(L);m.ra=(int64_t)(uint64_t)(U(m.ra)*n/d);m.rb=(int64_t)(uint64_t)(U(m.rb)*n/d);
 m.k=U(m.ra)*U(m.rb);}return true;}

int main(){
  const int64_t MN=100000;
  printf("=== absolute size of the split-around-LP excess ===\n");
  int64_t worst_abs=0; double worst_pct=0; int64_t wl_,wa_,wp_;
  for(int64_t l=100000;l<=700000;l+=37000)
  for(int64_t a=2000;a<=200000;a+=7331)
  for(int64_t lp=1000;lp<=300000;lp+=9973){
    market one=mk(l),sp=mk(l);int64_t h=a/2;
    bet s=pb(one,0,a);bet p1=pb(sp,0,h);al(sp,lp);if(!wl(sp,lp,MN))continue;
    bet p2=pb(sp,0,a-h);int64_t d=p1.weight+p2.weight-s.weight;
    if(d>worst_abs){worst_abs=d;worst_pct=100.0*d/s.weight;wl_=l;wa_=a;wp_=lp;}
  }
  printf("  worst absolute excess = %lld satoshi (%.4f%%) at liq=%lld bet=%lld lp=%lld\n",
         (long long)worst_abs,worst_pct,(long long)wl_,(long long)wa_,(long long)wp_);
  printf("  (old 50/50 code on the same shape: +29.6%% claim weight, thousands of satoshi)\n");

  printf("\n=== does bare LP cycling ratchet the curve over many rounds? ===\n");
  for(int64_t l:{100001LL,400000LL,333333LL}) for(int64_t lp:{7777LL,50000LL}){
    market m=mk(l);int64_t ra0=m.ra,rb0=m.rb;int cycles=0;
    for(int i=0;i<2000;i++){al(m,lp);if(!wl(m,lp,MN))break;cycles++;}
    printf("  liq=%-7lld lp=%-6lld after %d cycles: ra %lld->%lld (%+lld) rb %lld->%lld (%+lld)\n",
      (long long)l,(long long)lp,cycles,(long long)ra0,(long long)m.ra,(long long)(m.ra-ra0),
      (long long)rb0,(long long)m.rb,(long long)(m.rb-rb0));
  }

  printf("\n=== full 9-step cycle repeated 500x (does weight or phantom depth ratchet?) ===\n");
  for(int64_t l:{400000LL,333333LL}){
    market c=mk(l);int64_t first=0,last=0;
    for(int i=1;i<=500;i++){
      bet x=pb(c,0,50000);al(c,50000);if(!wl(c,50000,MN))break;
      bet y=pb(c,0,50000);int64_t w=x.weight+y.weight;
      cb(c,y);cb(c,x); if(i==1)first=w; last=w;
    }
    printf("  liq=%-7lld weight pass1=%lld pass500=%lld  final ra=%lld rb=%lld rsum=%lld held=%lld phantom=%+lld\n",
      (long long)l,(long long)first,(long long)last,(long long)c.ra,(long long)c.rb,
      (long long)(c.ra+c.rb),(long long)(c.L+c.bs),(long long)(c.ra+c.rb-c.L-c.bs));
  }
  return 0;
}
