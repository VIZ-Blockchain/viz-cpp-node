#!/usr/bin/env python3
"""Exercise the production range handler with a reversible applied suffix.

This is an offline handler test, not a replacement for a live P2P acceptance.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_corruption_recovery import method

ROOT = Path(__file__).resolve().parents[2]
PATH = 'libraries/network/dlt_p2p_node.cpp'
source = subprocess.check_output(['git', 'show', 'origin/master:' + PATH], cwd=ROOT, text=True) if '--baseline' in sys.argv else (ROOT / PATH).read_text()
handler = method(source, 'void dlt_p2p_node::on_dlt_get_block_range(')
fixture = r'''
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <utility>
#include <vector>
#define ilog(...) ((void)0)
#define wlog(...) ((void)0)
#define DLT_LOG_DGRAY ""
#define DLT_LOG_RESET ""
using peer_id = int;
struct block { uint32_t n; int key; int previous; uint32_t block_num() const {return n;} int id() const {return key;} };
struct opt_block : std::optional<block> { using std::optional<block>::optional; bool valid() const {return has_value();} };
struct dlt_get_block_range_message { uint32_t start_block_num, end_block_num; };
struct dlt_block_range_reply_message { std::vector<block> blocks; uint32_t last_block_next_available=0; bool is_last=false; };
namespace fc { namespace raw { size_t pack_size(const block&) {return 100;} size_t pack_size(const dlt_block_range_reply_message& r) {return 16+r.blocks.size()*100;} } }
struct delegate { std::map<uint32_t,block> blocks; uint32_t head=62, lib=39;
 uint32_t get_dlt_latest_block() const {return lib;} uint32_t get_head_block_num() const {return head;}
 opt_block read_block_by_num(uint32_t n) {auto it=blocks.find(n); return it==blocks.end()?opt_block{}:opt_block{it->second};} };
dlt_block_range_reply_message message(dlt_block_range_reply_message r) {return r;}
struct dlt_p2p_node { static constexpr uint32_t MAX_MESSAGE_SIZE=128*1024; delegate* _delegate; dlt_block_range_reply_message sent; int sends=0;
 explicit dlt_p2p_node(delegate& d):_delegate(&d){} void send_message(peer_id,dlt_block_range_reply_message r){sent=std::move(r);++sends;} void record_packet_result(peer_id,bool){}
 void on_dlt_get_block_range(peer_id,const dlt_get_block_range_message&); };
HANDLER
int main(){
 delegate d; for(uint32_t n=39;n<=62;++n)d.blocks[n]={n,int(n),int(n-1)}; dlt_p2p_node p(d);
 p.on_dlt_get_block_range(1,{56,62});
 if(p.sent.blocks.size()!=7 || p.sent.blocks.front().id()!=56 || p.sent.blocks.back().id()!=62 || !p.sent.is_last) return 1;
 p.on_dlt_get_block_range(1,{56,58});
 if(p.sent.blocks.size()!=3 || p.sent.last_block_next_available!=59 || p.sent.is_last) return 2;
 d.blocks[58].previous=999; p.on_dlt_get_block_range(1,{56,62});
 if(p.sent.blocks.size()!=2 || p.sent.blocks.back().id()!=57 || p.sent.last_block_next_available!=58 || p.sent.is_last) return 3;
 std::cout<<"PASS: reversible suffix served, paging retained, reorg returns linked prefix\n";
}
'''.replace('HANDLER', handler)
with tempfile.TemporaryDirectory(prefix='viz-reversible-range-') as tmp:
    p=Path(tmp); (p/'test.cpp').write_text(fixture)
    subprocess.run(['g++','-std=c++17','-O0','-Wall','-Wextra',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    sys.exit(subprocess.run([str(p/'test')]).returncode)
