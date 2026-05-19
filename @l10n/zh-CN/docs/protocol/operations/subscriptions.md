# 付费订阅操作

付费订阅允许账户提供以 VIZ 代币支付的分级定期服务，支持可选的自动续订。

---

## `set_paid_subscription_operation`（ID 50）

**授权：** `account` 的 `active`

创建或更新订阅服务。首次创建时收取 `create_paid_subscription_fee`。

| 字段 | 类型 | 描述 |
|------|------|------|
| `account` | `account_name_type` | 提供订阅的账户 |
| `url` | `string` | 包含订阅详情的 URL |
| `levels` | `uint16_t` | 订阅等级数量（≥ 1） |
| `amount` | `asset`（VIZ） | 每期每单位等级的基础价格 |
| `period` | `uint16_t` | 订阅周期（天，≥ 1） |

```json
[50, {
  "account": "alice",
  "url": "https://alice.example.com/subscribe",
  "levels": 3,
  "amount": "10.000 VIZ",
  "period": 30
}]
```

- 订阅者的实际费用 = `amount × level`。
- `levels = 3`，`amount = "10.000 VIZ"` → 等级 1 每期 10 VIZ，等级 2 每期 20 VIZ，等级 3 每期 30 VIZ。
- 更新此操作会更改未来订阅的参数；现有活跃订阅在续订前继续按旧条款执行。

---

## `paid_subscribe_operation`（ID 51）

**授权：** `subscriber` 的 `active`

订阅或续订付费订阅。代币立即从 `subscriber` 转账给 `account`。

| 字段 | 类型 | 描述 |
|------|------|------|
| `subscriber` | `account_name_type` | 订阅账户 |
| `account` | `account_name_type` | 提供订阅的账户 |
| `level` | `uint16_t` | 订阅等级（1 – `levels`） |
| `amount` | `asset`（VIZ） | 支付金额 |
| `period` | `uint16_t` | 支付的周期数 |
| `auto_renewal` | `bool` | 启用每期自动续订 |

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

- `amount` 必须与 `subscription.amount × level × period` 完全匹配。
- `level` 必须在范围 [1, `subscription.levels`] 内。
- `auto_renewal: true` — 在余额充足的情况下每期自动扣除代币。
- `auto_renewal: false` — 一次性订阅；在付费周期结束后到期。

**虚拟操作：**

| 虚拟操作 | 触发条件 |
|---------|---------|
| `paid_subscription_action_operation` | 支付已处理 |
| `cancel_paid_subscription_operation` | 订阅到期或已取消 |

---

参见：[数据类型](../data-types.md)、[操作概述](./overview.md)、[虚拟操作](../virtual-operations.md)。
