#!/usr/bin/env python3
"""Wiring of the two rules that let a stalled chain actually recover. Source-level, no node.

Mainnet 02.11.2026 21:46 UTC: head #84957544 got its QC, the 60 s unstick rule produced
ONE block (#84957545, 21:47:54), and then nothing for 31 minutes. The unstick rule itself was
fine -- the participation gate in maybe_validate_block() returns `low_participation` long
before the unstick check is reached, and the 128-slot window is below 33% from ~86 s after
any stall for the next two minutes. So the rule can fire exactly once. The block that came
at 22:19 (an operator's stale-production override) entered emergency, which blanked every
validator key; the chain then stood until 02:00 when the committee key came online.

Then the second half: the committee produced until 03:04:50 and went silent. The hybrid
schedule gives the committee 13 of 21 slots, and update_last_irreversible_block() ranked
those 13 slots as 13 votes, so LIB froze at the committee's last block (#84961428) while 8
real validators kept producing -- head walked toward the max_undo assert (30000 blocks) that
halts every node at once.

Checks:
  1. the participation gate consults the BFT evidence (a QC held for head or its parent, or
     the unstick rule) before refusing, and only after HF16;
  2. the emergency LIB percentile counts each validator once, not once per slot.

Run: python3 tests/chain/test_stall_recovery_wiring.py [--selftest]
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DB_CPP = ROOT / 'libraries' / 'chain' / 'database.cpp'
VAL_CPP = ROOT / 'plugins' / 'validator' / 'validator.cpp'
FILES = {'db_cpp': DB_CPP, 'val_cpp': VAL_CPP}


def _masked(source):
    """Same-length copy with comments and string/char literals blanked out."""
    out = list(source)
    i, n = 0, len(source)
    while i < n:
        c = source[i]
        if c == '/' and i + 1 < n and source[i + 1] == '/':
            while i < n and source[i] != '\n':
                out[i] = ' '
                i += 1
        elif c == '/' and i + 1 < n and source[i + 1] == '*':
            out[i] = out[i + 1] = ' '
            i += 2
            while i + 1 < n and not (source[i] == '*' and source[i + 1] == '/'):
                out[i] = ' '
                i += 1
            if i + 1 < n:
                out[i] = out[i + 1] = ' '
                i += 2
        elif c in ('"', "'"):
            quote = c
            out[i] = ' '
            i += 1
            while i < n and source[i] != quote:
                out[i] = ' '
                if source[i] == '\\' and i + 1 < n:
                    out[i + 1] = ' '
                    i += 1
                i += 1
            if i < n:
                out[i] = ' '
                i += 1
        else:
            i += 1
    return ''.join(out)


def method(source, signature):
    """Body of the function whose definition starts with `signature`, brace matched."""
    masked = _masked(source)
    start = masked.find(signature)
    if start < 0:
        return None
    brace = masked.find('{', start)
    if brace < 0:
        return None
    depth = 0
    for i in range(brace, len(masked)):
        if masked[i] == '{':
            depth += 1
        elif masked[i] == '}':
            depth -= 1
            if depth == 0:
                return source[brace + 1:i]
    return None


def wiring_problems(**files):
    problems = []
    db_cpp = _masked(files['db_cpp'])
    val_cpp = _masked(files['val_cpp'])

    # 1. The participation gate.
    gate = method(val_cpp, 'validator_plugin::impl::maybe_validate_block(')
    if gate is None:
        problems.append('validator_plugin::impl::maybe_validate_block() is missing')
    else:
        g = _masked(gate)
        refusal = re.search(r'if\s*\(\s*prate\s*<\s*_required_validator_participation\s*&&\s*!\s*quorum_proven\s*\)', g)
        if not refusal:
            problems.append('the participation gate refuses on the 128-slot window alone: after a stall the '
                            'window is <33% for two minutes, so the unstick rule can produce exactly one block')
        else:
            # Everything that proves a quorum must be decided BEFORE that refusal.
            before = g[:refusal.start()]
            if not re.search(r'quorum_proven\s*=[^;]*qc_on_branch\(\s*hn\s*\)[^;]*qc_on_branch\(\s*hn\s*-\s*1\s*\)', before):
                problems.append('quorum_proven does not consult a QC held for head or for its parent (late-QC '
                                'production carries the grandparent certificate, so the parent counts too)')
            if not re.search(r'quorum_proven\s*=[^;]*db\.emergency_unstick_block\(\s*now\s*\)', before):
                problems.append('quorum_proven ignores the unstick rule: the first block after 60 s of silence '
                                'is refused by the gate before the unstick check is ever reached')
            if not re.search(r'return\s+qc\s*&&\s*qc->block_id\s*==\s*db\.find_block_id_for_num\(\s*n\s*\)', before):
                problems.append('qc_on_branch does not compare the certificate to OUR block at that height: a QC '
                                'for a sibling would pass as proof that our branch has a quorum')
            if not re.search(r'if\s*\(\s*db\.has_hardfork\(CHAIN_HARDFORK_16\)\s*\)\s*\{\s*db\.with_weak_read_lock', before):
                problems.append('the quorum proof is not gated on HF16 (there are no certificates to read before it) '
                                'or reads the database without a read lock')

    # 2. The emergency LIB percentile.
    lib = method(db_cpp, 'void database::update_last_irreversible_block(')
    if lib is None:
        problems.append('database::update_last_irreversible_block() is missing')
    else:
        body = _masked(lib)
        if not re.search(r'std::find\(\s*wit_objs\.begin\(\)\s*,\s*wit_objs\.end\(\)\s*,\s*w\s*\)\s*==\s*wit_objs\.end\(\)',
                         body):
            problems.append('the LIB percentile ranks one entry per SLOT: the committee holds 13 of 21 in the '
                            'hybrid schedule, so LIB freezes at its last block when its key goes silent')
        if not re.search(r'std::nth_element\(\s*wit_objs\.begin\(\)', body):
            problems.append('the LIB percentile is no longer taken over wit_objs')
    return problems


MUTATIONS = {
    'gate-ignores-quorum-proof': (
        VAL_CPP, 'if (prate < _required_validator_participation && !quorum_proven) {',
        'if (prate < _required_validator_participation) {'),
    'gate-ignores-unstick': (
        VAL_CPP, 'quorum_proven = qc_on_branch(hn) || (hn > 1 && qc_on_branch(hn - 1)) ||\n'
                 '                                                    db.emergency_unstick_block(now);',
        'quorum_proven = qc_on_branch(hn) || (hn > 1 && qc_on_branch(hn - 1));'),
    'gate-ignores-parent-qc': (
        VAL_CPP, 'quorum_proven = qc_on_branch(hn) || (hn > 1 && qc_on_branch(hn - 1)) ||',
        'quorum_proven = qc_on_branch(hn) ||'),
    'gate-accepts-sibling-qc': (
        VAL_CPP, 'return qc && qc->block_id == db.find_block_id_for_num(n);',
        'return qc.valid();'),
    'gate-not-hf16-gated': (
        VAL_CPP, 'if (db.has_hardfork(CHAIN_HARDFORK_16)) {\n'
                 '                                db.with_weak_read_lock([&]() {',
        'if (true) {\n'
        '                                db.with_weak_read_lock([&]() {'),
    'lib-counts-slots': (
        DB_CPP, 'if (std::find(wit_objs.begin(), wit_objs.end(), w) == wit_objs.end())\n'
                '                        wit_objs.push_back(w);',
        'wit_objs.push_back(w);'),
}


def load():
    return {key: path.read_text() for key, path in FILES.items()}


def selftest():
    files = load()
    failures = []
    total = 0
    for name, (path, old, new) in MUTATIONS.items():
        key = next(k for k, p in FILES.items() if p == path)
        if files[key].count(old) != 1:
            failures.append(f'{name}: anchor found {files[key].count(old)} times -- mutation did not land')
            continue
        mutated = dict(files)
        mutated[key] = files[key].replace(old, new, 1)
        problems = wiring_problems(**mutated)
        total += len(problems)
        if not problems:
            failures.append(f'{name}: stayed GREEN (the check it targets is vacuous)')
        else:
            print(f'RED as expected: {name} ({len(problems)})')
    if failures:
        for f in failures:
            print(f'BAD: {f}', file=sys.stderr)
        return 1
    print(f'PASS: {len(MUTATIONS)} mutations, every one red ({total} findings)')
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--selftest', action='store_true')
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    problems = wiring_problems(**load())
    if problems:
        for p in problems:
            print(f'FINDING: {p}')
        return 1
    print('PASS: the participation gate yields to a held QC or the unstick rule, and the emergency '
          'LIB percentile counts each validator once')
    return 0


if __name__ == '__main__':
    sys.exit(main())
