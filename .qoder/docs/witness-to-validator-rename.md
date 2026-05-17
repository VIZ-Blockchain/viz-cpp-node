# Naming Analysis: VIZ "Witness" → "Validator" Rename Proposal

## 1. What VIZ Witnesses Actually Do

Looking at the codebase, VIZ witnesses perform two distinct roles:

**Block production** — scheduled in rotation, they run `block_production_loop()`, sign blocks with `block_signing_key`, and broadcast them via P2P.

**Block post validation (BPV)** — they sign `block_post_validation_object`s to confirm blocks produced by other witnesses, which drives LIB (Last Irreversible Block) advancement.

So witnesses are both producers and validators — which is exactly why "witness" is semantically weak. It sounds passive ("I saw this happen") and says nothing about their active role in consensus.

---

## 2. What XRPL Calls These Nodes

XRPL uses:

| XRPL Term | Meaning |
|-----------|---------|
| **Validator** | A server actively participating in consensus — proposes, votes on, and confirms transaction sets |
| **UNL (Unique Node List)** | The trusted set of validators a node relies on |
| **Validation vote** | A cryptographic fingerprint published post-consensus round |
| **Participant** | Generic term for any consensus node |

XRPL does not split "producer" from "validator" — validators do both. This matches exactly how VIZ witnesses work.

The same pattern holds across other major PoS ecosystems:
- **Ethereum PoS** — validators attest and propose blocks
- **Cosmos / Tendermint** — validators propose and pre-vote/pre-commit blocks
- **Polkadot** — validators produce and attest parachain blocks

---

---

## 2b. Current Implementation Status (2026-05-17)

### Done

| Item | Notes |
|------|-------|
| Protocol operations (types 6, 7, 8, 30, 42) | Renamed; old-name alias table in `operation_util_impl.cpp` |
| Operation field names (`witness` → `validator` in types 7 and 42) | Node accepts both old and new on input |
| Chain properties fields (`inflation_validator_percent`, etc.) | Old names accepted in snapshot import |
| Chain objects (`validator_object`, `validator_schedule_object`) | C++ types renamed; files not renamed yet |
| Schedule fields (`current_shuffled_validators`, `num_scheduled_validators`) | Done |
| API object (`validator_api_object`) | Done |
| API methods (`get_active_validators`, `get_validator_by_account`, etc.) | Done |
| CLI wallet commands (`get_active_validators`, `vote_for_validator`, etc.) | Done |
| Block header fields (`validator`, `validator_signature`) | Done |
| Dynamic global property (`current_validator`) | Done |
| Internal skip flag (`skip_validator_signature`) | Done |
| P2P function signatures (`validator_signature` parameter) | Done |
| Internal plugin methods (`block_validation_loop`, `maybe_validate_block`, etc.) | Done |
| Internal enum (`block_validation_condition`) | Done |
| Snapshot backward compat | Old field names accepted on import; new names on export |
| Operation name alias table | `resolve_operation_name()` in `operation_util_impl.cpp` |

### Deferred to Future PR

Nothing remaining. All renames complete.

### Explicitly Kept (not renamed)

- `witness_vote_object` — internal vote-tracking object; not exposed by name in protocol
- `witness_penalty_expire_object` — internal object; not exposed in protocol
- `witness_penalty_expire_object::witness` field — internal back-reference, not a block header field

---

## 3. How Operations Are Serialized (Critical for Compatibility)

Understanding the wire format determines what is and is not a breaking change.

### Binary / wire protocol

Operations in `fc::static_variant` are serialized as `[integer_index, {fields}]`:

```cpp
// thirdparty/fc/include/fc/static_variant.hpp
void from_variant(const fc::variant &v, fc::static_variant<T...> &s) {
    auto ar = v.get_array();
    s.set_which(ar[0].as_uint64());  // INTEGER index — not the struct name
    s.visit(to_static_variant(ar[1]));
}
```

**Renaming C++ struct names has zero impact on the binary wire format** as long as the order in the `static_variant` list in `operations.hpp` is preserved.

### JSON-RPC name format

Operation names exposed via JSON-RPC are derived from the C++ type name by `name_from_type()`:

```cpp
// libraries/protocol/operation_util_impl.cpp
std::string name_from_type(const std::string &type_name) {
    auto start = type_name.find_last_of(':') + 1;
    auto end   = type_name.find_last_of('_');
    return type_name.substr(start, end - start);
    // "graphene::protocol::witness_update_operation" → "witness_update"
}
```

**Renaming a struct changes its JSON name.** Any JS/PHP/Python client that submits transactions using string operation names (e.g. `["witness_update", {...}]`) will break if the struct is renamed without a compatibility layer.

---

## 4. Backward-Compatibility Fallback (Old Client Support)

**Yes, a fallback is feasible without a hardfork.**

The approach: add a static alias table in the JSON deserialization path that maps old string names to new ones before the type lookup. Clients sending `"witness_update"` would be transparently remapped to `"validator_update"`.

### Implementation location

The alias mapping belongs in the operation variant `from_variant` path, in:
- `libraries/protocol/operation_util_impl.cpp` — `name_from_type()` or a new `resolve_operation_name()` wrapper
- Or in the JSON-RPC layer that dispatches incoming transaction broadcasts

### Alias table (old → new)

| Old JSON name | New JSON name |
|--------------|---------------|
| `witness_update` | `validator_update` |
| `account_witness_vote` | `account_validator_vote` |
| `account_witness_proxy` | `account_validator_proxy` |
| `shutdown_witness` | `shutdown_validator` |
| `witness_reward` | `validator_reward` |

### Behavior

- Nodes running the renamed version accept **both** old and new operation names in incoming JSON.
- Nodes serialize outgoing JSON using **new names only**.
- Binary wire format is unchanged — integer indices are stable.
- No hardfork needed for the fallback layer itself.
- JS/PHP/CLI clients that have not been updated continue to work transparently.
- The fallback can be removed in a future release after all clients are migrated.

---

## 5. Full Rename Tables

### 5.1 Protocol Operations

These are the on-chain operations. The C++ struct rename is a code-only change (binary wire index preserved); the JSON name fallback handles old clients.

| Current struct name | New struct name | JSON old name | JSON new name | Type ID | Virtual? |
|--------------------|-----------------|--------------|---------------|---------|----------|
| `witness_update_operation` | `validator_update_operation` | `witness_update` | `validator_update` | 6 | no |
| `account_witness_vote_operation` | `account_validator_vote_operation` | `account_witness_vote` | `account_validator_vote` | 7 | no |
| `account_witness_proxy_operation` | `account_validator_proxy_operation` | `account_witness_proxy` | `account_validator_proxy` | 8 | no |
| `shutdown_witness_operation` | `shutdown_validator_operation` | `shutdown_witness` | `shutdown_validator` | 50 | yes |
| `witness_reward_operation` | `validator_reward_operation` | `witness_reward` | `validator_reward` | 66 | yes |

> `chain_properties_update_operation` and `versioned_chain_properties_update_operation` — these are submitted by witnesses but describe chain property voting, not the witness role itself. **Keep names as-is.**

### 5.2 Core Objects and Types

| Current Name | New Name | File |
|-------------|----------|------|
| `witness_object` | `validator_object` | `libraries/chain/include/graphene/chain/witness_objects.hpp` |
| `witness_schedule_object` | `validator_schedule_object` | `libraries/chain/include/graphene/chain/witness_objects.hpp` |
| `witness_schedule_type` | `validator_schedule_type` | `libraries/chain/include/graphene/chain/witness_objects.hpp` |
| `current_shuffled_witnesses[]` | `current_shuffled_validators[]` | field in `validator_schedule_object` |
| `block_post_validation_object` | `validator_confirmation_object` | `libraries/chain/include/graphene/chain/chain_objects.hpp` |
| `witness_api_object` | `validator_api_object` | `libraries/api/include/graphene/api/witness_api_object.hpp` |

### 5.3 Internal Enum (`block_production_condition`)

| Current Name | New Name | File |
|-------------|----------|------|
| namespace `block_production_condition` | `block_validation_condition` | `plugins/witness/include/graphene/plugins/witness/witness.hpp` |
| `block_production_condition_enum` | `block_validation_condition_enum` | same |
| `exception_producing_block` | `exception_validating_block` | same |

All other enum values (`produced`, `not_synced`, `not_my_turn`, `not_time_yet`, `no_private_key`, `low_participation`, `lag`, `consecutive`, `fork_collision`, `minority_fork`) need no rename.

### 5.4 Plugin Internal Methods

| Current Name | New Name | File |
|-------------|----------|------|
| `block_production_loop()` | `block_validation_loop()` | `plugins/witness/witness.cpp` |
| `maybe_produce_block()` | `maybe_validate_block()` | `plugins/witness/witness.cpp` |
| `is_witness_scheduled_soon()` | `is_validator_scheduled_soon()` | `plugins/witness/witness.hpp` |

### 5.5 Plugins (Directories and CMake Targets)

| Current Name | New Name |
|-------------|----------|
| `witness_plugin` | `validator_plugin` |
| `plugins/witness/` | `plugins/validator/` |
| `witness_api_plugin` | `validator_api_plugin` |
| `plugins/witness_api/` | `plugins/validator_api/` |
| `witness_guard_plugin` | `validator_guard_plugin` |
| `plugins/witness_guard/` | `plugins/validator_guard/` |

### 5.6 Witness API Endpoints (JSON-RPC)

| Current Name | New Name |
|-------------|----------|
| `get_active_witnesses()` | `get_active_validators()` |
| `get_witness_schedule()` | `get_validator_schedule()` |
| `get_witnesses()` | `get_validators()` |
| `get_witness_by_account()` | `get_validator_by_account()` |
| `get_witnesses_by_vote()` | `get_validators_by_vote()` |
| `get_witnesses_by_counted_vote()` | `get_validators_by_counted_vote()` |
| `get_witness_count()` | `get_validator_count()` |
| `lookup_witness_accounts()` | `lookup_validator_accounts()` |

> API endpoint fallback: keep old method names as deprecated aliases that forward to new implementations for one release cycle.

### 5.7 CLI Wallet Commands

| Current Command | New Command | Operation Used |
|----------------|-------------|----------------|
| `list_witnesses()` | `list_validators()` | — (read) |
| `get_witness()` | `get_validator()` | — (read) |
| `get_active_witnesses()` | `get_active_validators()` | — (read) |
| `update_witness()` | `update_validator()` | `validator_update_operation` |
| `vote_for_witness()` | `vote_for_validator()` | `account_validator_vote_operation` |
| `set_voting_proxy()` | `set_voting_proxy()` | `account_validator_proxy_operation` — command name stays |

File: `libraries/wallet/wallet.cpp` and `libraries/wallet/include/graphene/wallet/wallet.hpp`

### 5.8 Configuration Keys

| Current Key | New Key | File |
|------------|---------|------|
| `plugin = witness` | `plugin = validator` | `config_witness.ini` |
| `plugin = witness_guard` | `plugin = validator_guard` | `config_witness.ini` |
| `plugin = witness_api` | `plugin = validator_api` | `config_witness.ini` |
| `--witness = "name"` | `--validator = "name"` | `config_witness.ini` |
| `witness-guard-enabled` | `validator-guard-enabled` | `config_witness.ini` |
| `witness-guard-disable` | `validator-guard-disable` | `config_witness.ini` |
| `witness-guard-interval` | `validator-guard-interval` | `config_witness.ini` |
| `witness-guard-witness` | `validator-guard-validator` | `config_witness.ini` |

Config keys fallback: on startup, if an old key is detected emit a warning — `"Config key 'witness' is deprecated, use 'validator'"` — and continue reading the value.

---

## 6. External Client Libraries

JS and PHP libraries are external repositories not in this codebase. They reference:
- Operation names as strings: `"witness_update"`, `"account_witness_vote"`, `"account_witness_proxy"`
- API method names: `get_active_witnesses()`, `get_witness_by_account()`, etc.

### Impact without fallback

| Client action | Breaks without fallback? |
|--------------|--------------------------|
| Submit `witness_update` transaction by string name | Yes |
| Submit transaction by integer type ID (6, 7, 8) | No — wire format unchanged |
| Call `get_active_witnesses()` API | Yes |
| Read operation from block history | No — history uses integer IDs |

### Impact with fallback (Section 4)

| Client action | Breaks with fallback? |
|--------------|----------------------|
| Submit `witness_update` by string name | No — alias maps to new name |
| Call `get_active_witnesses()` | No — old endpoint aliased |
| Receive response containing `validator_update` instead of `witness_update` | Yes — clients parsing response type names will see the new name |

### Required changes in JS/PHP libs

Even with server-side fallback, clients will receive **responses** with new names. The minimum update per library:

| What to update | Detail |
|---------------|--------|
| Operation name constants | `"witness_update"` → `"validator_update"` etc. |
| API method names in client code | `getActiveWitnesses()` → `getActiveValidators()` etc. |
| Response field parsing | Any code checking `op[0] === "witness_update"` |
| Type constants / enums | Any named constants for operation types |

---

## 7. Terms to Keep Unchanged

| Identifier | Why it stays |
|------------|-------------|
| `block_signing_key` / `signing_key` | Accurately describes the cryptographic key used to sign blocks and post-validations |
| `delegate_vesting_shares_operation` | "Delegate" is already taken in VIZ for vesting share delegation — **do not use "delegate" as the consensus-role name** |
| `chain_properties_update_operation` | Describes chain governance, not the witness role |
| `versioned_chain_properties_update_operation` | Same |
| Enum values `top`, `support`, `none` | Scheduling tier names, not role names |

---

## 8. Implementation Phases

### Phase 1 — Internal rename (zero breaking changes) ✅ Done

1. ✅ Rename `block_production_condition` namespace, enum, and `exception_producing_block` in `witness.hpp` + `witness.cpp`.
2. ✅ Rename internal method names: `block_production_loop`, `maybe_produce_block`, `is_witness_scheduled_soon`.
3. ⏳ Rename `block_post_validation_object` → `validator_confirmation_object` — deferred with physical file renames.
4. ✅ Rename `current_shuffled_witnesses[]` field.
5. ✅ Build verified.
6. ✅ `.qoder/` documentation updated.

### Phase 2 — API and config rename (with fallbacks) ✅ Done

1. ✅ Add operation name alias table in `operation_util_impl.cpp` — old JSON names → new names.
2. ✅ Rename `witness_update_operation` → `validator_update_operation` and the other four operations (Section 5.1). Binary type IDs preserved.
3. ✅ Add deprecated endpoint aliases in `validator_api` plugin for all `get_witness_*` methods.
4. ✅ Rename CLI wallet commands; keep old names as deprecated aliases.
5. ✅ Config keys updated (`plugin = validator`, `plugin = validator_api`, `plugin = validator_guard`). `--witness` kept as deprecated alias for `--validator` in config.ini backward compat.
6. ✅ Plugin directories renamed: `plugins/validator/`, `plugins/validator_api/`, `plugins/validator_guard/`. CMake targets updated.
7. ✅ Rename `witness_object`, `witness_schedule_object`, `witness_api_object` (C++ types and files).
8. ✅ `config_witness.ini` and all other `config*.ini` updated to new plugin names.
9. ✅ Rename block header fields: `validator`, `validator_signature`.
10. ✅ Rename dynamic global property field: `current_validator`.
11. ✅ Rename skip flag: `skip_validator_signature`.
12. ✅ Plugin namespaces: `validator_plugin`, `validator_api`, `validator_guard`. `plugin_name` strings updated.

### Phase 3 — External library updates

1. Update JS client library: operation name constants, API method names, response parsing, block header field names.
2. Update PHP client library: same scope.
3. After both libraries are released, schedule removal of the server-side fallback aliases.

---

## 9. Files Affected (Source Code)

| File | Status | Scope |
|------|--------|-------|
| `plugins/witness/include/graphene/plugins/witness/witness.hpp` | ✅ Done | Enum namespace, method declarations |
| `plugins/witness/witness.cpp` | ✅ Done | All enum references, method definitions, block field accesses |
| `plugins/witness_guard/witness_guard.cpp` | ✅ Done | Object types, block field accesses |
| `plugins/witness_guard/include/.../witness_guard.hpp` | ✅ Done | Class names, config declarations |
| `plugins/witness_api/plugin.cpp` | ✅ Done | API method names + deprecated aliases |
| `plugins/witness_api/include/.../plugin.hpp` | ✅ Done | API method declarations |
| `plugins/p2p/p2p_plugin.cpp` | ✅ Done | `validator_signature` parameter |
| `plugins/p2p/include/.../p2p_plugin.hpp` | ✅ Done | `validator_signature` parameter |
| `plugins/chain/plugin.cpp` | ✅ Done | Block field access |
| `plugins/snapshot/plugin.cpp` | ✅ Done | Object types, field accesses, backward compat import |
| `plugins/database_api/api.cpp` | ✅ Done | Object type references |
| `plugins/account_history/plugin.cpp` | ✅ Done | Operation visitor method names |
| `libraries/chain/include/graphene/chain/witness_objects.hpp` | ✅ Done | Object type names, field names (file not renamed yet) |
| `libraries/chain/include/graphene/chain/global_property_object.hpp` | ✅ Done | `current_validator` field + FC_REFLECT |
| `libraries/chain/include/graphene/chain/chain_objects.hpp` | ✅ Done | `validator_confirmation_object`, `validator_confirmation_index` |
| `libraries/chain/database.cpp` | ✅ Done | All object and block field references |
| `libraries/chain/database.hpp` | ✅ Done | `skip_validator_signature` flag |
| `libraries/protocol/include/graphene/protocol/block_header.hpp` | ✅ Done | `validator`, `validator_signature` fields |
| `libraries/protocol/block.cpp` | ✅ Done | `validator_signature` references |
| `libraries/protocol/include/graphene/protocol/chain_operations.hpp` | ✅ Done | Operation struct names, field names |
| `libraries/protocol/include/graphene/protocol/chain_virtual_operations.hpp` | ✅ Done | Virtual operation struct names |
| `libraries/protocol/include/graphene/protocol/operations.hpp` | ✅ Done | static_variant list — struct names only, order unchanged |
| `libraries/protocol/operation_util_impl.cpp` | ✅ Done | Alias table for old JSON names |
| `libraries/api/include/graphene/api/witness_api_object.hpp` | ✅ Done | `validator_api_object` type |
| `libraries/api/witness_api_object.cpp` | ✅ Done | Constructor, field assignments |
| `libraries/api/include/graphene/api/chain_api_properties.hpp` | ✅ Done | Chain properties field names |
| `libraries/api/chain_api_properties.cpp` | ✅ Done | Field assignments |
| `libraries/network/dlt_p2p_node.cpp` | ✅ Done | Block field accesses, `validator_signature` parameter |
| `libraries/network/include/graphene/network/dlt_p2p_node.hpp` | ✅ Done | `validator_signature` parameter |
| `plugins/validator/CMakeLists.txt` | ✅ Done | Target `graphene_validator`, new source/header paths |
| `plugins/validator_api/CMakeLists.txt` | ✅ Done | Target `graphene_validator_api` |
| `plugins/validator_guard/CMakeLists.txt` | ✅ Done | Target `graphene_validator_guard` |
| `plugins/p2p/CMakeLists.txt` | ✅ Done | Include path `../validator/include` |
| `plugins/snapshot/CMakeLists.txt` | ✅ Done | Link `graphene_validator` |
| `programs/vizd/CMakeLists.txt` | ✅ Done | Links `graphene::validator`, `graphene::validator_api`, `graphene::validator_guard` |
| `programs/cli_wallet/CMakeLists.txt` | ✅ Done | Link `graphene::validator_api` |
| `libraries/wallet/wallet.cpp` | ✅ Done | CLI wallet command implementations |
| `libraries/wallet/include/graphene/wallet/wallet.hpp` | ✅ Done | CLI wallet method declarations |
| `libraries/wallet/include/graphene/wallet/remote_node_api.hpp` | ✅ Done | Remote API method names |
| `share/vizd/config/config_witness.ini` | ✅ Done | Plugin names → `validator`, `validator_api`, `validator_guard` |
| `share/vizd/config/config.ini` | ✅ Done | Plugin names |
| `share/vizd/config/config_debug.ini` | ✅ Done | Plugin names |
| `share/vizd/config/config_debug_mongo.ini` | ✅ Done | Plugin names |
| `share/vizd/config/config_mongo.ini` | ✅ Done | Plugin names |
| `share/vizd/config/config_stock_exchange.ini` | ✅ Done | Plugin names |
| `share/vizd/config/config_testnet.ini` | ✅ Done | Plugin names |

---

## 10. Summary

`witness` → `validator` is the right rename. It:

- Matches XRPL, Ethereum PoS, Cosmos, and Polkadot terminology
- Accurately describes both the block production and post-validation duties
- Removes the passive/observational connotation of "witness"
- Makes `block_post_validation_object` → `validator_confirmation_object` semantically clear

**The rename is safe for unupdated JS/PHP clients** — the binary wire format uses integer type IDs, not string names, so old clients submitting transactions continue to work. A server-side name alias table handles the JSON string name fallback at zero cost. The only visible breakage for old clients is in response parsing, where they may encounter new names (`validator_update` instead of `witness_update`) in block history reads.
