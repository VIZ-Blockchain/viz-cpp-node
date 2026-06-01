# 验证者操作

---

## `validator_update_operation`（ID 6）

**授权：** `owner` 的 `active`

注册或更新验证者节点。将 `block_signing_key` 设置为空密钥可将验证者从区块生产中移除。

| 字段 | 类型 | 描述 |
|------|------|------|
| `owner` | `account_name_type` | 验证者账户名 |
| `url` | `string` | 验证者网站或信息 URL（非空，最多 256 字节） |
| `block_signing_key` | `public_key_type` | 用于签署生产区块的密钥 |

```json
[6, {
  "owner": "alice",
  "url": "https://alice.example.com",
  "block_signing_key": "VIZ5hqSa4NkEZGAMUpoH5EaEr64mBJuMcPpGjvk8qb7hcPFTbXSQ9"
}]
```

- **空密钥**（停用）：`"VIZ1111111111111111111111111111111114T1Anm"` — 从区块生产中移除，不删除验证者记录。
- 广播此操作需要 `validator_declaration_fee`（支付到委员会基金）。

---

## `chain_properties_update_operation`（ID 25）

**授权：** `owner` 的 `active`

对基础链属性投票（`chain_properties_init` 格式）。链上值是所有活跃验证者的中位数。

| 字段 | 类型 | 描述 |
|------|------|------|
| `owner` | `account_name_type` | 投票的验证者 |
| `props` | `chain_properties_init` | 提议的链参数 |

```json
[25, {
  "owner": "alice",
  "props": {
    "account_creation_fee": "1.000 VIZ",
    "maximum_block_size": 65536,
    "create_account_delegation_ratio": 10,
    "create_account_delegation_time": 2592000,
    "min_delegation": "1.000 VIZ",
    "min_curation_percent": 0,
    "max_curation_percent": 10000,
    "bandwidth_reserve_percent": 1000,
    "bandwidth_reserve_below": "1.000000 SHARES",
    "flag_energy_additional_cost": 1000,
    "vote_accounting_min_rshares": 0,
    "committee_request_approve_min_percent": 1000
  }
}]
```

- 所有百分比字段以基点表示（0–10000）。
- `min_curation_percent` 必须 ≤ `max_curation_percent`。
- 对于 HF9+ 扩展属性，使用 `versioned_chain_properties_update_operation`（ID 46）。

---

## `versioned_chain_properties_update_operation`（ID 46）

**授权：** `owner` 的 `active`

对支持所有硬分叉扩展的版本化链属性投票。当前节点优先于 `chain_properties_update_operation`。

| 字段 | 类型 | 描述 |
|------|------|------|
| `owner` | `account_name_type` | 投票的验证者 |
| `props` | `versioned_chain_properties` | 序列化为 `[index, object]` 的版本化属性 |

```json
[46, {
  "owner": "alice",
  "props": [3, {
    "account_creation_fee": "1.000 VIZ",
    "maximum_block_size": 65536,
    "create_account_delegation_ratio": 10,
    "create_account_delegation_time": 2592000,
    "min_delegation": "1.000 VIZ",
    "min_curation_percent": 0,
    "max_curation_percent": 10000,
    "bandwidth_reserve_percent": 1000,
    "bandwidth_reserve_below": "1.000000 SHARES",
    "flag_energy_additional_cost": 1000,
    "vote_accounting_min_rshares": 0,
    "committee_request_approve_min_percent": 1000,
    "inflation_validator_percent": 2000,
    "inflation_ratio_committee_vs_reward_fund": 1000,
    "inflation_recalc_period": 28800,
    "data_operations_cost_additional_bandwidth": 0,
    "validator_miss_penalty_percent": 100,
    "validator_miss_penalty_duration": 86400,
    "create_invite_min_balance": "1.000 VIZ",
    "committee_create_request_fee": "1.000 VIZ",
    "create_paid_subscription_fee": "1.000 VIZ",
    "account_on_sale_fee": "10.000 VIZ",
    "subaccount_on_sale_fee": "1.000 VIZ",
    "validator_declaration_fee": "1.000 VIZ",
    "withdraw_intervals": 28
  }]
}]
```

- `props` 是静态变体：使用索引 `3` 表示 `chain_properties_hf9`（当前）。
- 各版本索引的完整字段列表见[数据类型](../data-types.md#versioned_chain_properties)。

---

## `account_validator_vote_operation`（ID 7）

**授权：** `account` 的 `active`

为验证者投票或移除投票。按累积投票权重排名前 21 的验证者生产区块。

| 字段 | 类型 | 描述 |
|------|------|------|
| `account` | `account_name_type` | 投票账户 |
| `witness` | `account_name_type` | 要投票的验证者 |
| `approve` | `bool` | `true` 添加投票，`false` 移除投票 |

```json
[7, {
  "account": "alice",
  "witness": "bob",
  "approve": true
}]
```

- 投票权重与投票者的 SHARES 质押成比例。
- `approve: false` 移除之前投出的票。

---

## `account_validator_proxy_operation`（ID 8）

**授权：** `account` 的 `active`

将所有验证者投票委托给代理账户。设置代理时，所有现有直接投票被移除。

| 字段 | 类型 | 描述 |
|------|------|------|
| `account` | `account_name_type` | 设置代理的账户 |
| `proxy` | `account_name_type` | 代理账户；`""` 移除代理 |

```json
[8, {
  "account": "alice",
  "proxy": "bob"
}]
```

- `proxy: ""`（空字符串）移除代理并恢复直接投票。
- 不能将代理设置为自己。
- 代理链被传递解析（A→B→C）；最大链深度受到限制。
- 设置代理会移除所有直接验证者投票。

---

参见：[数据类型](../data-types.md)、[操作概述](./overview.md)、[链属性](../../governance/chain-properties.md)。
