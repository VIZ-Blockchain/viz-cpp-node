# Hardfork Management

VIZ Ledger coordinates protocol upgrades through a deterministic hardfork system. Hardforks are defined at compile time, activated by validator consensus, and applied automatically during block processing — no node restart required.

---

## How It Works

### 1. Definition files

Each hardfork has a dedicated `*.hf` file in `libraries/chain/hardfork.d/` that defines compile-time constants:

```cpp
// Example: 9.hf
#define CHAIN_HARDFORK_9          9
#define CHAIN_HARDFORK_9_VERSION  version(1, 0, 9)
#define CHAIN_HARDFORK_9_TIME     (fc::time_point_sec(1650000000))
```

The preamble file (`0-preamble.hf`) declares the total count and the hardfork property object schema. Currently: `CHAIN_NUM_HARDFORKS = 12`.

### 2. Validator consensus

Validators publish their preferred next hardfork version via `versioned_chain_properties_update_operation`. During each validator schedule update, the node:

1. Collects the hardfork version each active validator supports.
2. If a majority agree on a version ≥ next scheduled version, sets `next_hardfork` and `next_hardfork_time`.

### 3. Activation during block processing

When the head block time passes `next_hardfork_time` and sufficient validators support the version, the node calls `apply_hardfork(N)`. All subsequent behavior changes are gated by `has_hardfork(N)` checks in evaluators, inflation logic, and chain properties.

---

## Hardfork History

| HF | Key changes |
|----|------------|
| 1 | Median calculation fix |
| 2 | Committee approval threshold fix |
| 3 | Minor protocol corrections |
| 4 | Award operations, custom operation sequences |
| 5 | Bandwidth and authority fixes |
| 6 | Validator miss penalties, vote counting |
| 7 | Social/content adjustments |
| 8 | Protocol cleanup |
| 9 | Invite system, paid subscriptions, validator fees, withdraw_intervals |
| 10 | Inflation model |
| 11 | Emission model changes |
| 12 | Emergency consensus recovery (see below) |

---

## HF12: Emergency Consensus Recovery

HF12 introduces automatic network recovery when block production stalls.

### Activation

If the last irreversible block (LIB) timestamp is more than `CHAIN_EMERGENCY_CONSENSUS_TIMEOUT_SEC` (1 hour) behind wall clock, emergency mode activates automatically. An emergency validator account (`CHAIN_EMERGENCY_VALIDATOR_ACCOUNT = "committee"`) with a known public key (`CHAIN_EMERGENCY_VALIDATOR_PUBLIC_KEY`) is created and inserted into the block production schedule.

### Three-state safety enforcement

| Network state | Condition | Behavior |
|--------------|-----------|----------|
| Healthy | Participation ≥ 33% | Enforces safe defaults; overrides manual config |
| Distressed | Participation < 33% | Honors manual config overrides for operator recovery |
| Emergency | Emergency mode active | Bypasses stale and participation checks automatically |

### Enhanced validator scheduling

- **Hybrid schedule**: Emergency validator fills unavailable slots while keeping real validator positions.
- **Vote-weighted fork switching**: Uses sum of raw votes from unique non-committee validators as the primary fork comparison criterion.
- **Median exclusion**: Emergency validator's property votes are excluded from median chain parameter computation.

---

## Hardfork Property Object

The persistent `hardfork_property_object` (singleton in chainbase) tracks:

| Field | Description |
|-------|-------------|
| `processed_hardforks` | Vector of applied hardfork times |
| `last_hardfork` | ID of the last applied hardfork |
| `current_hardfork_version` | Protocol version currently enforced |
| `next_hardfork` | Next scheduled hardfork version |
| `next_hardfork_time` | When `next_hardfork` will activate |

For HF12+, additional fields track emergency consensus state.

---

## Database Lifecycle and Hardforks

### On open

```
database::open()
  → init_schema(), initialize_indexes(), initialize_evaluators()
  → load hardfork_property from chainbase
  → init_hardforks()  ← populates _hardfork_times[] and _hardfork_versions[] arrays
  → assert: chainbase revision == head_block_num
```

### On reindex

Replays all blocks from block_log with skip flags (no signature checks, no merkle) for speed. `apply_hardfork()` fires at each hardfork boundary during replay, ensuring deterministic state reconstruction.

### On block apply

```
process_hardforks()
  → check if next_hardfork_time has passed
  → check if validator consensus supports the version
  → if yes: apply_hardfork(N)
             → run version-specific state migrations
             → update current_hardfork_version
```

---

## Rollback and Fork Switching

The database uses undo sessions for atomic block application — partial failures roll back cleanly.

For fork switches, `fetch_branch_from()` returns both branches to their common ancestor, pops the current branch, and reapplies the new one. HF12 adds vote-weighted chain comparison during this process.

If block application fails, the fork database entry is removed and an exception is thrown. The P2P layer handles the exception by marking the sending peer appropriately.

---

## Adding a New Hardfork

1. **Create `N.hf`** in `libraries/chain/hardfork.d/`:
   ```cpp
   #define CHAIN_HARDFORK_N          N
   #define CHAIN_HARDFORK_N_VERSION  version(1, 0, N)
   #define CHAIN_HARDFORK_N_TIME     (fc::time_point_sec(UNIX_TIMESTAMP))
   ```

2. **Increment `CHAIN_NUM_HARDFORKS`** in `0-preamble.hf` to N.

3. **Gate new behavior** in evaluators and runtime logic:
   ```cpp
   if (db.has_hardfork(CHAIN_HARDFORK_N)) {
       // new behavior
   } else {
       // legacy behavior
   }
   ```

4. **Add state migrations** in `apply_hardfork(N)` within `database.cpp`:
   ```cpp
   case CHAIN_HARDFORK_N:
       // one-time migration code
       break;
   ```

5. **Consider emergency mode**: If the hardfork modifies validator scheduling or chain parameters, ensure the emergency validator is excluded from affected computations.

6. **Test with reindex**: Run a full reindex against mainnet block data to confirm deterministic replay produces identical state.

---

## Troubleshooting

| Symptom | Likely cause | Resolution |
|---------|-------------|------------|
| Hardfork not triggering | Validator consensus not reached | Verify validators publish the target version; check `get_next_scheduled_hardfork()` API |
| Revision mismatch on open | chainbase revision ≠ head block num | Reindex from block log or restore from snapshot |
| Memory exhaustion during reindex | Shared memory too small | Increase `shared-file-size`; enable auto-resize |
| Emergency mode not activating | HF12 not applied yet | Verify `current_hardfork_version` ≥ 1.0.12 |
| State mismatch after reindex | Non-deterministic has_hardfork() branch | Audit `apply_hardfork()` for side effects |

**Diagnostics:**

```json
{ "method": "database_api.get_hardfork_version", "params": [] }
{ "method": "database_api.get_next_scheduled_hardfork", "params": [] }
```

---

## Upgrade Checklist

- [ ] Define `N.hf` with realistic timestamp (coordinate with validators)
- [ ] Increment `CHAIN_NUM_HARDFORKS` in `0-preamble.hf`
- [ ] Implement `apply_hardfork(N)` migrations
- [ ] Gate behavior changes with `has_hardfork()` checks
- [ ] Back up database and block log before deploying
- [ ] Start node in read-only mode to verify compatibility
- [ ] Monitor logs for hardfork activation events
- [ ] Coordinate with validators to publish the new version
- [ ] Confirm `get_next_scheduled_hardfork()` shows the expected version/time

---

See also: [Chain Properties](../governance/chain-properties.md), [Validators](../protocol/operations/validators.md), [Database Schema](./database-schema.md), [Database API](../plugins/database-api.md).
