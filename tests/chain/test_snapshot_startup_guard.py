#!/usr/bin/env python3
"""--snapshot must never leave the node running against an unopened database.

Measured 01.11.2026: `vizd --snapshot <existing file>` with no `plugin = ... snapshot`
crashed with SIGSEGV ~250 ms after the startup banner, on every attempt, on images
that predate this branch. Two independent defects produced it:

  1. the chain plugin accepted --snapshot, found that no snapshot-load callback was
     registered yet, set `pending_snapshot_load` and returned from plugin_startup
     WITHOUT opening the database — and with the plugin disabled nobody ever
     registers that callback, so the open never happens at all;
  2. the P2P plugin starts right after and its periodic task asks the chain for
     head/LIB, which reads a chainbase object through a null segment -> SIGSEGV
     (not an exception: the try/catch around the periodic task cannot help).

This test is pure source wiring: no build, no node. Run
    python3 tests/chain/test_snapshot_startup_guard.py
    python3 tests/chain/test_snapshot_startup_guard.py --selftest
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

CHAIN_PLUGIN = "plugins/chain/plugin.cpp"
P2P_PLUGIN = "plugins/p2p/p2p_plugin.cpp"
DATABASE_CPP = "libraries/chain/database.cpp"
DATABASE_HPP = "libraries/chain/include/graphene/chain/database.hpp"

MISSING_PLUGIN_GUARD = '!plugin_enabled_in_config("snapshot")'
DEFER_FLAG = "pending_snapshot_load = true"


def read_sources(root=ROOT):
    return {
        CHAIN_PLUGIN: (root / CHAIN_PLUGIN).read_text(),
        P2P_PLUGIN: (root / P2P_PLUGIN).read_text(),
        DATABASE_CPP: (root / DATABASE_CPP).read_text(),
        DATABASE_HPP: (root / DATABASE_HPP).read_text(),
    }


def _masked(source):
    """Same-length copy with comments and string literals blanked out."""
    masked = []
    i, n = 0, len(source)
    in_line = in_block = in_str = False
    while i < n:
        c = source[i]
        nxt = source[i + 1] if i + 1 < n else ""
        if in_line:
            in_line = c != "\n"
            masked.append(" " if c != "\n" else "\n")
        elif in_block:
            if c == "*" and nxt == "/":
                in_block = False
                masked.append("  ")
                i += 1
            else:
                masked.append("\n" if c == "\n" else " ")
        elif in_str:
            if c == "\\":
                masked.append("  ")
                i += 1
            else:
                if c == '"':
                    in_str = False
                masked.append(" ")
        elif c == "/" and nxt == "/":
            in_line = True
            masked.append("  ")
            i += 1
        elif c == "/" and nxt == "*":
            in_block = True
            masked.append("  ")
            i += 1
        elif c == '"':
            in_str = True
            masked.append(" ")
        else:
            masked.append(c)
        i += 1
    masked = "".join(masked)
    return masked


def _method(source, signature):
    """(raw, masked) body of the method starting at `signature`, brace-matched.

    Checks run against `masked` so a comment that merely MENTIONS the guard cannot
    satisfy them (live selftest catch: the comment above head_block_num() names
    `_state_open`, which made the stripped-guard mutation look clean).
    """
    masked = _masked(source)
    start = source.index(signature)
    opening = masked.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (masked[end] == "{") - (masked[end] == "}")
        end += 1
    return source[start:end], masked[start:end]


def wiring_problems(sources):
    problems = []
    chain = sources[CHAIN_PLUGIN]
    p2p = sources[P2P_PLUGIN]
    db_cpp = sources[DATABASE_CPP]
    db_hpp = sources[DATABASE_HPP]

    # (1) both deferral branches must refuse to defer when the plugin cannot arrive
    if chain.count(MISSING_PLUGIN_GUARD) < 2:
        problems.append(
            "chain plugin: fewer than two plugin_enabled_in_config(\"snapshot\") guards — a --snapshot "
            "or --replay-from-snapshot branch can defer the database open forever"
        )
    if chain.count(DEFER_FLAG) < 2:
        problems.append("chain plugin: fewer than two deferral assignments — the deferral "
                        "this guard protects is gone; re-derive the test before trusting it")
    # every deferral must be preceded by its own guard: a deferral reached with the
    # plugin missing is exactly the hang this test exists for
    pos = chain.find(DEFER_FLAG)
    while pos >= 0:
        guard = chain.rfind(MISSING_PLUGIN_GUARD, 0, pos)
        if guard < 0 or pos - guard > 2000:
            problems.append("chain plugin: a pending_snapshot_load assignment is not preceded "
                            "by a plugin_enabled_in_config(\"snapshot\") guard and can defer the open unguarded")
            break
        pos = chain.find(DEFER_FLAG, pos + 1)
    for sig in ("Snapshot path configured", "Replay-from-snapshot configured"):
        try:
            idx = chain.index(sig)
        except ValueError:
            problems.append("chain plugin: deferral branch for %r disappeared" % sig)
            continue
        window = chain[max(0, idx - 3000):idx]
        guard = window.rfind(MISSING_PLUGIN_GUARD)
        if guard < 0:
            problems.append("chain plugin: %r branch has no missing-plugin guard" % sig)
        elif "throw std::runtime_error" not in window[guard:]:
            problems.append("chain plugin: %r branch does not throw on a missing plugin" % sig)

    # (2) head/LIB accessors must be safe before the segment is mapped
    for sig in ("uint32_t database::head_block_num() const", "block_id_type database::head_block_id() const"):
        try:
            _, body = _method(db_cpp, sig)
        except ValueError:
            problems.append("database.cpp: %s disappeared" % sig)
            continue
        if "is_state_open" not in body and "_state_open" not in body:
            problems.append("database.cpp: %s reads the segment without an openness guard" % sig)

    if "_state_open" not in db_hpp or "is_state_open" not in db_hpp:
        problems.append("database.hpp: no _state_open/is_state_open flag")

    # (2b) the flag itself must bracket the open correctly: cleared before anything
    # unmaps the segment, set only once the indexes (and, for the snapshot path, the
    # genesis DGP) exist — open_from_snapshot can race a live P2P periodic task.
    try:
        _, plain_open = _method(db_cpp, "void database::open(")
    except ValueError:
        problems.append("database.cpp: database::open() disappeared")
        plain_open = ""
    if plain_open and "_state_open = false" not in plain_open:
        problems.append("database.cpp: open() does not clear _state_open before rebinding the segment")
    if plain_open and "_state_open = true" not in plain_open:
        problems.append("database.cpp: open() never marks the state open (head/LIB would read 0)")
    try:
        _, snap_open = _method(db_cpp, "void database::open_from_snapshot(")
    except ValueError:
        problems.append("database.cpp: open_from_snapshot() disappeared")
        snap_open = ""
    if snap_open:
        reset = snap_open.find("_state_open = false")
        wipe = snap_open.find("chainbase::database::wipe(")
        ready = snap_open.rfind("_state_open = true")
        genesis = snap_open.find("init_genesis(")
        if reset < 0:
            problems.append("database.cpp: open_from_snapshot() does not clear _state_open "
                            "before the wipe unmaps the old segment")
        elif 0 <= wipe < reset:
            problems.append("database.cpp: open_from_snapshot() wipes before clearing _state_open")
        if ready < 0:
            problems.append("database.cpp: open_from_snapshot() never marks the state open")
        elif genesis >= 0 and ready < genesis:
            problems.append("database.cpp: open_from_snapshot() marks the state open before the "
                            "genesis DGP exists — a concurrent reader would find no DGP")

    # (3) p2p delegate must not query an unopened database
    for sig in ("block_id_type get_head_block_id() const override",
                "uint32_t get_head_block_num() const override",
                "block_id_type get_lib_block_id() const override",
                "uint32_t get_lib_block_num() const override",
                "bool is_emergency_consensus_active() const override",
                "uint32_t block_interval() const override"):
        try:
            _, body = _method(p2p, sig)
        except ValueError:
            problems.append("p2p_plugin.cpp: %s disappeared" % sig)
            continue
        if "is_state_open" not in body:
            problems.append("p2p_plugin.cpp: %s has no is_state_open() guard" % sig)

    return problems


def baseline_from_git():
    """Sources of the revision BEFORE the fix landed, located from history.

    The first version assumed that origin/master is still pre-fix. That made this selftest turn red
    the moment the fix was merged: on master the "baseline" carries the guard too, so it passes and
    the required shortfall cannot be shown (observed on master b0b244f1, 2026-11-01 -- CI green on
    the PR, red on the merge result). The pre-fix revision is found by asking git which commit
    introduced the guard, then reading that commit's parent.

    Returns None when history is not available (a pull_request checkout is depth 1) or when the
    located revision still carries the guard -- nothing honest can be proven then, and the caller
    says so instead of passing silently.
    """
    try:
        log = subprocess.run(["git", "log", "-S", MISSING_PLUGIN_GUARD, "--format=%H", "--", CHAIN_PLUGIN],
                             cwd=str(ROOT), capture_output=True, text=True)
    except OSError:
        return None
    commits = log.stdout.split()
    if not commits:
        return None
    introducing = commits[-1]           # git log is newest first; the oldest match added the guard
    parent = subprocess.run(["git", "rev-parse", "--verify", "%s^" % introducing],
                            cwd=str(ROOT), capture_output=True, text=True)
    if parent.returncode != 0:
        return None
    rev = parent.stdout.strip()
    out = {}
    for path in (CHAIN_PLUGIN, P2P_PLUGIN, DATABASE_CPP, DATABASE_HPP):
        shown = subprocess.run(["git", "show", "%s:%s" % (rev, path)],
                               cwd=str(ROOT), capture_output=True, text=True)
        if shown.returncode != 0:
            return None
        out[path] = shown.stdout
    if MISSING_PLUGIN_GUARD in out[CHAIN_PLUGIN]:
        # The "pre-fix" revision already has the guard: the lookup is wrong, and a baseline taken
        # from it would pass by construction. Say that instead of reporting a clean run.
        return {"__baseline_has_fix__": rev}
    return out


def selftest():
    failures = []
    good = read_sources()
    if wiring_problems(good):
        failures.append("selftest: the current tree is not clean: %r" % wiring_problems(good))

    # (a) remove BOTH missing-plugin guards: must, at minimum, report the shortfall
    mutated = dict(good)
    mutated[CHAIN_PLUGIN] = good[CHAIN_PLUGIN].replace(MISSING_PLUGIN_GUARD, "false")
    if mutated[CHAIN_PLUGIN] == good[CHAIN_PLUGIN]:
        failures.append("selftest: could not remove the missing-plugin guards")
    elif not any('plugin_enabled_in_config("snapshot") guards' in p for p in wiring_problems(mutated)):
        failures.append("selftest: removed missing-plugin guards were not reported")

    # (a2) a deferral placed above the guards must be reported as unguarded
    mutated = dict(good)
    mutated[CHAIN_PLUGIN] = DEFER_FLAG + ";\n" + good[CHAIN_PLUGIN]
    if not any("not preceded by a plugin_enabled_in_config" in p for p in wiring_problems(mutated)):
        failures.append("selftest: guard-after-deferral was not reported")

    # (b) remove the openness guard from head_block_num()
    sig = "uint32_t database::head_block_num() const"
    body, _ = _method(good[DATABASE_CPP], sig)
    stripped = body.replace("if (!_state_open) return 0;", "", 1)
    if stripped == body:
        failures.append("selftest: could not strip the head_block_num() guard")
    else:
        mutated = dict(good)
        mutated[DATABASE_CPP] = good[DATABASE_CPP].replace(body, stripped, 1)
        if not any("head_block_num() const reads the segment" in p for p in wiring_problems(mutated)):
            failures.append("selftest: stripped head_block_num() guard was not reported")

    # (c) remove the p2p delegate guard
    sig = "uint32_t get_head_block_num() const override"
    body, _ = _method(good[P2P_PLUGIN], sig)
    stripped = body.replace("if (!chain.db().is_state_open()) return 0;", "", 1)
    if stripped == body:
        failures.append("selftest: could not strip the p2p head guard")
    else:
        mutated = dict(good)
        mutated[P2P_PLUGIN] = good[P2P_PLUGIN].replace(body, stripped, 1)
        if not any("get_head_block_num" in p and "is_state_open() guard" in p
                   for p in wiring_problems(mutated)):
            failures.append("selftest: stripped p2p head guard was not reported")

    # (e) open_from_snapshot() must clear the flag before the wipe
    sig = "void database::open_from_snapshot("
    body, _ = _method(good[DATABASE_CPP], sig)
    stripped = body.replace("_state_open = false;", "", 1)
    if stripped == body:
        failures.append("selftest: could not strip the open_from_snapshot() reset")
    else:
        mutated = dict(good)
        mutated[DATABASE_CPP] = good[DATABASE_CPP].replace(body, stripped, 1)
        if not any("open_from_snapshot() does not clear _state_open" in p
                   for p in wiring_problems(mutated)):
            failures.append("selftest: stripped open_from_snapshot() reset was not reported")

    # (d) baseline RED: the pre-fix revision must be reported as broken (best effort —
    # a pull_request checkout carries no origin/master)
    baseline = baseline_from_git()
    if baseline is None:
        print("selftest note: baseline RED skipped — no history to locate the pre-fix revision")
    elif "__baseline_has_fix__" in baseline:
        failures.append("baseline revision %s already carries the guard — the history lookup is "
                        "wrong, the baseline would pass by construction"
                        % baseline["__baseline_has_fix__"])
    else:
        found = wiring_problems(baseline)
        if len(found) < 5:
            failures.append("baseline should fail at least 5 checks, got %d: %r"
                            % (len(found), found))

    return failures


def main():
    problems = wiring_problems(read_sources())
    if problems:
        print("FAIL: %d wiring problem(s)" % len(problems))
        for p in problems:
            print(" -", p)
        return 1
    print("ok: --snapshot cannot run against an unopened database (2 plugin guards, "
          "2 database accessors, 6 p2p delegate queries)")
    if "--selftest" in sys.argv:
        failures = selftest()
        if failures:
            print("SELFTEST FAIL")
            for f in failures:
                print(" -", f)
            return 1
        print("selftest ok: mutations and baseline RED behave")
    return 0


if __name__ == "__main__":
    sys.exit(main())
