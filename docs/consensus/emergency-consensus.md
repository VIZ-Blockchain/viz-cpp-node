# Emergency Consensus Mode

Emergency consensus mode (introduced in HF12) activates automatically when the network stalls for 1 hour. A special "committee" validator takes over block production to maintain chain continuity until real validators restore their signing keys.

---

## Key Constants

| Constant | Value | Meaning |
|----------|-------|---------|
| `CHAIN_EMERGENCY_CONSENSUS_TIMEOUT_SEC` | 3600 s | Inactivity time before activation |
| `CHAIN_EMERGENCY_VALIDATOR_ACCOUNT` | `"committee"` | Emergency block producer account |
| `CHAIN_EMERGENCY_VALIDATOR_PUBLIC_KEY` | `VIZ75CR...` | Deterministic emergency signing key |
| `CHAIN_EMERGENCY_EXIT_NORMAL_BLOCKS` | 21 | Consecutive real-validator blocks for exit |
| `CHAIN_IRREVERSIBLE_THRESHOLD` | 75% | Fraction of schedule slots required to exit |
| `CHAIN_MAX_VALIDATORS` | 21 | Maximum validator slots |

### State fields in `dynamic_global_property_object`

| Field | Default | Meaning |
|-------|---------|---------|
| `emergency_consensus_active` | `false` | Emergency mode is active |
| `emergency_consensus_start_block` | `0` | Block number at activation |

---

## Activation

`update_global_dynamic_data()` runs on every applied block and checks:

1. **HF12 gate** — skip if the hardfork has not activated.
2. **Already active** — skip if emergency mode is already on.
3. **LIB block available in block log** — skip if the LIB block is not present in the block log (DLT nodes after snapshot restore have an empty block log; a missing LIB block would compute millions of stale seconds and trigger a false activation deadlock).
4. **Timeout** — compute `seconds_since_lib = current_block.timestamp − lib_block.timestamp`. If `< 3600`, skip.

All checks use only block-embedded timestamps — no wall clock, no skip flags. This guarantees identical deterministic activation on every node, including replays.

### Activation sequence

When the timeout threshold is crossed:

1. Set `dgp.emergency_consensus_active = true` and `dgp.emergency_consensus_start_block = block_num`.
2. Create or update the "committee" validator object:
   - `signing_key = CHAIN_EMERGENCY_VALIDATOR_PUBLIC_KEY`
   - `props = current_median_props`
   - Hardfork votes set to the currently applied version (neutral voter).
3. Disable ALL real validators: set `signing_key = zero`, reset `penalty_percent = 0`, `current_run = 0`.
4. Remove all `witness_penalty_expire` objects.
5. Override the validator schedule: all `CHAIN_MAX_VALIDATORS` slots → "committee".
6. Notify fork DB: `_fork_db.set_emergency_mode(true)` (enables deterministic hash tie-breaking).
7. Log: `"EMERGENCY CONSENSUS MODE activated at block #N. No blocks for X seconds since LIB Y."`

---

## Emergency Operation

### Block production — master vs. follower

Nodes with `emergency-private-key` configured in `config.ini` are **emergency masters**; all other nodes are **followers**.

```ini
# Emergency master node only
emergency-private-key = 5Jzzz...   # CHAIN_EMERGENCY_VALIDATOR_ACCOUNT private key
```

| Role | DLT sync check | Minority fork check | Production |
|------|----------------|---------------------|------------|
| Master | Bypassed (would deadlock) | Skipped (all blocks being "ours" is expected) | Produces blocks for all slots |
| Follower | Normal | 21-block check (1 full round) | Standard validator production for own scheduled slots |

### Fork DB deterministic tie-breaking

During emergency, multiple masters (geographic redundancy) may produce competing blocks at the same height with the same key. Arrival order varies by P2P topology.

`fork_database::_push_block()` resolves ties:
```
item->num == _head->num AND _emergency_consensus_active:
    item->id < _head->id  →  _head = item  (lower block hash wins)
    else                  →  keep current _head
```

All nodes converge on the same chain tip regardless of which block they saw first.

### LIB advancement

`update_last_irreversible_block()` computes LIB normally but **caps it at HEAD − 1** during emergency. Without the cap, since all 21 slots are "committee" and `committee.last_supported_block_num == HEAD`, the `nth_element` computation returns HEAD — which would commit the current block's undo session mid-apply, causing irreversible corruption.

After 3 committee blocks (`CHAIN_IRREVERSIBLE_SUPPORT_MIN_RUN`), LIB advances every block, keeping the fork DB window small.

### Hybrid validator schedule

`update_witness_schedule()` builds a hybrid schedule each round:

- Slots where the real validator's `signing_key` is non-zero: keep the real validator.
- Empty or zero-key slots: fill with "committee".

This allows real validators to return incrementally. Each time a real validator restores their signing key (via `validator_update_operation`), the next schedule rebuild includes them.

Committee is excluded from hardfork version tallying and median chain property computation (it copies the current median, so counting it per-slot would distort the median).

---

## Deactivation

Every schedule rebuild checks the exit condition:

```
real_witness_slots >= CHAIN_MAX_VALIDATORS × 75%
```

With 21 validators: `21 × 0.75 = 15.75 → 15` real validator slots required.

When the condition is met:
1. `dgp.emergency_consensus_active = false`.
2. `_fork_db.set_emergency_mode(false)`.
3. Log: `"EMERGENCY CONSENSUS MODE deactivated at block #N. R real validators active."`.

The network resumes normal operation on the next schedule cycle.

---

## Startup Recovery

`database::open()` checks the validator schedule after replay. If any slot is empty (string `""`), the emergency was in progress when the node shut down:

1. If `emergency_consensus_active` is not already set → set it and set fork DB emergency flag.
2. Fill all slots with "committee".
3. Log: `"schedule repaired, all N slots set to committee"`.

This ensures the schedule is always consistent after an unclean shutdown during emergency mode.

---

## Validator Guard Integration

The `validator_guard` plugin continues to operate during emergency and is in fact more critical:

- Real validators are disabled (signing key set to null) during activation.
- The validator guard automatically broadcasts `validator_update_operation` to restore each validator's signing key once the null key is detected on-chain.
- The `enable-stale-production` guard in the validator guard **does not block** key restoration during emergency mode ("Emergency consensus handles its own recovery and key restoration may still be needed").
- Once 15 validators restore their keys, the exit condition triggers.

See [Validator Guard](../node/validator-guard.md).

---

## P2P Interaction Guards

Several P2P safeguards are emergency-aware:

| Guard | Behavior during emergency |
|-------|--------------------------|
| `resync_from_lib()` | **Skipped entirely** — popping blocks near LIB during emergency would crash |
| `stale_sync_check_task()` | If master's head is advancing → reset timer, skip recovery; if follower head is stuck → allow recovery |
| `handle_block()` (DLT, sync mode, gap 0–2) | Treated as normal (not sync) to prevent production loop disruption |
| Snapshot stalled sync detection | Same logic as stale sync check |

The `resync_from_lib()` guard is the most critical: during emergency, LIB is close to HEAD. Popping blocks back to LIB and resetting the fork DB would cause peer blocks from the real network to link to the re-seeded LIB, trigger a fork switch, pop below the committed LIB, and either crash or corrupt state.

---

## Block Validation — Relaxed Slot Mapping

`verify_signing_witness()` normally asserts that the block producer matches the scheduled validator exactly. During emergency:

```
If block.validator != scheduled_witness:
    dlog("Emergency mode: accepting block from BW at slot scheduled for SW")
    → accept anyway (signature is still validated against block.validator's signing_key)
```

This allows emergency masters to produce blocks even if a few slots are still assigned to real validators in the pending schedule.

---

## Snapshot Compatibility

Emergency state fields are included in snapshots with forward-compatible defaults:

- `emergency_consensus_active` missing in snapshot → defaults to `false`.
- `emergency_consensus_start_block` missing → defaults to `0`.

Snapshots created during an active emergency preserve the state correctly; snapshots created before HF12 import as non-emergency.

---

## Guard Summary

| # | Location | Guard |
|---|----------|-------|
| 1 | `update_global_dynamic_data` | Only activate if HF12 + not already active + LIB block available |
| 2 | `update_witness_schedule` | Hybrid override + exit check at ≥75% real validators |
| 3 | `update_last_irreversible_block` | Cap LIB at HEAD − 1 during emergency |
| 4 | `verify_signing_witness` | Relax slot-to-validator mapping |
| 5 | `fork_db._push_block` | Deterministic hash tie-breaking |
| 6 | `maybe_produce_block` (master) | Bypass sync, stale, participation; skip minority fork |
| 7 | `maybe_produce_block` (follower) | Must sync first; 21-block isolation check |
| 8 | `resync_from_lib` | **Skip entirely** during emergency |
| 9 | `stale_sync_check_task` | Skip if master's head advancing; allow if follower stuck |
| 10 | `handle_block` | Near-caught-up blocks treated as normal in DLT emergency |
| 11 | `database::open` | Startup schedule repair |
| 12 | `validator_guard` | Do not suppress key restoration during emergency |
| 13 | `snapshot import` | Forward-compatible field handling |
| 14 | `update_witness_schedule` | Exclude committee from hardfork version tallying |
| 15 | `update_median_witness_props` | Exclude committee from median computation |

---

## Key Invariants

1. **Deterministic activation** — uses only block-embedded timestamps; identical on every node and every replay.
2. **DLT snapshot safety** — skips activation if LIB block is absent from block log.
3. **Emergency fork immutability** — `resync_from_lib()` refuses to execute during emergency.
4. **Master/follower distinction** — only nodes with `--emergency-private-key` are masters.
5. **Fork DB convergence** — deterministic hash tie-breaking ensures all nodes pick the same block.
6. **LIB safety** — capped at HEAD − 1 to preserve undo protection.
7. **Neutral committee voting** — committee votes for current applied hardfork version, copies median props.

---

See also: [Fair-DPOS](./fair-dpos.md), [Fork Resolution](./fork-resolution.md), [Validator Node](../node/validator-node.md), [Validator Guard](../node/validator-guard.md).
