# 托管操作

托管将 VIZ 代币保存在条件转账中：资金只有在收款方和中立代理人双方批准后才会释放，或在争议情况下由代理人仲裁。

**托管流程：**
```
escrow_transfer  →  escrow_approve（由"to"和"agent"双方）
                 →  escrow_release（由"from"或"to"）
                 →  [escrow_dispute]  →  escrow_release（仅由"agent"）
                 ↓
           （批准截止日期已过）
                 →  expire_escrow_ratification_operation [虚拟 — 资金返还]
```

---

## `escrow_transfer_operation`（ID 15）

**授权：** `from` 的 `active`

创建托管。资金立即从 `from` 转入托管余额；`agent` 和 `to` 都必须批准才能释放。

| 字段 | 类型 | 描述 |
|------|------|------|
| `from` | `account_name_type` | 发送方 |
| `to` | `account_name_type` | 预期收款方 |
| `agent` | `account_name_type` | 中立托管代理人（仲裁者） |
| `escrow_id` | `uint32_t` | 发送方选择的唯一 ID（默认 30） |
| `token_amount` | `asset`（VIZ） | 托管中持有的金额 |
| `fee` | `asset`（VIZ） | 代理费 — 批准后支付给代理人 |
| `ratification_deadline` | `time_point_sec` | 双方批准的截止日期 |
| `escrow_expiration` | `time_point_sec` | 如果托管从未释放的到期时间 |
| `json_metadata` | `string` | 可选条款/元数据 |

```json
[15, {
  "from": "alice",
  "to": "bob",
  "agent": "charlie",
  "escrow_id": 1001,
  "token_amount": "100.000 VIZ",
  "fee": "1.000 VIZ",
  "ratification_deadline": "2024-06-01T00:00:00",
  "escrow_expiration": "2024-07-01T00:00:00",
  "json_metadata": "{\"description\":\"payment for work\"}"
}]
```

- `ratification_deadline` 必须早于 `escrow_expiration`。
- 广播时两个时间戳都必须在未来。
- `escrow_id` 在 `from` 账户中必须唯一。
- 如果在 `ratification_deadline` 之前未批准，虚拟 `expire_escrow_ratification_operation` 触发，资金返还给 `from`。

---

## `escrow_approve_operation`（ID 18）

**授权：** `who` 的 `active`

批准或拒绝托管。`to` 和 `agent` 都必须批准，托管才能激活。

| 字段 | 类型 | 描述 |
|------|------|------|
| `from` | `account_name_type` | 原始托管发送方 |
| `to` | `account_name_type` | 原始托管收款方 |
| `agent` | `account_name_type` | 托管代理人 |
| `who` | `account_name_type` | 批准者：必须是 `to` 或 `agent` |
| `escrow_id` | `uint32_t` | 托管 ID |
| `approve` | `bool` | `true` 批准，`false` 拒绝 |

```json
[18, {
  "from": "alice",
  "to": "bob",
  "agent": "charlie",
  "who": "bob",
  "escrow_id": 1001,
  "approve": true
}]
```

- `who` 必须是 `to` 或 `agent`。
- 一旦批准，无法撤销。
- 如果 `approve: false` — 托管取消，资金返还给 `from`。
- 必须在 `ratification_deadline` 之前广播。

---

## `escrow_dispute_operation`（ID 16）

**授权：** `who` 的 `active`

对已批准的托管提出争议。争议后，只有 `agent` 可以释放资金。

| 字段 | 类型 | 描述 |
|------|------|------|
| `from` | `account_name_type` | 原始托管发送方 |
| `to` | `account_name_type` | 原始托管收款方 |
| `agent` | `account_name_type` | 托管代理人 |
| `who` | `account_name_type` | 提出争议者：必须是 `from` 或 `to` |
| `escrow_id` | `uint32_t` | 托管 ID |

```json
[16, {
  "from": "alice",
  "to": "bob",
  "agent": "charlie",
  "who": "alice",
  "escrow_id": 1001
}]
```

- 只能对**已批准**的托管（`to` 和 `agent` 都已批准）提出争议。
- 必须在 `escrow_expiration` 之前提出。

---

## `escrow_release_operation`（ID 17）

**授权：** `who` 的 `active`

将托管资金释放给 `receiver`。允许部分释放。

| 字段 | 类型 | 描述 |
|------|------|------|
| `from` | `account_name_type` | 原始托管发送方 |
| `to` | `account_name_type` | 原始托管收款方 |
| `agent` | `account_name_type` | 托管代理人 |
| `who` | `account_name_type` | 释放资金的账户 |
| `receiver` | `account_name_type` | 接收资金的账户（必须是 `from` 或 `to`） |
| `escrow_id` | `uint32_t` | 托管 ID |
| `token_amount` | `asset`（VIZ） | 释放金额（可部分释放） |

```json
[17, {
  "from": "alice",
  "to": "bob",
  "agent": "charlie",
  "who": "alice",
  "receiver": "bob",
  "escrow_id": 1001,
  "token_amount": "100.000 VIZ"
}]
```

**释放权限规则：**

| 状态 | 谁可以释放 | 释放给谁 |
|------|-----------|---------|
| 无争议，到期前 | `from` 或 `to` | 另一方 |
| 无争议，到期后 | `from` 或 `to` | 任意一方 |
| 争议中 | 仅 `agent` | 任意一方 |

- 允许部分释放；剩余部分留在托管中。

---

参见：[数据类型](../data-types.md)、[操作概述](./overview.md)、[虚拟操作](../virtual-operations.md)。
