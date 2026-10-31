#!/usr/bin/env python3
"""Behaviour of the timeout-vote journal on its own, against the real header.

The journal is what makes accumulated timeout votes survive a restart. It is deliberately
free of fc/chainbase, so this test compiles the production header as-is -- no fixture copy,
no stubbed production code -- and drives it through the cases the node will hit:

  * round trip: what was appended comes back byte for byte;
  * compaction: a replayed file keeps only the votes the node accepted;
  * garbage (bad hex, missing fields, control characters, truncated lines) is dropped, never fatal;
  * votes older than the round window never reach the signature check;
  * an oversized file is refused and truncated instead of read into memory;
  * snapshot import resets the cache.

Run: python3 tests/chain/test_timeout_vote_journal.py
     python3 tests/chain/test_timeout_vote_journal.py --selftest   (proves the cases can go red)
"""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'libraries' / 'chain' / 'include' / 'graphene' / 'chain' / 'timeout_vote_journal.hpp'

CPP = r'''
#include <graphene/chain/timeout_vote_journal.hpp>

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using graphene::chain::timeout_vote_journal;
using sig_t = timeout_vote_journal::signature_type;

static sig_t make_sig(unsigned char seed) {
    sig_t s{};
    for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<unsigned char>(seed + i);
    return s;
}

static long count_lines(const std::string &path) {
    std::FILE *f = std::fopen(path.c_str(), "r");
    assert(f);
    long n = 0;
    int c, prev = '\n';
    while ((c = std::fgetc(f)) != EOF) {
        if (c == '\n') ++n;
        prev = c;
    }
    if (prev != '\n' && prev != EOF) ++n;
    std::fclose(f);
    return n;
}

static bool exists(const std::string &path) {
    std::FILE *f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    std::fclose(f);
    return true;
}

static void append_raw(const std::string &path, const std::string &text) {
    std::FILE *f = std::fopen(path.c_str(), "a");
    assert(f);
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const std::string dir = argv[1];
    std::string error;

    // 1. Round trip: three votes in, three votes out, signature bytes intact.
    {
        const std::string path = dir + "/roundtrip.log";
        {
            timeout_vote_journal j(path);
            assert(j.open(error));
            j.append(100, 99, "viz1", make_sig(1));
            j.append(101, 100, "viz2", make_sig(2));
            j.append(102, 101, "viz3", make_sig(3));
        }
        assert(count_lines(path) == 3);
        timeout_vote_journal j(path);
        assert(j.open(error));
        std::vector<std::string> seen;
        std::vector<sig_t> sigs;
        const auto r = j.replay([&](uint32_t round, uint32_t hqc, const std::string &v, const sig_t &s) {
            seen.push_back(v + ":" + std::to_string(round) + ":" + std::to_string(hqc));
            sigs.push_back(s);
            return true;
        }, 0, error);
        assert(error.empty());
        assert(r.lines == 3 && r.accepted == 3 && r.dropped == 0);
        assert(seen.size() == 3);
        assert(seen[0] == "viz1:100:99");
        assert(seen[2] == "viz3:102:101");
        assert(sigs[0] == make_sig(1));
        assert(sigs[2] == make_sig(3));
        // A replay that accepts everything must not duplicate what it read.
        assert(count_lines(path) == 3);
    }

    // 2. Compaction: the node rejects one vote (duplicate) and one is stale -- only accepted stay.
    {
        const std::string path = dir + "/compact.log";
        append_raw(path, "200\t199\tviz1\t" + std::string(65 * 2, '0') + "\n");
        append_raw(path, "200\t199\tviz1\t" + std::string(65 * 2, '0') + "\n");  // duplicate line
        append_raw(path, "201\t200\tviz2\t" + std::string(65 * 2, '0') + "\n");
        timeout_vote_journal j(path);
        assert(j.open(error));
        int calls = 0;
        const auto r = j.replay([&](uint32_t round, uint32_t, const std::string &v, const sig_t &) {
            ++calls;
            return !(round == 200 && v == "viz1" && calls > 1);  // first wins, repeat rejected
        }, 0, error);
        assert(r.lines == 3);
        assert(r.accepted == 2);
        assert(r.dropped == 1);
        assert(count_lines(path) == 2);
        // Compaction replaces the file atomically and leaves no temporary behind.
        assert(!exists(path + ".tmp"));
        // Reopening a compacted file replays cleanly -- no growth on every start.
        timeout_vote_journal again(path);
        assert(again.open(error));
        const auto r2 = again.replay([](uint32_t, uint32_t, const std::string &, const sig_t &) { return true; }, 0, error);
        assert(r2.accepted == 2);
        assert(count_lines(path) == 2);
    }

    // 3. Garbage is dropped, not fatal: a damaged file must not keep the node from starting.
    {
        const std::string path = dir + "/garbage.log";
        append_raw(path, "300\t299\tviz1\t" + std::string(65 * 2, '0') + "\n");  // valid
        append_raw(path, "not-a-round\t299\tviz1\t" + std::string(65 * 2, '0') + "\n");
        append_raw(path, "301\t299\n");                                          // missing fields
        append_raw(path, "301\t299\tviz1\tdeadbeef\n");                          // short hex
        append_raw(path, "301\t299\tviz1\t" + std::string(65 * 2, 'z') + "\n");  // non-hex
        append_raw(path, "301\t299\tbad\tname\t" + std::string(65 * 2, '0') + "\n");  // extra field
        append_raw(path, "\n");
        const std::string truncated = "302\t301\tviz2\t" + std::string(130, '0');  // no trailing newline
        append_raw(path, truncated);
        timeout_vote_journal j(path);
        assert(j.open(error));
        int calls = 0;
        const auto r = j.replay([&](uint32_t, uint32_t, const std::string &, const sig_t &) {
            ++calls;
            return true;
        }, 0, error);
        assert(r.accepted == 2);   // the valid line and the truncated-but-complete last line
        assert(r.dropped == 5);
        assert(calls == 2);
        assert(count_lines(path) == 2);
    }

    // 4. The round window is applied before the callback: stale votes cost no signature check.
    {
        const std::string path = dir + "/window.log";
        append_raw(path, "10\t9\tviz1\t" + std::string(65 * 2, '0') + "\n");
        append_raw(path, "500\t499\tviz2\t" + std::string(65 * 2, '0') + "\n");
        append_raw(path, "900\t899\tviz3\t" + std::string(65 * 2, '0') + "\n");
        timeout_vote_journal j(path);
        assert(j.open(error));
        std::vector<uint32_t> rounds;
        const auto r = j.replay([&](uint32_t round, uint32_t, const std::string &, const sig_t &) {
            rounds.push_back(round);
            return true;
        }, 500, error);
        assert(rounds.size() == 2 && rounds[0] == 500 && rounds[1] == 900);
        assert(r.dropped == 1);
        assert(count_lines(path) == 2);
    }

    // 5. A validator name that would break the line format is never written.
    {
        const std::string path = dir + "/names.log";
        timeout_vote_journal j(path);
        assert(j.open(error));
        j.append(1, 0, "viz1", make_sig(1));
        j.append(1, 0, "bad\tname", make_sig(1));
        j.append(1, 0, "bad\nname", make_sig(1));
        j.append(1, 0, std::string(64, 'a'), make_sig(1));   // longer than any account name
        j.close();
        assert(count_lines(path) == 1);
    }

    // 6. An oversized file is a compaction bug, not data: refuse it and start over.
    {
        const std::string path = dir + "/huge.log";
        std::FILE *f = std::fopen(path.c_str(), "w");
        assert(f);
        std::string block(64 * 1024, 'x');
        size_t written = 0;
        while (written <= timeout_vote_journal::MAX_FILE_BYTES) {
            std::fwrite(block.data(), 1, block.size(), f);
            written += block.size();
        }
        std::fclose(f);
        timeout_vote_journal j(path);
        assert(j.open(error));
        int calls = 0;
        const auto r = j.replay([&](uint32_t, uint32_t, const std::string &, const sig_t &) {
            ++calls;
            return true;
        }, 0, error);
        assert(calls == 0);
        assert(r.lines == 0);
        assert(!error.empty());
        assert(count_lines(path) == 0);
        j.append(7, 6, "viz1", make_sig(7));
        j.close();
        assert(count_lines(path) == 1);
    }

    // 7. Snapshot import wipes the chain state, so the cache goes with it.
    {
        const std::string path = dir + "/wipe.log";
        append_raw(path, "400\t399\tviz1\t" + std::string(65 * 2, '0') + "\n");
        timeout_vote_journal j(path);
        assert(j.open(error));
        assert(j.truncate(error));
        assert(count_lines(path) == 0);
        j.append(1, 0, "viz1", make_sig(1));
        j.close();
        assert(count_lines(path) == 1);
    }

    // 8. Using a journal that was never opened is a no-op, not a crash.
    {
        timeout_vote_journal j(dir + "/never.log");
        j.append(1, 0, "viz1", make_sig(1));
        j.flush();
        j.close();
        std::string ignored;
        const auto r = j.replay([](uint32_t, uint32_t, const std::string &, const sig_t &) { return true; }, 0, ignored);
        assert(r.lines == 0);
        assert(!ignored.empty());   // "not open" is said out loud, not silently green
    }

    std::puts("PASS: timeout vote journal round trip, compaction, garbage, window, oversize, wipe, no-open");
    return 0;
}
'''


INCLUDE_ROOT = ROOT / 'libraries' / 'chain' / 'include'

# Each mutation must make the suite RED. If one stays green, the case it targets is vacuous.
MUTATIONS = {
    'window-prefilter-ignored': (
        'if (round < min_round) {', 'if (false && round < min_round) {'),
    'validator-name-unguarded': (
        'if (!valid_validator(validator)) return;', 'if (false) return;'),
    'no-compaction-rewrite': (
        'if (!rewrite(keep, rewrite_error) && error.empty()) error = rewrite_error;',
        '(void)rewrite_error;'),
    'signature-bytes-dropped': (
        'signature[i] = static_cast<unsigned char>(hi * 16 + lo);',
        'signature[i] = 0;'),
}


class CompileFailed(Exception):
    pass


def build_and_run(include_root, workdir):
    src = Path(workdir) / 'test.cpp'
    binary = Path(workdir) / 'test'
    src.write_text(CPP)
    compiled = subprocess.run([
        os.environ.get('CXX', 'c++'), '-std=c++14', '-Wall', '-Wextra', '-Werror',
        '-I', str(include_root), str(src), '-o', str(binary),
    ], capture_output=True, text=True)
    if compiled.returncode != 0:
        raise CompileFailed(compiled.stderr)
    return subprocess.run([str(binary), str(Path(workdir) / 'data')],
                          capture_output=True, text=True)


def selftest():
    failures = []
    with tempfile.TemporaryDirectory(prefix='viz-tvj-mut-') as tmp:
        for name, (old, new) in MUTATIONS.items():
            root = Path(tmp) / name
            header_dir = root / 'graphene' / 'chain'
            header_dir.mkdir(parents=True)
            text = HEADER.read_text()
            if old not in text:
                failures.append(f'{name}: anchor not found -- mutation did not land')
                continue
            (header_dir / HEADER.name).write_text(text.replace(old, new, 1))
            data = Path(tmp) / f'{name}-data'
            data.mkdir()
            try:
                result = build_and_run(root, data)
            except CompileFailed as exc:
                failures.append(f'{name}: mutation does not compile\n{exc}')
                continue
            if result.returncode == 0:
                failures.append(f'{name}: suite stayed GREEN (the case it targets is vacuous)')
            else:
                print(f'RED as expected: {name}')
    if failures:
        for f in failures:
            print(f'BAD: {f}', file=sys.stderr)
        return 1
    print(f'PASS: {len(MUTATIONS)} mutations, every one red')
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--selftest', action='store_true')
    args = parser.parse_args()
    if not HEADER.exists():
        print(f'MISSING: {HEADER}', file=sys.stderr)
        return 2
    if args.selftest:
        return selftest()
    with tempfile.TemporaryDirectory(prefix='viz-tvj-') as tmp:
        (Path(tmp) / 'data').mkdir()
        result = build_and_run(INCLUDE_ROOT, tmp)
        sys.stdout.write(result.stdout)
        sys.stderr.write(result.stderr)
        return result.returncode


if __name__ == '__main__':
    sys.exit(main())
