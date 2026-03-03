# VIZ Blockchain — Proposal Operations

Spec for implementing multi-signature proposal operations in PHP/Node.js libraries.

Proposals allow a group of accounts to jointly approve and execute a set of operations. Any account can create a proposal; signatories approve via `proposal_update_operation`.

---

## `proposal_create_operation`

**Type ID:** `22`
**Required authority:** `active` of `author`

Creates a transaction proposal containing one or more operations that require multi-sig approval.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `author` | `account_name_type` | yes | Account creating the proposal |
| `title` | `string` | yes | Unique title per author (used as proposal ID) |
| `memo` | `string` | yes | Description of the proposal |
| `expiration_time` | `time_point_sec` | yes | Proposal expiration time |
| `proposed_operations` | `vector<operation_wrapper>` | yes | Operations in the proposal |
| `review_period_time` | `optional<time_point_sec>` | no | Optional review period |
| `extensions` | `extensions_type` | yes | Always `[]` |

### `operation_wrapper`

Each entry in `proposed_operations` is an `operation_wrapper`:
```json
{"op": [type_id, operation_object]}
```

### JSON Example

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

### PHP Example

```php
$op = [
    'type' => 'proposal_create_operation',
    'value' => [
        'author'          => 'alice',
        'title'           => 'transfer-proposal-001',
        'memo'            => 'Joint transfer to shared fund',
        'expiration_time' => '2024-12-31T23:59:59',
        'proposed_operations' => [
            ['op' => [2, [
                'from'   => 'multisig-wallet',
                'to'     => 'fund',
                'amount' => '1000.000 VIZ',
                'memo'   => '',
            ]]],
        ],
        'review_period_time' => null,
        'extensions'         => [],
    ],
];
```

### Node.js Example

```js
const op = ['proposal_create', {
    author: 'alice',
    title: 'transfer-proposal-001',
    memo: 'Joint transfer to shared fund',
    expiration_time: '2024-12-31T23:59:59',
    proposed_operations: [
        { op: ['transfer', {
            from: 'multisig-wallet',
            to: 'fund',
            amount: '1000.000 VIZ',
            memo: '',
        }] },
    ],
    review_period_time: null,
    extensions: [],
}];
```

### Checklist
- [ ] `title` must be unique per `author` (together they form the proposal ID)
- [ ] `expiration_time` must be in the future
- [ ] `review_period_time` if set, must be before `expiration_time`
- [ ] `proposed_operations` may contain multiple operations
- [ ] Operations inside a proposal follow the same rules as normal operations
- [ ] Sign with `author`'s active key

---

## `proposal_update_operation`

**Type ID:** `23`
**Required authority:** Depends on which approval sets are modified

Adds or removes approvals from a proposal. Proposal executes automatically when enough approvals are collected.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `author` | `account_name_type` | yes | Author of the proposal |
| `title` | `string` | yes | Title of the proposal |
| `active_approvals_to_add` | `flat_set<account_name_type>` | yes | Accounts adding active approval |
| `active_approvals_to_remove` | `flat_set<account_name_type>` | yes | Accounts removing active approval |
| `master_approvals_to_add` | `flat_set<account_name_type>` | yes | Accounts adding master approval |
| `master_approvals_to_remove` | `flat_set<account_name_type>` | yes | Accounts removing master approval |
| `regular_approvals_to_add` | `flat_set<account_name_type>` | yes | Accounts adding regular approval |
| `regular_approvals_to_remove` | `flat_set<account_name_type>` | yes | Accounts removing regular approval |
| `key_approvals_to_add` | `flat_set<public_key_type>` | yes | Keys adding approval |
| `key_approvals_to_remove` | `flat_set<public_key_type>` | yes | Keys removing approval |
| `extensions` | `extensions_type` | yes | Always `[]` |

### JSON Example

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

### PHP Example

```php
$op = [
    'type' => 'proposal_update_operation',
    'value' => [
        'author'                    => 'alice',
        'title'                     => 'transfer-proposal-001',
        'active_approvals_to_add'   => ['bob'],
        'active_approvals_to_remove'=> [],
        'master_approvals_to_add'   => [],
        'master_approvals_to_remove'=> [],
        'regular_approvals_to_add'  => [],
        'regular_approvals_to_remove'=> [],
        'key_approvals_to_add'      => [],
        'key_approvals_to_remove'   => [],
        'extensions'                => [],
    ],
];
```

### Checklist
- [ ] Transaction must be signed by keys satisfying the authorities being added/removed
- [ ] If proposal requires only active authority, do NOT add master authority
- [ ] If both master and active are required, only master can approve
- [ ] Proposal executes automatically when approval threshold is reached
- [ ] After successful execution, proposal is resolved and further updates are rejected
- [ ] All `*_to_add` and `*_to_remove` arrays default to `[]` if not needed

---

## `proposal_delete_operation`

**Type ID:** `24`
**Required authority:** `active` of `requester`

Vetoes and permanently deletes a proposal. Can be done by any required authority.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `author` | `account_name_type` | yes | Author of the proposal |
| `title` | `string` | yes | Title of the proposal |
| `requester` | `account_name_type` | yes | Account requesting deletion |
| `extensions` | `extensions_type` | yes | Always `[]` |

### JSON Example

```json
[24, {
  "author": "alice",
  "title": "transfer-proposal-001",
  "requester": "bob",
  "extensions": []
}]
```

### PHP Example

```php
$op = [
    'type' => 'proposal_delete_operation',
    'value' => [
        'author'     => 'alice',
        'title'      => 'transfer-proposal-001',
        'requester'  => 'bob',
        'extensions' => [],
    ],
];
```

### Checklist
- [ ] `requester` must be a required authority on the proposal
- [ ] Permanently removes the proposal — cannot be undone
- [ ] Sign with `requester`'s active key
