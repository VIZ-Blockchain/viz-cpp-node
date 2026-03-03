# VIZ Blockchain — Transfer & Vesting Operations

Spec for implementing transfer and vesting-related operations in PHP/Node.js libraries.

---

## `transfer_operation`

**Type ID:** `2`
**Required authority:** `active` of `from` (for VIZ tokens), `master` of `from` (for SHARES)

Transfers tokens (VIZ or SHARES) from one account to another.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `from` | `account_name_type` | yes | Sending account |
| `to` | `account_name_type` | yes | Receiving account |
| `amount` | `asset` | yes | Amount to transfer (VIZ or SHARES) |
| `memo` | `string` | yes | Optional memo (plain text or encrypted) |

### JSON Example

```json
[2, {
  "from": "alice",
  "to": "bob",
  "amount": "10.000 VIZ",
  "memo": "payment for services"
}]
```

### PHP Example

```php
$op = [
    'type' => 'transfer_operation',
    'value' => [
        'from'   => 'alice',
        'to'     => 'bob',
        'amount' => '10.000 VIZ',
        'memo'   => 'payment for services',
    ],
];
```

### Node.js Example

```js
const op = ['transfer', {
    from: 'alice',
    to: 'bob',
    amount: '10.000 VIZ',
    memo: 'payment for services',
}];
```

### Checklist
- [ ] `amount.symbol` must be `VIZ` or `SHARES`
- [ ] If `amount.symbol == SHARES` → sign with **master** key
- [ ] If `amount.symbol == VIZ` → sign with **active** key
- [ ] `amount.amount` must be > 0
- [ ] `memo` may be empty string `""`
- [ ] Encrypted memo format starts with `#` followed by base58 ciphertext

---

## `transfer_to_vesting_operation`

**Type ID:** `3`
**Required authority:** `active` of `from`

Converts liquid VIZ tokens into SHARES (staking/vesting). Can vest into another account's balance.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `from` | `account_name_type` | yes | Account providing VIZ |
| `to` | `account_name_type` | yes | Account receiving SHARES (same as `from` if empty) |
| `amount` | `asset` (VIZ) | yes | Amount of VIZ to convert to SHARES |

### JSON Example

```json
[3, {
  "from": "alice",
  "to": "alice",
  "amount": "100.000 VIZ"
}]
```

### Checklist
- [ ] `amount.symbol` must be `VIZ`
- [ ] `amount.amount` must be > 0
- [ ] `to` can equal `from` (self-vesting) or be a different account
- [ ] Sign with `from`'s active key

---

## `withdraw_vesting_operation`

**Type ID:** `4`
**Required authority:** `active` of `account`

Initiates a vesting withdrawal — schedules gradual conversion of SHARES back to VIZ over multiple intervals.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `account` | `account_name_type` | yes | Account initiating withdrawal |
| `vesting_shares` | `asset` (SHARES) | yes | Total SHARES to withdraw (0 to cancel) |

### JSON Example

```json
[4, {
  "account": "alice",
  "vesting_shares": "1000.000000 SHARES"
}]
```

### Checklist
- [ ] `vesting_shares.symbol` must be `SHARES`
- [ ] Set `vesting_shares` to `"0.000000 SHARES"` to **cancel** an active withdrawal
- [ ] Withdrawal is spread over `withdraw_intervals` intervals (default 28 days)
- [ ] Each interval: `vesting_shares / withdraw_intervals` SHARES are withdrawn
- [ ] Sign with `account`'s active key

---

## `set_withdraw_vesting_route_operation`

**Type ID:** `11`
**Required authority:** `active` of `from_account`

Routes a percentage of vesting withdrawals to another account (optionally re-vesting immediately).

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `from_account` | `account_name_type` | yes | Account whose withdrawals are being routed |
| `to_account` | `account_name_type` | yes | Account receiving routed funds |
| `percent` | `uint16_t` | yes | Percentage to route (0–10000, basis points) |
| `auto_vest` | `bool` | yes | If true, immediately re-vest the routed tokens |

### JSON Example

```json
[11, {
  "from_account": "alice",
  "to_account": "bob",
  "percent": 5000,
  "auto_vest": false
}]
```

### Checklist
- [ ] `percent` range: 0 (remove route) to 10000 (100%)
- [ ] Multiple routes allowed, but total percent across all routes must be <= 10000
- [ ] `auto_vest: true` means Bob receives SHARES, not VIZ
- [ ] Set `percent: 0` to delete the route to `to_account`
- [ ] Sign with `from_account`'s active key

---

## `delegate_vesting_shares_operation`

**Type ID:** `19`
**Required authority:** `active` of `delegator`

Delegates SHARES from one account to another. The delegator retains ownership but the delegatee gains bandwidth and voting power.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `delegator` | `account_name_type` | yes | Account delegating SHARES |
| `delegatee` | `account_name_type` | yes | Account receiving delegation |
| `vesting_shares` | `asset` (SHARES) | yes | Amount to delegate (0 removes delegation) |

### JSON Example

```json
[19, {
  "delegator": "alice",
  "delegatee": "bob",
  "vesting_shares": "500.000000 SHARES"
}]
```

### PHP Example

```php
$op = [
    'type' => 'delegate_vesting_shares_operation',
    'value' => [
        'delegator'      => 'alice',
        'delegatee'      => 'bob',
        'vesting_shares' => '500.000000 SHARES',
    ],
];
```

### Node.js Example

```js
const op = ['delegate_vesting_shares', {
    delegator: 'alice',
    delegatee: 'bob',
    vesting_shares: '500.000000 SHARES',
}];
```

### Checklist
- [ ] `vesting_shares.symbol` must be `SHARES`
- [ ] Set `vesting_shares` to `"0.000000 SHARES"` to **remove** delegation
- [ ] `vesting_shares` must be >= chain `min_delegation` property (unless 0)
- [ ] When delegation is removed, SHARES enter a 1-week limbo period before returning
- [ ] Virtual operation `return_vesting_delegation_operation` fires when limbo period ends
- [ ] Sign with `delegator`'s active key
