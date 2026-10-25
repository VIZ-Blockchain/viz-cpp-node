#!/usr/bin/env python3
"""Offline regression: compile the actual plugin helpers, not a Python rewrite.

Run: python3 tests/validator/test_signing_cache.py
RED: python3 tests/validator/test_signing_cache.py --source-ref d4fd4bcd
Only stdlib + a C++14 compiler; temporary output stays under TMPDIR.
The fake DB controls committed/pending state; it does not test chain execution.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PLUGIN = "plugins/validator/validator.cpp"


def function(text, signature):
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-ref")
    args = parser.parse_args()
    source = (subprocess.check_output(["git", "show", f"{args.source_ref}:{PLUGIN}"], cwd=ROOT, text=True)
              if args.source_ref else (ROOT / PLUGIN).read_text())
    effective = function(source, "public_key_type effective_signing_key(")
    refresh = function(source, "void update_committed_signing_keys(")
    fields_start = source.index("std::map<string, public_key_type> _committed_signing_keys;")
    fields_end = source.index("std::mutex _committed_keys_mutex;", fields_start)
    fields_end += len("std::mutex _committed_keys_mutex;")
    fields = source[fields_start:fields_end]
    old = "signed_block &block" in refresh
    adapter = "update_committed_signing_keys(b);" if old else "update_committed_signing_keys();"
    cpp = r'''
#include <map>
#include <set>
#include <string>
#include <mutex>
#include <vector>
#include <iostream>
#include <stdexcept>
using string = std::string;
using public_key_type = int;
using block_id_type = int;
struct validator_update_operation { string owner; int block_signing_key; };
struct operation {
    template<class T> struct tag { enum { value = 1 }; };
    validator_update_operation up;
    int which() const { return 1; }
    template<class T> const T& get() const { return up; }
};
namespace graphene { namespace chain {
struct signed_block {
    struct transaction { std::vector<operation> operations; };
    std::vector<transaction> transactions;
    unsigned num = 0;
    unsigned block_num() const { return num; }
};
}}
struct fake_db {
    struct validator { int signing_key = 0; };
    std::map<string, validator> visible;
    unsigned num = 1;
    int id = 11;
    unsigned head_block_num() const { return num; }
    int head_block_id() const { return id; }
    const validator* find_validator(const string& name) const {
        auto it = visible.find(name);
        return it == visible.end() ? nullptr : &it->second;
    }
};
struct plugin {
    fake_db db;
    fake_db& database() { return db; }
    std::set<string> _validators{"alice", "bob", "not-created"};
''' + fields + "\n" + effective + "\n" + refresh + r'''
    void capture(int key, unsigned num, int id, bool rotation) {
        db.num = num; db.id = id; db.visible["alice"].signing_key = key;
        graphene::chain::signed_block b; b.num = num;
        if (rotation) {
            graphene::chain::signed_block::transaction tx;
            tx.operations.push_back(operation{{"alice", key}});
            b.transactions.push_back(tx);
        }
        ''' + adapter + r'''
    }
    void seed() {
        db.visible["alice"].signing_key = 10;
        db.visible["bob"].signing_key = 20;
        _committed_signing_keys["alice"] = 10;
        _committed_signing_keys["bob"] = 20;
        _committed_keys_block_num = db.head_block_num();
''' + ("_committed_keys_block_id = db.head_block_id();" if not old else "") + r'''
    }
};
int failures = 0;
void check(const char* name, bool ok) {
    std::cout << (ok ? "PASS " : "FAIL ") << name << '\n';
    if (!ok) ++failures;
}
int main() {
    plugin p; p.seed();
    check("startup seed", p.effective_signing_key("alice", 10) == 10);
    p.db.visible["alice"].signing_key = 11;
    check("pending rotation signs with committed key", p.effective_signing_key("alice", 11) == 10);
    p.capture(11, 2, 22, true);
    check("committed rotation", p.effective_signing_key("alice", 11) == 11);
    p.db.visible["alice"].signing_key = 12;
    check("second pending rotation", p.effective_signing_key("alice", 12) == 11);
    // Reorg removes the committed 10->11 rotation. The adopted head contains
    // no rotation. The orphaned rotation is then restored to the pending pool.
    p.capture(10, 3, 33, false);
    p.db.visible["alice"].signing_key = 11;
    check("reverted rotation restored as pending", p.effective_signing_key("alice", 11) == 10);
    check("unrelated validator preserved", p.effective_signing_key("bob", 21) == 20);
    check("visible null disables production", p.effective_signing_key("alice", 0) == 0);
    p.capture(0, 4, 44, true);
    check("committed null remains disabled despite pending enable", p.effective_signing_key("alice", 12) == 0);
    p.capture(12, 5, 55, true);
    check("committed re-enable", p.effective_signing_key("alice", 13) == 12);
    p.db.num = 6; p.db.id = 66;
    check("stale height falls back", p.effective_signing_key("alice", 13) == 13);
    p.db.num = 5;
    check("same-height different head falls back", p.effective_signing_key("alice", 13) == 13);
    p.capture(12, 5, 55, false);
    p.db.visible.erase("alice");
    p.capture(12, 6, 66, false);
    p.db.visible.erase("alice");
''' + (r'''
    p.update_committed_signing_keys();
    check("reverted validator creation removes cache entry", p.effective_signing_key("alice", 14) == 14);
''' if not old else "") + r'''
    return failures ? 1 : 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="validator-cache-", dir=os.environ.get("TMPDIR")) as tmp:
        path = Path(tmp)
        (path / "test.cpp").write_text(cpp)
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++14", "-Wall", "-Wextra", "-Werror", "-pthread",
                        str(path / "test.cpp"), "-o", str(path / "test")], check=True)
        result = subprocess.run([str(path / "test")])
    if result.returncode:
        return result.returncode
    if not args.source_ref:
        # Static integration guard: notification must be in the no-pending lambda,
        # after both fallible operations, not in a deferred applied_block handler.
        database = (ROOT / "libraries/chain/database.cpp").read_text()
        push = function(database, "bool database::push_block(")
        callback = function(push, "detail::without_pending_transactions(")
        assert callback.index("result = _push_block(new_block, skip);") < callback.index(
            "check_free_memory(false, new_block.block_num());") < callback.index(
            "CHAIN_TRY_NOTIFY(committed_chain_state);") < callback.index("} catch")
        assert "with_strong_write_lock([&]()" in push[:push.index("detail::without_pending_transactions(")]
        applied = function(source, "void validator_plugin::impl::on_block_applied(")
        assert "update_committed_signing_keys" not in applied
        assert "scoped_connection _committed_chain_state_connection;" in source
        assert "_committed_chain_state_connection.disconnect();" in source
        connected = function(source, "pimpl->_committed_chain_state_connection = d.committed_chain_state.connect(")
        assert "pimpl->update_committed_signing_keys();" in connected
        print("PASS synchronous runtime hook placement / lifecycle (source guard)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
