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

结果 `median_props` 对象存储在 `validator_schedule_object` 中，并在所有区块处理中强制执行。

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
| `inflation_validator_percent` | uint16（bp） | 2000（20%） | 验证者在区块通胀中的份额 |
| `inflation_ratio_committee_vs_reward_fund` | uint16（bp） | 5000（50%） | 剩余通胀的分配：委员会基金 vs 奖励基金 |
| `inflation_recalc_period` | uint32（区块） | 806400（~28天） | 通胀重新计算的频率 |

通胀流程：`block_reward × inflation_validator_percent` → 验证者。剩余分配：`inflation_ratio_committee_vs_reward_fund` → 委员会基金；其余 → 奖励基金。

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
| `validator_miss_penalty_percent` | uint16（bp） | 100（1%） | 错过区块时的投票权重降低 |
| `validator_miss_penalty_duration` | uint32（秒） | 86400（1天） | 错过惩罚的持续时间 |

### 费用

所有费用进入委员会基金（DAO 国库）。

| 属性 | 类型 | 默认值 | 描述 |
|------|------|-------|------|
| `committee_create_request_fee` | asset（VIZ） | 100.000 VIZ | 创建委员会资金请求的费用 |
| `create_paid_subscription_fee` | asset（VIZ） | 100.000 VIZ | 创建付费订阅的费用 |
| `account_on_sale_fee` | asset（VIZ） | 10.000 VIZ | 将账户挂牌出售的费用 |
| `subaccount_on_sale_fee` | asset（VIZ） | 100.000 VIZ | 将子账户创建权挂牌出售的费用 |
| `validator_declaration_fee` | asset（VIZ） | 10.000 VIZ | 验证者注册的一次性费用 |
| `create_invite_min_balance` | asset（VIZ） | 10.000 VIZ | 最低邀请余额 |

### 质押提取

| 属性 | 类型 | 默认值 | 描述 |
|------|------|-------|------|
| `withdraw_intervals` | uint16 | 28 | SHARES 解除质押的每日分期数 |

### 委员会投票 (HF14)

对 DAO 委员会请求选票的反垃圾限制，自 HF14 起生效。这两个字段位于 **PM 结构**（`chain_properties_pm`，版本 5）而非基础 `chain_properties_hf9` —— 以免改动已生效的 hf9 wire 格式，破坏 HF14 之前验证者投票的原有位置布局。

| 属性 | 类型 | 默认值 | 描述 |
|------|------|-------|------|
| `committee_votes_per_request` | uint32 | 100 000 | 单个委员会请求在拒绝新投票前可累积的最大选票数 |
| `committee_vote_min_vesting` | asset (VIZ) | 1000.000 VIZ | 投票时所需的最低有效质押（换算为 SHARES），用于投出或修改委员会选票 |

---

## 属性版本

属性分阶段随硬分叉引入：

| 版本 | 索引 | 硬分叉 | 新增字段 |
|------|------|-------|---------|
| `chain_properties_init` | 0 | 创世 | account_creation_fee、maximum_block_size、委托参数、策展、带宽、标记成本、最低 rshares 投票、委员会阈值 |
| `chain_properties_hf4` | 1 | HF4 | inflation_validator_percent、inflation_ratio_committee_vs_reward_fund、inflation_recalc_period |
| `chain_properties_hf6` | 2 | HF6 | data_operations_cost_additional_bandwidth、validator_miss_penalty_percent、validator_miss_penalty_duration |
| `chain_properties_hf9` | 3 | HF9 | create_invite_min_balance、committee_create_request_fee、create_paid_subscription_fee、account_on_sale_fee、subaccount_on_sale_fee、validator_declaration_fee、withdraw_intervals |
| `chain_properties_hf13` | 4 | HF13 | distribution_epoch_length |
| `chain_properties_pm` | 5 | HF14 | ~30 个预测市场参数 + 终止开关 `pm_commit_reveal_enabled`、`pm_lazy_pool_enabled` + 投票上限 `committee_votes_per_request`、`committee_vote_min_vesting`、`pm_dispute_votes_per_market`、`pm_dispute_vote_min_vesting` |

所有新的验证者属性提交请使用版本索引 **5**（`chain_properties_pm`）。索引 4 为 `chain_properties_hf13`（`distribution_epoch_length`）。

### 预测市场参数 (v5, HF14) {#pm-parameters}

均为中位数投票；参见 [预测市场操作](../protocol/operations/prediction-markets.md)。

所有 PM 百分比均以 bp 计（10000 = 100.00%），与其他 `*_percent` 一致；不再使用千分比（‰）。

- **预言机：** `pm_min_oracle_insurance`、`pm_max_oracle_fee_percent`（**唯一**的费率治理上限——针对预言机 %）、`pm_oracle_registration_fee`、`pm_oracle_penalty_percent`、`pm_oracle_dispute_response_sec`、`pm_oracle_accept_window_sec`（默认 3600 = 1 小时——指定的预言机须在此窗口内接受或拒绝待定市场；超时后 cron 向创建者退还种子流动性，但**不**退还创建费，并作废市场 → `pm_market_expired`）。
- **风险 / 覆盖率** *（市场下注量的百分比，100 = 1.0×）：* `pm_listing_min_coverage_percent`（250 = 2.5×）——预言机保险覆盖低于其下注量此比例的市场，会从默认 `list_markets` 目录中隐藏（经 `show_risky` 显示）；`pm_betting_min_coverage_percent`（150 = 1.5×）——建议性阈值，发布供客户端在下注前要求显式风险确认（不在链上强制；须 `≤ pm_listing_min_coverage_percent`）。
- **市场：** `pm_min_liquidity`、`pm_market_creation_fee`、`pm_max_outcomes`、`pm_max_market_duration`。*（无聚合费率上限；creator/liquidity 费率无上限、自我约束；静态 `总和 ≤ 100%` 偿付不变式。）*
- **批次 / 承诺-揭示：** `pm_batch_epoch_blocks`、`pm_reveal_window_blocks`、`pm_min_batch_bet`、`pm_commit_no_reveal_penalty_percent`、`pm_commit_reveal_enabled`。
- **争议：** `pm_dispute_fee`、`pm_dispute_grace_sec`、`pm_dispute_vote_period_sec`、`pm_dispute_auto_close_sec`、`pm_dispute_approve_min_percent`、`pm_no_contest_penalty_percent`、`pm_dispute_reward_multiplier`（bp 乘数，10000 = 1×）、`pm_dispute_votes_per_market`（默认 100 000）、`pm_dispute_vote_min_vesting`（默认 1000.000 VIZ）。
- **时间惩罚：** `pm_default_time_penalty_percent`、`pm_max_time_penalty`。
- **懒惰池：** `pm_lazy_pool_enabled`、`pm_lazy_alloc_percent`、`pm_lazy_max_total_alloc_percent`、`pm_lazy_recall_step_percent`、`pm_lazy_lock_sec`、`pm_lazy_emergency_penalty_percent`、`pm_lazy_min_liquidity_fee_percent`（默认 200 = 2%——池拒绝为 `liquidity_fee_percent` 低于此奖励下限的市场共同提供流动性）。
- **杠杆（可选）：** `pm_leverage_enabled`、`pm_leverage_fund_percent`、`pm_leverage_max_per_position_bp`、`pm_leverage_max_position_ratio_percent`、`pm_leverage_min_market_liquidity`、`pm_leverage_safety_margin_percent`、`pm_leverage_max_slippage_percent`、`pm_leverage_m_factor_percent`、`pm_leverage_pool_profit_percent`、`pm_leverage_expiration_buffer_sec`、`pm_conversion_profit_cost_percent`。
- **公平性：** `pm_processing_cap_per_block`。

三个 `*_enabled` 标志（`pm_commit_reveal_enabled`、`pm_lazy_pool_enabled`、`pm_leverage_enabled`）为实时终止开关：验证者中位数可在无需新硬分叉的情况下停用承诺-揭示、懒惰池或杠杆。

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
