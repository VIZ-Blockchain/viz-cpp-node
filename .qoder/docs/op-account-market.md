# VIZ Blockchain — Account Market Operations

Spec for implementing account sale operations in PHP/Node.js libraries.

Account market operations allow accounts to be bought and sold. An account owner sets their account for sale, and a buyer can purchase it.

---

## `set_account_price_operation`

**Type ID:** `54`
**Required authority:** `master` of `account`

Sets an account for sale or updates its sale parameters.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `account` | `account_name_type` | yes | Account being listed for sale |
| `account_seller` | `account_name_type` | yes | Account to receive payment |
| `account_offer_price` | `asset` (VIZ) | yes | Asking price |
| `account_on_sale` | `bool` | yes | `true` to list, `false` to delist |

### JSON Example

```json
[54, {
  "account": "alice",
  "account_seller": "alice",
  "account_offer_price": "1000.000 VIZ",
  "account_on_sale": true
}]
```

### PHP Example

```php
$op = [
    'type' => 'set_account_price_operation',
    'value' => [
        'account'             => 'alice',
        'account_seller'      => 'alice',
        'account_offer_price' => '1000.000 VIZ',
        'account_on_sale'     => true,
    ],
];
```

### Node.js Example

```js
const op = ['set_account_price', {
    account: 'alice',
    account_seller: 'alice',
    account_offer_price: '1000.000 VIZ',
    account_on_sale: true,
}];
```

### Checklist
- [ ] `account_offer_price.symbol` must be `VIZ`
- [ ] `account_offer_price.amount` > 0
- [ ] `account_on_sale: false` delists the account
- [ ] Fee (`account_on_sale_fee`) is charged when listing
- [ ] `account_seller` can differ from `account` (payment redirected)
- [ ] Sign with `account`'s master key

---

## `set_subaccount_price_operation`

**Type ID:** `55`
**Required authority:** `master` of `account`

Lists the right to create subaccounts of `account` for sale. A "subaccount" of `alice` would be `alice.bob`.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `account` | `account_name_type` | yes | Parent account |
| `subaccount_seller` | `account_name_type` | yes | Account to receive payment |
| `subaccount_offer_price` | `asset` (VIZ) | yes | Price per subaccount creation |
| `subaccount_on_sale` | `bool` | yes | `true` to list, `false` to delist |

### JSON Example

```json
[55, {
  "account": "alice",
  "subaccount_seller": "alice",
  "subaccount_offer_price": "50.000 VIZ",
  "subaccount_on_sale": true
}]
```

### PHP Example

```php
$op = [
    'type' => 'set_subaccount_price_operation',
    'value' => [
        'account'                => 'alice',
        'subaccount_seller'      => 'alice',
        'subaccount_offer_price' => '50.000 VIZ',
        'subaccount_on_sale'     => true,
    ],
];
```

### Checklist
- [ ] `subaccount_offer_price.symbol` must be `VIZ`
- [ ] `subaccount_on_sale: false` delists
- [ ] Fee (`subaccount_on_sale_fee`) is charged when listing
- [ ] Sign with `account`'s master key

---

## `buy_account_operation`

**Type ID:** `56`
**Required authority:** `active` of `buyer`

Purchases an account that is listed for sale. All authorities are transferred to the buyer.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `buyer` | `account_name_type` | yes | Purchasing account |
| `account` | `account_name_type` | yes | Account being purchased |
| `account_offer_price` | `asset` (VIZ) | yes | Purchase price (must match listing) |
| `account_authorities_key` | `public_key_type` | yes | New key set as all authorities of purchased account |
| `tokens_to_shares` | `asset` (VIZ) | yes | Additional VIZ to convert to SHARES for bought account |

### JSON Example

```json
[56, {
  "buyer": "bob",
  "account": "alice",
  "account_offer_price": "1000.000 VIZ",
  "account_authorities_key": "VIZ5newowner...",
  "tokens_to_shares": "0.000 VIZ"
}]
```

### PHP Example

```php
$op = [
    'type' => 'buy_account_operation',
    'value' => [
        'buyer'                   => 'bob',
        'account'                 => 'alice',
        'account_offer_price'     => '1000.000 VIZ',
        'account_authorities_key' => 'VIZ5newowner...',
        'tokens_to_shares'        => '0.000 VIZ',
    ],
];
```

### Node.js Example

```js
const op = ['buy_account', {
    buyer: 'bob',
    account: 'alice',
    account_offer_price: '1000.000 VIZ',
    account_authorities_key: 'VIZ5newowner...',
    tokens_to_shares: '0.000 VIZ',
}];
```

### Checklist
- [ ] `account` must be currently listed for sale (`account_on_sale: true`)
- [ ] `account_offer_price` must exactly match the listed price
- [ ] `account_authorities_key` is set as master, active, regular, and memo key
- [ ] `tokens_to_shares.symbol` must be `VIZ`
- [ ] `tokens_to_shares.amount` >= 0 (can be 0)
- [ ] Payment goes to `account_seller` as specified in the listing
- [ ] Virtual `account_sale_operation` fires on successful purchase
- [ ] Sign with `buyer`'s active key

---

## `target_account_sale_operation`

**Type ID:** `61`
**Required authority:** `master` of `account`

Lists an account for sale to a specific buyer only (private/targeted sale). Added in HF11.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `account` | `account_name_type` | yes | Account being listed |
| `account_seller` | `account_name_type` | yes | Account to receive payment |
| `target_buyer` | `account_name_type` | yes | Only this account can buy |
| `account_offer_price` | `asset` (VIZ) | yes | Asking price |
| `account_on_sale` | `bool` | yes | `true` to list, `false` to delist |

### JSON Example

```json
[61, {
  "account": "alice",
  "account_seller": "alice",
  "target_buyer": "charlie",
  "account_offer_price": "500.000 VIZ",
  "account_on_sale": true
}]
```

### PHP Example

```php
$op = [
    'type' => 'target_account_sale_operation',
    'value' => [
        'account'             => 'alice',
        'account_seller'      => 'alice',
        'target_buyer'        => 'charlie',
        'account_offer_price' => '500.000 VIZ',
        'account_on_sale'     => true,
    ],
];
```

### Checklist
- [ ] Only `target_buyer` can purchase this account via `buy_account_operation`
- [ ] `account_offer_price.symbol` must be `VIZ`
- [ ] `account_on_sale: false` delists the targeted sale
- [ ] Sign with `account`'s master key
