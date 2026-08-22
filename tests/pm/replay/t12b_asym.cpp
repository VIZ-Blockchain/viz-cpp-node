// asymmetric variant: bet A = x, opposing bet B = y, cancel A, re-bet A = x, cancel B.
#include <cstdio>
#include <cstdint>
typedef unsigned __int128 u128;
static u128 U(int64_t v){return (u128)(uint64_t)v;} static int64_t D(u128 v){return (int64_t)(uint64_t)v;}
struct market{int64_t ra=0,rb=0,L=0;u128 k=0;}; struct bet{int64_t amount=0,weight=0;int side=0;};
static market mk(int64_t l){market m;int64_t h=l/2;m.ra=h;m.rb=l-h;m.L=l;m.k=U(m.ra)*U(m.rb);return m;}
static bet place(market&m,int s,int64_t a){int64_t rin=s==0?m.ra:m.rb,rout=s==0?m.rb:m.ra;
  int64_t no=D(m.k/U(rin+a)); bet b;b.amount=a;b.weight=rout-no;b.side=s;
  if(s==0){m.ra+=a;m.rb=no;}else{m.rb+=a;m.ra=no;} return b;}
static bool cancel(market&m,const bet&b){int64_t rin=b.side==0?m.ra:m.rb; if(rin<b.amount)return false;
  if(b.side==0){m.ra-=b.amount;m.rb+=b.weight;}else{m.rb-=b.amount;m.ra+=b.weight;}
  m.k=U(m.ra)*U(m.rb); return true;}
int main(){
  double best=0; int64_t bl=0,bx=0,by=0; long long ok=0,tot=0,neg=0;
  for(int64_t liq=100000; liq<=2000000; liq+=37000)
  for(int64_t x=1000; x<=1000000; x+=13331)
  for(int64_t y=1000; y<=1000000; y+=17777){
    market h=mk(liq); int64_t hw=place(h,0,x).weight; if(hw<=0) continue; ++tot;
    market s=mk(liq);
    bet p1=place(s,0,x); bet p2=place(s,1,y);
    if(!cancel(s,p1)) continue;
    bet p3=place(s,0,x);
    if(!cancel(s,p2)) continue;
    ++ok; double g=100.0*(p3.weight-hw)/(double)hw;
    if(g<0)++neg;
    if(g>best){best=g;bl=liq;bx=x;by=y;}
  }
  printf("asymmetric sweep: %lld/%lld chains complete, %lld adverse\n", ok, tot, neg);
  printf("best %+.2f%% claim-weight inflation at liquidity=%lld  betA=%lld  betB=%lld\n", best,(long long)bl,(long long)bx,(long long)by);
  // show that instance
  market s=mk(bl),h=mk(bl); int64_t hw=place(h,0,bx).weight;
  bet p1=place(s,0,bx); bet p2=place(s,1,by); cancel(s,p1); bet p3=place(s,0,bx); cancel(s,p2);
  printf("  honest weight %lld -> chained weight %lld  (stake %lld, all other principal refunded)\n",
    (long long)hw,(long long)p3.weight,(long long)bx);
  return 0;
}
