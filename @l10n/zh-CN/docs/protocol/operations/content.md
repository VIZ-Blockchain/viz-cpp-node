# 内容操作

> **弃用通知：** `vote_operation`（ID 0）、`content_operation`（ID 1）和 `delete_content_operation`（ID 9）已**弃用**。它们保留在操作变体中以保持历史和归档兼容性，但不应在新代码中使用。`custom_operation`（ID 10）是活跃的通用数据通道。

---

## `content_operation` *（已弃用）*（ID 1）

**授权：** `author` 的 `regular`

创建或更新内容对象（帖子或评论）。内容通过 `author` + `permlink` 寻址。

| 字段 | 类型 | 描述 |
|------|------|------|
| `parent_author` | `account_name_type` | 父内容的作者；根帖子为 `""` |
| `parent_permlink` | `string` | 父内容的 permlink；对根帖子充当分类标签 |
| `author` | `account_name_type` | 内容作者 |
| `permlink` | `string` | 作者命名空间内的唯一标识符 |
| `title` | `string` | 帖子标题 |
| `body` | `string` | 帖子正文（Markdown） |
| `curation_percent` | `int16_t` | 策展奖励份额（基点，0–10000） |
| `json_metadata` | `string` | JSON 元数据字符串 |
| `extensions` | `content_extensions_type` | 可选受益人列表 |

受益人扩展格式（在 `extensions` 内）：
```json
[[0, {
  "beneficiaries": [
    {"account": "bob", "weight": 2500}
  ]
}]]
```

- `parent_author == ""` → 根帖子；否则为评论。
- `permlink` 在每个作者内必须唯一。
- `curation_percent` 必须在链的 `[min_curation_percent, max_curation_percent]` 范围内。
- 受益人权重之和必须 ≤ 10000，且按账户名升序排列。

---

## `vote_operation` *（已弃用）*（ID 0）

**授权：** `voter` 的 `regular`

对内容进行加权投票。

| 字段 | 类型 | 描述 |
|------|------|------|
| `voter` | `account_name_type` | 投票账户 |
| `author` | `account_name_type` | 内容作者 |
| `permlink` | `string` | 内容 permlink |
| `weight` | `int16_t` | 投票权重：负数 = 标记，正数 = 点赞，0 = 取消投票 |

- `weight` 范围：−10000 到 10000。
- 标记投票可能产生额外能量成本（链属性 `flag_energy_additional_cost`）。

---

## `delete_content_operation` *（已弃用）*（ID 9）

**授权：** `author` 的 `regular`

删除内容对象。

| 字段 | 类型 | 描述 |
|------|------|------|
| `author` | `account_name_type` | 内容作者 |
| `permlink` | `string` | 要删除的内容 permlink |

- 有待发放奖励的内容无法删除。

---

## `custom_operation`（ID 10）

**授权：** 签名者的 `active` 或 `regular`（至少一个）

将任意 JSON 数据发布到区块链。应用程序使用它构建自定义链上协议。

| 字段 | 类型 | 描述 |
|------|------|------|
| `required_active_auths` | `flat_set<account_name_type>` | 需要活跃密钥签名的账户 |
| `required_regular_auths` | `flat_set<account_name_type>` | 需要常规密钥签名的账户 |
| `id` | `string` | 应用定义的命名空间标识符（最多 32 个字符） |
| `json` | `string` | 有效的 UTF-8 JSON 载荷 |

```json
[10, {
  "required_active_auths": [],
  "required_regular_auths": ["alice"],
  "id": "my_app",
  "json": "{\"action\":\"follow\",\"target\":\"bob\"}"
}]
```

- `required_active_auths` 或 `required_regular_auths` 至少一个必须非空。
- `required_active_auths` 中的账户必须用其 **active** 密钥签名。
- `required_regular_auths` 中的账户必须用其 **regular** 密钥签名。
- 两个集合可以同时填写，用于多权限操作。
- `json` 字段被计为数据操作 — 可能产生额外带宽成本（链属性 `data_operations_cost_additional_bandwidth`）。

---

参见：[数据类型](../data-types.md)、[操作概述](./overview.md)、[奖励](./awards.md)。
