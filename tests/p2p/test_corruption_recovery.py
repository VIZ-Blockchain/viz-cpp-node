#!/usr/bin/env python3
"""Compile the actual P2P entrypoint bodies against controlled offline dependencies.

No copied ingestion/recovery logic: the two delegate methods and chain policy
methods are extracted from production sources each run. The fixture replaces
storage, app shutdown and snapshot I/O, not the code under test.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    # Mask comments and literals so braces inside them do not affect extraction.
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',
                    lambda m: ' ' * len(m.group()), source, flags=re.S)
    opening = masked.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    return source[start:end]


FIXTURE = r'''
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include <map>
#define wlog(...) ((void)0)
#define elog(...) ((void)0)
#define dlog(...) ((void)0)
namespace fc {
struct exception : std::runtime_error {
    exception() : std::runtime_error("controlled rejection") {}
    std::string to_detail_string() const { return what(); }
};
struct duration { long count() const { return 100000000; } };
struct time_point { static time_point now() { return {}; } };
duration operator-(time_point, time_point) { return {}; }
}
namespace graphene { namespace chain {
struct block_too_old_exception : fc::exception {};
struct unlinkable_block_exception : fc::exception {};
struct shared_memory_corruption_exception : fc::exception {};
struct tagged_corruption : shared_memory_corruption_exception {};
struct deferred_resize_exception : fc::exception {};
struct database { enum { skip_nothing = 0, skip_transaction_signatures = 1 }; };
} namespace network { struct deferred_resize_exception {}; } }
using block_id_type = int;
namespace protocol {
struct signed_transaction {};
struct signed_block {
    int previous = 0;
    uint32_t height = 11;
    uint32_t block_num() const { return height; }
    int id() const { return 11; }
};
}
using protocol::signed_block;
using protocol::signed_transaction;
struct fork_item { signed_block data; fork_item() { data.height = 20; } };
struct fork_db {
    int resets = 0;
    std::shared_ptr<fork_item> head() { return std::make_shared<fork_item>(); }
    void reset() { ++resets; }
    void start_block(const signed_block&) {}
    void push_block(const signed_block&) {}
};
struct controlled_db {
    fork_db forks;
    int pushes = 0, tx_pushes = 0, failure = 0;
    bool retry = false, applied = true, known = false;
    uint32_t skip_seen = 99;
    bool push_block(const signed_block&, uint32_t skip) {
        skip_seen = skip;
        if (++pushes == 1 && retry) throw graphene::chain::block_too_old_exception();
        switch (failure) {
        case 1: throw graphene::chain::tagged_corruption();
        case 2: throw fc::exception();
        case 3: throw graphene::chain::unlinkable_block_exception();
        case 4: throw graphene::chain::block_too_old_exception();
        case 5: throw graphene::chain::deferred_resize_exception();
        }
        return applied;
    }
    void push_transaction(const signed_transaction&) {
        ++tx_pushes;
        if (failure == 1) throw graphene::chain::tagged_corruption();
        if (failure == 2) throw fc::exception();
    }
    fork_db& get_fork_db() { return forks; }
    uint32_t head_block_num() const { return known ? 11 : 10; }
    int head_block_id() const { return 10; }
    int find_block_id_for_num(uint32_t) const { return known ? 11 : 0; }
    std::optional<signed_block> fetch_block_by_id(int) { return signed_block{}; }
};
namespace appbase {
struct application { int quits = 0; void quit() { ++quits; } };
application& app() { static application a; return a; }
}
namespace chain {
struct plugin_impl {
    controlled_db storage;
    bool auto_recover_from_snapshot = true;
    void accept_transaction(const signed_transaction& tx) { storage.push_transaction(tx); }
};
struct plugin {
    plugin_impl implementation;
    plugin_impl* my = &implementation;
    int recoveries = 0;
    bool recovery_throws = false;
    controlled_db& db() { return my->storage; }
    void attempt_auto_recovery() {
        ++recoveries;
        if (recovery_throws) throw fc::exception();
    }
    void handle_shared_memory_corruption();
    void accept_transaction(const signed_transaction&);
};
CHAIN_METHODS
}
enum class dlt_block_accept_result { ACCEPTED, ALREADY_KNOWN, FORK_DB_ONLY, DEAD_FORK, REJECTED };
struct delegate_interface {
    virtual dlt_block_accept_result accept_block(const signed_block&, bool) = 0;
    virtual bool accept_transaction(const signed_transaction&) = 0;
};
struct delegate : delegate_interface {
    chain::plugin& chain;
    fc::time_point _startup_time;
    explicit delegate(chain::plugin& c) : chain(c) {}
P2P_METHODS
};
using peer_id = int;
struct dlt_transaction_message { signed_transaction trx; };
struct peer_state { std::string endpoint = "fixture-peer"; };
struct dlt_p2p_node {
    std::map<peer_id, peer_state> _peer_states;
    delegate_interface* _delegate;
    explicit dlt_p2p_node(delegate_interface* value) : _delegate(value) {}
    bool add_to_mempool(const signed_transaction&, bool, peer_id) { return true; }
    void on_dlt_transaction(peer_id peer, const dlt_transaction_message& msg);
};
NETWORK_METHODS
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "scenario required");
        std::string scenario = argv[1];
        chain::plugin chain;
        delegate p2p(chain);
        auto& db = chain.db();
        auto& app = appbase::app();
        bool enabled = scenario.find("disabled") == std::string::npos;
        chain.my->auto_recover_from_snapshot = enabled;
        bool network = scenario.find("network_tx_") == 0;
        bool chain_entry = scenario.find("chain_tx_") == 0;
        bool tx = network || chain_entry || scenario.find("tx_") == 0;
        bool corrupt = scenario.find("corrupt") != std::string::npos;
        bool recovery_failure = scenario.find("recovery_failure") != std::string::npos;
        chain.recovery_throws = recovery_failure;
        db.failure = (corrupt || recovery_failure) ? 1 :
            scenario.find("ordinary") != std::string::npos ? 2 :
            scenario.find("unlinkable") != std::string::npos ? 3 :
            scenario.find("too_old") != std::string::npos ? 4 :
            scenario.find("resize") != std::string::npos ? 5 : 0;
        db.retry = scenario.find("retry_") == 0;
        db.applied = scenario.find("not_applied") == std::string::npos;
        db.known = scenario.find("known") != std::string::npos;
        bool typed = false, failed_recovery = false, resized = false, accepted_tx = false;
        auto result = dlt_block_accept_result::REJECTED;
        try {
            if (network) {
                dlt_p2p_node node(&p2p);
                node.on_dlt_transaction(1, {});
            } else if (chain_entry) chain.accept_transaction({});
            else if (tx) accepted_tx = p2p.accept_transaction({});
            else result = p2p.accept_block({}, true);
        } catch (const graphene::chain::tagged_corruption&) { typed = true; }
          catch (const graphene::network::deferred_resize_exception&) { resized = true; }
          catch (const fc::exception&) { failed_recovery = true; }
        if (corrupt || recovery_failure) {
            require(chain.recoveries == (enabled ? 1 : 0), "wrong recovery count / policy bypass");
            require(app.quits == (enabled ? 0 : 1), "disabled recovery must request shutdown");
            if (chain_entry) require(typed, "chain corruption must remain typed until P2P boundary");
            else if (tx) require(!typed && !failed_recovery && !accepted_tx,
                "handled local corruption must not escape into the transport disconnect catch");
            else if (recovery_failure) require(failed_recovery, "failed recovery must escape retry rejection handlers");
            else {
                require(!typed && !failed_recovery, "block corruption escaped as an unrelated failure");
                require(result == dlt_block_accept_result::REJECTED, "corrupt block must not become ALREADY_KNOWN");
            }
        } else {
            require(!typed && !failed_recovery, "ordinary rejection escaped the delegate");
            require(chain.recoveries == 0 && app.quits == 0, "ordinary operation triggered recovery");
            if (tx) require(accepted_tx == (db.failure == 0), "ordinary transaction result changed");
            else if (db.failure == 5 && !db.retry) require(resized, "resize must retain network exception");
            else {
                auto expected = db.failure == 3 ? dlt_block_accept_result::DEAD_FORK :
                    db.failure && db.retry ? dlt_block_accept_result::ALREADY_KNOWN :
                    db.failure ? dlt_block_accept_result::REJECTED :
                    db.applied ? dlt_block_accept_result::ACCEPTED :
                    db.known ? dlt_block_accept_result::ALREADY_KNOWN : dlt_block_accept_result::FORK_DB_ONLY;
                require(result == expected, "ordinary block classification changed");
            }
        }
        if (tx) require(db.tx_pushes == 1, "transaction was retried or skipped");
        else {
            require(db.pushes == (db.retry ? 2 : 1), "wrong block push count");
            require(db.forks.resets == (db.retry ? 1 : 0), "retry did not reset fork_db");
            require(db.skip_seen == graphene::chain::database::skip_transaction_signatures, "sync skip flags changed");
        }
        std::cout << scenario << " OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
'''


class CorruptionRecoveryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        p2p = (ROOT / 'plugins/p2p/p2p_plugin.cpp').read_text()
        chain = (ROOT / 'plugins/chain/plugin.cpp').read_text()
        methods = method(chain, 'void plugin::accept_transaction(')
        if 'void plugin::handle_shared_memory_corruption(' in chain:
            methods += '\n' + method(chain, 'void plugin::handle_shared_memory_corruption(')
        network = (ROOT / 'libraries/network/dlt_p2p_node.cpp').read_text()
        code = FIXTURE.replace('NETWORK_METHODS', method(network, 'void dlt_p2p_node::on_dlt_transaction(')).replace('CHAIN_METHODS', methods).replace('P2P_METHODS',
            method(p2p, 'dlt_block_accept_result accept_block(') + '\n' +
            method(p2p, 'bool accept_transaction('))
        cls.tmp = tempfile.TemporaryDirectory(prefix='viz-p2p-', dir=os.environ.get('TMPDIR'))
        source = Path(cls.tmp.name) / 'entrypoints.cpp'
        cls.binary = Path(cls.tmp.name) / 'entrypoints'
        source.write_text(code)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
                        '-Werror', str(source), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_entrypoints(self):
        scenarios = [
            'tx_corrupt_enabled', 'tx_corrupt_disabled', 'tx_success', 'tx_ordinary',
            'chain_tx_corrupt_enabled', 'chain_tx_corrupt_disabled',
            'network_tx_corrupt_enabled', 'network_tx_corrupt_disabled',
            'block_corrupt_enabled', 'block_corrupt_disabled',
            'retry_corrupt_enabled', 'retry_corrupt_disabled',
            'retry_recovery_failure', 'block_recovery_failure',
            'block_success', 'block_not_applied', 'block_not_applied_known',
            'retry_success', 'retry_not_applied', 'retry_not_applied_known',
            'block_ordinary', 'retry_ordinary', 'retry_unlinkable', 'retry_too_old',
            'block_resize', 'retry_resize',
        ]
        for scenario in scenarios:
            with self.subTest(scenario=scenario):
                result = subprocess.run([str(self.binary), scenario], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                print(result.stdout, end='')


if __name__ == '__main__':
    unittest.main()
