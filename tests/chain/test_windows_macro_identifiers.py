#!/usr/bin/env python3
"""No identifier in the Windows-built sources may be named near, far or small.

The MinGW build of `vizd` includes `windef.h`, which keeps three 16-bit-era macros alive:

    #define near    /* empty */
    #define far     /* empty */
    #define small   char

They are empty (or a type), so a declaration such as `std::string near;` compiles to `std::string ;`
and the Windows Release job dies on a line Linux accepts without a warning. That is exactly what
happened on 01.11.2026: `database::qc_state_report()` declared `std::string near;` (7897
`libraries/chain/database.cpp`), master's Linux CI stayed green, and the Windows job has been red
since #233 -- it only runs on pushes to master, so no PR could have seen it.

This file is the offline bearer for that class: it strips comments and literals, then refuses any
remaining token that is exactly one of those macro names.

Run: python3 tests/chain/test_windows_macro_identifiers.py [--selftest]
"""
import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
TREES = ('libraries', 'plugins', 'programs')
SUFFIXES = ('.cpp', '.hpp', '.h', '.cc')
MACROS = ('near', 'far', 'small')

TOKEN = re.compile(r'(?<![A-Za-z0-9_])(%s)(?![A-Za-z0-9_])' % '|'.join(MACROS))


def _masked(source):
    """Same-length copy with comments, string/char literals and raw strings blanked out."""
    out = list(source)
    i, n = 0, len(source)
    while i < n:
        c = source[i]
        if c == '/' and i + 1 < n and source[i + 1] == '/':
            while i < n and source[i] != '\n':
                out[i] = ' '
                i += 1
        elif c == '/' and i + 1 < n and source[i + 1] == '*':
            out[i] = out[i + 1] = ' '
            i += 2
            while i + 1 < n and not (source[i] == '*' and source[i + 1] == '/'):
                out[i] = ' '
                i += 1
            if i + 1 < n:
                out[i] = out[i + 1] = ' '
                i += 2
        elif c == 'R' and i + 1 < n and source[i + 1] == '"':
            close = source.find(')"', i + 2)
            if close < 0:
                close = n - 2
            for j in range(i, min(close + 2, n)):
                out[j] = ' '
            i = close + 2
        elif c in ('"', "'"):
            quote = c
            out[i] = ' '
            i += 1
            while i < n and source[i] != quote:
                out[i] = ' '
                if source[i] == '\\' and i + 1 < n:
                    out[i + 1] = ' '
                    i += 1
                i += 1
            if i < n:
                out[i] = ' '
                i += 1
        else:
            i += 1
    return ''.join(out)


def problems_in(source):
    """Lines that use a Win32 macro name as an identifier. Empty list = clean."""
    masked = _masked(source)
    if len(masked) != len(source):
        raise AssertionError('masking changed the length; offsets would be wrong')
    problems = []
    for lineno, line in enumerate(masked.splitlines(), 1):
        for m in TOKEN.finditer(line):
            problems.append('line %d: `%s` is a windef.h macro -- rename it (%s)' %
                            (lineno, m.group(1), line.strip()[:80]))
    return problems


def scan():
    found = []
    for tree in TREES:
        for path in sorted((ROOT / tree).rglob('*')):
            if path.suffix not in SUFFIXES:
                continue
            try:
                source = path.read_text(errors='replace')
            except OSError as exc:
                found.append('%s: unreadable (%s)' % (path, exc))
                continue
            for problem in problems_in(source):
                found.append('%s: %s' % (path.relative_to(ROOT), problem))
    return found


def selftest():
    """Every arm must answer as expected, or the check proves nothing."""
    arms = [
        ('declaration', 'std::string near;\n', 1),
        ('other type', 'int far = 0;\n', 1),
        ('char-named local', 'unsigned char small;\n', 1),
        ('line comment', '// near is empty on Windows\n', 0),
        ('block comment', '/* far and small */\nint x;\n', 0),
        ('string literal', 'out += "far away, near head, small pool";\n', 0),
        ('raw string', 'const char* js = R"(var near = 1; var far = 2;)";\n', 0),
        ('renamed local', 'std::string near_qcs;\n', 0),
        ('similar name', 'size_t nearest = 0;\n', 0),
    ]
    bad = 0
    for name, source, expected in arms:
        problems = problems_in(source)
        if len(problems) != expected:
            bad += 1
            print('SELFTEST FAIL %s: expected %d, got %d (%s)' % (name, expected, len(problems), problems))
    if bad:
        print('selftest: %d arm(s) wrong -- the check is not trustworthy' % bad)
        return 1
    print('selftest: %d arms pass' % len(arms))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--selftest', action='store_true', help='prove the check can go red')
    args = parser.parse_args()

    if args.selftest:
        rc = selftest()
        if rc:
            return rc

    found = scan()
    if found:
        print('Windows Release would fail on these identifiers:')
        for item in found:
            print('  ' + item)
        return 1
    print('no windef.h macro names used as identifiers in %s' % ', '.join(TREES))
    return 0


if __name__ == '__main__':
    sys.exit(main())
