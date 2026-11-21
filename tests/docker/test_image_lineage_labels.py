#!/usr/bin/env python3
"""Every image we publish must answer "which commit am I?" by itself.

Context (21.11.2026). Asked whether the image on the testnet stand carried the vote-journal lock
fix, I could not answer from the image. `image.version` is `24.04` (the base Ubuntu), no label
carried a source revision, and the release version lives inside the binary - so the answer had to
be argued from two indirect facts (a `blockchain version` string that existed only in that PR's
tree, and the digest changing right after that PR's build). Every one of those looks like proof
and none of them is: a tag, a digest or `strings` on a binary each describe what somebody did,
not what the image contains.

The class this file guards is at the SEAM, and both halves fail silently on their own:

  * Dockerfile declares the labels but the workflow does not pass the build-arg  -> label reads
    `unknown` on every published image, and "unknown" is indistinguishable from "old image";
  * the workflow passes the arg but the Dockerfile never uses it                  -> label absent,
    and `docker inspect` returns an empty string that reads like a missing feature.

So both are checked together, per image, for the two Dockerfiles we actually ship.

The default is required to be the literal `unknown`: a default like `dev` or a plausible sha would
make an unlabelled image look labelled, which is the failure this whole file exists to prevent
(zero is a legal value, and the blindness hides behind it).

Run: python3 tests/docker/test_image_lineage_labels.py [--selftest]
"""
import argparse
import re
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
DOCKERFILES = (
    'share/vizd/docker/Dockerfile-testnet',
    'share/vizd/docker/Dockerfile-production',
)
WORKFLOWS = (
    '.github/workflows/docker-main.yml',
    '.github/workflows/docker-pr-build.yml',
)
REVISION_LABEL = 'org.opencontainers.image.revision'
FALLBACK = 'unknown'


def _comment_lines(text):
    """1-based numbers of lines whose content is comment-only or blank (for message context)."""
    out = set()
    for i, line in enumerate(text.splitlines(), 1):
        stripped = line.strip()
        if not stripped or stripped.startswith('#'):
            out.add(i)
    return out


def dockerfile_problems(text, name='<dockerfile>'):
    """Findings for one Dockerfile: the ARG exists, its fallback is honest, a LABEL consumes it."""
    problems = []
    comments = _comment_lines(text)
    lines = text.splitlines()

    arg_re = re.compile(r'^\s*ARG\s+GIT_REVISION(?:=(\S+))?\s*$')
    arg_values = [(i, m.group(1)) for i, line in enumerate(lines, 1)
                  if (m := arg_re.match(line)) and i not in comments]
    if not arg_values:
        problems.append('%s: no `ARG GIT_REVISION` outside comments - the label has nothing to '
                        'read, and an empty label reads like an absent feature' % name)
    else:
        for lineno, value in arg_values:
            if value is None:
                problems.append('%s:%d ARG GIT_REVISION has NO default - a build without the '
                                'build-arg fails or silently bakes an empty label' % (name, lineno))
            elif value != FALLBACK:
                problems.append('%s:%d ARG GIT_REVISION defaults to %r, not %r - an image built '
                                'without the arg would look labelled'
                                % (name, lineno, value, FALLBACK))

    # A LABEL statement can span lines with trailing backslashes, so it is read as a statement --
    # not as "the line that mentions the label name". Looking for the name alone was wrong in the
    # selftest: a commented-out `LABEL` header still left the continuation line carrying the name,
    # and the check stayed green on a Dockerfile with no label at all.
    statements = []
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.strip().startswith('LABEL ') and (i + 1) not in comments:
            start = i + 1
            parts = []
            while i < len(lines):
                parts.append(lines[i])
                if not lines[i].rstrip().endswith('\\'):
                    break
                i += 1
            statements.append((start, ' '.join(parts)))
        i += 1

    labelled = [(n, st) for n, st in statements if REVISION_LABEL in st]
    if not labelled:
        problems.append('%s: no LABEL %s - `docker inspect` cannot name the commit'
                        % (name, REVISION_LABEL))
    else:
        for lineno, statement in labelled:
            if '${GIT_REVISION}' not in statement:
                problems.append('%s:%d LABEL %s does not interpolate ${GIT_REVISION} - the label '
                                'is a constant and can never disagree with reality'
                                % (name, lineno, REVISION_LABEL))
    return problems


def workflow_problems(text, name='<workflow>'):
    """Findings for one workflow: every docker build step passes GIT_REVISION."""
    problems = []
    comments = _comment_lines(text)
    lines = text.splitlines()

    # A build step is `docker/build-push-action` plus its `with:` block; the block ends at the
    # next line whose indentation is <= the `- uses:` line. Counting is enough here: the claim is
    # "each build passes the arg", so we compare the two counts and, when they differ, point at
    # the steps that do not.
    steps = []
    for i, line in enumerate(lines):
        if 'docker/build-push-action' in line and (i + 1) not in comments:
            indent = len(line) - len(line.lstrip())
            block = [line]
            for follow in lines[i + 1:]:
                if follow.strip() and (len(follow) - len(follow.lstrip())) <= indent \
                        and follow.lstrip().startswith('- '):
                    break
                block.append(follow)
            steps.append((i + 1, '\n'.join(block)))

    if not steps:
        problems.append('%s: no docker/build-push-action step found - either the workflow moved '
                        'or this check reads the wrong file' % name)
        return problems

    missing = [lineno for lineno, block in steps if 'GIT_REVISION=' not in block]
    if missing:
        problems.append('%s: %d of %d image build step(s) do not pass GIT_REVISION (lines %s) - '
                        'those images carry the %r fallback while looking published'
                        % (name, len(missing), len(steps),
                           ', '.join(str(n) for n in missing), FALLBACK))
    return problems


def problems_in(dockerfile_text, workflow_text, name='<fixture>'):
    return (dockerfile_problems(dockerfile_text, name + '.Dockerfile')
            + workflow_problems(workflow_text, name + '.workflow'))


def scan():
    problems = []
    for rel in DOCKERFILES:
        problems += dockerfile_problems((ROOT / rel).read_text(), rel)
    for rel in WORKFLOWS:
        problems += workflow_problems((ROOT / rel).read_text(), rel)
    return problems


GOOD_DOCKERFILE = '''
FROM phusion/baseimage:noble-1.0.3 AS production
ARG GIT_REVISION=unknown
ARG GIT_REF=unknown
LABEL org.opencontainers.image.title="vizd" \\
      org.opencontainers.image.revision="${GIT_REVISION}" \\
      org.opencontainers.image.ref.name="${GIT_REF}"
'''

GOOD_WORKFLOW = '''
      - uses: docker/build-push-action@v7
        with:
          context: .
          file: ./share/vizd/docker/Dockerfile-testnet
          build-args: |
            GIT_REVISION=${{ github.sha }}
            GIT_REF=${{ github.ref_name }}
'''


def selftest():
    arms = [
        ('both halves correct', GOOD_DOCKERFILE, GOOD_WORKFLOW, 0),
        ('label deleted', GOOD_DOCKERFILE.replace(
            '      org.opencontainers.image.revision="${GIT_REVISION}" \\\n', ''),
         GOOD_WORKFLOW, 1),
        ('label is a constant', GOOD_DOCKERFILE.replace(
            '"${GIT_REVISION}"', '"4.4.3"'), GOOD_WORKFLOW, 1),
        ('fallback looks like a sha', GOOD_DOCKERFILE.replace(
            'ARG GIT_REVISION=unknown', 'ARG GIT_REVISION=8333733c'), GOOD_WORKFLOW, 1),
        ('fallback missing', GOOD_DOCKERFILE.replace(
            'ARG GIT_REVISION=unknown\n', 'ARG GIT_REVISION\n'), GOOD_WORKFLOW, 1),
        ('workflow does not pass the arg', GOOD_DOCKERFILE,
         GOOD_WORKFLOW.replace('            GIT_REVISION=${{ github.sha }}\n', ''), 1),
        ('commented-out label does not count', GOOD_DOCKERFILE.replace(
            'LABEL org.opencontainers', '# LABEL org.opencontainers'), GOOD_WORKFLOW, 1),
        ('commented-out arg does not count', GOOD_DOCKERFILE.replace(
            'ARG GIT_REVISION=unknown', '# ARG GIT_REVISION=unknown'), GOOD_WORKFLOW, 1),
    ]
    bad = 0
    for name, dockerfile, workflow, expected in arms:
        found = problems_in(dockerfile, workflow)
        if len(found) != expected:
            bad += 1
            print('SELFTEST FAIL %s: expected %d finding(s), got %d' % (name, expected, len(found)))
            for item in found:
                print('    ' + item)
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
        print('image lineage is not provable from the image itself:')
        for item in found:
            print('  ' + item)
        return 1
    print('lineage labels present in %d Dockerfile(s) and passed by %d workflow(s)'
          % (len(DOCKERFILES), len(WORKFLOWS)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
