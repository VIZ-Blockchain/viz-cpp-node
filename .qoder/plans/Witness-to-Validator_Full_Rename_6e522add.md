# Witness-to-Validator Full Rename Plan

Phase 1 (internal C++ methods/enums) is already done. This plan covers everything remaining.

## Scope: 5 Protocol Operations

| Type ID | Old Struct | New Struct |
|---------|-----------|------------|
| 6 | `witness_update_operation` | `validator_update_operation` |
| 7 | `account_witness_vote_operation` | `account_validator_vote_operation` |
| 8 | `account_witness_proxy_operation` | `account_validator_proxy_operation` |
| 30 | `shutdown_witness_operation` | `shutdown_validator_operation` |
| 42 | `witness_reward_operation` | `validator_reward_operation` |

## Task 1: Protocol operation struct renames

**Files:**
- `libraries/protocol/include/graphene/protocol/chain_operations.hpp` — rename 3 structs + 3 FC_REFLECT macros
- `libraries/protocol/include/graphene/protocol/chain_virtual_operations.hpp` — rename 2 structs + 2 FC_REFLECT macros + constructors
- `libraries/protocol/include/graphene/protocol/operations.hpp` — update 5 names in static_variant list (order unchanged!)
- `libraries/protocol/chain_operations.cpp` — rename 3 validate() method definitions

Rename `witness` field to `validator` in `account_validator_vote_operation` (was `account_witness_vote_operation`). Also rename `witness` field to `validator` in `validator_reward_operation` (was `witness_reward_operation`).

## Task 2: JSON backward-compatibility alias table

**File:** `libraries/protocol/operation_util_impl.cpp`

Add a `resolve_operation_name()` function that maps old JSON names to new names:
- `witness_update` → `validator_update`
- `account_witness_vote` → `account_validator_vote`
- `account_witness_proxy` → `account_validator_proxy`
- `shutdown_witness` → `shutdown_validator`
- `witness_reward` → `validator_reward`

This must be hooked into the `from_variant` path so nodes accept both old and new JSON names from clients.

## Task 3: Evaluator renames

**Files:**
- `libraries/chain/include/graphene/chain/chain_evaluator.hpp` — `DEFINE_EVALUATOR(witness_update)` → `DEFINE_EVALUATOR(validator_update)`, same for vote and proxy
- `libraries/chain/chain_evaluator.cpp` — 3 method signatures
- `libraries/chain/chain_properties_evaluators.cpp` — `witness_update_evaluator::do_apply`
- `libraries/chain/database.cpp` — 3 `register_evaluator` calls + all `push_virtual_operation(witness_reward_operation(...))` → `validator_reward_operation(...)` and `push_virtual_operation(shutdown_witness_operation(...))` → `shutdown_validator_operation(...)`

## Task 4: Chain object renames

**File:** `libraries/chain/include/graphene/chain/witness_objects.hpp`
- `witness_object` → `validator_object`
- `witness_schedule_object` → `validator_schedule_object`
- `witness_schedule_type` → `validator_schedule_type`
- `current_shuffled_witnesses` → `current_shuffled_validators`
- `witness_index` → `validator_index`
- `witness_id_type` → `validator_id_type`
- `witness_object_type` → `validator_object_type`
- All FC_REFLECT macros

Also rename the file itself: `witness_objects.hpp` → `validator_objects.hpp`

**File:** `libraries/chain/include/graphene/chain/chain_objects.hpp`
- `block_post_validation_object` → `validator_confirmation_object`

**File:** `libraries/chain/include/graphene/chain/chain_object_types.hpp`
- `witness_object_type` → `validator_object_type`
- `witness_schedule_object_type` → `validator_schedule_object_type`

**Every file that includes `witness_objects.hpp`** must be updated to include `validator_objects.hpp` and use new type names.

## Task 5: Database layer references

**Files:**
- `libraries/chain/database.cpp` — all references to witness_object, witness_schedule_object, get_witness_schedule_object(), etc.
- `libraries/chain/database.hpp` — method signatures like `get_witness_schedule_object()`
- `libraries/chain/include/graphene/chain/database.hpp` — same

Key method renames:
- `get_witness_schedule_object()` → `get_validator_schedule_object()`
- `get_scheduled_witness()` → `get_scheduled_validator()`
- `get_slot_time()`, `get_slot_at_time()` — keep (no "witness")
- `adjust_witness_votes()` → `adjust_validator_votes()`
- `adjust_proxied_witness_votes()` → `adjust_proxied_validator_votes()`

## Task 6: API object + endpoint renames

**File:** `libraries/api/include/graphene/api/witness_api_object.hpp` → rename to `validator_api_object.hpp`
- `witness_api_object` → `validator_api_object`

**File:** `libraries/api/witness_api_object.cpp` — same rename + all field references

**File:** `plugins/witness_api/plugin.cpp` — rename all 8 API endpoints + add deprecated aliases:
- `get_active_witnesses` → `get_active_validators` (+ keep old as alias)
- `get_witness_schedule` → `get_validator_schedule` (+ keep old as alias)
- `get_witnesses` → `get_validators` (+ keep old as alias)
- `get_witness_by_account` → `get_validator_by_account` (+ keep old as alias)
- `get_witnesses_by_vote` → `get_validators_by_vote` (+ keep old as alias)
- `get_witnesses_by_counted_vote` → `get_validators_by_counted_vote` (+ keep old as alias)
- `get_witness_count` → `get_validator_count` (+ keep old as alias)
- `lookup_witness_accounts` → `lookup_validator_accounts` (+ keep old as alias)

**File:** `plugins/witness_api/include/graphene/plugins/witness_api/plugin.hpp` — update DEFINE_API_ARGS macros

## Task 7: Wallet renames

**Files:**
- `libraries/wallet/wallet.cpp` — rename commands, keep old as deprecated aliases
- `libraries/wallet/include/graphene/wallet/wallet.hpp` — method declarations
- `libraries/wallet/include/graphene/wallet/remote_node_api.hpp` — remote API method names

## Task 8: Plugin directory + config renames

This involves actual directory renaming which is outside the scope of code editing tools. Manual steps:
- `plugins/witness/` → `plugins/validator/`
- `plugins/witness_api/` → `plugins/validator_api/`
- `plugins/witness_guard/` → `plugins/validator_guard/`
- Update all CMakeLists.txt references
- Update `share/vizd/config/config_witness.ini` → `config_validator.ini`
- Update `share/vizd/config/config.ini` plugin names

## Task 9: Update documentation

- `.qoder/docs/witness-to-validator-migration-reference.md` — update status table to "Done"
- `.qoder/docs/op-witness.md` → rename to `op-validator.md` with new operation names
- `.qoder/docs/witness-plugin.md` → rename/rewrite
- `.qoder/docs/data-types.md` — update operation type table

## Task 10: Build verification

Compile the entire project to verify no broken references.

## Execution Order

Tasks 1-5 must be done together (they're interdependent — renaming a struct breaks all references). Recommended approach:
1. Task 1 (protocol structs) + Task 2 (alias table) together
2. Task 3 (evaluators) — immediately after, since they reference the structs
3. Task 4 (chain objects) — next, largest scope
4. Task 5 (database layer) — after chain objects
5. Task 6 (API) — after database
6. Task 7 (wallet) — after API
7. Task 8 (plugins/config) — last, requires directory renames
8. Task 9 (docs)
9. Task 10 (build verify)

## Risk Assessment

- **Binary wire format:** Zero risk — integer type IDs are unchanged
- **JSON compatibility:** The alias table ensures old clients can still submit transactions with old names
- **Shared memory on-disk format:** `witness_object` is serialized to shared memory by type ID, not name — but FC_REFLECT field names ARE serialized in some contexts. Need to verify if renaming FC_REFLECT fields breaks the shared memory format. If it does, a migration layer is needed.
- **Snapshot format:** Snapshots serialize objects as JSON with field names — renaming fields breaks snapshot compatibility with old snapshots
