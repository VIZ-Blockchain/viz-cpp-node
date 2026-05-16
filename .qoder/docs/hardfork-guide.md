# Hardfork Implementation Guide

Checklist and rules for implementing a new hardfork in the VIZ node.
Covers protocol, chain, plugins, deployment, and schema versioning.

---

## 1. Define the hardfork

### 1.1. Hardfork `.hf` file

Create `libraries/chain/hardfork.d/N.hf`:

```cpp
#ifndef CHAIN_HARDFORK_N
#define CHAIN_HARDFORK_N N
#define CHAIN_HARDFORK_N_TIME  1234567890  // Unix timestamp — must be in the future
#define CHAIN_HARDFORK_N_VERSION hardfork_version(3, N, 0)
#endif
```

### 1.2. Bump CHAIN_NUM_HARDFORKS

In `libraries/chain/hardfork.d/0-preamble.hf`:
```cpp
#define CHAIN_NUM_HARDFORKS N   // was N-1
```

### 1.3. Bump CHAIN_VERSION (if protocol-visible)

In `libraries/protocol/include/graphene/protocol/config.hpp`:
```cpp
#define CHAIN_VERSION  (version(3, N, 0))
```

---

## 2. Protocol changes

### 2.1. New operation

Add to `libraries/protocol/include/graphene/protocol/chain_operations.hpp`:
- Struct definition with `validate()` and authority getters
- Forward-declare any new `chain_properties_hfN` struct

Add to `libraries/protocol/include/graphene/protocol/operations.hpp`:
- Entry in the `static_variant` for the new op
- Guard comment `// VIZ HF N: ...`

Implement `validate()` in `libraries/protocol/chain_operations.cpp`.

### 2.2. New virtual operation

Add to `libraries/protocol/include/graphene/protocol/chain_virtual_operations.hpp`:
```cpp
struct my_new_virtual_operation : public virtual_operation {
    my_new_virtual_operation() {}
    my_new_virtual_operation(args...) : ... {}
    // fields
};
FC_REFLECT(...)
```

Add to `libraries/protocol/include/graphene/protocol/operations.hpp` in the virtual op section.

---

## 3. Chain objects

### 3.1. Adding fields to existing chainbase objects

> **⚠ SCHEMA RULE — mandatory step:**
> Whenever you add, remove, or resize a field in any chainbase-managed struct
> (`witness_object`, `account_object`, etc.), you MUST increment
> `CHAIN_SCHEMA_VERSION` in `config.hpp`.
>
> ```cpp
> // config.hpp — increment this for every field layout change:
> #define CHAIN_SCHEMA_VERSION  uint32_t(N)
> ```
>
> The chain plugin reads `<data_dir>/schema_version` at startup and compares it with
> `CHAIN_SCHEMA_VERSION`.  A mismatch triggers proactive `shared_memory.bin` wipe
> before `db.open()` is called, preventing corrupt reads.  Without this, old shared
> memory can be opened with incorrect object sizes, leading to silent data corruption
> or crashes.
>
> **History format** — add a comment entry:
> ```cpp
> ///   N  — HFN: describe every affected object and field
> ```

New fields should always have zero-value defaults:
```cpp
uint16_t my_new_field = 0;
share_type my_reward   = 0;
```

Zero defaults mean `apply_hardfork(N)` requires no migration — all existing objects
are in a valid pre-HF state without any extra writes.

Add new fields to `FC_REFLECT`.

### 3.2. Adding new chainbase objects

Follow [dlt-hardfork-new-objects.md](dlt-hardfork-new-objects.md) for:
- `object_type` enum entry
- Object + index header
- `initialize_indexes()` registration
- Snapshot plugin `serialize_state` / `load_snapshot` support

> **Schema rule applies here too**: adding a new index is a layout change.
> Increment `CHAIN_SCHEMA_VERSION`.

---

## 4. Evaluators

### 4.1. Declare evaluator

In `libraries/chain/include/graphene/chain/chain_evaluator.hpp`:
```cpp
DEFINE_EVALUATOR(my_new_op)
```

### 4.2. Implement evaluator

In `libraries/chain/chain_properties_evaluators.cpp` (or a dedicated file):
```cpp
void my_new_op_evaluator::do_apply(const my_new_op_operation& o) {
    ASSERT_REQ_HF(CHAIN_HARDFORK_N, "my_new_op_operation");
    // ...
}
```

### 4.3. Register evaluator

In `libraries/chain/database.cpp`, `initialize_evaluators()`:
```cpp
_my->_evaluator_registry.register_evaluator<my_new_op_evaluator>();
```

---

## 5. database.cpp wiring

### 5.1. Register hardfork

In `init_hardforks()` (or hardfork array initialisation):
```cpp
FC_ASSERT(CHAIN_HARDFORK_N == N);
_hardfork_times[N] = fc::time_point_sec(CHAIN_HARDFORK_N_TIME);
_hardfork_versions[N] = hardfork_version(CHAIN_HARDFORK_N_VERSION);
```

### 5.2. apply_hardfork case

```cpp
case CHAIN_HARDFORK_N: {
    // Migration for any data that cannot be expressed via field defaults.
    // If all new fields default to zero → leave this block empty with a comment.
    break;
}
```

### 5.3. chain_properties median (if new consensus property)

Add the new property to the median computation in `update_witness_schedule()` or the
equivalent location where `wso.median_props` is built:
```cpp
wso.median_props.my_new_param = median_value;
```

---

## 6. Plugins

### 6.1. account_history

Add impact extractor for any new virtual operation:
```cpp
void operator()(const my_new_virtual_operation& op) {
    impacted.insert(op.sender);
    impacted.insert(op.receiver);
}
```

### 6.2. witness_api

If new fields appear on `witness_object` → add them to `witness_api_object`:
- `libraries/api/include/graphene/api/witness_api_object.hpp` — field declaration + `FC_REFLECT`
- `libraries/api/witness_api_object.cpp` — initialise in constructor

### 6.3. chain_properties_update visitor

In `chain_properties_evaluators.cpp`, add the new `chain_properties_hfN` visitor:
```cpp
result_type operator()(const chain_properties_hfN& p) const {
    FC_ASSERT(_db.has_hardfork(CHAIN_HARDFORK_N), "chain_properties_hfN");
    _wprops = p;
}
```

---

## 7. Deployment checklist

Before merging / deploying:

- [ ] `CHAIN_NUM_HARDFORKS` incremented
- [ ] `CHAIN_VERSION` bumped if protocol-visible change
- [ ] `CHAIN_SCHEMA_VERSION` incremented if ANY chainbase object layout changed
- [ ] Hardfork `.hf` file created with future timestamp
- [ ] All new fields have zero defaults; `apply_hardfork` comment explains why no migration needed
- [ ] New evaluator registered in `initialize_evaluators()`
- [ ] New virtual op registered in `account_history` plugin
- [ ] `witness_api_object` updated if `witness_object` changed
- [ ] Snapshot plugin updated if new chainbase objects added

---

## 8. Schema version — full lifecycle

```
First run (fresh node or missing schema_version file):
  stored = 0, compiled = N  → mismatch
  wipe shared_memory (no-op if not present)
  write schema_version = N
  db.open() → init_genesis → revision=0, head=0 → no exception

Upgrade (old binary had schema version M < N):
  stored = M, compiled = N  → mismatch
  wipe shared_memory.bin
  write schema_version = N
  db.open() → init_genesis → revision=0, head=head_block_log
  → database_revision_exception
  → auto-recovery: snapshot import + dlt_block_log replay
    OR replay-if-corrupted: full block_log replay

Normal restart (schema unchanged):
  stored = N, compiled = N  → match
  db.open() proceeds normally
  write schema_version = N  (confirms success)
```

**Key files:**
- `libraries/protocol/include/graphene/protocol/config.hpp` — `CHAIN_SCHEMA_VERSION`
- `plugins/chain/plugin.cpp` — `read_schema_version()`, `write_schema_version()`,
  schema check block in `plugin_startup()`
- `<data_dir>/schema_version` — plain text file, single `uint32_t`

---

## 9. Related documentation

- [dlt-hardfork-new-objects.md](dlt-hardfork-new-objects.md) — adding new chainbase indexes
- [hf13-validator-reward-sharing.md](hf13-validator-reward-sharing.md) — reference implementation (HF13)
- [snapshot-plugin.md](snapshot-plugin.md) — snapshot format and import/export
- [shared-memory.md](shared-memory.md) — chainbase shared memory internals
