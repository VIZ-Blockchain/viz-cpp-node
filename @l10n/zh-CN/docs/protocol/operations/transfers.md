# 转账操作

---

## `transfer_operation`（ID 2）

**授权：** `from` 的 `active`（VIZ）/ `from` 的 `master`（SHARES）

在账户间转账 VIZ 或 SHARES 代币。

| 字段 | 类型 | 描述 |
|------|------|------|
| `from` | `account_name_type` | 发送账户 |
| `to` | `account_name_type` | 接收账户 |
| `amount` | `asset` | 转账金额（VIZ 或 SHARES） |
| `memo` | `string` | Memo 文本（明文或加密；可为 `""`） |

```json
[2, {
  "from": "alice",
  "to": "bob",
  "amount": "10.000 VIZ",
  "memo": "payment for services"
}]
```

- `amount.symbol` 必须为 `VIZ` 或 `SHARES`。
- VIZ 转账需要 **active** 权限；SHARES 转账需要 **master** 权限。
- 加密 memo 格式：`#` 后跟 base58 编码的密文。

---

## `transfer_to_vesting_operation`（ID 3）

**授权：** `from` 的 `active`

将流动 VIZ 转换为 SHARES（质押）。SHARES 可以被转入不同的账户。

| 字段 | 类型 | 描述 |
|------|------|------|
| `from` | `account_name_type` | 提供 VIZ 的账户 |
| `to` | `account_name_type` | 接收 SHARES 的账户（可与 `from` 相同） |
| `amount` | `asset`（VIZ） | 要质押的 VIZ 数量 |

```json
[3, {
  "from": "alice",
  "to": "alice",
  "amount": "100.000 VIZ"
}]
```

- `amount.symbol` 必须为 `VIZ`。
- `to` 可以是任意现有账户 — 适合赠送质押份额。

---

## `withdraw_vesting_operation`（ID 4）

**授权：** `account` 的 `active`

启动通过多个间隔逐步将 SHARES 提取回流动 VIZ 的过程。

| 字段 | 类型 | 描述 |
|------|------|------|
| `account` | `account_name_type` | 发起提取的账户 |
| `vesting_shares` | `asset`（SHARES） | 要提取的总 SHARES；`0.000000 SHARES` 取消 |

```json
[4, {
  "account": "alice",
  "vesting_shares": "1000.000000 SHARES"
}]
```

- 提取分布在 `withdraw_intervals` 个间隔上（链属性，默认 28）。
- 每个间隔：转换 `vesting_shares / withdraw_intervals` SHARES。
- 设置为 `"0.000000 SHARES"` 取消活跃提取。

---

## `set_withdraw_vesting_route_operation`（ID 11）

**授权：** `from_account` 的 `active`

将 vesting 提取的一定百分比路由到指定账户，可选择将路由部分重新质押。

| 字段 | 类型 | 描述 |
|------|------|------|
| `from_account` | `account_name_type` | 提取被路由的账户 |
| `to_account` | `account_name_type` | 目标账户 |
| `percent` | `uint16_t` | 路由百分比（0–10000 基点） |
| `auto_vest` | `bool` | 如果为 `true`，路由的代币立即在 `to_account` 中重新质押 |

```json
[11, {
  "from_account": "alice",
  "to_account": "bob",
  "percent": 5000,
  "auto_vest": false
}]
```

- `percent` = 0 删除到 `to_account` 的此路由。
- `from_account` 所有路由之和不得超过 10000。
- 允许到不同账户的多条路由。

---

## `delegate_vesting_shares_operation`（ID 19）

**授权：** `delegator` 的 `active`

将 SHARES 委托给另一个账户。受托方获得带宽和投票权；所有权保留在委托方。

| 字段 | 类型 | 描述 |
|------|------|------|
| `delegator` | `account_name_type` | 委托 SHARES 的账户 |
| `delegatee` | `account_name_type` | 接收委托的账户 |
| `vesting_shares` | `asset`（SHARES） | 委托金额；`0.000000 SHARES` 移除委托 |

```json
[19, {
  "delegator": "alice",
  "delegatee": "bob",
  "vesting_shares": "500.000000 SHARES"
}]
```

- `vesting_shares` 必须 ≥ 链属性 `min_delegation`，或恰好为 `0.000000 SHARES` 以移除。
- 移除委托时，SHARES 进入 7 天返还窗口后才被重新记入。
- 返还窗口结束时触发虚拟 `return_vesting_delegation_operation`。

---

参见：[数据类型](../data-types.md)、[操作概述](./overview.md)。
