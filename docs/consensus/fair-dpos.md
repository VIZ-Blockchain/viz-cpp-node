# Fair-DPOS Consensus

VIZ Ledger uses **Fair Delegated Proof of Stake (Fair-DPOS)**, an extension of the classic DPoS algorithm that adds participation enforcement and miss penalties to prevent validators from collecting rewards without actually producing blocks.

---

## How Classic DPoS Works

In standard DPoS:
- Token holders vote for validator accounts (weighted by their SHARES).
- The top-voted validators are scheduled to produce blocks in a round-robin schedule.
- Every 3 seconds one slot fires; the scheduled validator either produces a block or misses the slot.

VIZ runs with **21 active validators** per schedule round.

---

## The "Fair" Extension

In classic DPoS a validator can miss blocks indefinitely and still receive votes (and sometimes rewards). Fair-DPOS adds:

1. **Participation tracking** — a 128-bit bitmask tracks the last 128 slots. Each slot is marked 1 (produced) or 0 (missed).
2. **Participation threshold** — the node will not produce if fewer than `required-participation`% of recent slots were filled by any validator (default 33%). This guards against a minority-fork scenario.
3. **Miss penalties** — validators that miss blocks accumulate a miss counter. On each hardfork evaluation the worst performers can be removed from the active set.
4. **Reward sharing** (HF13) — validator block rewards are partially redistributed to their voters, aligning delegator incentives with validator performance.

---

## Validator Schedule

### Building the schedule

Every `CHAIN_WITNESS_SCHEDULE_BLOCK_NUM` blocks (21) the chain recomputes the active validator set:

1. Take the top-21 validators by total vote weight (SHARES delegated to them).
2. Add the **time-share validators** — a rotating slot for lower-ranked validators to participate occasionally, preventing complete concentration.
3. Shuffle the resulting 21 slots using the head block ID as entropy seed (deterministic shuffle = same result on all nodes).

The resulting ordered list becomes the **current schedule**. Each position corresponds to a 3-second slot.

### Slot assignment

Given a wall-clock time `T`:

```
slot_num  = (T - genesis_time) / CHAIN_BLOCK_INTERVAL
scheduled = schedule[slot_num % num_scheduled_validators]
```

Block timestamps are always the **deterministic slot time**, never the raw clock:
```
block_time = genesis_time + slot_num × 3s
```

### Missed slots

When a validator misses their slot, `update_global_dynamic_data()` increments `current_aslot` and marks the slot as missed in the participation bitmask. Other validators do not fill in for a missed slot — the 3-second rhythm continues on the next slot regardless.

---

## Participation Rate

The participation rate is:

```
participation = popcount(recent_slots_filled) / 128
```

where `recent_slots_filled` is the 128-bit sliding window of slot outcomes.

**Validator production is blocked when participation drops below `required-participation`** (default 33%). This prevents a node on a minority fork from continuing to produce blocks when most of the network is unreachable.

Config:
```ini
required-participation = 33   # minimum %, 0–99
```

---

## Last Irreversible Block (LIB)

A block is irreversible once more than 2/3 of active validators have built on top of it. The chain tracks this in `last_irreversible_block_num`.

```
irreversibility_threshold = ceil(num_scheduled_validators * 2 / 3)
```

With 21 validators: `ceil(21 × 2/3) = 14` confirmations. Once 14 validators have produced blocks descended from block N, block N becomes LIB.

LIB advancement is **skipped during emergency consensus mode** (see [Emergency Consensus](./emergency-consensus.md)).

### Fast LIB via Block Post-Validation

The classic LIB path requires 14 validators to **produce** blocks above the target — at 3 s per slot that takes ~63 seconds. Block post-validation provides a fast path by replacing implicit block-production confirmation with explicit out-of-band signature messages, reducing finality to **~4 seconds**.

**Flow:**

1. After `apply_block(N)`, the chain stores a `validator_confirmation_object` for block N.
2. Each scheduled validator with a loaded signing key signs `chain_id + block_id` and broadcasts a `block_post_validation_message` (P2P message type **6009**).
3. Receiving peers verify the signature against the on-chain `validator.signing_key`, then mark that validator as confirmed for the block.
4. `check_block_post_validation_chain()` walks from LIB+1 forward — if ≥14 of 21 scheduled validators have confirmed a block, LIB advances to that block and the process repeats.

The classic DPOS path (~63 s) remains active as a **fallback** if confirmation messages are lost.

**Constraints:**
- Only validators in the **current shuffled schedule** count toward the 2/3 threshold.
- Post-validation LIB is **disabled during emergency consensus** to avoid deadlocking recovery.
- The confirmation list is capped at 20 entries (`CHAIN_MAX_BLOCK_POST_VALIDATION_COUNT`).

For the full technical details including wire format and timing breakdown, see [Block Processing → Block Post-Validation](./block-processing.md#block-post-validation-fast-lib-finality).

---

## Hardfork Voting

Validators participate in hardfork activation by setting their reported `hardfork_version_vote` via `validator_update_operation`. Hardfork N activates when:

1. A supermajority (>80%) of the current validator set has signalled support.
2. The hardfork's scheduled activation timestamp has passed.

Both conditions must be true. This allows network operators to block unwanted hardforks by withholding votes even after the scheduled time.

---

## Minority Fork Guard

If the last 21 consecutive blocks are all produced by validators that belong to this node's configured validator set, the validator plugin concludes the node is isolated and automatically rolls back to LIB. This is the **minority fork guard**.

The check is bypassed when:
- `enable-stale-production = true` (development/testnet)
- Emergency consensus mode is active

---

## Emergency Consensus Mode

If no block has been produced for `CHAIN_EMERGENCY_CONSENSUS_TIMEOUT_SEC` (default 1 hour), the chain switches to **emergency mode**:

- All 21 validator slots are assigned to `CHAIN_EMERGENCY_VALIDATOR_ACCOUNT` ("committee").
- The emergency validator signs blocks using `CHAIN_EMERGENCY_VALIDATOR_PUBLIC_KEY`.
- All validator penalties are reset; shut-down validators are re-enabled.
- LIB advancement is paused during the emergency period.
- Emergency mode exits after `CHAIN_EMERGENCY_EXIT_NORMAL_BLOCKS` (21) consecutive normal blocks advance LIB past the emergency start block.

See [Emergency Consensus](./emergency-consensus.md) for full details.

---

## HF12: Vote-Weighted Fork Comparison

Starting at hardfork 12, when two competing forks exist at the same block height, the chain uses **vote-weighted fork comparison** instead of simple longest-chain:

1. For each fork branch, sum the total SHARES delegated to each unique validator that produced a block on that branch.
2. Apply a **+10% bonus** to the longer chain to break ties in favor of production continuity.
3. The fork with higher total vote weight wins.
4. If still tied, the current fork is maintained until the two-level fork collision resolver's 21-deferral timeout fires.

See [Fork Resolution](./fork-resolution.md) for the full fork collision algorithm.

---

## Configuration Summary

| Setting | Default | Description |
|---------|---------|-------------|
| `required-participation` | `33` (33%) | Minimum participation to produce blocks |
| `enable-stale-production` | `false` | Bypass participation check (testnet only) |
| `emergency-private-key` | — | Optional emergency consensus signing key |
| Active validators | 21 | Hardcoded in `CHAIN_MAX_VALIDATORS` |
| Block interval | 3 s | `CHAIN_BLOCK_INTERVAL` |
| LIB threshold | ⌈21 × 2/3⌉ = 14 | Blocks confirming irreversibility |
| Emergency timeout | 3600 s | `CHAIN_EMERGENCY_CONSENSUS_TIMEOUT_SEC` |

---

## Key Source Locations

| Component | File |
|-----------|------|
| Validator schedule construction | `libraries/chain/database.cpp` — `update_witness_schedule()` |
| Participation bitmask update | `libraries/chain/database.cpp` — `update_global_dynamic_data()` |
| LIB advancement (classic) | `libraries/chain/database.cpp` — `update_last_irreversible_block()` |
| LIB advancement (fast) | `libraries/chain/database.cpp` — `check_block_post_validation_chain()`, `apply_block_post_validation()` |
| Post-validation creation | `libraries/chain/database.cpp` — `create_block_post_validation()` |
| Post-validation broadcast | `plugins/validator/validator.cpp` — block post-validation timer tick |
| Hardfork voting | `libraries/chain/database.cpp` — `process_hardforks()` |
| Production loop | `plugins/validator/validator.cpp` — `maybe_produce_block()` |
| Emergency mode activation | `libraries/chain/database.cpp` — `check_emergency_consensus()` |
| HF12 fork comparison | `libraries/chain/database.cpp` — `compare_fork_branches()` |

---

See also: [Block Processing](./block-processing.md), [Fork Resolution](./fork-resolution.md), [Emergency Consensus](./emergency-consensus.md), [Validator Node](../node/validator-node.md).
