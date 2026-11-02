#!/usr/bin/env python3
"""Exercise the real hello builder with a deliberately advancing fake delegate.

This is a controlled interleaving, not a live-node race measurement. Run in Docker.
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
code = template.replace('// @BUILD_HELLO@', source[start:pos])
with tempfile.TemporaryDirectory(prefix='viz-hello-interleave-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(code)
    subprocess.run(['g++', '-std=c++14', '-Wall', '-Wextra', '-Werror', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True, timeout=45)
    subprocess.run([str(path / 'test')], check=True, timeout=10)
