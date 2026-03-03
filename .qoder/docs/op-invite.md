# VIZ Blockchain — Invite Operations

Spec for implementing invite-related operations in PHP/Node.js libraries.

Invites allow existing VIZ users to onboard new users without requiring them to have an existing account. An invite is a one-time-use key with a VIZ balance.

---

## `create_invite_operation`

**Type ID:** `43`
**Required authority:** `active` of `creator`

Creates an invite link by generating a key and funding it with VIZ tokens.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `creator` | `account_name_type` | yes | Account creating the invite |
| `balance` | `asset` (VIZ) | yes | Amount of VIZ to lock in the invite |
| `invite_key` | `public_key_type` | yes | Public key of the invite (secret = private key) |

### JSON Example

```json
[43, {
  "creator": "alice",
  "balance": "5.000 VIZ",
  "invite_key": "VIZ5invite..."
}]
```

### PHP Example

```php
// Generate invite key pair first:
// $privateKey = '...' // keep secret, share as invite link
// $publicKey  = derived from privateKey

$op = [
    'type' => 'create_invite_operation',
    'value' => [
        'creator'    => 'alice',
        'balance'    => '5.000 VIZ',
        'invite_key' => 'VIZ5invite...',
    ],
];
```

### Node.js Example

```js
const op = ['create_invite', {
    creator: 'alice',
    balance: '5.000 VIZ',
    invite_key: 'VIZ5invite...',
}];
```

### Checklist
- [ ] `balance.symbol` must be `VIZ`
- [ ] `balance.amount` >= chain `create_invite_min_balance` property
- [ ] Generate a random secp256k1 key pair for the invite
- [ ] Private key = invite secret (share in invite link)
- [ ] Public key = `invite_key` field
- [ ] Sign with `creator`'s active key

---

## `claim_invite_balance_operation`

**Type ID:** `44`
**Required authority:** `active` of `initiator`

Claims the VIZ balance from an invite, transferring it to `receiver`. The invite is consumed.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `initiator` | `account_name_type` | yes | Account claiming the invite |
| `receiver` | `account_name_type` | yes | Account to receive the VIZ balance |
| `invite_secret` | `string` | yes | WIF private key of the invite |

### JSON Example

```json
[44, {
  "initiator": "bob",
  "receiver": "bob",
  "invite_secret": "5Ky1MXn..."
}]
```

### PHP Example

```php
$op = [
    'type' => 'claim_invite_balance_operation',
    'value' => [
        'initiator'     => 'bob',
        'receiver'      => 'bob',
        'invite_secret' => '5Ky1MXn...',
    ],
];
```

### Node.js Example

```js
const op = ['claim_invite_balance', {
    initiator: 'bob',
    receiver: 'bob',
    invite_secret: '5Ky1MXn...',
}];
```

### Checklist
- [ ] `invite_secret` is the WIF (Wallet Import Format) private key of the invite
- [ ] `initiator` must be an existing account
- [ ] `receiver` may differ from `initiator` (can redirect balance to another account)
- [ ] Invite is consumed after claiming — cannot be reused
- [ ] Sign with `initiator`'s active key

---

## `invite_registration_operation`

**Type ID:** `45`
**Required authority:** `active` of `initiator`

Uses an invite to create a new account. The invite balance is used to fund the new account.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `initiator` | `account_name_type` | yes | Existing account triggering registration |
| `new_account_name` | `account_name_type` | yes | Name for the new account |
| `invite_secret` | `string` | yes | WIF private key of the invite |
| `new_account_key` | `public_key_type` | yes | Master/active/regular/memo key for new account |

### JSON Example

```json
[45, {
  "initiator": "bob",
  "new_account_name": "carol",
  "invite_secret": "5Ky1MXn...",
  "new_account_key": "VIZ5newacct..."
}]
```

### PHP Example

```php
$op = [
    'type' => 'invite_registration_operation',
    'value' => [
        'initiator'        => 'bob',
        'new_account_name' => 'carol',
        'invite_secret'    => '5Ky1MXn...',
        'new_account_key'  => 'VIZ5newacct...',
    ],
];
```

### Node.js Example

```js
const op = ['invite_registration', {
    initiator: 'bob',
    new_account_name: 'carol',
    invite_secret: '5Ky1MXn...',
    new_account_key: 'VIZ5newacct...',
}];
```

### Checklist
- [ ] `invite_secret` is the WIF private key of the invite
- [ ] `new_account_name` must pass account name validation
- [ ] `new_account_key` is set as all four keys (master, active, regular, memo) for the new account
- [ ] Invite balance is converted to SHARES for the new account
- [ ] Invite is consumed after use
- [ ] Sign with `initiator`'s active key

---

## `use_invite_balance_operation`

**Type ID:** `58`
**Required authority:** `active` of `initiator`

Alternative to `claim_invite_balance_operation` — transfers invite balance to receiver. The difference from `claim_invite_balance_operation` is that this one may convert balance to SHARES.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `initiator` | `account_name_type` | yes | Account using the invite |
| `receiver` | `account_name_type` | yes | Account receiving the balance |
| `invite_secret` | `string` | yes | WIF private key of the invite |

### JSON Example

```json
[58, {
  "initiator": "bob",
  "receiver": "bob",
  "invite_secret": "5Ky1MXn..."
}]
```

### Checklist
- [ ] `invite_secret` is the WIF private key
- [ ] `receiver` must be an existing account
- [ ] Invite is consumed after use
- [ ] Sign with `initiator`'s active key
