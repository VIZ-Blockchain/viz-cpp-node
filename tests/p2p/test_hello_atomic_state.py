#!/usr/bin/env python3
"""Exercise the real hello builder with a deliberately advancing fake delegate.

The real delegate method runs against a fake lock and chain state. This is not a
live-node race measurement.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'libraries/network/dlt_p2p_node.cpp').read_text()
template = (root / 'tests/p2p/fixtures/status_snapshot_harness.cpp').read_text()
start = source.index('dlt_hello_message dlt_p2p_node::build_hello_message() const {')
pos = source.index('{', start) + 1
depth = 1
while depth:
    depth += (source[pos] == '{') - (source[pos] == '}')
    pos += 1
plugin = (root / 'plugins/p2p/p2p_plugin.cpp').read_text()
start_delegate = plugin.index('graphene::network::dlt_chain_position get_chain_position() const override {')
end_delegate = plugin.index('\n    }', start_delegate) + len('\n    }')
method = plugin[start_delegate:end_delegate].replace(' get_chain_position() const override {', ' dlt_delegate::get_chain_position() const {', 1)
code = template.replace('// @BUILD_HELLO@', source[start:pos]).replace('// @DELEGATE_POSITION@', method)
with tempfile.TemporaryDirectory(prefix='viz-hello-interleave-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['g++', '-std=c++14', '-Wall', '-Wextra', '-Werror', '-pthread', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True, timeout=45)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
