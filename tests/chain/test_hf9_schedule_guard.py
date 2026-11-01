#!/usr/bin/env python3
"""The HF9 purge must not delete a validator whose name is in the current schedule.

`apply_hardfork(CHAIN_HARDFORK_9)` removes every validator with
`last_confirmed_block_num == 0` ("empty/spam"). On a chain where those validators have simply not
produced yet -- a fresh genesis, or any chain that is about to use them -- the schedule
(`validator_schedule_object.current_shuffled_validators`) still names them, and the migration does
not rebuild it.

The consequence is not a cosmetic dangling name. `database::update_global_dynamic_data()` walks the
missed slots on the APPLY path and calls `get_validator(scheduled_name)`; a name without a validator
object throws `unknown key`, so the block that would carry the chain past the slot is rejected by
every node and the chain freezes there.

Measured 02.11.2026 on a single node of the 21-validator HF16 bench:
  * fresh data dir   -> 22 validators, head passes #21;
  * `testnet-hardfork = 16` + restart -> 1 validator (`committee`), log says
    `HF9 remove empty/spam validator viz...` for every initiator, head stuck at #21.

The fix this file guards: skip the purge for a validator whose name is in the current schedule.

Run: python3 tests/chain/test_hf9_schedule_guard.py [--selftest]
"""
import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
HF_CPP = ROOT / 'libraries' / 'chain' / 'database_hardfork.cpp'


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


def case_hf9(source):
    """Body of `case CHAIN_HARDFORK_9:` up to its `break;` (brace balanced from the case label)."""
    masked = _masked(source)
    label = masked.find('case CHAIN_HARDFORK_9:')
    if label < 0:
        return None
    brace = masked.find('{', label)
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
    body = case_hf9(cpp)
    if body is None:
        return ['case CHAIN_HARDFORK_9 not found -- the purge cannot be judged']

    masked = _masked(body)
    purge = masked.find('if(0==current.last_confirmed_block_num)')
    if purge < 0:
        return ['the zero-confirmation purge condition is gone or changed shape -- re-read before trusting this test']

    guard = masked.find('current_shuffled_validators')
    removal = masked.find('remove(current)')

    # 1. The purge must consult the schedule before it removes anything.
    if guard < 0:
        problems.append('HF9 purges zero-confirmation validators without consulting the current schedule')
    elif removal >= 0 and guard > removal:
        problems.append('the schedule check sits AFTER remove(current) -- the validator is already gone')

    # 2. ... and the scheduled case must actually skip the purge (continue), not merely log.
    if guard >= 0:
        window = masked[guard:guard + 400]
        if 'continue;' not in window:
            problems.append('the scheduled-validator branch does not skip the purge (missing continue)')

    # 3. The check has to look at the schedule OBJECT, not at the validator's own record:
    #    `last_confirmed_block_num` is exactly the field that is zero here.
    if guard >= 0 and 'get_validator_schedule_object()' not in masked[:guard]:
        problems.append('the schedule check does not read get_validator_schedule_object()')

    # 4. Membership in the schedule has to BE the predicate. Re-testing the purge field inside the
    #    guard makes the branch unconditional (every zero-confirmation validator is skipped), which
    #    reads like a fix but silently disables the purge.
    if guard >= 0 and 'last_confirmed_block_num' in masked[guard:guard + 400]:
        problems.append('the guard decides by last_confirmed_block_num instead of schedule membership')
    return problems


GUARD_BLOCK = (
    "                            const validator_schedule_object &schedule = get_validator_schedule_object();\n"
    "                            if(std::find(schedule.current_shuffled_validators.begin(),\n"
    "                                         schedule.current_shuffled_validators.end(),\n"
    "                                         current.owner) != schedule.current_shuffled_validators.end()){\n"
    "                                continue;\n"
    "                            }\n")

GUARD_COND = (
    "if(std::find(schedule.current_shuffled_validators.begin(),\n"
    "                                         schedule.current_shuffled_validators.end(),\n"
    "                                         current.owner) != schedule.current_shuffled_validators.end()){\n"
    "                                continue;\n"
    "                            }")

REMOVE_LINES = (
    "                            elog(\"HF9 remove empty/spam validator ${a}\", (\"a\", current.owner));\n"
    "                            remove(current);")

LATE_GUARD = (
    "\n                            const validator_schedule_object &late = get_validator_schedule_object();\n"
    "                            if(std::find(late.current_shuffled_validators.begin(),\n"
    "                                         late.current_shuffled_validators.end(),\n"
    "                                         current.owner) != late.current_shuffled_validators.end()){\n"
    "                                continue;\n"
    "                            }")

# Each mutation is a list of (old, new) replacements applied in order, so a mutation can move a
# block (remove it here, re-add it there) instead of merely renaming it in place.
MUTATIONS = {
    'no-guard': [(GUARD_BLOCK, '')],
    'guard-without-continue': [(
        GUARD_COND,
        "if(std::find(schedule.current_shuffled_validators.begin(),\n"
        "                                         schedule.current_shuffled_validators.end(),\n"
        "                                         current.owner) != schedule.current_shuffled_validators.end()){\n"
        "                                elog(\"HF9 keeps scheduled validator ${a}\", (\"a\", current.owner));\n"
        "                            }")],
    'guard-after-removal': [
        (GUARD_BLOCK, ''),
        (REMOVE_LINES, "                            remove(current);\n"
                       "                            elog(\"HF9 remove empty/spam validator ${a}\", (\"a\", current.owner));"
                       + LATE_GUARD),
    ],
    'guard-on-wrong-field': [(
        "current.owner) != schedule.current_shuffled_validators.end()){",
        "current.owner) != schedule.current_shuffled_validators.end()\n"
        "                               || current.last_confirmed_block_num == 0){")],
}


def selftest():
    text = HF_CPP.read_text()
    failures = []
    total = 0
    for name, edits in MUTATIONS.items():
        mutated = text
        landing = True
        for old, new in edits:
            if old not in mutated:
                failures.append(f'{name}: anchor not found -- mutation did not land')
                landing = False
                break
            mutated = mutated.replace(old, new, 1)
        if not landing:
            continue
        problems = problems_in(mutated)
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
    problems = problems_in(HF_CPP.read_text())
    if problems:
        for p in problems:
            print(f'FINDING: {p}')
        return 1
    print('PASS: HF9 keeps validators whose names are in the current schedule')
    return 0


if __name__ == '__main__':
    sys.exit(main())
