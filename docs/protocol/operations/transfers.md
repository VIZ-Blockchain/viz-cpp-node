# Transfer Operations

---

## `transfer_operation` (ID 2)

**Auth:** `active` of `from` (VIZ) / `master` of `from` (SHARES)

Transfers VIZ or SHARES tokens between accounts.

| Field | Type | Description |
|-------|------|-------------|
| `from` | `account_name_type` | Sending account |
| `to` | `account_name_type` | Receiving account |
| `amount` | `asset` | Amount to transfer (VIZ or SHARES) |
| `memo` | `string` | Memo text (plain or encrypted; may be `""`) |

```json
[2, {
  "from": "alice",
  "to": "bob",
  "amount": "10.000 VIZ",
  "memo": "payment for services"
}]
```

- `amount.symbol` must be `VIZ` or `SHARES`.
- VIZ transfers require **active** authority; SHARES transfers require **master** authority.
- Encrypted memo format: `#` followed by base58-encoded ciphertext.

---

## `transfer_to_vesting_operation` (ID 3)

**Auth:** `active` of `from`

Converts liquid VIZ into SHARES (staking). The SHARES can be credited to a different account.

| Field | Type | Description |
|-------|------|-------------|
| `from` | `account_name_type` | Account providing VIZ |
| `to` | `account_name_type` | Account receiving SHARES (may equal `from`) |
| `amount` | `asset` (VIZ) | Amount of VIZ to stake |

```json
[3, {
  "from": "alice",
  "to": "alice",
  "amount": "100.000 VIZ"
}]
```

- `amount.symbol` must be `VIZ`.
- `to` may be any existing account — useful for gifting staked shares.

---

## `withdraw_vesting_operation` (ID 4)

**Auth:** `active` of `account`

Initiates a gradual withdrawal of SHARES back to liquid VIZ over multiple intervals.

| Field | Type | Description |
|-------|------|-------------|
| `account` | `account_name_type` | Account initiating withdrawal |
| `vesting_shares` | `asset` (SHARES) | Total SHARES to withdraw; `0.000000 SHARES` cancels |

```json
[4, {
  "account": "alice",
  "vesting_shares": "1000.000000 SHARES"
}]
```

- Withdrawal is spread over `withdraw_intervals` intervals (chain property, default 28).
- Each interval: `vesting_shares / withdraw_intervals` SHARES are converted.
- Set to `"0.000000 SHARES"` to cancel an active withdrawal.

---

## `set_withdraw_vesting_route_operation` (ID 11)

**Auth:** `active` of `from_account`

Routes a percentage of vesting withdrawals to a specified account, optionally re-vesting the routed portion.

| Field | Type | Description |
|-------|------|-------------|
| `from_account` | `account_name_type` | Account whose withdrawals are routed |
| `to_account` | `account_name_type` | Destination account |
| `percent` | `uint16_t` | Percentage to route (0–10000 basis points) |
| `auto_vest` | `bool` | If `true`, routed tokens are immediately re-vested in `to_account` |

```json
[11, {
  "from_account": "alice",
  "to_account": "bob",
  "percent": 5000,
  "auto_vest": false
}]
```

- `percent` = 0 deletes this route to `to_account`.
- The sum of all routes from `from_account` must not exceed 10000.
- Multiple routes to different accounts are allowed.

---

## `delegate_vesting_shares_operation` (ID 19)

**Auth:** `active` of `delegator`

Delegates SHARES to another account. The delegatee gains bandwidth and voting power; ownership stays with the delegator.

| Field | Type | Description |
|-------|------|-------------|
| `delegator` | `account_name_type` | Account delegating SHARES |
| `delegatee` | `account_name_type` | Account receiving the delegation |
| `vesting_shares` | `asset` (SHARES) | Amount to delegate; `0.000000 SHARES` removes delegation |

```json
[19, {
  "delegator": "alice",
  "delegatee": "bob",
  "vesting_shares": "500.000000 SHARES"
}]
```

- `vesting_shares` must be ≥ `min_delegation` chain property, or exactly `0.000000 SHARES` to remove.
- When delegation is removed, the SHARES enter a 7-day return window before crediting back.
- Virtual `return_vesting_delegation_operation` fires when the return window ends.

---

See also: [Data Types](../data-types.md), [Operations Overview](./overview.md).
