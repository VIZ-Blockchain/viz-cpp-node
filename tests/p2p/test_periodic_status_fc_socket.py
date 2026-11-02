#!/usr/bin/env python3
"""Exercise status send and shutdown with real FC fibers and loopback TCP sockets; run builds in Docker.

Requires Release libfc: promise_base layout differs in debug builds.
Uses fake peers and actual FC loopback sockets; message serialization is stubbed. Requires a previously built libfc.a and Boost.
VIZ_FC_ROOT can point to a separately built checkout for local Docker tests.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FC_ROOT = Path(os.environ.get('VIZ_FC_ROOT', str(ROOT)))
source = (ROOT / 'libraries/network/dlt_p2p_node.cpp').read_text()
fixture = 'status_fc_socket_control.cpp' if os.environ.get('VIZ_FC_SOCKET_CONTROL') == '1' else 'status_fc_socket_harness.cpp'
template = (ROOT / 'tests/p2p/fixtures' / fixture).read_text()

def method(signature):
    start = source.index(signature)
    pos = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]

code = template.replace('// @BROADCAST_METHOD@', method('void dlt_p2p_node::broadcast_chain_status() {'))
code = code.replace('// @CLOSE_METHOD@', method('void dlt_p2p_node::close() {'))
with tempfile.TemporaryDirectory(prefix='viz-status-fc-') as directory:
    tmp = Path(directory)
    cpp = tmp / 'test.cpp'
    cpp.write_text(code)
    executable = tmp / 'test'
    cmd = [os.environ.get('CXX', 'g++'), '-DNDEBUG', '-std=c++17', '-O0', '-Wall', '-Wextra', '-Werror',
           '-I' + str(FC_ROOT / 'thirdparty/fc/include'), str(cpp), '-o', str(executable),
           str(FC_ROOT / 'build/thirdparty/fc/libfc.a')]
    cmd += ['-l' + name for name in ('boost_context', 'boost_coroutine', 'boost_thread',
            'boost_chrono', 'boost_system', 'boost_filesystem', 'boost_date_time', 'boost_atomic',
            'ssl', 'crypto', 'z', 'pthread', 'dl', 'rt')]
    subprocess.run(cmd, check=True, timeout=120)
    subprocess.run([str(executable)], check=True, timeout=15)
