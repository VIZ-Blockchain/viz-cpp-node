#!/usr/bin/env python3
"""A failed P2P snapshot import must NAME its cause, not just say it failed.

Measured 01.11.2026 on the published image (8dff776f): a fresh node asked a trusted peer
for its snapshot, downloaded it, parsed the JSON, and then logged only

    P2P snapshot import failed — wiping corrupted shared memory before restart
    P2P snapshot download thread failed

while the very same file loaded through `--snapshot` printed the real reason:
"header.chain_id == db.get_chain_id(): Chain ID mismatch: snapshot=441adba…, node=2040effd…".
The operator (and any later reader of the log) could not tell "incompatible snapshot" from
"corrupt state", and the node silently fell back to syncing from genesis.

This test is the carrier for the fix: both catch sites log the exception, and the failure
path still wipes state and rethrows exactly as before (the fix must not change behaviour,
only its report).

Run: python3 tests/chain/test_snapshot_import_cause.py [--selftest]
"""
import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLUGIN = ROOT / "plugins/snapshot/plugin.cpp"

IMPORT_ANCHOR = "if (!snapshot_path.empty())"
THREAD_ANCHOR = "P2P snapshot download thread failed"


def import_region(text):
    at = text.find(IMPORT_ANCHOR)
    if at < 0:
        return ""
    end = text.find("Snapshot sync complete", at)
    return text[at:end if end > at else at + 4000]


def thread_region(text):
    at = text.find("P2P snapshot sync exhausted after")
    return text[at:at + 1600] if at >= 0 else ""


def wiring_problems(text):
    problems = []
    imp = import_region(text)
    if not imp:
        return ["import path not found: the anchor moved"]

    # 1. Every catch in the import path names the exception.
    if 'elog("P2P snapshot import failed: ${e}' not in imp:
        problems.append("import failure does not print the exception: 'failed' alone hides the cause")
    if '("e", e.to_detail_string())' not in imp or '("e", e.what())' not in imp:
        problems.append("import failure does not bind both fc::exception and std::exception causes")
    if imp.count("P2P snapshot import failed") < 3:
        problems.append("a catch clause in the import path still reports without a cause")
    if "P2P snapshot import failed: unknown exception" not in imp:
        problems.append("the (...)-fallback is gone: an unknown throw would escape unlabelled")

    # 2. Behaviour unchanged: wipe + rethrow still happen on failure.
    if imp.count("chain_plug.wipe_state()") < 3:
        problems.append("wipe_state() is not reached from every catch: an exception path may leave "
                        "a half-imported state behind")
    if imp.count("throw;") < 3:
        problems.append("failure no longer rethrows: the outer handler would think the import worked")

    # 3. The thread-level swallow names the cause too.
    thr = thread_region(text)
    if not thr:
        problems.append("thread-level catch not found: the anchor moved")
    elif '("e", e.to_detail_string())' not in thr or '("e", e.what())' not in thr:
        problems.append("thread-level catch does not print the exception")
    elif "P2P snapshot download thread failed: unknown exception" not in thr:
        problems.append("thread-level (...)-fallback is gone: an unknown throw would escape unlabelled")
    return problems


def selftest():
    text = PLUGIN.read_text()
    failures = []

    def expect_red(name, mutated, needle):
        if mutated == text:
            failures.append("selftest %s: mutation did not apply" % name)
            return
        problems = wiring_problems(mutated)
        if not problems:
            failures.append("selftest %s: mutation survived the wiring check" % name)
        elif needle and not any(needle in p for p in problems):
            failures.append("selftest %s: red for the wrong reason: %s" % (name, problems))

    # (a) import message loses the cause
    expect_red("import-cause-removed",
               text.replace('elog("P2P snapshot import failed: ${e} — wiping shared memory before restart",\n'
                            '                                         ("e", e.to_detail_string()));',
                            'elog("P2P snapshot import failed — wiping shared memory before restart");'),
               "bind both")
    # (b) thread message loses the cause
    expect_red("thread-cause-removed",
               text.replace('elog("P2P snapshot download thread failed: ${e}", ("e", e.to_detail_string()));',
                            'elog("P2P snapshot download thread failed");')
                   .replace('elog("P2P snapshot download thread failed: ${e}", ("e", e.what()));',
                            'elog("P2P snapshot download thread failed");'),
               "thread-level catch does not print the exception")
    # (c) wipe removed from the import path
    expect_red("wipe-removed", text.replace("chain_plug.wipe_state();", "/* removed */"),
               "wipe_state()")
    # (d) rethrow removed
    expect_red("rethrow-removed", text.replace("throw;", "return;"), "rethrows")

    baseline = wiring_problems(text)
    if baseline:
        failures.append("live file is already red: " + "; ".join(baseline))
    return failures


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        failures = selftest()
        for f in failures:
            print("FAIL " + f)
        print("selftest: %s" % ("ok" if not failures else "failed"))
        return 0 if not failures else 1

    problems = wiring_problems(PLUGIN.read_text())
    for p in problems:
        print("FAIL " + p)
    print("snapshot import cause: %s" % ("ok" if not problems else "%d problem(s)" % len(problems)))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
