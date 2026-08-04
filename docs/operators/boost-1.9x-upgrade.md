# Upgrading to a Boost 1.9x build

Node binaries from this release are built against Boost 1.90 instead of the
Boost 1.83 that Ubuntu 24.04 ships.

## Do I need to replay?

**Yes.** `shared_memory.bin` written by a Boost 1.83 build cannot be reused by a
Boost 1.90 build. This was measured, not assumed: a segment created by a 1.83
binary and reopened by an otherwise identical 1.90 binary fails at the
named-object lookup — Boost.Interprocess's segment index is not layout-stable
across these versions, so the state is unreadable rather than merely suspect.

The node now refuses such a state directory up front and tells you both
versions:

    database was created by a different compiler, build, or operating system.
      state was built with: 13.3.0 boost-1_83
      this binary uses:     13.3.0 boost-1_90
    Replay from the block log or import a snapshot to rebuild state.

For a state directory written *before* this check existed, the stored side reads
`<unreadable -- predates this check, or a Boost too different to parse>`. The
action is the same.

Either replay from your block log or import a recent snapshot. **Your block log
is unaffected** — only `shared_memory.bin` is rebuilt. Downgrading back to a
1.83 build is symmetric: it will refuse the 1.90 state and want a replay too.

## Building from source

The Docker images need no action. Building from source on stock Ubuntu 24.04
still works: the tree supports Boost 1.83 through 1.9x, and CI covers both ends
of that range. To match the official images, build Boost 1.90 with
`link=static` and `cxxflags=-std=c++14`, or use the
`vizblockchain/boost:1.90-noble` image as your builder:

    docker build -f share/vizd/docker/Dockerfile-boost -t vizblockchain/boost:1.90-noble .

When configuring against a Boost installed under `/usr/local` rather than by
apt, pass `-DBOOST_ROOT=/usr/local`, as the production and testnet Dockerfiles
do.
