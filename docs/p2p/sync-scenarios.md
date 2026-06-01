# P2P Sync Scenarios

This page describes how the DLT P2P layer handles common sync situations: initial startup, catching up after downtime, DLT range gaps, fork recovery, and emergency consensus.

---

## Node Classification

The scenarios below use a 4-node reference setup:

| Role | Description |
|------|-------------|
| **Master** | FORWARD mode; DLT block log `[A..B]`; holds emergency private key |
| **Slave (NEAR)** | Head at `A-1` (exactly adjacent to master's DLT range) |
| **Slave (FAR)** | Head significantly below `A` (not in master's DLT range) |
| **Fresh node** | No blocks; genesis state only |

---

## Scenario 1: NEAR Slave (head adjacent to master's DLT range)

**Setup:** Master DLT range `[1000-2000]`. Slave head = 999.

### Hello handshake

1. Slave sends hello: `head_num=999, head_id=H999`.
2. Master's `check_fork_alignment` — multi-tier check:
   - `head_num=999` is below `dlt_earliest=1000` — not in range.
   - `head_num + 1 == dlt_earliest (1000)` → **boundary link check**: reads block 1000, verifies `block_1000.previous == H999`.
   - Match → `fork_alignment = true`, `exchange_enabled = true`.
3. Master replies: `exchange_enabled=true, fork_alignment=true`.
4. Slave enters **SYNCING** lifecycle state on master.

### Block sync

Slave requests `dlt_get_block_range(start=1000, end=1199, prev=H999)`. Master responds with blocks 1000–1199 from its DLT log. Slave applies each block. This repeats in batches of 200 until the slave reaches block 2000 and `is_last=true` fires `transition_to_forward()`.

**Result:** Clean P2P sync without any snapshot download. No backoff penalties.

---

## Scenario 2: FAR Slave (head far below master's DLT range)

**Setup:** Master DLT range `[1000-2000]`. Slave head = 800.

### Hello handshake

1. Slave sends hello: `head_num=800, head_id=H800`.
2. Master's fork alignment check: `800 < 1000`, boundary link fails (`800 + 1 ≠ 1000`), LIB fallback also fails (LIB ID pruned).
3. `fork_alignment = false`, but `exchange_enabled = false`.
4. Master **does not disconnect** the slave because `hello.node_status == SYNC` — SYNC peers always advance to ACTIVE lifecycle state.

### Sync attempt

Slave enters ACTIVE lifecycle state on master. Since `exchange_enabled = false`, master does not send forward blocks. Slave attempts block range request: `request_blocks_from_peer` detects `our_head+1 (801) < peer_dlt_earliest (1000)` — **gap detected**.

The node searches all connected peers for one whose DLT range covers block 801. If found, that peer is used as the bridging sync source. If no peer can bridge the gap:

```
[P2P] Gap detected: our_head=800, nearest_peer_dlt_earliest=1000
      No peer can serve blocks 801-999. Snapshot import may be required.
```

After ~90 seconds with no head progress, the snapshot plugin's stalled-sync detector fires and initiates a snapshot download from trusted peers (configured via `trusted-peer-for-snapshot`). After importing a snapshot at block 1500, the slave re-enters SYNC mode and catches up normally.

---

## Scenario 3: Fresh Node (no blocks)

**Setup:** Node has no blocks; `head_num=0, head_id=zero_id`.

### Hello handshake

1. Fresh node sends hello: `head_num=0`.
2. Master's fork alignment: `head_num == 0` → **empty peer** → `fork_alignment = true` (treated as "new node, not yet on any fork").
3. `exchange_enabled = true` (master will accept blocks from this node).
4. Fresh node advances to ACTIVE lifecycle state on master.

### Sync attempt

In `request_blocks_from_peer`, `our_head=0` and `peer_dlt_latest=2000`. However `peer_dlt_earliest=1000`, so the earliest available is block 1000. The request starts from `max(our_head+1, peer_dlt_earliest) = 1000`. The node receives blocks 1000+ but cannot apply them because the chain database has no state before block 1000.

The snapshot plugin detects the stall and downloads a snapshot (e.g., at block 1500). After import, the fresh node catches up from block 1500 → 2000 normally.

---

## Scenario 4: Node Restart After Crash

**Setup:** Node was at head 1912, DLT range `[1750-1912]`. After restart, peers are at block 2000.

### Startup recovery

1. `database::open()` checks DLT block log consistency: if the log's head matches the database head → consistent; otherwise reset the log.
2. The last **100 blocks** from the DLT block log are seeded into `fork_db` (blocks 1813–1912). This gives newly arriving blocks a 100-block parent window without requiring them to be fetched first.
3. A **60-second grace period** applies: for the first 60s after startup, blocks within 10 of the head are treated as `FORK_DB_ONLY` instead of `DEAD_FORK`. This prevents "rejection cascade" when peers replay blocks near the head that the fresh fork_db doesn't yet know about.

### Catching up

The node re-enters SYNC mode and requests blocks from 1913 onward. Peers with DLT range `[1800-2000]` can serve all needed blocks. The node catches up to 2000 and transitions to FORWARD.

---

## Scenario 5: Fork Switch

**Setup:** Node at head `H` on fork A. Peer has fork B head `H'` where `H' > H` and fork B has more vote weight.

### Fork detection

1. Block from fork B arrives via broadcast. Fork DB links it to its parent chain.
2. `track_fork_state()` is called after each block. When fork B sustains its lead for **42 blocks** (2 full validator rounds), `resolve_fork()` runs.
3. `resolve_fork()` computes the total vote weight (SHARES delegated) of validators on each branch. Fork B must maintain a 6-consecutive-block confirmation before the switch is committed.

### Fork switch execution

1. `pop_block()` removes blocks from fork A back to the common ancestor. Popped transactions go to `_popped_tx`.
2. Blocks from fork B are applied from the common ancestor to the new head.
3. `_popped_tx` and `_pending_tx` are reapplied; transactions already in fork B's chain are silently skipped.

**Fork status in stats:** transitions `NORMAL → LOOKING → NORMAL` (or `MINORITY` if this node is on the losing branch).

---

## Scenario 6: Emergency Consensus Sync

**Setup:** Network has been stalled for >3600 seconds. Emergency consensus is active.

### Master operation

The emergency master (node with `emergency-private-key` in config) produces all 21 blocks per round using the "committee" signing key. In stats: `+emrg +ekey`.

### Slave sync during emergency

1. Slave connects to master. Master's hello includes `emergency_active=true, has_emergency_key=true`.
2. Slave's fork alignment still proceeds normally — committee blocks are regular signed blocks from the perspective of the P2P layer.
3. Slave enters SYNC mode and requests committee-produced blocks from master.
4. Block validation: `verify_signing_witness()` relaxes the slot-producer mapping check during emergency — if the block producer doesn't match the exact scheduled slot, it is accepted as long as the signature validates against the producer's `signing_key`.

### Validator key restoration

When real validators restore their signing keys (via `validator_update_operation`), the schedule rebuild includes them in the hybrid schedule. Once **15 of 21** validator slots are real (non-committee), emergency mode deactivates. Subsequent blocks are produced by real validators and synced normally.

---

## Scenario 7: Stale Sync Recovery

**Condition:** SYNC mode, no block received for 30 seconds.

1. `sync_stagnation_check()` fires: retry 1 of 3 — re-requests blocks from all active exchange-enabled peers.
2. 30 seconds later: retry 2 of 3.
3. 30 seconds later: retry 3 of 3.
4. After the third retry: `transition_to_forward()` with a stagnation warning.

If the node was still behind when it transitioned to FORWARD, `check_forward_stagnation()` will detect no head progress after 30 seconds and transition back to SYNC mode, starting a new cycle.

---

## Scenario 8: Gap Fill

**Condition:** FORWARD mode; 1–100 blocks missing in the block stream.

Gap fill triggers automatically when:
- An out-of-order block is received (block N+2 arrives before N+1).
- `periodic_task()` detects `highest_seen_block_num > our_head + 1`.
- `resume_block_processing()` is called after a snapshot pause.

**Protocol:**
1. Select the peer with the highest `peer_head_num` among active peers.
2. Send `dlt_gap_fill_request(block_nums=[N+1, N+2, ...])` (max 100 blocks).
3. Wait up to **15 seconds** for the reply.
4. On receipt, apply the returned blocks. If blocks are still missing, trigger another gap fill on the next periodic cycle.

**If no peer can serve the gap** (no exchange-enabled or SYNCING peer with a higher head), the node immediately transitions to SYNC mode.

---

## Scenario 9: SYNC ↔ FORWARD Oscillation Prevention

**Root cause of oscillation:** After transitioning FORWARD→SYNC, the sync stagnation timer inherits a stale timestamp, fires immediately, and `check_sync_catchup` sees zero peers ahead → transitions back to FORWARD. Loop continues.

Another variant: `sync_stagnation_check()` transitions to FORWARD, then `check_forward_behind()` runs on the **same periodic tick** and immediately transitions back to SYNC because the peer is still ahead — instant SYNC→FORWARD→SYNC oscillation with no progress.

**Fixes in place:**
- `transition_to_sync()` resets `_last_block_received_time` to `now`, so stagnation timers start fresh.
- `check_forward_stagnation()` does NOT transition to SYNC when all connected peers have the same head as our node — no point syncing when nobody is ahead.
- `check_sync_catchup()` does NOT claim "caught up" when zero active peers exist; instead it starts the 60-second isolation timer.
- After 60 seconds of isolation, `emergency_peer_reset()` clears all soft-bans and backoffs, forcing immediate reconnect to all known peers.
- `check_forward_behind()` has a **15-second grace period** after entering FORWARD (`FORWARD_BEHIND_GRACE_SEC`). During this window the behind-peer check is skipped, giving broadcast blocks time to arrive before re-evaluating whether a SYNC fallback is needed.

---

## Scenario 10: Dead Fork Blocks

**Condition:** A peer sends blocks from a chain that diverged before the node's fork DB window. `push_block()` throws `unlinkable_block_exception` and the block number is ≤ `head_block_num`.

**Behavior:**
1. `dlt_delegate::accept_block()` returns `DEAD_FORK`.
2. The block is NOT stored in `fork_db._unlinked_index` (prevents memory growth).
3. The peer accumulates a spam strike per dead-fork block.
4. After 10 strikes the peer is soft-banned for 3600 s.
5. The sync loop breaks out — no further blocks from this peer are processed in the current batch.

**Grace period (P22 fix):** For the first 60 seconds after node startup, blocks within 10 of the current head that fail with `unlinkable_block_exception` are returned as `FORK_DB_ONLY` (not `DEAD_FORK`). This prevents false banning of legitimate peers sending blocks near the head before fork_db is fully rebuilt from the last 100-block seed.

---

## Configuration Relevant to Sync

| Setting | Default | Effect |
|---------|---------|--------|
| `seed-node` | — | Static peers; reconnected after `emergency_peer_reset()` |
| `dlt-block-log-max-blocks` | 100000 | DLT log capacity; affects how far back peers can bridge gaps |
| `trusted-peer-for-snapshot` | — | Peers from which snapshot download is accepted |
| `stalled-sync-timeout-minutes` | 2 | Minutes before snapshot plugin triggers recovery |
| `enable-stale-production` | false | Allow validator to produce without being synced (development only) |

---

See also: [P2P Overview](./overview.md), [Forward Mode](./forward-mode.md), [Emergency Consensus](../consensus/emergency-consensus.md), [Snapshot](../node/snapshot.md).
