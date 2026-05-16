# HF13: Validator Reward Sharing

## Overview

Hardfork 13 introduces **stakeholder reward sharing**: validators can configure a percentage of
their block reward to be accumulated and periodically distributed among the accounts that voted
for them (stakeholders).

**Key design principle**: Stakeholder rewards are accumulated and distributed in **TOKEN (VIZ)**
atomic units, not SHARES.  Stakeholders receive SHARES only after `create_vesting()` converts
their token share at the time of distribution.  The `witness_reward_operation` virtual op for the
validator itself continues to carry SHARES as before, but only for the validator's own portion.

---

## New Chain Property: `distribution_epoch_length`

| Property | Type | Default | Range |
|---|---|---|---|
| `distribution_epoch_length` | `uint32_t` | `28800` (1 day) | `[21, ~10.5M]` |

Validators vote on this parameter via `versioned_chain_properties_update_operation` using the new
`chain_properties_hf13` struct.  The median across all scheduled validators becomes the consensus
value.  At every block whose number is divisible by `distribution_epoch_length`, accumulated
stakeholder rewards are paid out.

---

## New Operation: `set_reward_sharing_operation`

Validators use this operation to set their **sharing rate** — the fraction of their block reward
that will be set aside for stakeholder distribution.

```json
{
  "type": "set_reward_sharing_operation",
  "value": {
    "owner": "alice",
    "sharing_rate": 5000
  }
}
```

| Field | Type | Description |
|---|---|---|
| `owner` | `account_name_type` | Validator account name |
| `sharing_rate` | `uint16_t` | Basis points: 0 = 0%, 10000 = 100% |

**Required authority**: active key of `owner`.
**Requires HF13**: the operation is rejected before HF13 activates.
**Validator must exist**: account must have a registered `witness_object`.

---

## Block Reward Split (process_funds)

With HF13 active, when a validator (`sharing_rate > 0`) produces a block:

```
witness_reward    = CHAIN_DIGITAL_ASSET_ISSUED_PER_BLOCK * inflation_witness_percent
                    (TOKEN, computed in process_funds)

stakeholder_token = witness_reward * sharing_rate / CHAIN_100_PERCENT
validator_token   = witness_reward - stakeholder_token

# Validator receives their SHARES immediately:
create_vesting(validator_account, validator_token)
→ emits witness_reward_operation(validator, validator_shares)

# Stakeholder pool accumulates TOKEN:
witness_object.pending_stakeholder_reward += stakeholder_token
```

When `sharing_rate == 0`, the full reward goes to the validator as usual (no change).

---

## Epoch Distribution (process_validator_epoch_distribution)

Called at the end of `_apply_block` on every block where
`head_block_num % distribution_epoch_length == 0`.

For each `witness_object` with `pending_stakeholder_reward > 0`:

```
epoch_start_block = head_block_num - epoch_length + 1

# Time-weighted contribution per stakeholder:
for each stakeholder:
    first_block              = max(stakeholder.vote_created_block, epoch_start_block)
    blocks_in_epoch          = head_block_num - first_block + 1
    weighted[stakeholder]    = stakeholder.witness_vote_weight() * blocks_in_epoch

total_weighted = Σ weighted[stakeholder]

# Distribution:
for each stakeholder:
    stakeholder_token = total_token * weighted[stakeholder] / total_weighted

    if stakeholder_token < CHAIN_MIN_STAKEHOLDER_REWARD_PAYOUT:
        skip (dust)
    else:
        stakeholder_shares = create_vesting(stakeholder_account, stakeholder_token)
        emit stakeholder_reward_operation(validator, stakeholder, stakeholder_shares)

dust_token = total_token - Σ(distributed stakeholder_token)
if dust_token > 0:
    dust_shares = create_vesting(validator_account, dust_token)
    emit witness_reward_operation(validator, dust_shares)

wit.pending_stakeholder_reward = 0
```

### Important notes

- **Vote weight** used: `stakeholder.witness_vote_weight()` — includes own vesting shares plus
  shares proxied by accounts that delegate their voting to this stakeholder.
- **Time-weighted distribution**: each stakeholder's weight is multiplied by the number of blocks
  they were actually voting for this validator within the current epoch (see `vote_created_block`
  on `witness_vote_object`).  A stakeholder who joined mid-epoch receives a proportionally smaller
  share.
- **Pre-HF13 votes** (`vote_created_block == 0`) are treated as having voted since epoch start
  (`first_block = epoch_start_block`), so they receive a full-epoch weight.  No penalty for
  existing stakeholders.
- **Min payout**: `CHAIN_MIN_STAKEHOLDER_REWARD_PAYOUT = 1` TOKEN atomic unit (0.001 VIZ).
  Stakeholders with a computed share below this threshold receive nothing; their portion is
  returned to the validator as dust (see design rationale below).
- **No-stakeholder case**: if a validator has no stakeholders, the entire accumulated pool is
  returned to the validator via `witness_reward_operation`.

#### Dust design rationale

The accumulated reward pool belongs to the validator — sharing it with stakeholders is entirely
the validator's voluntary decision, expressed via `sharing_rate`.  A stakeholder who fails to
accumulate a share above `CHAIN_MIN_STAKEHOLDER_REWARD_PAYOUT` (e.g. they hold very little stake,
or voted mid-epoch) has simply not earned a viable payout.  The responsibility for that outcome
lies with the stakeholder, not with the validator.

Consequently, unclaimed dust is **not burned**.  After all eligible stakeholders are paid, any
remainder (dust from integer rounding, plus skipped sub-threshold shares) is transferred back to
the validator via `witness_reward_operation`.  The validator retains what their stakeholders
collectively failed to claim — no TOKEN leaves the system, and the validator is not penalised for
stakeholders with negligible weight.

---

## Mid-Epoch Sharing Rate Change

If a validator calls `set_reward_sharing_operation` while an epoch is in progress, the new rate
takes effect **immediately on the next block**.  The accumulated `pending_stakeholder_reward` is
NOT retroactively recalculated.

Concretely, if an epoch spans N blocks and the rate changes after block K:

```
pending_stakeholder_reward =
    Σ(block 1..K)     reward_i * old_rate / CHAIN_100_PERCENT
  + Σ(block K+1..N)   reward_i * new_rate / CHAIN_100_PERCENT
```

The pool that stakeholders receive at epoch end is thus a weighted mix of both rates.  This is the
intended behaviour: the validator controls their own split on a per-block basis, and stakeholders
observe the change immediately on-chain via the `sharing_rate` field of the `witness_object`.

---

## Flash-Voter Protection

Flash-voting (voting just before epoch end to capture the full accumulated pool) is mitigated by
the **time-weighted distribution** described above.

### How it works

`witness_vote_object` stores `vote_created_block` — the block number when the vote was cast.  At
epoch end, each stakeholder's effective weight is scaled by:

```
blocks_in_epoch = head_block_num - max(vote_created_block, epoch_start_block) + 1
```

A flash voter who votes in the last `k` blocks of a `N`-block epoch receives only `k/N` of their
stake-proportional share.  For a 1-day epoch (28800 blocks), voting in the last block yields
`1/28800 ≈ 0.003%` of their proportional share — economically insignificant.

### Residual risk

The protection covers **new votes** only.  If a stakeholder already held a vote from a previous
epoch, they receive a full-epoch weight regardless of their stake changes within the epoch.  This
is acceptable: they were genuine stakeholders.

Vote removal before epoch end results in the stakeholder not appearing in `witness_vote_index` at
distribution time, so they receive nothing — no exploit in this direction.

---

## New Virtual Operation: `stakeholder_reward_operation`

Emitted once per stakeholder per distribution epoch when they receive a non-dust reward.

| Field | Type | Description |
|---|---|---|
| `validator` | `account_name_type` | Validator that produced the accumulated rewards |
| `stakeholder` | `account_name_type` | Stakeholder account receiving the reward |
| `shares` | `asset` | SHARES credited to the stakeholder |

---

## Token Accounting

```
Before HF13:
  current_supply += witness_reward (TOKEN)              [issued]
  total_vesting_fund += witness_reward (TOKEN)           [via create_vesting]
  validator.vesting_shares += validator_shares           [via create_vesting]

After HF13 with sharing_rate > 0:
  current_supply += witness_reward (TOKEN)               [issued, same as before]
  total_vesting_fund += validator_token (TOKEN)          [via create_vesting — immediate]
  validator.vesting_shares += validator_shares           [via create_vesting — immediate]
  wit.pending_stakeholder_reward += stakeholder_token    [TOKEN, pending]

  ...at epoch end:
  total_vesting_fund += stakeholder_token (TOKEN)        [via create_vesting per stakeholder]
  stakeholder.vesting_shares += stakeholder_shares       [via create_vesting per stakeholder]
  wit.pending_stakeholder_reward = 0                     [cleared]
```

The floating `pending_stakeholder_reward` TOKEN is already counted in `current_supply`.  The
`total_vesting_fund` balance is updated when the TOKEN converts to SHARES via `create_vesting` at
distribution time.

---

## Configuration Example

```ini
# Set sharing rate to 30% (3000 basis points)
# Submit set_reward_sharing_operation via API or wallet

# Set epoch length to 5 days via versioned_chain_properties_update_operation:
# chain_properties_hf13.distribution_epoch_length = 144000  (5 * 28800 blocks)
```

---

## Deployment: Shared Memory Compatibility and Replay

### Why replay is required

HF13 adds three new fields to chainbase-managed objects:

| Object | New field | Default |
|---|---|---|
| `witness_object` | `sharing_rate` (`uint16_t`) | `0` |
| `witness_object` | `pending_stakeholder_reward` (`share_type`) | `0` |
| `witness_vote_object` | `vote_created_block` (`uint32_t`) | `0` |

Chainbase stores objects as raw binary in a memory-mapped file (`shared_memory.bin`).
Adding a field changes `sizeof()`, so the old shared memory is **binary-incompatible** with
the new binary.  If the new binary opens old shared memory, chainbase reads objects at
incorrect offsets, producing corrupt data and ultimately a `database_revision_exception`
(revision counter in shared memory will not match `head_block_num`).

**No apply_hardfork migration is needed**: the new fields default to `0`, which is the
correct pre-HF13 state.  Once the node replays or restores from snapshot, all objects
are initialised correctly.

---

### Automatic recovery (existing mechanism)

The chain plugin already handles this case.  On `database_revision_exception` at startup,
it executes one of two recovery paths depending on config:

```
Startup
  └─ db.open(shared_memory)
       └─ EXCEPTION: database_revision_exception
            │
            ├─ [auto-recover-from-snapshot = true AND snapshot exists]
            │    wipe shared_memory
            │    → import latest snapshot  (state restored to block N)
            │    → replay dlt_block_log blocks N+1..dlt_head
            │      (bridges gap between snapshot and last known block)
            │    → continue P2P sync for remaining blocks
            │
            └─ [replay-if-corrupted = true, no snapshot]
                 → replay from block_log (slow, may take hours)
```

The dlt_block_log replay step (handled in `snapshot_plugin.cpp:2104–2121`) is automatic:
after the snapshot is imported, the node calls `db.reindex_from_dlt(snapshot_head + 1)`
to re-apply any local blocks that are newer than the snapshot.  This minimises P2P sync
work after upgrade.

**Recommended config for production validators:**

```ini
# config.ini (or config_witness.ini)
replay-if-corrupted = true
replay-from-snapshot = true
snapshot-auto-latest = true
snapshot-dir = /path/to/snapshots
```

With this config, upgrading to the HF13 binary requires no manual intervention:
1. Stop the node.
2. Replace the binary.
3. Start the node — auto-recovery fires, wipes shared memory, loads the latest snapshot,
   replays `dlt_block_log`, then syncs the remaining blocks via P2P.

---

### Manual recovery procedure

If auto-recovery is not configured, or the recovery path fails, perform the following:

```bash
# 1. Stop vizd
systemctl stop vizd

# 2. Delete shared memory (forces clean open on next start)
rm -f /path/to/witness_node_data_dir/shared_memory.bin
rm -f /path/to/witness_node_data_dir/shared_memory.meta

# Option A — restore from snapshot + replay dlt_block_log:
./vizd --replay-from-snapshot /path/to/snapshot-block-XXXXXXXX.json \
       --data-dir /path/to/witness_node_data_dir

# Option B — full replay from block_log (slow):
./vizd --replay --data-dir /path/to/witness_node_data_dir
```

After Option A the node replays `dlt_block_log` automatically (same path as auto-recovery),
then syncs remaining blocks via P2P.  Typical recovery time with a recent snapshot: a few
minutes.  Full replay (Option B) may take several hours depending on blockchain height.

---

### How chainbase detects the mismatch

Chainbase does **not** compare `sizeof()` at open time — it opens the memory-mapped file
and begins reading.  The mismatch surfaces as a revision inconsistency:

```
database::open()
  chainbase::database::open(shared_mem_dir)   ← objects read at wrong offsets
  undo_all()                                   ← traverses corrupt undo state
  revision() != head_block_num()               ← counter mismatch detected here
  → throws database_revision_exception
```

Alternatively, `undo_all()` may trigger a `boost::interprocess::lock_exception` if the
process crashed while holding a shared-memory mutex (unrelated to HF13, but the same
recovery path applies).

**Implication**: deleting `shared_memory.bin` before starting is the safest option.  The
file is always rebuilt from snapshot or block_log; nothing of value is lost.

---

### Proactive schema version check (implemented in HF13)

Rather than waiting for a corrupt read to surface as a `database_revision_exception`,
the chain plugin now performs a **proactive schema version check** before calling
`db.open()`:

```
plugin_startup()
  │
  ├─ read <data_dir>/schema_version  (0 if file absent = pre-HF13 node)
  ├─ compare with CHAIN_SCHEMA_VERSION (compile-time constant, currently 13)
  │
  ├─ MISMATCH → wipe shared_memory.bin immediately (no corrupt read occurs)
  │             write new schema_version to disk
  │             → fall through to normal db.open()
  │                → revision=0 ≠ head_block_num → database_revision_exception
  │                → existing recovery: auto-snapshot or replay
  │
  └─ MATCH    → proceed normally
                 db.open() succeeds → write schema_version (confirm success)
```

**Key constant** (`config.hpp`):
```cpp
// Increment whenever a chainbase-managed object gains, loses, or resizes a field.
#define CHAIN_SCHEMA_VERSION  uint32_t(13)
```

**Key file**: `<data_dir>/schema_version` — a plain text file containing a single
`uint32_t`.  Absent file is treated as version `0` (pre-HF13).

This mechanism is completely transparent: nodes that don't have `schema_version`
(old deployments upgrading to HF13 for the first time) automatically get `stored=0`,
mismatch is detected, shared memory is wiped, and recovery proceeds.

See [hardfork-guide.md](hardfork-guide.md) for the rule: **increment
`CHAIN_SCHEMA_VERSION` in every hardfork that adds, removes, or resizes a field in
any chainbase-managed object**.

---

## Files Changed

| File | Change |
|---|---|
| `libraries/chain/hardfork.d/13.hf` | New hardfork definition |
| `libraries/chain/hardfork.d/0-preamble.hf` | `CHAIN_NUM_HARDFORKS` 12 → 13 |
| `libraries/protocol/include/graphene/protocol/config.hpp` | `CHAIN_VERSION` 3.2.0; `CHAIN_VALIDATOR_MAX_SHARING_RATE`, `CHAIN_MIN_STAKEHOLDER_REWARD_PAYOUT`, `CHAIN_SCHEMA_VERSION` |
| `libraries/protocol/include/graphene/protocol/chain_operations.hpp` | `chain_properties_hf13`, `set_reward_sharing_operation` |
| `libraries/protocol/include/graphene/protocol/chain_virtual_operations.hpp` | `stakeholder_reward_operation` |
| `libraries/protocol/include/graphene/protocol/operations.hpp` | New ops in variant |
| `libraries/protocol/chain_operations.cpp` | `validate()` for `set_reward_sharing_operation` |
| `libraries/chain/include/graphene/chain/witness_objects.hpp` | `chain_properties` alias → `hf13`; `sharing_rate`, `pending_stakeholder_reward` fields; `vote_created_block` on `witness_vote_object` |
| `libraries/chain/include/graphene/chain/chain_evaluator.hpp` | `DEFINE_EVALUATOR(set_reward_sharing)` |
| `libraries/chain/include/graphene/chain/database.hpp` | `process_validator_epoch_distribution()` declaration |
| `libraries/chain/chain_properties_evaluators.cpp` | HF13 visitor case; `set_reward_sharing_evaluator::do_apply` |
| `libraries/chain/chain_evaluator.cpp` | Set `vote_created_block` in all `create<witness_vote_object>` paths |
| `libraries/chain/database.cpp` | Hardfork registration; evaluator registration; median; `process_funds` split; `process_validator_epoch_distribution` (time-weighted); `_apply_block` call; `apply_hardfork` case |
| `plugins/account_history/plugin.cpp` | Impacted accounts for `stakeholder_reward_operation` |
| `plugins/chain/plugin.cpp` | `CHAIN_SCHEMA_VERSION` check + proactive wipe before `db.open()`; `read_schema_version` / `write_schema_version` helpers; `#include <fstream>` |
