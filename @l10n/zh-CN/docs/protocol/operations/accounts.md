# 账户操作

---

## `account_create_operation`（ID 20）

**授权：** `creator` 的 `active`

创建新的区块链账户。费用被转换为新账户的 SHARES。

| 字段 | 类型 | 描述 |
|------|------|------|
| `fee` | `asset`（VIZ） | 创建费 ≥ 链的 `account_creation_fee` |
| `delegation` | `asset`（SHARES） | 给新账户的初始 SHARES 委托 |
| `creator` | `account_name_type` | 支付费用的账户 |
| `new_account_name` | `account_name_type` | 新账户名 |
| `master` | `authority` | 主权限 |
| `active` | `authority` | 活跃权限 |
| `regular` | `authority` | 常规权限 |
| `memo_key` | `public_key_type` | Memo 公钥 |
| `json_metadata` | `string` | JSON 元数据（可为 `""`） |
| `referrer` | `account_name_type` | 推荐账户（可为 `""`） |
| `extensions` | `extensions_type` | 始终为 `[]` |

```json
[20, {
  "fee": "1.000 VIZ",
  "delegation": "10.000000 SHARES",
  "creator": "alice",
  "new_account_name": "bob",
  "master":  { "weight_threshold": 1, "account_auths": [], "key_auths": [["VIZ5...", 1]] },
  "active":  { "weight_threshold": 1, "account_auths": [], "key_auths": [["VIZ5...", 1]] },
  "regular": { "weight_threshold": 1, "account_auths": [], "key_auths": [["VIZ5...", 1]] },
  "memo_key": "VIZ5...",
  "json_metadata": "",
  "referrer": "",
  "extensions": []
}]
```

- 三种权限都是必填的（即使使用相同的密钥）。
- `fee.symbol` 必须为 `VIZ`；`delegation.symbol` 必须为 `SHARES`。

---

## `account_update_operation`（ID 5）

**授权：** `account` 的 `master`（如果存在 `master` 字段），否则 `active`

更新账户的密钥和元数据。

| 字段 | 类型 | 描述 |
|------|------|------|
| `account` | `account_name_type` | 要更新的账户 |
| `master` | `optional<authority>` | 新的主权限（不更改则省略） |
| `active` | `optional<authority>` | 新的活跃权限 |
| `regular` | `optional<authority>` | 新的常规权限 |
| `memo_key` | `public_key_type` | 新的 memo 密钥（即使不更改也是必填） |
| `json_metadata` | `string` | 新的 JSON 元数据 |

```json
[5, {
  "account": "alice",
  "active": { "weight_threshold": 1, "account_auths": [], "key_auths": [["VIZ5new...", 1]] },
  "memo_key": "VIZ5new...",
  "json_metadata": "{\"profile\":\"updated\"}"
}]
```

- 如果存在 `master` → 用当前 **master** 密钥签名。
- 如果 `master` 不存在 → 用当前 **active** 密钥签名。
- `memo_key` 始终必填。

---

## `account_metadata_operation`（ID 21）

**授权：** `account` 的 `regular`

仅更新账户的 JSON 元数据。带宽成本低于 `account_update`。

| 字段 | 类型 | 描述 |
|------|------|------|
| `account` | `account_name_type` | 要更新的账户 |
| `json_metadata` | `string` | 新的 JSON 元数据字符串 |

```json
[21, {
  "account": "alice",
  "json_metadata": "{\"name\":\"Alice\",\"about\":\"Hello!\"}"
}]
```

---

参见：[数据类型](../data-types.md)、[操作概述](./overview.md)。
