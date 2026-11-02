#!/usr/bin/env python3
"""Wiring of the HF17 emergency exit: source-level regression, no live node.

The unit test (exit_threshold_test.cpp) pins the FORMULA. It cannot see the two properties
that make the fix work at all, because both are about where code sits inside
update_validator_schedule():

  1. the exit is decided on the NORMAL schedule, i.e. BEFORE the hybrid override. Put the
     check after the override and the schedule is already padded to 21 slots, so the chain
     leaves emergency still publishing a 21-slot schedule that demands quorum(21) = 15
     signatures until the next 21-block rebuild -- from at most R real signers, and the
     producer of a certified block does not sign its own block. At R = 15 that window has
     zero slack, which is the second half of the defect this fork closes;
  2. the block that exits must NOT also run the override. The check and the override are
     two independent `if`s in the same function, so an `exited_emergency` flag is the only
     thing stopping "exit, then pad the schedule anyway".

Both are invisible to a compiler and to the formula test: each is a one-line reordering or a
dropped `!exited_emergency`, and the chain would still build, still pass ctest, and still be
wrong. Hence this file.

Also pinned here: the field exists in FC_REFLECT (LAST -- the reflection order is the
serialized order, so an inserted field silently corrupts every snapshot), the snapshot
importer reads it with a contains() guard, the chainbase schema version is bumped for the
layout change, and CHAIN_VERSION's middle component IS the hardfork number (4.3.0), so the
binary advertises the fork it is about to vote for.

Run: python3 tests/chain/test_emergency_exit_wiring.py [--selftest]
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CHAIN = ROOT / 'libraries' / 'chain'
PROTOCOL = ROOT / 'libraries' / 'protocol' / 'include' / 'graphene' / 'protocol'

DB_CPP = CHAIN / 'database.cpp'
DB_HARDFORK_CPP = CHAIN / 'database_hardfork.cpp'
GPO_HPP = CHAIN / 'include' / 'graphene' / 'chain' / 'global_property_object.hpp'
PREAMBLE_HF = CHAIN / 'hardfork.d' / '0-preamble.hf'
HF17 = CHAIN / 'hardfork.d' / '17.hf'
SNAPSHOT_CPP = ROOT / 'plugins' / 'snapshot' / 'plugin.cpp'
CONFIG_HPP = PROTOCOL / 'config.hpp'
CONFIG_TESTNET_HPP = PROTOCOL / 'config_testnet.hpp'

FILES = {
    'db_cpp': DB_CPP,
    'db_hardfork_cpp': DB_HARDFORK_CPP,
    'gpo_hpp': GPO_HPP,
    'preamble_hf': PREAMBLE_HF,
    'hf17': HF17,
    'snapshot_cpp': SNAPSHOT_CPP,
    'config_hpp': CONFIG_HPP,
    'config_testnet_hpp': CONFIG_TESTNET_HPP,
}


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


def reflect_fields(source, struct):
    """Field names inside FC_REFLECT((graphene::chain::<struct>), ...), in order."""
    masked = _masked(source)
    start = masked.find('FC_REFLECT((graphene::chain::%s)' % struct)
    if start < 0:
        return []
    # Walk from the FC_REFLECT( paren to the one that closes the whole reflection list.
    depth = 0
    end = -1
    for i in range(start + len('FC_REFLECT'), len(masked)):
        if masked[i] == '(':
            depth += 1
        elif masked[i] == ')':
            depth -= 1
            if depth == 0:
                end = i
                break
    if end < 0:
        return []
    body_start = masked.find('),', start)
    if body_start < 0 or body_start >= end:
        return []
    return re.findall(r'\(\s*(\w+)\s*\)', masked[body_start + 1:end])


def wiring_problems(**files):
    problems = []
    db_cpp = _masked(files['db_cpp'])

    body = method(db_cpp, 'void database::update_validator_schedule(')
    if body is None:
        return ['database::update_validator_schedule() is missing -- that is where emergency consensus '
                'is entered, maintained and left']
    b = _masked(body)

    # --- 1. The exit is decided BEFORE the hybrid override ------------------------------
    # Position matters, not the presence of both: with the override first, the schedule is
    # already 21 padded slots and the exit publishes a schedule the returning validators
    # cannot fill.
    exit_at = b.find('emergency_exit_threshold_effective')
    override_at = b.find('replace unavailable/empty slots with committee')  # comment, masked out
    override_sig_at = b.find('find_validator(wname)')
    if exit_at < 0:
        problems.append('the HF17 exit check is not in update_validator_schedule(): nothing decides the exit '
                        'from the entry schedule')
    if override_sig_at < 0:
        problems.append('the hybrid override (find_validator(wname) availability pass) is gone from '
                        'update_validator_schedule() -- the premise of the ordering check no longer holds')
    if exit_at >= 0 and override_sig_at >= 0 and exit_at > override_sig_at:
        problems.append('the exit check sits AFTER the hybrid override: the schedule is padded to '
                        'CHAIN_MAX_VALIDATORS first, so the exiting block still publishes 21 slots demanding '
                        'quorum(21) signatures from at most R real validators (zero slack at R = 15)')

    # --- 2. The threshold is the quorum of the size measured on entry -------------------
    if not re.search(r'emergency_exit_threshold_effective\(\s*emergency_dgp\.emergency_schedule_size_on_entry\s*\)', b):
        problems.append('the exit threshold is not emergency_exit_threshold_effective(size on entry): the whole '
                        'point of the fork -- and a raw 75% of CHAIN_MAX_VALIDATORS is the defect itself')

    # --- 3. The fork gate ---------------------------------------------------------------
    if not re.search(r'has_hardfork\(CHAIN_HARDFORK_12\)\s*&&\s*has_hardfork\(CHAIN_HARDFORK_17\)', b):
        problems.append('the new exit check is not gated on HF17 (and HF12): it would change the moment the '
                        'chain leaves emergency for blocks that already happened')

    # --- 4. An exiting block must not run the override ----------------------------------
    if not re.search(r'exited_emergency\s*=\s*true\s*;', b):
        problems.append('the exit branch never sets exited_emergency: the override cannot know it must stand '
                        'down in the block that leaves emergency')
    if not re.search(r'has_hardfork\(CHAIN_HARDFORK_12\)\s*&&\s*!\s*exited_emergency\s*&&', b):
        problems.append('the hybrid override is not guarded by !exited_emergency: the block that leaves '
                        'emergency pads the schedule right back to 21 slots -- exit without effect')

    # --- 5. The pre-fork exit path is switched off, not merely shadowed -----------------
    # On the production config the legacy threshold (15) is never below the HF17 one, so the
    # gate looks redundant -- but under BUILD_TESTNET CHAIN_MAX_VALIDATORS is 1 and the legacy
    # expression yields 0, which ends emergency with zero real validators.
    if not re.search(r'!\s*has_hardfork\(CHAIN_HARDFORK_17\)\s*&&\s*real_validator_slots\s*>=\s*exit_threshold', b):
        problems.append('the legacy (post-override) exit is not switched off from HF17: on the testnet build '
                        'its threshold is 21*75%/100 of one validator = 0, so emergency ends with no real '
                        'validator at all and a committee-only padded schedule is left behind')

    # --- 6. The size is recorded ON ENTRY, gated on the fork ---------------------------
    # This block lives outside update_validator_schedule(): emergency is switched on in the
    # block-application path, and the measurement must be taken there, from the NORMAL
    # schedule of that block.
    entry_at = db_cpp.find('const bool record_entry_size')
    if entry_at < 0:
        problems.append('entering emergency does not record the schedule size at all '
                        '(no `const bool record_entry_size` next to the switch-on of '
                        'emergency_consensus_active)')
        entry = ''
    else:
        entry = db_cpp[entry_at:entry_at + 900]
    if entry:
        if not re.search(r'record_entry_size\s*=\s*has_hardfork\(CHAIN_HARDFORK_17\)', entry):
            problems.append('the entry measurement is not gated on HF17 (record_entry_size = '
                            'has_hardfork(CHAIN_HARDFORK_17)): pre-fork blocks would store an entry size '
                            'that no rule reads, in a field older nodes do not have')
        if not re.search(r'num_scheduled_validators\s*/\s*CHAIN_BLOCK_VALIDATOR_REPEAT', entry):
            problems.append('the recorded size is not normalised by CHAIN_BLOCK_VALIDATOR_REPEAT: with '
                            'repeat > 1 it would be the ARRAY length, and the threshold would ask for a '
                            'quorum of a schedule several times the real one')
        if not re.search(r'if\s*\(\s*record_entry_size\s*\)\s*dgp\.emergency_schedule_size_on_entry\s*=\s*'
                         r'schedule_size_on_entry\s*;', entry):
            problems.append('the measured size is not written to emergency_schedule_size_on_entry under the '
                            'record_entry_size guard')
        if not re.search(r'dgp\.emergency_consensus_active\s*=\s*true\s*;', entry):
            problems.append('the entry measurement is not next to emergency_consensus_active = true: it is '
                            'no longer tied to the moment emergency actually starts')

    # --- 7. The startup repair path resets the field -----------------------------------
    # There the schedule is broken by definition, so there is no size to measure; a stale
    # value from an earlier episode would decide when this one ends.
    if not re.search(r'_dgp\.emergency_schedule_size_on_entry\s*=\s*0\s*;', db_cpp):
        problems.append('the startup schedule-repair path does not reset emergency_schedule_size_on_entry: a '
                        'stale value from an earlier episode decides the exit of the repaired one')

    # --- 8. The field is the LAST one in FC_REFLECT ------------------------------------
    fields = reflect_fields(files['gpo_hpp'], 'dynamic_global_property_object')
    if not fields:
        problems.append('FC_REFLECT for dynamic_global_property_object is unreadable -- cannot check field order')
    elif 'emergency_schedule_size_on_entry' not in fields:
        problems.append('emergency_schedule_size_on_entry is not in FC_REFLECT: it never reaches the snapshot '
                        'file and every restored node starts on "not measured"')
    elif fields[-1] != 'emergency_schedule_size_on_entry':
        problems.append('emergency_schedule_size_on_entry is not the LAST reflected field (found %r): the '
                        'reflection order is the serialized order, so an inserted field shifts every field '
                        'after it and old snapshots decode into the wrong members'
                        % (fields[-1],))

    # --- 9. The snapshot importer reads it defensively ---------------------------------
    snap = _masked(files['snapshot_cpp'])
    if not re.search(r'contains\(\s*"emergency_schedule_size_on_entry"\s*\)', files['snapshot_cpp']):
        problems.append('the snapshot importer does not guard emergency_schedule_size_on_entry with contains(): '
                        'importing a pre-HF17 snapshot would throw (or, worse, read garbage) instead of '
                        'treating the episode as not measured')

    # --- 10. Chainbase layout change => schema version bump -----------------------------
    for key, label in (('config_hpp', 'config.hpp'), ('config_testnet_hpp', 'config_testnet.hpp')):
        m = re.search(r'#define\s+CHAIN_SCHEMA_VERSION\s+uint32_t\((\d+)\)', _masked(files[key]))
        if not m:
            problems.append('CHAIN_SCHEMA_VERSION is unreadable in %s' % label)
        elif int(m.group(1)) < 16:
            problems.append('CHAIN_SCHEMA_VERSION is %s in %s: a field added to dynamic_global_property_object '
                            'is a chainbase layout change, and an old shared_memory.bin would be reopened with '
                            'the new struct' % (m.group(1), label))

    # --- 11. Fork registration and the version the binary advertises --------------------
    m = re.search(r'#define\s+CHAIN_NUM_HARDFORKS\s+(\d+)', _masked(files['preamble_hf']))
    if not m:
        problems.append('CHAIN_NUM_HARDFORKS is unreadable in 0-preamble.hf')
    elif int(m.group(1)) < 17:
        problems.append('CHAIN_NUM_HARDFORKS is %s: hardfork.d/17.hf is not compiled into the fork table' % m.group(1))

    hf17 = _masked(files['hf17'])
    if not re.search(r'#define\s+CHAIN_HARDFORK_17_VERSION\s+hardfork_version\(\s*version\(\s*4\s*,\s*3\s*,\s*0\s*\)\s*\)', hf17):
        problems.append('17.hf does not define CHAIN_HARDFORK_17_VERSION as hardfork_version(version(4, 3, 0))')

    for key, label in (('config_hpp', 'config.hpp'), ('config_testnet_hpp', 'config_testnet.hpp')):
        m = re.search(r'#define\s+CHAIN_VERSION\s+\(?\s*version\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)', _masked(files[key]))
        if not m:
            problems.append('CHAIN_VERSION is unreadable in %s' % label)
        elif (int(m.group(1)), int(m.group(2))) != (4, 3):
            problems.append('CHAIN_VERSION in %s is %s.%s.%s, not 4.3.0: the middle component IS the hardfork '
                            'number, so the binary would not advertise (and auto-vote for) HF17'
                            % (label, m.group(1), m.group(2), m.group(3)))

    dh = _masked(files['db_hardfork_cpp'])
    if not re.search(r'_hardfork_times\[\s*CHAIN_HARDFORK_17\s*\]\s*=', dh):
        problems.append('database_hardfork.cpp never registers CHAIN_HARDFORK_17_TIME: the fork would have a '
                        'version but no time, and the time is what gates activation')
    if not re.search(r'_hardfork_versions\[\s*CHAIN_HARDFORK_17\s*\]\s*=', dh):
        problems.append('database_hardfork.cpp never registers CHAIN_HARDFORK_17_VERSION')

    return problems


def _swap_exit_after_override(text):
    """Put the exit check AFTER the hybrid override -- the order the code had before HF17."""
    exit_at = text.find('bool exited_emergency = false;')
    override_at = text.find('if (has_hardfork(CHAIN_HARDFORK_12) && !exited_emergency &&')
    if exit_at < 0 or override_at < 0 or override_at < exit_at:
        return None
    depth = 0
    override_end = -1
    for i in range(override_at, len(text)):
        if text[i] == '{':
            depth += 1
        elif text[i] == '}':
            depth -= 1
            if depth == 0:
                override_end = i + 1
                break
    if override_end < 0:
        return None
    return text[:exit_at] + text[override_at:override_end] + '\n' + text[exit_at:override_at] + text[override_end:]


# A mutation is either a list of (old, new) literal pairs, or a callable on the file text.
MUTATIONS = {
    'exit-check-after-the-override': _swap_exit_after_override,
    'override-runs-in-the-exiting-block': [
        ('has_hardfork(CHAIN_HARDFORK_12) && !exited_emergency &&', 'has_hardfork(CHAIN_HARDFORK_12) &&'),
    ],
    'exit-flag-never-set': [
        ('exited_emergency = true;', ''),
    ],
    'threshold-from-the-constant': [
        ('emergency_exit_threshold_effective(emergency_dgp.emergency_schedule_size_on_entry)',
         '((CHAIN_MAX_VALIDATORS * CHAIN_IRREVERSIBLE_THRESHOLD) / CHAIN_100_PERCENT)'),
    ],
    'legacy-exit-still-live': [
        ('if (!has_hardfork(CHAIN_HARDFORK_17) && real_validator_slots >= exit_threshold) {',
         'if (real_validator_slots >= exit_threshold) {'),
    ],
    'entry-ungated': [
        ('const bool record_entry_size = has_hardfork(CHAIN_HARDFORK_17);',
         'const bool record_entry_size = true;'),
    ],
    'entry-size-not-written': [
        ('dgp.emergency_schedule_size_on_entry = schedule_size_on_entry;', ''),
    ],
    'entry-size-not-normalised': [
        ('num_scheduled_validators / CHAIN_BLOCK_VALIDATOR_REPEAT)',
         'num_scheduled_validators)'),
    ],
    'repair-path-keeps-stale-size': [
        ('_dgp.emergency_schedule_size_on_entry = 0;', ''),
    ],
    'reflect-field-moved-into-the-middle': [
        ('                (round_anchor_round)\n',
         '                (emergency_schedule_size_on_entry)\n                (round_anchor_round)\n'),
        ('                (emergency_schedule_size_on_entry)\n)', '\n)'),
    ],
    'snapshot-import-unguarded': [
        ('obj.emergency_schedule_size_on_entry = v.get_object().contains("emergency_schedule_size_on_entry")\n'
         '            ? static_cast<uint32_t>(v["emergency_schedule_size_on_entry"].as_uint64()) : 0;',
         'obj.emergency_schedule_size_on_entry = static_cast<uint32_t>(v["emergency_schedule_size_on_entry"].as_uint64());'),
    ],
    'schema-version-not-bumped': [
        ('#define CHAIN_SCHEMA_VERSION                  uint32_t(16)',
         '#define CHAIN_SCHEMA_VERSION                  uint32_t(15)'),
    ],
    'chain-version-does-not-advertise-hf17': [
        ('define CHAIN_VERSION                         (version(4, 3, 0))',
         'define CHAIN_VERSION                         (version(4, 2, 0))'),
    ],
    'num-hardforks-not-bumped': [
        ('#define CHAIN_NUM_HARDFORKS 17', '#define CHAIN_NUM_HARDFORKS 16'),
    ],
    'hardfork-time-not-registered': [
        ('_hardfork_times[CHAIN_HARDFORK_17] = fc::time_point_sec(CHAIN_HARDFORK_17_TIME);', ''),
    ],
}

# A mutation may target a file other than database.cpp; the pair is applied to that file.
MUTATION_FILE = {
    'reflect-field-moved-into-the-middle': 'gpo_hpp',
    'snapshot-import-unguarded': 'snapshot_cpp',
    'schema-version-not-bumped': 'config_hpp',
    'chain-version-does-not-advertise-hf17': 'config_testnet_hpp',
    'num-hardforks-not-bumped': 'preamble_hf',
    'hardfork-time-not-registered': 'db_hardfork_cpp',
}


def load():
    return {key: path.read_text() for key, path in FILES.items()}


def selftest():
    files = load()
    failures = []
    total = 0
    for name, pairs in MUTATIONS.items():
        key = MUTATION_FILE.get(name, 'db_cpp')
        mutated = dict(files)
        if callable(pairs):
            text = pairs(files[key])
            if text is None:
                failures.append('%s: mutation did not land' % name)
                continue
        else:
            text = files[key]
            landed = True
            for old, new in pairs:
                if old not in text:
                    failures.append('%s: anchor not found -- mutation did not land' % name)
                    landed = False
                    break
                text = text.replace(old, new, 1)
            if not landed:
                continue
        mutated[key] = text
        problems = wiring_problems(**mutated)
        total += len(problems)
        if not problems:
            failures.append('%s: stayed GREEN (the check it targets is vacuous)' % name)
        else:
            print('RED as expected: %s (%d)' % (name, len(problems)))
    if failures:
        for f in failures:
            print('BAD: %s' % f, file=sys.stderr)
        return 1
    print('PASS: %d mutations, every one red (%d findings)' % (len(MUTATIONS), total))
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
            print('FINDING: %s' % p)
        return 1
    print('PASS: the exit is decided before the hybrid override and an exiting block skips it; the threshold '
          'comes from the size measured on entry; the pre-fork path is off; the field is last in FC_REFLECT, '
          'guarded in the importer, and the schema and version carry the fork')
    return 0


if __name__ == '__main__':
    sys.exit(main())
