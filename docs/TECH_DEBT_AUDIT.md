# Tech Debt Audit — viz-cpp-node

**Generated:** 2026-08-11 (full re-audit against `master` @ `d4fe3334`)
**Scope:** source under `libraries/`, `plugins/`, `programs/`, `tests/`, root `CMakeLists.txt`, `.github/workflows/`, `share/vizd/`. Excludes `thirdparty/` (vendored submodules: fc, chainbase, appbase).

> This is a from-scratch re-audit that **replaces** the 2026-05-01 audit and its
> 2026-06-17 refresh. Those described a tree that has since drifted materially —
> e.g. they claimed "zero tests / no `tests/` directory" (a `consensus_sim`
> harness now exists), referenced `libraries/network/node.cpp` (renamed to
> `dlt_p2p_node.cpp`), and audited a `documentation/` tree that a pending PR
> removes. The old finding IDs (F001–F051) are **not** carried forward; this
> document uses a fresh `D###` numbering. Where a current item maps to prior
> work it is called out inline.

## Executive summary

- **God-file concentration is still the dominant structural risk, and it now
  coincides exactly with the highest churn.** The three biggest hand-written
  translation units are also the three most-changed files in the last six
  months: `libraries/chain/database.cpp` (7,991 LOC, 130 commits/6mo),
  `plugins/snapshot/plugin.cpp` (4,657 LOC, 125 commits/6mo), and
  `libraries/network/dlt_p2p_node.cpp` (4,380 LOC, 125 commits/6mo). Size ×
  churn is where consensus bugs concentrate. Together with `wallet.cpp` (2,796)
  and `chain_evaluator.cpp` (2,350) these five hold ~22k LOC.
- **Tests now exist but are not enforced.** `tests/consensus_sim/` is a real
  deterministic multi-node harness (32 `BOOST_AUTO_TEST_CASE` scenarios across 10
  files: determinism replay, equivocation, wedge predicate, smoke) built under
  ASAN + UBSAN. **But it is gated `OFF` by default (`BUILD_CONSENSUS_TESTS`) and
  no CI workflow builds or runs it.** The correctness gate is written but not
  wired in. This is the single highest-value open item.
- **The build system does no optimization by default.** There is no default
  `CMAKE_BUILD_TYPE`, and `-O3/-O2` are set only inside the MinGW branch. A bare
  `cmake ..` (or an IDE configure) produces an unoptimized node. *(Addressed in
  open PR #146.)*
- **No precompiled headers despite a 3.16 minimum.** Every TU re-parses the same
  heavy Boost.MultiIndex / FC reflection headers. *(Opt-in PCH proposed in open
  PR #148.)*
- **Massive CMake duplication.** 26 library/plugin `CMakeLists.txt` carry the
  full source list twice, once per `SHARED`/`STATIC` branch. *(Collapsed in open
  PR #147, which also fixes a real drift in the chain lib's two branches.)*
- **`using namespace` in public headers is widespread — 51 occurrences**,
  including `using namespace std;` in `plugins/account_by_key/.../account_by_key_objects.hpp`
  and 6 stacked usings in `wallet/remote_node_api.hpp`. These leak into every
  consumer TU.
- **Console I/O bypasses the fc logging boundary**, concentrated in
  `plugins/snapshot/plugin.cpp` (81 `cerr`/`cout`/`printf` callsites with
  bespoke ANSI-color output). Note: a handful of `std::cerr` calls in
  `database.cpp` are the deliberate stall-watchdog and must stay.
- **Submodules track no pinned commit discipline.** `.gitmodules` lists three
  moving forks with no `branch=`/tag; `submodule update --remote` can silently
  advance them.
- **Repo hygiene:** `.qoder/` (168 tracked files, ~6.6 MB of AI-tool scratch)
  and a stale `documentation/` tree are still tracked. *(Both addressed in open
  PRs #144 and #145.)*

## Architectural mental model

VIZ is a Graphene-derived blockchain (Steem/Hive/BitShares lineage) with a
Fair-DPOS consensus tweak, mid-migration to a "VIZ Ledger" DLT positioning
(snapshot-assisted state storage). Four layers:

1. **`thirdparty/`** — `fc` (serialization, logging, exceptions, async),
   `chainbase` (mmap object DB), `appbase` (plugin lifecycle). Vendored as
   submodules.
2. **`libraries/`** — `protocol` (operations, types, asset), `chain` (state
   machine, evaluators, hardforks), `network` (P2P — note the recent
   `node.cpp` → `dlt_p2p_node.cpp` DLT redesign), `api`, `wallet`, `time`,
   `utilities` (BIP39 wordlist, key derivation).
3. **`plugins/`** — ~20 appbase plugins registered explicitly in
   `programs/vizd/main.cpp` `register_plugins()`. Data plugins index chain
   state; API plugins expose JSON-RPC. `mongo_db` has moved out to
   `examples-plugins/`.
4. **`programs/`** — `vizd`, `cli_wallet`, `util`, `build_helpers`.

**Where it breaks down:** the clean layering is undercut by god-file
concentration in `database.cpp` (block apply, fork resolution, undo sessions,
snapshot integration, hardfork application, a push-block stall monitor) and by
`dlt_p2p_node.cpp` / `snapshot/plugin.cpp` each pooling many responsibilities in
one high-churn TU. An in-flight witness→validator rename is visible in the churn
history (`witness.cpp`, `witness_guard.cpp` → `validator*`).

## Findings

| ID   | Category            | Location                                                    | Sev  | Effort | Description | Recommendation |
|------|---------------------|-------------------------------------------------------------|------|--------|-------------|----------------|
| D001 | Test enforcement    | `.github/workflows/*`, `CMakeLists.txt:229`                 | High | M | `tests/consensus_sim/` (32 ASAN/UBSAN test cases across 10 scenario files) exists but `BUILD_CONSENSUS_TESTS` defaults OFF and **no CI workflow builds or runs it**. `docker-pr-build.yml` builds only `vizd`. The correctness gate is written but not enforced. | Add a CI job: `cmake -DBUILD_CONSENSUS_TESTS=ON` + run `consensus_sim_tests` on PRs. This is the highest-leverage change in the repo. |
| D002 | Architectural decay | `libraries/chain/database.cpp` (7,991 LOC, 130 commits/6mo) | High | L | Highest size × churn in the repo. Block apply, fork resolution, undo lifecycle, snapshot import, hardfork apply, stall monitor in one TU. Longest-to-compile file; MSVC needs `/bigobj`. | Extract along functional seams. *(Started in open PR #149 — hardfork cluster → `database_hardfork.cpp`.)* Continue with apply-block path, snapshot integration, undo-session lifecycle. |
| D003 | Architectural decay | `plugins/snapshot/plugin.cpp` (4,657 LOC, 125 commits/6mo)  | High | L | Highest-churn plugin. Import, export, P2P transfer, peer query, ANSI logging in one file. Also holds 81 console-I/O callsites (D008) and 43 `catch(...)` handlers. | Split into `snapshot_import` / `snapshot_export` / `snapshot_peer_query` + a detail header. |
| D004 | Architectural decay | `libraries/network/dlt_p2p_node.cpp` (4,380 LOC, 125 commits/6mo) | High | L | God file in the P2P layer (product of the DLT redesign). Peer discovery, routing, fetch coordination, sync state in one TU. | Split `node_impl` along member groupings: peer DB, sync, fetch. Mechanical once the redesign settles. |
| D005 | Architectural decay | `libraries/wallet/wallet.cpp` 2,796 · `wallet.hpp` 1,557    | Med  | L | Wallet impl and its 1,557-LOC public header mix account ops, keys, signing, paid-subscription, forwarding. Header drags many plugin types into every cli_wallet TU. | Extract partial-class impls; forward-declare in the header, move bodies to `.cpp`. |
| D006 | Architectural decay | `libraries/chain/chain_evaluator.cpp` (2,350 LOC)           | Med  | M | Monolith of operation evaluators; new ops appended with no grouping. Low churn (9/6mo) so lower priority than D002–D004. | Group by category (account/content/validator/asset) into separate evaluator TUs. |
| D007 | Type & contract     | 51 `using namespace` in public headers                      | High | M | Leaks into every consumer TU. Worst: `using namespace std;` in `plugins/account_by_key/include/.../account_by_key_objects.hpp:14`; 6 stacked usings in `wallet/include/graphene/wallet/remote_node_api.hpp:19-25`; `using namespace boost::multi_index;` in `chain_object_types.hpp` and `account_by_key_objects.hpp`. | Move usings into `.cpp`, or replace with targeted `using ns::Type;`. Start with `std;` — mechanical, high value. |
| D008 | Consistency rot     | `plugins/snapshot/plugin.cpp` (~81), `plugins/chain/plugin.cpp` (11), others | Med | M | ~100 direct `std::cerr`/`cout`/`printf` callsites across libs+plugins despite fc logging being standard; snapshot uses bespoke ANSI-color macros. | Convert to `fc::*log`. **Exception:** the ~5 `std::cerr` calls in `database.cpp` are the intentional stall-watchdog (written to cerr precisely so the monitor never blocks on node locks) — leave them. |
| D009 | Type & contract     | 28 raw `new`/`delete` in `database.cpp`, 14 in `chain_evaluator.cpp`, others | Low | M | Project is C++14; `make_unique` available. Consensus-core files, so verify each pairing before converting. | Sweep `new X()`+`delete` → `unique_ptr`; `new char[N]` → `vector<char>`/`make_unique<char[]>`. Low priority; needs a compiler to verify. |
| D010 | Build config        | `CMakeLists.txt` — no default `CMAKE_BUILD_TYPE`            | Med  | S | Bare `cmake ..` yields empty optimization flags (unoptimized node); `-O3/-O2` set only in the MinGW branch. | *(Open PR #146.)* Default to Release when unset on single-config generators. |
| D011 | Build speed         | No `target_precompile_headers` anywhere (min CMake 3.16)    | Med  | M | Every TU re-parses heavy Boost.MultiIndex / FC reflection headers. | *(Open PR #148 — opt-in `ENABLE_PCH` for chain/protocol/wallet.)* |
| D012 | Build maintenance   | 26 `CMakeLists.txt` with dual `SHARED`/`STATIC` source lists | Med | M | Full source list duplicated per library/plugin; already drifted in `libraries/chain` (SHARED branch listed `invite_evaluator.cpp` twice, missing `invite_objects.hpp`). | *(Open PR #147 — single `VIZ_LIBRARY_TYPE` var; also fixes the chain drift.)* |
| D013 | Build hygiene       | `programs/build_helpers/` — two hardfork concatenators       | Low  | S | `cat-parts.cpp` (MSVC) and `cat_parts.py` (everyone else); the C++ binary was compiled on every platform. Drift risk. | *(Open PR #146 gates `cat-parts` to MSVC/MinGW.)* Longer term, pick one implementation. |
| D014 | Dependency & config | `.gitmodules` — 3 submodules, no pinned discipline           | Med  | S | fc/chainbase/appbase track moving forks with no `branch=`/tag. `submodule update --remote` can silently advance them; reproducible builds from an old commit can shift. | Pin to tags or document that the recorded SHA is authoritative and `--remote` is not to be used. Maintainer decision on which commit is canonical. |
| D015 | Dependency & config | `examples-plugins/mongo_db/` (5 files)                        | Low  | S | Moved out of `plugins/` (good), but no CI smoke build; silent bit-rot risk. | Add a compile-only CI check or mark explicitly unmaintained. |
| D016 | Documentation       | `share/vizd/config/` — 5 templates, no README                | Low  | S | `config.ini`, `config_debug.ini`, `config_stock_exchange.ini`, `config_testnet.ini`, `config_witness.ini` with no guide to which to use; README points users here without saying which. | Add `share/vizd/config/README.md` with a one-line purpose per template. |
| D017 | Repo hygiene        | `.qoder/` (168 files, ~6.6MB) + `documentation/` (8 files)   | Low  | S | AI-tool scratch dir and a legacy doc tree still tracked; the `documentation/` tree overlaps the current `docs/` VitePress site and has drifted. | *(Open PRs #144 untrack `.qoder/`; #145 consolidates `documentation/` into `docs/`.)* |
| D018 | Consistency rot     | Root build scripts: `build_mingv.sh` (typo), mixed dash/underscore naming | Low | S | `build_mingv.sh` is misspelled; `documentation/building.md` tells users to run `build_mingw.sh` which doesn't exist. | *(Open PR #144 renames to `build_mingw.sh`.)* |
| D019 | Dead conditional    | `chain_evaluator.cpp:1168` "TODO: Remove after hardfork 2"    | Low  | S | 19 TODO/FIXME markers total; this one gates pre-HF4 behavior and is long past. | Verify replay-safety, then delete the gate and keep the post-fork branch. Treat all other hardfork conditionals as append-only (D-note below). |

## Top 5 — if you fix nothing else

1. **D001 — Wire `consensus_sim` into CI.** The tests are already written (ASAN
   + UBSAN, deterministic multi-node). Turning them from "exists" into "runs on
   every PR" is the biggest correctness win available and needs no new test
   code — just a workflow job with `-DBUILD_CONSENSUS_TESTS=ON`.
2. **D002 — Keep splitting `database.cpp`.** 130 commits in six months into one
   7,991-LOC file with no per-file test isolation. PR #149 starts it; continue
   seam by seam.
3. **D007 — Strip `using namespace` from public headers**, starting with the
   `std;` in `account_by_key_objects.hpp`. Mechanical, high blast-radius win.
4. **D010 + D011 + D012 — Land the build PRs (#146/#147/#148).** Default-Release,
   collapsed CMake, and opt-in PCH together cut both wall-clock build time and
   the drift surface, at near-zero risk.
5. **D003 / D004 — Split the two other high-churn god files.** Snapshot and
   dlt_p2p_node each see ~125 commits/6mo; both are prime bug territory.

## Things that look bad but are actually fine

- **`libraries/utilities/words.cpp` (49,787 lines).** BIP39 wordlist as a
  `const char* word_list[]` in `.rodata`. Size is intrinsic to the data — not a
  god file.
- **132 `has_hardfork(HARDFORK_*)` checks in `libraries/chain/`.** Required for
  replay correctness — a node syncing from genesis must reproduce behavior at
  every height. Treat as append-only; only the very oldest pre-launch gates
  (D019) can be removed, and only carefully.
- **`catch(...)` in `peer_connection.cpp` / `message_oriented_connection.cpp`
  and the validator/p2p event loops.** These sit in fc::thread-driven loops
  where an uncaught exception would tear down a connection thread; they log via
  `dlog`/`wlog`/`elog` and set a flag. Correct pattern — do not "clean up."
- **The ~5 `std::cerr` calls in `database.cpp`.** Deliberate stall-watchdog
  output, written to cerr so the monitor can never block on node locks.
  Converting them to fc logging would defeat the watchdog.
- **Mixed `std::thread`/`fc::thread`.** Intentional: `fc::thread` integrates with
  `fc::future` and the cooperative scheduler; `std::thread` is for one-shot OS
  threads.
- **Boost coroutine found separately in root CMake.** Known workaround for a
  Boost CMake config quirk, not duplication.

## Open questions for the maintainer

1. **D001 CI cost.** `consensus_sim` builds at `-O1 -g -fsanitize=address,undefined`.
   Is the CI runner budget OK with an ASAN build per PR, or should it run on a
   schedule/label instead of every PR?
2. **D014 submodules.** Is tracking moving forks intentional (so `--remote`
   follows them), or is the recorded SHA authoritative? Different fixes apply.
3. **D015 mongo_db.** Is the `examples-plugins/mongo_db` plugin a supported
   deployment target or a reference example? Determines whether it needs CI.
4. **D005 wallet header.** Is the cli_wallet compile-time fan-out from
   `wallet.hpp` a felt pain, or tolerable? Drives priority of the header split.
5. **`config_stock_exchange.ini`.** Does this document a real deployment shape
   worth keeping, or is it vestigial? (Feeds D016.)
