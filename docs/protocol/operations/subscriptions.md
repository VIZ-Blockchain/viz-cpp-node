# Paid Subscription Operations

Paid subscriptions allow accounts to offer tiered recurring services payable in VIZ tokens with optional auto-renewal.

---

## `set_paid_subscription_operation` (ID 50)

**Auth:** `active` of `account`

Creates or updates a subscription offering. A `create_paid_subscription_fee` is charged on first creation.

| Field | Type | Description |
|-------|------|-------------|
| `account` | `account_name_type` | Account offering the subscription |
| `url` | `string` | URL with subscription details |
| `levels` | `uint16_t` | Number of subscription tiers (≥ 1) |
| `amount` | `asset` (VIZ) | Base price per period per unit level |
| `period` | `uint16_t` | Subscription period in days (≥ 1) |

```json
[50, {
  "account": "alice",
  "url": "https://alice.example.com/subscribe",
  "levels": 3,
  "amount": "10.000 VIZ",
  "period": 30
}]
```

- Actual cost for a subscriber = `amount × level`.
- `levels = 3` with `amount = "10.000 VIZ"` → tier 1 costs 10 VIZ, tier 2 costs 20 VIZ, tier 3 costs 30 VIZ per period.
- Updating this operation changes the parameters for future subscriptions; existing active subscriptions continue at the previous terms until renewal.

---

## `paid_subscribe_operation` (ID 51)

**Auth:** `active` of `subscriber`

Subscribes to or renews a paid subscription. Tokens transfer immediately from `subscriber` to `account`.

| Field | Type | Description |
|-------|------|-------------|
| `subscriber` | `account_name_type` | Subscribing account |
| `account` | `account_name_type` | Account offering the subscription |
| `level` | `uint16_t` | Subscription tier (1 – `levels`) |
| `amount` | `asset` (VIZ) | Payment amount |
| `period` | `uint16_t` | Number of periods to pay for |
| `auto_renewal` | `bool` | Enable automatic renewal each period |

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

- `amount` must match `subscription.amount × level × period` exactly.
- `level` must be in range [1, `subscription.levels`].
- `auto_renewal: true` — tokens are deducted automatically each period while sufficient balance exists.
- `auto_renewal: false` — one-time subscription; expires after the paid period.

**Virtual operations:**

| Virtual Op | Trigger |
|-----------|---------|
| `paid_subscription_action_operation` | Payment processed |
| `cancel_paid_subscription_operation` | Subscription expires or is cancelled |

---

See also: [Data Types](../data-types.md), [Operations Overview](./overview.md), [Virtual Operations](../virtual-operations.md).
