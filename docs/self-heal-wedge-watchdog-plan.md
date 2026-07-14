# Plan: self-healing "wedged-behind-network" watchdog + snapshot-import completeness

Status: **DRAFT / not implemented.** Design for a fresh implementation session.
Author context: written after the 2026-07-13 rpc.viz.cx stuck-node incident.

---

## 1. The incident this prevents

rpc.viz.cx (a snapshot-synced RPC node) froze at head **81,579,861** for ~14h while the
network advanced to ~81,597,000. Root symptom in the logs:

```
_apply_block ] 13 out_of_range: unknown key {"name":"viz-social-bot"}  database.cpp:1469 get_account
Failed to push new block  next_block.block_num()=81579862
Gap fill: block #81579862 rejected 3 times — blacklisting ... (validator key likely invalid on this fork)
```

The node's committed state was **missing account `viz-social-bot`**, so it could not apply
the canonical block that did a `withdraw_vesting` for that account — and every subsequent
canonical block the same way. The node interpreted the real chain as a "fork with an invalid
validator key" and blacklisted the peers serving it. Head never advanced again.

Recovery required a **manual** `docker compose up -d --force-recreate vizd` (fresh writable
layer → snapshot re-sync). This class of outage recurs (see the 2026-06-17 and 2026-06-22
dead-fork incidents) and always needs a human.

### Why the existing guards did not catch it

The `#117–#120` machinery (`push_block_appears_stalled()`, the undo_all watchdog at
`libraries/chain/database.cpp:302-334`, the push_block monitor at `database.cpp:1128`)
detects a **write-lock deadlock**: `push_block` *held* past `PUSH_BLOCK_STALL_WARN_SEC`
with `_apply_progress` not moving. Our failure is the opposite — a **rejection livelock**:
`push_block` fails *fast* every time, so the lock is never held long and the deadlock
monitor never fires. Nothing currently watches "head frozen while peers are thousands of
blocks ahead and we keep rejecting their blocks."

### Why it is permanent, not transient

The divergence is at/below the node's **irreversible** head (head ≈ LIB at freeze time).
Fork switching recovers only *within* the reversible window via `pop_block`; you cannot pop
below LIB. So the bad state is baked in. The p2p recovery primitives that exist —
`resync_from_lib()` / `trigger_resync()` (`libraries/network/dlt_p2p_node.cpp:2460/2490`) —
only re-hello peers and re-request blocks; they never touch state. The **only** code path
that rebuilds state (`database::wipe()` + snapshot import) is gated on **empty state**
(`sync-snapshot-from-trusted-peer` = "load snapshot on empty state") or a newer snapshot
appearing. A node with corrupt-but-non-empty state never triggers it → stuck forever.

---

## 2. Root-cause findings (snapshot import), read-only static analysis

Investigated `plugins/snapshot/plugin.cpp` + `snapshot_serializer.hpp`. Findings, with
confidence levels. **None of these is proven to be the exact cause of the single missing
account** — that needs reproduction against the actual snapshot — but they are real gaps.

### Finding A — no import-side *completeness* verification (HIGH confidence gap)

- Export computes a SHA-256 over the serialized `state` JSON (`plugin.cpp:1410-1414`) and
  import verifies it (`plugin.cpp:1408-1416`). **This only proves the download matches what
  the serving node serialized.** If the serving node serialized *incomplete* state, the
  checksum still passes — garbage-in is verified as valid garbage.
- After import there is **no invariant check** — e.g. "every `account` referenced by an
  `account_authority` exists", "`dgp.current_supply` reconciles with summed balances",
  "critical singletons present". A missing account would sail through.

### Finding B — type-enum fragility, guarded for exactly ONE index (HIGH confidence, but wrong signature for this incident)

- `plugin.cpp:982-992` documents the hazard: if the chainbase `object_type` enum shifts
  (types added/removed before a given object's type id), `get_index<X>()` can resolve to the
  wrong/empty index and a whole section silently serializes empty — "Snapshot will be
  INCOMPLETE." Only `validator_vote` is sanity-checked (validators>0 but votes==0).
- `account_index` has **no** equivalent guard. **However**, this bug empties a *whole*
  section, not a single account — so it does not explain losing just `viz-social-bot`. Worth
  fixing defensively, but it is not the incident's mechanism.

### Finding C — undo / fork-switch interaction with snapshot-established state (MEDIUM confidence — most plausible for a *single* missing account)

- After import, `fork_db` holds only the head block (`plugin.cpp:1724`), and
  snapshot-established objects have no undo history. Recent commits target exactly this seam:
  `ec57277 fix(snapshot): update undo logic ... during snapshot imports`,
  `3b3b9ea fix(database): prevent undo_all() stall and handle corrupted shared memory`,
  `1b08a0b`/`4f3e00d` (pop_block no-progress deadlocks).
- Plausible mechanism: an account created *after* the snapshot point, then a fork switch /
  `pop_block` / `undo` sequence that removes it without the canonical branch re-creating it
  — leaving a permanent hole. **Needs reproduction to confirm**; do not fix blind.

### Recommended root-cause follow-ups (ranked, not yet actioned)

1. **Add post-import invariant checks** (Finding A) — cheap, high value, catches *any*
   incomplete import regardless of cause. `plugin.cpp` after the import block (~1740).
2. **Generalize the type-enum sanity check** (Finding B) to all critical indices, or better,
   persist per-section object counts in the snapshot header and assert imported counts match.
3. **Reproduce Finding C** on a test net: snapshot at block N, create an account at N+k, force
   a fork switch across N+k, verify the account survives. Only then design the undo fix.

---

## 3. The fix: wedged-behind-network watchdog (primary deliverable)

Turn the permanent manual-intervention outage into automatic self-heal within ~15–20 min.
The node already tracks everything needed — this wires it together and adds a recovery trigger.

### 3.1 Where it lives

`libraries/network/dlt_p2p_node.cpp`, in `periodic_task()` (the fiber loop started at
`dlt_p2p_node.cpp:165`, body around `:172`). The p2p node already has, at fiber cadence:

- `_delegate->get_head_block_num()` — our head (used at `:1186`, `:1206`, etc.)
- `_highest_seen_block_num` — network tip as seen from peers (maintained at `:1462`, `:1647`)
- `_gap_rejected_count` / `_gap_rejected_blacklist_until` — gap-fill rejection signal
  (`:1747-1748`, `:2206-2207`)

### 3.2 Detection logic (all conditions must hold, sustained)

Add a small watchdog struct evaluated each `periodic_task()` tick:

```
behind      = _highest_seen_block_num - our_head        // how far behind the network
head_stuck  = our_head has not increased since last observation
rejecting   = _gap_rejected_count increased since last observation   // actively refusing the chain
```

Declare **WEDGED** when, continuously for `WEDGE_CONFIRM_SEC`:
- `behind > WEDGE_BEHIND_THRESHOLD`   (e.g. **200** blocks), AND
- `head_stuck` (our_head flat), AND
- `rejecting` (gap-fill rejections still climbing — proves it is divergence, not just a
  slow/partitioned link where we'd simply have no blocks to apply).

Proposed constants (mirror the style of `PUSH_BLOCK_STALL_WARN_SEC` in
`database.hpp:725`), as `static constexpr` on `dlt_p2p_node`:

```
WEDGE_BEHIND_THRESHOLD = 200      // blocks behind network tip
WEDGE_CONFIRM_SEC      = 900      // 15 min sustained before acting
WEDGE_RELOG_SEC        = 60       // loud re-log cadence while wedged
```

Rationale for the AND of all three: a healthy node that is merely *syncing* is behind but
`our_head` is *advancing* (fails `head_stuck`). A network partition leaves us behind and
stuck but with **no** rejections (fails `rejecting`) — for that we just keep retrying peers,
we must not wipe good state. Only genuine state divergence produces behind + stuck +
actively-rejecting simultaneously.

### 3.3 Action on confirmed WEDGE

1. `elog` a loud, unambiguous line (behind count, our_head, highest_seen, reject count).
2. Write a `force_resync` marker file into the state dir (next to `shared_memory.bin`),
   containing the reason + timestamps for the post-mortem.
3. `std::_Exit(N)` with a distinct code — precedent: the undo_all watchdog already does
   `std::_Exit(1)` at `database.cpp:334`. Exiting from the fiber is safe; do **not** try to
   wipe+reimport in-process (that path is fragile mid-run).

### 3.4 Recovery path (on next start)

The snapshot plugin decides at open whether to bootstrap. Extend that decision (the
`open_from_snapshot` / "empty state" gate in `plugins/snapshot/plugin.cpp`) so that:

> if a `force_resync` marker exists → treat state as needing re-bootstrap: run the existing
> `database::wipe()` (`database.cpp:1070`) + snapshot import from `trusted-snapshot-peer`,
> then delete the marker.

This reuses the exact path a `--force-recreate` triggers today, so it is well-exercised.
Combined with a container `restart: unless-stopped` policy (already set in the compose file),
`std::_Exit` → restart → marker seen → wipe + snapshot-resync → back at tip. No operator, no
`docker compose` call.

> IMPLEMENTATION NOTE — verify the exact open-time bootstrap gate before wiring the marker.
> Confirm where `open_from_snapshot` vs. open-existing is chosen and that `database::wipe()`
> is callable there. Also confirm the marker path survives container restart on *our* deploy
> (state is in the container writable layer for rpc.viz.cx; on validators it may be a
> volume — the marker must live wherever `shared_memory.bin` lives so it persists exactly as
> long as the state it condemns).

### 3.5 Safety / edge cases

- **False positive → needless resync.** Worst case is an unnecessary snapshot re-bootstrap
  (minutes of downtime), not data loss. The triple-AND + 15-min confirm makes this rare.
- **Validators (`id`, `babin`).** They run the same binary. A wedged validator is *already*
  missing blocks; auto-resync is strictly better than silent missed-block deactivation. But
  a validator mid-resync must not sign — confirm block production stays disabled until the
  node is fork-aligned again after import (should already hold via SYNC state, verify).
- **Snapshot-serving.** rpc.viz.cx also *serves* snapshots (`allow-snapshot-serving=true`).
  Ensure a node that just wiped does not serve a half-imported snapshot (serving should be
  gated on fork-aligned/normal status — verify).
- **Resize interplay.** There is already a `resize_in_progress` marker (`database.cpp:273`)
  and shared-file resize logic. Make sure `force_resync` handling composes with it (don't
  wipe mid-resize).

---

## 4. Testing

- **Unit-ish:** factor the wedge predicate into a pure function of
  `(behind, head_advanced, rejects_advanced, elapsed)` and table-test the state machine
  (syncing → not wedged; partition → not wedged; divergence → wedged after confirm).
- **Integration (testnet):** force a node onto a dead branch below LIB (import a stale/partial
  snapshot), point it at healthy peers, assert: watchdog fires within `WEDGE_CONFIRM_SEC`,
  marker written, process exits, restart re-bootstraps, head reaches tip.
- **Negative:** partition the node (drop peer connections) → assert it does **not** wipe.

---

## 5. Rollout

1. Land watchdog + marker recovery behind a config flag `auto-resync-on-wedge` (default
   **off** for one release; observe the `elog`/marker in the wild without the `_Exit`).
2. After confirming detection has zero false positives on all three of our nodes
   (rpc.viz.cx, axveer `id`, martin `babin`), flip default on.
3. Separately land the Finding A post-import invariant checks — independent, lower risk,
   ships first.

---

## 6. Open questions for the implementer

- Exact location + signature of the open-time snapshot-bootstrap gate (§3.4).
- Does `_delegate->get_head_block_num()` reflect *irreversible* progress or just head? For
  `head_stuck` we want head; fine. But consider also gating on LIB not advancing to avoid
  firing during a legitimate deep reorg.
- Should the threshold be block-count (200) or time-based (network tip timestamp vs. our head
  timestamp)? Time-based ("our head is >10 min behind wall clock per peer consensus") may be
  more robust than a raw block delta. Decide during implementation.
