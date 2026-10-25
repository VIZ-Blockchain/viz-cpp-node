#!/usr/bin/env python3
"""Compile the real delegate read and node scale/predicate against a guarded DB fixture.
No live DB access. Run inside Docker; CXX may select the compiler.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
from test_corruption_recovery import method
ROOT = Path(__file__).resolve().parents[2]
plugin = (ROOT/'plugins/p2p/p2p_plugin.cpp').read_text()
node = (ROOT/'libraries/network/dlt_p2p_node.cpp').read_text()
header = (ROOT/'libraries/network/include/graphene/network/dlt_p2p_node.hpp').read_text()
read = method(plugin, 'uint32_t block_interval() const override')
scale = method(node, 'uint32_t dlt_p2p_node::slot_scale() const').replace('dlt_p2p_node::', '')
predicate = method(header, 'static bool is_wedged(')
cache = re.search(r'mutable std::atomic<uint32_t> _cached_block_interval\{CHAIN_BLOCK_INTERVAL\};', plugin)
assert cache, 'delegate cache must be atomic'
cpp = r'''
#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <cassert>
constexpr uint32_t CHAIN_BLOCK_INTERVAL=3;
struct DB {
    bool available=true, guarded=false; uint32_t interval=3;
    uint32_t block_interval() const { assert(guarded); return interval; }
    template<class F> auto with_weak_read_lock(F f) -> decltype(f()) {
        if(!available) throw std::runtime_error("DB unavailable");
        guarded=true; auto value=f(); guarded=false; return value;
    }
};
struct Chain { DB value; DB& db() {return value;} };
struct Base { virtual uint32_t block_interval() const=0; virtual ~Base()=default; };
struct Delegate:Base {
    Chain& chain;
    explicit Delegate(Chain& c):chain(c){}
''' + cache.group() + '\n' + read + r'''
};
struct Node {
    Base* _delegate=nullptr;
    static constexpr uint32_t WEDGE_BEHIND_THRESHOLD=200, WEDGE_CONFIRM_SEC=900;
''' + scale + '\n' + predicate + r'''
};
int main() {
    Chain chain; Delegate d(chain); Node node; assert(node.slot_scale()==1);
    node._delegate=&d;
    chain.value.available=false; assert(d.block_interval()==3); // lazy safe default
    chain.value.available=true; assert(d.block_interval()==3); assert(node.slot_scale()==1);
    chain.value.interval=1; assert(d.block_interval()==1); assert(node.slot_scale()==3);
    chain.value.available=false; assert(d.block_interval()==1); assert(node.slot_scale()==3);
    chain.value.available=true; chain.value.interval=3; assert(node.slot_scale()==1); // restored pre-HF state
    chain.value.interval=0; assert(d.block_interval()==3); // invalid value cannot poison cache
    chain.value.interval=1;
    const auto s=node.slot_scale();
    assert(!(6>2*s)); assert(7>2*s);
    assert(!(125>=42*s)); assert(126>=42*s);
    assert(!Node::is_wedged(200*s,false,true,900,200*s));
    assert(Node::is_wedged(200*s+1,false,true,900,200*s));
    assert(!Node::is_wedged(200*s+1,false,true,899,200*s));
}
'''
with tempfile.TemporaryDirectory(prefix='viz-slot-') as tmp:
    source=Path(tmp)/'test.cpp'; binary=Path(tmp)/'test'; source.write_text(cpp)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++14','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
print('PASS: guarded delegate, 3->1->3, unavailable DB fallback, invalid interval, all scaled boundaries')
