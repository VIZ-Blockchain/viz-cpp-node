# VIZ Blockchain — Account Recovery Operations

Spec for implementing account recovery operations in PHP/Node.js libraries.

The recovery mechanism allows a trusted recovery account to help restore access to a compromised account using a previous valid master authority.

---

## Recovery Flow

```
request_account_recovery  →  recover_account  (within 24 hours)
change_recovery_account   (30-day delay)
```

---

## `request_account_recovery_operation`

**Type ID:** `12`
**Required authority:** `active` of `recovery_account`

Initiates an account recovery request. The recovery account proposes a new master authority for the compromised account. The account holder has 24 hours to confirm.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `recovery_account` | `account_name_type` | yes | The trusted recovery account |
| `account_to_recover` | `account_name_type` | yes | Compromised account to recover |
| `new_master_authority` | `authority` | yes | The new master authority to assign |
| `extensions` | `extensions_type` | yes | Always `[]` |

### JSON Example

```json
[12, {
  "recovery_account": "recover-service",
  "account_to_recover": "alice",
  "new_master_authority": {
    "weight_threshold": 1,
    "account_auths": [],
    "key_auths": [["VIZ5newkey...", 1]]
  },
  "extensions": []
}]
```

### PHP Example

```php
$op = [
    'type' => 'request_account_recovery_operation',
    'value' => [
        'recovery_account'    => 'recover-service',
        'account_to_recover'  => 'alice',
        'new_master_authority' => [
            'weight_threshold' => 1,
            'account_auths'    => [],
            'key_auths'        => [['VIZ5newkey...', 1]],
        ],
        'extensions' => [],
    ],
];
```

### Node.js Example

```js
const op = ['request_account_recovery', {
    recovery_account: 'recover-service',
    account_to_recover: 'alice',
    new_master_authority: {
        weight_threshold: 1,
        account_auths: [],
        key_auths: [['VIZ5newkey...', 1]],
    },
    extensions: [],
}];
```

### Checklist
- [ ] Only the listed recovery account of `account_to_recover` can send this
- [ ] Only one active recovery request per account at any time
- [ ] Sending again updates the request to a new authority and resets the 24h timer
- [ ] To cancel: set `new_master_authority.weight_threshold` to `0`
- [ ] Sign with `recovery_account`'s active key

---

## `recover_account_operation`

**Type ID:** `13`
**Required authority:** Both `new_master_authority` AND `recent_master_authority` signatures

Confirms account recovery. The account holder proves past ownership via a recent master authority, and takes the new master authority.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `account_to_recover` | `account_name_type` | yes | Account being recovered |
| `new_master_authority` | `authority` | yes | New master authority (must match recovery request) |
| `recent_master_authority` | `authority` | yes | A previous valid master authority (within last 30 days) |
| `extensions` | `extensions_type` | yes | Always `[]` |

### JSON Example

```json
[13, {
  "account_to_recover": "alice",
  "new_master_authority": {
    "weight_threshold": 1,
    "account_auths": [],
    "key_auths": [["VIZ5newkey...", 1]]
  },
  "recent_master_authority": {
    "weight_threshold": 1,
    "account_auths": [],
    "key_auths": [["VIZ5oldkey...", 1]]
  },
  "extensions": []
}]
```

### Checklist
- [ ] Must be broadcast within 24 hours of the recovery request
- [ ] `new_master_authority` must exactly match the one in the recovery request
- [ ] `recent_master_authority` must have been valid in the past 30 days
- [ ] Transaction must be signed by keys satisfying **both** authorities
- [ ] After recovery, the old master key is invalidated

---

## `change_recovery_account_operation`

**Type ID:** `14`
**Required authority:** `master` of `account_to_recover`

Changes the recovery account for an account. Takes effect after a 30-day delay.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `account_to_recover` | `account_name_type` | yes | Account changing its recovery account |
| `new_recovery_account` | `account_name_type` | yes | New recovery account name |
| `extensions` | `extensions_type` | yes | Always `[]` |

### JSON Example

```json
[14, {
  "account_to_recover": "alice",
  "new_recovery_account": "new-recovery-service",
  "extensions": []
}]
```

### PHP Example

```php
$op = [
    'type' => 'change_recovery_account_operation',
    'value' => [
        'account_to_recover'  => 'alice',
        'new_recovery_account' => 'new-recovery-service',
        'extensions'          => [],
    ],
];
```

### Node.js Example

```js
const op = ['change_recovery_account', {
    account_to_recover: 'alice',
    new_recovery_account: 'new-recovery-service',
    extensions: [],
}];
```

### Checklist
- [ ] 30-day delay between submitting the change and it taking effect
- [ ] This prevents attackers from changing the recovery account during an active attack
- [ ] `new_recovery_account` must be an existing account
- [ ] If `new_recovery_account == ""`, top-voted witness becomes recovery account
- [ ] Sign with `account_to_recover`'s master key
