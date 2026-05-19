# 委员会操作

委员会（工作者提案）系统允许社区成员从委员会基金申请资金。SHARES 持有者投票批准或拒绝请求；批准的请求从基金中获得支付。

---

## `committee_worker_create_request_operation`（ID 35）

**授权：** `creator` 的 `regular`

创建新的资金请求。提交时从创建者处收取 `committee_create_request_fee`。

| 字段 | 类型 | 描述 |
|------|------|------|
| `creator` | `account_name_type` | 创建请求的账户 |
| `url` | `string` | 描述提案的 URL（非空，最多 255 字节） |
| `worker` | `account_name_type` | 将接收支付的账户 |
| `required_amount_min` | `asset`（VIZ） | 最低可接受支付金额 |
| `required_amount_max` | `asset`（VIZ） | 最高可接受支付金额 |
| `duration` | `uint32_t` | 请求持续时间（秒） |

```json
[35, {
  "creator": "alice",
  "url": "https://alice.example.com/proposal",
  "worker": "alice",
  "required_amount_min": "100.000 VIZ",
  "required_amount_max": "500.000 VIZ",
  "duration": 604800
}]
```

**约束：**

| 参数 | 值 |
|------|-----|
| 最小持续时间 | 5 天（432000 秒） |
| 最大持续时间 | 30 天（2592000 秒） |
| `required_amount_max` | 必须 > `required_amount_min` |

- `required_amount_min` ≥ 0；`required_amount_max` > `required_amount_min`。
- `worker` 可以与 `creator` 不同。

---

## `committee_worker_cancel_request_operation`（ID 36）

**授权：** `creator` 的 `regular`

在到期之前取消现有资金请求。

| 字段 | 类型 | 描述 |
|------|------|------|
| `creator` | `account_name_type` | 请求的创建者 |
| `request_id` | `uint32_t` | 要取消的请求 ID |

```json
[36, {
  "creator": "alice",
  "request_id": 42
}]
```

- 只有请求的 `creator` 才能取消它。
- `request_id` 必须指向现有的活跃请求。

---

## `committee_vote_request_operation`（ID 37）

**授权：** `voter` 的 `regular`

对资金请求投票。投票权重与投票者的 SHARES 质押成比例。

| 字段 | 类型 | 描述 |
|------|------|------|
| `voter` | `account_name_type` | 投票的账户 |
| `request_id` | `uint32_t` | 请求 ID |
| `vote_percent` | `int16_t` | 投票权重（基点，−10000 到 10000） |

```json
[37, {
  "voter": "bob",
  "request_id": 42,
  "vote_percent": 10000
}]
```

- `vote_percent` > 0 → 支持；`vote_percent` < 0 → 反对；`vote_percent` = 0 → 移除投票。
- 当加权净投票百分比 ≥ 链属性 `committee_request_approve_min_percent` 时，请求被批准。

**委员会生命周期触发的虚拟操作：**

| 虚拟操作 | 触发条件 |
|---------|---------|
| `committee_cancel_request_operation` | 请求在未批准的情况下到期 |
| `committee_approve_request_operation` | 请求达到批准阈值 |
| `committee_payout_request_operation` | 处理支付 |
| `committee_pay_request_operation` | 工作者收到付款 |

---

参见：[数据类型](../data-types.md)、[操作概述](./overview.md)、[虚拟操作](../virtual-operations.md)、[委员会治理](../../governance/committee.md)。
