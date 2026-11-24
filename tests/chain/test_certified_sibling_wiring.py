#!/usr/bin/env python3
"""A certified sibling of a QC-less head must become head. Source-level, no node.

Mainnet 05.11.2026 09:44-10:04Z: two siblings of #85147200 gathered a QC, but head was a third,
QC-less one. Producers took the TC path, dropped head and built one more QC-less sibling on the
parent each slot; nobody votes for a block whose TC claims a QC older than one already seen, so
the chain stood until emergency. Two holes kept it there:
  - the producer looked for a TC without first taking the sibling its QC names;
  - the same-height comparison ranks blocks by the certificate they CARRY, so a TC sibling
    always displaced the certified one.

Run: python3 tests/chain/test_certified_sibling_wiring.py [--selftest]
"""
import argparse
import re
import sys
from pathlib import Path

from test_stall_recovery_wiring import _masked, method

ROOT = Path(__file__).resolve().parents[2]
VAL_CPP = ROOT / 'plugins' / 'validator' / 'validator.cpp'
DB_CPP = ROOT / 'libraries' / 'chain' / 'database.cpp'


def wiring_problems(val_cpp, db_cpp):
    problems = []
    body = method(_masked(val_cpp), 'validator_plugin::impl::maybe_validate_block(')
    if body is None:
        return ['validator_plugin::impl::maybe_validate_block() is missing']
    body = _masked(body)
    call = body.find('db.switch_head_to_certified_sibling()')
    tc = body.find('db.get_timeout_cert_between')
    if call < 0:
        problems.append('the producer never takes a certified sibling of a QC-less head')
    elif tc >= 0 and call > tc:
        problems.append('the certified sibling is taken after the TC search: the TC rebuild wins first')
    elif not re.search(r'qc\s*&&\s*qc->block_id\s*!=\s*db\.head_block_id\(\)', body[:call]):
        problems.append('no read-lock pre-check: the write lock is taken on every production tick')
    if 'db.converge_to_canonical_sibling()' not in body:
        problems.append('the producer never converges on the sibling the votes name (the 18.11.2026 hole)')

    sw = method(_masked(db_cpp), 'bool database::switch_head_to_certified_sibling(')
    if sw is None:
        problems.append('database::switch_head_to_certified_sibling() is missing')
    else:
        sw = _masked(sw)
        for pat, why in [
            (r'emergency_consensus_active\)\s*return\s+false', 'switches during emergency consensus'),
            (r'num\s*<=\s*dpo\.last_irreversible_block_num\)\s*return\s+false', 'may switch at or below LIB'),
            (r'sib->data\.previous\s*!=\s*head_item->data\.previous', 'takes a block that is not a sibling of head'),
            (r'verify_block_vote\(', 'trusts the stored QC without re-verifying its signatures'),
            (r'ok\s*<\s*quorum', 'switches on a QC below quorum'),
            (r'make_sibling_head\(sib,\s*head_item,', 'does not go through the shared switch (guard and restore live there)'),
        ]:
            if not re.search(pat, sw):
                problems.append(f'switch_head_to_certified_sibling {why}')

    # 18.11.2026: the QC-less siblings of #86227200 never gathered a QC, so the certified rule
    # above had nothing to take and the chain stood for six hours. The voted-sibling rule must
    # exist, read only block properties and received votes, require a strict lead, and go through
    # the same switch.
    cv = method(_masked(db_cpp), 'bool database::converge_to_canonical_sibling(')
    if cv is None:
        problems.append('database::converge_to_canonical_sibling() is missing')
    else:
        cv = _masked(cv)
        for pat, why in [
            (r'emergency_consensus_active\)\s*return\s+false', 'converges during emergency consensus'),
            (r'make_sibling_head\(sib,\s*head_item,', 'does not go through the shared switch (guard and restore live there)'),
        ]:
            if not re.search(pat, cv):
                problems.append(f'converge_to_canonical_sibling {why}')

    cs = method(_masked(db_cpp), 'fc::optional<block_id_type> database::canonical_sibling_id(')
    if cs is None:
        problems.append('database::canonical_sibling_id() is missing')
    else:
        cs = _masked(cs)
        for pat, why in [
            (r'num\s*<=\s*dpo\.last_irreversible_block_num\)\s*return', 'ranked a fork at or below LIB'),
            (r'item->data\.previous\s*!=\s*head_item->data\.previous', 'ranked a block that is not a sibling of head'),
            (r'item->invalid\s*\|\|\s*item->anchor_only', 'ranked an invalid or anchor-only entry'),
            (r'if\s*\(n\s*<=\s*head_count\)\s*continue;', 'switches on a tie: without a strict lead the answer depends on which siblings a node happens to know'),
            (r'set\.insert\(v\.first\)', 'counted the same validator twice (a rebuild would outrank a real majority)'),
            (r'_block_votes_mutex', 'read the vote tallies without their mutex'),
            (r'own->block_id\s*==\s*head_id\)\s*return\s+fc::optional<block_id_type>\(\)',
             'ranks a CERTIFIED head against siblings: a re-vote in a later round could pull the node off a block the network already certified'),
        ]:
            if not re.search(pat, cs):
                problems.append(f'canonical_sibling_id {why}')

    # 18.11.2026: entering emergency with ten keyed validators blanked every key and left the
    # chain unable to certify anything. The entry must refuse when the exit threshold is out of
    # reach, and the refusal must be written down.
    ea = method(_masked(db_cpp), 'bool database::emergency_entry_allowed(')
    if ea is None:
        problems.append('database::emergency_entry_allowed() is missing')
    else:
        ea = _masked(ea)
        for pat, why in [
            (r'emergency_exit_threshold_effective\(', 'does not compare the keyed count with the exit threshold'),
            (r'keyed\s*>=\s*required', 'never refuses entry'),
        ]:
            if not re.search(pat, ea):
                problems.append(f'emergency_entry_allowed {why}')
        # The refusal has to be readable in the log; _masked blanks string literals, so this one
        # reads the raw source.
        if 'Emergency consensus REFUSED' not in db_cpp:
            problems.append('emergency_entry_allowed refuses entry silently')
    if not re.search(r'CHAIN_EMERGENCY_CONSENSUS_TIMEOUT_SEC\s*&&\s*emergency_entry_allowed\(\)', _masked(db_cpp)):
        problems.append('the emergency entry site does not consult emergency_entry_allowed()')

    ms = method(_masked(db_cpp), 'bool database::make_sibling_head(')
    if ms is None:
        problems.append('database::make_sibling_head() is missing')
    else:
        ms = _masked(ms)
        for pat, why in [
            (r'_fork_db\.set_head\(sib\)', 'fork_db head is left on the parent after the switch'),
            (r'apply_block\(head_item->data', 'a failed sibling does not restore the previous head'),
            (r'pop_block\(\);\s*if\s*\(head_block_id\(\)\s*==\s*head_item->id\)\s*\{[^}]*_fork_db\.set_head\(head_item\);\s*return\s+false;',
             'a no-progress pop_block() (committed/empty undo) goes on to apply the sibling on the old state'),
        ]:
            if not re.search(pat, ms):
                problems.append(f'make_sibling_head {why}')

    if not re.search(r'const\s+auto\s+own\s*=\s*get_block_qc\(new_head->num\);\s*'
                     r'return\s+own\s*&&\s*own->block_id\s*==\s*new_head->id;', _masked(db_cpp)):
        problems.append('a head holding its own QC is still displaced by a sibling carrying a later certificate')
    return problems


MUTATIONS = {
    'no-call': ('if (sibling_qc) db.with_strong_write_lock([&]() { db.switch_head_to_certified_sibling(); });', ''),
    'no-reverify': ('if (verify_block_vote(qc->block_num, qc->block_id, qc->round, sv.first, sv.second)) ++ok;', '++ok;'),
    'no-lib-floor': ('if (num <= dpo.last_irreversible_block_num) return false;', ''),
    'not-sibling': ('|| sib->data.previous != head_item->data.previous', ''),
    'no-own-qc-guard': ('return own && own->block_id == new_head->id;', 'return false;'),
    'no-restore': ('apply_block(head_item->data, skip_nothing);', ''),
    'no-pop-guard': ('if (head_block_id() == head_item->id) {', 'if (false) {'),
    'converge-no-call': ('db.converge_to_canonical_sibling(); });', '});'),
    'converge-tie-moves': ('if (n <= head_count) continue;', ''),
    'converge-off-certified-head': ('if (own && own->block_id == head_id) return fc::optional<block_id_type>();', ''),
    'converge-not-sibling': ('if (item->num != num || item->data.previous != head_item->data.previous) continue;', ''),
    'converge-counts-twice': ('set.insert(v.first);', 'set.insert(account_name_type(std::to_string(set.size())));'),
    'emergency-guard-removed': ('&& emergency_entry_allowed()', ''),
    'emergency-never-refuses': ('if (keyed >= required) return true;', 'return true;'),
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
    print('PASS: a certified sibling of a QC-less head becomes head before any TC rebuild')
    return 0


if __name__ == '__main__':
    sys.exit(main())
