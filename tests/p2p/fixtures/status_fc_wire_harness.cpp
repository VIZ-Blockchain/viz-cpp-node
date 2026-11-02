// Real dispatch, send queue, framing, FC scheduler and sockets. Uses the real status message and FC raw serialization.

#include <fc/thread/thread.hpp>
#include <fc/time.hpp>
#include <fc/network/tcp_socket.hpp>
#include <fc/network/ip.hpp>
#include <graphene/network/message.hpp>
#include <graphene/network/dlt_p2p_messages.hpp>
#include <deque>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <atomic>
#include <map>
#include <set>
#include <vector>
#include <memory>
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
#include <cstdlib>
#undef assert
#define assert(condition) do { if (!(condition)) { std::cerr << "Assertion failed: " #condition " at " << __FILE__ << ":" << __LINE__ << "\n"; std::abort(); } } while (false)
#undef ilog
#define ilog(...) ((void)0)
#undef wlog
#define wlog(...) ((void)0)
using peer_id=int;
using namespace graphene::network;
const dlt_message_type_enum graphene::network::dlt_fork_status_message::type = dlt_fork_status_message_type;
using status = dlt_fork_status_message;
struct dlt_peer_state {
 static constexpr size_t SEND_QUEUE_MAX_DEPTH=100;
 int lifecycle_state=0; bool exchange_enabled=false, fork_alignment=false; std::string endpoint;
 uint8_t peer_node_status=0, peer_fork_status=0;
 graphene::protocol::block_id_type peer_head_id, peer_lib_id;
 uint32_t peer_head_num=0, peer_lib_num=0, peer_dlt_earliest=0, peer_dlt_latest=0;
 std::deque<std::vector<char>> send_queue; uint32_t send_queue_total=0,send_queue_dropped=0;
};
using state=dlt_peer_state;
struct delegate {
 bool is_block_known(const graphene::protocol::block_id_type&) { return false; }
 uint32_t get_lib_block_num() { return 50; }
};
struct fake_socket { bool closed=false; void close(){closed=true;} };
struct fake_server { void close() {} };
struct dlt_p2p_node {
 delegate* _delegate=nullptr; int _fork_status=7; fc::thread* _thread=nullptr; bool _running=true;
 std::map<peer_id,fc::future<void>> _status_send_fibers,_read_fibers;
 std::set<peer_id> _status_sends_inflight,_peer_sending,_disconnect_in_progress;
 fc::future<void> _accept_fiber,_periodic_fiber;
 std::map<int,state> _peer_states; std::map<int,std::shared_ptr<fc::tcp_socket>> _connections; fake_server _tcp_server;
 int disconnects=0, packets=0, build_calls=0; uint32_t _highest_seen_block_num=0, test_lib=99;
 static constexpr uint32_t CHAIN_STATUS_BROADCAST_INTERVAL_SEC=30;
 std::atomic<bool> _block_processing_paused{false}; fc::time_point _last_chain_status_broadcast;
 std::set<std::string> _trusted_peers; std::vector<std::string> _seed_nodes;
 std::atomic<uint32_t> _cached_our_lib{0}, _best_peer_lib{0}, _peers_above_our_lib{0};
 std::atomic<uint64_t> _peer_lib_version{0};
 status build_hello_message(){++build_calls;status s;s.head_block_num=150;s.lib_block_num=test_lib;return s;}
 void record_packet_result(peer_id,bool){++packets;}
 void on_dlt_fork_status(peer_id,const dlt_fork_status_message&); void refresh_peer_lib_cache();
 void handle_disconnect(peer_id, const char*) { ++disconnects; }
 void send_message(peer_id,const message&); void drain_send_queue(peer_id,std::vector<char>);
 void broadcast_chain_status(); void periodic_chain_status(); void close();
};
// @HELPERS@
// @SEND_METHOD@
// @DRAIN_METHOD@
// @BROADCAST_METHOD@
// @PERIODIC_METHOD@
// @CLOSE_METHOD@
// @RECEIVE_METHOD@
// @CACHE_METHOD@
int main(){
 int listener=::socket(AF_INET,SOCK_STREAM,0); assert(listener>=0);
 sockaddr_in address{}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_LOOPBACK); address.sin_port=0;
 assert(::bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
 assert(::listen(listener,2)==0); socklen_t length=sizeof(address);
 assert(::getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)==0);
 std::atomic<int> got{0}; int slow=-1, fast=-1;
 dlt_p2p_node receiver; delegate receiver_chain; receiver._delegate=&receiver_chain;
 receiver._peer_states[7].lifecycle_state=DLT_PEER_LIFECYCLE_ACTIVE;
 receiver._peer_states[7].endpoint="seed.example:2001"; receiver._trusted_peers.insert("seed.example:2001");
 std::thread server([&]{
  slow=::accept(listener,nullptr,nullptr); assert(slow>=0);
  fast=::accept(listener,nullptr,nullptr); assert(fast>=0);
  for(uint32_t expected : {99,129}) {
   message_header header{}; size_t offset=0;
   while(offset<sizeof(header)){auto n=::read(fast,reinterpret_cast<char*>(&header)+offset,sizeof(header)-offset); assert(n>0);offset+=n;}
   assert(header.msg_type==dlt_fork_status_message_type && header.size>0 && header.size<1000);
   message frame; frame.msg_type=header.msg_type; frame.size=header.size; frame.data.resize(header.size); offset=0;
   while(offset<frame.data.size()){auto n=::read(fast,frame.data.data()+offset,frame.data.size()-offset); assert(n>0);offset+=n;}
   auto decoded=frame.as<dlt_fork_status_message>(); receiver.on_dlt_fork_status(7,decoded);
   assert(receiver._peer_states.at(7).peer_lib_num==expected);
   assert(receiver._best_peer_lib.load()==expected && receiver._cached_our_lib.load()==50 && receiver._peers_above_our_lib.load()==1);
   ++got;
  }
 });
 fc::thread worker("test-p2p-socket"); dlt_p2p_node node; delegate chain;
 node._thread=&worker; node._delegate=&chain; node._peer_states[1].lifecycle_state=DLT_PEER_LIFECYCLE_ACTIVE; node._peer_states[2].lifecycle_state=DLT_PEER_LIFECYCLE_ACTIVE;
 worker.async([&]{
  auto endpoint=fc::ip::endpoint(fc::ip::address("127.0.0.1"),ntohs(address.sin_port));
  for(int id : {1,2}){auto socket=std::make_shared<fc::tcp_socket>();socket->connect_to(endpoint);node._connections[id]=socket;}
 },"connect").wait();
 // Block the actual write-drain on peer 1 with a large unrelated frame.
 message large; large.msg_type=dlt_transaction_message_type; large.data.resize(16*1024*1024,'x'); large.size=large.data.size();
 auto slow_write=worker.async([&]{node.send_message(1,large);},"slow writer");
 for(int i=0;i<100 && !worker.async([&]{return node._peer_sending.count(1)!=0;},"wait stalled").wait();++i)
   std::this_thread::sleep_for(std::chrono::milliseconds(10));
 assert(worker.async([&]{return node._peer_sending.count(1)!=0;},"stalled").wait());
 auto begin=std::chrono::steady_clock::now();
 worker.async([&]{node._block_processing_paused=true;node.periodic_chain_status();},"paused initial").wait();
 assert(worker.async([&]{return node.build_calls==0 && node._last_chain_status_broadcast==fc::time_point();},"no read while paused").wait());
 worker.async([&]{node._block_processing_paused=false;node.periodic_chain_status();},"start").wait();
 auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
 assert(elapsed<500);
 for(int i=0;i<100 && got<1;++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
 assert(got>=1);
 assert(worker.async([&]{return node._peer_states.at(1).send_queue.size()==1;},"queued").wait());
 worker.async([&]{node.test_lib=129;node.periodic_chain_status();},"too early").wait();
 assert(worker.async([&]{return node.build_calls==1;},"rate limited").wait());
 worker.async([&]{node._last_chain_status_broadcast=fc::time_point::now()-fc::seconds(31);node._block_processing_paused=true;node.periodic_chain_status();},"paused due").wait();
 assert(worker.async([&]{return node.build_calls==1;},"paused overdue").wait());
 worker.async([&]{node._block_processing_paused=false;node.periodic_chain_status();},"repeat").wait();
 for(int i=0;i<100 && got<2;++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
 assert(got==2);
 for(int i=0;i<100 && worker.async([&]{return node._status_sends_inflight.count(1)!=0;},"wait repeat").wait();++i)
   std::this_thread::sleep_for(std::chrono::milliseconds(10));
 assert(worker.async([&]{
  const auto& state=node._peer_states.at(1);
  if(node._status_sends_inflight.count(1)!=0 || state.send_queue.size()!=1 || state.send_queue_total!=1) return false;
  const auto& bytes=state.send_queue.front(); message_header header{};
  if(bytes.size()<sizeof(header)) return false;
  std::memcpy(&header,bytes.data(),sizeof(header));
  if(header.msg_type!=dlt_fork_status_message_type || header.size!=bytes.size()-sizeof(header)) return false;
  message frame; frame.msg_type=header.msg_type;frame.size=header.size;frame.data.assign(bytes.begin()+sizeof(header),bytes.end());
  return frame.as<dlt_fork_status_message>().lib_block_num==129;
 },"coalesced").wait());
 begin=std::chrono::steady_clock::now();
 std::cerr << "before close\n"; worker.async([&]{node.close();},"close").wait(); std::cerr << "after close\n";
 elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
 assert(elapsed<2000); assert(node._status_send_fibers.empty() && node._status_sends_inflight.empty());
 try { slow_write.wait(); } catch(const fc::exception&) {}
 std::cerr << "before join\n"; server.join(); std::cerr << "after join\n"; ::close(slow); ::close(fast); ::close(listener); std::cerr << "before quit\n"; worker.quit(); std::cerr << "after quit\n";
 std::cout << "PASS: periodic status respects timing and pause; real TCP status updates peer and trusted LIB cache\n";
}
