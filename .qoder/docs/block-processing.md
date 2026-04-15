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

## Fork Switch Flow

When a node switches to a different fork:

1. `pop_block()` removes the current head block
   - Transactions from the popped block are saved to `_popped_tx`
   - Source: [database.cpp:1223-1238](../../libraries/chain/database.cpp#L1223)

2. The new block is applied via `push_block()`

3. In `~pending_transactions_restorer()`:
   - `_popped_tx` transactions are processed first (from the old fork)
   - Then original `_pending_transactions` are processed
   - Duplicate transactions (already in the new chain) are silently skipped

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

## Configuration Constants

| Constant | Value | Purpose |
|---|---|---|
| `CHAIN_PENDING_TRANSACTION_EXECUTION_LIMIT` | 200ms | Max time to spend re-applying pending txs after block push |
| `CHAIN_BLOCK_GENERATION_POSTPONED_TX_LIMIT` | 5 | Max consecutive oversized txs to skip during block generation |
| `CHAIN_BLOCK_SIZE` | 65536 bytes | Hard limit on block size |
| `maximum_block_size` | Dynamic (witness median) | Soft limit on block size |
