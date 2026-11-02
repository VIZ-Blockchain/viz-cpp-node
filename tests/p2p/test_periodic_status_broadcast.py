#!/usr/bin/env python3
"""Exercise the actual status-broadcast method with fake peers; run in Docker.

This tests selection, values, timer and snapshot-pause behavior with fake peers,
not sockets, rollout to old nodes, or peer authentication. Requires C++17.
"""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / 'libraries/network/dlt_p2p_node.cpp').read_text()
def method(signature):
    start = source.index(signature)
    pos = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]

broadcast = method('void dlt_p2p_node::broadcast_chain_status() {')
periodic = method('void dlt_p2p_node::periodic_chain_status() {')

prefix = r'''
#include <map>
#include <vector>
#include <cassert>
#include <iostream>
#include <cstdint>
namespace fc {
int64_t wall_us=1000000;
struct duration { int64_t value; int64_t count() const { return value; } };
struct time_point {
    int64_t value=0;
    static time_point now() { return {wall_us}; }
    bool operator!=(const time_point& rhs) const { return value!=rhs.value; }
    bool operator>=(const time_point& rhs) const { return value>=rhs.value; }
    duration operator-(const time_point& rhs) const { return {value-rhs.value}; }
};
}
using peer_id=int;
const int DLT_PEER_LIFECYCLE_ACTIVE=1;
struct status { int fork_status=0,head_block_id=0,head_block_num=0,lib_block_id=0,lib_block_num=0,dlt_earliest_block=0,dlt_latest_block=0,node_status=0; };
using dlt_fork_status_message=status;
struct message { status s; explicit message(status v):s(v){} };
struct state { int lifecycle_state; bool exchange_enabled; };
struct dlt_p2p_node {
    void* _delegate=nullptr; int _fork_status=7;
    bool _block_processing_paused=false;
    fc::time_point _last_chain_status_broadcast;
    static constexpr uint32_t CHAIN_STATUS_BROADCAST_INTERVAL_SEC=30;
    std::map<int,state> _peer_states;
    std::vector<int> delivered; std::vector<status> frames;
    status build_hello_message() { status s; s.head_block_num=101; s.lib_block_num=99; return s; }
    void send_message(int id,message m) {
        if(_peer_states.count(id)) { delivered.push_back(id); frames.push_back(m.s); }
    }
    void send_to_all_our_fork_peers(message m) {
        for(const auto& peer:_peer_states)
            if(peer.second.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE && peer.second.exchange_enabled)
                send_message(peer.first,m);
    }
    void broadcast_chain_status();
    void periodic_chain_status();
};
'''
suffix = r'''
int main() {
    dlt_p2p_node node;
    node._peer_states={{1,{1,true}},{2,{1,false}},{3,{0,true}}};
    node._delegate=&node;
    node.periodic_chain_status();
    assert((node.delivered==std::vector<int>{1,2})); // active aligned and unaligned
    assert(node.frames.size()==2);
    for(const auto& frame:node.frames) {
        assert(frame.head_block_num==101);
        assert(frame.lib_block_num==99);
        assert(frame.fork_status==7);
    }
    node.delivered.clear();
    fc::wall_us=30000000;
    node.periodic_chain_status();
    assert(node.delivered.empty()); // 29 seconds after first
    fc::wall_us=31000000;
    node._block_processing_paused=true;
    node.periodic_chain_status();
    assert(node.delivered.empty()); // snapshot pause
    node._block_processing_paused=false;
    node.periodic_chain_status();
    assert((node.delivered==std::vector<int>{1,2})); // due immediately after resume
    node.delivered.clear();
    fc::wall_us=2000000; // wall clock moved backwards
    node.periodic_chain_status();
    assert((node.delivered==std::vector<int>{1,2}));
    node.delivered.clear();
    node._delegate=nullptr;
    node.broadcast_chain_status();
    assert(node.delivered.empty()); // no delegate
    std::cout << "PASS: active aligned and unaligned peers get current LIB; inactive and null delegate do not\n";
}
'''
with tempfile.TemporaryDirectory(prefix='viz-status-broadcast-') as directory:
    tmp = Path(directory)
    cpp = tmp / 'test.cpp'
    cpp.write_text(prefix + broadcast + '\n' + periodic + suffix)
    executable = tmp / 'test'
    subprocess.run(['g++', '-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
