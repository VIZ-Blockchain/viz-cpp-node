#!/usr/bin/env python3
"""Wiring of the three state-integrity guards: source-level regression, no live node.

The three guards are about WHERE code sits, not what a formula computes, so every way of
breaking them is a one-line move that still compiles and still passes every other test:

  1. the partial-state marker is written in each catch handler that throws
     snapshot_import_left_partial_state, cleared ONLY after the import completed, and read back
     in plugin_startup() BEFORE the state is opened -- the marker is the only thing that tells a
     restart "this state is half an import, refuse it". Written after the throw it never lands;
     cleared on the failure path it turns the refusal into a silent resume; checked after the
     database is opened it is checked too late.
  2. the cursor check `next_id > max(id)` runs at both doors that can leave a cursor behind: at
     the end of the import and when an existing database is opened. Dropping either call leaves
     a node that can hand out an id it already holds.
  3. the DLT minority-fork escalation asks the SNAPSHOT plugin for a trusted-peer download.
     A node's own periodic snapshots are behind its head by construction, so importing a local
     one replays dlt_block_log back into the same fork -- the first version of this guard did
     exactly that and was decorative. The escalation must reach a fresh snapshot, must bypass
     the P2P-first stage (the validator already ran it), and must not be held back by the
     emergency-mode wait. request_forced_recovery() must also start the stalled-sync thread,
     because our nodes run with --enable-stalled-sync-detection=false.

Run: python3 tests/chain/test_state_integrity_wiring.py [--selftest]
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SNAPSHOT_CPP = ROOT / 'plugins' / 'snapshot' / 'plugin.cpp'
CHAIN_PLUGIN_HPP = ROOT / 'plugins' / 'chain' / 'include' / 'graphene' / 'plugins' / 'chain' / 'plugin.hpp'
VALIDATOR_CPP = ROOT / 'plugins' / 'validator' / 'validator.cpp'

FILES = {
    'snapshot_cpp': SNAPSHOT_CPP,
    'chain_plugin_hpp': CHAIN_PLUGIN_HPP,
    'validator_cpp': VALIDATOR_CPP,
}

MUTATION_FILE = {
    'marker-not-written': 'snapshot_cpp',
    'marker-not-cleared': 'snapshot_cpp',
    'marker-not-checked-at-startup': 'snapshot_cpp',
    'no-quit-on-partial-state': 'snapshot_cpp',
    'no-cursor-check-at-startup': 'snapshot_cpp',
    'no-cursor-check-after-import': 'snapshot_cpp',
    'callback-not-registered': 'snapshot_cpp',
    'forced-does-not-start-thread': 'snapshot_cpp',
    'no-forced-bypass-of-timeout': 'snapshot_cpp',
    'no-forced-bypass-of-p2p-stage': 'snapshot_cpp',
    'no-forced-bypass-of-emergency-wait': 'snapshot_cpp',
    'escalation-not-asked': 'validator_cpp',
}

MUTATIONS = {
    'marker-not-written': [('detail::mark_partial_state();', '', )],
    'marker-not-cleared': [('detail::clear_partial_state_marker();', '')],
    'marker-not-checked-at-startup': [
        ('if (fc::exists(detail::partial_state_marker())) {', 'if (false) {')],
    'no-quit-on-partial-state': None,  # callable, see below
    'no-cursor-check-at-startup': [('detail::verify_all_index_cursors(my->db, "on startup");', '')],
    'no-cursor-check-after-import': [
        ('detail::verify_all_index_cursors(db, "after snapshot import");', '')],
    'callback-not-registered': [('chain_plug.snapshot_force_recovery_callback = [this]() {', 'auto unused = [this]() {')],
    'forced-does-not-start-thread': None,  # callable, see below
    'no-forced-bypass-of-timeout': [('elapsed > timeout || forced', 'elapsed > timeout')],
    'no-forced-bypass-of-p2p-stage': [
        ('!_p2p_recovery_attempted && !forced', '!_p2p_recovery_attempted')],
    'no-forced-bypass-of-emergency-wait': [('emergency && !forced', 'emergency')],
    'escalation-not-asked': [('recovery_started = chain().snapshot_force_recovery_callback();',
                              'recovery_started = false;')],
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


def _drop(signature, needle):
    """Mutation: remove the first `needle` from the body of `signature` (not from some other
    function that happens to contain the same statement)."""
    def mutate(source):
        body = method(source, signature)
        if body is None or needle not in body:
            return None
        return source.replace(body, body.replace(needle, '', 1), 1)
    return mutate


MUTATIONS['no-quit-on-partial-state'] = _drop(
    'void snapshot_plugin::plugin_startup()', 'appbase::app().quit();')
MUTATIONS['forced-does-not-start-thread'] = _drop(
    'bool snapshot_plugin::request_forced_recovery()', 'my->start_stalled_sync_detection();')


def guarded_block(source, needle):
    """Body of the `if (...)` statement whose condition contains `needle`, brace matched."""
    masked = _masked(source)
    start = masked.find(needle)
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


def load():
    return {key: path.read_text() for key, path in FILES.items()}


def wiring_problems(**files):
    problems = []
    snap = files['snapshot_cpp']
    snap_masked = _masked(snap)

    import_body = method(snap, 'void snapshot_plugin::plugin_impl::load_snapshot(')
    if import_body is None:
        return ['snapshot_plugin::plugin_impl::load_snapshot() is missing -- that is the one place '
                'that clears the state and may leave it partial']
    import_masked = _masked(import_body)

    # --- 1a. The marker is written before every "partial state" throw -------------------
    throws = re.findall(r'FC_THROW_EXCEPTION\(\s*snapshot_import_left_partial_state', import_masked)
    if len(throws) != 3:
        problems.append('expected 3 snapshot_import_left_partial_state throws (fc::exception, '
                        'std::exception, unknown) in load_snapshot(), found %d: one handler can '
                        'leave a cleared state without saying so' % len(throws))
    for m in re.finditer(r'FC_THROW_EXCEPTION\(\s*snapshot_import_left_partial_state', import_masked):
        before = import_masked[max(0, m.start() - 400):m.start()]
        if 'mark_partial_state()' not in before:
            problems.append('a snapshot_import_left_partial_state throw is not preceded by '
                            'detail::mark_partial_state(): the next start of the node would open '
                            'the half state and look healthy')
            break

    # --- 1b. The marker is cleared only after the import really finished ---------------
    success_at = import_body.find('All objects imported successfully')
    cleared_at = import_masked.find('clear_partial_state_marker()')
    if cleared_at < 0:
        problems.append('load_snapshot() never clears the partial-state marker: the first failed '
                        'import would refuse every later start')
    elif success_at < 0:
        problems.append('the success log is gone from load_snapshot(): cannot place the marker '
                        'clear relative to a completed import')
    elif cleared_at < success_at:
        problems.append('the partial-state marker is cleared BEFORE the import completed: a failed '
                        'import would leave no evidence behind')

    # --- 1c. Startup refuses a marker it did not earn --------------------------------
    startup_body = method(snap, 'void snapshot_plugin::plugin_startup()')
    if startup_body is None:
        problems.append('snapshot_plugin::plugin_startup() is missing -- that is where the marker '
                        'must be read before the state is used')
    else:
        startup_body_masked = _masked(startup_body)
        marker_at = startup_body_masked.find('fc::exists(detail::partial_state_marker())')
        marker_block = guarded_block(startup_body, 'fc::exists(detail::partial_state_marker())')
        if marker_at < 0:
            problems.append('plugin_startup() does not check the partial-state marker: a restart '
                            'after a failed import opens half a state and resumes')
        elif marker_block is None:
            problems.append('the partial-state marker is tested outside an if-block: nothing is '
                            'refused by the check')
        else:
            marker_block_masked = _masked(marker_block)
            if 'state_will_be_replaced' not in marker_block_masked:
                problems.append('plugin_startup() checks the marker but never decides whether this run '
                                'replaces the state: it would refuse the legitimate recovery import too')
            if 'appbase::app().quit()' not in marker_block_masked:
                problems.append('plugin_startup() logs the partial state but does not quit: the node '
                                'continues to produce blocks on it')
        cursor_at = startup_body_masked.find('verify_all_index_cursors(my->db, ')
        if cursor_at < 0:
            problems.append('plugin_startup() does not verify the index cursors of a normally opened '
                            'database: a state written by an older build can hand out an id it already '
                            'holds, and the damage appears at the first create()')
        elif marker_at >= 0 and marker_at > cursor_at:
            problems.append('the partial-state check runs AFTER the cursor check and the snapshot load '
                            'trigger: half a state is opened and measured before it is refused')

    # --- 2. The cursor check runs at both doors ----------------------------------------
    if 'verify_all_index_cursors(db, ' not in import_masked:
        problems.append('load_snapshot() does not verify the cursors after the import: a snapshot '
                        'whose ids are not strictly ascending leaves next_id behind max(id) and the '
                        'next create() collides or overwrites an imported object')

    # --- 3a. The chain plugin carries the callback ------------------------------------
    chain_hpp = _masked(files['chain_plugin_hpp'])
    if 'std::function<bool()> snapshot_force_recovery_callback' not in chain_hpp:
        problems.append('chain::plugin has no snapshot_force_recovery_callback: the validator has '
                        'no way to ask for a fresh snapshot and falls back to a local one, which '
                        'cannot repair a fork')

    # --- 3b. The snapshot plugin registers and implements it --------------------------
    if 'snapshot_force_recovery_callback = ' not in snap_masked:
        problems.append('the snapshot plugin never registers snapshot_force_recovery_callback: the '
                        'validator sees an empty function and the escalation dies at the first hop')
    recover_body = method(snap, 'bool snapshot_plugin::request_forced_recovery()')
    if recover_body is None:
        problems.append('snapshot_plugin::request_forced_recovery() is missing -- the callback has '
                        'nothing to call')
    else:
        recover = _masked(recover_body)
        if 'forced_recovery_requested.store(true' not in recover:
            problems.append('request_forced_recovery() does not arm the forced flag: the stalled-sync '
                            'loop would wait out its full timeout before acting')
        if 'start_stalled_sync_detection()' not in recover:
            problems.append('request_forced_recovery() does not start the stalled-sync thread: our '
                            'nodes run with --enable-stalled-sync-detection=false, so on them nothing '
                            'would ever consume the request')
        if 'trusted_snapshot_peers.empty()' not in recover:
            problems.append('request_forced_recovery() does not refuse when there are no trusted peers: '
                            'it would report a recovery it cannot perform')

    # --- 3c. The loop honours the request and skips what it must skip ------------------
    loop_body = method(snap, 'void snapshot_plugin::plugin_impl::check_stalled_sync_loop()')
    if loop_body is None:
        problems.append('check_stalled_sync_loop() is missing -- the forced request would never be '
                        'consumed')
    else:
        loop = _masked(loop_body)
        if 'forced_recovery_requested.exchange(' not in loop:
            problems.append('check_stalled_sync_loop() never consumes the forced flag: it stays set '
                            'and every later pass behaves as forced')
        if not re.search(r'elapsed\s*>\s*timeout\s*\|\|\s*forced', loop):
            problems.append('the forced request does not bypass the stalled-sync timeout: the '
                            'escalation would wait for a timer it was built to avoid')
        if not re.search(r'!\s*_p2p_recovery_attempted\s*&&\s*!\s*forced', loop):
            problems.append('the forced request does not skip the P2P-first stage: the validator '
                            'already ran that stage, so the escalation spends another cycle on it')
        if not re.search(r'emergency\s*&&\s*!\s*forced', loop):
            problems.append('the forced request is still held back by the emergency-mode wait: on a '
                            'forked chain in emergency the state is never replaced')

    # --- 3d. The validator escalates, once, after a measured stall --------------------
    val = _masked(files['validator_cpp'])
    if 'FORK_ESCALATION_SEC' not in val:
        problems.append('the validator has no FORK_ESCALATION_SEC: the escalation fires on the first '
                        'loop or on a wall-clock constant nobody can tune')
    if not re.search(r'to_seconds\(\)\s*>=\s*FORK_ESCALATION_SEC', val):
        problems.append('the escalation is not gated on the head staying stuck for FORK_ESCALATION_SEC: '
                        'a normal reorg would replace the state')
    if 'snapshot_force_recovery_callback()' not in val:
        problems.append('the validator never calls snapshot_force_recovery_callback(): the escalation '
                        'has no path to a fresh snapshot')
    if 'if (chain().snapshot_force_recovery_callback)' not in val:
        problems.append('the validator calls snapshot_force_recovery_callback() unguarded: an empty '
                        'std::function throws bad_function_call inside block processing')
    if '_fork_escalated = false;' not in val:
        problems.append('a moved head does not reset _fork_escalated: one fork would disable the '
                        'escalation for the lifetime of the process')

    return problems


def selftest():
    files = load()
    failures = []
    for name, pairs in MUTATIONS.items():
        key = MUTATION_FILE[name]
        text = files[key]
        if callable(pairs):
            mutated_text = pairs(text)
            if mutated_text is None:
                failures.append('%s: mutation did not land' % name)
                continue
            text = mutated_text
            landed = True
        else:
            landed = True
        for old, new in (() if callable(pairs) else pairs):
            if old not in text:
                failures.append('%s: anchor not found -- mutation did not land' % name)
                landed = False
                break
            text = text.replace(old, new, 1)
        if not landed:
            continue
        mutated = dict(files)
        mutated[key] = text
        problems = wiring_problems(**mutated)
        if not problems:
            failures.append('%s: stayed GREEN (the check it targets is vacuous)' % name)
        else:
            print('RED as expected: %s (%d)' % (name, len(problems)))
    if failures:
        for f in failures:
            print('SELFTEST FAILURE: %s' % f)
        return 1
    print('PASS: every mutation lands and reddens at least one check')
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
    print('PASS: the partial-state marker is written before each refusal, cleared only after a '
          'completed import and read at startup; the cursor check runs at both doors; the minority-'
          'fork escalation reaches a trusted-peer download, starts its own thread and skips the '
          'stages it was built to skip')
    return 0


if __name__ == '__main__':
    sys.exit(main())
