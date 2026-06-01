# Fork Resolution

This page covers how VIZ Ledger detects, selects, and resolves competing forks — from the basic fork database to the HF12 vote-weighted collision resolver.

---

## Fork Database

The **fork database** (`fork_database`) is an in-memory tree of candidate chain tips. Every block received from P2P is inserted here before being applied to the chain state.

Key operations:
- `push_block(b)` — link `b` to its parent; if the parent is unknown, cache in `_unlinked_index`
- `_push_next(item)` — when a parent arrives, iteratively link all cached children
- `fetch_branch_from(a, b)` — walk both branches back to their common ancestor
- `set_max_size(n)` — prune oldest blocks from both linked and unlinked indices

### Duplicate detection

Before inserting, the fork DB checks whether a block with the same ID already exists. If so, it is silently ignored. This prevents redundant processing of blocks re-broadcast during P2P sync.

### Unlinked index

Blocks whose parent is not yet in the fork DB are stored in `_unlinked_index`. When the parent later arrives:
1. `_push_block(parent)` links the parent.
2. `_push_next(parent)` iterates `_unlinked_index` for children of the parent.
3. Children are moved to `_index` and recursively linked.
4. The fork DB head may jump multiple blocks in one call.

The gap threshold (100 blocks) prevents memory bloat: blocks more than 100 ahead of the database head with an unknown parent are silently rejected before reaching the fork DB.

---

## Fork Selection: Longest-Chain Rule

After inserting a block, the fork DB returns the new head. If the new head is higher than the database head and diverges from it, a **fork switch** is attempted.

**Pre-HF12:** Simple longest-chain rule — the fork with the highest block number wins.

---

## HF12: Vote-Weighted Fork Comparison

Starting at hardfork 12, when two competing forks exist at the same block height, `compare_fork_branches()` is used instead of simple longest-chain:

### Algorithm

1. **Fetch branches** via `fetch_branch_from(fork_a_tip, fork_b_tip)` to the common ancestor.
2. **Sum vote weight per validator** for each branch — only unique validator accounts are counted once per branch. The emergency validator account (`"committee"`) is excluded.
3. **Apply +10% bonus** to the longer chain.
4. **Return**: `+1` if branch A is stronger, `-1` if B is stronger, `0` if tied.

### Fork collision handling

When `compare_fork_branches()` is called from the validator plugin:
- If one fork is clearly stronger → produce on that fork.
- If tied or inconclusive → defer (increment `fork_collision_defer_count_`).
- After **21 consecutive deferrals** (one full validator round) → timeout: call `remove_blocks_by_number(height)` to clear stale competing blocks, then produce on the canonical chain.

| Condition | `peer_needs_sync_items_from_us` flag |
|-----------|-------------------------------------|
| Reply empty | `false` — our chain is empty |
| Reply = 1 item in synopsis | `false` — peer is caught up |
| Reply >1 item, `remaining == 0` | `false` — peer nearly caught up (switch to inventory) |
| Reply >1 item, `remaining > 0` | `true` — peer is far behind (stay in sync mode) |

---

## Fork Switch Process

When the node switches to a better fork:

```
1. fetch_branch_from(new_head, current_head)
   → branches.first  = [new_tip, ..., common_ancestor]
   → branches.second = [current_tip, ..., common_ancestor]

2. Linear extension check:
   branches.second.size() == 1 AND common_ancestor == head
   → Skip pop loop; apply branches.first directly.

3. Actual fork switch:
   for each block in branches.second (reverse):
       FORK-SWITCH-POP: pop_block()      ← save txs to _popped_tx
   for each block in branches.first (reverse):
       FORK-SWITCH-APPLY: apply_block()

4. On exception:
   for each block applied above:
       FORK-RECOVER-POP: pop_block()     ← undo partial apply
   Invalidate the failed fork.
   Re-raise the exception.
```

The **linear extension** distinction is critical in DLT mode where LIB == head: a pop loop would be infinite because undo sessions are already committed.

---

## Irreversible Block Determination

After each block application, `update_last_irreversible_block()` advances the Last Irreversible Block (LIB):

1. Collect the `last_supported_block_num` for each of the 21 scheduled validators.
2. Sort and take the `⌈21 × 25%⌉ = 5`th from the bottom (i.e., the value where 75% of validators are at or above it).
3. The resulting block number becomes the new LIB.

Once a block is LIB, it is written to `block_log` (or `dlt_block_log` in DLT mode) and its undo session is committed.

**LIB is capped at HEAD − 1 during emergency consensus mode** to prevent committing the undo session of the block being currently applied.

---

## Stale Fork Pruning

Two mechanisms prevent stale data from accumulating:

1. **`remove_blocks_by_number(num)`** — removes all blocks at a specific height. Called by the fork collision resolver after the 21-deferral timeout.
2. **`set_max_size(n)`** — prunes oldest blocks from both `_index` and `_unlinked_index` when the fork DB exceeds `n` entries.

---

## Minority Fork Guard

Before each block production, the validator plugin checks the last 21 blocks in the fork DB:

- If all 21 were produced by this node's own configured validators → the node is isolated on a minority fork.
- Action (`enable-stale-production = false`): call `resync_from_lib()` — pop to LIB, reset fork DB, re-initiate P2P sync, reconnect seed nodes.
- Action (`enable-stale-production = true`): log warning, continue producing.
- Emergency consensus active → skip check (all slots being "ours" is expected for an emergency master).

---

## Fork Collision Metrics (HF12)

HF12 added two fields to `dynamic_global_property_object` for on-chain monitoring:

| Field | Type | Description |
|-------|------|-------------|
| `fork_collision_count` | `uint32_t` | Cumulative count of fork collisions since genesis |
| `last_fork_collision_block_num` | `uint32_t` | Block number of the most recent collision |

Read via `get_dynamic_global_properties`.

---

## Fork DB Diagnostics

The fork DB exposes O(1) accessors for monitoring:

| Method | Returns |
|--------|---------|
| `linked_size()` | Number of blocks in the linked index |
| `unlinked_size()` | Number of blocks in the unlinked index |
| `linked_min_block_num()` | Lowest block number in linked index |
| `linked_max_block_num()` | Highest block number in linked index |
| `unlinked_min_block_num()` | Lowest block number in unlinked index |
| `unlinked_max_block_num()` | Highest block number in unlinked index |

The P2P stats task logs these every 5 minutes:

```
Block storage | dlt_log: [79174319..79274318] | dlt_resizes: 412 | fork_db: linked=18 unlinked=0
```

A growing `unlinked_size` that does not drain suggests a persistent gap in the received block stream (P2P connectivity issue or node on an isolated fork).

---

## Troubleshooting

| Symptom | Diagnosis |
|---------|-----------|
| `fork_collision` production result | Competing block at target height; wait for 21-deferral timeout or vote-weight resolution |
| `minority_fork` production result | Node is isolated; check P2P peers and seed connectivity |
| `unlinked_size` growing indefinitely | Parent blocks not arriving; check P2P connectivity |
| Repeated fork switches in logs | Network partition between two validator subsets; investigate connectivity between them |
| Head not advancing in DLT mode | Linear extension vs fork switch confusion; check `FORK-SWITCH-POP` logs |

---

See also: [Fair-DPOS](./fair-dpos.md), [Block Processing](./block-processing.md), [Emergency Consensus](./emergency-consensus.md).
