# Contributing to viz-cpp-node

This describes how changes reach `master` today. It replaces the 2018 draft in
issue #10.

## Branches

- `master` is the only long-lived branch. Production (`latest`) and testnet
  Docker images are built from it.
- Force pushes to `master` are disabled. Every change lands through a pull
  request.
- Work in a short-lived branch named `<type>/<short-description>`, for example
  `fix/hf16-readiness-quorum`, `docs/hf16-round-of`, `test/hf16-p2p-handler-scale`.
  Common types: `fix`, `feat`, `docs`, `test`, `ci`, `build`, `chore`, `refactor`,
  `perf`.
- Contributors without write access open the pull request from a fork; the same
  naming applies.
- Rebase on the current `master` before asking for review if your branch has
  fallen behind or conflicts. A force push to your own feature branch is fine.

## Commits

- Subject line: `area: what the change does`, in the imperative, e.g.
  `snapshot: restore PM seed flags` or `p2p: scale block-count thresholds by the
  live slot length for HF16`. The area is the plugin, library or subsystem
  (`snapshot`, `p2p`, `chain`, `pm`, `docs`, `ci`, ...).
- The body explains why the change is needed, not a line-by-line narration of
  the diff. Reference the issue it fixes or follows up on (`#127`).
- Keep each commit buildable. Keep unrelated changes in separate pull requests.

## Pull requests

- Target `master`.
- At least one approving review from a maintainer is required.
- The required checks must pass: the `Tests` workflow (`consensus_sim + pm unit
  tests`, `Testnet config + consensus_sim` for 1 and 2 validators) and the PR
  Docker build. They run on every pull request, including docs-only ones.
  Fork pull requests build the Docker image without publishing it.
- Describe what changed, why, and how you verified it (tests added or run,
  replay or snapshot import, testnet run).
- Consensus changes (operations, evaluators, hardfork logic, invariants) must
  be gated behind a hardfork (see
  [docs/consensus/hardforks.md](docs/consensus/hardforks.md)) and come with
  tests. Say in the description which hardfork the change belongs to.

## Tests

See [docs/development/testing.md](docs/development/testing.md) for the test
suites and how to run them locally, and
[docs/development/building.md](docs/development/building.md) for build
instructions. Bug fixes should include a regression test that fails without the
fix.

## Documentation

The documentation site lives in `docs/`, with Russian and Simplified Chinese
translations under `@l10n/ru/docs` and `@l10n/zh-CN/docs`. When you change a
page whose behaviour also appears in the translations, update them in the same
pull request or say in the description that they still need updating.

## Reporting issues

Open a GitHub issue with the node version (release tag, commit or Docker tag),
the relevant `config.ini` options, and log excerpts. Report security problems
privately to the maintainers instead of opening a public issue.
