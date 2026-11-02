#include <graphene/protocol/block_vote.hpp>
#include <graphene/protocol/block.hpp>
#include <graphene/chain/emergency_exit.hpp>
#include <fc/bitutil.hpp>
#include <optional>
#include <fc/crypto/elliptic.hpp>
#include <map>
#include <mutex>
#include <vector>
#include <iostream>
#include <cstdlib>
#define REQUIRE(c) do { if (!(c)) { std::cerr << "FAILED: " #c << " at " << __LINE__ << "\n"; std::abort(); } } while(false)
#undef ilog
#define ilog(...) ((void)0)
#define CHAIN_HARDFORK_16 16
#define CHAIN_HARDFORK_18 18
using namespace graphene::protocol;
namespace protocol = graphene::protocol;
struct validator_object { public_key_type signing_key; };
struct validator_schedule_object { int num_scheduled_validators=0; std::vector<account_name_type> current_shuffled_validators; };
struct fake_block { block_id_type block_id; fc::time_point_sec timestamp; block_id_type id() const {return block_id;} };
struct database {
 validator_schedule_object schedule; std::map<account_name_type,validator_object> vals; chain_id_type chain;
 std::optional<fake_block> block; bool hf16=true,hf18=false; protocol::block_qc_certificate remembered;
 struct block_qc {block_id_type block_id;uint32_t round=0;};
 mutable std::mutex _block_votes_mutex; std::map<uint32_t,block_qc> _block_qcs;
 std::map<uint32_t,block_id_type> branch_ids; std::map<block_id_type,signed_block> branch_blocks;
 static constexpr uint32_t skip_validator_signature=1;
 struct dynprops { account_name_type current_validator; };
 const validator_schedule_object& get_validator_schedule_object() const { return schedule; }
 const validator_object* find_validator(const account_name_type& a) const { auto it=vals.find(a);return it==vals.end()?nullptr:&it->second; }
 chain_id_type get_chain_id() const {return chain;}
 bool has_hardfork(int n) const {return n==CHAIN_HARDFORK_16 ? hf16 : n==CHAIN_HARDFORK_18 && hf18;}
 bool emergency_unstick_block(fc::time_point_sec) const {return false;}
 bool block_needs_parent_qc(const std::string&) const {return true;}
 dynprops get_dynamic_global_properties() const {return {};}
 fc::time_point_sec head_block_time() const {return fc::time_point_sec(200);}
 fc::optional<signed_block> fetch_block_by_id(const block_id_type& id) const {auto it=branch_blocks.find(id);if(it==branch_blocks.end())return {};return it->second;}
 block_id_type find_block_id_for_num(uint32_t n) const {auto it=branch_ids.find(n);return it==branch_ids.end()?block_id_type():it->second;}
 uint32_t highest_qc_on_main_branch(uint32_t) const;
 void validate_block_qc(const signed_block&,uint32_t) const;
 static uint32_t block_cert_round(const signed_block&);
 uint32_t verified_fork_cert_round(const signed_block&) const;
 std::optional<fake_block> fetch_block_by_number(uint32_t n) const {return block && protocol::block_header::num_from_id(block->id())==n ? block : std::nullopt;}
 uint32_t round_of(fc::time_point_sec t) const {return t.sec_since_epoch();}
 static uint32_t block_qc_quorum(uint32_t n) {return graphene::chain::emergency_quorum_for(n);}
 void remember_block_qc(const protocol::block_qc_certificate& c) {remembered=c;auto it=_block_qcs.find(c.block_num);if(it!=_block_qcs.end() && it->second.round>=c.round)return;_block_qcs[c.block_num]={c.block_id,c.round};}
 bool accept_block_qc_certificate(const protocol::block_qc_certificate&);
 bool verify_block_vote(uint32_t,const block_id_type&,uint32_t,const account_name_type&,const signature_type&) const;
 const validator_object* scheduled_timeout_voter(const account_name_type&) const;
 bool verify_timeout_vote(uint32_t,uint32_t,const account_name_type&,const signature_type&) const;
 bool verify_timeout_vote_v2(uint32_t,uint32_t,const block_id_type&,const account_name_type&,const signature_type&) const;
};
namespace graphene { namespace protocol {
// @BLOCK_NUM_METHOD@
// @BLOCK_SIGNATURE_METHODS@
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
 const auto original_chain=db.chain; db.chain=fc::sha256::hash(std::string("network-b"));
 REQUIRE(!db.verify_block_vote(123,id,200,alice,vote)); db.chain=original_chain;
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
 // QC admission uses CURRENT validators and signing keys, not the historical set.
 // Keep exactly three valid signatures so changing one loses quorum.
 auto historic=qc;historic.signatures.erase(account_name_type("val3"));
 REQUIRE(q.accept_block_qc_certificate(historic));
 const account_name_type rotated("val0");
 auto old_signing_key=q.vals.at(rotated).signing_key;
 auto replacement=fc::ecc::private_key::regenerate(fc::sha256::hash(std::string("replacement-val0")));
 q.vals.at(rotated).signing_key=public_key_type(replacement.get_public_key());
 REQUIRE(!q.accept_block_qc_certificate(historic));
 q.vals.at(rotated).signing_key=old_signing_key;REQUIRE(q.accept_block_qc_certificate(historic));
 // A schedule replacement has the same consequence even if the old key remains known.
 auto old_schedule=q.schedule.current_shuffled_validators;
 q.schedule.current_shuffled_validators[0]=mallory;q.vals[mallory]={public_key_type(mallory_key.get_public_key())};
 REQUIRE(!q.accept_block_qc_certificate(historic));
 q.schedule.current_shuffled_validators=old_schedule;REQUIRE(q.accept_block_qc_certificate(historic));
 // Merely checking against today's keys cannot prove which keys were authorised historically.
 const auto previous_validators=q.vals;
 auto signed_after_rotation=historic;
 for(int i=0;i<3;++i){
  const account_name_type name(std::string("val")+std::to_string(i));
  auto next_key=fc::ecc::private_key::regenerate(fc::sha256::hash(std::string("rotated")+std::to_string(i)));
  q.vals[name]={public_key_type(next_key.get_public_key())};
  signed_after_rotation.signatures[name]=next_key.sign_compact(protocol::block_vote_digest(q.chain,123,qc_id,200));
 }
 REQUIRE(q.accept_block_qc_certificate(signed_after_rotation));
 q.vals=previous_validators;REQUIRE(!q.accept_block_qc_certificate(signed_after_rotation));

 q.hf16=false; REQUIRE(!q.accept_block_qc_certificate(qc)); q.hf16=true;
 q.block.reset(); REQUIRE(!q.accept_block_qc_certificate(qc));
 // Validate complete QC/TC carried by a real signed_block header; state is mocked.
 signed_block child; child.previous=qc_id; child.timestamp=fc::time_point_sec(204);child.validator="val0";
 protocol::block_timeout_certificate tc;tc.round=202;
 protocol::block_timeout_certificate_v2 tc2;tc2.round=202;
 for(int i=0;i<3;++i){
  const account_name_type name(std::string("val")+std::to_string(i));
  auto key=fc::ecc::private_key::regenerate(fc::sha256::hash(std::string("key")+std::to_string(i)));
  protocol::block_timeout_vote tv;tv.high_qc_round=200;tv.signature=key.sign_compact(protocol::block_timeout_digest(q.chain,202,200));tc.votes[name]=tv;
  protocol::block_timeout_vote_v2 tv2;tv2.high_qc_round=200;tv2.high_qc_block_id=qc_id;
  tv2.signature=key.sign_compact(protocol::block_timeout_digest_v2(q.chain,202,200,qc_id));tc2.votes[name]=tv2;
 }
 auto check=[&](const signed_block& b){try{q.validate_block_qc(b,0);return true;}catch(const fc::exception&){return false;}};
 child.extensions.insert(block_header_extensions(qc));child.extensions.insert(block_header_extensions(tc));
 REQUIRE(check(child));
 // Header validation also depends on the state supplied for the carrying block.
 // Unlike network QC admission it rejects even one invalid signature outright.
 q.vals.at(rotated).signing_key=public_key_type(replacement.get_public_key());
 REQUIRE(!check(child));
 q.vals.at(rotated).signing_key=old_signing_key; REQUIRE(check(child));
 auto rotated_tc=tc;
 rotated_tc.votes.at(rotated).signature=replacement.sign_compact(protocol::block_timeout_digest(q.chain,202,200));
 auto wrong_timeout_key=child;wrong_timeout_key.extensions.clear();
 wrong_timeout_key.extensions.insert(block_header_extensions(qc));wrong_timeout_key.extensions.insert(block_header_extensions(rotated_tc));
 REQUIRE(!check(wrong_timeout_key));
 auto bad_qc=qc;bad_qc.signatures.begin()->second=vote;
 auto corrupt_qc=child;corrupt_qc.extensions.clear();corrupt_qc.extensions.insert(block_header_extensions(bad_qc));corrupt_qc.extensions.insert(block_header_extensions(tc));
 REQUIRE(!check(corrupt_qc));
 auto no_qc=child;no_qc.extensions.clear();no_qc.extensions.insert(block_header_extensions(tc));REQUIRE(!check(no_qc));
 auto late_tc=tc;late_tc.round=204;
 auto invalid_window=child;invalid_window.extensions.clear();invalid_window.extensions.insert(block_header_extensions(qc));invalid_window.extensions.insert(block_header_extensions(late_tc));
 REQUIRE(!check(invalid_window));
 auto tc_few=tc;tc_few.votes.erase(tc_few.votes.begin());
 auto fewer=child;fewer.extensions.clear();fewer.extensions.insert(block_header_extensions(qc));fewer.extensions.insert(block_header_extensions(tc_few));
 REQUIRE(!check(fewer));
 auto bad_tc=tc;bad_tc.votes.begin()->second.signature=vote;
 auto corrupt=child;corrupt.extensions.clear();corrupt.extensions.insert(block_header_extensions(qc));corrupt.extensions.insert(block_header_extensions(bad_tc));
 REQUIRE(!check(corrupt));
 q.hf18=true; REQUIRE(!check(child));
 auto modern=child;modern.extensions.clear();modern.extensions.insert(block_header_extensions(qc));modern.extensions.insert(block_header_extensions(tc2));
 REQUIRE(check(modern));
 auto wrong_block=tc2;auto first=wrong_block.votes.begin();first->second.high_qc_block_id=other;
 auto first_key=fc::ecc::private_key::regenerate(fc::sha256::hash(std::string("key0")));
 first->second.signature=first_key.sign_compact(protocol::block_timeout_digest_v2(q.chain,202,200,other));
 auto incompatible=child;incompatible.extensions.clear();incompatible.extensions.insert(block_header_extensions(qc));incompatible.extensions.insert(block_header_extensions(wrong_block));
 REQUIRE(!check(incompatible));
 // Admission tests use a genuine producer signature and real fork certificate verifier.
 auto producer_key=fc::ecc::private_key::regenerate(fc::sha256::hash(std::string("key0")));
 auto admitted=[&](const signed_block& b){try{return q.verified_fork_cert_round(b);}catch(const fc::exception&){return uint32_t(0);}};
 q.hf18=false;child.sign(producer_key); REQUIRE(admitted(child)==202);
 auto bad_producer=child;bad_producer.sign(mallory_key);REQUIRE(admitted(bad_producer)==0);
 auto no_quorum=fewer;no_quorum.sign(producer_key); REQUIRE(admitted(no_quorum)==0);
 auto bad_votes=corrupt;bad_votes.sign(producer_key); REQUIRE(admitted(bad_votes)==0);
 // Validly signed votes can still describe a QC newer than the one attached.
 auto newer_tc=tc;auto old_first=newer_tc.votes.begin();old_first->second.high_qc_round=201;
 old_first->second.signature=producer_key.sign_compact(protocol::block_timeout_digest(q.chain,202,201));
 auto newer_child=child;newer_child.extensions.clear();newer_child.extensions.insert(block_header_extensions(qc));newer_child.extensions.insert(block_header_extensions(newer_tc));newer_child.sign(producer_key);
 REQUIRE(admitted(newer_child)==0); REQUIRE(!check(newer_child));
 auto older_tc=tc;auto older_first=older_tc.votes.begin();older_first->second.high_qc_round=199;
 older_first->second.signature=producer_key.sign_compact(protocol::block_timeout_digest(q.chain,202,199));
 auto older_child=child;older_child.extensions.clear();older_child.extensions.insert(block_header_extensions(qc));older_child.extensions.insert(block_header_extensions(older_tc));older_child.sign(producer_key);
 REQUIRE(admitted(older_child)==202);REQUIRE(check(older_child));
 q.hf18=true;modern.sign(producer_key);REQUIRE(admitted(modern)==202);
 // A valid signature alone must not prioritize a TC bound to another QC block.
 incompatible.sign(producer_key);REQUIRE(admitted(incompatible)==0);REQUIRE(!check(incompatible));
 auto newer_tc2=tc2;auto new_first=newer_tc2.votes.begin();new_first->second.high_qc_round=201;
 new_first->second.signature=producer_key.sign_compact(protocol::block_timeout_digest_v2(q.chain,202,201,qc_id));
 auto newer_modern=child;newer_modern.extensions.clear();newer_modern.extensions.insert(block_header_extensions(qc));newer_modern.extensions.insert(block_header_extensions(newer_tc2));newer_modern.sign(producer_key);
 REQUIRE(admitted(newer_modern)==0); REQUIRE(!check(newer_modern));
 auto older_tc2=tc2;auto older_v2=older_tc2.votes.begin();older_v2->second.high_qc_round=199;older_v2->second.high_qc_block_id=other;
 older_v2->second.signature=producer_key.sign_compact(protocol::block_timeout_digest_v2(q.chain,202,199,other));
 auto older_modern=child;older_modern.extensions.clear();older_modern.extensions.insert(block_header_extensions(qc));older_modern.extensions.insert(block_header_extensions(older_tc2));older_modern.sign(producer_key);
 REQUIRE(admitted(older_modern)==202); REQUIRE(check(older_modern));
 q.hf18=false; REQUIRE(!check(modern)); REQUIRE(admitted(modern)==0);
 // Two actual signed headers and independently validated QCs on a simulated branch.
 database f; f.chain=q.chain; f.schedule=q.schedule; f.vals=q.vals;
 auto make_qc=[&](const signed_block& b){
  protocol::block_qc_certificate cert;cert.block_num=b.block_num();cert.block_id=b.id();cert.round=f.round_of(b.timestamp);
  for(int i=0;i<3;++i){const account_name_type name(std::string("val")+std::to_string(i));
   auto key=fc::ecc::private_key::regenerate(fc::sha256::hash(std::string("key")+std::to_string(i)));
   cert.signatures[name]=key.sign_compact(protocol::block_vote_digest(f.chain,cert.block_num,cert.block_id,cert.round));}
  return cert;
 };
 signed_block parent;parent.previous=id;parent.previous._hash[0]=fc::endian_reverse_u32(122);parent.timestamp=fc::time_point_sec(200);parent.validator="val0";parent.sign(producer_key);
 auto pcert=make_qc(parent);f.block=fake_block{parent.id(),parent.timestamp};REQUIRE(f.accept_block_qc_certificate(pcert));
 f.branch_ids[123]=parent.id();f.branch_blocks[parent.id()]=parent;
 REQUIRE(f.highest_qc_on_main_branch(124)==0); // one QC alone is insufficient
 auto attach_child=[&](bool carry,uint32_t round){
  signed_block b;b.previous=parent.id();b.timestamp=fc::time_point_sec(round);b.validator="val0";
  if(carry)b.extensions.insert(block_header_extensions(pcert));b.sign(producer_key);
  auto ccert=make_qc(b);f._block_qcs.erase(124);f.block=fake_block{b.id(),b.timestamp};REQUIRE(f.accept_block_qc_certificate(ccert));
  f.branch_ids[124]=b.id();f.branch_blocks[b.id()]=b;
 };
 attach_child(true,201);REQUIRE(f.highest_qc_on_main_branch(124)==123);
 REQUIRE(f.highest_qc_on_main_branch(123)==0);
 f.branch_ids[123]=other;REQUIRE(f.highest_qc_on_main_branch(124)==0);f.branch_ids[123]=parent.id();
 attach_child(false,201);REQUIRE(f.highest_qc_on_main_branch(124)==0);
 attach_child(true,202);REQUIRE(f.highest_qc_on_main_branch(124)==0);
 std::cout << "PASS: real QC/TC signature and admission rules and verified two-QC finality on a simulated branch\n";
}
