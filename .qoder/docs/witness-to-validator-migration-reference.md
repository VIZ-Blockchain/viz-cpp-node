# VIZ Blockchain — Witness → Validator Migration Reference

## Quick Summary

The VIZ blockchain is renaming "witness" terminology to "validator" across the entire stack. This document is a reference for JS/PHP library developers to support both old and new names during migration.

**Key principle: binary wire format uses integer type IDs, not string names. Submitting transactions by integer ID never breaks. Only JSON string names change.**

---

## 0. What Libraries Must Change Themselves vs What They Just Relay

This distinction is the most important rule for library developers.

### Must change (library constructs these from scratch):

| What | Where in library code |
|------|----------------------|
| Operation name→ID mapping table | Serialization/deserialization layer |
| Field names inside type 7 and type 42 operations | Operation builders, schema definitions |
| Chain properties field names (`inflation_witness_percent`, etc.) | `chain_properties_update` builder |
| TypeScript interfaces / PHP classes for operation structs | Type definitions |
| TypeScript interfaces / PHP classes for `validator_schedule_object` | Type definitions |

### Do NOT need to change (library receives from node and relays):

| What | Reason |
|------|--------|
| Block header fields | **Done.** Node now returns `validator` / `validator_signature`. Libraries that relay raw block objects with no field-specific code need no changes. Code explicitly accessing `.witness` / `.witness_signature` on a block header must be updated to `.validator` / `.validator_signature`. |
| Raw API response objects (beyond type definitions) | JSON deserialization is dynamic — field access works regardless of name if library doesn't validate field names |
| Historical transaction data from node | Same — the node returns it, library relays it |

**Practical rule:** if your library has a hardcoded string `"witness"` as a field name when *building* a JSON object to *send* to a node — that string must be updated. If the string `"witness"` only appears in a *comment*, a *display label*, or a *type definition that only affects IDE autocomplete* — it can wait.

---

## Current Status (What Has Been Done vs What Is Planned)

| Layer | Status | Visible to JS/PHP? |
|-------|--------|---------------------|
| Internal C++ methods (e.g., `is_validator_scheduled_soon`) | **Done** | No |
| Internal enums (`block_validation_condition`) | **Done** | No |
| Internal skip flags (`skip_validator_signature`) | **Done** | No |
| Block header fields (`validator`, `validator_signature`) | **Done** | Yes — block responses |
| Dynamic global property (`current_validator`) | **Done** | Yes — `get_dynamic_global_properties` |
| Protocol operation struct names and JSON names | **Done** | Yes — JSON name in transactions |
| Operation field names inside types 7 and 42 | **Done** | Yes — field names in operation body |
| Chain properties field names | **Done** | Yes — JSON field names in governance ops |
| API method names (`get_active_validators`, etc.) | **Done** | Yes — JSON-RPC calls |
| Chain object types (`validator_object`, etc.) | **Done** | Yes — API response type names |
| CLI wallet commands (`get_active_validators`, etc.) | **Done** | Yes — if using CLI wallet |
| Physical file renames (`.hpp`/`.cpp`) | Pending — future PR | No — internal build only |
| Plugin directory and CMake target renames | Pending — future PR | Yes — config `plugin =` keys |
| Config key renames (`--validator`, etc.) | Pending — future PR | Yes — node operator config |
| API namespace (`witness_api` → `validator_api`) | Pending — future PR | Yes — JSON-RPC `"api"` field |

---

## 1. Protocol Operations (JSON-RPC Transaction Submission)

These are the operations users submit in transactions. The **integer type ID never changes** — only the JSON string name changes.

### Operations to Rename

| Type ID | Current JSON Name (old) | New JSON Name | Virtual? | Fields (unchanged) |
|---------|------------------------|---------------|----------|---------------------|
| `6` | `witness_update` | `validator_update` | no | `owner`, `url`, `block_signing_key` |
| `7` | `account_witness_vote` | `account_validator_vote` | no | `account`, **`witness` → `validator`**, `approve` |
| `8` | `account_witness_proxy` | `account_validator_proxy` | no | `account`, `proxy` |
| `30` | `shutdown_witness` | `shutdown_validator` | **yes** | `owner` |
| `42` | `witness_reward` | `validator_reward` | **yes** | **`witness` → `validator`**, `shares` |

> **Field renames inside operations:** In type 7 (`account_validator_vote`) the field `witness` (the target account name) is renamed to `validator`. In type 42 (`validator_reward`) the field `witness` is renamed to `validator`. The node accepts both old and new field names in incoming JSON, but responses use new names only.

### What JS/PHP Developers Must Handle

**Sending transactions (2 safe approaches):**

```js
// Approach A: Use integer type ID (always safe, never breaks)
const op = [6, {
    owner: 'alice',
    url: 'https://alice.example.com',
    block_signing_key: 'VIZ5hq...',
}];

// Approach B: Use string name (need to support both old and new)
const op = ['validator_update', {  // new name
    owner: 'alice',
    url: 'https://alice.example.com',
    block_signing_key: 'VIZ5hq...',
}];
```

**Sending type 7 (vote) with updated field name:**

```js
// Old (still accepted by node, but deprecated):
const op = [7, { account: 'alice', witness: 'bob', approve: true }];

// New (correct):
const op = [7, { account: 'alice', validator: 'bob', approve: true }];
```

**Receiving transactions (operation history, block parsing):**

```js
// Old server response:
["witness_update", { "owner": "alice", ... }]

// New server response:
["validator_update", { "owner": "alice", ... }]

// Your code must accept BOTH names for the same operation
```

**Server-side fallback:** The C++ node will accept both old and new JSON names in incoming transactions. But responses will use **new names only**.

### Implementation Pattern for JS/PHP

```js
// Operation name mapping (accept both, send new)
const OP_NAME_MAP = {
    'witness_update': 'validator_update',
    'account_witness_vote': 'account_validator_vote',
    'account_witness_proxy': 'account_validator_proxy',
    'shutdown_witness': 'shutdown_validator',
    'witness_reward': 'validator_reward',
};

// Reverse map (for receiving — normalize old names to new)
const OP_ALIAS_MAP = {
    'witness_update': 'validator_update',
    'account_witness_vote': 'account_validator_vote',
    'account_witness_proxy': 'account_validator_proxy',
    'shutdown_witness': 'shutdown_validator',
    'witness_reward': 'validator_reward',
};

// Type ID constants (never change)
const OP_TYPE_ID = {
    validator_update: 6,
    account_validator_vote: 7,
    account_validator_proxy: 8,
    shutdown_validator: 30,
    validator_reward: 42,
};
```

```php
// PHP equivalent
const OP_NAME_MAP = [
    'witness_update' => 'validator_update',
    'account_witness_vote' => 'account_validator_vote',
    'account_witness_proxy' => 'account_validator_proxy',
    'shutdown_witness' => 'shutdown_validator',
    'witness_reward' => 'validator_reward',
];

const OP_TYPE_ID = [
    'validator_update' => 6,
    'account_validator_vote' => 7,
    'account_validator_proxy' => 8,
    'shutdown_validator' => 30,
    'validator_reward' => 42,
];
```

---

## 2. API Methods (JSON-RPC Calls)

### API Namespace

The JSON-RPC namespace (`"api"` field) is currently **`"witness_api"`** and will be renamed to **`"validator_api"`** when the plugin directory is renamed. Until then, all calls — including new `get_validator_*` methods — must use `"witness_api"`:

```json
{ "api": "witness_api", "method": "get_active_validators", "params": [] }
```

There is no `"validator_api"` alias at this time. The namespace change will require a fallback strategy identical to the method-name fallback (try `validator_api`, fall back to `witness_api`).

### Methods to Rename

| Current Name (old) | New Name | Returns (unchanged) |
|-------------------|----------|---------------------|
| `get_active_witnesses` | `get_active_validators` | `vector<account_name_type>` |
| `get_witness_schedule` | `get_validator_schedule` | `witness_schedule_object` → `validator_schedule_object` |
| `get_witnesses` | `get_validators` | `vector<optional<witness_api_object>>` → `validator_api_object` |
| `get_witness_by_account` | `get_validator_by_account` | `optional<witness_api_object>` → `validator_api_object` |
| `get_witnesses_by_vote` | `get_validators_by_vote` | `vector<witness_api_object>` → `validator_api_object` |
| `get_witnesses_by_counted_vote` | `get_validators_by_counted_vote` | `vector<witness_api_object>` → `validator_api_object` |
| `get_witness_count` | `get_validator_count` | `uint64_t` |
| `lookup_witness_accounts` | `lookup_validator_accounts` | `set<account_name_type>` |

### What JS/PHP Developers Must Handle

**Server-side fallback:** Old method names will remain as deprecated aliases for one release cycle. Calling `get_active_witnesses` will still work but will log a deprecation warning on the server.

### Implementation Pattern for JS/PHP

```js
// Dual-support API wrapper
class VizApi {
    async getActiveValidators() {
        try {
            return await this.call('get_active_validators');
        } catch (e) {
            // Fallback to old name for older nodes
            return await this.call('get_active_witnesses');
        }
    }

    async getValidatorByAccount(account) {
        try {
            return await this.call('get_validator_by_account', [account]);
        } catch (e) {
            return await this.call('get_witness_by_account', [account]);
        }
    }

    // ... same pattern for all renamed methods
}
```

---

## 3. API Response Objects

### Object Types to Rename

| Current Name (old) | New Name | Key Fields (unchanged) |
|-------------------|----------|------------------------|
| `witness_object` | `validator_object` | `id`, `owner`, `url`, `signing_key`, `votes`, `schedule` |
| `witness_schedule_object` | `validator_schedule_object` | `current_shuffled_validators[]`, `num_scheduled` |
| `witness_api_object` | `validator_api_object` | All fields same as `witness_object` + computed fields |

### Field Renames in Response Objects

| Object | Current Field Name (old) | New Field Name |
|--------|-------------------------|----------------|
| `validator_schedule_object` | `current_shuffled_witnesses` | `current_shuffled_validators` |
| `validator_schedule_object` | `num_scheduled_witnesses` | `num_scheduled_validators` |

### What JS/PHP Developers Must Handle

```js
// Old response:
{
    "current_shuffled_witnesses": ["alice", "bob", ...],
    "num_scheduled_witnesses": 21
}

// New response:
{
    "current_shuffled_validators": ["alice", "bob", ...],
    "num_scheduled_validators": 21
}

// Safe accessor pattern
function getShuffledValidators(schedule) {
    return schedule.current_shuffled_validators
        || schedule.current_shuffled_witnesses;  // fallback for old nodes
}
```

---

## 4. Operation Field Names (Changing and Unchanged)

### Fields Being Renamed

| Field (old) | Field (new) | Operation | Note |
|-------------|-------------|-----------|------|
| `witness` | `validator` | `account_validator_vote` (type 7) | Target account name |
| `witness` | `validator` | `validator_reward` (type 42) | Virtual op — library receives, not constructs |

The node accepts both old and new field names in incoming JSON (backward compat). Responses always use new names.

### Fields That Stay Unchanged

| Field | Operation | Why It Stays |
|-------|-----------|-------------|
| `owner` | `validator_update` (type 6) | Describes the account, not the role |
| `url` | `validator_update` (type 6) | URL is a URL |
| `block_signing_key` | `validator_update` (type 6) | Describes the cryptographic key purpose |
| `account` | `account_validator_vote` (type 7) | Describes the voting account |
| `proxy` | `account_validator_proxy` (type 8) | Describes the proxy account |
| `approve` | `account_validator_vote` (type 7) | Boolean flag |
| `shares` | `validator_reward` (type 42) | Vesting shares amount |

---

## 5. Chain Properties Field Renames

**This section is critical for library developers.** The `chain_properties_update_operation` and `versioned_chain_properties_update_operation` carry governance parameters with `witness` in their names. These field names change in JSON. Libraries that construct these operations must update field names.

**Binary format is safe** — field order is preserved in binary serialization, names are not written. Only JSON field names change.

### Fields Being Renamed

| Old Field Name | New Field Name | In Struct |
|----------------|----------------|-----------|
| `inflation_witness_percent` | `inflation_validator_percent` | `chain_properties_hf4` |
| `witness_miss_penalty_percent` | `validator_miss_penalty_percent` | `chain_properties_hf6` |
| `witness_miss_penalty_duration` | `validator_miss_penalty_duration` | `chain_properties_hf6` |
| `witness_declaration_fee` | `validator_declaration_fee` | `chain_properties_hf9` |

### What JS/PHP Developers Must Handle

```js
// Old (still accepted by node with compat layer):
const props = {
    inflation_witness_percent: 1500,
    witness_miss_penalty_percent: 100,
    witness_miss_penalty_duration: 86400,
    witness_declaration_fee: { amount: '10000', asset: 'VIZ' },
};

// New (correct):
const props = {
    inflation_validator_percent: 1500,
    validator_miss_penalty_percent: 100,
    validator_miss_penalty_duration: 86400,
    validator_declaration_fee: { amount: '10000', asset: 'VIZ' },
};
```

```php
// PHP equivalent
$props = [
    'inflation_validator_percent' => 1500,
    'validator_miss_penalty_percent' => 100,
    'validator_miss_penalty_duration' => 86400,
    'validator_declaration_fee' => ['amount' => '10000', 'asset' => 'VIZ'],
];
```

---

## 6. Config Keys (For Node Operators)

Not directly relevant to JS/PHP libraries, but included for completeness:

| Current Config Key (old) | New Config Key |
|--------------------------|----------------|
| `plugin = witness` | `plugin = validator` |
| `plugin = witness_api` | `plugin = validator_api` |
| `plugin = witness_guard` | `plugin = validator_guard` |
| `--witness = "name"` | `--validator = "name"` |
| `witness-guard-enabled` | `validator-guard-enabled` |
| `witness-guard-disable` | `validator-guard-disable` |
| `witness-guard-interval` | `validator-guard-interval` |
| `witness-guard-witness` | `validator-guard-validator` |

---

## 7. CLI Wallet Commands (If Using CLI Wallet)

| Current Command (old) | New Command | Notes |
|----------------------|-------------|-------|
| `list_witnesses()` | `list_validators()` | Read-only |
| `get_witness()` | `get_validator()` | Read-only |
| `get_active_witnesses()` | `get_active_validators()` | Read-only |
| `update_witness()` | `update_validator()` | Sends type 6 |
| `vote_for_witness()` | `vote_for_validator()` | Sends type 7 |
| `set_voting_proxy()` | `set_voting_proxy()` | Command name stays, sends type 8 |

---

## 8. What NEVER Changes

| Item | Why |
|------|-----|
| Integer type IDs (6, 7, 8, 30, 42) | Binary wire format uses integer indices |
| Binary serialization of operations | Struct field order is preserved; field names are not written to binary |
| Field names `block_signing_key`, `url`, `approve`, `proxy` | Describe data, not the role |
| Null key for deactivation: `VIZ1111111111111111111111111111111114T1Anm` | Same null key format |
| Signing authority level (`active`) | Operations still require active authority |
| Block interval, slot scheduling, consensus rules | Unchanged |

> **Block header fields** are now `validator` and `validator_signature` in all node responses. Binary wire format is unchanged — field names are not serialized, only values by position. Libraries relaying raw block objects reflect new names automatically; no version negotiation needed.

---

## 9. Migration Strategy for Library Developers

### Phase A — Prepare (Before Node Upgrade)

1. Add dual-name support for operation type identification:
   - Accept both `witness_update` and `validator_update` as name for type ID 6
   - Accept both `account_witness_vote` and `account_validator_vote` as name for type ID 7
   - Same for types 8, 30, 42
2. Update field names in operation builders:
   - Type 7: accept both `witness` and `validator` for the target account field; send `validator`
   - Chain properties: add new field names; keep old for backward compat with old nodes
3. Add dual-name support for API methods:
   - Try new method name first, fall back to old name
4. Add dual field access for response objects:
   - Check `current_shuffled_validators` first, fall back to `current_shuffled_witnesses`
5. **Send transactions using integer type IDs** for maximum compatibility

### Phase B — After Node Upgrade

1. Default to new names for sending
2. Keep old name acceptance for receiving (history may contain old-format blocks)
3. Release library update with both names supported

### Phase C — Cleanup (After All Nodes Upgraded)

1. Remove old name fallbacks
2. Use only new names throughout

---

## 10. Complete Quick-Reference Table

### Operations

| Type ID | Old JSON Name | New JSON Name | Field changes |
|---------|--------------|---------------|---------------|
| 6 | `witness_update` | `validator_update` | none |
| 7 | `account_witness_vote` | `account_validator_vote` | `witness` → `validator` |
| 8 | `account_witness_proxy` | `account_validator_proxy` | none |
| 30 | `shutdown_witness` | `shutdown_validator` | none |
| 42 | `witness_reward` | `validator_reward` | `witness` → `validator` |

### API Methods

| Old Name | New Name |
|----------|----------|
| `get_active_witnesses` | `get_active_validators` |
| `get_witness_schedule` | `get_validator_schedule` |
| `get_witnesses` | `get_validators` |
| `get_witness_by_account` | `get_validator_by_account` |
| `get_witnesses_by_vote` | `get_validators_by_vote` |
| `get_witnesses_by_counted_vote` | `get_validators_by_counted_vote` |
| `get_witness_count` | `get_validator_count` |
| `lookup_witness_accounts` | `lookup_validator_accounts` |

### Response Fields

| Old Field | New Field | In Object |
|-----------|-----------|-----------|
| `current_shuffled_witnesses` | `current_shuffled_validators` | `validator_schedule_object` |
| `num_scheduled_witnesses` | `num_scheduled_validators` | `validator_schedule_object` |

### Block Header Fields

| Old Field | New Field |
|-----------|-----------|
| `witness` | `validator` |
| `witness_signature` | `validator_signature` |

### Dynamic Global Property Fields

| Old Field | New Field |
|-----------|-----------|
| `current_witness` | `current_validator` |

### Chain Properties Fields

| Old Field | New Field | In Struct |
|-----------|-----------|-----------|
| `inflation_witness_percent` | `inflation_validator_percent` | `chain_properties_hf4` |
| `witness_miss_penalty_percent` | `validator_miss_penalty_percent` | `chain_properties_hf6` |
| `witness_miss_penalty_duration` | `validator_miss_penalty_duration` | `chain_properties_hf6` |
| `witness_declaration_fee` | `validator_declaration_fee` | `chain_properties_hf9` |
