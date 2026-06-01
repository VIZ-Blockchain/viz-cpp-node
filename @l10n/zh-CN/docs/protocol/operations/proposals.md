# 提案操作

提案实现多签名治理：一个账户创建一组操作，需要规定签署人集合批准后才能执行。当收集到足够多的批准后，提案自动执行。

---

## `proposal_create_operation`（ID 22）

**授权：** `author` 的 `active`

创建交易提案。提案由 `author` + `title` 对标识。

| 字段 | 类型 | 描述 |
|------|------|------|
| `author` | `account_name_type` | 创建提案的账户 |
| `title` | `string` | 每个作者唯一的标题（用作提案 ID） |
| `memo` | `string` | 人类可读的描述 |
| `expiration_time` | `time_point_sec` | 提案过期时间 |
| `proposed_operations` | `vector<operation_wrapper>` | 批准后执行的操作 |
| `review_period_time` | `optional<time_point_sec>` | 可选：此时间后不再接受新批准 |
| `extensions` | `extensions_type` | 始终为 `[]` |

`proposed_operations` 中的每个条目是一个 `operation_wrapper`：
```json
{"op": [type_id, operation_object]}
```

```json
[22, {
  "author": "alice",
  "title": "transfer-proposal-001",
  "memo": "Joint transfer to shared fund",
  "expiration_time": "2024-12-31T23:59:59",
  "proposed_operations": [
    {
      "op": [2, {
        "from": "multisig-wallet",
        "to": "fund",
        "amount": "1000.000 VIZ",
        "memo": ""
      }]
    }
  ],
  "review_period_time": null,
  "extensions": []
}]
```

- `title` 对每个 `author` 必须唯一。
- `expiration_time` 必须在未来。
- 若设置了 `review_period_time`，它必须早于 `expiration_time`；此时间后不再接受新批准。
- `proposed_operations` 可包含任意类型的多个操作。

---

## `proposal_update_operation`（ID 23）

**授权：** 取决于被修改的批准集合

添加或移除批准。收集到足够批准后提案自动执行。

| 字段 | 类型 | 描述 |
|------|------|------|
| `author` | `account_name_type` | 提案作者 |
| `title` | `string` | 提案标题 |
| `active_approvals_to_add` | `flat_set<account_name_type>` | 授予 active 批准的账户 |
| `active_approvals_to_remove` | `flat_set<account_name_type>` | 撤销 active 批准的账户 |
| `master_approvals_to_add` | `flat_set<account_name_type>` | 授予 master 批准的账户 |
| `master_approvals_to_remove` | `flat_set<account_name_type>` | 撤销 master 批准的账户 |
| `regular_approvals_to_add` | `flat_set<account_name_type>` | 授予 regular 批准的账户 |
| `regular_approvals_to_remove` | `flat_set<account_name_type>` | 撤销 regular 批准的账户 |
| `key_approvals_to_add` | `flat_set<public_key_type>` | 授予批准的公钥 |
| `key_approvals_to_remove` | `flat_set<public_key_type>` | 撤销批准的公钥 |
| `extensions` | `extensions_type` | 始终为 `[]` |

```json
[23, {
  "author": "alice",
  "title": "transfer-proposal-001",
  "active_approvals_to_add": ["bob"],
  "active_approvals_to_remove": [],
  "master_approvals_to_add": [],
  "master_approvals_to_remove": [],
  "regular_approvals_to_add": [],
  "regular_approvals_to_remove": [],
  "key_approvals_to_add": [],
  "key_approvals_to_remove": [],
  "extensions": []
}]
```

- 交易必须由与被添加或移除批准对应的密钥签名。
- 不需要的 `*_to_add` 和 `*_to_remove` 字段默认为 `[]`。
- 执行后提案标记为已完成；后续更新将被拒绝。

---

## `proposal_delete_operation`（ID 24）

**授权：** `requester` 的 `active`

永久删除（否决）提案。提案的任何必需权限方均可调用此操作。

| 字段 | 类型 | 描述 |
|------|------|------|
| `author` | `account_name_type` | 提案作者 |
| `title` | `string` | 提案标题 |
| `requester` | `account_name_type` | 请求删除的账户 |
| `extensions` | `extensions_type` | 始终为 `[]` |

```json
[24, {
  "author": "alice",
  "title": "transfer-proposal-001",
  "requester": "bob",
  "extensions": []
}]
```

- `requester` 必须是该提案的必需权限方。
- 删除是永久性的，无法撤销。

---

参见：[数据类型](../data-types.md)、[操作概述](./overview.md)、[Database API — get_proposed_transactions](../../plugins/database-api.md#get_proposed_transactionsaccount-from-limit)。
