# VIZ Blockchain — Account Operations

Spec for implementing account-related operations in PHP/Node.js libraries.

---

## `account_create_operation`

**Type ID:** `20`
**Required authority:** `active` of `creator`

Creates a new blockchain account.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `fee` | `asset` (VIZ) | yes | Creation fee (converted to SHARES for new account) |
| `delegation` | `asset` (SHARES) | yes | Initial SHARES delegation to new account |
| `creator` | `account_name_type` | yes | Account paying the fee and creating |
| `new_account_name` | `account_name_type` | yes | Name for the new account |
| `master` | `authority` | yes | Master authority for new account |
| `active` | `authority` | yes | Active authority for new account |
| `regular` | `authority` | yes | Regular authority for new account |
| `memo_key` | `public_key_type` | yes | Memo public key |
| `json_metadata` | `string` | yes | JSON metadata (may be empty `""`) |
| `referrer` | `account_name_type` | yes | Referrer account name (may be empty `""`) |
| `extensions` | `extensions_type` | yes | Always `[]` |

### JSON Example

```json
[20, {
  "fee": "1.000 VIZ",
  "delegation": "10.000000 SHARES",
  "creator": "alice",
  "new_account_name": "bob",
  "master": {
    "weight_threshold": 1,
    "account_auths": [],
    "key_auths": [["VIZ5hqSa4NkEZGAMUpoH5EaEr64mBJuMcPpGjvk8qb7hcPFTbXSQ9", 1]]
  },
  "active": {
    "weight_threshold": 1,
    "account_auths": [],
    "key_auths": [["VIZ5hqSa4NkEZGAMUpoH5EaEr64mBJuMcPpGjvk8qb7hcPFTbXSQ9", 1]]
  },
  "regular": {
    "weight_threshold": 1,
    "account_auths": [],
    "key_auths": [["VIZ5hqSa4NkEZGAMUpoH5EaEr64mBJuMcPpGjvk8qb7hcPFTbXSQ9", 1]]
  },
  "memo_key": "VIZ5hqSa4NkEZGAMUpoH5EaEr64mBJuMcPpGjvk8qb7hcPFTbXSQ9",
  "json_metadata": "",
  "referrer": "",
  "extensions": []
}]
```

### PHP Example

```php
$op = [
    'type' => 'account_create_operation',
    'value' => [
        'fee'              => '1.000 VIZ',
        'delegation'       => '10.000000 SHARES',
        'creator'          => 'alice',
        'new_account_name' => 'bob',
        'master'  => ['weight_threshold' => 1, 'account_auths' => [], 'key_auths' => [['VIZ5hq...', 1]]],
        'active'  => ['weight_threshold' => 1, 'account_auths' => [], 'key_auths' => [['VIZ5hq...', 1]]],
        'regular' => ['weight_threshold' => 1, 'account_auths' => [], 'key_auths' => [['VIZ5hq...', 1]]],
        'memo_key'      => 'VIZ5hq...',
        'json_metadata' => '',
        'referrer'      => '',
        'extensions'    => [],
    ],
];
```

### Node.js Example

```js
const op = ['account_create', {
    fee: '1.000 VIZ',
    delegation: '10.000000 SHARES',
    creator: 'alice',
    new_account_name: 'bob',
    master:  { weight_threshold: 1, account_auths: [], key_auths: [['VIZ5hq...', 1]] },
    active:  { weight_threshold: 1, account_auths: [], key_auths: [['VIZ5hq...', 1]] },
    regular: { weight_threshold: 1, account_auths: [], key_auths: [['VIZ5hq...', 1]] },
    memo_key: 'VIZ5hq...',
    json_metadata: '',
    referrer: '',
    extensions: [],
}];
```

### Checklist
- [ ] `fee.symbol` must be `VIZ`
- [ ] `delegation.symbol` must be `SHARES`
- [ ] `new_account_name` must pass `is_valid_create_account_name` validation
- [ ] All three authorities must be provided (even if identical)
- [ ] `memo_key` must be a valid VIZ public key
- [ ] `fee` >= chain `account_creation_fee` property
- [ ] Sign with `creator`'s active key

---

## `account_update_operation`

**Type ID:** `5`
**Required authority:** `master` of `account` (if changing master key), else `active`

Updates account keys and metadata.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `account` | `account_name_type` | yes | Account to update |
| `master` | `optional<authority>` | no | New master authority (triggers master-auth requirement) |
| `active` | `optional<authority>` | no | New active authority |
| `regular` | `optional<authority>` | no | New regular authority |
| `memo_key` | `public_key_type` | yes | New memo key |
| `json_metadata` | `string` | yes | New JSON metadata |

### JSON Example

```json
[5, {
  "account": "alice",
  "active": {
    "weight_threshold": 1,
    "account_auths": [],
    "key_auths": [["VIZ5new...", 1]]
  },
  "memo_key": "VIZ5new...",
  "json_metadata": "{\"profile\":\"updated\"}"
}]
```

### Checklist
- [ ] `master` field is **optional** — omit (or set to `null`) if not changing master key
- [ ] If `master` is present → sign with current **master** key
- [ ] If `master` is absent → sign with current **active** key
- [ ] `memo_key` is always required (even if unchanged)

---

## `account_metadata_operation`

**Type ID:** `21`
**Required authority:** `regular` of `account`

Updates only the account's JSON metadata. Cheaper in bandwidth than full `account_update`.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `account` | `account_name_type` | yes | Account to update |
| `json_metadata` | `string` | yes | New JSON metadata string |

### JSON Example

```json
[21, {
  "account": "alice",
  "json_metadata": "{\"name\":\"Alice\",\"about\":\"Hello!\"}"
}]
```

### PHP Example

```php
$op = [
    'type' => 'account_metadata_operation',
    'value' => [
        'account'       => 'alice',
        'json_metadata' => json_encode(['name' => 'Alice', 'about' => 'Hello!']),
    ],
];
```

### Node.js Example

```js
const op = ['account_metadata', {
    account: 'alice',
    json_metadata: JSON.stringify({ name: 'Alice', about: 'Hello!' }),
}];
```

### Checklist
- [ ] `json_metadata` must be valid UTF-8
- [ ] Max metadata size limited by bandwidth
- [ ] Sign with `account`'s regular key
- [ ] Faster/cheaper than `account_update` for metadata-only changes
