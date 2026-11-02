// Real FC scheduler and TCP sockets; stubbed peers and message serialization.

#include <fc/thread/thread.hpp>
#include <fc/time.hpp>
#include <fc/network/tcp_socket.hpp>
#include <fc/network/ip.hpp>
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
 std::map<int,state> _peer_states; std::map<int,std::shared_ptr<fc::tcp_socket>> _connections; fake_server _tcp_server;
 int started_slow=0, finished_slow=0, delivered_fast=0; bool long_wait=false;
 status build_hello_message(){status s;s.head_block_num=101;s.lib_block_num=99;return s;}
 void send_message(int id,message frame) {
  if(id==1){
   ++started_slow; auto socket=_connections.at(id); std::vector<char> bytes(16*1024*1024,'x'); size_t offset=0;
   try { while(offset<bytes.size()) offset+=socket->writesome(bytes.data()+offset,bytes.size()-offset); ++finished_slow; } catch(const fc::exception&) {}
  } else {
   int value=frame.s.lib_block_num; auto ptr=reinterpret_cast<const char*>(&value); size_t offset=0;
   while(offset<sizeof(value)) offset+=_connections.at(id)->writesome(ptr+offset,sizeof(value)-offset);
   ++delivered_fast;
  }
 }
 void broadcast_chain_status(); void close();
};
// @BROADCAST_METHOD@
// @CLOSE_METHOD@
int main(){
 int listener=::socket(AF_INET,SOCK_STREAM,0); assert(listener>=0);
 sockaddr_in address{}; address.sin_family=AF_INET; address.sin_addr.s_addr=htonl(INADDR_LOOPBACK); address.sin_port=0;
 assert(::bind(listener,reinterpret_cast<sockaddr*>(&address),sizeof(address))==0);
 assert(::listen(listener,2)==0); socklen_t length=sizeof(address);
 assert(::getsockname(listener,reinterpret_cast<sockaddr*>(&address),&length)==0);
 std::atomic<bool> got{false}; int slow=-1, fast=-1;
 std::thread server([&]{
  slow=::accept(listener,nullptr,nullptr); assert(slow>=0);
  fast=::accept(listener,nullptr,nullptr); assert(fast>=0);
  got=true;
 });
 fc::thread worker("test-p2p-socket"); dlt_p2p_node node; delegate chain;
 node._thread=&worker; node._delegate=&chain; node._peer_states={{1,{1,true}},{2,{1,false}}};
 worker.async([&]{
  auto endpoint=fc::ip::endpoint(fc::ip::address("127.0.0.1"),ntohs(address.sin_port));
  for(int id : {1,2}){auto socket=std::make_shared<fc::tcp_socket>();socket->connect_to(endpoint);node._connections[id]=socket;}
 },"connect").wait();
 auto begin=std::chrono::steady_clock::now();
 for(int i=0;i<100 && !got;++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
 assert(got);
 std::cerr << "before close\n"; worker.async([&]{for(auto& item:node._connections) item.second->close();node._connections.clear();},"close").wait(); std::cerr << "after close\n";
 auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-begin).count();
 assert(elapsed<2000); assert(node._status_send_fibers.empty() && node._status_sends_inflight.empty());
 std::cerr << "before join\n"; server.join(); std::cerr << "after join\n"; ::close(slow); ::close(fast); ::close(listener); std::this_thread::sleep_for(std::chrono::milliseconds(500)); std::cerr << "before quit\n"; worker.quit(); std::cerr << "after quit\n";
 std::cout << "PASS: FC loopback sockets connect and shut down without status broadcast\n";
}
