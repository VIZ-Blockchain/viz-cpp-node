#!/usr/bin/env python3
"""Regression guard for the QC-cache prune that keeps DLT replay linear.

Replay applies blocks without live votes, so push_block_vote() never trims
_block_qcs. Without a prune the cache grows by one entry per replayed block and
highest_qc_on_main_branch() walks all of it for every block: an 80 000-block
replay took 198.7 s instead of 4.5 s (issue #13846, perf: 91% CPU there).

Plain run checks the tree in this repository. --selftest feeds mutated copies of
the same checks and requires every mutation to be reported.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CALL = 'prune_block_qc_cache(dpo.last_irreversible_block_num);'
DECL = 'void prune_block_qc_cache(uint32_t lib);'


def wiring_problems(source, header):
    problems = []
    if DECL not in header:
        problems.append('prune_block_qc_cache is not declared in database.hpp')
    lib_block = source[source.index('void database::update_last_irreversible_block'):]
    if CALL not in lib_block:
        problems.append('update_last_irreversible_block does not prune the QC cache')
    else:
        before = lib_block[:lib_block.index(CALL)]
        if 'commit(dpo.last_irreversible_block_num)' not in before:
            problems.append('the cache must be pruned after LIB is committed')
    start = source.index('uint32_t database::highest_qc_on_main_branch')
    body = source[start:source.index('\n        }', start)]
    if '_block_qcs.rbegin()' in body:
        problems.append('full reverse copy of _block_qcs is back (O(cache) per applied block)')
    return problems


def prune_body(source):
    pstart = source.index('void database::prune_block_qc_cache')
    pend = source.index('\n        }', pstart)
    return source[source.index('{', pstart) + 1:pend]


def behaviour_problems(body):
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
'''.replace('// @BODY@', body)
    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / 'prune_fixture.cpp'
        exe = Path(td) / 'prune_fixture'
        src.write_text(fixture)
        try:
            subprocess.run(['g++', '-std=c++17', '-O1', '-o', str(exe), str(src)],
                           check=True, capture_output=True)
            subprocess.run([str(exe)], check=True, capture_output=True)
        except subprocess.CalledProcessError as exc:
            detail = (exc.stderr or b'').decode(errors='replace').strip().splitlines()
            return ['prune window/behaviour fixture failed: ' +
                    (detail[-1] if detail else 'exit %d' % exc.returncode)]
    return []


def check(source, header):
    problems = wiring_problems(source, header)
    if not problems:
        problems = behaviour_problems(prune_body(source))
    return problems


def selftest():
    source = (ROOT / 'libraries/chain/database.cpp').read_text()
    header = (ROOT / 'libraries/chain/include/graphene/chain/database.hpp').read_text()
    arms = {
        'baseline is clean': ([source, header], False),
        'prune call removed': ([source.replace('                ' + CALL + '\n', '', 1), header], True),
        'declaration removed': ([source, header.replace(DECL, '', 1)], True),
        'prune window widened to 0': ([source.replace('KEEP_BELOW_LIB = 64', 'KEEP_BELOW_LIB = 0', 1), header], True),
        'full reverse copy restored': ([source.replace(
            'uint32_t database::highest_qc_on_main_branch(uint32_t max_num) const {',
            'uint32_t database::highest_qc_on_main_branch(uint32_t max_num) const {\n'
            '            for (auto it = _block_qcs.rbegin(); it != _block_qcs.rend(); ++it) { (void)it; }', 1), header], True),
    }
    bad = 0
    for name, ((src, hdr), expect_red) in arms.items():
        try:
            red = bool(check(src, hdr))
        except Exception:
            red = True
        if red != expect_red:
            print('SELFTEST FAIL: %s -> %s (expected %s)' %
                  (name, 'red' if red else 'green', 'red' if expect_red else 'green'))
            bad += 1
    if bad:
        raise SystemExit(1)
    print('selftest: %d arms pass' % len(arms))


def main():
    if '--selftest' in sys.argv[1:]:
        selftest()
        return
    problems = check((ROOT / 'libraries/chain/database.cpp').read_text(),
                     (ROOT / 'libraries/chain/include/graphene/chain/database.hpp').read_text())
    if problems:
        for p in problems:
            print('FAIL:', p)
        raise SystemExit(1)
    print('ok: QC-cache prune wiring and behaviour')


if __name__ == '__main__':
    main()
