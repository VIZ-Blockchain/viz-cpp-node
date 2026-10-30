#!/usr/bin/env python3
"""Run consensus_sim_tests one test case per process, N processes at a time.

The suite is a single Boost.Test binary; run serially it takes ~15-20 min on a
4-core CI runner, almost all of it in one core. Test cases are independent
(each builds its own node in a unique temp dir, failure logs are named by seed
and scenario), so they can run side by side.

Usage:
    run_parallel.py BINARY [-j N] [--filter SUITE[,SUITE...]] [--min-cases N]
                    [-- EXTRA_ARGS...]

The case list comes from the DOT listing (`--list_content=DOT`) and must match
the indented `--list_content` listing exactly; any mismatch, or fewer enabled
cases than --min-cases, aborts the run instead of quietly running a subset.
Disabled cases are counted and listed.

Exit status is 0 only if every case passed. The slowest cases are printed at
the end so the split can be checked from the CI log.
"""
import argparse
import re
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed


def _list_output(binary, fmt=None):
    arg = "--list_content" + ("=" + fmt if fmt else "")
    # Boost.Test prints the tree to stderr on older versions and stdout on newer.
    return subprocess.run([binary, arg], stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, check=True).stdout


def list_cases_hrf(binary):
    """Enabled leaf paths from the indented listing (a trailing '*' = enabled).

    A row is a leaf when the next row is not indented deeper. An empty suite
    also looks like a leaf, but it is never enabled, so the enabled list is
    still exactly the enabled test cases.
    """
    out = _list_output(binary)
    rows = []
    for line in out.splitlines():
        name = line.strip()
        if not name:
            continue
        indent = len(line) - len(line.lstrip(" "))
        rows.append((indent, name.rstrip("*").strip(), name.endswith("*")))
    if not rows:
        sys.exit("run_parallel: --list_content returned nothing:\n" + out)

    step = min((i for i, _, _ in rows if i > 0), default=4)
    cases, stack = [], []
    for idx, (indent, name, enabled) in enumerate(rows):
        del stack[indent // step:]
        stack.append(name)
        is_leaf = idx + 1 == len(rows) or rows[idx + 1][0] <= indent
        if is_leaf and enabled:
            cases.append("/".join(stack))
    return cases


# DOT listing: `tuN[...,color=green|yellow,label="name|file(line)"];` per node and
# `tuP -> tuC;` per edge. Boost.Test gives test cases ids with bit 16 set
# (TUT_CASE = 0x10000) and suites ids without it, so cases are identified by id,
# not by layout. Enabled nodes are green, disabled ones yellow.
_DOT_NODE = re.compile(r'^tu(\d+)\[.*color=(\w+),label="([^|"]*)')
_DOT_EDGE = re.compile(r'^tu(\d+) -> tu(\d+);')
_TUT_CASE = 0x10000


def list_cases_dot(binary):
    """(enabled, disabled) test-case paths from the DOT listing."""
    nodes, parent, order = {}, {}, []
    for line in _list_output(binary, "DOT").splitlines():
        line = line.strip()
        m = _DOT_NODE.match(line)
        if m:
            nid = int(m.group(1))
            nodes[nid] = (m.group(3), m.group(2) == "green")
            order.append(nid)
            continue
        m = _DOT_EDGE.match(line)
        if m:
            parent[int(m.group(2))] = int(m.group(1))

    def path(nid):
        parts = []
        while nid in parent:          # the master suite has no parent and no name in paths
            parts.append(nodes[nid][0])
            nid = parent[nid]
        return "/".join(reversed(parts))

    enabled, disabled = [], []
    for nid in order:
        if nid & _TUT_CASE:
            (enabled if nodes[nid][1] else disabled).append(path(nid))
    return enabled, disabled


def list_cases(binary):
    """Enabled and disabled case paths, from two independent listings that must agree.

    A Boost.Test format change that made one parser return a subset would
    otherwise let CI go green having run fewer cases.
    """
    enabled, disabled = list_cases_dot(binary)
    hrf = list_cases_hrf(binary)
    if sorted(hrf) != sorted(enabled):
        only_dot = sorted(set(enabled) - set(hrf))[:5]
        only_hrf = sorted(set(hrf) - set(enabled))[:5]
        sys.exit(f"run_parallel: case list mismatch: DOT listing has {len(enabled)} enabled "
                 f"cases, indented listing {len(hrf)} (only in DOT: {only_dot}, "
                 f"only in indented: {only_hrf}). Refusing to run a partial set.")
    return enabled, disabled


def run_case(binary, case, extra):
    start = time.monotonic()
    proc = subprocess.run([binary, "--run_test=" + case] + extra,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    rc = proc.returncode
    # A filter that matches nothing is a "Test setup error" with exit 0 on some
    # Boost versions; that must not count as a pass.
    if rc == 0 and "no test cases matching filter" in proc.stdout:
        rc = 2
    return case, rc, time.monotonic() - start, proc.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--filter", help="comma-separated top-level suites to run")
    ap.add_argument("--list", action="store_true", help="print the cases and exit")
    ap.add_argument("--min-cases", type=int, default=0,
                    help="fail if the binary has fewer enabled cases than this (before --filter)")
    ap.add_argument("extra", nargs="*", help="extra args passed to every run (after --)")
    args = ap.parse_args()

    binary = os.path.abspath(args.binary)
    cases, disabled = list_cases(binary)

    if args.min_cases and len(cases) < args.min_cases:
        sys.exit(f"run_parallel: only {len(cases)} enabled cases, expected at least "
                 f"{args.min_cases}. Was a test file dropped from the build?")

    if args.filter:
        keep = set(args.filter.split(","))
        missing = keep - {c.split("/")[0] for c in cases}
        if missing:
            sys.exit("run_parallel: --filter names no enabled cases: " + ", ".join(sorted(missing)))
        cases = [c for c in cases if c.split("/")[0] in keep]
        disabled = [c for c in disabled if c.split("/")[0] in keep]
    if args.list:
        print("\n".join(cases))
        return 0
    if not cases:
        sys.exit("run_parallel: no test cases selected")

    print(f"run_parallel: {len(cases)} cases, {args.jobs} at a time; "
          f"skipped (disabled): {len(disabled)}", flush=True)
    for case in disabled:
        print(f"  disabled: {case}", flush=True)
    t0 = time.monotonic()
    results, failed = [], []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_case, binary, c, args.extra) for c in cases]
        for fut in as_completed(futures):
            case, rc, dt, out = fut.result()
            results.append((dt, case))
            status = "ok" if rc == 0 else f"FAILED (exit {rc})"
            print(f"[{len(results)}/{len(cases)}] {case}: {status} {dt:.1f}s", flush=True)
            if rc != 0:
                failed.append((case, out))

    for case, out in failed:
        print(f"\n===== {case} =====\n{out}", flush=True)

    print(f"\nrun_parallel: wall {time.monotonic() - t0:.0f}s, "
          f"sum of cases {sum(d for d, _ in results):.0f}s")
    print("slowest cases:")
    for dt, case in sorted(results, reverse=True)[:10]:
        print(f"  {dt:7.1f}s  {case}")

    if failed:
        print(f"\nrun_parallel: {len(failed)} of {len(cases)} cases failed:")
        for case, _ in failed:
            print(f"  {case}")
        return 1
    print(f"run_parallel: all {len(cases)} cases passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
