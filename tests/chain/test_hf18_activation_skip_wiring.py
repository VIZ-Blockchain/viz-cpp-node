#!/usr/bin/env python3
"""The TC skip must not pop the HF18 activation block. Source-level, no node.

If the block that activates HF18 misses its QC, validators time out on it with v2 votes. A
producer that takes the v2 TC pops that block, lands on the pre-fork parent (HF18 off, pool
cleared) and gives up the slot; on the next tick the parent's QC is enough to produce, so it
builds a TC-less block that gets no votes and activates HF18 again. Every producer repeats
this each slot with a fresh head time, so the 60 s unstick never fires: a livelock on the
activation day. The producer must recognise the activation head BEFORE pop_block() and keep
waiting, so the unstick builds on top of it.

Run: python3 tests/chain/test_hf18_activation_skip_wiring.py [--selftest]
"""
import argparse
import re
import sys
from pathlib import Path

from test_stall_recovery_wiring import _masked, method

ROOT = Path(__file__).resolve().parents[2]
VAL_CPP = ROOT / 'plugins' / 'validator' / 'validator.cpp'


def wiring_problems(val_cpp):
    body = method(_masked(val_cpp), 'validator_plugin::impl::maybe_validate_block(')
    if body is None:
        return ['validator_plugin::impl::maybe_validate_block() is missing']
    body = _masked(body)
    problems = []
    guard = re.search(r'if\s*\(\s*base\s*&&\s*hfp\.next_hardfork\s*==\s*CHAIN_HARDFORK_18_VERSION\s*&&\s*'
                      r'base->timestamp\s*<\s*hfp\.next_hardfork_time\s*\)\s*\{\s*skip_tc_v2\.reset\(\)', body)
    if not guard:
        return ['the v2 TC skip is taken even when head activated HF18: popping it lands on the pre-fork '
                'parent and the producers livelock rebuilding the activation block']
    if not re.search(r'base\s*=\s*db\.fetch_block_by_id\(\s*rebuild_on\s*\?\s*\*rebuild_on\s*:\s*head->previous\s*\)',
                     body[:guard.start()]):
        problems.append('the activation test reads the wrong base: a late-QC head is rebuilt on the certified '
                        'block, not on head->previous')
    pop = body.find('db.pop_block()')
    if pop < 0 or pop < guard.start():
        problems.append('the activation test runs after pop_block(): by then HF18 is already undone')
    return problems


MUTATIONS = {
    'guard-removed': ('base->timestamp < hfp.next_hardfork_time) {', 'false) {'),
    'base-ignores-rebuild-on': ('db.fetch_block_by_id(rebuild_on ? *rebuild_on : head->previous)',
                                'db.fetch_block_by_id(head->previous)'),
    'wrong-hardfork': ('hfp.next_hardfork == CHAIN_HARDFORK_18_VERSION &&', 'true &&'),
}


def selftest():
    src = VAL_CPP.read_text()
    failures = []
    for name, (old, new) in MUTATIONS.items():
        if src.count(old) != 1:
            failures.append(f'{name}: anchor found {src.count(old)} times -- mutation did not land')
        elif not wiring_problems(src.replace(old, new, 1)):
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
    problems = wiring_problems(VAL_CPP.read_text())
    for p in problems:
        print(f'FINDING: {p}')
    if problems:
        return 1
    print('PASS: the TC skip keeps an HF18 activation head and waits for the unstick')
    return 0


if __name__ == '__main__':
    sys.exit(main())
