#!/usr/bin/env python3
"""Offline real-handler regression; fake time/hash/delegate, never a live node.

--baseline extracts the committed versions of the production handlers/members.
--probe reports resource measurements without imposing post-fix assertions.
SANITIZE=1 enables ASan/UBSan. No production source is rewritten by this runner.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
from test_corruption_recovery import method

ROOT = Path(__file__).resolve().parents[2]
CPP = 'libraries/network/dlt_p2p_node.cpp'
HPP = 'libraries/network/include/graphene/network/dlt_p2p_node.hpp'

FIXTURE = r'''
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#define dlog(...) ((void)0)
#define CHAIN_MAX_ACCOUNT_NAME_LENGTH 32
namespace fc {
static int64_t clock_now = 1000;
static uint64_t age_checks = 0;
struct microseconds { int64_t value; };
inline microseconds seconds(int64_t n) { return {n}; }
inline bool operator>(microseconds a, microseconds b) { return a.value > b.value; }
struct time_point {
    int64_t value = 0;
    static time_point now() { return {clock_now}; }
};
inline microseconds operator-(time_point a, time_point b) {
    ++age_checks; return {a.value-b.value};
}
struct sha256 {
    uint64_t value = 0;
    static sha256 hash(uint64_t n) { return {n}; }
    bool operator<(sha256 b) const { return value < b.value; }
};
namespace raw { template<class T> uint64_t pack(const T& msg) { return msg.key(); } }
}
CACHE_INCLUDE
using peer_id = int;
using block_id_type = uint64_t;
using signature_type = uint64_t;
constexpr int INVALID_PEER_ID = -1;
constexpr int DLT_CAP_BLOCK_VOTE = 1, DLT_CAP_TIMEOUT_VOTE = 2;
namespace graphene { namespace protocol {
struct block_header { static uint32_t num_from_id(block_id_type id) { return uint32_t(id); } };
} }
struct dlt_block_vote_message {
    uint32_t block_num = 1, round = 1;
    block_id_type block_id = 1;
    std::string validator = "alice";
    signature_type signature = 1;
    uint64_t key() const { return signature; }
};
struct dlt_timeout_vote_message {
    uint32_t round = 1, high_qc_round = 0;
    std::string validator = "alice";
    signature_type signature = 1;
    uint64_t key() const { return signature; }
};
template<class T> int message(const T&) { return 0; }
struct controlled_delegate {
    bool accepts = true, throws = false;
    size_t calls = 0;
    bool accept() { ++calls; if (throws) throw std::runtime_error("controlled failure"); return accepts; }
    bool accept_block_vote(const dlt_block_vote_message&) { return accept(); }
    bool accept_timeout_vote(const dlt_timeout_vote_message&) { return accept(); }
};
struct peer_state { uint32_t spam_strikes = 0; fc::time_point last_good_packet_time; };
struct dlt_p2p_node {
MEMBERS
THRESHOLD
    controlled_delegate* _delegate;
    std::map<peer_id, peer_state> _peer_states;
    size_t gossips = 0, bans = 0;
    std::function<void()> during_gossip;
    explicit dlt_p2p_node(controlled_delegate* d) : _delegate(d) { _peer_states.emplace(7, peer_state{}); }
    void gossip_to_capable(int, int, peer_id) {
        ++gossips;
        auto callback = std::move(during_gossip);
        during_gossip = nullptr;
        if (callback) callback();
    }
    void soft_ban_peer(peer_id, const std::string&) { ++bans; }
    bool record_packet_result(peer_id, bool);
    void process_block_vote(const dlt_block_vote_message&, peer_id);
    void process_timeout_vote(const dlt_timeout_vote_message&, peer_id);
    void on_dlt_block_vote(peer_id, const dlt_block_vote_message&);
    void on_dlt_timeout_vote(peer_id, const dlt_timeout_vote_message&);
    void broadcast_block_post_validation(const block_id_type&, const std::string&, const signature_type&, uint32_t);
    void broadcast_timeout_vote(uint32_t, uint32_t, const std::string&, const signature_type&);
};
METHODS
void require(bool ok, const char* msg) { if (!ok) throw std::runtime_error(msg); }
int main(int argc, char** argv) {
 try {
    require(argc >= 3, "scenario and vote type required");
    std::string scenario = argv[1]; bool block = std::string(argv[2]) == "block";
    size_t n = argc > 3 ? std::stoull(argv[3]) : 20000;
    bool probe = scenario == "probe";
    controlled_delegate d; dlt_p2p_node node(&d);
    dlt_block_vote_message b; dlt_timeout_vote_message t;
    auto send = [&](uint64_t key, peer_id peer = 7) {
        b.signature = t.signature = key;
        if (block) node.on_dlt_block_vote(peer, b); else node.on_dlt_timeout_vote(peer, t);
    };
    auto size = [&]() { return block ? node._block_votes_seen.size() : node._timeout_votes_seen.size(); };
    auto started = std::chrono::steady_clock::now();
    if (scenario == "rejected" || probe) {
        d.accepts = false;
        for (size_t i = 1; i <= n; ++i) send(i);
        if (!probe) { require(size() == 0, "rejected votes retained in cache"); require(fc::age_checks <= n*2, "rejected flood scans cache"); }
        require(node.gossips == 0, "rejected vote gossiped");
    } else if (scenario == "cap") {
        for (size_t i = 1; i <= n; ++i) { send(i); require(size() <= 8192, "cache exceeds hard cap"); }
        require(size() == 8192, "cap negative control not reached");
        require(fc::age_checks <= n*2, "accepted flood scans cache");
        send(n); require(d.calls == n, "recent duplicate not suppressed");
        send(1); require(d.calls == n+1, "oldest entry not evicted");
    } else if (scenario == "expiry") {
        send(1); require(d.calls == 1 && node.gossips == 1, "accepted vote lost");
        fc::clock_now += 120; send(1); require(d.calls == 1, "TTL boundary changed");
        ++fc::clock_now; send(1); require(d.calls == 2, "expired vote not retried");
    } else if (scenario == "cleanup") {
        for (size_t i = 1; i <= 8192; ++i) send(i);
        fc::clock_now += 121; fc::age_checks = 0;
        send(9000); require(fc::age_checks <= 65, "unbounded expiry cleanup burst");
        require(size() <= 8192, "expiry overflow");
        send(8000); require(d.calls == 8194, "expired tail falsely deduplicated");
    } else if (scenario == "accounting") {
        b.validator = t.validator = "";
        for (unsigned i = 0; i < node.SPAM_STRIKE_THRESHOLD; ++i) send(i+1);
        require(node.bans == 1 && node._peer_states[7].spam_strikes == node.SPAM_STRIKE_THRESHOLD,
                "malformed traffic does not use existing strike policy");
        require(d.calls == 0 && size() == 0, "malformed traffic reached delegate/cache");
        b.validator = t.validator = std::string(33, 'x'); send(99);
        require(d.calls == 0, "oversized name reached delegate");
        send(100, INVALID_PEER_ID); require(node._peer_states.size() == 1, "local malformed vote scored peer");
        b.validator = t.validator = "alice";
        if (block) {
            b.block_num = 0; send(101); b.block_num = 2; send(102);
            require(d.calls == 0, "impossible block vote reached delegate"); b.block_num = 1;
        }
        d.accepts = false; auto strikes = node._peer_states[7].spam_strikes;
        send(103); send(103); require(node._peer_states[7].spam_strikes == strikes,
            "ambiguous false (stale/already known/local failure) unfairly strikes peer");
        node._delegate = nullptr; send(104); require(node._peer_states[7].spam_strikes == strikes, "missing delegate strikes peer");
    } else if (scenario == "local_interleaving_retry") {
        node.during_gossip = [&]() {
            for (size_t i = 2; i <= 8193; ++i) { b.signature = i; node.on_dlt_block_vote(7, b); }
        };
        node.broadcast_block_post_validation(1, "alice", 1, 1);
        const auto before = node.gossips;
        d.accepts = false;
        node.broadcast_block_post_validation(1, "alice", 1, 1);
        require(node.gossips == before + 1, "initial gossip yield suppresses local retry");
    } else if (scenario == "local_expiry") {
        node.broadcast_block_post_validation(1, "alice", 1, 1);
        fc::clock_now += 121;
        d.accepts = false;
        node.broadcast_block_post_validation(1, "alice", 1, 1);
        require(node.gossips == 1, "expired local vote bypasses acceptance");
        d.accepts = true;
        node.broadcast_block_post_validation(1, "alice", 1, 1);
        require(node.gossips == 2, "expired local vote cannot be reaccepted");
    } else if (scenario == "local_eviction_retry") {
        node.broadcast_block_post_validation(1, "alice", 1, 1);
        require(d.calls == 1 && node.gossips == 1, "initial local vote not accepted");
        for (size_t i = 2; i <= 8193; ++i) { b.signature = i; node.on_dlt_block_vote(7, b); }
        const auto before = node.gossips;
        d.accepts = false; // chain has already counted the original local vote
        node.broadcast_block_post_validation(1, "alice", 1, 1);
        require(node.gossips == before + 1, "remote cache churn suppresses local retry");
    } else if (scenario == "retry") {
        if (block) {
            node.broadcast_block_post_validation(1, "alice", 1, 1);
            fc::clock_now += 100;
            node.broadcast_block_post_validation(1, "alice", 1, 1);
            require(d.calls == 1 && node.gossips == 2, "local retry no longer gossips");
            fc::clock_now += 21; send(1); require(d.calls == 2, "local resend pins expiry");
        } else {
            node.broadcast_timeout_vote(1, 0, "alice", 1);
            node.broadcast_timeout_vote(1, 0, "alice", 1);
            require(d.calls == 1 && node.gossips == 1, "timeout duplicate semantics changed");
        }
    } else if (scenario == "failure_retry") {
        d.throws = true;
        try { send(1); } catch (const std::runtime_error&) {}
        require(size() == 0, "exception poisons dedup");
        d.throws = false; send(1); require(node.gossips == 1, "failed vote cannot be retried");
    } else throw std::runtime_error("unknown scenario");
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-started).count();
    std::cout << scenario << ' ' << (block ? "block" : "timeout") << " n=" << n
              << " retained=" << size() << " age_checks=" << fc::age_checks
              << " delegate_calls=" << d.calls << " elapsed_us=" << us << '\n';
    return 0;
 } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', action='store_true')
    parser.add_argument('--probe', action='store_true')
    args = parser.parse_args()
    def source(path):
        if args.baseline:
            return subprocess.check_output(['git', 'show', 'HEAD:' + path], cwd=ROOT, text=True)
        return (ROOT / path).read_text()
    cpp, hpp = source(CPP), source(HPP)
    signatures = ['void dlt_p2p_node::' + name + '(' for name in (
        'process_block_vote', 'process_timeout_vote', 'on_dlt_block_vote',
        'on_dlt_timeout_vote', 'broadcast_block_post_validation', 'broadcast_timeout_vote')]
    signatures.append('bool dlt_p2p_node::record_packet_result(')
    members = '\n'.join(re.findall(r'^\s*[^\n]*\s_block_votes_seen;|^\s*[^\n]*\s_timeout_votes_seen;|^\s*[^\n]*\s_local_block_votes_seen;', hpp, re.M))
    threshold = re.search(r'    static constexpr uint32_t\s+SPAM_STRIKE_THRESHOLD = \d+;', hpp).group()
    include = '' if args.baseline else '#include <graphene/network/dlt_vote_cache.hpp>\nusing graphene::network::dlt_vote_cache;'
    code = FIXTURE.replace('CACHE_INCLUDE', include).replace('MEMBERS', members).replace('\nTHRESHOLD\n', '\n' + threshold + '\n').replace('METHODS', '\n'.join(method(cpp, sig) for sig in signatures))
    with tempfile.TemporaryDirectory(prefix='viz-vote-', dir=os.environ.get('TMPDIR')) as tmp:
        tmp = Path(tmp)
        for stub in ('fc/time.hpp', 'fc/crypto/sha256.hpp'):
            p = tmp / stub; p.parent.mkdir(parents=True, exist_ok=True); p.write_text('#pragma once\n')
        source_file, binary = tmp / 'votes.cpp', tmp / 'votes'
        source_file.write_text(code)
        flags = ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g'] if os.environ.get('SANITIZE') else []
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++14', '-O2', '-Wall', '-Wextra', '-Werror', *flags,
                        '-I'+str(tmp), '-I'+str(ROOT / 'libraries/network/include'), str(source_file), '-o', str(binary)], check=True)
        failed = 0
        cases = [('probe', str(n)) for n in (1000, 2000, 4000, 8000)] if args.probe else [(name, '20000') for name in (
            'rejected', 'cap', 'expiry', 'cleanup', 'accounting', 'retry', 'failure_retry',
            'local_eviction_retry', 'local_interleaving_retry', 'local_expiry')]
        executed = 0
        for vote in ('block', 'timeout'):
            for scenario, n in cases:
                if scenario.startswith('local_') and vote == 'timeout':
                    continue
                executed += 1
                result = subprocess.run([str(binary), scenario, vote, n], capture_output=True, text=True)
                print(result.stdout or f'FAIL {scenario} {vote}: {result.stderr.strip()}', end='' if result.stdout else '\n')
                failed += result.returncode != 0
        if failed: raise SystemExit(f'{failed} scenarios failed')
        print(f'PASS: {executed} scenarios')


if __name__ == '__main__':
    main()
