# Tech Debt Audit — viz-cpp-node

**Generated:** 2026-08-11 (full re-audit against `master` @ `d4fe3334`)
**Refreshed:** 2026-10-30 against `master` @ `aa224f7f`. Resolved findings moved
to [Resolved since the audit](#resolved-since-the-audit); sizes and churn
updated. Finding IDs are unchanged.
**Scope:** source under `libraries/`, `plugins/`, `programs/`, `tests/`, root `CMakeLists.txt`, `.github/workflows/`, `share/vizd/`. Excludes `thirdparty/` (vendored submodules: fc, chainbase, appbase).

## Executive summary

- **God-file concentration is still the dominant structural risk, and it
  coincides with the highest churn.** The three biggest hand-written
  translation units are also the most-changed files since May:
  `libraries/chain/database.cpp` (8,140 LOC, 138 commits),
  `libraries/network/dlt_p2p_node.cpp` (4,712 LOC, 136 commits) and
  `plugins/snapshot/plugin.cpp` (5,623 LOC, 101 commits). Size × churn is where
  consensus bugs concentrate. The hardfork cluster has moved out of
  `database.cpp` into `database_hardfork.cpp`; the rest of the split is open.
- **`using namespace` in public headers is widespread — 57 occurrences in 36
  headers**, including `using namespace std;` in
  `plugins/account_by_key/.../account_by_key_objects.hpp` and 6 stacked usings
  in `wallet/remote_node_api.hpp`. These leak into every consumer TU.
- **Console I/O bypasses the fc logging boundary**, concentrated in
  `plugins/snapshot/plugin.cpp` (81 `cerr`/`cout`/`printf` callsites with
  bespoke ANSI-color output) and `plugins/chain/plugin.cpp` (12). Note: the
  `std::cerr` calls in `database.cpp` are the deliberate stall-watchdog and must
  stay.
- **Submodules track no pinned commit discipline.** `.gitmodules` lists three
  moving forks with no `branch=`/tag; `submodule update --remote` can silently
  advance them.
- **Tests now run on every PR.** `tests.yml` builds with
  `-DBUILD_CONSENSUS_TESTS=ON` and runs every `add_test` binary (consensus_sim,
  pm, node_admin, logging, webserver); its jobs are required checks on
  `master`. The build-system items from the first pass (default Release,
  collapsed CMake, opt-in PCH, `cat-parts` gating) and the repo-hygiene items
  have landed too — see [Resolved since the audit](#resolved-since-the-audit).

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
| D002 | Architectural decay | `libraries/chain/database.cpp` (8,140 LOC, 138 commits since May) | High | L | Highest size × churn in the repo. Block apply, fork resolution, undo lifecycle, snapshot integration, stall monitor in one TU. Longest-to-compile file; MSVC needs `/bigobj`. | Extract along functional seams. *(Started: hardfork application now lives in `database_hardfork.cpp`, 5dd2475f.)* Continue with apply-block path, snapshot integration, undo-session lifecycle. |
| D003 | Architectural decay | `plugins/snapshot/plugin.cpp` (5,623 LOC, 101 commits since May) | High | L | Highest-churn plugin. Import, export, P2P transfer, peer query, invariant checks, ANSI logging in one file. Also holds 81 console-I/O callsites (D008) and 53 `catch(...)` handlers. | Split into `snapshot_import` / `snapshot_export` / `snapshot_peer_query` + a detail header. |
| D004 | Architectural decay | `libraries/network/dlt_p2p_node.cpp` (4,712 LOC, 136 commits since May) | High | L | God file in the P2P layer (product of the DLT redesign). Peer discovery, routing, fetch coordination, sync state in one TU. | Split `node_impl` along member groupings: peer DB, sync, fetch. Mechanical once the redesign settles. |
| D005 | Architectural decay | `libraries/wallet/wallet.cpp` 3,208 · `wallet.hpp` 1,670    | Med  | L | Wallet impl and its 1,670-LOC public header mix account ops, keys, signing, paid-subscription, forwarding. Header drags many plugin types into every cli_wallet TU. | Extract partial-class impls; forward-declare in the header, move bodies to `.cpp`. |
| D006 | Architectural decay | `libraries/chain/chain_evaluator.cpp` (2,396 LOC)           | Med  | M | Monolith of operation evaluators; new ops appended with no grouping. Low churn (10 commits since May) so lower priority than D002–D004. | Group by category (account/content/validator/asset) into separate evaluator TUs. |
| D007 | Type & contract     | 57 `using namespace` in 36 public headers                      | High | M | Leaks into every consumer TU. Worst: `using namespace std;` in `plugins/account_by_key/include/.../account_by_key_objects.hpp:14`; 6 stacked usings in `wallet/include/graphene/wallet/remote_node_api.hpp:19-25`; `using namespace boost::multi_index;` in `chain_object_types.hpp` and `account_by_key_objects.hpp`. | Move usings into `.cpp`, or replace with targeted `using ns::Type;`. Start with `std;` — mechanical, high value. |
| D008 | Consistency rot     | `plugins/snapshot/plugin.cpp` (81), `plugins/chain/plugin.cpp` (12), others | Med | M | ~100 direct `std::cerr`/`cout`/`printf` callsites across libs+plugins despite fc logging being standard; snapshot uses bespoke ANSI-color macros. | Convert to `fc::*log`. **Exception:** the 5 `std::cerr` calls in `database.cpp` are the intentional stall-watchdog (written to cerr precisely so the monitor never blocks on node locks) — leave them. |
| D009 | Type & contract     | 28 raw `new` in `database.cpp`, 12 `new` / 2 `delete` in `chain_evaluator.cpp`, others | Low | M | Project is C++14; `make_unique` available. Consensus-core files, so verify each pairing before converting. | Sweep `new X()`+`delete` → `unique_ptr`; `new char[N]` → `vector<char>`/`make_unique<char[]>`. Low priority; needs a compiler to verify. |
| D012 | Build maintenance   | `plugins/prediction_market_api/CMakeLists.txt`               | Low | S | The other 25 `CMakeLists.txt` now use the single `VIZ_LIBRARY_TYPE` variable; `prediction_market_api` (added with HF14 after the collapse) still carries its own `if(BUILD_SHARED_LIBRARIES)` SHARED/STATIC branches. | Switch it to `add_library(graphene_${CURRENT_TARGET} ${VIZ_LIBRARY_TYPE} ...)` like its siblings. |
| D014 | Dependency & config | `.gitmodules` — 3 submodules, no pinned discipline           | Med  | S | fc/chainbase/appbase track moving forks with no `branch=`/tag. `submodule update --remote` can silently advance them; reproducible builds from an old commit can shift. | Pin to tags or document that the recorded SHA is authoritative and `--remote` is not to be used. Maintainer decision on which commit is canonical. |
| D015 | Dependency & config | `examples-plugins/mongo_db/` (5 files)                        | Low  | S | Moved out of `plugins/` (good), but no CI smoke build; silent bit-rot risk. | Add a compile-only CI check or mark explicitly unmaintained. |
| D019 | Dead conditional    | `chain_evaluator.cpp` "TODO: Remove after hardfork 2"    | Low  | S | 21 TODO/FIXME markers total; this one gates pre-HF4 behavior and is long past. | Verify replay-safety, then delete the gate and keep the post-fork branch. Treat all other hardfork conditionals as append-only (D-note below). |

## Top 5 — if you fix nothing else

1. **D002 — Keep splitting `database.cpp`.** 138 commits since May into one
   8,140-LOC file. The hardfork cluster is out; continue seam by seam.
2. **D007 — Strip `using namespace` from public headers**, starting with the
   `std;` in `account_by_key_objects.hpp`. Mechanical, high blast-radius win.
3. **D003 / D004 — Split the two other high-churn god files.** Snapshot and
   dlt_p2p_node each see 100+ commits since May; both are prime bug territory.
4. **D008 — Route snapshot console output through fc logging**, so operators
   get one log stream and one format.
5. **D014 — Decide the submodule policy** (pinned SHA vs. tracked branch) and
   write it down.

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

1. **D014 submodules.** Is tracking moving forks intentional (so `--remote`
   follows them), or is the recorded SHA authoritative? Different fixes apply.
2. **D015 mongo_db.** Is the `examples-plugins/mongo_db` plugin a supported
   deployment target or a reference example? Determines whether it needs CI.
3. **D005 wallet header.** Is the cli_wallet compile-time fan-out from
   `wallet.hpp` a felt pain, or tolerable? Drives priority of the header split.
4. **`config_stock_exchange.ini`.** Does this document a real deployment shape
   worth keeping, or is it vestigial? (Feeds D016.)

## Resolved since the audit

| ID   | What was fixed | Where |
|------|----------------|-------|
| D001 | `consensus_sim` (and the pm, node_admin, logging, webserver suites) build with `-DBUILD_CONSENSUS_TESTS=ON` and run on every push and PR; the jobs are required checks on `master`. | `.github/workflows/tests.yml` (652e6cdd and later) |
| D010 | Single-config generators default to `CMAKE_BUILD_TYPE=Release` when unset. | root `CMakeLists.txt` |
| D011 | Opt-in precompiled headers via `ENABLE_PCH` (default OFF). | root `CMakeLists.txt` |
| D012 | All libraries and plugins except `prediction_market_api` use `VIZ_LIBRARY_TYPE` instead of duplicated SHARED/STATIC lists (the remainder is tracked above). | `libraries/*/CMakeLists.txt`, `plugins/*/CMakeLists.txt` |
| D013 | `cat-parts` is built only on MSVC/MinGW; other platforms use `cat_parts.py`. | `programs/build_helpers/CMakeLists.txt` |
| D016 | `share/vizd/config/README.md` lists what each template is for; `config_debug.ini` no longer loads `test_api` / `debug_node`, which `vizd` does not register. | `share/vizd/config/` |
| D017 | `.qoder/` untracked and ignored; the legacy `documentation/` tree is gone, `docs/` is the only doc tree. | `.gitignore`, `docs/` |
| D018 | Root script is `build_mingw.sh`. | repo root |
