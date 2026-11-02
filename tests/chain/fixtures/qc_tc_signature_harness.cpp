#include <graphene/protocol/block_vote.hpp>
#include <graphene/protocol/block.hpp>
#include <graphene/chain/emergency_exit.hpp>
#include <fc/bitutil.hpp>
#include <optional>
#include <fc/crypto/elliptic.hpp>
#include <map>
#include <vector>
#include <iostream>
#include <cstdlib>
#define REQUIRE(c) do { if (!(c)) { std::cerr << "FAILED: " #c << " at " << __LINE__ << "\n"; std::abort(); } } while(false)
#undef ilog
#define ilog(...) ((void)0)
#define CHAIN_HARDFORK_16 16
using namespace graphene::protocol;
namespace protocol = graphene::protocol;
struct validator_object { public_key_type signing_key; };
struct validator_schedule_object { int num_scheduled_validators=0; std::vector<account_name_type> current_shuffled_validators; };
struct fake_block { block_id_type block_id; fc::time_point_sec timestamp; block_id_type id() const {return block_id;} };
struct database {
 validator_schedule_object schedule; std::map<account_name_type,validator_object> vals; chain_id_type chain;
 std::optional<fake_block> block; bool hf16=true; protocol::block_qc_certificate remembered;
 const validator_schedule_object& get_validator_schedule_object() const { return schedule; }
 const validator_object* find_validator(const account_name_type& a) const { auto it=vals.find(a);return it==vals.end()?nullptr:&it->second; }
 chain_id_type get_chain_id() const {return chain;}
 bool has_hardfork(int) const {return hf16;}
 std::optional<fake_block> fetch_block_by_number(uint32_t n) const {return block && protocol::block_header::num_from_id(block->id())==n ? block : std::nullopt;}
 uint32_t round_of(fc::time_point_sec t) const {return t.sec_since_epoch();}
 static uint32_t block_qc_quorum(uint32_t n) {return graphene::chain::emergency_quorum_for(n);}
 void remember_block_qc(const protocol::block_qc_certificate& c) {remembered=c;}
 bool accept_block_qc_certificate(const protocol::block_qc_certificate&);
 bool verify_block_vote(uint32_t,const block_id_type&,uint32_t,const account_name_type&,const signature_type&) const;
 const validator_object* scheduled_timeout_voter(const account_name_type&) const;
 bool verify_timeout_vote(uint32_t,uint32_t,const account_name_type&,const signature_type&) const;
 bool verify_timeout_vote_v2(uint32_t,uint32_t,const block_id_type&,const account_name_type&,const signature_type&) const;
};
namespace graphene { namespace protocol {
// @BLOCK_NUM_METHOD@
} }
// @METHODS@
int main(){
 auto alice_key=fc::ecc::private_key::regenerate(fc::sha256::hash(std::string("test-alice")));
 auto mallory_key=fc::ecc::private_key::regenerate(fc::sha256::hash(std::string("test-mallory")));
 const account_name_type alice("alice"), mallory("mallory"), inactive("inactive");
 database db; db.chain=fc::sha256::hash(std::string("network-a"));
 db.vals[alice]={public_key_type(alice_key.get_public_key())}; db.vals[mallory]={public_key_type(mallory_key.get_public_key())};
 db.vals[inactive]={public_key_type()}; db.schedule.current_shuffled_validators={alice,inactive};db.schedule.num_scheduled_validators=2;
 const auto id=fc::ripemd160::hash(std::string("block-a")); const auto other=fc::ripemd160::hash(std::string("block-b"));
 const auto vote=alice_key.sign_compact(protocol::block_vote_digest(db.chain,123,id,200));
 REQUIRE(db.verify_block_vote(123,id,200,alice,vote));
 REQUIRE(!db.verify_block_vote(124,id,200,alice,vote)); REQUIRE(!db.verify_block_vote(123,other,200,alice,vote));
 REQUIRE(!db.verify_block_vote(123,id,201,alice,vote)); REQUIRE(!db.verify_block_vote(123,id,200,mallory,vote));
 REQUIRE(!db.verify_block_vote(123,id,200,inactive,vote));
 const auto fake=mallory_key.sign_compact(protocol::block_vote_digest(db.chain,123,id,200)); REQUIRE(!db.verify_block_vote(123,id,200,alice,fake));
 auto other_chain=db; other_chain.chain=fc::sha256::hash(std::string("network-b")); REQUIRE(!other_chain.verify_block_vote(123,id,200,alice,vote));
 const auto t1=alice_key.sign_compact(protocol::block_timeout_digest(db.chain,205,200));
 REQUIRE(db.verify_timeout_vote(205,200,alice,t1)); REQUIRE(!db.verify_timeout_vote(205,201,alice,t1));
 REQUIRE(!db.verify_timeout_vote(206,200,alice,t1)); REQUIRE(!db.verify_timeout_vote(200,200,alice,t1));
 REQUIRE(!db.verify_timeout_vote(205,200,mallory,t1)); REQUIRE(!db.verify_timeout_vote(205,200,alice,vote));
 const auto t2=alice_key.sign_compact(protocol::block_timeout_digest_v2(db.chain,205,200,id));
 REQUIRE(db.verify_timeout_vote_v2(205,200,id,alice,t2)); REQUIRE(!db.verify_timeout_vote_v2(205,200,other,alice,t2));
 REQUIRE(!db.verify_timeout_vote_v2(205,200,block_id_type(),alice,t2)); REQUIRE(!db.verify_timeout_vote_v2(205,0,id,alice,t2));
 REQUIRE(!db.verify_timeout_vote_v2(205,200,id,alice,t1)); REQUIRE(!db.verify_timeout_vote(205,200,alice,t2));
 const auto zero=alice_key.sign_compact(protocol::block_timeout_digest_v2(db.chain,205,0,block_id_type()));
 REQUIRE(db.verify_timeout_vote_v2(205,0,block_id_type(),alice,zero));
 // QC admission uses the actual database function, real quorum and secp256k1.
 database q; q.chain=db.chain; auto qc_id=id; qc_id._hash[0]=fc::endian_reverse_u32(123);
 q.block=fake_block{qc_id,fc::time_point_sec(200)};
 protocol::block_qc_certificate qc; qc.block_num=123;qc.block_id=qc_id;qc.round=200;
 for(int i=0;i<4;++i){
  const account_name_type name(std::string("val")+std::to_string(i));
  auto key=fc::ecc::private_key::regenerate(fc::sha256::hash(std::string("key")+std::to_string(i)));
  q.vals[name]={public_key_type(key.get_public_key())}; q.schedule.current_shuffled_validators.push_back(name);
  qc.signatures[name]=key.sign_compact(protocol::block_vote_digest(q.chain,123,qc_id,200));
 }
 q.schedule.num_scheduled_validators=4;
 REQUIRE(q.accept_block_qc_certificate(qc)); REQUIRE(q.remembered.signatures.size()==4);
 auto short_qc=qc; short_qc.signatures.erase(short_qc.signatures.begin()); short_qc.signatures.erase(short_qc.signatures.begin());
 REQUIRE(!q.accept_block_qc_certificate(short_qc));
 auto mixed=qc; mixed.signatures.begin()->second=mallory_key.sign_compact(protocol::block_vote_digest(q.chain,123,qc_id,200));
 REQUIRE(q.accept_block_qc_certificate(mixed)); REQUIRE(q.remembered.signatures.size()==3);
 mixed.signatures.begin()->second=vote; auto second=mixed.signatures.begin();++second;second->second=vote;
 REQUIRE(!q.accept_block_qc_certificate(mixed));
 auto wrong=qc; wrong.block_id=other; REQUIRE(!q.accept_block_qc_certificate(wrong));
 wrong=qc; wrong.round=201; REQUIRE(!q.accept_block_qc_certificate(wrong));
 q.hf16=false; REQUIRE(!q.accept_block_qc_certificate(qc)); q.hf16=true;
 q.block.reset(); REQUIRE(!q.accept_block_qc_certificate(qc));
 std::cout << "PASS: real QC admission, quorum and QC/TC signature checks reject altered inputs\n";
}
