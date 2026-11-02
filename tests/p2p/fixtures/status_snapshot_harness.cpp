#include <cassert>
#include <cstdint>
#include <iostream>
struct dlt_hello_message {
 uint32_t protocol_version=0,head_block_id=0,head_block_num=0,lib_block_id=0,lib_block_num=0;
 uint32_t dlt_earliest_block=0,dlt_latest_block=0,emergency_active=0,has_emergency_key=0;
 uint32_t fork_status=0,node_status=0,node_id=0,capabilities=0;
};
constexpr uint32_t DLT_CAP_BLOCK_VOTE=1,DLT_CAP_TIMEOUT_VOTE=2,DLT_CAP_BLOCK_QC=4,DLT_CAP_TIMEOUT_VOTE_V2=8;
namespace graphene { namespace network {
struct dlt_chain_position { uint32_t head_id=0,lib_id=0,head_num=0,lib_num=0; };
}}
struct properties {uint32_t head_block_id=100,head_block_number=100,last_irreversible_block_id=99,last_irreversible_block_num=99;};
struct fake_db {
 mutable properties p; mutable bool locked=false; mutable int reads=0; bool open=true,advance=false;
 bool is_state_open() const {return open;}
 const properties& get_dynamic_global_properties() const {assert(locked);return p;}
 template<class F> auto with_weak_read_lock(F f) const -> decltype(f()) {
  assert(!locked);locked=true;++reads; auto out=f();locked=false;
  if(advance){++p.head_block_id;++p.head_block_number;++p.last_irreversible_block_id;++p.last_irreversible_block_num;}
  return out;
 }
};
struct fake_chain {fake_db d; fake_db& db() {return d;} const fake_db& db() const {return d;}};
struct dlt_delegate {
 fake_chain& chain;
 graphene::network::dlt_chain_position get_chain_position() const;
 uint32_t get_dlt_earliest_block() const {return 1;}
 uint32_t get_dlt_latest_block() const {return chain.db().p.head_block_number;}
 bool is_emergency_consensus_active() const {return false;}
 bool has_emergency_private_key() const {return false;}
};
struct dlt_p2p_node {
 dlt_delegate* _delegate; uint32_t _fork_status=0,_node_status=0,_node_id=0;
 dlt_hello_message build_hello_message() const;
};
// @DELEGATE_POSITION@
// @BUILD_HELLO@
int main() {
 fake_chain chain;dlt_delegate d{chain};dlt_p2p_node n{&d};
 chain.d.open=false;auto unopened=n.build_hello_message();
 assert(unopened.head_block_num==0 && unopened.lib_block_num==0 && chain.d.reads==0);
 chain.d.open=true;chain.d.advance=true;auto snapshot=n.build_hello_message();
 assert(chain.d.reads==1 && !chain.d.locked);
 assert(snapshot.head_block_id==100 && snapshot.head_block_num==100);
 assert(snapshot.lib_block_id==99 && snapshot.lib_block_num==99 && snapshot.lib_block_num<=snapshot.head_block_num);
 assert(chain.d.p.head_block_number==101 && chain.d.p.last_irreversible_block_num==100);
 std::cout << "PASS: delegate snapshots head and LIB under one read lock; hello preserves that state\n";
}
