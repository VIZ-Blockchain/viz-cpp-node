# Proposal Operations

Proposals enable multi-signature governance: one account creates a bundle of operations that require approval from a defined set of signatories before execution. The proposal executes automatically when approval is complete.

---

## `proposal_create_operation` (ID 22)

**Auth:** `active` of `author`

Creates a transaction proposal. The proposal is identified by the `author` + `title` pair.

| Field | Type | Description |
|-------|------|-------------|
| `author` | `account_name_type` | Account creating the proposal |
| `title` | `string` | Unique title per author (used as proposal ID) |
| `memo` | `string` | Human-readable description |
| `expiration_time` | `time_point_sec` | Proposal expiration time |
| `proposed_operations` | `vector<operation_wrapper>` | Operations to execute upon approval |
| `review_period_time` | `optional<time_point_sec>` | Optional: no new approvals accepted after this time |
| `extensions` | `extensions_type` | Always `[]` |

Each entry in `proposed_operations` is an `operation_wrapper`:
```json
{"op": [type_id, operation_object]}
```

```json
[22, {
  "author": "alice",
  "title": "transfer-proposal-001",
  "memo": "Joint transfer to shared fund",
  "expiration_time": "2024-12-31T23:59:59",
  "proposed_operations": [
    {
      "op": [2, {
        "from": "multisig-wallet",
        "to": "fund",
        "amount": "1000.000 VIZ",
        "memo": ""
      }]
    }
  ],
  "review_period_time": null,
  "extensions": []
}]
```

- `title` must be unique per `author`.
- `expiration_time` must be in the future.
- If `review_period_time` is set, it must be before `expiration_time`; no new approvals are accepted after this point.
- `proposed_operations` may contain multiple operations of any type.

---

## `proposal_update_operation` (ID 23)

**Auth:** Varies — determined by which approval sets are modified

Adds or removes approvals. The proposal executes automatically as soon as sufficient approvals are collected.

| Field | Type | Description |
|-------|------|-------------|
| `author` | `account_name_type` | Author of the proposal |
| `title` | `string` | Title of the proposal |
| `active_approvals_to_add` | `flat_set<account_name_type>` | Accounts granting active approval |
| `active_approvals_to_remove` | `flat_set<account_name_type>` | Accounts revoking active approval |
| `master_approvals_to_add` | `flat_set<account_name_type>` | Accounts granting master approval |
| `master_approvals_to_remove` | `flat_set<account_name_type>` | Accounts revoking master approval |
| `regular_approvals_to_add` | `flat_set<account_name_type>` | Accounts granting regular approval |
| `regular_approvals_to_remove` | `flat_set<account_name_type>` | Accounts revoking regular approval |
| `key_approvals_to_add` | `flat_set<public_key_type>` | Public keys granting approval |
| `key_approvals_to_remove` | `flat_set<public_key_type>` | Public keys revoking approval |
| `extensions` | `extensions_type` | Always `[]` |

```json
[23, {
  "author": "alice",
  "title": "transfer-proposal-001",
  "active_approvals_to_add": ["bob"],
  "active_approvals_to_remove": [],
  "master_approvals_to_add": [],
  "master_approvals_to_remove": [],
  "regular_approvals_to_add": [],
  "regular_approvals_to_remove": [],
  "key_approvals_to_add": [],
  "key_approvals_to_remove": [],
  "extensions": []
}]
```

- The transaction must be signed by the keys corresponding to the approvals being added or removed.
- All `*_to_add` and `*_to_remove` fields default to `[]` when not needed.
- After execution the proposal is resolved; further updates are rejected.

---

## `proposal_delete_operation` (ID 24)

**Auth:** `active` of `requester`

Permanently deletes (vetoes) a proposal. Can be invoked by any required authority on the proposal.

| Field | Type | Description |
|-------|------|-------------|
| `author` | `account_name_type` | Author of the proposal |
| `title` | `string` | Title of the proposal |
| `requester` | `account_name_type` | Account requesting deletion |
| `extensions` | `extensions_type` | Always `[]` |

```json
[24, {
  "author": "alice",
  "title": "transfer-proposal-001",
  "requester": "bob",
  "extensions": []
}]
```

- `requester` must be a required authority on the proposal.
- Deletion is permanent and cannot be undone.

---

See also: [Data Types](../data-types.md), [Operations Overview](./overview.md), [Database API — get_proposed_transactions](../../plugins/database-api.md#get_proposed_transactionsaccount-from-limit).
