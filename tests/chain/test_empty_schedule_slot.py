#!/usr/bin/env python3
"""An empty slot in validator_schedule must not reject the block that passes it.

`validator_schedule_object` is sized `num_scheduled_validators`, but the chain can have fewer
validators than that, and the remaining entries of `current_shuffled_validators` are EMPTY
strings. `get_validator("")` throws `unknown key`.

In `database::update_global_dynamic_data()` the miss loop walks every slot skipped since the
previous block:

    for (i in 0 .. missed_blocks) { get_validator(get_scheduled_validator(i + 1)) ... }

This runs on the APPLY path, so the throw does not merely skip a penalty: it rejects the block
being applied. Every node rejects it, the head never advances, and the chain is frozen exactly
at the empty slot -- forever. Measured 01.11.2026 on the 21-validator HF16 stand: head stuck at
#21 with `_apply_block: out_of_range: unknown key {"name":""}`, `Next expected: block #22 by`
(empty name), 500 consecutive not_my_turn slots.

The fix this file guards: resolve the scheduled name once, skip it when empty, and only then
look the validator up. Nobody can be charged for a slot that has no owner.

Run: python3 tests/chain/test_empty_schedule_slot.py [--selftest]
"""
import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
DB_CPP = ROOT / 'libraries' / 'chain' / 'database.cpp'


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


def problems_in(cpp):
    problems = []
    body = method(cpp, 'void database::update_global_dynamic_data(')
    if body is None:
        return ['database::update_global_dynamic_data() not found -- the miss loop cannot be judged']

    masked = _masked(body)
    whole = _masked(cpp)

    # 1. No call site may feed a schedule name straight into get_validator(): that is the throw.
    direct = re.findall(r'get_validator\s*\(\s*get_scheduled_validator\s*\(', whole)
    if direct:
        problems.append(f'{len(direct)} call site(s) pass a scheduled name straight into get_validator() '
                        '(get_validator(get_scheduled_validator(...))) -- an empty slot throws unknown key')

    # 2. The loop must resolve the name into a variable first (you cannot test what you inlined).
    resolve = re.search(r'const\s+string\s+missed_validator\s*=\s*get_scheduled_validator\s*\(', masked)
    if not resolve:
        problems.append('the miss loop does not capture the scheduled name before the lookup')

    # 3. ... and skip it before any validator object is touched.
    guard = masked.find('missed_validator.empty()')
    lookup = masked.find('get_validator(missed_validator)')
    if guard < 0:
        problems.append('the miss loop has no empty-slot guard')
    elif lookup >= 0 and guard > lookup:
        problems.append('the empty-slot guard is evaluated AFTER get_validator() -- the throw happens first')
    elif 'continue;' not in masked[guard:guard + 200]:
        problems.append('the empty-slot branch does not skip the slot (missing continue)')

    # 4. The guard must sit INSIDE the loop, after the slot count was fixed (missed_blocks--),
    #    so it cannot change how many slots were skipped.
    loop_at = masked.find('for (uint32_t i = 0; i < missed_blocks; ++i)')
    if loop_at < 0:
        problems.append('the miss loop is gone or changed shape -- re-read before trusting this test')
    elif guard >= 0 and guard < loop_at:
        problems.append('the empty-slot guard is outside the miss loop')
    return problems


MUTATIONS = {
    'no-guard': ('if (missed_validator.empty())\n                            continue;\n', ''),
    'guard-without-continue': ('if (missed_validator.empty())\n                            continue;',
                               'if (missed_validator.empty())\n                            (void)missed_validator;'),
    'guard-after-lookup': ('const string missed_validator = get_scheduled_validator(i + 1);\n'
                           '                        if (missed_validator.empty())\n'
                           '                            continue;\n'
                           '                        const auto &validator_missed = get_validator(missed_validator);',
                           'const string missed_validator = get_scheduled_validator(i + 1);\n'
                           '                        const auto &validator_missed = get_validator(missed_validator);\n'
                           '                        if (missed_validator.empty())\n'
                           '                            continue;'),
    'inline-lookup': ('const string missed_validator = get_scheduled_validator(i + 1);',
                      'const string missed_validator = get_scheduled_validator(i + 1); (void)0;'
                      ' const auto &probe = get_validator(get_scheduled_validator(i + 1)); (void)probe;'),
}


def selftest():
    text = DB_CPP.read_text()
    failures = []
    total = 0
    for name, (old, new) in MUTATIONS.items():
        if old not in text:
            failures.append(f'{name}: anchor not found -- mutation did not land')
            continue
        problems = problems_in(text.replace(old, new, 1))
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
    problems = problems_in(DB_CPP.read_text())
    if problems:
        for p in problems:
            print(f'FINDING: {p}')
        return 1
    print('PASS: the miss loop skips empty schedule slots before resolving a validator')
    return 0


if __name__ == '__main__':
    sys.exit(main())
