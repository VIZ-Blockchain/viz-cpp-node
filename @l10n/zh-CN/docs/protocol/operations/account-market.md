# 账户市场操作

账户市场允许在链上挂牌出售和购买账户及子账户命名空间。

---

## `set_account_price_operation`（ID 54）

**授权：** `account` 的 `master`

将账户挂牌公开出售或更新挂牌信息。挂牌时收取 `account_on_sale_fee`。

| 字段 | 类型 | 描述 |
|------|------|------|
| `account` | `account_name_type` | 挂牌出售的账户 |
| `account_seller` | `account_name_type` | 接收付款的账户（可与 `account` 不同） |
| `account_offer_price` | `asset`（VIZ） | 要价 |
| `account_on_sale` | `bool` | `true` 挂牌；`false` 撤牌 |

```json
[54, {
  "account": "alice",
  "account_seller": "alice",
  "account_offer_price": "1000.000 VIZ",
  "account_on_sale": true
}]
```

- `account_on_sale: false` 撤牌但不退还手续费。
- `account_seller` 可以是任意账户，适用于经纪销售。

---

## `set_subaccount_price_operation`（ID 55）

**授权：** `account` 的 `master`

将创建子账户的权利（如 `account.childname`）挂牌出售。挂牌时收取 `subaccount_on_sale_fee`。

| 字段 | 类型 | 描述 |
|------|------|------|
| `account` | `account_name_type` | 父账户 |
| `subaccount_seller` | `account_name_type` | 接收付款的账户 |
| `subaccount_offer_price` | `asset`（VIZ） | 每次创建子账户权利的价格 |
| `subaccount_on_sale` | `bool` | `true` 挂牌；`false` 撤牌 |

```json
[55, {
  "account": "alice",
  "subaccount_seller": "alice",
  "subaccount_offer_price": "50.000 VIZ",
  "subaccount_on_sale": true
}]
```

- 买家每次交易购买在 `account` 命名空间下创建一个子账户的权利。

---

## `buy_account_operation`（ID 56）

**授权：** `buyer` 的 `active`

购买当前挂牌出售的账户。所有权限转移给买家。

| 字段 | 类型 | 描述 |
|------|------|------|
| `buyer` | `account_name_type` | 购买账户 |
| `account` | `account_name_type` | 被购买的账户 |
| `account_offer_price` | `asset`（VIZ） | 购买价格（必须与挂牌价格完全匹配） |
| `account_authorities_key` | `public_key_type` | 设置为被购账户 master、active、regular 和 memo 的新密钥 |
| `tokens_to_shares` | `asset`（VIZ） | 为被购账户额外转换为 SHARES 的 VIZ（可为 `"0.000 VIZ"`） |

```json
[56, {
  "buyer": "bob",
  "account": "alice",
  "account_offer_price": "1000.000 VIZ",
  "account_authorities_key": "VIZ5newowner...",
  "tokens_to_shares": "0.000 VIZ"
}]
```

- `account_offer_price` 必须与 `set_account_price_operation` 中的价格完全匹配。
- `account_authorities_key` 同时应用于所有四个权限槽。
- 付款发送至挂牌中指定的 `account_seller`。
- 购买成功时触发虚拟操作 `account_sale_operation`。

---

## `target_account_sale_operation`（ID 61）

**授权：** `account` 的 `master`

将账户挂牌为仅限特定买家的私下（定向）销售。只有 `target_buyer` 才能购买此挂牌。

| 字段 | 类型 | 描述 |
|------|------|------|
| `account` | `account_name_type` | 挂牌出售的账户 |
| `account_seller` | `account_name_type` | 接收付款的账户 |
| `target_buyer` | `account_name_type` | 唯一允许购买的账户 |
| `account_offer_price` | `asset`（VIZ） | 要价 |
| `account_on_sale` | `bool` | `true` 挂牌；`false` 撤牌 |

```json
[61, {
  "account": "alice",
  "account_seller": "alice",
  "target_buyer": "charlie",
  "account_offer_price": "500.000 VIZ",
  "account_on_sale": true
}]
```

- `account_on_sale: false` 取消定向挂牌。
- 买家使用标准的 `buy_account_operation` 完成购买。

---

参见：[数据类型](../data-types.md)、[操作概述](./overview.md)、[虚拟操作](../virtual-operations.md)、[Database API — 账户市场](../../plugins/database-api.md#account-market)。
