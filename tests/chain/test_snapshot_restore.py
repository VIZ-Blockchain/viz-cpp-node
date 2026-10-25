#!/usr/bin/env python3
"""Exercise the extracted production restore method; no node, sockets or real DB.

Only dependencies are controlled. Run with python3 tests/chain/test_snapshot_restore.py.
RESTORE_SOURCE may name an alternate plugin.cpp for baseline RED checks.
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
    masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"',
                    lambda m: ' ' * len(m.group()), source, flags=re.S)
    opening = masked.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    return source[start:end]


FIXTURE = r'''
#include <atomic>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#define wlog(...) ((void)0)
#define elog(...) ((void)0)
namespace fc {
struct exception : std::runtime_error {
    exception() : std::runtime_error("controlled fc failure") {}
    std::string to_detail_string() const { return what(); }
};
}
std::vector<std::string> events;
void fail(int kind) {
    if (kind == 1) throw fc::exception();
    if (kind == 2) throw std::runtime_error("controlled std failure");
    if (kind == 3) throw 42;
}
namespace appbase { struct abstract_plugin { enum state { registered, started }; }; }
namespace graphene { namespace plugins { namespace p2p {
struct p2p_plugin {
    int pause_failure = 0, reset_failure = 0, resume_failure = 0;
    bool paused = false, resume_partial = false;
    appbase::abstract_plugin::state state = appbase::abstract_plugin::started;
    auto get_state() const { return state; }
    void pause_block_processing_sync() {
        events.push_back("pause"); fail(pause_failure); paused = true;
    }
    void reset_peers_after_recovery() { events.push_back("reset"); fail(reset_failure); }
    void resume_block_processing() {
        events.push_back("resume");
        if (resume_partial) paused = false;
        fail(resume_failure); paused = false;
    }
};
} } }
namespace appbase {
struct application {
    graphene::plugins::p2p::p2p_plugin p2p;
    bool missing = false;
    int quits = 0;
    template<typename T> T* find_plugin() { return missing ? nullptr : &p2p; }
    std::string data_dir() { return "offline"; }
    void quit() { events.push_back("quit"); ++quits; }
};
application& app() { static application a; return a; }
}
std::string operator/(const std::string& a, const char* b) { return a + "/" + b; }
namespace bfs {
bool exists(const std::string& p) { return p != "missing"; }
bool is_regular_file(const std::string& p) { return p != "directory"; }
}
struct controlled_db {
    int close_failure = 0, head_failure = 0;
    uint32_t head = 99;
    void close(bool) { events.push_back("close"); head = 0; fail(close_failure); }
    uint32_t head_block_num() { events.push_back("head"); fail(head_failure); return head; }
};
struct implementation {
    std::atomic<bool> recovery_in_progress{false}, currently_syncing{false};
    std::mutex recovery_read_mutex;
    controlled_db db;
    std::string snapshot_path = "previous";
};
struct plugin {
    implementation impl;
    implementation* my = &impl;
    bool snapshot_load_callback = true, empty_import = false, populated_failure = false;
    int import_failure = 0;
    void do_snapshot_load(const std::string&, bool recovery) {
        if (!recovery) throw std::runtime_error("not a recovery load");
        events.push_back("import");
        if (populated_failure) my->db.head = 123;
        fail(import_failure);
        my->db.head = empty_import ? 0 : 123;
    }
    bool restore_from_snapshot(const std::string&, std::string&);
};
PRODUCTION_METHOD
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        require(argc == 2, "scenario required");
        std::string s = argv[1];
        plugin p;
        auto& app = appbase::app();
        auto& net = app.p2p;
        auto& db = p.my->db;
        bool prior_sync = s.find("syncing") != std::string::npos;
        p.my->currently_syncing = prior_sync;
        auto starts = [&](const std::string& prefix) { return s.find(prefix) == 0; };
        int kind = s.find("_fc") != std::string::npos ? 1 :
                   s.find("_std") != std::string::npos ? 2 : 3;
        if (starts("pause_")) net.pause_failure = kind;
        if (starts("close_")) db.close_failure = kind;
        if (starts("import_")) p.import_failure = kind;
        if (starts("reset_")) net.reset_failure = kind;
        if (starts("resume_")) net.resume_failure = kind;
        net.resume_partial = s == "resume_partial_std";
        if (s == "head_throws") db.head_failure = 1;
        p.empty_import = s == "zero_head";
        p.populated_failure = s == "import_populated_std";
        app.missing = s == "missing_p2p";
        if (s == "unstarted_p2p") net.state = appbase::abstract_plugin::registered;
        std::string path = "snapshot", error;
        if (s == "empty_path") path.clear();
        if (s == "missing_file") path = "missing";
        if (s == "directory") path = "directory";
        if (s == "no_callback") p.snapshot_load_callback = false;
        bool busy = s == "busy";
        if (busy) p.my->recovery_in_progress = true;
        bool ok = p.restore_from_snapshot(path, error);
        require(p.my->recovery_in_progress == busy, "recovery ownership/guard changed");
        bool validation = s == "empty_path" || s == "missing_file" || s == "directory" || s == "no_callback" || busy;
        bool pause = starts("pause_");
        bool close = starts("close_");
        bool imported_bad = starts("import_") || s == "zero_head" || s == "head_throws";
        bool resume = starts("resume_") || starts("reset_");
        bool failed = validation || pause || close || imported_bad || resume;
        std::cerr << "ok=" << ok << " syncing=" << p.my->currently_syncing
                  << " head=" << db.head << " events=";
        for (const auto& event : events) std::cerr << event << ',';
        std::cerr << '\n';
        require(ok == !failed, "incorrect restore success/failure result");
        require(error.empty() == !failed, "incorrect error result");
        if (validation || pause) {
            require(db.head == 99, "failed preflight/pause mutated database");
            require(p.my->snapshot_path == "previous", "failed pause replaced snapshot path");
            require(p.my->currently_syncing == prior_sync, "failed pause changed pre-restore syncing gate");
            require(events == (pause ? std::vector<std::string>{"pause"} : std::vector<std::string>{}), "pre-mutation abort continued restore");
        } else if (close) {
            require(events == std::vector<std::string>({"pause", "close", "quit"}), "failed close imported/resumed or did not request shutdown");
            require(p.my->snapshot_path == "previous", "failed close installed new snapshot path");
            require(net.paused && p.my->currently_syncing, "failed close lost safe gate");
        } else if (imported_bad) {
            require(net.paused && p.my->currently_syncing, "failed import lost safe gate");
            for (auto& e : events) require(e != "reset" && e != "resume", "failed import resumed P2P");
        } else if (resume) {
            require(p.my->currently_syncing, "failed resume cleared syncing gate");
            require(db.head == 123, "resume failure changed imported state");
        } else {
            bool has_p2p = s != "missing_p2p" && s != "unstarted_p2p";
            require(events == (has_p2p ? std::vector<std::string>{"pause", "close", "import", "head", "reset", "resume"} : std::vector<std::string>{"close", "import", "head"}), "happy-path ordering changed");
            require(!p.my->currently_syncing && !net.paused && db.head == 123, "successful restore did not resume");
            require(p.my->snapshot_path == path, "successful restore lost path");
        }
        require(app.quits == (close ? 1 : 0), "unexpected shutdown policy");
        std::cout << s << " OK\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
'''


class SnapshotRestoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        source = Path(os.environ.get('RESTORE_SOURCE', ROOT / 'plugins/chain/plugin.cpp')).read_text()
        body = method(source, 'bool plugin::restore_from_snapshot(')
        cls.temp = tempfile.TemporaryDirectory(prefix='viz-restore-', dir=os.environ.get('TMPDIR'))
        cls.addClassCleanup(cls.temp.cleanup)
        cpp = Path(cls.temp.name) / 'restore.cpp'
        cpp.write_text(FIXTURE.replace('PRODUCTION_METHOD', body))
        cls.binary = Path(cls.temp.name) / 'restore'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-pthread', str(cpp), '-o', str(cls.binary)], check=True)


def scenario_test(scenario):
    def test(self):
        result = subprocess.run([str(self.binary), scenario], text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, f'{scenario}: {result.stderr}')
        self.assertIn(f'{scenario} OK', result.stdout)
    return test


SCENARIOS = ['happy', 'happy_syncing', 'missing_p2p', 'unstarted_p2p',
             'empty_path', 'missing_file', 'directory', 'no_callback', 'busy',
             'zero_head', 'head_throws', 'resume_partial_std', 'import_populated_std']
SCENARIOS += [f'{stage}_{kind}{suffix}'
              for stage in ('pause', 'close', 'import', 'reset', 'resume')
              for kind in ('fc', 'std', 'unknown')
              for suffix in (('', '_syncing') if stage == 'pause' else ('',))]
for scenario in SCENARIOS:
    setattr(SnapshotRestoreTests, 'test_' + scenario, scenario_test(scenario))


if __name__ == '__main__':
    unittest.main(verbosity=2)
