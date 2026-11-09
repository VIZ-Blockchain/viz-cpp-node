#!/usr/bin/env python3
"""A refused TC skip must name its cause, not just its verdict. Source-level, no node.

Mainnet 01.11.2026 19:21:27Z: head #84865873 had no QC, the node refused the TC skip, and
the only line it left was `TC skip of QC-less head ... refused ...: no usable TC between
head's QC round and our round`. Behind that line a TC for round 597934973 DID exist with
13 votes, but three of them claimed a high QC for another block at the parent's round
(validators on a divergent tip), so only 12 of the 13 needed survived the v2 filter.
Finding that out took a manual reading of `get_timeout_cert_between_v2`; the log itself
could not say whether the votes were missing, carried a higher high-QC round, or named
another block.

Checks:
  1. `diagnose_timeout_cert_window_v2()` mirrors the search's three filters, builds nothing (a
     diagnosis must not change what the node accepts) and is BOUNDED: every counted vote costs a
     signature verification, and an uncapped walk of the 01.11 window (22 716 rounds) would hold
     the vote mutex for seconds on the production path;
  2. the HF18 refusal path calls it once per head and logs the counts (scanned/window,
     usable/quorum/round and the dropped buckets), so the next refusal answers "why" on its own;
  3. the once-per-head dedup is keyed by a stable reason, not by the counts that change as votes
     arrive (otherwise the line repeats every slot).

Run: python3 tests/chain/test_tc_skip_diagnosis_wiring.py [--selftest]
"""
import argparse
import re
import sys
from pathlib import Path

from test_stall_recovery_wiring import _masked, method

ROOT = Path(__file__).resolve().parents[2]
VAL_CPP = ROOT / 'plugins' / 'validator' / 'validator.cpp'
DB_CPP = ROOT / 'libraries' / 'chain' / 'database.cpp'

DIAG = 'timeout_cert_window_diagnosis database::diagnose_timeout_cert_window_v2('
KEY = 'no usable TC between head\'s QC round and our round'


def wiring_problems(val_cpp, db_cpp):
    problems = []

    diag_raw = method(db_cpp, DIAG)
    if diag_raw is None:
        problems.append('database::diagnose_timeout_cert_window_v2() is missing')
    else:
        diag = _masked(diag_raw)
        for pat, why in [
            (r'_block_votes_mutex', 'does not read the vote store under the same lock as the search'),
            (r'entry\.high_qc_round\s*>\s*max_high_qc_round', 'does not separate votes that claimed a higher QC round'),
            (r'entry\.high_qc_round\s*==\s*max_high_qc_round\s*&&\s*entry\.high_qc_block_id\s*!=\s*max_high_qc_block_id',
             'does not separate votes that named another block at the parent QC round'),
            (r'verify_timeout_vote_v2\(', 'does not separate votes with a bad signature'),
            (r'scanned\s*<\s*max_rounds', 'walks the whole window uncapped: every counted vote is a signature check'),
            (r'rounds_in_window', 'does not size the window, so a capped scan cannot say how much it skipped'),
        ]:
            if not re.search(pat, diag):
                problems.append(f'diagnose_timeout_cert_window_v2 {why}')
        if 'block_timeout_certificate_v2' in diag:
            problems.append('diagnose_timeout_cert_window_v2 builds a certificate: a diagnosis must be read-only')

    body_raw = method(val_cpp, 'validator_plugin::impl::maybe_validate_block(')
    if body_raw is None:
        problems.append('validator_plugin::impl::maybe_validate_block() is missing')
    else:
        # Ordering is judged on the masked copy (same length, literals blanked); the log
        # arguments are code, but the format string between them is a literal, so the
        # argument pattern is matched on the raw text.
        body = _masked(body_raw)
        search_at = body.find('get_timeout_cert_between_v2(')
        diag_at = body.find('db.diagnose_timeout_cert_window_v2(')
        if diag_at < 0:
            problems.append('the HF18 refusal never asks why the TC window was empty')
        elif search_at >= 0 and diag_at < search_at:
            problems.append('the diagnosis runs before the search it explains')
        for pat, why in [
            (r'\("n",\s*d\.rounds_scanned\)\s*\("t",\s*d\.rounds_in_window\)',
             'does not say how much of the window the scan covered'),
            (r'\("k",\s*d\.usable\)\s*\("q",\s*d\.quorum\)', 'does not say how many votes survived against the quorum'),
            (r'\("x",\s*d\.above_parent_qc\)\s*\("y",\s*d\.other_block\)', 'does not say why votes were dropped'),
            (r'\("z",\s*d\.bad_signature\)', 'does not count votes with a bad signature'),
        ]:
            if not re.search(pat, body_raw):
                problems.append(f'the refusal {why}')

    raw = val_cpp
    key = f'const std::string why = "{KEY}";'
    if key not in raw:
        problems.append('the once-per-head dedup is not keyed by a stable reason: the counts would repeat every slot')
    else:
        guarded = raw.find('if (skip_diag_head != db.head_block_num()')
        at = raw.find('const auto d = db.diagnose_timeout_cert_window_v2(')
        logged = raw.find('wlog(', at if at >= 0 else 0)
        if guarded < 0 or at < 0 or not (guarded < at < logged):
            problems.append('the diagnosis runs outside the once-per-head guard: a signature walk on every slot')
    return problems


MUTATIONS = {
    'no-call': ('const auto d = db.diagnose_timeout_cert_window_v2(',
                'const auto d = db.get_timeout_cert_between_v2('),
    'no-round-filter': ('if (entry.high_qc_round > max_high_qc_round) { ++above; continue; }', ''),
    'no-block-id-filter': ('if (entry.high_qc_round == max_high_qc_round && entry.high_qc_block_id != max_high_qc_block_id) { ++other; continue; }', ''),
    'no-signature-check': ('if (!verify_timeout_vote_v2(it->first, entry.high_qc_round, entry.high_qc_block_id, tv.first, entry.signature)) { ++bad_sig; continue; }', ''),
    'builds-certificate': ('timeout_cert_window_diagnosis d;', 'timeout_cert_window_diagnosis d; protocol::block_timeout_certificate_v2 tc;'),
    'uncapped-walk': ('while (it != _timeout_votes.begin() && scanned < max_rounds) {', 'while (it != _timeout_votes.begin()) {'),
    'unstable-dedup-key': (f'const std::string why = "{KEY}";', 'const std::string why = std::to_string(own_round);'),
    'diagnose-outside-guard': ('if (skip_diag_head != db.head_block_num() || skip_diag_why != why) {', 'if (true) {'),
    'counts-dropped-from-log': ('("x", d.above_parent_qc)("y", d.other_block)', '("x", 0)("y", 0)'),
    'window-total-dropped': ('("n", d.rounds_scanned)("t", d.rounds_in_window)', '("n", d.rounds_scanned)("t", 0)'),
}


def selftest():
    val, db = VAL_CPP.read_text(), DB_CPP.read_text()
    failures = []
    for name, (old, new) in MUTATIONS.items():
        n = val.count(old) + db.count(old)
        if n != 1:
            failures.append(f'{name}: anchor found {n} times -- mutation did not land')
            continue
        mv, md = (val.replace(old, new, 1), db) if old in val else (val, db.replace(old, new, 1))
        if not wiring_problems(mv, md):
            failures.append(f'{name}: stayed GREEN (the check it targets is vacuous)')
        else:
            print(f'RED as expected: {name}')
    for f in failures:
        print(f'BAD: {f}', file=sys.stderr)
    if failures:
        return 1
    print(f'PASS: {len(MUTATIONS)} mutations, every one red')
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--selftest', action='store_true')
    if parser.parse_args().selftest:
        return selftest()
    problems = wiring_problems(VAL_CPP.read_text(), DB_CPP.read_text())
    for p in problems:
        print(f'FINDING: {p}')
    if problems:
        return 1
    print('PASS: a refused TC skip names how many votes survived and why the others were dropped')
    return 0


if __name__ == '__main__':
    sys.exit(main())
