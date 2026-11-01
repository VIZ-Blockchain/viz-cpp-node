#!/usr/bin/env python3
"""Wiring of the block-QC gossip: source-level regression, no live node.

Mainnet sat 5 h 20 min on head #84865875 (01.11.2026) because the votes that make a QC
travelled over p2p but the QC itself did not: three nodes knew the head was certified and
the producer of the next slot had nothing to extend it with. The fix ships a new message,
and such a message is only useful when SIX places agree -- miss one and nothing happens,
silently:

  1. the type id exists in dlt_message_type_enum AND in its FC_REFLECT_ENUM list (a type the
     enum does not reflect fails to dispatch, and the sender looks like it sends garbage);
  2. dlt_block_qc_message::type is defined in dlt_p2p_messages.cpp (the message wrapper
     builds on it);
  3. hello.capabilities advertises DLT_CAP_BLOCK_QC (a peer that never says it understands
     the message is never sent one);
  4. on_message dispatches the new type;
  5. the p2p delegate hands the certificate to the chain, which re-verifies it (a delegate
     that returns "accepted" without the chain call stores nothing);
  6. the validator re-broadcasts the head QC it holds while the chain is stalled, otherwise
     the certificate that a restart wiped never comes back.

Plus two properties that are safety, not plumbing: the sender only targets capable peers (an
old node scores the packet as unknown against us), and the chain stores the RE-VERIFIED set
of signatures, never the sender's word.

Run: python3 tests/chain/test_qc_gossip_wiring.py [--selftest]
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MSG_HPP = ROOT / 'libraries' / 'network' / 'include' / 'graphene' / 'network' / 'dlt_p2p_messages.hpp'
MSG_CPP = ROOT / 'libraries' / 'network' / 'dlt_p2p_messages.cpp'
NODE_CPP = ROOT / 'libraries' / 'network' / 'dlt_p2p_node.cpp'
P2P_CPP = ROOT / 'plugins' / 'p2p' / 'p2p_plugin.cpp'
DB_CPP = ROOT / 'libraries' / 'chain' / 'database.cpp'
VAL_CPP = ROOT / 'plugins' / 'validator' / 'validator.cpp'

FILES = {'msg_hpp': MSG_HPP, 'msg_cpp': MSG_CPP, 'node_cpp': NODE_CPP,
         'p2p_cpp': P2P_CPP, 'db_cpp': DB_CPP, 'val_cpp': VAL_CPP}


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


def _enum_has(text, name):
    m = re.search(r'enum\s+dlt_message_type_enum\s*\{(.*?)\}', text, re.S)
    return bool(m and re.search(r'\b%s\s*=\s*5119\b' % re.escape(name), m.group(1)))


def wiring_problems(**files):
    problems = []
    msg_hpp = _masked(files['msg_hpp'])
    msg_cpp = _masked(files['msg_cpp'])
    node_cpp = _masked(files['node_cpp'])
    p2p_cpp = _masked(files['p2p_cpp'])
    db_cpp = _masked(files['db_cpp'])
    val_cpp = _masked(files['val_cpp'])

    # 1. Type id present in the enum and in the reflection of that enum.
    if 'dlt_block_qc_message_type' not in msg_hpp:
        problems.append('no dlt_block_qc_message_type in dlt_p2p_messages.hpp -- the message cannot exist')
    else:
        if not _enum_has(files['msg_hpp'], 'dlt_block_qc_message_type'):
            problems.append('dlt_block_qc_message_type is missing from dlt_message_type_enum (or is not 5119)')
        reflect = re.search(r'FC_REFLECT_ENUM\(graphene::network::dlt_message_type_enum,(.*?)\)\)', msg_hpp, re.S)
        if not reflect or 'dlt_block_qc_message_type' not in reflect.group(1):
            problems.append('dlt_block_qc_message_type is absent from the FC_REFLECT_ENUM of the message types: '
                            'the type would never dispatch')

    # 2. Static type constant.
    if not re.search(r'dlt_message_type_enum\s+dlt_block_qc_message::type\s*=\s*dlt_block_qc_message_type\s*;',
                     msg_cpp):
        problems.append('dlt_block_qc_message::type is not defined in dlt_p2p_messages.cpp')

    # 3. Capability: declared and advertised in hello.
    if 'DLT_CAP_BLOCK_QC' not in msg_hpp:
        problems.append('no DLT_CAP_BLOCK_QC capability bit -- peers cannot say they understand the message')
    hello = re.search(r'hello\.capabilities\s*=\s*([^;]+);', node_cpp)
    if not hello:
        problems.append('hello.capabilities assignment not found -- cannot judge what we advertise')
    elif 'DLT_CAP_BLOCK_QC' not in hello.group(1):
        problems.append('hello.capabilities does not advertise DLT_CAP_BLOCK_QC: capable peers will never be '
                        'told we can take the message, so no QC would ever be sent to us')

    # 4. Dispatch.
    if not re.search(r'case\s+dlt_block_qc_message_type\s*:\s*on_dlt_block_qc\s*\(', node_cpp):
        problems.append('on_message has no dispatch for dlt_block_qc_message_type -- incoming QCs are dropped as '
                        'unknown packets and the peer is scored down')

    # 5a. The chain call behind the delegate.
    accept = method(p2p_cpp, 'bool accept_block_qc(')
    if accept is None:
        problems.append('p2p_plugin implements no accept_block_qc() -- the node never accepts a gossiped QC')
    elif 'accept_block_qc_certificate(' not in _masked(accept):
        problems.append('accept_block_qc() does not reach database::accept_block_qc_certificate(): the message '
                        'would be relayed while nothing is ever stored')

    # 5b. Only capable peers are sent the message.
    broadcast = method(node_cpp, 'void dlt_p2p_node::broadcast_block_qc(')
    if broadcast is None:
        problems.append('dlt_p2p_node::broadcast_block_qc() is missing -- a held QC cannot be re-sent')
    elif not re.search(r'gossip_to_capable\([^;]*DLT_CAP_BLOCK_QC', _masked(broadcast)):
        problems.append('broadcast_block_qc() does not restrict itself to DLT_CAP_BLOCK_QC peers '
                        '(a pre-HF node scores the unknown packet against us)')
    relay = method(node_cpp, 'void dlt_p2p_node::process_block_qc(')
    if relay is None:
        problems.append('dlt_p2p_node::process_block_qc() is missing -- an accepted QC would stop at us')
    elif 'accept_block_qc(' not in _masked(relay):
        problems.append('process_block_qc() stores nothing: the delegate is never asked to verify the certificate')

    # 6. The chain verifies instead of trusting the sender.
    chain_accept = method(db_cpp, 'bool database::accept_block_qc_certificate(')
    if chain_accept is None:
        problems.append('database::accept_block_qc_certificate() is missing -- nothing can verify a gossiped QC')
    else:
        body = _masked(chain_accept)
        if not re.search(r'fetch_block_by_number\(\s*cert\.block_num\s*\)', body):
            problems.append('accept_block_qc_certificate() does not look the certified height up: a QC for a '
                            'block we do not have (or for another fork) would be stored')
        elif not re.search(r'block->id\(\)\s*!=\s*cert\.block_id', body):
            problems.append('accept_block_qc_certificate() never compares the block id it found with the one the '
                            'sender certified (a QC for a competing block at the same height would be taken)')
        if 'round_of(' not in body:
            problems.append('accept_block_qc_certificate() does not tie the QC round to the block slot')
        if 'verify_block_vote(' not in body:
            problems.append('accept_block_qc_certificate() stores WITHOUT verifying the signatures -- the sender '
                            'would be an authority')
        if 'block_qc_quorum(' not in body:
            problems.append('accept_block_qc_certificate() does not check a quorum')
        if not re.search(r'remember_block_qc\(\s*verified\s*\)', body):
            problems.append('accept_block_qc_certificate() does not store the RE-VERIFIED certificate '
                            '(remember_block_qc(cert) would keep the sender\'s unverified signatures)')

    # 7. The node that holds a QC re-sends it while the chain is stalled.
    if 'post_broadcast_block_qc' not in val_cpp:
        problems.append('the validator plugin never broadcasts a block QC: a head certified by a quorum stays a '
                        'private fact of the nodes that collected the votes (the 01.11 stall, verbatim)')
    elif not re.search(r'if\s*\(\s*certified\s*&&\s*now_round\s*>=\s*head_round[^)]*\)\s*\{[^}]*'
                       r'post_broadcast_block_qc', val_cpp, re.S):
        problems.append('the block-QC broadcast is not guarded by "certified && the chain has moved on": '
                        'it would either send a certificate we do not hold or flood the network on every slot')
    return problems


MUTATIONS = {
    'enum-entry-removed': (
        MSG_HPP, '    dlt_block_qc_message_type           = 5119,\n', ''),
    'reflect-enum-entry-removed': (
        MSG_HPP, '    (dlt_timeout_vote_message_type)\n    (dlt_block_qc_message_type))',
        '    (dlt_timeout_vote_message_type))'),
    'static-type-removed': (
        MSG_CPP, 'const dlt_message_type_enum dlt_block_qc_message::type           = dlt_block_qc_message_type;\n', ''),
    'capability-not-advertised': (
        NODE_CPP, 'hello.capabilities = DLT_CAP_BLOCK_VOTE | DLT_CAP_TIMEOUT_VOTE | DLT_CAP_BLOCK_QC;',
        'hello.capabilities = DLT_CAP_BLOCK_VOTE | DLT_CAP_TIMEOUT_VOTE;'),
    'dispatch-removed': (
        NODE_CPP, '            case dlt_block_qc_message_type:\n                on_dlt_block_qc(peer, msg.as<dlt_block_qc_message>());\n                break;\n', ''),
    'delegate-does-not-reach-chain': (
        P2P_CPP, 'return chain.db().accept_block_qc_certificate(msg.cert);', 'return true;'),
    'broadcast-not-capability-gated': (
        NODE_CPP, 'gossip_to_capable(message(msg), DLT_CAP_BLOCK_QC, INVALID_PEER_ID);',
        'send_to_all_our_fork_peers(message(msg));'),
    'relay-stores-nothing': (
        NODE_CPP, 'if (!_delegate || !_delegate->accept_block_qc(msg)) {', 'if (!_delegate) {'),
    'chain-trusts-sender': (
        DB_CPP, 'remember_block_qc(verified);', 'remember_block_qc(cert);'),
    'chain-skips-verification': (
        DB_CPP, 'if (verify_block_vote(cert.block_num, cert.block_id, cert.round, sv.first, sv.second))',
        'if (true)'),
    'chain-skips-block-lookup': (
        DB_CPP, 'const auto block = fetch_block_by_number(cert.block_num);', 'const auto block = fetch_block_by_number(0);'),
    'validator-never-rebroadcasts': (
        VAL_CPP, 'p2p().post_broadcast_block_qc(qc);', '(void)qc;'),
    'validator-broadcasts-unconditionally': (
        VAL_CPP, 'if (certified && now_round >= head_round + 2) {', 'if (now_round >= head_round + 2) {'),
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
    print('PASS: the QC message is declared, reflected, advertised, dispatched, verified by the chain '
          'and re-broadcast while the chain is stalled')
    return 0


if __name__ == '__main__':
    sys.exit(main())
