#!/usr/bin/env python3
"""Every Python test under tests/ must be invoked by some GitHub workflow run block.

Why this exists: CMake knows only the C++ suites (`add_test` in tests/*/CMakeLists.txt),
so a Python test file left in the repo is not run by ctest and not collected by any glob.
It runs only if a workflow names it. A missing line never fails anything -- the job still
passes (a job fails on a RED test, not on an ABSENT one), so the only symptom is a test
that silently does not exist. Found on the HF17 wiring test (PR #239), which had to be
wired in by hand into the offline-checks step.

Registration is searched across ALL workflow run blocks, not one step: the same mistake
in the other direction is a guard that demands a particular step and reports a test that
is registered elsewhere (tests/p2p/test_corruption_recovery.py runs in its own step).

Codes: 0 clean; 1 an unregistered test file (or a --selftest-capable one without its
--selftest invocation, which would leave the mutations unmeasured); 2 blindness (no
workflows readable, or no test files at all -- silence must not read as clean).

Run: python3 scripts/check_offline_tests_registered.py [--selftest]
"""
import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKFLOWS = ROOT / '.github' / 'workflows'
TESTS = ROOT / 'tests'


def run_blocks(workflow_sources):
    """Concatenated `run:` block bodies of every workflow, comments blanked."""
    blocks = []
    for text in workflow_sources:
        lines = text.splitlines()
        i = 0
        while i < len(lines):
            m = re.match(r'^(\s*)(?:-\s+)?run:\s*(.*)$', lines[i])
            if not m:
                i += 1
                continue
            base_indent = len(m.group(1))
            body = [m.group(2)]
            i += 1
            while i < len(lines):
                line = lines[i]
                if not line.strip():
                    body.append('')
                    i += 1
                    continue
                indent = len(line) - len(line.lstrip())
                if indent <= base_indent and not line.lstrip().startswith('#'):
                    break
                body.append(line)
                i += 1
            blocks.append('\n'.join(re.sub(r'(?<!\S)#.*$', '', l) for l in body))
    return '\n'.join(blocks)


def registration_problems(workflows_text, test_sources):
    """test_sources: list of (path, source). Returns a list of human-readable problems."""
    if not workflows_text.strip():
        return ['no workflow run block is readable -- nothing to register against']
    problems = []
    for path, source in test_sources:
        rel = path.relative_to(ROOT).as_posix()
        registered = rel in workflows_text
        if not registered:
            problems.append('%s is in the repo but no workflow run block invokes it: it runs nowhere '
                            '(CMake only knows the C++ suites), and the job stays green because an '
                            'absent line cannot fail' % rel)
            continue
        if '--selftest' in source and ('--selftest' not in _command_line(workflows_text, rel)):
            problems.append('%s has a --selftest but no workflow invokes it with --selftest: the '
                            'mutations that prove the checks can redden are never run' % rel)
    return problems


def _command_line(workflows_text, rel):
    """The workflow lines that name `rel` -- the selftest flag must sit on one of them."""
    return '\n'.join(line for line in workflows_text.splitlines() if rel in line)


def collect():
    sources = []
    for path in sorted(TESTS.rglob('test_*.py')):
        if '__pycache__' in path.parts:
            continue
        sources.append((path, path.read_text()))
    return sources


def load_workflows():
    if not WORKFLOWS.is_dir():
        return None
    return [p.read_text() for p in sorted(WORKFLOWS.glob('*.yml'))]


def selftest():
    sources = collect()
    workflows = load_workflows()
    if not sources:
        print('BLIND: no tests/**/test_*.py found', file=sys.stderr)
        return 2
    if not workflows:
        print('BLIND: no workflows found', file=sys.stderr)
        return 2
    text = run_blocks(workflows)
    sample_rel = sources[0][0].relative_to(ROOT).as_posix()
    failures = []
    findings = 0
    # A: remove the line naming one real test -> it must be reported.
    mutated = '\n'.join(l for l in text.splitlines() if sample_rel not in l)
    if mutated == text:
        failures.append('mutation A did not land (no line names %s)' % sample_rel)
    else:
        hits = registration_problems(mutated, sources)
        findings += len(hits)
        if not hits:
            failures.append('mutation A stayed GREEN: an unregistered test was not reported')
    # B: drop every tests/ path -> every test must be reported.
    wiped = '\n'.join(l for l in text.splitlines() if 'tests/' not in l)
    hits = registration_problems(wiped, sources)
    findings += len(hits)
    if len(hits) < len(sources):
        failures.append('mutation B reported %d of %d tests -- the scan is partial' % (len(hits), len(sources)))
    # C: no run blocks at all -> blindness, not a pass.
    if not registration_problems('', sources):
        failures.append('mutation C stayed GREEN: an unreadable workflow did not read as blindness')
    if failures:
        for f in failures:
            print('BAD: %s' % f, file=sys.stderr)
        return 1
    print('PASS: 3 mutations, every one red (%d findings)' % findings)
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--selftest', action='store_true')
    args = parser.parse_args()
    if args.selftest:
        return selftest()
    workflows = load_workflows()
    if not workflows:
        print('BLIND: no workflows under %s' % WORKFLOWS.relative_to(ROOT), file=sys.stderr)
        return 2
    sources = collect()
    if not sources:
        print('BLIND: no tests/**/test_*.py found -- "no problems" would be a lie', file=sys.stderr)
        return 2
    text = run_blocks(workflows)
    if not text.strip():
        print('BLIND: workflow files contain no run blocks', file=sys.stderr)
        return 2
    problems = registration_problems(text, sources)
    if problems:
        for p in problems:
            print('FINDING: %s' % p)
        return 1
    print('PASS: %d Python tests under tests/ and every one is invoked by a workflow run block'
          % len(sources))
    return 0


if __name__ == '__main__':
    sys.exit(main())
