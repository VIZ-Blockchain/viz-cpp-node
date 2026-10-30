#!/usr/bin/env python3
"""Run consensus_sim_tests one test case per process, N processes at a time.

The suite is a single Boost.Test binary; run serially it takes ~15-20 min on a
4-core CI runner, almost all of it in one core. Test cases are independent
(each builds its own node in a unique temp dir, failure logs are named by seed
and scenario), so they can run side by side.

Usage:
    run_parallel.py BINARY [-j N] [--filter SUITE[,SUITE...]] [-- EXTRA_ARGS...]

Exit status is 0 only if every case passed. The slowest cases are printed at
the end so the split can be checked from the CI log.
"""
import argparse
import os
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor, as_completed


def list_cases(binary):
    """Return 'suite/.../case' paths of enabled test cases, in declaration order."""
    # Boost.Test prints the tree to stderr on older versions and stdout on newer.
    out = subprocess.run([binary, "--list_content"], stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, check=True).stdout
    rows = []
    for line in out.splitlines():
        name = line.strip()
        if not name:
            continue
        indent = len(line) - len(line.lstrip(" "))
        enabled = name.endswith("*")
        rows.append((indent, name.rstrip("*").strip(), enabled))
    if not rows:
        sys.exit("run_parallel: --list_content returned nothing:\n" + out)

    step = min((i for i, _, _ in rows if i > 0), default=4)
    cases, stack = [], []
    for idx, (indent, name, enabled) in enumerate(rows):
        depth = indent // step
        del stack[depth:]
        stack.append(name)
        is_leaf = idx + 1 == len(rows) or rows[idx + 1][0] <= indent
        if is_leaf and enabled:
            cases.append("/".join(stack))
    return cases


def run_case(binary, case, extra):
    start = time.monotonic()
    proc = subprocess.run([binary, "--run_test=" + case] + extra,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    return case, proc.returncode, time.monotonic() - start, proc.stdout


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("binary")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--filter", help="comma-separated top-level suites to run")
    ap.add_argument("--list", action="store_true", help="print the cases and exit")
    ap.add_argument("extra", nargs="*", help="extra args passed to every run (after --)")
    args = ap.parse_args()

    binary = os.path.abspath(args.binary)
    cases = list_cases(binary)
    if args.filter:
        keep = set(args.filter.split(","))
        cases = [c for c in cases if c.split("/")[0] in keep]
    if args.list:
        print("\n".join(cases))
        return 0
    if not cases:
        sys.exit("run_parallel: no test cases selected")

    print(f"run_parallel: {len(cases)} cases, {args.jobs} at a time", flush=True)
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
