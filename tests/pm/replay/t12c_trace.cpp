#include <cstdio>
#include <cstdint>
typedef unsigned __int128 u128;
static u128 U(int64_t v){return (u128)(uint64_t)v;} static int64_t D(u128 v){return (int64_t)(uint64_t)v;}
struct market{int64_t ra=0,rb=0,a_bets=0,b_bets=0,bets=0,L=0;u128 k=0;}; struct bet{int64_t amount=0,weight=0;int side=0;};
static market mk(int64_t l){market m;int64_t h=l/2;m.ra=h;m.rb=l-h;m.L=l;m.k=U(m.ra)*U(m.rb);return m;}
static bet place(market&m,int s,int64_t a){int64_t rin=s==0?m.ra:m.rb,rout=s==0?m.rb:m.ra;
  int64_t no=D(m.k/U(rin+a)); bet b;b.amount=a;b.weight=rout-no;b.side=s;
  if(s==0){m.ra+=a;m.rb=no;m.a_bets+=a;}else{m.rb+=a;m.ra=no;m.b_bets+=a;} m.bets+=a; return b;}
static bool cancel(market&m,const bet&b){int64_t rin=b.side==0?m.ra:m.rb; if(rin<b.amount)return false;
  if(b.side==0){m.ra-=b.amount;m.rb+=b.weight;m.a_bets-=b.amount;}else{m.rb-=b.amount;m.ra+=b.weight;m.b_bets-=b.amount;}
  m.bets-=b.amount; m.k=U(m.ra)*U(m.rb); return true;}
static void show(const char*t,const market&m){
  printf("  %-24s ra=%-10lld rb=%-10lld k=%-16llu a_bets=%-8lld b_bets=%lld\n",
    t,(long long)m.ra,(long long)m.rb,(unsigned long long)m.k,(long long)m.a_bets,(long long)m.b_bets);}
int main(){
  const int64_t LIQ=211000, X=1000, Y=960958;
  market h=mk(LIQ); bet hb=place(h,0,X);
  printf("honest: %lld on A buys weight %lld\n", (long long)X,(long long)hb.weight);
  market s=mk(LIQ); show("created",s);
  bet p1=place(s,0,X);   show("1. bet A (1000)",s);
  bet p2=place(s,1,Y);   printf("  2. bet B (%lld) weight=%lld\n",(long long)Y,(long long)p2.weight); show("",s);
  printf("  3. cancel A: %s\n", cancel(s,p1)?"ok":"refused"); show("",s);
  bet p3=place(s,0,X);   printf("  4. bet A again (1000) weight=%lld  <-- honest %lld  (%.1fx)\n",
    (long long)p3.weight,(long long)hb.weight,(double)p3.weight/hb.weight); show("",s);
  printf("  5. cancel B: %s\n", cancel(s,p2)?"ok":"refused"); show("final",s);
  printf("\n  net: actor risked %lld, holds weight %lld; %lld refunded in full.\n",
    (long long)X,(long long)p3.weight,(long long)Y);
  printf("  a 2nd honest bettor staking %lld now gets weight %lld\n",
    (long long)X, (long long)place(s,0,X).weight);
  return 0;
}
