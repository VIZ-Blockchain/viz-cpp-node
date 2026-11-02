#!/usr/bin/env python3
"""Run real vote-verification methods and protocol digests with actual secp256k1.

Requires a Release libfc matching the supplied headers, and secp256k1 archive.
Uses actual QC admission and vote verification; database, schedule and chain blocks are simulated.
Run compilation in Docker.
"""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FC_ROOT = Path(os.environ.get('VIZ_FC_ROOT', str(ROOT)))
source = (ROOT / 'libraries/chain/database.cpp').read_text()
template = (ROOT / 'tests/chain/fixtures/qc_tc_signature_harness.cpp').read_text()

def method(signature):
    start = source.index(signature)
    pos = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]

names = ('bool database::verify_block_vote(', 'const validator_object *database::scheduled_timeout_voter(',
         'bool database::verify_timeout_vote(', 'bool database::verify_timeout_vote_v2(',
         'bool database::accept_block_qc_certificate(', 'void database::validate_block_qc(')
code = template.replace('// @METHODS@', '\n'.join(method(name) for name in names))
block_source = (ROOT / 'libraries/protocol/block.cpp').read_text()
start = block_source.index('uint32_t block_header::num_from_id('); end = block_source.index('}', start) + 1
code = code.replace('// @BLOCK_NUM_METHOD@', block_source[start:end])
secp = FC_ROOT / 'build/thirdparty/fc/vendor/secp256k1-zkp/src/project_secp256k1-build/.libs/libsecp256k1.a'
with tempfile.TemporaryDirectory(prefix='viz-qctc-') as directory:
    cpp = Path(directory) / 'test.cpp'; cpp.write_text(code)
    executable = Path(directory) / 'test'
    cmd = [os.environ.get('CXX', 'g++'), '-DNDEBUG', '-std=c++17', '-O0', '-w',
           '-I' + str(FC_ROOT / 'thirdparty/fc/include'), '-I' + str(ROOT / 'libraries/protocol/include'), '-I' + str(ROOT / 'libraries/chain/include'),
           str(cpp), str(ROOT / 'libraries/protocol/types.cpp'), '-o', str(executable),
           str(FC_ROOT / 'build/thirdparty/fc/libfc.a'), str(secp)]
    cmd += ['-l' + x for x in ('boost_context','boost_coroutine','boost_thread','boost_chrono','boost_system',
                               'boost_filesystem','boost_date_time','boost_atomic','ssl','crypto','z','pthread','dl','rt')]
    subprocess.run(cmd, check=True, timeout=120)
    subprocess.run([str(executable)], check=True, timeout=20)
