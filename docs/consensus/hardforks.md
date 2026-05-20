# Hardforks

A hardfork is a network upgrade that changes consensus rules. All nodes must upgrade before the scheduled activation timestamp; nodes running old software will diverge from the network after activation.

---

## Activation Mechanism

Each hardfork has:
- A **unique number** (N).
- A **Unix timestamp** — the earliest wall-clock time at which the hardfork can activate.
- A **validator vote supermajority** — >80% of the current validator set must signal the new hardfork version via `validator_update_operation`.

Both conditions must be satisfied simultaneously. Validators can block an unwanted hardfork by withholding their version vote even after the scheduled timestamp.

---

## Hardfork History

| # | Version | Key changes |
|---|---------|-------------|
| 1–10 | 1.x – 2.x | Foundation, social graph, energy system, committee, subscriptions |
| 11 | 3.0.0 | — |
| 12 | 3.1.0 | Fork collision metrics, vote-weighted fork comparison, emergency consensus mode, NTP improvements |
| 13 | 3.2.0 | Validator reward sharing with vote-proportional distribution |

---

## HF12 Summary

HF12 (version 3.1.0) introduced:

1. **Fork collision counter** — `fork_collision_count` and `last_fork_collision_block_num` added to `dynamic_global_property_object`. Observable via `get_dynamic_global_properties`.
2. **Vote-weighted fork comparison** (`compare_fork_branches()`) — fork selection uses total delegated SHARES per validator branch + 10% bonus for longer chain.
3. **Emergency consensus mode** — activates automatically after 1 hour with no blocks; "committee" account takes all 21 slots. See [Emergency Consensus](./emergency-consensus.md).
4. **Minority fork auto-resync** — validator plugin detects node isolation (21 consecutive own-blocks) and rolls back to LIB.
5. **NTP improvements** — dedicated NTP client with configurable servers, interval, and round-trip threshold.

---

## HF13 Summary

HF13 (version 3.2.0) introduced:

**Validator reward sharing**: part of each block's validator reward is redistributed proportionally to the accounts that voted for that validator (by their SHARES vote weight).

- New field on `validator_object`: `reward_percent` — fraction of block reward shared with voters (0–10000 basis points).
- New virtual operation: `validator_reward_virtual_operation` — fired once per reward distribution.
- Set via `validator_update_operation`.

---

## Implementing a New Hardfork

### Step 1: Create hardfork definition file

`libraries/chain/hardfork.d/N.hf`:

```cpp
#ifndef CHAIN_HARDFORK_N
#define CHAIN_HARDFORK_N N
#define CHAIN_HARDFORK_N_TIME  1234567890  // Unix timestamp — must be in the future
#define CHAIN_HARDFORK_N_VERSION hardfork_version(3, N, 0)
#endif
```

### Step 2: Bump constants

`libraries/chain/hardfork.d/0-preamble.hf`:
```cpp
#define CHAIN_NUM_HARDFORKS N
```

`libraries/protocol/include/graphene/protocol/config.hpp` (if protocol-visible):
```cpp
#define CHAIN_VERSION  (version(3, N, 0))
```

### Step 3: Schema version

If any chainbase object layout changes (new fields, removed fields, resized types), **increment `CHAIN_SCHEMA_VERSION`** in `config.hpp`:

```cpp
#define CHAIN_SCHEMA_VERSION  uint32_t(N)
```

The chain plugin checks this at startup. A mismatch wipes `shared_memory.bin` before opening, preventing corrupt reads from old layouts.

New fields should always have **zero-value defaults** to avoid migration code:
```cpp
uint16_t my_new_field = 0;
```

### Step 4: Wire into database.cpp

`init_hardforks()`:
```cpp
FC_ASSERT(CHAIN_HARDFORK_N == N);
_hardfork_times[N]    = fc::time_point_sec(CHAIN_HARDFORK_N_TIME);
_hardfork_versions[N] = hardfork_version(CHAIN_HARDFORK_N_VERSION);
```

`apply_hardfork()` case:
```cpp
case CHAIN_HARDFORK_N: {
    // Migration if any. Leave empty with a comment if zero defaults cover it.
    break;
}
```

### Step 5: Operation and evaluator (if new op)

1. Add struct to `chain_operations.hpp` with `validate()` and authority getters.
2. Add to the `static_variant` in `operations.hpp`.
3. Declare `DEFINE_EVALUATOR(my_new_op)` in `chain_evaluator.hpp`.
4. Implement `do_apply()` in a `.cpp` evaluator file — always check `ASSERT_REQ_HF(CHAIN_HARDFORK_N, ...)` first.
5. Register in `initialize_evaluators()` in `database.cpp`.

### Step 6: Plugin updates

| Plugin | What to update |
|--------|----------------|
| `account_history` | Add impact extractor for any new virtual operation |
| `validator_api` | Add new fields from `validator_object` to `validator_api_object` |
| `snapshot` | Add new chainbase objects to `serialize_state` / `load_snapshot` |

---

## Schema Version Lifecycle

```
Fresh node (no existing data):
  stored = 0, compiled = N → mismatch
  wipe shared_memory (no-op if absent)
  write schema_version = N
  genesis → normal startup

Upgrade (old binary had version M < N):
  stored = M, compiled = N → mismatch
  wipe shared_memory.bin
  write schema_version = N
  db.open() → revision mismatch exception
  → auto-recovery: snapshot import + dlt_block_log replay

Normal restart:
  stored = N, compiled = N → match
  db.open() proceeds normally
```

**Key files:**
- `config.hpp` — `CHAIN_SCHEMA_VERSION`
- `plugins/chain/plugin.cpp` — schema check and wipe logic
- `<data_dir>/schema_version` — plain text file with current version

---

## Deployment Checklist

- [ ] `CHAIN_NUM_HARDFORKS` incremented
- [ ] `CHAIN_VERSION` bumped (if protocol-visible)
- [ ] `CHAIN_SCHEMA_VERSION` incremented (if any chainbase object layout changed)
- [ ] Hardfork `.hf` file created with future activation timestamp
- [ ] All new fields have zero defaults; `apply_hardfork` comment explains why no migration needed
- [ ] New evaluator registered in `initialize_evaluators()`
- [ ] New virtual op registered in `account_history` plugin
- [ ] `validator_api_object` updated if `validator_object` changed
- [ ] Snapshot plugin updated if new chainbase objects added

---

See also: [Fair-DPOS](./fair-dpos.md), [Emergency Consensus](./emergency-consensus.md), [Snapshots](../node/snapshot.md).
