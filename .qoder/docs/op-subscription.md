# VIZ Blockchain — Paid Subscription Operations

Spec for implementing paid subscription operations in PHP/Node.js libraries.

Paid subscriptions allow accounts to offer tiered subscription services payable in VIZ tokens, with optional auto-renewal.

---

## `set_paid_subscription_operation`

**Type ID:** `50`
**Required authority:** `active` of `account`

Creates or updates a paid subscription offering for an account. Subscribers can then subscribe via `paid_subscribe_operation`.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `account` | `account_name_type` | yes | Account offering the subscription |
| `url` | `string` | yes | URL with subscription details |
| `levels` | `uint16_t` | yes | Number of subscription tiers (1–N) |
| `amount` | `asset` (VIZ) | yes | Price per period per unit level |
| `period` | `uint16_t` | yes | Subscription period in days |

### JSON Example

```json
[50, {
  "account": "alice",
  "url": "https://alice.example.com/subscribe",
  "levels": 3,
  "amount": "10.000 VIZ",
  "period": 30
}]
```

### PHP Example

```php
$op = [
    'type' => 'set_paid_subscription_operation',
    'value' => [
        'account' => 'alice',
        'url'     => 'https://alice.example.com/subscribe',
        'levels'  => 3,
        'amount'  => '10.000 VIZ',
        'period'  => 30,
    ],
];
```

### Node.js Example

```js
const op = ['set_paid_subscription', {
    account: 'alice',
    url: 'https://alice.example.com/subscribe',
    levels: 3,
    amount: '10.000 VIZ',
    period: 30,
}];
```

### Checklist
- [ ] `amount.symbol` must be `VIZ`
- [ ] `amount.amount` must be > 0
- [ ] `levels` must be >= 1
- [ ] `period` must be >= 1 (days)
- [ ] Fee (`create_paid_subscription_fee`) charged on first creation
- [ ] Actual subscription cost = `amount * level` per period
- [ ] Sign with `account`'s active key

---

## `paid_subscribe_operation`

**Type ID:** `51`
**Required authority:** `active` of `subscriber`

Subscribes to or renews a paid subscription. Tokens are transferred from `subscriber` to `account`.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `subscriber` | `account_name_type` | yes | Subscribing account |
| `account` | `account_name_type` | yes | Account offering the subscription |
| `level` | `uint16_t` | yes | Subscription tier (1–max_levels) |
| `amount` | `asset` (VIZ) | yes | Payment amount |
| `period` | `uint16_t` | yes | Number of periods to subscribe |
| `auto_renewal` | `bool` | yes | Whether to auto-renew |

### JSON Example

```json
[51, {
  "subscriber": "bob",
  "account": "alice",
  "level": 2,
  "amount": "20.000 VIZ",
  "period": 1,
  "auto_renewal": true
}]
```

### PHP Example

```php
$op = [
    'type' => 'paid_subscribe_operation',
    'value' => [
        'subscriber'   => 'bob',
        'account'      => 'alice',
        'level'        => 2,
        'amount'       => '20.000 VIZ',
        'period'       => 1,
        'auto_renewal' => true,
    ],
];
```

### Node.js Example

```js
const op = ['paid_subscribe', {
    subscriber: 'bob',
    account: 'alice',
    level: 2,
    amount: '20.000 VIZ',
    period: 1,
    auto_renewal: true,
}];
```

### Checklist
- [ ] `amount.symbol` must be `VIZ`
- [ ] `amount` must match `subscription_amount * level * period`
- [ ] `level` must be in range [1, subscription.levels]
- [ ] `period` >= 1
- [ ] `auto_renewal: true` → tokens deducted automatically each period
- [ ] `auto_renewal: false` → one-time subscription
- [ ] If already subscribed: upgrading level requires matching payment for remaining time difference
- [ ] Virtual `paid_subscription_action_operation` fires on payment
- [ ] Virtual `cancel_paid_subscription_operation` fires on cancellation/expiry
- [ ] Sign with `subscriber`'s active key
