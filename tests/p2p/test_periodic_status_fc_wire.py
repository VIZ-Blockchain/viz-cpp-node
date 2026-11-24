#!/usr/bin/env python3
"""Exercise status send and shutdown with real FC fibers and loopback TCP sockets; run builds in Docker.

Requires Release libfc: promise_base layout differs in debug builds.
Uses fake peer state, actual FC loopback sockets, DLT message serialization and extracted real send queue methods.
VIZ_FC_ROOT can point to a separately built checkout for local Docker tests.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from boost_link import boost_libs

ROOT = Path(__file__).resolve().parents[2]
FC_ROOT = Path(os.environ.get('VIZ_FC_ROOT', str(ROOT)))
source = (ROOT / 'libraries/network/dlt_p2p_node.cpp').read_text()
fixture = 'status_fc_wire_harness.cpp'
template = (ROOT / 'tests/p2p/fixtures' / fixture).read_text()

def method(signature):
    start = source.index(signature)
    pos = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]

helpers = source[source.index('inline uint32_t frame_msg_type('):source.index('} // anonymous namespace', source.index('inline uint32_t frame_msg_type('))]
code = template.replace('// @HELPERS@', helpers)
code = code.replace('// @SEND_METHOD@', method('void dlt_p2p_node::send_message(peer_id peer, const message& msg) {'))
code = code.replace('// @DRAIN_METHOD@', method('void dlt_p2p_node::drain_send_queue(peer_id peer, std::vector<char> buf) {'))
code = code.replace('// @BROADCAST_METHOD@', method('void dlt_p2p_node::broadcast_chain_status() {'))
code = code.replace('// @PERIODIC_METHOD@', method('void dlt_p2p_node::periodic_chain_status() {'))
code = code.replace('// @CLOSE_METHOD@', method('void dlt_p2p_node::close() {'))
code = code.replace('// @RECEIVE_METHOD@', method('void dlt_p2p_node::on_dlt_fork_status(peer_id peer, const dlt_fork_status_message& msg) {'))
code = code.replace('// @CACHE_METHOD@', method('void dlt_p2p_node::refresh_peer_lib_cache() {'))
with tempfile.TemporaryDirectory(prefix='viz-status-fc-') as directory:
    tmp = Path(directory)
    cpp = tmp / 'test.cpp'
    cpp.write_text(code)
    executable = tmp / 'test'
    cmd = [os.environ.get('CXX', 'g++'), '-DNDEBUG', '-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable', '-Wno-unused-parameter', '-Wno-deprecated-copy', '-Wno-type-limits',
           '-I' + str(FC_ROOT / 'thirdparty/fc/include'), '-I' + str(ROOT / 'libraries/network/include'), '-I' + str(ROOT / 'libraries/protocol/include'), str(cpp), '-o', str(executable),
           str(FC_ROOT / 'build/thirdparty/fc/libfc.a')]
    cmd += ['-l' + name for name in boost_libs()]
    subprocess.run(cmd, check=True, timeout=120)
    subprocess.run([str(executable)], check=True, timeout=15)
