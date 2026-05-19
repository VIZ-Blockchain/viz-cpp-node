# Block Processing

Internal mechanics of block application, pending transaction management, and fork switching.

---

## Overview

When a node receives a new block via P2P, the chain plugin calls `database::push_block()`. The sequence is:

1. Temporarily remove pending (mempool) transactions from the database.
2. Apply the incoming block.
3. Re-apply pending transactions not included in the block.

This is managed by the `without_pending_transactions` helper in `db_with.hpp`.

---

## Key Data Structures

| Structure | Type | Purpose |
|-----------|------|---------|
| `_pending_tx` | `vector<signed_transaction>` | Mempool: received transactions waiting to be included in a block |
| `_popped_tx` | `deque<signed_transaction>` | Transactions from a popped block (during fork switch); re-applied after switch |
| `_pending_tx_session` | `optional<session>` | Undo session covering all pending transaction state changes |

---

## Block Application Flow

```
push_block(new_block)
  └─ without_pending_transactions(db, skip, _pending_tx, callback)
       ├─ pending_transactions_restorer ctor: clear_pending()
       ├─ callback: _push_block(new_block)      ← apply the incoming block
       └─ ~pending_transactions_restorer()       ← restore pending transactions
```

### Step-by-step inside `_push_block()`

1. **Early rejection checks** (see below).
2. Push the block to `fork_db`.
3. If the new fork head directly extends the current head (`new_block.previous == head_block_id()`):
   - Skip fork switch logic, fall through to `apply_block()`.
4. If the new head is higher and diverges from current head:
   - **Vote-weighted fork comparison** (HF12) — see [Fork Resolution](./fork-resolution.md).
   - Pop old-fork blocks until common ancestor.
   - Apply new-fork blocks in order.
5. `apply_block()` runs transaction evaluators, updates dynamic global properties, processes virtual operations.
6. `update_last_irreversible_block()` — advances LIB if ≥14 validators have confirmed.

---

## Pending Transaction Restoration

The `~pending_transactions_restorer()` destructor processes two lists in order after the new block has been applied.

### Step 1: Re-apply `_popped_tx` (from fork switch)

```
for each tx in _popped_tx:
    if time_elapsed > 200ms → postpone (push back to _pending_tx)
    else if is_known_transaction(tx) → skip (already in chain)
    else → _push_transaction(tx) → applied_txs++
```

### Step 2: Re-apply `_pending_transactions` (original mempool)

```
for each tx in _pending_transactions:
    if time_elapsed > 200ms → postpone
    else if is_known_transaction(tx) → skip
    else → _push_transaction(tx) → applied_txs++
           on transaction_exception → discard (invalid)
           on fc::exception → silently discard
```

### Step 3: Log summary

If any transactions were postponed:
```
Postponed N pending transactions. M were applied.
```

---

## Time Limit

**`CHAIN_PENDING_TRANSACTION_EXECUTION_LIMIT` = 200 ms**

Once elapsed since the start of restoration, all remaining transactions are pushed back to `_pending_tx` without being applied. This prevents the node from blocking on a large mempool.

The limit triggers during:
- High-throughput blocks with many pending transactions
- CPU-intensive operations
- System under load

---

## Block Size Limit During Generation

**`CHAIN_BLOCK_GENERATION_POSTPONED_TX_LIMIT` = 5**

During `_generate_block()`, transactions that would exceed `maximum_block_size` are skipped. After 5 consecutive oversized transactions the generation loop breaks. These remain in `_pending_tx` for the next block.

Log:
```
Postponed N transactions due to block size limit
```

---

## Fork DB Head-Seeding

Before pushing a block, `_push_block()` ensures the current database head block is present in `fork_db`:

```
if new_block.previous == head_block_id()
   AND head_block_id() NOT in fork_db:
     fetch head block from block log (or dlt_block_log in DLT mode)
     fork_db.start_block(head_block)
```

Without this seed, valid next-blocks would throw `unlinkable_block_exception` because their `previous` is not in `fork_db`. This also fixes validator nodes that generate their own blocks — `generate_block()` sets `pending_block.previous = head_block_id()`.

---

## Direct-Extension Bypass

After pushing a block to `fork_db`, if the block directly extends the database head:

```
if new_block.previous == head_block_id():
    → skip fork switch, fall through to apply_block()
```

This handles the case where `fork_db._head` points to a stale higher block from a previous failed sync cycle. Without this bypass, the stale head would trigger fork switch logic that silently drops the valid next-block.

---

## Early Block Rejection

`_push_block()` applies several early rejection checks to avoid unnecessary work and prevent infinite sync loops:

| Check | Condition | Action |
|-------|-----------|--------|
| Already applied | `block.num ≤ head` and ID matches existing block | Silently ignore (duplicate) |
| Different fork | `block.num ≤ head`, different ID, parent not in fork_db | Silently reject |
| Far-ahead, gap > 100 | `block.num > head`, parent unknown, gap > 100 blocks | Silently reject (memory protection) |
| Far-ahead, gap ≤ 100 | `block.num > head`, parent unknown, gap ≤ 100 | Allow to fork_db (cached in unlinked index) |
| Direct next block | `block.previous == head_block_id()` | Always allowed |

The 100-block gap threshold prevents memory bloat from dead-fork chains while allowing normal out-of-order block processing during P2P sync.

---

## Fork Switch

When the node switches to a different fork:

1. `pop_block()` removes the current head block; its transactions move to `_popped_tx`.
2. Repeat until the common ancestor is reached.
3. Apply new-fork blocks in order from common ancestor to new head.
4. `~pending_transactions_restorer()` reapplies `_popped_tx` first, then the original mempool.

Transactions already in the new chain are silently skipped via `is_known_transaction()`.

### Linear extension vs. actual fork

`_push_next()` in `fork_db` can auto-link multiple orphan blocks when their parent arrives, causing `fork_db._head` to jump several blocks ahead of the database head in one `push_block()` call. The code distinguishes:

- **Linear extension** (`branches.second.size() == 1` and common ancestor == current head): no pop operations needed; blocks are applied directly.
- **Actual fork switch** (divergent branches): full pop-and-reapply sequence.

This distinction is critical in DLT mode where LIB == head and undo sessions are committed — a pop loop on a linear extension would be infinite.

---

## Orphan Block Handling (Unlinked Index)

When a block arrives whose parent is unknown, `fork_db` stores it in `_unlinked_index`. When the missing parent later arrives:

1. `_push_block(parent)` links the parent to the chain.
2. `_push_next(parent)` iterates `_unlinked_index` for children of `parent`.
3. Children are moved to `_index` and recursively linked.
4. `fork_db._head` may advance multiple blocks in one call (triggers linear extension path).

---

## Peer Strike-Based Soft-Ban

Peers are not immediately banned for sending unlinkable blocks. A counter accumulates:

| Path | Threshold | Reset condition |
|------|-----------|----------------|
| Normal operation: unlinkable block at/below head | 20 strikes | Valid block accepted from same peer |
| Sync path: generic block rejection | 20 strikes | Valid block accepted from same peer |
| Dead fork / block too old | Immediate ban | — |

Honest peers can recover from transient errors (snapshot reload, timing races, brief micro-forks).

---

## Validator Block Production Timing

The validator plugin uses a 250ms timer with 250ms look-ahead:

1. Timer fires every **250ms** (aligned to 250ms wall-clock boundaries, minimum sleep 50ms).
2. `maybe_produce_block()` computes `now = NTP_time + 250ms`.
3. `get_slot_at_time(now)` finds the current slot.
4. If the slot belongs to a configured validator and `|scheduled_time - now| ≤ 500ms`, produce the block with the deterministic `scheduled_time` as the timestamp.

```
Slot at T=6.000, tick at T=5.750:
  now = 5.750 + 0.250 = 6.000 → slot matched → produce
```

This yields a 500ms safety margin against the lag threshold.

### Production conditions (checked in order)

| Condition | Failure result |
|-----------|----------------|
| Chain is synced (or `enable-stale-production`) | `not_synced` |
| `get_slot_at_time(now) > 0` | `not_time_yet` |
| Scheduled validator is in our configured set | `not_my_turn` |
| Non-null signing key on chain | `not_my_turn` |
| Private key for signing key in memory | `no_private_key` |
| Network participation ≥ threshold (pre-HF12) | `low_participation` |
| `|scheduled_time - now| ≤ 500ms` | `lag` |
| No competing block at same height in fork_db | `fork_collision` |
| Last 21 blocks NOT all from our validators | `minority_fork` |

---

## Configuration Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `CHAIN_PENDING_TRANSACTION_EXECUTION_LIMIT` | 200 ms | Max time to re-apply pending transactions after block push |
| `CHAIN_BLOCK_GENERATION_POSTPONED_TX_LIMIT` | 5 | Max consecutive oversized transactions skipped during generation |
| `CHAIN_BLOCK_SIZE` | 65536 bytes | Hard block size limit |
| `maximum_block_size` | Dynamic (validator median) | Soft block size limit |
| `CHAIN_BLOCK_INTERVAL` | 3 s | Block production interval |

---

## Debug Log Prefixes

| Prefix | Meaning |
|--------|---------|
| `FORK-SWITCH-POP: popping head #H` | Normal fork switch — popping old-fork block |
| `FORK-RECOVER-POP: popping head #H` | Error recovery — reverting a failed fork switch |
| `POP_BLOCK: db_head=#X fork_db_head=#Y` | State before every `pop_block()` call |
| `Fork switch: new_head=#X branches.first=N branches.second=M` | Branches before fork switch; `M=0` means linear extension |

---

See also: [Fair-DPOS](./fair-dpos.md), [Fork Resolution](./fork-resolution.md), [Validator Node](../node/validator-node.md).
