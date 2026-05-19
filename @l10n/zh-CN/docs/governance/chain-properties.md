# 链属性

链属性是网络的可治理参数：费用、区块大小、通胀率、惩罚规则等。没有任何中央机构设置这些参数——每个活跃验证者发布其首选值，区块链对所有活跃验证者取**中位数**并应用。

---

## 工作原理

### 1. 验证者发布首选项

每个验证者通过 `versioned_chain_properties_update_operation` 提交其首选参数：

```json
[46, {
  "owner": "alice",
  "props": [3, {
    "account_creation_fee": "1.000 VIZ",
    "maximum_block_size": 131072,
    ...
  }]
}]
```

`[3, {...}]` 表示版本 3（`chain_properties_hf9`，当前格式）。

### 2. 中位数计算

在每次验证者计划更新时，区块链调用 `update_median_witness_props()`。对**每个属性独立地**：
1. 收集每个活跃验证者的值。
2. 排序。
3. 取**中位数**（索引 `active.size() / 2`）。

```
示例——5 个验证者对 account_creation_fee 投票：
  0.5, 1.0, 1.0, 2.0, 5.0 VIZ
              ↑
         中位数 = 1.0 VIZ
```

中位数对极端值有抵抗力：单个验证者无法导致突然的大幅变化；要显著移动任何参数，需要多数人同意。

### 3. 应用

结果 `median_props` 对象存储在 `witness_schedule_object` 中，并在所有区块处理中强制执行。

---

## 所有可治理属性

### 账户和委托

| 属性 | 类型 | 默认值 | 描述 |
|------|------|-------|------|
| `account_creation_fee` | asset（VIZ） | 1.000 VIZ | 创建新账户的最低费用 |
| `create_account_delegation_ratio` | uint32 | 10 | 所需委托 = ratio × fee |
| `create_account_delegation_time` | uint32（秒） | 2592000（30天） | 创建委托的锁定时间 |
| `min_delegation` | asset（VIZ） | 1.000 VIZ | 任何 SHARES 委托的最低金额 |

### 区块大小和带宽

| 属性 | 类型 | 默认值 | 描述 |
|------|------|-------|------|
| `maximum_block_size` | uint32（字节） | 131072 | 最大区块大小；控制吞吐量 |
| `bandwidth_reserve_percent` | uint16（bp） | 1000（10%） | 小账户的额外带宽 |
| `bandwidth_reserve_below` | asset（SHARES） | 500.000000 | 获得带宽预留的资格阈值 |
| `data_operations_cost_additional_bandwidth` | uint32（%） | 0 | 数据操作（custom_operation）的额外带宽倍数 |

### 通胀和经济

| 属性 | 类型 | 默认值 | 描述 |
|------|------|-------|------|
| `inflation_witness_percent` | uint16（bp） | 2000（20%） | 验证者在区块通胀中的份额 |
| `inflation_ratio_committee_vs_reward_fund` | uint16（bp） | 5000（50%） | 剩余通胀的分配：委员会基金 vs 奖励基金 |
| `inflation_recalc_period` | uint32（区块） | 806400（~28天） | 通胀重新计算的频率 |

通胀流程：`block_reward × inflation_witness_percent` → 验证者。剩余分配：`inflation_ratio_committee_vs_reward_fund` → 委员会基金；其余 → 奖励基金。

### 奖励系统

| 属性 | 类型 | 默认值 | 描述 |
|------|------|-------|------|
| `min_curation_percent` | uint16（bp） | 500（5%） | 内容支付中的最低策展奖励份额 |
| `max_curation_percent` | uint16（bp） | 500（5%） | 最高策展奖励份额 |
| `vote_accounting_min_rshares` | uint32 | 5000000 | 奖励产生非零收益所需的最低 rshares |
| `flag_energy_additional_cost` | uint16（bp） | 0 | 反对票/标记的额外能量成本 |

### 验证者问责

| 属性 | 类型 | 默认值 | 描述 |
|------|------|-------|------|
| `witness_miss_penalty_percent` | uint16（bp） | 100（1%） | 错过区块时的投票权重降低 |
| `witness_miss_penalty_duration` | uint32（秒） | 86400（1天） | 错过惩罚的持续时间 |

### 费用

所有费用进入委员会基金（DAO 国库）。

| 属性 | 类型 | 默认值 | 描述 |
|------|------|-------|------|
| `committee_create_request_fee` | asset（VIZ） | 100.000 VIZ | 创建委员会资金请求的费用 |
| `create_paid_subscription_fee` | asset（VIZ） | 100.000 VIZ | 创建付费订阅的费用 |
| `account_on_sale_fee` | asset（VIZ） | 10.000 VIZ | 将账户挂牌出售的费用 |
| `subaccount_on_sale_fee` | asset（VIZ） | 100.000 VIZ | 将子账户创建权挂牌出售的费用 |
| `witness_declaration_fee` | asset（VIZ） | 10.000 VIZ | 验证者注册的一次性费用 |
| `create_invite_min_balance` | asset（VIZ） | 10.000 VIZ | 最低邀请余额 |

### 质押提取

| 属性 | 类型 | 默认值 | 描述 |
|------|------|-------|------|
| `withdraw_intervals` | uint16 | 28 | SHARES 解除质押的每日分期数 |

---

## 属性版本

属性分阶段随硬分叉引入：

| 版本 | 索引 | 硬分叉 | 新增字段 |
|------|------|-------|---------|
| `chain_properties_init` | 0 | 创世 | account_creation_fee、maximum_block_size、委托参数、策展、带宽、标记成本、最低 rshares 投票、委员会阈值 |
| `chain_properties_hf4` | 1 | HF4 | inflation_witness_percent、inflation_ratio_committee_vs_reward_fund、inflation_recalc_period |
| `chain_properties_hf6` | 2 | HF6 | data_operations_cost_additional_bandwidth、witness_miss_penalty_percent、witness_miss_penalty_duration |
| `chain_properties_hf9` | 3 | HF9 | create_invite_min_balance、committee_create_request_fee、create_paid_subscription_fee、account_on_sale_fee、subaccount_on_sale_fee、witness_declaration_fee、withdraw_intervals |

所有新的验证者属性提交请使用版本索引 3（`chain_properties_hf9`）。

---

## 治理循环

```
SHARES 持有者 → 为验证者投票
验证者 → 发布首选属性值
区块链 → 取活跃集合的中位数
中位数 → 作为实时网络规则应用
```

更改参数需要**大多数活跃验证者**发布新值。流程：
1. 社区讨论所需变更（例如降低费用）。
2. 验证者更新其发布的属性。
3. 用户将投票转移给发布所需值的验证者。
4. 一旦大多数活跃验证者发布新值，中位数就会移动。
5. 新值自动生效——无需硬分叉或治理投票。

---

## 读取当前属性

```json
{ "method": "database_api.get_chain_properties", "params": [] }
```

返回当前生效的中位数属性。参见 [Database API](../plugins/database-api.md#get_chain_properties)。

---

参见：[验证者](../protocol/operations/validators.md)、[Database API](../plugins/database-api.md)、[质押和 DAO](./staking-and-dao.md)。
