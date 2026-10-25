#!/usr/bin/env python3
"""Offline extracted-body regression; NOT a consensus/crypto/serialization test.

Compiles the production evidence-selection loop with explicit fake validator and
signature state. Also checks that the production transaction loop's initial
budget includes the assembled header. Run the native hf16_production_safety_suite tests
for real database, signatures and FC serialization coverage.
"""
import pathlib
import subprocess
import tempfile
import unittest
import os

SOURCE = pathlib.Path(os.environ.get('PRODUCTION_SOURCE',
    str(pathlib.Path(__file__).resolve().parents[2] / 'libraries/chain/database.cpp')))
TEXT = SOURCE.read_text()
GEN = TEXT[TEXT.index('signed_block database::_generate_block('):TEXT.index('void database::pop_block()')]

class ProductionSafety(unittest.TestCase):
    def test_evidence_selection_actual_body(self):
        begin = GEN.index('for (auto it = _pending_equivocations.begin();')
        end = GEN.index('size_t n = 0;', begin)
        loop = GEN[begin:end]
        cpp = r'''
#include <map>
#include <string>
#include <cstdint>
#include <iostream>
using account_name_type = std::string;
struct Evidence { uint32_t block_num; int block_id; int signature; };
namespace protocol { struct block_equivocation_proof {
  account_name_type validator; uint32_t round; Evidence first, second;
}; }
struct Validator { uint32_t last_equivocation_round = 0; int key = 7; };
std::map<account_name_type, Validator> validators;
const Validator* find_validator(const account_name_type& n) {
  auto i=validators.find(n); return i==validators.end()?nullptr:&i->second;
}
uint32_t block_interval() { return 1; }
bool verify_block_vote(uint32_t, int, uint32_t, const account_name_type& n, int signature) {
  auto v=find_validator(n); return v && signature==v->key;
}
int main() {
  int failures=0;
  for (int scenario=0; scenario<11; ++scenario) {
    validators.clear(); validators["alice"] = Validator{};
    uint32_t own_round=100;
    protocol::block_equivocation_proof proof{"alice",99,{10,1,7},{10,2,7}};
    bool expected=true, retained=true;
    if(scenario==1) { proof.round=100; expected=false; }
    if(scenario==2) { proof.round=101; expected=false; }
    if(scenario==3) { validators["alice"].key=8; expected=false; retained=false; }
    if(scenario==4) { proof.second.signature=8; expected=false; retained=false; }
    if(scenario==5) { validators["alice"].last_equivocation_round=99; expected=false; retained=false; }
    if(scenario==6) { validators.clear(); expected=false; retained=false; }
    if(scenario==7) { own_round=3700; proof.round=100; }
    if(scenario==8) { own_round=3701; proof.round=100; expected=false; retained=false; }
    if(scenario==9) { proof.first.signature=8; expected=false; retained=false; }
    std::map<std::pair<account_name_type,uint32_t>,protocol::block_equivocation_proof> _pending_equivocations;
    _pending_equivocations.emplace(std::make_pair(proof.validator,proof.round),proof);
    if(scenario==10) {
      auto future=proof; future.round=101;
      _pending_equivocations.emplace(std::make_pair(future.validator,future.round),future);
    }
    std::map<account_name_type,const protocol::block_equivocation_proof*> pick;
''' + loop + r'''
    if(scenario==10 && (!pick.count("alice") || pick["alice"]->round!=99 || _pending_equivocations.size()!=2)) return 2;
    if((!pick.empty())!=expected || (!_pending_equivocations.empty())!=retained) {
      std::cerr<<"selection case "<<scenario<<" failed\n"; ++failures;
    }
  }
  return failures?1:0;
}
'''
        root = os.environ.get('TMPDIR', str(pathlib.Path.home()/'.hermes/cache/scratch'))
        with tempfile.TemporaryDirectory(prefix='production-safety-', dir=root) as tmp:
            src = pathlib.Path(tmp)/'selection.cpp'
            exe = pathlib.Path(tmp)/'selection'
            src.write_text(cpp)
            subprocess.run(['g++','-std=c++14','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_transaction_budget_actual_body(self):
        begin = GEN.index('for (const signed_transaction &tx : _pending_tx)')
        end = GEN.index('if (postponed_tx_count > 0)', begin)
        loop = GEN[begin:end]
        if 'total_block_size = fc::raw::pack_size(pending_block)' in GEN:
            budget_begin = GEN.index('total_block_size = fc::raw::pack_size(pending_block)')
            budget_end = GEN.index('\n                }', budget_begin)
            budget = GEN[budget_begin:budget_end]
        else:
            budget = ''  # baseline: the real loop starts with its empty-header estimate
        cpp = r'''
#include <vector>
#include <cstdint>
#include <algorithm>
#include <stdexcept>
#include <iostream>
using std::size_t;
struct signed_transaction { uint64_t expiration=200; size_t bytes=200; };
struct Block { size_t header=400; std::vector<signed_transaction> transactions; };
namespace fc {
struct exception : std::exception { const char* to_detail_string() const {return "mock";} };
namespace raw {
size_t pack_size(const signed_transaction& t) { return t.bytes; }
size_t pack_size(const Block& b) {
  size_t size=b.header+1; // vector count: deliberately exercise the 127/128 boundary
  if(b.transactions.size()>=128) ++size;
  for(auto t:b.transactions) size+=t.bytes;
  return size;
}
}}
struct shared_memory_corruption_exception : fc::exception {};
struct Session { void squash() {} };
Session start_undo_session() { return {}; }
void _apply_transaction(const signed_transaction&,uint32_t) {}
#define wlog(...) ((void)0)
constexpr uint32_t CHAIN_BLOCK_SIZE=1000;
constexpr uint32_t CHAIN_BLOCK_GENERATION_POSTPONED_TX_LIMIT=10000;
int main() {
 for(int scenario=0; scenario<3; ++scenario) {
  Block pending_block;
  std::vector<signed_transaction> _pending_tx(5);
  uint32_t maximum_block_size=scenario==1?2000:1000;
  struct Props { uint32_t maximum_block_size; };
  auto get_dynamic_global_properties = [&]() { return Props{maximum_block_size}; };
  (void)get_dynamic_global_properties;
  if(scenario==2) { pending_block.header=600; _pending_tx.assign(400, signed_transaction{200,1}); }
  size_t total_block_size=104;
  uint64_t postponed_tx_count=0, when=100; uint32_t skip=0;
''' + budget + loop + r'''
  if(fc::raw::pack_size(pending_block)>std::min(maximum_block_size,CHAIN_BLOCK_SIZE)) return 1;
  if(scenario<2 && pending_block.transactions.size()!=2) return 2;
  if(scenario==2 && pending_block.transactions.size()<128) return 3;
 }
}
'''
        root = os.environ.get('TMPDIR', str(pathlib.Path.home()/'.hermes/cache/scratch'))
        with tempfile.TemporaryDirectory(prefix='production-budget-', dir=root) as tmp:
            src = pathlib.Path(tmp)/'budget.cpp'
            exe = pathlib.Path(tmp)/'budget'
            src.write_text(cpp)
            subprocess.run(['g++','-std=c++14','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)

    def test_full_header_budget_precedes_transaction_packing(self):
        packing = GEN.index('for (const signed_transaction &tx : _pending_tx)')
        header = GEN.index('pending_block.extensions.insert(block_header_extensions(*cert))')
        self.assertLess(header, packing, 'QC/TC/proofs assembled after transaction packing')
        prefix = GEN[:packing]
        self.assertIn('fc::raw::pack_size(pending_block)', prefix)
        self.assertIn('has_hardfork(CHAIN_HARDFORK_16)', prefix)
        self.assertIn('std::min', prefix)

if __name__ == '__main__':
    unittest.main()
