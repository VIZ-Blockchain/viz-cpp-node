# Account Market Operations

The account market allows accounts and subaccount namespaces to be listed for sale and purchased on-chain.

---

## `set_account_price_operation` (ID 54)

**Auth:** `master` of `account`

Lists an account for public sale or updates the listing. An `account_on_sale_fee` is charged on listing.

| Field | Type | Description |
|-------|------|-------------|
| `account` | `account_name_type` | Account being listed |
| `account_seller` | `account_name_type` | Account to receive the payment (may differ from `account`) |
| `account_offer_price` | `asset` (VIZ) | Asking price |
| `account_on_sale` | `bool` | `true` to list; `false` to delist |

```json
[54, {
  "account": "alice",
  "account_seller": "alice",
  "account_offer_price": "1000.000 VIZ",
  "account_on_sale": true
}]
```

- `account_on_sale: false` delists without a fee refund.
- `account_seller` can be any account — useful for brokered sales.

---

## `set_subaccount_price_operation` (ID 55)

**Auth:** `master` of `account`

Lists the right to create subaccounts (e.g., `account.childname`) for sale. A `subaccount_on_sale_fee` is charged on listing.

| Field | Type | Description |
|-------|------|-------------|
| `account` | `account_name_type` | Parent account |
| `subaccount_seller` | `account_name_type` | Account to receive payments |
| `subaccount_offer_price` | `asset` (VIZ) | Price per subaccount creation right |
| `subaccount_on_sale` | `bool` | `true` to list; `false` to delist |

```json
[55, {
  "account": "alice",
  "subaccount_seller": "alice",
  "subaccount_offer_price": "50.000 VIZ",
  "subaccount_on_sale": true
}]
```

- Buyers purchase the right to create one subaccount under `account`'s namespace per transaction.

---

## `buy_account_operation` (ID 56)

**Auth:** `active` of `buyer`

Purchases an account currently listed for sale. All authorities are transferred to the buyer.

| Field | Type | Description |
|-------|------|-------------|
| `buyer` | `account_name_type` | Purchasing account |
| `account` | `account_name_type` | Account being purchased |
| `account_offer_price` | `asset` (VIZ) | Purchase price (must exactly match the listing) |
| `account_authorities_key` | `public_key_type` | New key set as master, active, regular, and memo of the purchased account |
| `tokens_to_shares` | `asset` (VIZ) | Additional VIZ to convert to SHARES for the bought account (may be `"0.000 VIZ"`) |

```json
[56, {
  "buyer": "bob",
  "account": "alice",
  "account_offer_price": "1000.000 VIZ",
  "account_authorities_key": "VIZ5newowner...",
  "tokens_to_shares": "0.000 VIZ"
}]
```

- `account_offer_price` must exactly match the price in `set_account_price_operation`.
- `account_authorities_key` is applied to all four authority slots simultaneously.
- Payment is sent to `account_seller` as specified in the listing.
- Virtual `account_sale_operation` fires on successful purchase.

---

## `target_account_sale_operation` (ID 61)

**Auth:** `master` of `account`

Lists an account for private (targeted) sale to a specific buyer only. A buyer other than `target_buyer` cannot purchase this listing.

| Field | Type | Description |
|-------|------|-------------|
| `account` | `account_name_type` | Account being listed |
| `account_seller` | `account_name_type` | Account to receive payment |
| `target_buyer` | `account_name_type` | Only this account may buy |
| `account_offer_price` | `asset` (VIZ) | Asking price |
| `account_on_sale` | `bool` | `true` to list; `false` to delist |

```json
[61, {
  "account": "alice",
  "account_seller": "alice",
  "target_buyer": "charlie",
  "account_offer_price": "500.000 VIZ",
  "account_on_sale": true
}]
```

- `account_on_sale: false` cancels the targeted listing.
- The buyer uses the standard `buy_account_operation` to complete the purchase.

---

See also: [Data Types](../data-types.md), [Operations Overview](./overview.md), [Virtual Operations](../virtual-operations.md), [Database API — Account Market](../../plugins/database-api.md#account-market).
