// Real FC scheduler; stubbed message, peers and sockets. Filled by the Python driver.

#include <fc/thread/thread.hpp>
#include <fc/time.hpp>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
#undef ilog
#define ilog(...) ((void)0)
using peer_id=int;
const int DLT_PEER_LIFECYCLE_ACTIVE=1;
struct status { int fork_status=0,head_block_id=0,head_block_num=0,lib_block_id=0,lib_block_num=0,dlt_earliest_block=0,dlt_latest_block=0,node_status=0; };
using dlt_fork_status_message=status;
struct message { status s; explicit message(status v):s(v){} };
struct state { int lifecycle_state; bool exchange_enabled; };
struct delegate {};
struct fake_socket { bool closed=false; void close(){closed=true;} };
struct fake_server { void close() {} };
struct dlt_p2p_node {
 delegate* _delegate=nullptr; int _fork_status=7; fc::thread* _thread=nullptr; bool _running=true;
 std::map<peer_id,fc::future<void>> _status_send_fibers,_read_fibers;
 std::set<peer_id> _status_sends_inflight,_peer_sending;
 fc::future<void> _accept_fiber,_periodic_fiber;
 std::map<int,state> _peer_states; std::map<int,std::shared_ptr<fake_socket>> _connections; fake_server _tcp_server;
 int started_slow=0, finished_slow=0, delivered_fast=0; bool long_wait=false;
 status build_hello_message(){status s;s.head_block_num=101;s.lib_block_num=99;return s;}
 void send_message(int id,message) {
  if(id==1){++started_slow;fc::usleep(fc::milliseconds(long_wait?5000:800));++finished_slow;}
  else if(id==2) ++delivered_fast;
 }
 void broadcast_chain_status(); void close();
};
// @BROADCAST_METHOD@
// @CLOSE_METHOD@
int main(){
 fc::thread worker("test-p2p"); dlt_p2p_node node; delegate chain;
 node._thread=&worker;node._delegate=&chain;node._peer_states={{1,{1,true}},{2,{1,false}}};
 auto begin=std::chrono::steady_clock::now();
 worker.async([&]{node.broadcast_chain_status();},"start").wait();
 auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
 assert(elapsed<500);
 std::this_thread::sleep_for(std::chrono::milliseconds(100));
 auto early=worker.async([&]{return node.started_slow==1 && node.finished_slow==0 && node.delivered_fast==1;},"check");
 assert(early.wait());
 worker.async([&]{node.broadcast_chain_status();},"repeat").wait();
 std::this_thread::sleep_for(std::chrono::milliseconds(30));
 assert(worker.async([&]{return node.started_slow==1;},"bound").wait());
 std::this_thread::sleep_for(std::chrono::milliseconds(850));
 assert(worker.async([&]{return node.finished_slow==1 && node.delivered_fast>=1;},"done").wait());
 worker.async([&]{node.long_wait=true;node.broadcast_chain_status();},"long").wait();
 std::this_thread::sleep_for(std::chrono::milliseconds(30));
 begin=std::chrono::steady_clock::now();
 worker.async([&]{node.close();},"close").wait();
 elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
 assert(elapsed<2000);
 assert(node._status_send_fibers.empty() && node._status_sends_inflight.empty());
 worker.quit(); std::cout << "PASS: real FC fibers keep heartbeat and healthy peer moving; sends stay bounded and close cancels them\n";
}
