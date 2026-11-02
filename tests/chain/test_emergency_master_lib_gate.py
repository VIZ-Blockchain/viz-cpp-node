#!/usr/bin/env python3
"""Wiring of the emergency-master peer-LIB gate: source-level regression, no live node.

On 02.11.2026 api.viz.world — the only node holding the emergency (committee) key — produced
its own blocks for ~18 minutes on a branch the network had already left, and the public API
served that minority fork. Nothing in the node could see it:

  * the "last N blocks are all ours" isolation scan is skipped FOR the master by design —
    every block on its tip really is its own, so the scan would fire on a healthy master;
  * the stalled-sync escalation keys on a head that does NOT move, and a producing master
    moves its head every slot.

Peers are the only witnesses left, and the one part of a peer's claim that cannot be
invented without two thirds of the validators is its last-irreversible block. The gate is
therefore built from three pieces that must stay wired exactly like this:

  1. the P2P node keeps a lock-free cache of how many ACTIVE peers report a LIB above ours.
     A peer that disconnects or is removed must stop counting in the same breath — a stale
     claim from a departed peer would block the master forever. The cache is refreshed on
     hello, on every fork_status and after every applied block (our LIB moves too).
  2. the production thread reads that cache without a p2p-thread hop: posting to the P2P
     thread inside a slot would put the block behind the network thread it is meant to
     outrun, and waiting on it from that thread is a self-deadlock.
  3. the master branch consults it BEFORE signing a committee block, requires a quorum of
     peers (one stale snapshot is not evidence) AND a peer LIB strictly above ours, and
     answers with minority_fork so the node stops extending its own tip. A head that keeps
     coming back after the rewind escalates to a fresh snapshot, guarded like every other
     escalation: a missing callback must not throw inside block processing.

Run: python3 tests/chain/test_emergency_master_lib_gate.py [--selftest]
     python3 tests/chain/test_emergency_master_lib_gate.py --baseline origin/master
"""
import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
NODE_HPP = ROOT / 'libraries' / 'network' / 'include' / 'graphene' / 'network' / 'dlt_p2p_node.hpp'
NODE_CPP = ROOT / 'libraries' / 'network' / 'dlt_p2p_node.cpp'
P2P_HPP = ROOT / 'plugins' / 'p2p' / 'include' / 'graphene' / 'plugins' / 'p2p' / 'p2p_plugin.hpp'
P2P_CPP = ROOT / 'plugins' / 'p2p' / 'p2p_plugin.cpp'
VALIDATOR_CPP = ROOT / 'plugins' / 'validator' / 'validator.cpp'

FILES = {
    'node_hpp': NODE_HPP,
    'node_cpp': NODE_CPP,
    'p2p_hpp': P2P_HPP,
    'p2p_cpp': P2P_CPP,
    'validator_cpp': VALIDATOR_CPP,
}

MUTATIONS = {
    'getter-walks-the-peer-map': ('node_hpp', [
        ('v.peers_above_our_lib = _peers_above_our_lib.load(std::memory_order_acquire);',
         'v.peers_above_our_lib = (uint32_t)_peer_states.size();')]),
    'cache-counts-disconnected-peers': ('node_cpp', [
        ('if (state.lifecycle_state != DLT_PEER_LIFECYCLE_ACTIVE) continue;', '')]),
    'cache-counts-silent-peers': ('node_cpp', [
        ('if (state.peer_lib_num == 0) continue;  // peer advertised nothing yet', '')]),
    'cache-not-refreshed-on-hello': ('node_cpp', [
        ('refresh_peer_lib_cache();  // hello carries the peer\'s first LIB claim', '')]),
    'cache-not-refreshed-on-fork-status': ('node_cpp', [
        ('refresh_peer_lib_cache();  // the peer just moved its claim; the master gate reads this',
         '')]),
    'cache-not-refreshed-on-block': ('node_cpp', [
        ('    // Our LIB may have moved with this block: a peer that was "above our LIB" a\n'
         '    // slot ago may no longer be, and a stale count would keep the emergency\n'
         '    // master from producing for no reason.\n'
         '    refresh_peer_lib_cache();', '')]),
    'plugin-hops-onto-the-p2p-thread': ('p2p_cpp', [
        ('    return my && my->node ? my->node->get_peer_lib_view()\n'
         '                          : graphene::network::dlt_p2p_node::peer_lib_view();',
         '    return my->p2p_thread.async([this]() {\n'
         '        return my->node ? my->node->get_peer_lib_view()\n'
         '                         : graphene::network::dlt_p2p_node::peer_lib_view();\n'
         '    }).wait();')]),
    'gate-ignores-quorum': ('validator_cpp', [
        ('peer_libs.peers_above_our_lib >= EMERGENCY_PEER_LIB_QUORUM &&',
         'peer_libs.peers_above_our_lib >= 1 &&')]),
    'gate-ignores-lib-comparison': ('validator_cpp', [
        ('peer_libs.best_peer_lib > peer_libs.our_lib', 'true')]),
    'gate-does-not-stop-production': ('validator_cpp', [
        ('                            return block_validation_condition::minority_fork;\n'
         '                        }',
         '                            return block_validation_condition::not_time_yet;\n'
         '                        }')]),
    'recovery-started-still-resyncs': ('validator_cpp', [
        ('                                    _minority_fork_recovering = true;\n'
         '                                    _minority_fork_recovery_start = fc::time_point::now();\n'
         '                                    return block_validation_condition::minority_fork;\n',
         '')]),
    'gate-reads-the-cache-never': ('validator_cpp', [
        ('const auto peer_libs = p2p().get_peer_lib_view();',
         'const graphene::network::dlt_p2p_node::peer_lib_view peer_libs;')]),
    'escalation-callback-unguarded': ('validator_cpp', [
        ('                                    if (chain().snapshot_force_recovery_callback)\n'
         '                                        recovery_started = chain().snapshot_force_recovery_callback();',
         '                                    recovery_started = chain().snapshot_force_recovery_callback();')]),
    'escalation-never-resets': ('validator_cpp', [
        ('                                _master_gate_escalated = false;\n', '')]),
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


def enclosing_if(source, pos):
    """The whole `if (...) { ... }` statement whose body contains `pos` (condition included)."""
    masked = _masked(source)
    start = masked.rfind('if (', 0, pos)
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
                return source[start:i + 1]
    return None


def wiring_problems(**files):
    problems = []

    # --- 1. The P2P node owns a cache built from ACTIVE peers' LIB claims ---------------
    hpp = files['node_hpp']
    if 'struct peer_lib_view' not in hpp:
        problems.append('dlt_p2p_node has no peer_lib_view: the witness thread has nothing to '
                        'read without touching the peer map it does not own')
    getter = method(hpp, 'peer_lib_view get_peer_lib_view() const')
    if getter is None:
        problems.append('dlt_p2p_node::get_peer_lib_view() is missing: the gate cannot read '
                        'the corroboration from the production thread')
    else:
        g = _masked(getter)
        for field in ('_peers_above_our_lib', '_best_peer_lib', '_cached_our_lib'):
            if field not in g:
                problems.append('get_peer_lib_view() does not read %s: the cache would report '
                                'a stale or empty answer' % field)
        if '_peer_states' in g:
            problems.append('get_peer_lib_view() walks _peer_states: that map belongs to the P2P '
                            'thread, so the production thread would read it without a lock')
        if 'std::memory_order' not in g:
            problems.append('get_peer_lib_view() reads the atomics without an ordering: the '
                            'witness thread may see a peer count from before our LIB moved')

    cpp = files['node_cpp']
    refresh = method(cpp, 'void dlt_p2p_node::refresh_peer_lib_cache()')
    if refresh is None:
        problems.append('dlt_p2p_node::refresh_peer_lib_cache() is missing: nothing updates the '
                        'cache the gate reads')
    else:
        r = _masked(refresh)
        if 'DLT_PEER_LIFECYCLE_ACTIVE' not in r:
            problems.append('refresh_peer_lib_cache() counts peers that are not ACTIVE: a peer '
                            'that dropped keeps blocking the master with a claim it can no longer back')
        if 'peer_lib_num' not in r:
            problems.append('refresh_peer_lib_cache() does not use the peers\' LIB claims: a head '
                            'can be one node\'s invention, a LIB cannot')
        if not re.search(r'peer_lib_num\s*==\s*0', r):
            problems.append('refresh_peer_lib_cache() treats a peer that has not advertised a LIB '
                            'yet as a witness: a zero claim must not enter the count or the best-'
                            'peer LIB, or an unready peer would speak for the network')
        for field in ('_peers_above_our_lib', '_best_peer_lib', '_cached_our_lib'):
            if field + '.store(' not in r:
                problems.append('refresh_peer_lib_cache() never stores %s' % field)

    # Refreshed wherever a claim can change, and nowhere else in a way that skips it.
    for signature, label in (
            ('void dlt_p2p_node::on_dlt_hello(', 'hello'),
            ('void dlt_p2p_node::on_dlt_fork_status(', 'fork_status'),
            ('void dlt_p2p_node::handle_disconnect(', 'disconnect'),
            ('void dlt_p2p_node::on_block_applied(', 'applied block')):
        body = method(cpp, signature)
        if body is None:
            problems.append('%s is missing from dlt_p2p_node: its refresh of the peer-LIB cache '
                            'cannot be verified' % signature)
        elif 'refresh_peer_lib_cache()' not in _masked(body):
            problems.append('%s does not refresh the peer-LIB cache: the master gate keeps a '
                            'stale answer (%s)' % (signature, label))

    # --- 2. The plugin exposes it without posting to the P2P thread --------------------
    if 'get_peer_lib_view' not in files['p2p_hpp']:
        problems.append('p2p_plugin does not declare get_peer_lib_view(): the validator cannot '
                        'reach the cache at all')
    impl = method(files['p2p_cpp'], 'graphene::network::dlt_p2p_node::peer_lib_view p2p_plugin::get_peer_lib_view() const')
    if impl is None:
        problems.append('p2p_plugin::get_peer_lib_view() is missing or has another signature')
    else:
        i = _masked(impl)
        if 'p2p_thread.async' in i or '.wait()' in i:
            problems.append('p2p_plugin::get_peer_lib_view() posts to the P2P thread and waits: '
                            'called from the production loop it would stand behind the network '
                            'thread it must outrun (and wait on itself if that thread is the caller)')
        if 'get_peer_lib_view()' not in i:
            problems.append('p2p_plugin::get_peer_lib_view() does not forward to the node')

    # --- 3. The master branch consults it before signing a committee block -------------
    val = files['validator_cpp']
    val_masked = _masked(val)
    if 'EMERGENCY_PEER_LIB_QUORUM' not in val_masked:
        problems.append('the validator has no EMERGENCY_PEER_LIB_QUORUM: one peer\'s claim would '
                        'be enough to stop the master')
    gate_at = val_masked.find('p2p().get_peer_lib_view()')
    if gate_at < 0:
        problems.append('the validator never reads p2p().get_peer_lib_view(): the emergency master '
                        'still has no way to learn the network went elsewhere')
    master_at = val_masked.find('if (!we_are_master)')
    skipped_at = val.find('DLT minority fork check SKIPPED - we are emergency master')
    if master_at < 0 or skipped_at < 0:
        problems.append('the DLT emergency master branch is gone: this gate belongs to it and '
                        'cannot be verified against it')
    elif gate_at >= 0 and not (master_at < gate_at < skipped_at):
        problems.append('the peer-LIB gate is not inside the emergency master branch of the DLT '
                        'fork check: a slave already has the isolation scan, the master does not')

    cond_at = val.find('peer_libs.peers_above_our_lib')
    gate_block = enclosing_if(val, cond_at) if cond_at >= 0 else None
    if cond_at >= 0 and gate_block is None:
        problems.append('the peer-LIB gate is not inside an if-statement: nothing conditions '
                        'production on the peers\' claims')
    if gate_block is not None:
        gb = _masked(gate_block)
        if 'EMERGENCY_PEER_LIB_QUORUM' not in gb:
            problems.append('the gate does not require a quorum of peers: a single stale peer '
                            'snapshot could stop the only producer on the network')
        if not re.search(r'best_peer_lib\s*>\s*peer_libs\.our_lib', gb):
            problems.append('the gate accepts any peer claim: it must require a LIB strictly above '
                            'ours, otherwise a peer that is merely level with us stops production')
        if not re.search(r'return\s+block_validation_condition::minority_fork;\s*\}\s*$', gb):
            problems.append('the gate does not end by answering minority_fork: production continues '
                            'on the fork the gate just detected')
        esc_at = gb.find('if (recovery_started)')
        if esc_at >= 0:
            branch = enclosing_if(gb, esc_at + len('if (recovery_started)'))
            if branch is None or 'block_validation_condition::minority_fork' not in _masked(branch):
                problems.append('after a forced snapshot recovery starts the gate still falls '
                                'through to resync_from_lib: the snapshot plugin is replacing the '
                                'state, and a rewind racing it can throw the replacement away '
                                '(the slave path above returns first)')
        if 'snapshot_force_recovery_callback' not in gb:
            problems.append('the gate never escalates to a fresh snapshot: a head that comes back '
                            'after the rewind keeps the node on the dead fork forever')
        elif not re.search(r'if\s*\(\s*chain\(\)\.snapshot_force_recovery_callback\s*\)', gb):
            problems.append('the escalation calls snapshot_force_recovery_callback() unguarded: an '
                            'empty std::function throws bad_function_call inside block processing')
        if '_master_gate_escalated = false;' not in gb:
            problems.append('a new stuck head does not reset _master_gate_escalated: one fork would '
                            'spend the snapshot escalation for the lifetime of the process')

    return problems


def load():
    return {key: path.read_text() for key, path in FILES.items()}


def load_baseline(ref):
    out = {}
    for key, path in FILES.items():
        rel = path.relative_to(ROOT)
        out[key] = subprocess.run(['git', '-C', str(ROOT), 'show', '%s:%s' % (ref, rel)],
                                  capture_output=True, text=True, check=True).stdout
    return out


def selftest():
    files = load()
    failures = []
    for name, (key, pairs) in MUTATIONS.items():
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
    parser.add_argument('--baseline', metavar='REF',
                        help='run the checks against another git ref (e.g. origin/master) and '
                             'require them to FIND something -- a check that cannot redden the '
                             'code it was written for proves nothing')
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    if args.baseline:
        problems = wiring_problems(**load_baseline(args.baseline))
        if not problems:
            print('BASELINE %s is GREEN: these checks do not describe anything missing there, so '
                  'they cannot be guarding the change' % args.baseline)
            return 1
        print('BASELINE %s is RED as expected (%d findings)' % (args.baseline, len(problems)))
        return 0
    problems = wiring_problems(**load())
    if problems:
        for p in problems:
            print('FINDING: %s' % p)
        return 1
    print('PASS: the peer-LIB cache counts only active peers and is refreshed on every claim '
          'change; the production thread reads it without a P2P-thread hop; the emergency master '
          'refuses to extend a tip whose LIB the network has finalised past, and escalates to a '
          'fresh snapshot when a rewind keeps returning to the same head')
    return 0


if __name__ == '__main__':
    sys.exit(main())
