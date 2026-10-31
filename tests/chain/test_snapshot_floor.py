#!/usr/bin/env python3
"""Snapshot floor rule: never import state at or below our own head/LIB.

The guard itself landed in PR #219 (dac90c2f); this is its test carrier. It compiles the
production header on its own (no node, no DB, no sockets) and checks the truth table, then
asserts the rule is actually WIRED INTO both places in plugin.cpp where it matters:

  1. phase 1 of download_snapshot_from_peers() filters peers by the floor;
  2. immediately before the downloaded file can be renamed for import, the floor is
     re-measured -- the download takes minutes, and a head that grew past the snapshot
     would be rolled back by the state wipe;
  3. load_snapshot() itself refuses a file at or below our head/LIB BEFORE it wipes any
     object -- that is the only guard the direct paths (--snapshot, --snapshot-auto-latest,
     hot-reload callback) pass through, and it checks the file's own header, not what a
     peer advertised.

Run: python3 tests/chain/test_snapshot_floor.py [--selftest]
"""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "plugins/snapshot/include/graphene/plugins/snapshot/snapshot_floor.hpp"
PLUGIN = ROOT / "plugins/snapshot/plugin.cpp"

# (own_floor, peer_block_num, expected)
CASES = [
    (0, 0, True),            # no state yet: nothing to refuse
    (0, 84800000, True),     # same, with a real block number
    (100, 99, False),        # below our finality: a rewind
    (100, 100, False),       # at our floor: gains nothing, still costs a full wipe
    (100, 101, True),        # strictly ahead: usable
    (84792899, 84792000, False),   # mainnet 31.10.2026: the rewound case
    (4294967295, 4294967295, False),  # no overflow on the boundary
]


def build_program(header_text):
    checks = "\n".join(
        '  CHECK({}u, {}u, {});'.format(floor, peer, "true" if want else "false")
        for floor, peer, want in CASES)
    return """\
#include <cstdint>
#include <iostream>

{header}

#define CHECK(f, p, want) do {{ \\
    if (::graphene::plugins::snapshot::snapshot_ahead_of_floor((uint32_t)(f), (uint32_t)(p)) != (want)) {{ \\
        std::cerr << "FAIL floor=" << (f) << " peer=" << (p) << " wanted " << (want) << "\\n"; \\
        ++bad; \\
    }} \\
}} while (0)

int main() {{
  int bad = 0;
{checks}
  return bad == 0 ? 0 : 1;
}}
""".format(header=header_text, checks=checks)


def run_truth_table(header_text, workdir):
    src = Path(workdir) / "floor_test.cpp"
    exe = Path(workdir) / "floor_test"
    src.write_text(build_program(header_text))
    subprocess.run(["g++", "-std=c++14", "-O0", "-o", str(exe), str(src)],
                   check=True, capture_output=True)
    proc = subprocess.run([str(exe)], capture_output=True, text=True)
    return proc.returncode, proc.stdout + proc.stderr


def plugin_text():
    return PLUGIN.read_text()


def wiring_problems(text):
    problems = []
    floor_call = "snapshot_ahead_of_floor(own_floor, info.block_num)"
    recheck_call = "snapshot_ahead_of_floor(recheck_floor, best->block_num)"
    if floor_call not in text:
        problems.append("phase-1 filter does not call the shared predicate")
    if "info.block_num <= own_floor" in text:
        problems.append("phase-1 filter still compares inline instead of using the predicate")
    if recheck_call not in text:
        problems.append("download path does not re-check the floor before the file can be imported")
    if "read_own_snapshot_floor(db)" not in text:
        problems.append("floor is not measured by read_own_snapshot_floor()")
    if text.count("snapshot_ahead_of_floor(") < 3:
        problems.append("predicate used fewer than three times: a call site is missing")

    import_call = "snapshot_ahead_of_floor(own_floor, header.snapshot_block_num)"
    if import_call not in text:
        problems.append("load_snapshot() does not refuse a file at or below our head/LIB "
                        "(the direct paths --snapshot / --snapshot-auto-latest / callback)")
    if import_call in text and "allow_snapshot_rewind ||" not in text:
        problems.append("load_snapshot() floor guard has no --allow-snapshot-rewind override")
    guard_at = text.find(import_call)
    load_at = text.find("void snapshot_plugin::plugin_impl::load_snapshot")
    wipe_at = text.find("Clearing existing objects before snapshot import")
    if guard_at >= 0:
        if load_at < 0 or guard_at < load_at:
            problems.append("floor guard is not inside load_snapshot()")
        if wipe_at < 0 or guard_at > wipe_at:
            problems.append("floor guard runs after the state wipe: the rewind already happened")
    recheck_at = text.find(recheck_call)
    if recheck_at >= 0:
        rename_at = text.find("boost::filesystem::rename(temp_path, final_path)")
        if rename_at < 0 or recheck_at > rename_at:
            problems.append("re-check is not before the rename that exposes the file for import")
        checksum_at = text.find("Checksum verified OK")
        if checksum_at < 0 or recheck_at < checksum_at:
            problems.append("re-check does not run after the checksum was verified")
    return problems


def selftest():
    header_text = HEADER.read_text()
    failures = []

    # (a) a mutation the truth table must catch: "ahead" relaxed to "not behind"
    with tempfile.TemporaryDirectory() as tmp:
        mutated = header_text.replace("peer_block_num > own_floor", "peer_block_num >= own_floor")
        if mutated == header_text:
            failures.append("selftest: could not mutate the header (operator not found)")
        else:
            rc, out = run_truth_table(mutated, tmp)
            if rc == 0:
                failures.append("selftest: '>=' mutation survived the truth table")

    # (b) a mutation the wiring check must catch: the caller-side re-check removed
    text = plugin_text()
    stripped = text.replace("snapshot_ahead_of_floor(recheck_floor, best->block_num)",
                            "true /* mutation */", 1)
    if stripped == text:
        failures.append("selftest: could not remove the re-check call")
    elif not wiring_problems(stripped):
        failures.append("selftest: missing re-check was not reported")

    # (c) floor read degraded to head only
    head_only = header_text.replace("own_floor == 0 ||", "")
    if head_only == header_text:
        failures.append("selftest: could not mutate the unknown-floor clause")
    elif run_truth_table(head_only, tempfile.mkdtemp())[0] == 0:
        failures.append("selftest: unknown-floor mutation survived")

    # (d) the load_snapshot() guard removed: the direct paths must be reported as unguarded
    stripped_load = text.replace(
        "snapshot_ahead_of_floor(own_floor, header.snapshot_block_num)", "true /* mutation */", 1)
    if stripped_load == text:
        failures.append("selftest: could not remove the load_snapshot() guard")
    elif not any("load_snapshot() does not refuse" in p for p in wiring_problems(stripped_load)):
        failures.append("selftest: missing load_snapshot() guard was not reported")

    # (e) the operator override deleted: an operator could no longer recover from a bad
    #     file without recompiling, so the wiring check must complain
    no_override = text.replace("allow_snapshot_rewind || ", "", 1)
    if no_override == text:
        failures.append("selftest: could not remove the override clause")
    elif not any("no --allow-snapshot-rewind override" in p for p in wiring_problems(no_override)):
        failures.append("selftest: missing override clause was not reported")

    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true",
                    help="prove the checks can go red (mutated header / wiring)")
    args = ap.parse_args()

    if not HEADER.is_file() or not PLUGIN.is_file():
        print("BLIND: missing {} or {}".format(HEADER, PLUGIN))
        return 2

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        rc, out = run_truth_table(HEADER.read_text(), tmp)
        if rc != 0:
            failures.append("truth table failed:\n" + out.strip())

    problems = wiring_problems(plugin_text())
    failures.extend(problems)

    if args.selftest:
        failures.extend(selftest())

    if failures:
        for f in failures:
            print("FAIL: " + f)
        return 1
    print("ok: snapshot floor truth table ({} cases) and three call sites".format(len(CASES)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
