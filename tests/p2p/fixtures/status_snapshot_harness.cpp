#include <cassert>
#include <cstdint>
#include <iostream>
struct dlt_hello_message {
 uint32_t protocol_version=0,head_block_id=0,head_block_num=0,lib_block_id=0,lib_block_num=0;
 uint32_t dlt_earliest_block=0,dlt_latest_block=0,emergency_active=0,has_emergency_key=0;
 uint32_t fork_status=0,node_status=0,node_id=0,capabilities=0;
};
constexpr uint32_t DLT_CAP_BLOCK_VOTE=1,DLT_CAP_TIMEOUT_VOTE=2,DLT_CAP_BLOCK_QC=4,DLT_CAP_TIMEOUT_VOTE_V2=8;
struct delegate {
 mutable uint32_t head=100,lib=99; bool advance=false;
 uint32_t get_head_block_id() const {auto x=head; if(advance){++head;++lib;} return x;}
 uint32_t get_head_block_num() const {auto x=head; if(advance){++head;++lib;} return x;}
 uint32_t get_lib_block_id() const {auto x=lib; if(advance){++head;++lib;} return x;}
 uint32_t get_lib_block_num() const {return lib;}
 uint32_t get_dlt_earliest_block() const {return 1;}
 uint32_t get_dlt_latest_block() const {return head;}
 bool is_emergency_consensus_active() const {return false;}
 bool has_emergency_private_key() const {return false;}
};
struct dlt_p2p_node {
 delegate* _delegate; uint32_t _fork_status=0,_node_status=0,_node_id=0;
 dlt_hello_message build_hello_message() const;
};
// @BUILD_HELLO@
int main() {
 delegate stable; dlt_p2p_node n{&stable}; auto good=n.build_hello_message();
 assert(good.head_block_id==good.head_block_num && good.lib_block_id==good.lib_block_num && good.lib_block_num<=good.head_block_num);
 delegate advancing; advancing.advance=true; dlt_p2p_node raced{&advancing}; auto mixed=raced.build_hello_message();
 assert(mixed.head_block_id==100 && mixed.head_block_num==101);
 assert(mixed.lib_block_id==101 && mixed.lib_block_num==102 && mixed.lib_block_num>mixed.head_block_num);
 std::cout << "PASS: independent status reads can mix successive internally valid chain states\n";
}
