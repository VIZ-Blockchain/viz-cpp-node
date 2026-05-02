# VIZ Blockchain — Block Processing & Pending Transactions

Internal mechanics of block application, transaction queuing, and the pending transaction lifecycle.

---

## Overview

When a node receives a new block via P2P, it must:
1. Temporarily remove its pending (mempool) transactions from the database
2. Apply the incoming block
3. Re-apply the pending transactions that were not included in the block

This process is managed by the `without_pending_transactions` helper in `db_with.hpp`.

---

## Key Data Structures

| Structure | Type | Location | Purpose |
|---|---|---|---|
| `_pending_tx` | `vector<signed_transaction>` | `database.hpp:473` | Transactions received from the network, waiting to be included in a block |
| `_popped_tx` | `deque<signed_transaction>` | `database.hpp:472` | Transactions from a popped block (during fork switch), to be re-applied |
| `_pending_tx_session` | `optional<session>` | `database.hpp:517` | Undo session covering all pending transaction state changes |

---

## Block Application Flow

### `push_block()` → `without_pending_transactions()`

```
push_block(new_block)
  └─ without_pending_transactions(db, skip, _pending_tx, callback)
       ├─ pending_transactions_restorer constructor: clear_pending()
       ├─ callback: _push_block(new_block)        ← apply the incoming block
       └─ ~pending_transactions_restorer()         ← restore pending transactions
```

Source: [database.cpp:897-920](../../libraries/chain/database.cpp#L897)

The destructor of `pending_transactions_restorer` is where the "Postponed" log messages appear.

---

## Pending Transaction Restoration (Destructor Logic)

Source: [db_with.hpp](../../libraries/chain/include/graphene/chain/db_with.hpp)

The destructor processes two lists in order:

### Step 1: Re-apply `_popped_tx` (from fork switches)

```
for each tx in _popped_tx:
    if time limit exceeded → push to _pending_tx (postpone)
    else if is_known_transaction → skip (already in chain)
    else → _push_transaction(tx) → applied_txs++
```

### Step 2: Re-apply `_pending_transactions` (original mempool)

```
for each tx in _pending_transactions:
    if time limit exceeded → push to _pending_tx (postpone)
    else if is_known_transaction → skip (already in the new block)
    else → _push_transaction(tx) → applied_txs++
           on transaction_exception → dlog (invalid, discard)
           on fc::exception → silently discard
```

### Step 3: Log summary

If any transactions were postponed, a single warning is logged:
```
Postponed N pending transactions. M were applied.
```

---

## Time Limit: CHAIN_PENDING_TRANSACTION_EXECUTION_LIMIT

**Value:** `fc::milliseconds(200)` ([config.hpp:141](../../libraries/protocol/include/graphene/protocol/config.hpp#L141))

The destructor tracks elapsed time since the start of restoration. Once 200ms is exceeded, all remaining transactions are postponed (pushed back to `_pending_tx`) without attempting to apply them. This prevents the node from blocking for too long when re-applying a large number of pending transactions.

### When the time limit triggers

- High transaction throughput blocks (many pending txs to restore)
- Slow individual transaction evaluation (complex operations)
- System under load (CPU contention)

---

## Block Size Limit: CHAIN_BLOCK_GENERATION_POSTPONED_TX_LIMIT

**Value:** `5` ([config.hpp:140](../../libraries/protocol/include/graphene/protocol/config.hpp#L140))

During block **generation** (`_generate_block`), transactions that would exceed `maximum_block_size` are skipped. After `CHAIN_BLOCK_GENERATION_POSTPONED_TX_LIMIT` consecutive oversized transactions, the loop breaks entirely. These transactions remain in `_pending_tx` for the next block.

This is a different code path from the `pending_transactions_restorer` and produces a separate log:
```
Postponed N transactions due to block size limit
```

Source: [database.cpp:1125-1160](../../libraries/chain/database.cpp#L1125)

---

## Fork DB Head-Seeding

Source: [database.cpp `_push_block`](../../libraries/chain/database.cpp)

Before pushing a block to `fork_db`, `_push_block()` ensures the current database head block is present in `fork_db`. After snapshot import, stale sync recovery, or fork_db trimming, the head block may be absent from `fork_db`'s `_index`.

Without this seed, any block whose `previous == head_block_id()` would throw `unlinkable_block_exception` inside `fork_database::_push_block()` ("block does not link to known chain"), silently rejecting valid next-blocks and preventing head advancement.

```
if new_block.previous == head_block_id()
   AND head_block_id() NOT in fork_db:
     fetch head block from block log
     fork_db.start_block(head_block)   ← seeds fork_db with the head
```

This also fixes **witness nodes that generate their own blocks**: `generate_block()` sets `pending_block.previous = head_block_id()`, and without the seed the self-generated block would fail to push into `fork_db`.

---

## Direct-Extension Bypass

Source: [database.cpp `_push_block`](../../libraries/chain/database.cpp)

After pushing a block to `fork_db`, `_push_block()` checks whether the block directly extends the database head (`new_block.previous == head_block_id()`). If so, the fork switch logic is bypassed entirely and the block falls through to `apply_block()`.

This handles the case where `fork_db._head` points to a stale higher block accumulated from previous failed sync cycles (stale sync recovery does not reset `fork_db`). Without this bypass:

1. `fork_db.push_block()` returns the stale `_head` (e.g., block #79609893)
2. `new_head->data.previous != head_block_id()` evaluates to TRUE
3. The fork switch logic either rejects the block (head not in fork_db) or can't compare branches
4. The valid next-block is silently dropped, head never advances

```
if new_block.previous == head_block_id():
    → skip fork switch, fall through to apply_block
else if new_head->data.previous != head_block_id():
    → existing fork switch logic (unchanged)
```

Together with fork_db head-seeding, this ensures blocks that correctly link to the database head are always applied, regardless of `fork_db`'s internal `_head` state.

---

## Fork Switch Flow

When a node switches to a different fork:

1. `pop_block()` removes the current head block
   - Transactions from the popped block are saved to `_popped_tx`
   - Source: [database.cpp](../../libraries/chain/database.cpp)

2. The new block is applied via `push_block()`

3. In `~pending_transactions_restorer()`:
   - `_popped_tx` transactions are processed first (from the old fork)
   - Then original `_pending_transactions` are processed
   - Duplicate transactions (already in the new chain) are silently skipped

### Linear Extension vs. Actual Fork

When `fork_db._push_next()` auto-links orphan blocks from the unlinked index, the fork_db head can jump multiple blocks ahead of the database head in a single `push_block()` call. This triggers the fork switch code path (`new_head->data.previous != head_block_id()`), but there is no actual fork — the new chain extends directly from the current head.

`fetch_branch_from(new_head, head_block_id)` always appends the common ancestor to **both** branches. For a linear extension, the common ancestor IS the current head:
- `branches.first` = `[new_tip, ..., HEAD]` (blocks to apply + common ancestor)
- `branches.second` = `[HEAD]` (just the common ancestor)

**Detection:** `is_linear_extension = branches.second.size() == 1 && branches.second.back()->id == head_block_id()`.

**Behavior when linear:**
- Skip the pop loop entirely (the common ancestor is already applied, no blocks to undo)
- Skip the common ancestor when applying `branches.first` (avoid re-applying HEAD)
- On error: pop any newly applied blocks back to the common ancestor, set fork_db head to it

**Why this matters in DLT mode:** In DLT mode, LIB = head, so undo sessions are committed (not just pushed). `pop_block()` → `undo()` has no effect — `head_block_id()` never changes. The pop loop becomes infinite, eventually emptying the fork_db and crashing with "popping head block would leave fork DB empty".

For **actual forks** (`branches.second.size() > 1` or common ancestor != head), the original behavior is preserved: pop old-fork blocks including the common ancestor, then re-apply the common ancestor and new-fork blocks from `branches.first`.

### Debug Logging

Diagnostic logs at every `pop_block()` call site:

| Log prefix | Location | Meaning |
|---|---|---|
| `Fork switch: new_head=#X, db_head=#Y, branches.first=N, branches.second=M` | Before fork switch | Shows branch sizes; `branches.second=0` = linear extension |
| `FORK-SWITCH-POP: popping head #H` | Main pop loop | Normal fork switch pop |
| `FORK-RECOVER-POP: popping head #H` | Error recovery pop loop | Reverting failed fork switch |
| `POP_BLOCK: db_head=#X, fork_db_head=#Y, fork_db_head_prev=Z` | Inside `database::pop_block()` | Fork_db state before every pop; `prev=0` = root block (will crash) |

---

## Orphan Block Handling (Unlinked Index)

Source: [database.cpp `_push_block`](../../libraries/chain/database.cpp), [fork_database.cpp](../../libraries/chain/fork_database.cpp)

When a block arrives whose parent is unknown (missed broadcast), the node can either reject it or defer it for later linking.

### Pre-check in `_push_block()`

```
if block.num > head_num
   AND block.previous != head_block_id
   AND block.previous not in fork_db:
     if gap > 100 → reject (too far ahead, avoid memory bloat)
     if gap <= 100 → allow through to fork_db
```

Blocks within 100 of head pass to `fork_db.push_block()`, which throws `unlinkable_block_exception` but stores the block in `_unlinked_index` first.

### Auto-linking via `_push_next()`

When the missing parent block finally arrives and is pushed to fork_db:
1. `_push_block(parent)` links the parent to the chain
2. `_push_next(parent)` searches `_unlinked_index` for children of `parent`
3. Found children are moved from `_unlinked_index` to `_index` and recursively linked
4. fork_db head may jump multiple blocks ahead in one call

This triggers the linear extension fork switch (see above).

### P2P Recovery

When `unlinkable_block_exception` propagates to the P2P layer (`process_block_during_normal_operation`):
- Block **at or below head** → strike counter incremented (soft-ban after 20 strikes)
- Block **ahead of head** → `start_synchronizing_with_peer()` restarts sync to fetch the missing block

---

## Peer Strike-Based Soft-Ban

Source: [node.cpp](../../libraries/network/node.cpp), [peer_connection.hpp](../../libraries/network/include/graphene/network/peer_connection.hpp)

Peers are not immediately soft-banned for sending unlinkable or rejected blocks. Instead, a strike counter accumulates:

| Path | Threshold | Counter field |
|---|---|---|
| Normal operation: unlinkable block at/below head | 20 strikes | `unlinkable_block_strikes` |
| Sync path: generic block rejection | 20 strikes | `unlinkable_block_strikes` |
| Dead fork / block too old | Immediate | N/A |

**Reset on valid block:** When a peer sends a block that is successfully accepted (normal or sync), their `unlinkable_block_strikes` counter resets to 0. This allows honest peers to recover from transient errors (snapshot reload, timing races, brief micro-forks).

---

## Bug Fix: False "Postponed" Log Messages

### Original Bug (fixed)

In `db_with.hpp`, the `~pending_transactions_restorer()` destructor had three bugs on the logging line:

```cpp
// BUGGY CODE (before fix)
if( postponed_txs++ ) {
    wlog( "Postponed ${p} pending transactions. ${a} were applied.", ("p", postponed_txs)("a", applied_txs) );
}
```

| Bug | Impact |
|---|---|
| `postponed_txs++` inside `if` condition | Double increment: once in `else` branch, once in `if` condition — inflated counter |
| Log inside the `for` loop | Message printed on every iteration after first increment, instead of once at the end |
| Counter incremented for skipped known transactions | Transactions already in the block (`is_known_transaction` = true) still triggered the `if(postponed_txs++)` check |

### Example of False Output

With 3 pending transactions that are all already in the incoming block:
```
Postponed 2 pending transactions. 0 were applied.   ← iteration 2 (postponed_txs was 1, now 2)
Postponed 3 pending transactions. 0 were applied.   ← iteration 3 (postponed_txs was 2, now 3)
```

None of the transactions were actually postponed — they were already known, just skipped.

### Fix

Move the log outside the loop and remove the double increment:

```cpp
// FIXED CODE
}   // end of for loop
if( postponed_txs > 0 ) {
    wlog( "Postponed ${p} pending transactions. ${a} were applied.", ("p", postponed_txs)("a", applied_txs) );
}
```

Now the log only fires once, after both loops complete, with an accurate count of truly postponed transactions.

---

## Bug Fix: Witness Plugin Option Parsing

Source: [witness.cpp](../../plugins/witness/witness.cpp)

### Bug 1: `enable-stale-production` with `implicit_value(false)`

```cpp
// BUGGY CODE (before fix)
("enable-stale-production", bpo::value<bool>()->implicit_value(false), ...)
```

Using `--enable-stale-production` on the command line (without `=true`) would set the value to `false` — the same as the default. The flag was effectively a no-op unless you explicitly wrote `--enable-stale-production=true`.

**Fix:** Changed to `implicit_value(true)` so the bare flag enables stale production as expected.

### Bug 2: `required-participation` double-scaling

```cpp
// BUGGY CODE (before fix)
("required-participation", bpo::value<int>()->implicit_value(33), ...)
// ...
int e = options["required-participation"].as<int>();
_required_witness_participation = uint32_t(e * CHAIN_1_PERCENT);
```

The value passed by the user (e.g. `33` meaning 33%) was multiplied by `CHAIN_1_PERCENT` (100), producing 3300 basis points. This was correct for percentage input but:
- It was inconsistent with internal representation (basis points)
- Users putting basis points in config files would get 100× scaling
- `implicit_value(33)` made the bare flag `--required-participation` set 33%, but the behavior was unclear

**Fix:** Changed to `default_value(33 * CHAIN_1_PERCENT)` and removed the multiplication in parsing. The value is now always in basis points (0–10000 = 0%–100%):

```cpp
// FIXED CODE
("required-participation", bpo::value<uint32_t>()->default_value(33 * CHAIN_1_PERCENT), ...)
// ...
_required_witness_participation = options["required-participation"].as<uint32_t>();
```

---

## Legitimate Reasons for Pending Transaction Postponement

| Reason | Mechanism | Log |
|---|---|---|
| **200ms timeout exceeded** | `CHAIN_PENDING_TRANSACTION_EXECUTION_LIMIT` — remaining txs pushed to `_pending_tx` | `Postponed N pending transactions. M were applied.` |
| **Block size limit** (during generation) | `maximum_block_size` — oversized txs skipped, >5 consecutive → break | `Postponed N transactions due to block size limit` |
| **Transaction became invalid** | State changed by new block (e.g., account balance insufficient) | Caught by `fc::exception`, silently discarded |
| **Transaction already in block** | `is_known_transaction()` returns true | Silently skipped (no postpone, no log) |

---

## Witness Block Production Timing

Source: [witness.cpp](../../plugins/witness/witness.cpp)

### Production Loop Mechanism

The witness plugin uses a timer-based production loop with a look-ahead to detect when it's time to produce a block:

1. **Timer** fires every **250ms** (aligned to 250ms boundaries, minimum sleep 50ms)
2. On each tick, `maybe_produce_block()` computes `now = NTP_time + 250ms` (look-ahead)
3. `get_slot_at_time(now)` finds which slot corresponds to `now`
4. If the slot belongs to one of our witnesses and `|scheduled_time - now| <= 500ms`, the block is produced with `scheduled_time` as the timestamp

The block timestamp is always the **deterministic slot time** (computed from `head_block_time` rounded to `CHAIN_BLOCK_INTERVAL` boundary + `slot_num × 3s`), never the current clock time.

### Why 250ms tick + 250ms look-ahead?

With these matching values, the tick at `T_slot - 250ms` aligns `now` exactly to the slot boundary:

```
Slot at T=6.000, tick at T=5.750:
  now = 5.750 + 0.250 = 6.000 → slot matched → lag = 0ms → PRODUCE
```

This gives a **500ms safety margin** against the LAG threshold, compared to 0ms margin with the previous 1000ms tick + 500ms look-ahead.

### Missed Block Behavior

When a witness misses their slot, the production loop does NOT wait or retry. The next tick simply finds a later slot:

```
Slot T=3 missed (witness A absent):
  Tick at T=3.000 → now=3.250 → slot=1 → witness A → not our witness → not_my_turn
  (A's slot passes unclaimed)

Slot T=6 (witness B - our witness):
  Tick at T=5.750 → now=6.000 → slot=2 → witness B → PRODUCE with timestamp T=6.000
```

When block at T=6 is pushed, `update_global_dynamic_data()` counts `missed_blocks = get_slot_at_time(6.000) - 1 = 1` and increments `current_aslot` accordingly.

### Production Conditions (in order)

| Check | Condition | Result if failed |
|---|---|---|
| Sync status | Chain is not stale (or `enable-stale-production`) | `not_synced` |
| Slot time | `get_slot_at_time(now) > 0` | `not_time_yet` |
| Witness ownership | Scheduled witness is in our `_witnesses` set | `not_my_turn` |
| Signing key | Witness has non-zero `signing_key` on chain | `not_my_turn` |
| Private key | We have the private key for the signing key | `no_private_key` |
| Participation | Network participation ≥ required (pre-HF12 only) | `low_participation` |
| Lag | `|scheduled_time - now| <= 500ms` | `lag` |
| Fork collision | No competing block at same height in fork_db | `fork_collision` |
| Minority fork | Last 21 blocks NOT all from our own witnesses (or `enable-stale-production` or emergency mode) | `minority_fork` |

---

## Configuration Constants

| Constant | Value | Purpose |
|---|---|---|
| `CHAIN_PENDING_TRANSACTION_EXECUTION_LIMIT` | 200ms | Max time to spend re-applying pending txs after block push |
| `CHAIN_BLOCK_GENERATION_POSTPONED_TX_LIMIT` | 5 | Max consecutive oversized txs to skip during block generation |
| `CHAIN_BLOCK_SIZE` | 65536 bytes | Hard limit on block size |
| `maximum_block_size` | Dynamic (witness median) | Soft limit on block size |
