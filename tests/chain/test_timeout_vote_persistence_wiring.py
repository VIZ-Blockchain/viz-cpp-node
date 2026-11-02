#!/usr/bin/env python3
"""Wiring of the timeout-vote journal into the node: source-level regression, no live node.

tests/chain/test_timeout_vote_journal.py proves the journal itself. This file proves the node
actually connects it, and connects it in the only order that is safe:

  * open() replays the cache; open_from_snapshot() (which wipes the state) resets it instead;
  * a vote is cached only AFTER push_timeout_vote accepted it, and never while replaying;
  * the cache is flushed when a certificate forms, so a crash right after producing does not
    lose the votes that made the block admissible;
  * replay goes through push_timeout_vote -- the file is a cache, never a source of trust;
  * each vote kind is refused on the other side of HF18, so the pool never mixes a v1 vote
    (no block named) with a v2 one (block named) in the same (validator, round) slot.

Run: python3 tests/chain/test_timeout_vote_persistence_wiring.py [--selftest]
"""
import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
DB_CPP = ROOT / 'libraries' / 'chain' / 'database.cpp'
DB_HPP = ROOT / 'libraries' / 'chain' / 'include' / 'graphene' / 'chain' / 'database.hpp'


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


def call_pattern(fn, *args):
    parts = [re.escape(fn), r'\s*\(\s*']
    return ''.join(parts) + ''.join(re.escape(str(a)) + (r'\s*,\s*' if idx < len(args) - 1 else '')
                                     for idx, a in enumerate(args))


def wiring_problems(cpp, hpp):
    problems = []
    open_body = method(cpp, 'void database::open(')
    snapshot_body = method(cpp, 'void database::open_from_snapshot(')
    # push_timeout_vote / _v2 are fork guards; the body that does the work is the impl.
    push_body = method(cpp, 'bool database::push_timeout_vote_impl(')
    v1_body = method(cpp, 'bool database::push_timeout_vote(')
    v2_body = method(cpp, 'bool database::push_timeout_vote_v2(')
    init_body = method(cpp, 'void database::open_timeout_vote_journal(')
    masked_hpp = _masked(hpp)

    if (open_body is None or snapshot_body is None or push_body is None or init_body is None
            or v1_body is None or v2_body is None):
        return ['one of database::open / open_from_snapshot / push_timeout_vote(_v2/_impl) / '
                'open_timeout_vote_journal not found -- the wiring cannot be judged']

    # Regex checks run on comment-blanked copies: a comment that merely mentions a call must not
    # satisfy them (that exact mistake passed a check in PR #221).
    open_m, snapshot_m, push_m, init_m = (_masked(open_body), _masked(snapshot_body),
                                          _masked(push_body), _masked(init_body))
    v1_m, v2_m = _masked(v1_body), _masked(v2_body)

    # HF18 swaps the digest domain (the vote now names a block), so the two kinds can never be
    # mixed. Each entry point must refuse the other fork's votes: a v1 vote after the fork would
    # sit in the pool uncounted, and a v2 vote before it would be counted by nobody.
    guarded = r'if\s*\(\s*{neg}has_hardfork\s*\(\s*CHAIN_HARDFORK_18\s*\)\s*\)\s*return\s+false\s*;'
    if not re.search(guarded.format(neg=''), v1_m):
        problems.append('push_timeout_vote() accepts v1 votes after HF18 (they can never join a certificate)')
    if not re.search(guarded.format(neg='!'), v2_m):
        problems.append('push_timeout_vote_v2() accepts block-naming votes before HF18 (no validator counts them)')
    if 'push_timeout_vote_impl(' not in v1_m or 'push_timeout_vote_impl(' not in v2_m:
        problems.append('a fork guard does not delegate to push_timeout_vote_impl -- the body is not shared')
    if not re.search(r'v2\s*\?\s*std::string\(high_qc_block_id\)\s*:\s*std::string\(\)', push_m):
        problems.append('the journal is not told which kind of vote it caches (block id written for v1, or lost for v2)')

    # The cache must survive a normal start ...
    if not re.search(call_pattern('open_timeout_vote_journal', 'data_dir', 'true'), open_m):
        problems.append('database::open() does not replay the journal (open_timeout_vote_journal(data_dir, true) missing)')
    # ... and must NOT be replayed after a snapshot import wiped the state.
    if not re.search(call_pattern('open_timeout_vote_journal', 'data_dir', 'false'), snapshot_m):
        problems.append('open_from_snapshot() does not reset the journal (replaying votes for a wiped chain)')

    # Only an accepted vote is cached, and only after it entered the pool.
    accept_at = push_body.find('votes.emplace(')
    append_at = push_body.find('_timeout_vote_journal->append(')
    if append_at < 0:
        problems.append('push_timeout_vote() never caches an accepted vote')
    elif accept_at < 0:
        problems.append('push_timeout_vote() no longer accepts votes before caching -- review the order')
    elif append_at < accept_at:
        problems.append('the journal is written BEFORE the vote enters the pool (a rejected or duplicate vote would be cached)')

    # A replayed vote is already on disk; appending it again grows the file on every start.
    if append_at >= 0 and not re.search(
            r'if\s*\(\s*_timeout_vote_journal\s*&&\s*!_timeout_vote_journal_replaying\s*\)', push_m):
        problems.append('append is not guarded by _timeout_vote_journal_replaying (every start would duplicate the file)')

    # Both assembly branches form a certificate, and both must flush: a v2 certificate lost to a
    # crash is the same liveness hole as a v1 one.
    if push_body.count('TC formed for round') < 2:   # a log string: blanked in push_m
        problems.append('only one certificate branch is visible -- the v2 assembly cannot be judged')
    elif push_m.count('_timeout_vote_journal->flush()') < 2:
        problems.append('a certificate branch does not flush the journal (a crash right after producing loses its votes)')

    # A crash right after producing must not lose the votes behind the certificate.
    cert_at = push_body.find('TC formed for round')
    flush_at = push_body.find('_timeout_vote_journal->flush()')
    if cert_at < 0:
        problems.append('push_timeout_vote() no longer logs certificate formation -- locate it before judging')
    elif flush_at < 0:
        problems.append('the journal is not flushed when a certificate forms (a crash right after producing loses the votes)')
    elif flush_at < cert_at:
        problems.append('the journal is flushed before the certificate is formed, not after it')

    # A data dir can outlive its chain: wiping blockchain/ and reopening builds a fresh chain
    # while the journal stays behind. A chain with no blocks has cast no timeout votes, so the
    # file describes another instance and must not seed this one's pool.
    guard_at = init_m.find('head_block_num() == 0')
    replay_at = init_m.find('->replay(')
    if guard_at < 0:
        problems.append('open_timeout_vote_journal() replays without the genesis guard '
                        '(a rebuilt chain would inherit another instance\'s votes)')
    elif replay_at >= 0 and guard_at > replay_at:
        problems.append('the genesis guard sits after the replay -- it cannot prevent it')

    # A cache that cannot be read must never become a startup blocker: replay is contained,
    # and a failed replay leaves the journal detached instead of appending to a half-written file.
    flag_on = init_m.find('_timeout_vote_journal_replaying = true;')
    flag_off = init_m.find('_timeout_vote_journal_replaying = false;')
    if flag_on < 0 or flag_off < flag_on:
        problems.append('replay is not wrapped between the replay flag being set and cleared')
    else:
        guarded = init_m[flag_on:flag_off]
        if 'catch' not in guarded:
            problems.append('an exception from replay escapes into database::open() (a cache could block startup)')
        if 'throw;' in guarded:
            problems.append('the replay catch rethrows instead of containing the failure')
    failed_at = init_m.find('if (replay_failed)')
    attach_at = init_m.rfind('_timeout_vote_journal = std::move(journal);')
    if failed_at < 0:
        problems.append('a failed replay still attaches the journal (append would hit a half-rewritten file)')
    elif attach_at < failed_at:
        problems.append('cannot judge the failed-replay branch: the journal is never attached at all')
    elif 'return;' not in init_m[failed_at:attach_at]:
        problems.append('the failed-replay branch attaches the journal instead of returning')

    # The file is a cache, not a second source of trust: replay runs through push_timeout_vote.
    if 'push_timeout_vote(' not in init_m or 'push_timeout_vote_v2(' not in init_m:
        problems.append('replay does not go through both push_timeout_vote entry points '
                        '(cached votes would skip verification, or one kind would be replayed unverified)')
    if 'high_qc_block_id_hex.empty()' not in init_m:
        problems.append('replay does not choose the vote kind from the line itself (four-field vs five-field)')
    replay_at = init_m.find('->replay(')
    if replay_at >= 0:
        after = init_m[replay_at:]
        if '_timeout_vote_journal_replaying = false;' not in after:
            problems.append('_timeout_vote_journal_replaying is never cleared after replay (the cache would stop growing)')
    else:
        problems.append('open_timeout_vote_journal() never calls replay()')
    for insert in ('_timeout_votes[', '_timeout_votes.emplace', '_timeout_certs.emplace'):
        if insert in init_m:
            problems.append(f'replay inserts into {insert} directly instead of using push_timeout_vote')

    if '_timeout_vote_journal;' not in masked_hpp:
        problems.append('database.hpp has no _timeout_vote_journal member')
    if '_timeout_vote_journal_replaying' not in masked_hpp:
        problems.append('database.hpp has no _timeout_vote_journal_replaying flag')
    if '#include <graphene/chain/timeout_vote_journal.hpp>' not in hpp:
        problems.append('database.hpp does not include the journal header')
    return problems


MUTATIONS = {
    'open-does-not-replay': ('open_timeout_vote_journal(data_dir, /*replay_votes=*/true);',
                             'open_timeout_vote_journal(data_dir, /*replay_votes=*/false);'),
    'snapshot-replays-stale-cache': ('open_timeout_vote_journal(data_dir, /*replay_votes=*/false);',
                                     'open_timeout_vote_journal(data_dir, /*replay_votes=*/true);'),
    'append-before-accept': ('            auto &votes = _timeout_votes[round];\n            if (votes.count(validator)) return false;',
                             '            if (_timeout_vote_journal) _timeout_vote_journal->append(round, high_qc_round, validator, timeout_vote_journal::signature_type{});\n'
                             '            auto &votes = _timeout_votes[round];\n            if (votes.count(validator)) return false;'),
    'append-during-replay': ('if (_timeout_vote_journal && !_timeout_vote_journal_replaying)',
                             'if (_timeout_vote_journal)'),
    'no-flush-on-cert': ('if (_timeout_vote_journal) _timeout_vote_journal->flush();', '(void)nullptr;'),
    'flag-never-cleared': ('_timeout_vote_journal_replaying = false;\n\n            if (!error.empty())',
                           '\n            if (!error.empty())'),
    'replay-bypasses-checks': ('return push_timeout_vote(round, high_qc_round, account_name_type(validator), signature);',
                               'return true;'),
    'v1-accepted-after-fork': ('if (has_hardfork(CHAIN_HARDFORK_18)) return false;', 'if (false) return false;'),
    'v2-accepted-before-fork': ('if (!has_hardfork(CHAIN_HARDFORK_18)) return false;', 'if (false) return false;'),
    'v2-block-id-not-cached': ('v2 ? std::string(high_qc_block_id) : std::string()', 'std::string()'),
    'no-genesis-guard': ('if (head_block_num() == 0) {\n                if (!journal->truncate(error))',
                         'if (head_block_num() < 0) {\n                if (!journal->truncate(error))'),
    'replay-exception-escapes': ('error = "unknown exception";', 'throw;'),
    'failed-replay-attaches': ('elog("Timeout vote journal disabled after a failed replay: ${p}", ("p", path));\n'
                               '                return;',
                               'elog("Timeout vote journal disabled after a failed replay: ${p}", ("p", path));'),
}


def load():
    return DB_CPP.read_text(), DB_HPP.read_text()


def selftest():
    cpp_text, hpp_text = load()
    total = 0
    failures = []
    with __import__('tempfile').TemporaryDirectory(prefix='viz-tvj-wire-') as tmp:
        for name, (old, new) in MUTATIONS.items():
            if old not in cpp_text:
                failures.append(f'{name}: anchor not found -- mutation did not land')
                continue
            mutated = cpp_text.replace(old, new, 1)
            problems = wiring_problems(mutated, hpp_text)
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
    problems = wiring_problems(*load())
    if problems:
        for p in problems:
            print(f'FINDING: {p}')
        return 1
    print('PASS: journal replayed on open, reset on snapshot import, votes cached after acceptance, '
          'flushed on certificate formation, replay verified through push_timeout_vote')
    return 0


if __name__ == '__main__':
    sys.exit(main())
