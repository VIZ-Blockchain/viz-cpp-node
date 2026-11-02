#include <atomic>
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <iostream>
#include <functional>
constexpr int DLT_PEER_LIFECYCLE_ACTIVE=1;
struct state {int lifecycle_state=1;uint32_t peer_lib_num=0;std::string endpoint;};
struct delegate {uint32_t current=0;uint32_t get_lib_block_num()const{return current;}};
struct hooked_atomic {
 std::atomic<uint32_t> value{0}; mutable std::function<void()> after_load;
 uint32_t load(std::memory_order order)const {
  const auto old=value.load(order);auto hook=std::move(after_load);if(hook)hook();return old;
 }
 void store(uint32_t v,std::memory_order order){value.store(v,order);}
};
struct dlt_p2p_node {
 struct peer_lib_view {uint32_t peers_above_our_lib=0,best_peer_lib=0,our_lib=0;bool consistent=true;};
 delegate* _delegate=nullptr;
 std::map<int,state> _peer_states;std::set<std::string> _trusted_peers;std::vector<std::string> _seed_nodes;
 std::atomic<uint32_t> _cached_our_lib{0},_best_peer_lib{0};hooked_atomic _peers_above_our_lib;
 std::atomic<uint64_t> _peer_lib_version{0};
 peer_lib_view get_peer_lib_view()const noexcept;
 void refresh_peer_lib_cache();
};
// @GETTER@
// @REFRESH@
int main(){
 delegate d;dlt_p2p_node n;n._delegate=&d;n._trusted_peers={"a","b"};
 auto publish=[&](bool b){d.current=b?200:100;n._peer_states[1]={1,b?250u:150u,"a"};n._peer_states[2]={1,140,"b"};n.refresh_peer_lib_cache();};
 publish(false);auto a=n.get_peer_lib_view();assert(a.consistent && a.peers_above_our_lib==2 && a.best_peer_lib==150 && a.our_lib==100);
 publish(true);auto b=n.get_peer_lib_view();assert(b.consistent && b.peers_above_our_lib==1 && b.best_peer_lib==250 && b.our_lib==200);
 // Publish a complete new generation after the count is read but before the
 // other fields. The reader must notice and retry, not return the mixed tuple.
 publish(false);n._peers_above_our_lib.after_load=[&]{publish(true);};
 auto crossed=n.get_peer_lib_view();
 assert(crossed.consistent && crossed.peers_above_our_lib==1 && crossed.best_peer_lib==250 && crossed.our_lib==200);
 n._peer_lib_version.fetch_add(1);auto busy=n.get_peer_lib_view();assert(!busy.consistent);n._peer_lib_version.fetch_add(1);
 publish(false);std::atomic<bool> start{false};
 std::thread writer([&]{while(!start.load())std::this_thread::yield();for(int i=0;i<50000;++i)publish(i%2);});
 start.store(true);size_t stable=0,busy_count=0;
 for(int i=0;i<100000;++i){auto v=n.get_peer_lib_view();if(!v.consistent){++busy_count;continue;}++stable;
  assert((v.peers_above_our_lib==2 && v.best_peer_lib==150 && v.our_lib==100) ||
         (v.peers_above_our_lib==1 && v.best_peer_lib==250 && v.our_lib==200));}
 writer.join();assert(stable>0);
 std::cout<<"PASS: stable views are coherent; in-progress publications report unavailable ("<<busy_count<<" busy reads)\n";
}
