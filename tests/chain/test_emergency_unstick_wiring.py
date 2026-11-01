#!/usr/bin/env python3
"""Wiring of the HF16 emergency unstick: source-level regression, no live node.

Mainnet stood on head #84865875 for 5 h 20 min on 01.11.2026 while every second printed
`TC formed 13/13`. The owner's call (01.11.2026, 22:32) was the emergency plan in three
layers, 60 s of silence as the trigger, shipped without a hardfork because the network is
the one that must be unstuck:

  1. a stalled validator stops claiming a QC only it holds and votes the one the head
     carries, so the TC quorum is not poisoned by votes nobody else can prove;
  2. the TC is assembled from the quorum-compatible subset instead of being frozen at the
     first quorum, so one vote above the parent QC cannot spoil the certificate forever;
  3. after CHAIN_EMERGENCY_UNSTICK_TIMEOUT_SEC of chain time without a block, a producer
     may build without a parent QC at all -- and every validator accepts such a block.

Layer 3 is the one that can break consensus if its trigger is not deterministic, so most
of this test guards exactly that: the rule reads CHAIN time (the parent's timestamp) and
never the local clock; a block without a QC is legal only while the rule says so; the
timeout vote only lies down for a stalled chain; the TC cut is taken by round, not by the
order votes happened to arrive in.

Run: python3 tests/chain/test_emergency_unstick_wiring.py [--selftest]
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CONFIG_HPP = ROOT / 'libraries' / 'protocol' / 'include' / 'graphene' / 'protocol' / 'config.hpp'
# The compiled variant: this tree's sources include config_testnet.hpp directly, so a
# constant added to config.hpp alone is invisible to every translation unit (learned the
# hard way on 01.11.2026 -- the single-TU check named the undefined macro).
CONFIG_TESTNET_HPP = ROOT / 'libraries' / 'protocol' / 'include' / 'graphene' / 'protocol' / 'config_testnet.hpp'
DB_CPP = ROOT / 'libraries' / 'chain' / 'database.cpp'
VAL_CPP = ROOT / 'plugins' / 'validator' / 'validator.cpp'
# Layer 3 compares the new block's timestamp with the head's, so while the chain is healthy
# a producer can only reach the 60 s gap by stamping a block ~60 s into the future. That is
# what stops it -- not the unstick rule itself -- so the premise is guarded here: widen it
# and a lone validator forks the live chain with an unstick block.
CHAIN_PLUGIN_CPP = ROOT / 'plugins' / 'chain' / 'plugin.cpp'

FILES = {'config_hpp': CONFIG_HPP, 'config_testnet_hpp': CONFIG_TESTNET_HPP,
         'db_cpp': DB_CPP, 'val_cpp': VAL_CPP, 'chain_plugin_cpp': CHAIN_PLUGIN_CPP}


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

    # 0. The trigger exists in BOTH config variants and is one minute, as the owner asked.
    #    config.hpp alone is dead weight here: every source includes config_testnet.hpp.
    for key, label in (('config_hpp', 'config.hpp'), ('config_testnet_hpp', 'config_testnet.hpp')):
        m = re.search(r'#define\s+CHAIN_EMERGENCY_UNSTICK_TIMEOUT_SEC\s+(\d+)', _masked(files[key]))
        if not m:
            problems.append('CHAIN_EMERGENCY_UNSTICK_TIMEOUT_SEC is not defined in %s -- that variant cannot '
                            'compile the rule' % label)
        elif m.group(1) != '60':
            problems.append('CHAIN_EMERGENCY_UNSTICK_TIMEOUT_SEC is %s in %s, the owner asked for 60'
                            % (m.group(1), label))

    # 1. The trigger is the CHAIN, not the local clock: a wall-clock rule would make two
    #    nodes judge one block differently and split the chain.
    rule = method(db_cpp, 'bool database::emergency_unstick_block(')
    if rule is None:
        problems.append('database::emergency_unstick_block() is missing -- nothing decides when a QC may be skipped')
    else:
        body = _masked(rule)
        if 'head_block_time()' not in body:
            problems.append('emergency_unstick_block() does not read the parent block time: the trigger would '
                            'come from nowhere')
        if not re.search(r'CHAIN_EMERGENCY_UNSTICK_TIMEOUT_SEC', body):
            problems.append('emergency_unstick_block() ignores CHAIN_EMERGENCY_UNSTICK_TIMEOUT_SEC')
        if 'time_point::now' in body or 'fc::time_point(' in body:
            problems.append('emergency_unstick_block() reads the LOCAL clock: nodes would disagree about one '
                            'block and fork on the unstick rule')
        if 'has_hardfork(CHAIN_HARDFORK_16)' not in body:
            problems.append('emergency_unstick_block() is not gated on HF16 -- before HF16 there is no QC rule '
                            'to relax, and the rule would change pre-fork consensus')
        if not re.search(r'when\s*<=\s*head_time', body):
            problems.append('emergency_unstick_block() has no "the new block is later than head" guard')

    # 2. Validation accepts an uncertified block ONLY through that rule.
    validate = method(db_cpp, 'void database::validate_block_qc(')
    if validate is None:
        problems.append('database::validate_block_qc() is missing')
    else:
        vbody = _masked(validate)
        if not re.search(r'const\s+bool\s+unstick\s*=\s*emergency_unstick_block\(\s*next_block\.timestamp\s*\)', vbody):
            problems.append('validate_block_qc() does not consult emergency_unstick_block(next_block.timestamp): '
                            'an unstick block would be rejected as "carries no QC of its parent"')
        if not re.search(r'block_needs_parent_qc\(\s*next_block\.validator\s*\)\s*&&\s*!\s*unstick', vbody):
            problems.append('validate_block_qc() still demands the parent QC unconditionally')

    # 3. The producer may skip the certificate, and only under the same rule.
    gen = method(db_cpp, 'signed_block database::_generate_block(')
    if gen is None:
        problems.append('database::_generate_block() is missing')
    else:
        gbody = _masked(gen)
        if not re.search(r'FC_ASSERT\(\s*emergency_unstick_block\(\s*when\s*\)', gbody):
            problems.append('_generate_block() still refuses to hand the turn over without a parent QC: the '
                            'producer would never build the block the new rule accepts')
        if not re.search(r'FC_ASSERT\(\s*!tc\s*,\s*', gbody):
            problems.append('_generate_block() lets an unstick block carry a TC (a TC needs a parent QC)')

    # 4. The validator actually produces such a block instead of waiting forever.
    if not re.search(r'db\.with_weak_read_lock\([^;]*emergency_unstick_block\(\s*scheduled_time\s*\)', val_cpp):
        problems.append('the validator plugin never asks emergency_unstick_block(): a stalled producer still '
                        'waits for a QC that the network cannot deliver')

    # 5. Layer 1: a stalled node votes what head proves, not what only it remembers.
    if not re.search(r'certified\s*&&\s*!\s*stalled\s*\)\s*\?\s*head_qc->round\s*:\s*carried_qc_round', val_cpp):
        problems.append('the timeout vote still claims the node-local QC round while stalled: its vote poisons '
                        'every TC built with it (the 12/3 split of 01.11.2026)')
    stalled = re.search(r'const\s+bool\s+stalled\s*=\s*\(?\s*now\s*-\s*db\.head_block_time\(\)\s*\)?\.to_seconds\(\)\s*>=\s*'
                        r'CHAIN_EMERGENCY_UNSTICK_TIMEOUT_SEC', val_cpp)
    if not stalled:
        problems.append('the stalled flag is not measured against CHAIN_EMERGENCY_UNSTICK_TIMEOUT_SEC of head time')

    # 6. Layer 2: the TC is cut by round, never frozen at the first quorum.
    push_vote = method(db_cpp, 'bool database::push_timeout_vote(')
    if push_vote is None:
        problems.append('database::push_timeout_vote() is missing')
    else:
        pbody = _masked(push_vote)
        if 'votes.insert(votes.begin(), votes.end())' in pbody:
            problems.append('the TC is still built from ALL votes at the first quorum: one vote above the parent '
                            'QC poisons the certificate for the whole round')
        if not re.search(r'by_round\[quorum\s*-\s*1\]\.first', pbody):
            problems.append('the TC cut is not the smallest round a quorum fits under')
        if not re.search(r'_timeout_certs\[\s*round\s*\]\s*=', pbody):
            problems.append('an already formed TC is never replaced, so a poisoned certificate never heals')

    # 7. The premise that keeps layer 3 harmless on a LIVE chain: ingress refuses a block
    #    stamped more than `allow_future_time` (5 s) ahead of the local clock. Layer 3 allows
    #    an uncertified block only when the gap to head is >= 60 s, a gap reachable on a live
    #    chain solely by stamping the block that far into the future -- so the 5 s ingress
    #    bound is what stands between one validator and a fork of a healthy chain.
    plugin_cpp = _masked(files['chain_plugin_cpp'])
    m = re.search(r'allow_future_time\s*=\s*(\d+)', plugin_cpp)
    if not m:
        problems.append('plugins/chain no longer bounds how far a block may be stamped ahead of the local '
                        'clock: on a live chain a single validator could jump the 60 s unstick gap and fork')
    elif int(m.group(1)) >= 60:
        problems.append('allow_future_time is %s s: at or above the 60 s unstick gap, one validator can forge '
                        'a stall on a live chain and fork it' % m.group(1))
    check = method(plugin_cpp, 'void plugin::plugin_impl::check_time_in_block(')
    if check is None or not re.search(r'block\.timestamp\.sec_since_epoch\(\)\s*<=\s*max_accept_time',
                                      _masked(check or '')):
        problems.append('check_time_in_block() no longer rejects a block stamped ahead: the future-time bound '
                        'layer 3 relies on is gone')
    return problems


MUTATIONS = {
    'constant-widened-to-120': (
        CONFIG_TESTNET_HPP, '#define CHAIN_EMERGENCY_UNSTICK_TIMEOUT_SEC       60',
        '#define CHAIN_EMERGENCY_UNSTICK_TIMEOUT_SEC       120'),
    'constant-missing-from-compiled-variant': (
        CONFIG_TESTNET_HPP, '#define CHAIN_EMERGENCY_UNSTICK_TIMEOUT_SEC       60', ''),
    'rule-reads-wall-clock': (
        DB_CPP, 'const fc::time_point_sec head_time = head_block_time();',
        'const fc::time_point_sec head_time = fc::time_point_sec(fc::time_point::now());'),
    'rule-gap-inverted': (
        DB_CPP, 'if (when <= head_time) return false;',
        'if (when >= head_time) return false;'),
    'rule-not-gated-on-hf16': (
        DB_CPP, 'if (!has_hardfork(CHAIN_HARDFORK_16)) return false;\n'
                '            const auto &dgp = get_dynamic_global_properties();\n'
                '            if (dgp.emergency_consensus_active) return false;   // the committee path owns that mode',
        'const auto &dgp = get_dynamic_global_properties();\n'
        '            if (dgp.emergency_consensus_active) return false;   // the committee path owns that mode'),
    'validation-always-unsticks': (
        DB_CPP, 'block_needs_parent_qc(next_block.validator) && !unstick',
        'block_needs_parent_qc(next_block.validator) && false'),
    'validation-ignores-the-rule': (
        DB_CPP, 'const bool unstick = emergency_unstick_block(next_block.timestamp);',
        'const bool unstick = false;'),
    'generator-refuses-unstick': (
        DB_CPP, 'FC_ASSERT(emergency_unstick_block(when),',
        'FC_ASSERT(false,'),
    'generator-allows-tc-on-unstick': (
        DB_CPP, 'FC_ASSERT(!tc, "An unstick block carries no TC");', ''),
    'validator-never-produces-unstick': (
        VAL_CPP, 'db.with_weak_read_lock([&]() { return db.emergency_unstick_block(scheduled_time); })',
        'false'),
    'vote-still-claims-local-qc': (
        VAL_CPP, 'const uint32_t high_qc_round = (certified && !stalled) ? head_qc->round : carried_qc_round;',
        'const uint32_t high_qc_round = certified ? head_qc->round : carried_qc_round;'),
    'vote-stall-not-measured': (
        VAL_CPP, '(now - db.head_block_time()).to_seconds() >= CHAIN_EMERGENCY_UNSTICK_TIMEOUT_SEC',
        '(now - db.head_block_time()).to_seconds() >= 0'),
    'tc-frozen-at-first-quorum': (
        DB_CPP, '_timeout_certs[round] = std::move(tc);', '_timeout_certs.emplace(round, std::move(tc));'),
    'tc-cut-ignores-round': (
        DB_CPP, 'const uint32_t cut = by_round[quorum - 1].first;',
        'const uint32_t cut = by_round.back().first;'),
    'ingress-future-bound-widened': (
        CHAIN_PLUGIN_CPP, 'allow_future_time = 5;', 'allow_future_time = 600;'),
    'ingress-future-bound-removed': (
        CHAIN_PLUGIN_CPP,
        'FC_ASSERT(block.timestamp.sec_since_epoch() <= max_accept_time);',
        '(void)max_accept_time;'),
}


def load():
    return {key: path.read_text() for key, path in FILES.items()}


def selftest():
    files = load()
    failures = []
    total = 0
    for name, (path, old, new) in MUTATIONS.items():
        key = next(k for k, p in FILES.items() if p == path)
        if old not in files[key]:
            failures.append(f'{name}: anchor not found -- mutation did not land')
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
    print('PASS: the unstick trigger reads chain time, validation and production agree on it, '
          'the stalled vote stops poisoning TCs and the TC is cut by round')
    return 0


if __name__ == '__main__':
    sys.exit(main())
