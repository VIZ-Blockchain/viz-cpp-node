
# Fork Collision Resolution Fix

## Problem
When a witness node detects a fork collision (competing block at same height in fork_db), it defers block production forever. The competing block from the dead fork is never removed from fork_db, so `maybe_produce_block()` keeps returning `fork_collision` every 250ms. Meanwhile, blocks from the longer/healthier chain can't be applied because the head is stuck, creating a growing gap that eventually makes fork switching impossible.

## Root Cause Analysis

### How vote-weighted fork comparison works (existing HF12 code)

`compute_branch_weight()` in `database.cpp:_push_block()` (lines 1300-1314):
1. `fetch_branch_from(tip_a, tip_b)` walks both chains back to their **common ancestor**
2. Returns two vectors: `branches.first` = all blocks from fork A's tip to common ancestor, `branches.second` = same for fork B
3. For each branch, iterates through **ALL blocks** in that branch
4. For each block, gets the `witness` name (who produced that block)
5. Looks up `witness_object.votes` from current DB state — this is **on-chain stake vote count** (how many VIZ tokens voted for that witness), NOT the scheduled slot
6. Uses `flat_set` to count each witness **only once** (even if it produced multiple blocks)
7. Skips `CHAIN_EMERGENCY_WITNESS_ACCOUNT`
8. The branch with higher total vote weight wins; ties broken by longer chain
9. **NEW: Longer chain gets +10% bonus on its vote weight** — each block produced is a consensus "vote" by the producing witness. Witnesses on the longer chain didn't defer and kept producing by consensus rules, which is strong evidence of network support.

**Answer: it sums `wit_obj.votes` (on-chain stake) for ALL unique witnesses that produced blocks in the divergent portion of each fork, not just the top block. The longer chain gets a +10% bonus on its total weight.**

### Where the witness plugin goes wrong
In `witness.cpp:maybe_produce_block()` (lines 549-577), the fork collision check does NOT use vote-weight comparison:
- It only checks "does any competing block exist at `head_block_num+1` with a different parent?"
- If yes -> blindly defers, no evaluation of which fork is better
- It never asks: "which fork has more vote weight? Should I produce on my fork or switch?"
- This creates a deadlock: the witness waits for fork resolution, but fork resolution only happens via `_push_block()` which can't run because the head is stuck

### The deadlock sequence
1. Head at 79402010, competing block at 79402011 from dead fork in fork_db
2. Witness scheduled at 79402011 -> sees competing block -> defers
3. P2P blocks 79402012+ arrive but can't be pushed (gap, parent unknown in fork_db)
4. `_push_block()` never gets a chance to do vote-weight comparison
5. Witness keeps deferring every 250ms -> permanently stuck
6. Gap grows until fork_db window (2400 blocks) is exceeded -> no recovery possible

### Critical constraint: `compute_branch_weight` CANNOT solve the stuck scenario
In your scenario (head at 79402010, network at 79404861+), the longer chain's blocks are NOT in fork_db — they were rejected because the parent was unknown (gap too large). So `fetch_branch_from()` cannot compute the branch for the longer chain. The existing vote-weight comparison **requires both chain tips to exist in fork_db**.

This means we need TWO levels of fix:
- **Level 1 (immediate)**: When fork_db has competing blocks at `head+1`, use vote-weight comparison to decide whether to produce or switch. This handles the case where BOTH forks are in fork_db.
- **Level 2 (stuck recovery)**: When the witness has been deferring and head is stuck (not advancing), bypass the collision check entirely — the competing block is from a dead fork that clearly lost (the network moved on without it). Produce on our fork.

## Fix Strategy: Two-Level Fork Decision in Witness Plugin

### Level 1: Vote-weighted comparison (when both forks are in fork_db)
When a competing block at `head+1` exists in fork_db:
1. **Evaluate fork weights** using `compute_branch_weight()` logic (same as `_push_block()`)
2. **If our fork has more vote weight** -> produce on our fork (the competing block is from a losing fork)
3. **If the competing fork has more vote weight** -> switch to it first, then produce on it
4. **If tied** -> defer briefly (1-2 slots)

### Level 2: Stuck-head timeout (when fork_db comparison is impossible)
When the witness has been deferring for N consecutive slots and head hasn't advanced:
1. The competing block is clearly from a dead fork (network moved on)
2. Remove the competing block(s) from fork_db
3. Produce on our fork
4. This handles the case where the longer chain's blocks aren't in fork_db

### Level 3: Prune dead-fork entries on block apply (defensive)
When `_push_block()` successfully applies a block, remove competing blocks at that height from fork_db that are from dead forks

## Detailed Changes

### 1. `libraries/chain/include/graphene/chain/database.hpp` — Expose fork comparison as public method

```cpp
/// Compare two fork branches by vote weight (HF12 logic).
/// Sums wit_obj.votes (on-chain stake) for all unique witnesses in each branch,
/// from the tip back to the common ancestor.
/// The longer chain gets a +10% bonus on its total weight (reflects that more
/// witnesses kept producing on it by consensus rules without deferring).
/// Returns: >0 if branch_a is heavier, <0 if branch_b is heavier, 0 if tied
/// Returns 0 if either tip is not in fork_db (cannot compare)
int compare_fork_branches(const block_id_type& branch_a_tip, const block_id_type& branch_b_tip) const;
```

### 2. `libraries/chain/database.cpp` — Implement `compare_fork_branches()`

Extract `compute_branch_weight` lambda from lines 1300-1314 into the new public method.
Add +10% bonus to the longer chain's weight.
Wrap in try/catch to return 0 when `fetch_branch_from()` fails (one tip not in fork_db).
Refactor `_push_block()` to call `compare_fork_branches()` instead of inline code.

```cpp
int database::compare_fork_branches(const block_id_type& branch_a_tip, const block_id_type& branch_b_tip) const {
    try {
        if (!_fork_db.is_known_block(branch_a_tip) || !_fork_db.is_known_block(branch_b_tip))
            return 0;  // Cannot compare — one or both tips not in fork_db

        auto branches = _fork_db.fetch_branch_from(branch_a_tip, branch_b_tip);

        auto compute_branch_weight = [&](const fork_database::branch_type& branch) -> share_type {
            flat_set<account_name_type> seen_witnesses;
            share_type total_weight = 0;
            for (const auto& item : branch) {
                const auto& wit_name = item->data.witness;
                if (wit_name == CHAIN_EMERGENCY_WITNESS_ACCOUNT) continue;
                if (seen_witnesses.insert(wit_name).second) {
                    try {
                        const auto& wit_obj = get_witness(wit_name);
                        total_weight += wit_obj.votes;
                    } catch (...) {}
                }
            }
            return total_weight;
        };

        share_type weight_a = compute_branch_weight(branches.first);
        share_type weight_b = compute_branch_weight(branches.second);

        // Longer chain gets +10% bonus on its vote weight.
        // Each block produced is a consensus "vote" — witnesses on the longer
        // chain didn't defer and kept producing by consensus rules.
        // This reflects the stronger network support signal.
        auto a_num = block_header::num_from_id(branch_a_tip);
        auto b_num = block_header::num_from_id(branch_b_tip);
        if (a_num > b_num) {
            weight_a = weight_a + weight_a / 10;  // +10%
        } else if (b_num > a_num) {
            weight_b = weight_b + weight_b / 10;  // +10%
        }

        if (weight_a > weight_b) return 1;   // branch_a is heavier
        if (weight_b > weight_a) return -1;  // branch_b is heavier
        return 0;  // tied
    } catch (...) {
        return 0;  // Cannot compare
    }
}
```

### 3. `libraries/chain/include/graphene/chain/fork_database.hpp` — Add `remove_blocks_by_number()`

```cpp
void remove_blocks_by_number(uint32_t num);
```

### 4. `libraries/chain/fork_database.cpp` — Implement `remove_blocks_by_number()`

```cpp
void fork_database::remove_blocks_by_number(uint32_t num) {
    auto blocks = fetch_block_by_number(num);
    for (const auto& b : blocks) {
        _index.get<block_id>().erase(b->id);
    }
}
```

### 5. `libraries/chain/database.cpp` — Prune dead-fork blocks after apply

In `_push_block()`, after successfully applying a block that extends the current chain (no fork switch, around line 1417), add:

```cpp
// Prune stale competing blocks from dead forks at this height
auto competing = _fork_db.fetch_block_by_number(new_block.block_num());
for (const auto& cb : competing) {
    if (cb->id != new_head->id && cb->data.previous != head_block_id()) {
        wlog("Pruning stale competing block ${id} at height ${n} from fork_db (dead fork)",
             ("id", cb->id)("n", new_block.block_num()));
        _fork_db.remove(cb->id);
    }
}
```

### 6. `plugins/witness/include/graphene/plugins/witness/witness.hpp` — Add config

No header changes needed; the timeout is internal to `impl`.

### 7. `plugins/witness/witness.cpp` — Two-level fork decision

**Add to impl struct:**
```cpp
std::atomic<uint32_t> fork_collision_defer_count_{0};
uint32_t _fork_collision_timeout_blocks = 21;  // safety timeout: one full witness round (21 blocks = 63s)
fc::time_point _fork_collision_start_time;    // when we first started deferring
uint32_t _fork_collision_head_num = 0;        // head_block_num when collision started
```

**Add CLI option:** `--fork-collision-timeout-blocks` (default: 21, i.e. one full witness schedule round = 63 seconds. After a full round, all scheduled witnesses have produced on the longer chain, confirming it's canonical.)

**Replace the fork collision block (lines 545-578) with two-level logic:**

```cpp
// Check if a competing block already exists in the fork database for this block height.
{
    auto existing_blocks = db.get_fork_db().fetch_block_by_number(db.head_block_num() + 1);
    if (existing_blocks.size() > 0) {
        bool has_competing_block = false;
        item_ptr competing_block;

        if (dgp.emergency_consensus_active) {
            has_competing_block = true;
            competing_block = existing_blocks[0];
        } else {
            for (const auto &eb : existing_blocks) {
                if (eb->data.witness != scheduled_witness &&
                    eb->data.previous != db.head_block_id()) {
                    has_competing_block = true;
                    competing_block = eb;
                    break;
                }
            }
        }

        if (has_competing_block && competing_block) {
            fork_collision_defer_count_++;

            // LEVEL 2: Stuck-head timeout
            // If we've been deferring and the head hasn't advanced, the competing
            // block is from a dead fork. The network has moved on without it.
            // After 21 consecutive deferrals (one full witness round = 63s),
            // we can be sure the longer chain had all scheduled witnesses
            // produce on it — confirming it's the canonical chain.
            if (fork_collision_defer_count_ > _fork_collision_timeout_blocks) {
                wlog("Fork collision timeout exceeded (${n} deferrals, head stuck at ${h}). "
                     "Removing dead-fork competing block and producing on our chain.",
                     ("n", fork_collision_defer_count_.load())("h", db.head_block_num()));
                db.get_fork_db().remove_blocks_by_number(db.head_block_num() + 1);
                // Fall through to produce block
            }
            // LEVEL 1: Vote-weighted comparison (when both forks are in fork_db)
            else if (db.has_hardfork(CHAIN_HARDFORK_12)) {
                int weight_cmp = db.compare_fork_branches(
                    competing_block->id, db.head_block_id());

                if (weight_cmp < 0) {
                    // Our fork has MORE vote weight -> produce on our fork
                    wlog("Our fork has more vote weight at height ${h}. "
                         "Producing despite competing block from weaker fork.",
                         ("h", db.head_block_num() + 1));
                    // Remove the losing competing block
                    db.get_fork_db().remove(competing_block->id);
                    // Fall through to produce block
                } else if (weight_cmp > 0) {
                    // Competing fork has MORE vote weight
                    // The competing branch is in fork_db and has more support.
                    // We should switch to it. The normal _push_block path will
                    // handle the switch when the competing block's children arrive.
                    // For now, defer to let the fork switch happen naturally.
                    capture("height", db.head_block_num() + 1)("scheduled_witness", scheduled_witness);
                    wlog("Competing fork at height ${h} has more vote weight. "
                         "Deferring to allow fork switch to stronger chain.",
                         ("h", db.head_block_num() + 1));
                    return block_production_condition::fork_collision;
                } else {
                    // Tied (or comparison impossible — one tip not in fork_db)
                    // Defer briefly, timeout will kick in
                    capture("height", db.head_block_num() + 1)("scheduled_witness", scheduled_witness);
                    wlog("Fork collision at height ${h} with tied/unknown vote weight. "
                         "Deferring (attempt ${n}/${max}).",
                         ("h", db.head_block_num() + 1)
                         ("n", fork_collision_defer_count_.load())
                         ("max", _fork_collision_timeout_blocks));
                    return block_production_condition::fork_collision;
                }
            }
            // Pre-HF12: original defer behavior with timeout
            else {
                capture("height", db.head_block_num() + 1)("scheduled_witness", scheduled_witness);
                return block_production_condition::fork_collision;
            }
        }
    }
}
```

**Reset `fork_collision_defer_count_`** to 0 in `block_production_loop()` when result is:
- `produced` — block was made, no collision
- `not_my_turn` / `not_time_yet` — normal skips, collision resolved
- Any result where `db.head_block_num()` has changed since last check

## File Change Summary

| File | Change |
|------|--------|
| `libraries/chain/include/graphene/chain/database.hpp` | Add `compare_fork_branches()` declaration |
| `libraries/chain/database.cpp` | Extract `compute_branch_weight` into `compare_fork_branches()`, prune dead fork blocks after apply |
| `libraries/chain/include/graphene/chain/fork_database.hpp` | Add `remove_blocks_by_number()` declaration |
| `libraries/chain/fork_database.cpp` | Implement `remove_blocks_by_number()` |
| `plugins/witness/witness.cpp` | Two-level fork decision: vote-weighted comparison + stuck-head timeout |

## Key Design Decisions

1. **`wit_obj.votes` = on-chain stake** — the vote-weight comparison sums the stake (VIZ tokens) that voted for each unique witness that produced blocks in the divergent portion of each fork. It is NOT the scheduled slot count.
2. **All blocks in the divergent branch count** — not just the top block. `fetch_branch_from()` walks from each tip back to the common ancestor, and all unique witnesses on each branch contribute their stake weight.
3. **Longer chain gets +10% bonus** — each block is a consensus "vote" by the producing witness. Witnesses on the longer chain didn't defer and kept producing by consensus rules. The +10% bonus ensures that a slightly shorter chain with slightly more stake cannot override a clearly longer chain that more witnesses kept building on.
4. **Our fork wins ties** — when vote weights (including bonus) are equal, we produce on our own chain (less disruptive)
5. **Competing fork with more weight -> defer for switch** — if the other chain has more support, we defer to allow `_push_block()` to naturally switch us when more blocks arrive
6. **Stuck-head timeout = 21 blocks (one full witness round)** — after 21 consecutive deferrals (63 seconds), all scheduled witnesses have had a chance to produce on the longer chain. We can be confident the longer chain is canonical. The competing block from the dead fork is removed and production resumes.
7. **Pruning on block apply** — when a block is applied, competing blocks from dead forks at that height are removed, preventing them from causing false collision detection in the future

## Verification
- Short micro-forks (1-2 blocks): vote-weight comparison resolves quickly, no stuck
- Our fork has more votes: witness produces immediately
- Competing fork has more votes: witness defers for natural switch via `_push_block()`
- Stuck head (your scenario): timeout after 21 slots (one full witness round) removes dead-fork block, witness produces
- Stale fork_db entries pruned on each block apply
- Emergency mode: existing behavior preserved (any competing block = defer, timeout applies)

---

## Implementation Status

All planned changes have been implemented. Deviations from the original plan are noted below.

| # | Planned Change | File | Status | Notes |
|---|---------------|------|--------|-------|
| 1 | Add `compare_fork_branches()` declaration | `database.hpp` | Done | Matches plan exactly |
| 2 | Implement `compare_fork_branches()` with +10% longer-chain bonus | `database.cpp` | Done | Matches plan; refactored `_push_block()` to use it instead of inline lambda |
| 3 | Refactor `_push_block()` HF12 fork-switch to use `compare_fork_branches()` | `database.cpp` | Done | Replaced 26-line inline lambda with 4-line call to `compare_fork_branches()` |
| 4 | Add `remove_blocks_by_number()` declaration | `fork_database.hpp` | Done | Matches plan exactly |
| 5 | Implement `remove_blocks_by_number()` | `fork_database.cpp` | Done | Matches plan exactly |
| 6 | Prune dead-fork blocks after block apply | `database.cpp` | Done | Slight deviation: uses `new_block.id()` instead of `new_head->id` (code is in the non-fork-switch path where `new_head` is out of scope) |
| 7 | Add fork collision state fields to `impl` struct | `witness.cpp` | Done | **Deviation**: removed `_fork_collision_head_num` (dead code) and `_fork_collision_start_time` (unused). Only `fork_collision_defer_count_` and `_fork_collision_timeout_blocks` remain |
| 8 | Add `--fork-collision-timeout-blocks` CLI option | `witness.cpp` | Done | Default 21, matches plan |
| 9 | Two-level fork decision in `maybe_produce_block()` | `witness.cpp` | Done | **Deviation**: Level 2 timeout runs BEFORE the HF12 check, so pre-HF12 nodes also benefit from the timeout. The plan had Level 2 inside the HF12 branch |
| 10 | Reset `fork_collision_defer_count_` in `block_production_loop()` | `witness.cpp` | Done | Reset on `produced`, `not_synced`, `not_my_turn`. **Not reset** on `not_time_yet` (timer hasn't fired yet, count should persist) |

### Bugs Found and Fixed During Review

| # | Severity | Bug | Fix |
|---|----------|-----|-----|
| 1 | Critical | Pre-HF12 path deferred forever (same as original bug) — Level 2 timeout was inside the `else if (has_hardfork(HF12))` branch | Moved Level 2 timeout check before the HF12 branch so all nodes benefit |
| 2 | Low | `_fork_collision_head_num` declared but never read or written | Removed the field |
| 3 | Info | `fork_collision_defer_count_` was planned as `std::atomic<uint32_t>` but implemented as `uint32_t` | Kept as `uint32_t` — all access is single-threaded (block production loop) |
