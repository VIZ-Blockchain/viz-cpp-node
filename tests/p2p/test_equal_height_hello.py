#!/usr/bin/env python3
"""Execute the real hello-reply handler in a small P2P fixture, no network."""
from pathlib import Path
import subprocess
import sys
import tempfile
from test_corruption_recovery import method
root = Path(__file__).resolve().parents[2]
path = 'libraries/network/dlt_p2p_node.cpp'
source = (subprocess.check_output(['git', 'show', '209e65ef:' + path], cwd=root, text=True)
          if '--baseline' in sys.argv else (root / path).read_text())
handler = method(source, 'void dlt_p2p_node::on_dlt_hello_reply(')
fixture = r'''
#include <cstdint>
#include <map>
#include <iostream>
#define ilog(...) ((void)0)
#define DLT_LOG_ORANGE ""
#define DLT_LOG_RESET ""
using peer_id=int; using block_id_type=int;
constexpr int DLT_PEER_LIFECYCLE_HANDSHAKING=0, DLT_PEER_LIFECYCLE_SYNCING=1, DLT_PEER_LIFECYCLE_ACTIVE=2;
constexpr int DLT_NODE_STATUS_SYNC=0, DLT_NODE_STATUS_FORWARD=1;
constexpr uint32_t FORWARD_FALLBEHIND_THRESHOLD=3;
namespace fc { struct time_point {static time_point now(){return {};}}; }
struct dlt_hello_reply_message {bool exchange_enabled=true, fork_alignment=true; int initiator_head_seen=0,initiator_lib_seen=0;};
struct dlt_get_block_range_message {uint32_t start_block_num=0,end_block_num=0; int prev_block_id=0;};
struct state {bool exchange_enabled=true,fork_alignment=true; int recognized_head=0,recognized_lib=0,lifecycle_state=2; fc::time_point state_entered_time; uint32_t peer_head_num=62,peer_lib_num=39; int peer_head_id=2,peer_lib_id=39,endpoint=1;};
struct delegate {uint32_t get_head_block_num(){return 62;} uint32_t get_lib_block_num(){return 39;} int get_head_block_id(){return 1;} int get_lib_block_id(){return 39;}};
dlt_get_block_range_message message(dlt_get_block_range_message x){return x;}
struct dlt_p2p_node {std::map<int,state> _peer_states; delegate d,*_delegate=&d; int _node_status=1, sent=0,syncs=0; dlt_get_block_range_message req;
 void on_dlt_hello_reply(peer_id,const dlt_hello_reply_message&);
 void record_packet_result(peer_id,bool){} int slot_scale(){return 1;} void transition_to_sync(){++syncs;} void request_blocks_from_peer(peer_id){} void send_message(peer_id,dlt_get_block_range_message x){++sent;req=x;} };
HANDLER
int main(){ dlt_p2p_node n; n._peer_states[1]=state{}; dlt_hello_reply_message r;
 n.on_dlt_hello_reply(1,r); if(n.sent!=1||n.req.start_block_num!=62||n.req.end_block_num!=62||n.req.prev_block_id!=1||n.syncs) return 1;
 n.sent=0;n._peer_states[1].peer_head_id=1;n.on_dlt_hello_reply(1,r);if(n.sent) return 2;
 n._peer_states[1].peer_head_id=2;n._peer_states[1].peer_lib_id=99;n.on_dlt_hello_reply(1,r);if(n.sent) return 3;
 n._peer_states[1].peer_lib_id=39;n._peer_states[1].peer_head_num=70;n.on_dlt_hello_reply(1,r);if(n.sent||n.syncs!=1) return 4;
 n._peer_states[1].peer_head_num=62;n._peer_states[1].peer_lib_num=38;n.on_dlt_hello_reply(1,r);if(n.sent) return 5;
 n._peer_states[1].peer_lib_num=39;n._peer_states[1].peer_lib_id=0;n.on_dlt_hello_reply(1,r);if(n.sent) return 6;
 n._peer_states[1].peer_lib_id=39;n._peer_states[1].exchange_enabled=false;r.exchange_enabled=false;n.on_dlt_hello_reply(1,r);if(n.sent) return 7;
 std::cout<<"PASS: same-height rival requested once, common LIB required, ordinary catch-up retained\n";
}
'''.replace('HANDLER', handler)
with tempfile.TemporaryDirectory(prefix='viz-equal-hello-') as tmp:
    path=Path(tmp); (path/'test.cpp').write_text(fixture)
    subprocess.run(['g++','-std=c++17','-O0','-Wall','-Wextra',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    sys.exit(subprocess.run([str(path/'test')]).returncode)
