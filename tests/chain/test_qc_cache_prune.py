#!/usr/bin/env python3
"""Regression guard for the QC-cache prune that keeps DLT replay linear.

Replay applies blocks without live votes, so push_block_vote() never trims
_block_qcs. Without a prune the cache grows by one entry per replayed block and
highest_qc_on_main_branch() walks all of it for every block: an 80 000-block
replay took 198.7 s instead of 4.5 s (issue #13846, perf: 91% CPU there).
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'libraries/chain/database.cpp').read_text()
header = (root / 'libraries/chain/include/graphene/chain/database.hpp').read_text()

# 1. The prune is declared and wired into LIB advancement, after commit().
assert 'void prune_block_qc_cache(uint32_t lib);' in header, \
    'prune_block_qc_cache is not declared in database.hpp'
lib_block = source[source.index('void database::update_last_irreversible_block'):]
call = 'prune_block_qc_cache(dpo.last_irreversible_block_num);'
assert call in lib_block, 'update_last_irreversible_block does not prune the QC cache'
before_call = lib_block[:lib_block.index(call)]
assert before_call.rindex('commit(dpo.last_irreversible_block_num)') < len(before_call), \
    'the cache must be pruned after LIB is committed'

# 2. highest_qc_on_main_branch must not copy the whole cache up front.
start = source.index('uint32_t database::highest_qc_on_main_branch')
body = source[start:source.index('\n        }', start)]
assert '_block_qcs.rbegin()' not in body, \
    'full reverse copy of _block_qcs is back (O(cache) per applied block)'

# 3. Behaviour of the prune itself, executed from the real source text.
pstart = source.index('void database::prune_block_qc_cache')
pend = source.index('\n        }', pstart)
prune_body = source[source.index('{', pstart) + 1:pend]
fixture = r'''
#include <cassert>
#include <cstdint>
#include <map>
#include <mutex>
struct db {
    std::map<unsigned, unsigned> _block_qcs;
    std::mutex _block_votes_mutex;
    void prune_block_qc_cache(uint32_t lib) {
// @BODY@
    }
};
int main() {
    db d;
    for (unsigned i = 1; i <= 200; ++i) d._block_qcs.emplace(i, i);
    d.prune_block_qc_cache(150);              // keep 64 below LIB: 87..200
    assert(d._block_qcs.begin()->first == 87);
    assert(d._block_qcs.size() == 114);
    d.prune_block_qc_cache(150);              // idempotent
    assert(d._block_qcs.begin()->first == 87);
    d.prune_block_qc_cache(0);                // nothing below 0
    assert(d._block_qcs.begin()->first == 87);
    d.prune_block_qc_cache(300);              // everything is old now
    assert(d._block_qcs.empty());
    return 0;
}
'''
fixture = fixture.replace('// @BODY@', prune_body)
with tempfile.TemporaryDirectory() as td:
    src = Path(td) / 'prune_fixture.cpp'
    exe = Path(td) / 'prune_fixture'
    src.write_text(fixture)
    subprocess.run(['g++', '-std=c++17', '-O1', '-o', str(exe), str(src)], check=True)
    subprocess.run([str(exe)], check=True)
print('ok: QC-cache prune wiring and behaviour')
