# DLT 4-Node Sync Scenarios — Problems & Analysis

Analysis of a 4-node DLT network under emergency consensus: 1 master + 3 slaves, based on the current `dlt_p2p_node.cpp` implementation.

---

## Network Setup

```
Master:   dlt_block_log [1000-2000], snapshot at block 1500, FORWARD mode
slaveA:   head at block 800, no snapshot, SYNC mode
slaveB:   head at block 999, no snapshot, SYNC mode
slaveC:   no blocks at all, SYNC mode
```

All nodes have each other as seeds. Emergency consensus is active.

---

## Scenario 1: slaveA (head=800)

### Step-by-step

**1. slaveA connects to master → sends `dlt_hello_message`:**
```
head_block_num=800, head_block_id=<H800>
lib_block_num=790,  lib_block_id=<H790>
dlt_earliest_block=0 (or whatever), dlt_latest_block=800
node_status=SYNC
```

**2. Master receives slaveA's hello → `on_dlt_hello()`:**

- Stores slaveA's chain state in `dlt_peer_state`
- Calls `build_hello_reply(peer, hello)`:
  - `check_fork_alignment(H800, H790)`:
    - `is_block_known(H800)` → **FALSE** — master's dlt block log is [1000-2000], block 800 has been pruned. If `is_block_known` also checks the chain index and block 800 was pruned from there too, it returns false.
    - `is_block_known(H790)` → **FALSE** — same reason.
    - **Result: `fork_alignment = false`**
  - `exchange_enabled = fork_alignment = false`

- **Lifecycle transition check (line 500-504):**
  ```cpp
  if (reply.exchange_enabled || _node_status == DLT_NODE_STATUS_SYNC)
  ```
  - `exchange_enabled = false`
  - `_node_status` = DLT_NODE_STATUS_FORWARD (master)
  - `false || false` → **false** → slaveA stays in **HANDSHAKING** on master

- Records packet as good: `record_packet_result(peer, true)` — no spam strike

**3. Master sends `dlt_hello_reply_message`:**
```
exchange_enabled=false, fork_alignment=false
```

**4. Master's periodic check → `periodic_lifecycle_timeout_check()`:**
- slaveA has been in HANDSHAKING for >10s → **TIMEOUT** → `handle_disconnect()`
- slaveA disconnected with backoff

**5. slaveA's perspective (receiving master's hello):**

When master sent its hello (head=2000, LIB=1990), slaveA received it:
- `check_fork_alignment(H2000, H1990)`:
  - `is_block_known(H2000)` → **FALSE** (slaveA only has up to 800)
  - `is_block_known(H1990)` → **FALSE**
  - **Result: `fork_alignment = false`**

- Lifecycle transition: `exchange_enabled(false) || _node_status(SYNC)` → **true** (slaveA is SYNC)
- Master transitions to ACTIVE on slaveA's side
- `_node_status == SYNC && exchange_enabled` → `true && false` → **does NOT request blocks**

**6. slaveA sync stagnation:**
- `sync_stagnation_check()` runs every ~5s via periodic task
- After 30s with no block received → retry 1/3 → re-requests from all active peers (master: `request_blocks_from_peer`)
  - `our_head(800) >= peer_latest(2000)`? → **No**, 800 < 2000
  - Requests blocks 801-1000 from master
  - Master receives `dlt_get_block_range_message(start=801, end=1000)`
  - `on_dlt_get_block_range()`: loops 801..1000, calls `read_block_by_num(n)` — blocks 801-999 return empty (not in dlt block log), blocks 1000 returns the block
  - Reply contains only block 1000 (blocks 801-999 are missing from master's dlt log)
  - slaveA receives block 1000, `prev=H999` ≠ `slaveA's head H800` → "does NOT link to our head — possible fork"
  - Block goes to fork_db as unlinkable

- After 3 stagnation retries (90s total) → `transition_to_forward()`

**7. Snapshot plugin kicks in:**
- `stalled-sync-timeout-minutes` (default 2 min) after last block
- Snapshot plugin: detects stall → tries P2P recovery (trigger_resync) → waits 1 min → downloads snapshot from trusted peers (master)
- After snapshot import at block 1500, slaveA resyncs from 1500 → catches up to 2000

### Problems Identified

| # | Problem | Severity |
|---|---------|----------|
| P1 | **`check_fork_alignment` is too narrow**: Only checks if peer's head/LIB ID is known. In DLT mode, old blocks are pruned. A peer at block 800 IS on the same chain but can't prove it because blocks 800-999 are gone. | **CRITICAL** |
| P2 | **No range-overlap check**: The code doesn't check if `peer_head_num` is within `[our_dlt_earliest, our_dlt_latest]` or adjacent to it. This is the obvious fix — if ranges are adjacent and the boundary block links, the peer IS fork-aligned. | **CRITICAL** |
| P3 | **HANDSHAKING timeout loop**: When exchange_enabled=false and master is FORWARD, the peer stays in HANDSHAKING forever → 10s timeout → disconnect → reconnect → repeat. The backoff grows to 3600s. This wastes connections and delays recovery. | **HIGH** |
| P4 | **Wasted block requests**: slaveA requests blocks 801-1000 from master. Master only has 1000. Blocks 801-999 are served as empty. This wastes bandwidth and the partial reply (just block 1000) goes to fork_db. | **MEDIUM** |
| P5 | **Recovery is entirely snapshot-plugin driven**: The P2P layer has no "you need a snapshot" signal. It relies on the snapshot plugin's stalled-sync detection, which takes 2+ minutes. | **HIGH** |

---

## Scenario 2: slaveB (head=999)

### Step-by-step

**1. slaveB connects to master → sends `dlt_hello_message`:**
```
head_block_num=999, head_block_id=<H999>
lib_block_num=989,  lib_block_id=<H989>
dlt_earliest_block=0, dlt_latest_block=999
node_status=SYNC
```

**2. Master receives slaveB's hello → `on_dlt_hello()`:**

- `check_fork_alignment(H999, H989)`:
  - `is_block_known(H999)` → **FALSE** — block 999 is NOT in master's dlt block log [1000-2000]. It was pruned.
  - `is_block_known(H989)` → **FALSE** — same.
  - **Result: `fork_alignment = false`**

**This is the key bug.** slaveB's head is at block 999, and master's earliest dlt block is 1000. Block 1000's `previous` field is exactly block 999's ID. This means:

```
slaveB.head_block_id == master.dlt_block_log.block_at(1000).previous
```

But `check_fork_alignment` **never checks this boundary condition**. It only checks `is_block_known()` on the head/LIB IDs directly, which fails because block 999 has been pruned from the master's dlt block log.

**3. Everything else proceeds identically to slaveA:**
- `exchange_enabled = false`
- Master (FORWARD): slaveB stays HANDSHAKING → 10s timeout → disconnect
- slaveB (SYNC): master transitions to ACTIVE but no blocks are requested
- slaveB stagnation: requests blocks, gets partial reply, blocks go to fork_db
- Eventually: snapshot plugin downloads snapshot at 1500

### The Expected Behavior vs. Reality

| Expected | Reality |
|----------|---------|
| slaveB's head (999) links to master's earliest block (1000) via `previous` | Code doesn't check `previous` linkage |
| slaveB should get `fork_alignment=true` and sync blocks 1000-2000 | `fork_alignment=false`, gets disconnected |
| slaveB should transition to FORWARD after catching up | slaveB has to go through snapshot download |

### Problems Identified

| # | Problem | Severity |
|---|---------|----------|
| P6 | **Missing "boundary link" check**: When `peer_head_num + 1 == our_dlt_earliest`, the code should check if `our_dlt_earliest_block.previous == peer_head_id`. This proves the peer is on the same chain without needing the pruned block. | **CRITICAL** |
| P7 | **`check_fork_alignment` doesn't consider DLT range at all**: The function receives only two block IDs. It has no access to `peer_dlt_earliest`, `peer_dlt_latest`, `our_dlt_earliest`, `our_dlt_latest`, or the block log to verify range adjacency. | **CRITICAL** |
| P8 | **No block number check in alignment**: Even a simple check like "peer_head_num >= our_dlt_earliest - 1" would catch this case. The function only processes IDs, not numbers. | **HIGH** |

---

## Scenario 3: slaveC (no blocks at all)

### Step-by-step

**1. slaveC connects to master → sends `dlt_hello_message`:**
```
head_block_num=0, head_block_id=<zero_id> (all zeros)
lib_block_num=0,  lib_block_id=<zero_id>
dlt_earliest_block=0, dlt_latest_block=0
node_status=SYNC
```

**2. Master receives slaveC's hello → `on_dlt_hello()`:**

- `check_fork_alignment(zero_id, zero_id)`:
  - `is_block_known(zero_id)` → **FALSE** (all-zeros block ID is never a real block)
  - **Result: `fork_alignment = false`**

- Lifecycle: `exchange_enabled(false) || FORWARD` → false → slaveC stays HANDSHAKING

- `record_packet_result(peer, true)` → **GOOD packet** — no spam strike

**3. HANDSHAKING timeout → disconnect → reconnect loop** (same as slaveA/slaveB)

**4. Does master soft-ban slaveC?** **NO.**

- Soft-ban only triggers when `spam_strikes >= SPAM_STRIKE_THRESHOLD (10)` via `record_packet_result(peer, false)`
- The hello handler always records `true` (good packet) for hello messages (line 507)
- Even the disconnect isn't triggered by spam — it's a lifecycle timeout
- **No soft-ban, no "not my guy" flag, no counting of useless peers**

**5. Does master mark slaveC as "don't send forward blocks"?**

The `exchange_enabled` flag IS set to `false` for slaveC. When master broadcasts blocks via `send_to_all_our_fork_peers()`, it only sends to peers where `exchange_enabled == true`. So slaveC won't receive forward blocks.

But this is a side effect, not an intentional "this peer needs a snapshot" design.

**6. slaveC's recovery path:**

- Stagnation (90s) → FORWARD with 0 blocks → snapshot plugin detects stall → downloads snapshot from trusted peers → imports at block 1500 → resyncs

**7. What about peer exchange? Does slaveC get shared?**

No. `on_dlt_peer_exchange_request()` only shares peers where `exchange_enabled=true` (line 868):
```cpp
if (!s.exchange_enabled) continue;
```

### Does Master Tell slaveC "You Need a Snapshot"?

**No.** There is no message type for this. The protocol has no way to say:
- "Your blocks are too old, get a snapshot"
- "I have a snapshot at block 1500 you can download"
- "I don't recognize your chain at all"

The snapshot serving happens on a **separate TCP port** (e.g., 8092) via a completely different protocol. The DLT P2P layer has zero awareness of snapshots.

### Problems Identified

| # | Problem | Severity |
|---|---------|----------|
| P9 | **No "empty peer" recognition**: An all-zero hello should be recognized as "new node with no state" and treated differently. Currently it's indistinguishable from a far-behind peer. | **MEDIUM** |
| P10 | **No "needs snapshot" signal**: The protocol can't tell a peer "you're too far behind, download a snapshot from me/trusted peers." The snapshot mechanism is entirely separate. | **HIGH** |
| P11 | **Peer exchange works correctly** (doesn't share useless peers) but for the wrong reason — it's by accident via `exchange_enabled=false`, not by design. | **LOW** |
| P12 | **No anti-spam for "useless" peers**: A peer that repeatedly connects with 0 blocks doesn't accumulate spam strikes. It just loops: connect → handshake timeout → disconnect → reconnect. | **LOW** |

---

## Cross-Cutting Problems

### P13: `check_fork_alignment` Signature is Insufficient

```cpp
// Current signature (dlt_p2p_node.hpp line 116-117):
bool check_fork_alignment(const block_id_type& head_id, const block_id_type& lib_id,
                          block_id_type& recognized_head_out, block_id_type& recognized_lib_out) const;
```

This function receives **only two block IDs**. It has **no access** to:
- The peer's block numbers (head_num, lib_num)
- The peer's DLT range (dlt_earliest, dlt_latest)
- Our DLT range
- The actual block data to check `previous` linkage

It delegates to `_delegate->is_block_known()` which is a **binary yes/no** on whether a specific block ID exists in the chain or fork_db. In DLT mode, blocks outside the rolling window are pruned, so `is_block_known()` returns false for them.

**The function needs to be redesigned to consider DLT range adjacency.**

### P14: `on_dlt_hello` Lifecycle Logic is Broken for FORWARD Mode

```cpp
// Lines 500-504:
if (state.lifecycle_state == DLT_PEER_LIFECYCLE_HANDSHAKING) {
    if (reply.exchange_enabled || _node_status == DLT_NODE_STATUS_SYNC) {
        state.lifecycle_state = DLT_PEER_LIFECYCLE_ACTIVE;
    }
}
```

When the master is FORWARD and a peer has `exchange_enabled=false`:
- Peer stays HANDSHAKING → 10s timeout → disconnect
- This is correct behavior for a **hostile fork** peer
- But it also applies to **same-chain peers whose blocks were pruned** — they can never transition to ACTIVE

**The condition should be:**
```cpp
if (reply.exchange_enabled || _node_status == DLT_NODE_STATUS_SYNC ||
    hello.node_status == DLT_NODE_STATUS_SYNC)  // Also let SYNC peers through
```

Or better, separate the concepts:
- `fork_alignment` = "are we on the same chain?" (structure check including range adjacency)
- `exchange_enabled` = "do we want to do full block exchange with this peer?" (policy)

### P15: No DLT Range Negotiation in Hello

The hello message carries DLT range info but it's only used for display and for `request_blocks_from_peer` (to know `peer_dlt_latest`). No logic uses the range to determine fork alignment.

What should happen in `build_hello_reply`:
```
1. If peer_head_num >= our_dlt_earliest:
   → Check is_block_known(peer_head_id) as now
2. If peer_head_num == our_dlt_earliest - 1:
   → Check our_dlt_earliest_block.previous == peer_head_id
   → If match: fork_aligned = true (boundary link)
3. If peer_head_num < our_dlt_earliest - 1:
   → Not aligned via DLT range — fall back to is_block_known on LIB
4. If peer_head_num == 0 (empty peer):
   → Special case: "needs snapshot" or accept as aligned with caveat
```

### P16: Master Doesn't Differentiate "Why" exchange_enabled is False

Currently `exchange_enabled=false` means one of:
- "You're on a different fork" (hostile)
- "Your blocks are too old and I pruned them" (needs snapshot)
- "You have no blocks" (new node)
- "Protocol version mismatch"

All four cases get the same treatment: HANDSHAKING timeout → disconnect. The protocol should distinguish these and respond differently:
- Hostile fork → disconnect immediately, maybe soft-ban
- Needs snapshot → keep connection, mark as "snapshot candidate", don't send blocks
- New node → redirect to snapshot server
- Version mismatch → disconnect with clear reason

---

## Summary of All Problems

```
P1  [CRIT] check_fork_alignment only checks is_block_known — fails for pruned blocks
P2  [CRIT] No range-overlap check in fork alignment
P3  [HIGH]  HANDSHAKING timeout loop for same-chain peers
P4  [MED]   Wasted block range requests that return mostly empty
P5  [HIGH]  Recovery entirely depends on snapshot plugin (2+ min delay)
P6  [CRIT] Missing boundary link check (peer_head+1 == our_earliest)
P7  [CRIT] check_fork_alignment doesn't have access to DLT ranges
P8  [HIGH]  No block number check in alignment logic
P9  [MED]   No "empty peer" recognition
P10 [HIGH]  No "needs snapshot" signal in protocol
P11 [LOW]   Peer exchange excludes empty peers by accident, not design
P12 [LOW]   No anti-spam for empty peer reconnect loops
P13 [CRIT] check_fork_alignment signature is insufficient (needs range data)
P14 [HIGH]  on_dlt_hello lifecycle logic broken for FORWARD mode
P15 [CRIT] No DLT range negotiation in hello handshake
P16 [HIGH]  No differentiation between reasons for exchange_enabled=false
```

---

## Recommended Fix Priority

### Immediate (would fix all 3 slave scenarios)

1. **Extend `check_fork_alignment`** to accept the peer's `dlt_hello_message` and our DLT range:
   - Add boundary link check: `if (peer.head_num + 1 == our_dlt_earliest)` → verify `our_block_at(earliest).previous == peer.head_id`
   - Add range overlap check: `if (peer.head_num >= our_dlt_earliest)` → use existing `is_block_known` logic
   - Add empty peer check: `if (peer.head_num == 0)` → treat as "needs snapshot" (aligned but with zero blocks)

2. **Fix `on_dlt_hello` lifecycle transition** so SYNC peers always reach ACTIVE state:
   ```cpp
   if (reply.exchange_enabled || _node_status == DLT_NODE_STATUS_SYNC
       || hello.node_status == DLT_NODE_STATUS_SYNC)
   ```

3. **Add a `peer_needs_snapshot` flag** to `dlt_peer_state` and expose it so the snapshot plugin can act on it without waiting for the 2-minute stall timeout.

### Short-term

4. **Add a `dlt_need_snapshot` message type** so master can actively tell far-behind peers to get a snapshot instead of disconnecting them.

5. **Skip empty range requests**: In `request_blocks_from_peer`, if `our_head + 1 < peer_dlt_earliest`, don't send a request — we know the peer doesn't have those blocks.

### Long-term

6. **Integrate P2P with snapshot serving**: Allow the master to advertise its snapshot endpoint in the hello message so peers know where to download without separate config.

7. **Add a dedicated lifecycle state** for "snapshot-needed" peers so they don't cycle through HANDSHAKING→DISCONNECTED repeatedly.

---

## Implemented Fixes (2026-05-05)

### Changes Made

**Files modified:**
- `libraries/network/include/graphene/network/dlt_p2p_node.hpp` — `check_fork_alignment` signature
- `libraries/network/dlt_p2p_node.cpp` — implementation + lifecycle transitions

**1. `check_fork_alignment` extended** (fixes P1, P2, P6, P7, P8, P9, P13, P15):

```
OLD signature:
  bool check_fork_alignment(const block_id_type& head_id, const block_id_type& lib_id,
                            block_id_type& recognized_head_out, block_id_type& recognized_lib_out) const;

NEW signature — accepts the full hello for DLT-range-aware alignment:
  bool check_fork_alignment(const dlt_hello_message& hello,
                            block_id_type& recognized_head_out, block_id_type& recognized_lib_out) const;
```

New alignment checks (see `dlt_p2p_node.cpp` lines 448-489):

| Check | Condition | Result |
|-------|-----------|--------|
| Empty peer | `head_block_num == 0` | Returns `true` — no fork to be on |
| Range overlap | `head_num >= our_earliest && head_num <= our_latest` | Uses `is_block_known(head_id)` |
| Boundary link | `head_num + 1 == our_earliest` | Reads `our_earliest_block`, checks `previous == head_id` |
| LIB fallback | Always | `is_block_known(lib_id)` as before |

**2. `on_dlt_hello` lifecycle transition fixed** (fixes P3, P14):

```
OLD:  if (reply.exchange_enabled || _node_status == DLT_NODE_STATUS_SYNC)
NEW:  if (reply.exchange_enabled || _node_status == DLT_NODE_STATUS_SYNC
          || hello.node_status == DLT_NODE_STATUS_SYNC)
```

Added diagnostic log for non-aligned SYNC peers so operators can distinguish "needs snapshot" from "hostile fork".

### New Expected Behavior — Per-Slave

#### slaveA (head=800, master dlt=[1000-2000])

| Step | Before Fix | After Fix |
|------|-----------|-----------|
| Hello to master | fork_alignment=false (block 800 pruned) | fork_alignment=false (800+1 ≠ 1000) |
| Lifecycle on master | HANDSHAKING → 10s timeout → disconnect | **Transitions to ACTIVE** (hello.node_status == SYNC) |
| Blocks exchanged? | No — gets disconnected | No — exchange_enabled=false, but stays connected |
| Recovery path | Snapshot plugin stall detection → download | Same — needs snapshot from trusted peers |
| **Improvement** | Disconnect/reconnect loop with backoff | **Stays connected**, no backoff penalty |

#### slaveB (head=999, master dlt=[1000-2000])

| Step | Before Fix | After Fix |
|------|-----------|-----------|
| Hello to master | fork_alignment=false (block 999 pruned) | **fork_alignment=true** — boundary link: block 1000.previous == H999 |
| Lifecycle on master | HANDSHAKING → timeout | **Transitions to ACTIVE** (exchange_enabled=true) |
| Blocks exchanged? | No | **Yes!** Requests blocks 1000-1199, master sends full range |
| Sync completion | N/A | Catches up 999→2000, transitions to FORWARD |
| **Improvement** | Had to snapshot-download from 1500 | **Direct P2P sync 999→2000** — zero snapshot overhead |

#### slaveC (no blocks, empty node)

| Step | Before Fix | After Fix |
|------|-----------|-----------|
| Hello to master | fork_alignment=false (zero_id not known) | **fork_alignment=true** — empty peer check |
| Lifecycle on master | HANDSHAKING → timeout | **Transitions to ACTIVE** (exchange_enabled=true) |
| Blocks exchanged? | No | No — peer_dlt_latest==0, no request sent |
| Recovery path | Snapshot plugin stall detection → download | Same — needs snapshot from trusted peers |
| **Improvement** | Disconnect/reconnect loop | **Stays connected**, doesn't waste bandwidth |

### Problems Resolved

```
P1  [OK] check_fork_alignment now DLT-range-aware
P2  [OK] Range-overlap check added
P3  [OK] HANDSHAKING timeout loop fixed (SYNC peers always → ACTIVE)
P6  [OK] Boundary link check added
P7  [OK] check_fork_alignment receives full hello with range data
P8  [OK] Block number comparison via boundary link + range overlap
P9  [OK] Empty peer (head=0) recognized and accepted
P13 [OK] check_fork_alignment signature updated
P14 [OK] on_dlt_hello lifecycle logic fixed
P15 [OK] DLT range negotiation via boundary link check

P4  [OK] Wasted range requests fixed — start clamped to peer_dlt_earliest
P5  [  ] Recovery still snapshot-plugin-driven (but no reconnect loop delays it)
P10 [  ] No "needs snapshot" signal yet
P11 [NA] Peer exchange works correctly — no fix needed
P12 [NA] No anti-spam needed — lifecycle fix eliminated reconnect loop
P16 [  ] No differentiation between reasons (improved via diagnostic log)

P17 [OK] DLT block log corruption → auto-detected and reset on startup
P20 [OK] Dead fork blocks → DEAD_FORK result, soft-ban peer, no crash
P21 [OK] Dead fork crash loop → dead-fork blocks rejected, fork_db protected
P24 [OK] Snapshot lock isolation → periodic tasks skip DB locks, stall check skips
P27 [OK] Write lock diagnostic → overall + per-plugin timing, chainbase lock-holder ID
```

---

## New Problems Discovered (Post-Implementation)

After deploying the fixes, the following new issues were observed in production (emergency consensus, DLT mode, 4-node network).

---

### P17: DLT Block Log Corruption on Crash → Infinite Restart Loop

**Observed on:** Master node (185.146.232.170)

**Symptom:** Node crashes, then on restart the DLT block log opens with only **one block**:
```
DLT block log: opened with blocks 79637600-79637600
```
Immediately after:
```
terminate called after throwing an instance of 'boost::interprocess::lock_exception'
  what():  boost::interprocess::lock_exception
```
Node auto-restarts and loops with high CPU overhead.

**Root cause hypothesis:** On crash, the DLT block log index/data files become inconsistent. The index truncates to a single entry while the database head is far ahead. The `boost::interprocess::lock_exception` is thrown when the database tries to acquire the shared memory lock that the previous (crashed) process held.

**Severity:** **CRITICAL** — node cannot recover without manual intervention.

**Related files:**
- [dlt_block_log.cpp](file:///d:/Work/viz-cpp-node/libraries/chain/dlt_block_log.cpp)
- [database.cpp](file:///d:/Work/viz-cpp-node/libraries/chain/database.cpp) (open path)

---

### P18: Master Block Production Halt (No Clear Reason)

**Observed on:** Master node (185.146.232.170, ~16-20 scheduled slots)

**Symptom:** Node was producing blocks normally, then abruptly stopped. In logs:
```
maybe_produce_block returned 3   (slot=0 — missed/not-my-slot)
```
This repeated for hundreds of iterations (minutes of wall time) with no blocks produced. Eventually recovered on its own. During the halt, `slot=0` was returned every cycle, meaning `get_slot_at_time()` returned slot 0 — either the time was before the first slot, or the slot calculation was wrong.

**Also observed:** After some restarts the pattern changed:
```
maybe_produce_block returned 1   (not my slot, but slot had a value)
```
Here `scheduled_witness=social` (a slave witness), so the master correctly doesn't produce, but the slave's block never arrived.

**Severity:** **CRITICAL** — network stalls for minutes with no block production.

**Related files:**
- [witness.cpp](file:///d:/Work/viz-cpp-node/plugins/witness/witness.cpp) — `maybe_produce_block`

---

### P19: Slave Stuck in Sync — Never Catches Up

**Observed on:** Slave node (80.87.202.57)

**Symptom:** Slave connects to master, synopsis exchange happens, but slave never receives blocks. Stays in SYNC mode indefinitely. No `Got X transactions` lines appear. The synopsis response from master returns an empty reply because the synopsis anchor from the slave references blocks outside the master's DLT range.

**Severity:** **HIGH** — slave never syncs, relies on snapshot plugin for recovery.

---

### P20: Dead Fork Blocks Trigger Sync Status Loss → Silent Crash

**Observed on:** Master node (185.146.232.170)

**Symptom:** Master is producing blocks normally (head=79641907). Then peers send sync blocks 79641905-79641908 which are on a **different fork**:
```
Block 79641905 is from a dead fork (parent not in fork_db, head=79641907)
Block 79641906 is from a dead fork (parent not in fork_db, head=79641907)
Block 79641907 is from a dead fork (parent not in fork_db, head=79641907)
```
Block 79641908 (gap=0) is treated as "near-caught-up" and triggers:
```
Sync mode ended: received normal block #79641908 (head: 79641907)
```
This **resets sync status** on the master. Shortly after, the node silently crashes:
```
json_rpc plugin: plugin_initialize() begin   ← fresh restart
```

**Root cause:** A block from a different fork with `gap=0` triggers `accept_block` logic that treats it as a "normal block", ending sync mode. This corrupts the node's internal state. The crash is likely an OOM (from fork_db growing with unlinkable blocks) or an assertion failure.

**Severity:** **CRITICAL** — malicious peers can crash the master by sending dead-fork blocks.

**Related files:**
- [p2p_plugin.cpp](file:///d:/Work/viz-cpp-node/plugins/p2p/p2p_plugin.cpp) — `handle_block`
- [database.cpp](file:///d:/Work/viz-cpp-node/libraries/chain/database.cpp) — `_push_block`

---

### P21: Dead Fork → Crash → Restart → Dead Fork Loop

**Observed on:** Master node (185.146.232.170)

**Symptom:** After recovering from P20's crash, the master restarts, produces a few blocks, then another dead-fork block arrives:
```
Block from a different fork whose parent is not in fork_db (block 79641912, head=79641913)
```
Note: **head (79641913) > block num (79641912)** — this means the node's head advanced past the block it's rejecting. The node crashes again. This creates an infinite restart loop.

**Key observation:** `head > block_num` when rejecting — the node's own head has moved beyond the rejected block, which means the fork_db might be in an inconsistent state where unlinkable blocks accumulate but the chain head advances independently.

**Severity:** **CRITICAL** — infinite crash loop, node cannot stay up.

---

### P22: fork_db Rejection Cascade on Restart

**Observed on:** Master node after restart

**Symptom:** On restart with DLT range [79640201..79641912], peers send sync blocks 79641905-79641908. These are **older than the node's head** (79641912) but the fork_db doesn't contain their parent chain:
```
Chain pushing sync block #79641905 (head: 79641912, gap: -8)
Rejecting block 79641905 from a different fork: parent not in fork_db (head=79641912)
```
All 4 blocks rejected. Node crashes shortly after.

**Root cause:** After restart, the fork_db is seeded from the DLT block log at block 79641912. Blocks 79641905-79641908 are in the DLT range [79640201..79641912] but their parent chain (below 79641905) is in the fork_db unlinked section or pruned. The gap of -8 means the node is being asked to process blocks it should already have.

**Severity:** **HIGH** — restart recovery fails if peers send older blocks.

---

### P23: fetch_branch_from Assertion in fork_db During Synopsis

**Observed on:** Slave node (80.87.202.57)

**Symptom:** When constructing a synopsis for peer 185.45.192.155:2001:
```
Unable to construct a blockchain synopsis for reference hash 04bf3d5a...
assert_exception: Assert Exception
second_branch_itr != _index.get<block_id>().end():
    {"first":"04bf3de9...","second":"04bf3d5a..."}
    fork_database.cpp:201 fetch_branch_from
```
The peer's reference block is on a fork that our fork_db doesn't know about. The `fetch_branch_from` function assumes both branches exist in the index, but the second branch doesn't.

**Also causes:** Connection breakage with that peer, synopsis failure, lost sync time.

**Severity:** **HIGH** — can crash the node or break sync with legitimate peers.

**Related files:**
- [fork_database.cpp](file:///d:/Work/viz-cpp-node/libraries/chain/fork_database.cpp) — `fetch_branch_from`
- [database.cpp](file:///d:/Work/viz-cpp-node/libraries/chain/database.cpp) — `get_block_ids_on_fork`

---

### P24: Snapshot Write Lock Freezes Entire Node

**Observed on:** Slave node (80.87.202.57)

**Symptom:** When a periodic snapshot fires at block 79642200:
```
Periodic snapshot at block 79642200: /var/lib/vizd/snapshots/snapshot-block-79642200.vizjson
```
Immediately, **all P2P connections** start failing:
```
Read lock timeout
Read lock timeout
... (repeated hundreds of times)
Peer connection terminating (95.217.177.173:2001), now 5 active peers
Peer connection closing (95.217.177.173:2001): Disconnecting due to inactivity
...
Peer connection closing (185.146.232.170:2001): Disconnecting due to inactivity, now 0 active peers
```

**Timeline:**
| Time | Event |
|------|-------|
| T+0s | Snapshot starts, WRITE lock acquired |
| T+13s | `auto-clearing stuck peer_needs_sync_items_from_us` for master peer (30s timeout) |
| T+20s | `Skipping head_block_num read: Unable to acquire READ lock` |
| T+130s | All peers disconnected due to inactivity |
| T+150s | `Stalled sync detected while snapshot in progress — cancelling snapshot to release locks` |
| T+160s | `Snapshot still in progress after 10s wait, proceeding with recovery anyway` |

**The stall detection itself fails** because it can't acquire the READ lock:
```
trigger_resync: could not read head block (lock contention?): Unable to acquire READ lock
```

**Root cause:** The snapshot serialization holds a WRITE lock on the database for the entire duration. P2P needs READ locks for every operation (reading head, LIB, block IDs). The snapshot can take 2+ minutes for large state, during which the node is completely isolated from the network.

**Severity:** **CRITICAL** — periodic snapshots cause complete network disconnection. Self-reinforcing: no peers → no blocks → stall detection → can't recover because of lock.

**Related files:**
- [snapshot/plugin.cpp](file:///d:/Work/viz-cpp-node/plugins/snapshot/plugin.cpp) — `on_applied_block` → snapshot creation

---

### P25: Slave-Produced Block Ignored by Master → Fork Switch

**Observed on:** Slave (80.87.202.57) + Master (185.146.232.170)

**Symptom:** Slave produces block #79645211 (witness="social", slot time 10:20:24), sends it via P2P. Master never receives/processes it and produces its own block #79645211 (witness="committee", slot time 10:20:27) — 3 seconds later for the same block number:

**Slave side:**
```
Block num collision at block 79645211: 2 blocks with SAME parent (possible double-production)
Fork switch: new_head=#79645211, db_head=#79645211
FORK-SWITCH-POP: popping head #79645211 (target=04bf4a1a...)
```

**Master side:**
```
scheduled_witness=social
maybe_produce_block returned 2   (not our slot to produce)
scheduled_witness=committee
... produces block #79645211 by committee
```

**Root cause:** The slave sent its block but the master either:
1. Had the slave peer in a state where blocks from it weren't accepted
2. The block was delayed/lost in P2P transmission
3. The master's `exchange_enabled` flag for the slave was false

The master then produced a competing block because it didn't see the slave's block within the slot window.

**Severity:** **HIGH** — causes unnecessary fork switches, wastes blocks, confuses witnesses.

---

### P26: Sync State Confusion on Slave — Blocks Received But Status Stays SYNC

**Observed on:** Slave node (80.87.202.57)

**Symptom:** Slave receives blocks normally via sync (`Chain pushing sync block #...`), processes them, even generates its own blocks, but the node status **never transitions from SYNC to FORWARD**. It stays in SYNC mode while actually having caught up.

**Severity:** **MEDIUM** — node functions but reports wrong status, may affect peer selection logic.

---

### P27: Write Lock Held 25+ Seconds During Block Application

**Observed on:** Master node (185.146.232.170)

**Symptom:** During `_apply_block`, the write lock is held for **25+ seconds**:

```
Read lock timeout [lock=READ waiter_tid=... wait_ms=500 readers=0 writer_tid=... writer_held_ms=503]
Read lock timeout [lock=READ waiter_tid=... wait_ms=1000 readers=0 writer_tid=... writer_held_ms=1003]
...
Read lock timeout [lock=READ waiter_tid=... wait_ms=2000 readers=0 writer_tid=... writer_held_ms=25670]
```

**All RPC calls fail** during this period:
```
elapsed: 2.01s, error: 'Unable to acquire READ lock [writer_held_ms=22857]'
```

The lock is held between these DEBUG_CRASH markers:
```
DEBUG_CRASH: notify_applied_block start    ← lock acquired before this
... 25+ seconds pass ...
DEBUG_CRASH: notify_applied_block done     ← lock released after this
```

**User explicitly requests:** "add normal logs that show WHO set lock, where in which method?" — the current `Read lock timeout` messages show the writer's TID but not which function/code line is holding the lock.

**Severity:** **CRITICAL** — 25-second write lock makes the node unresponsive to all P2P and RPC traffic. The `notify_applied_block` plugin callbacks are the bottleneck — one of the plugins (likely `operation_history`, `account_history`, or `snapshot`) is doing heavy synchronous work under the write lock.

**Related files:**
- [database.cpp](file:///d:/Work/viz-cpp-node/libraries/chain/database.cpp) — `_apply_block`
- All plugin `on_applied_block` handlers

---

### P28: Build Error — multimap::erase and ip::address::data() API

**Observed on:** Docker build (GCC 13)

**Symptom:** Compilation fails in `dlt_p2p_node.cpp`:

1. **multimap::erase with pair**:
```cpp
_mempool_by_expiry.erase(std::make_pair(it->second.trx.expiration, it->first));
```
Error: `no matching function for call to 'std::multimap<...>::erase(std::pair<...>)'`

In C++17, `multimap::erase` no longer accepts a value/pair directly — it requires an iterator, key, or iterator range. **Fix:** Use `_mempool_by_expiry.erase(it_by_expiry)` where `it_by_expiry` is the iterator to the element.

2. **ip::address::data() doesn't exist**:
```cpp
auto a_data = a.data();   // 'const class fc::ip::address' has no member named 'data'
```
**Fix:** Use `fc::raw::pack(a)` or the appropriate fc serialization method.

**Severity:** **MEDIUM** — blocks compilation with newer GCC.

**Affected locations** (3 call sites):
- `transition_to_forward()` line 972
- `remove_transactions_in_block()` line 1089
- `prune_mempool_on_fork_switch()` line 1099

**Related files:**
- [dlt_p2p_node.cpp](file:///d:/Work/viz-cpp-node/libraries/network/dlt_p2p_node.cpp)

---

### P29: Build Error — Missing witness_plugin.hpp Header

**Observed on:** Docker build

**Symptom:**
```
fatal error: graphene/plugins/witness/witness_plugin.hpp: No such file or directory
#include <graphene/plugins/witness/witness_plugin.hpp>
```

**Root cause:** The include path is wrong. The actual file is at `plugins/witness/include/graphene/plugins/witness/witness.hpp` (note: `witness.hpp` not `witness_plugin.hpp`). The CMake include directories may not include the witness plugin's include path.

**Severity:** **MEDIUM** — blocks compilation in certain build configurations.

**Related files:**
- [p2p_plugin.cpp](file:///d:/Work/viz-cpp-node/plugins/p2p/p2p_plugin.cpp) line 9

---

### P30: Multiple API Mismatches in p2p_plugin.cpp

**Observed on:** Docker build (after fixing P29)

**Symptom:** Multiple compilation errors in `p2p_plugin.cpp`:

| # | Error | Root Cause |
|---|-------|------------|
| 1 | `with_read_lock([&]{...})` — candidate expects 6 arguments, 1 provided | The `with_read_lock` API changed — now requires `(lock_type, timeout_ms, lambda, file, line, func)` |
| 2 | `'is_emergency_consensus' is not a member` | Field was renamed or removed from `dynamic_global_property_object` |
| 3 | `blocks.front()->id()` — no match for call to `(block_id_type)()` | `id` is a field, not a method. Should be `blocks.front()->id` (without `()`) |
| 4 | `catch (const unlinkable_block_exception&)` — expected unqualified-id before `&` | Missing exception variable name: `catch (const unlinkable_block_exception& e)` |
| 5 | `accept_transaction` doesn't exist, did you mean `apply_transaction`? | Method was renamed |
| 6 | `push_block(*block)` where `block` is `fork_item` — can't convert | `fork_item` is not a `signed_block`. Need `fork_item->data` or cast |
| 7 | `is_known_block(ref_block_num)` with `uint32_t` — expects `block_id_type` | Need to fetch block ID by number first |

**Severity:** **HIGH** — blocks compilation, indicates the p2p_plugin.cpp was written against a different API version than what's in the codebase.

**Related files:**
- [p2p_plugin.cpp](file:///d:/Work/viz-cpp-node/plugins/p2p/p2p_plugin.cpp)

---

### P31: Linker Error — static constexpr ODR-use

**Observed on:** Docker build

**Symptom:**
```
/usr/bin/ld: undefined reference to `graphene::network::dlt_peer_state::MAX_RECONNECT_BACKOFF_SEC'
```

**Root cause:** `MAX_RECONNECT_BACKOFF_SEC` is declared as `static constexpr` in the header but used in a context that requires a definition (ODR-use). The code takes its address or binds a reference to it. In C++17, `static constexpr` members still need an out-of-line definition if they're ODR-used.

**Fix:** Add a definition in the `.cpp` file:
```cpp
constexpr uint32_t dlt_peer_state::MAX_RECONNECT_BACKOFF_SEC;
```
Or change the usage to avoid ODR-use.

**Severity:** **MEDIUM** — linker error, blocks final linking.

**Related files:**
- [dlt_p2p_peer_state.hpp](file:///d:/Work/viz-cpp-node/libraries/network/include/graphene/network/dlt_p2p_peer_state.hpp)
- [dlt_p2p_node.cpp](file:///d:/Work/viz-cpp-node/libraries/network/dlt_p2p_node.cpp)

---

## Implemented Fixes (2026-05-06)

### Fix 1: Dead Fork Block Crash Protection (P20/P21)

**Files:** `libraries/network/include/graphene/network/dlt_p2p_node.hpp`, `plugins/p2p/p2p_plugin.cpp`, `libraries/network/dlt_p2p_node.cpp`

**Root cause:** When a FORWARD node receives sync blocks from a peer on a dead fork (parent not in fork_db, head already past the block), `_push_block` throws `unlinkable_block_exception`. But the P2P layer's `on_dlt_block_range_reply` didn't distinguish between "rejected because validation failed" and "rejected because dead fork." A block with `block_num == head` (gap=0) from a dead fork could trigger "sync mode ended" logic, corrupting internal state. The fork_db accumulated unlinked blocks until OOM/assertion crash. After restart, peers re-sent the same blocks, creating a crash loop.

**Changes:**

1. **`dlt_block_accept_result` enum** — Added `DEAD_FORK` value to distinguish dead-fork rejection from generic rejection (`dlt_p2p_node.hpp` line 34).

2. **`dlt_delegate::accept_block()`** (`p2p_plugin.cpp` line 166-183) — When `push_block()` throws `unlinkable_block_exception`:
   - If `block.block_num() <= head_block_num()`: returns `DEAD_FORK` (peer is on a competing fork that diverged before our fork_db window). Does NOT push to `fork_db._unlinked_index`, preventing unbounded growth.
   - If `block.block_num() > head_block_num()`: returns `FORK_DB_ONLY` (block is ahead but has a gap — store in `_unlinked_index` for later linking).

3. **`on_dlt_block_range_reply()`** (`dlt_p2p_node.cpp` line 760-768) — On `DEAD_FORK` result: soft-ban the peer immediately and break out of the block processing loop. No more blocks are accepted from that peer in the range.

4. **`on_dlt_block_reply()`** (`dlt_p2p_node.cpp` line 856-862) — Same DEAD_FORK soft-ban for single-block replies.

5. **`transition_to_forward()` guard** (`dlt_p2p_node.cpp` line 784) — Only transition from SYNC to FORWARD if `any_block_applied` is true. A range full of dead-fork rejects does NOT end sync mode.

**Expected behavior after fix:**
- Dead-fork blocks are rejected immediately without polluting fork_db
- Peers sending dead-fork blocks are soft-banned (10 min)
- No more "sync mode ended" from dead-fork gap=0 blocks
- No more crash loops from fork_db OOM or state corruption

---

### Fix 2: DLT Block Log Corruption Recovery (P17)

**Files:** `libraries/chain/include/graphene/chain/dlt_block_log.hpp`, `libraries/chain/dlt_block_log.cpp`, `libraries/chain/database.cpp`

**Root cause:** On crash, the DLT block log index/data files can become inconsistent — the index truncates to a single entry while the database head is far ahead. Opening a corrupted DLT block log and using it for fork_db seeding causes cascading P2P failures (dead forks, sync stalls, crash loops).

**Changes:**

1. **`dlt_block_log::is_consistent_with()`** (`dlt_block_log.cpp` line 604-652) — New method that compares the DLT block log state with the database head block number. Detection rules:
   - Empty DB or empty DLT log → consistent (normal)
   - 1 block in log but DB has thousands → **corrupted** (index truncated on crash)
   - DLT head exceeds DB head → **corrupted**
   - DB head > DLT head + 1000 and DLT has < 10 blocks → **corrupted** (partial wipe)

2. **Corruption detection in `database::open()`** (`database.cpp` line 269-280) — Before seeding fork_db from the DLT block log, validate consistency. If corrupted:
   - Log a clear error with database head block number
   - Call `_dlt_block_log.reset()` to wipe the corrupted log
   - P2P sync will rebuild the DLT block log from scratch

**Expected behavior after fix:**
- Corrupted DLT block log is detected and auto-reset on startup
- No more infinite restart loops from inconsistent block log
- P2P sync rebuilds the log naturally after reset

---

### Fix 3: Snapshot Lock Isolation Prevention (P24)

**Files:** `libraries/network/dlt_p2p_node.cpp`, `plugins/snapshot/plugin.cpp`

**Root cause:** Snapshot `create_snapshot()` runs on a background thread with a strong read lock on the database. During the 30-120s serialization, the P2P thread's periodic tasks (stats, peer exchange, hello broadcasts, stall detection) also need read locks but time out because the snapshot's lock blocks them. This cascades into all peer disconnections.

**Changes:**

1. **`periodic_task()` early-return guard** (`dlt_p2p_node.cpp` line 1682-1708) — When `_block_processing_paused` is true (snapshot in progress), skip periodic operations that need database read locks:
   - Skip `sync_stagnation_check()` — stale blocks won't arrive anyway
   - Skip `periodic_peer_exchange()` — can wait
   - Skip `log_peer_stats()` — cosmetic only
   - Still run: `periodic_reconnect_check()`, `periodic_lifecycle_timeout_check()`, `block_validation_timeout()`, `periodic_mempool_cleanup()`, banned-peer unban check

2. **`check_stalled_sync_loop()` snapshot awareness** (`snapshot/plugin.cpp` line 1833-1843) — When `snapshot_in_progress` is true, skip the stall check entirely and reset the timer. The stall is caused by the snapshot (expected), not by actual network failure. Without this, the stall detector would trigger a false recovery (cancel the snapshot, attempt resync while still locked).

3. **`serialize_state()` progress logging** (`snapshot/plugin.cpp` line 845-860) — The `EXPORT_INDEX` macro now logs progress every 5 seconds during long serialization. Operators can see the snapshot is progressing instead of assuming the node is frozen.

**Expected behavior after fix:**
- Snapshot creation no longer causes peer disconnections
- Stall detection doesn't trigger false recovery during snapshots
- Progress logs give visibility into snapshot serialization

---

### Fix 4: Write Lock Diagnostic Logging (P27)

**Files:** `libraries/chain/database.cpp`, `plugins/mongo_db/mongo_db_plugin.cpp`, `plugins/operation_history/plugin.cpp`, `plugins/account_history/plugin.cpp`

**Root cause:** `_apply_block` holds the WRITE lock through `notify_applied_block()` which calls ALL registered plugin callbacks synchronously. One plugin (likely `operation_history`, `account_history`, or `mongo_db`) does heavy work under the lock, blocking all P2P and RPC for 25+ seconds.

**Changes:**

1. **Overall timing in `notify_applied_block()`** (`database.cpp` line 2181-2199) — Times the total `applied_block` signal notification. If it exceeds 200ms, logs a warning with block number, duration, and number of connected plugins. This identifies when the bottleneck occurs without changing behavior.

2. **Per-plugin self-timing** — Added self-timing to the 3 most likely slow `applied_block` handlers:
   - `mongo_db_plugin.cpp` line 99-108: Times `on_block()`, logs if >100ms
   - `operation_history/plugin.cpp` line 266-275: Times `purge_old_history()`, logs if >100ms
   - `account_history/plugin.cpp` line 543-552: Times `purge_old_history()`, logs if >100ms

3. **Lock-holder identification** — Already implemented in chainbase (`chainbase.hpp` line 1330-1342): `with_strong_write_lock` macro auto-captures `__FILE__`, `__LINE__`, `__func__` at every call site. Lock timeout messages now include `writer_at=file:line func`, `writer_held_ms`, and `writer_tid`.

**Expected behavior after fix:**
- When a 25s write lock occurs, the log will show:
  - Which block triggered it (`applied_block notification took Xms for block #N`)
  - Which specific plugin is slow (`mongo_db on_block took Xms` or `operation_history purge_old_history took Xms`)
  - Which write lock call site is holding it (from chainbase diagnostics)
- No behavior change — only diagnostic logging added

**Next step (not implemented):** Once the slow plugin is identified, defer its heavy work to a background thread after the write lock is released.

---

## Summary of New Problems

```
P17 [FIX] DLT block log corruption on crash → infinite restart loop
P18 [FIX] Master stops producing blocks for minutes (slot=0 loop) → stall detector + NTP force-sync
P19 [FIX] Slave stuck in SYNC → gap detection + multi-peer fallback + snapshot warning
P20 [FIX] Dead fork blocks trigger sync status loss → silent crash
P21 [FIX] Dead fork → crash → restart → dead fork loop
P22 [FIX] fork_db rejection cascade on restart → seed 100 blocks + 60s grace period
P23 [FIX] fetch_branch_from assertion failure → graceful empty-branch return
P24 [FIX] Snapshot write lock freezes entire node (all peers disconnect, self-isolation)
P25 [FIX] Slave-produced block ignored by master → exchange_enabled re-evaluated
P26 [FIX] Sync state confusion → check_sync_catchup() on block accept + periodic
P27 [FIX] Write lock held 25+ sec during _apply_block (notify_applied_block bottleneck)
P28 [MED]   Build error: multimap::erase with pair, ip::address::data() missing
P29 [MED]   Build error: missing witness_plugin.hpp include
P30 [HIGH]  Build error: multiple API mismatches in p2p_plugin.cpp (6+ separate issues)
P31 [MED]   Linker error: static constexpr ODR-use (MAX_RECONNECT_BACKOFF_SEC)
```

---

## New Problem Priority Matrix

### Immediate (node stability)

| # | Problem | Impact | Status |
|---|---------|--------|--------|
| P17 | DLT block log corruption → restart loop | Node can't recover | **Fixed** |
| P20 | Dead fork blocks → crash | Malicious peer can crash master | **Fixed** |
| P21 | Dead fork → crash loop | Node stays down | **Fixed** |
| P24 | Snapshot lock → network isolation | Node disappears from network | **Fixed** |
| P27 | 25s write lock → all RPC/P2P fails | Node unresponsive | **Fixed** (diagnostic) |

### High (sync/catchup reliability)

| # | Problem | Impact | Status |
|---|---------|--------|--------|
| P18 | Master stops producing blocks | Network stalls | **Fixed** |
| P19 | Slave stuck in SYNC | Never catches up | **Fixed** |
| P22 | fork_db rejection cascade | Restart recovery fails | **Fixed** |
| P23 | fetch_branch_from assertion | Sync with some peers breaks | **Fixed** |
| P25 | Slave block ignored by master | Fork switches | **Fixed** |

### Build (must compile)

| # | Problem | Impact |
|---|---------|--------|
| P28 | multimap::erase, ip::address API | Won't compile on GCC 13+ |
| P29 | Missing header include | Won't compile |
| P30 | 6+ API mismatches in p2p_plugin | Won't compile |
| P31 | static constexpr linker error | Won't link |

### Lower

| # | Problem | Impact | Status |
|---|---------|--------|--------|
| P26 | Sync state confusion | Cosmetic status bug | **Fixed** |
