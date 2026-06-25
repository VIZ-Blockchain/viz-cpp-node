# 预测市场操作 (Onix, HF14)

Onix 预测市场让 **预言机 (oracle)** 裁定一个 **市场**，下注者对其结果押注。两种定价引擎共用 **同一套结算模型** —— 严格 **零和 (zero-sum) 同注分彩 (parimutuel)**：输家的本金按曲线 **权重 (weight)** 按比例分给赢家，本金始终返还，**绝不增发代币**。

- **二元市场** (`market_type = 0`)：使用 **CPMM**（恒定乘积 `x·y=k`）定价；`weight = tokens_out`。
- **多元市场** (`market_type = 1`)：使用定点 **LMSR**（Q96）定价；`weight = tokens`。

所有金额均为 VIZ 的 `asset`（3 位小数）。对象引用为 `int64` id，由评估器解析为 chainbase 对象。每个操作都受 `has_hardfork(HF14)` 门控，并针对中位数 `chain_properties_pm`（治理 v5）校验。

**生命周期：**

```mermaid
flowchart TD
  REG[pm_oracle_register] --> CRE[pm_create_market]
  CRE --> ACC[pm_oracle_accept_market]
  ACC --> ACT([市场活跃])
  ACT --> BET[pm_place_bet / pm_add_liquidity<br/>pm_commit_bet → pm_reveal_bet]
  BET --> EXP{{betting_expiration}}
  EXP --> RES[pm_resolve_market | pm_no_contest]
  RES -. 争议 .-> DC[pm_dispute_create]
  DC -->|委员会模式| DV[pm_dispute_vote]
  DC -->|账户模式| DR[pm_dispute_resolve]
  RES --> GR{{宽限窗口}}
  DV --> GR
  DR --> GR
  GR ==> VOP[[虚拟 pm_auto_payout<br/>同注分彩结算 · 返还 LP 本金]]
```

> 下方的**操作 ID** 是每个操作在链上单一 `operation` variant 中的位置，延续 `stakeholder_reward_operation`（ID 65）之后的全局编号。

---

## 托管与零和不变量

下注 / 添加流动性 / 承诺托管会 **扣减** 账户余额；资金被 PM 对象持有（不触及 `current_supply` —— **无增发**）。结算时每一个 milli-VIZ 都会被支付回去：

```
Σ winner_payout + oracle_take + creator_take + lp_bonus + LP本金
    == Σ 所有下注 + LP本金 + forfeit_pool
```

协议费用（`pm_market_creation_fee`、`pm_oracle_registration_fee`）进入 DAO 基金（`dynamic_global_property_object.committee_fund`），与 `committee_worker_create_request` 一致。

---

## 常规操作

### `pm_oracle_register_operation`（ID 66）
**Auth：** `owner` 的 `active`

以保证金注册预言机。`pm_oracle_registration_fee` → DAO 基金；`insurance` 从余额锁定。

| 字段 | 类型 | 说明 |
|------|------|------|
| `owner` | `account_name_type` | 预言机账户 |
| `insurance` | `asset`（VIZ） | 保证金权益，`≥ pm_min_oracle_insurance` |
| `fee_percent` | `uint16_t` | 常驻裁定费（bp，10000 = 100%），`≤ pm_max_oracle_fee_percent`。建议价目——具有约束力的费用在接受时按市场报价 |
| `fixed_fee` | `asset`（VIZ） | 常驻每市场固定费（`≥ 0`）；建议性 |
| `rules_url` | `string` | 资料/规则，`≤ MAX_PM_PROFILE_URL_LEN` |
| `auto_accept` | `bool` | 若为 `true`，匹配市场在创建时即 **上线**，无需手动 `pm_oracle_accept`（默认 `false`） |
| `auto_accept_creator` | `account_name_type` | 将自动接受限于此创建者；**空 = 任意创建者** |
| `auto_accept_resolver` | `account_name_type` | 自动接受所需的争议设置：**空 = 仅委员会**（`dispute_mode == 0`）；填名 = 仅 `dispute_mode == 1` 且 `dispute_resolver` 等于该账户的市场 |

> **为何解析者锁定重要。** 自动接受仅在市场常驻条款（`fee_percent`/`fixed_fee`）处于预言机价目与链上限内、预言机已质押且未封禁、且争议设置匹配 `auto_accept_resolver` 时触发。解析者锁定可阻止做市商在本会自动接受的预言机下塞入**串通的争议解析者**：留空以强制公开委员会争议，或仅填你信任的那个解析者。不匹配的市场只是回退到常规手动接受（pending）流程。

### `pm_oracle_update_operation`（ID 67）
**Auth：** `owner` 的 `active`

增减保证金并修改策略。字段均可选。提取至 `pm_min_oracle_insurance` 以下或在服务活跃市场时被拒绝。

| 字段 | 类型 | 说明 |
|------|------|------|
| `insurance_delta` | `optional<asset>` | 带符号：`>0` 充值，`<0` 提取 |
| `fee_percent` | `optional<uint16_t>` | 新建议费（bp） |
| `fixed_fee` | `optional<asset>` | 新固定费 |
| `rules_url` | `optional<string>` | 新规则 url |
| `auto_accept` | `optional<bool>` | 开关自动接受 |
| `auto_accept_creator` | `optional<account_name_type>` | 设定允许的创建者（空 = 任意） |
| `auto_accept_resolver` | `optional<account_name_type>` | 设定所需解析者（空 = 仅委员会） |

### `pm_create_market_operation`（ID 68）
**Auth：** `creator` 的 `active`

创建市场；创建者注入首笔流动性并成为首个 LP。`pm_market_creation_fee` → DAO 基金。多元市场的 `lmsr_b` 须等于节点的 `lmsr_b_from_liquidity(liquidity, N)`。当 `dispute_mode == 1` 时，`dispute_resolver` 须存在且**不得**等于 `oracle` 或 `creator`（防自裁）。

| 字段 | 类型 | 说明 |
|------|------|------|
| `creator` / `oracle` | `account_name_type` | 创建者；已注册预言机（自预言机时为创建者） |
| `market_type` | `uint8_t` | 0 二元（CPMM），1 多元（LMSR） |
| `outcomes` | `vector<string>` | 2（二元）或 3..`pm_max_outcomes` 个标签；每个 `≤ MAX_PM_OUTCOME_LABEL_LEN` |
| `url` | `string` | 裁定标准，`≤ MAX_PM_MARKET_TITLE_LEN` |
| `oracle_fee_percent` | `uint16_t` | 预言机 % 的**报价上限**（bp）：创建者愿付的上限。预言机在接受时报出实际值（`≤` 此）。自预言机：最终值，`≤ pm_max_oracle_fee_percent` |
| `oracle_fixed_fee` | `asset`（VIZ） | 预言机固定费的**报价上限**；预言机在接受时报出 `≤` 此 |
| `creator_fee_percent` / `liquidity_fee_percent` | `uint16_t` | 创建者自身费用（bp），创建时最终；无治理上限（自限） |
| `liquidity` | `asset`（VIZ） | 种子，`≥ pm_min_liquidity` |
| `lmsr_b` | `share_type` | 仅多元：客户端计算的 b（节点校验） |
| `betting_expiration` / `result_expiration` | `time_point_sec` | `result > betting`；`≤ now + pm_max_market_duration` |
| `time_penalty_type/value`、`penalty_curve_type` | `uint8/uint32/uint8` | 迟到下注惩罚配置（仅利润，1e6 标度） |
| `allow_early_resolution/cancellation/batch/instant_bet` | `bool` | 标志（多元强制 `instant_bet=true`；`batch` 需 `pm_commit_reveal_enabled`） |
| `endogeneity_tier` | `uint8_t` | 1 经济数据 / 2 体育 / 3 政治 |
| `dispute_mode` | `uint8_t` | 0 委员会 / 1 账户 |
| `dispute_resolver` | `account_name_type` | 当 `dispute_mode==1` 时必填；≠ oracle/creator |

### `pm_oracle_accept_market_operation`（ID 69）
**Auth：** `oracle` 的 `active`

预言机接受（`status → active`）或拒绝（流动性退还创建者；`status → deleted`）一个待定市场。接受时预言机通过 `oracle_fee_percent` + `oracle_fixed_fee` **报出其实际条款**——各须 `≤` 该市场创建者的报价，且 `oracle_fee_percent ≤ pm_max_oracle_fee_percent`。报价被**冻结入市场**并发出 `pm_market_accepted` 虚拟操作（使历史解析器看到上线 + 条款）。此后结算只读这些冻结字段——绝不读实时中位值。

| 字段 | 类型 | 说明 |
|------|------|------|
| `market_id` | `int64` | 待定市场 |
| `accept` | `bool` | 接受（true）或拒绝（false） |
| `oracle_fee_percent` | `uint16_t` | 预言机报出的 %（bp）；`≤` 报价 & `≤ pm_max_oracle_fee_percent` |
| `oracle_fixed_fee` | `asset`（VIZ） | 预言机报出的固定费；`≤` 报价 |

### `pm_place_bet_operation`（ID 70）
**Auth：** `account` 的 `active`

按实时曲线即时下注。`min_tokens` 为滑点下限。`weight` 由收到的 CPMM/LMSR 代币确定。

| 字段 | 类型 | 说明 |
|------|------|------|
| `market_id` | `int64` | 目标市场 |
| `side` | `int8_t` | 二元：0/1；多元：-1 |
| `outcome_index` | `int16_t` | 多元：0..N-1；二元：-1 |
| `amount` | `asset`（VIZ） | 下注（`> 0`） |
| `min_tokens` | `share_type` | 滑点下限（0 = 无） |
| `mode` | `uint8_t` | 0 即时，1 批次 |

### `pm_commit_bet_operation`（ID 71）
**Auth：** `account` 的 `active`

承诺-揭示阶段 1（需 `allow_batch` + `pm_commit_reveal_enabled`）。托管 `≥ pm_min_batch_bet`。`commitment = H(market_id ‖ account ‖ side ‖ outcome_index ‖ amount ‖ min_tokens ‖ salt)`。`no_reveal_fee_percent` **必须等于** `median(pm_commit_no_reveal_penalty_percent)`（共识校验）并在承诺时快照。

### `pm_reveal_bet_operation`（ID 72）
**Auth：** `account` 的 `active`

承诺-揭示阶段 2：揭示下注并排入下一批次纪元。`amount ≤ escrow_amount`（多余退还）。节点据揭示字段 + `salt` 重算 commitment 并拒绝不符。已承诺却从未揭示的下注，按 `pm_commit_forfeit` 损失其托管的 `no_reveal_fee_percent`（→ `forfeit_pool`）。

### `pm_cancel_bet_operation`（ID 73）
**Auth：** `account` 的 `active`

取消未结/排队下注（需 `allow_cancellation`）。`min_return` 为退款滑点下限。

### `pm_add_liquidity_operation`（ID 74）
**Auth：** `provider` 的 `active`

向活跃市场添加流动性。结算时本金无条件返还，外加按比例的 LP 奖励份额（流动性费 + 时间惩罚池 + 零头）。

### `pm_withdraw_liquidity_operation`（ID 75）
**Auth：** `provider` 的 `active`

提取流动性（本金安全）。自 `betting_expiration` 至裁定锁定。`amount = 0` 提取整个头寸。

### `pm_resolve_market_operation`（ID 76）
**Auth：** `oracle` 的 `active`

预言机裁定为 `winning_outcome`。开启争议宽限窗口（`result_expiration + pm_dispute_grace_sec`）；其过后由 `pm_auto_payout` 结算。

### `pm_no_contest_operation`（ID 77）
**Auth：** `oracle` 的 `active`

预言机作废市场（全部下注退还，LP 本金返还）。可争议。一份 `pm_no_contest_penalty_percent` 的争议费从保证金罚没并分给被退款的下注者。

### `pm_dispute_create_operation`（ID 78）
**Auth：** `disputer` 的 `active`

在宽限窗口内提起争议；托管 `pm_dispute_fee`。设定预言机响应截止以及（委员会模式）投票/自动关闭计时器。

### `pm_dispute_vote_operation`（ID 79）
**Auth：** `voter` 的 `regular`（与 `committee_vote_request` 一致）

委员会模式投票。`vote_outcome = -1` 维持预言机；否则提议正确结果。`vote_percent ∈ [-10000, 10000]` 为信念权重，在 finalize 时按 `|vote_percent|` 计票。

委员会争议是**公开听证**——**没有 commit-reveal**（刻意且永久的选择：DAO 透明地裁决争议以维护平台信誉）。由于开放投票期间会浮现新论据，**选票可修改**：在 `voting_end_time` 之前重新发送 `pm_dispute_vote` 会**覆盖**先前投票（以最后一张为准）。实时计票可通过 `get_dispute_votes` 查看。

### `pm_dispute_resolve_operation`（ID 80）
**Auth：** `resolver` 的 `active`

由市场配置的 `dispute_resolver` 做出账户模式裁决。可罚没 `penalty_amount` 的保证金，并将预言机/创建者封禁至给定时间。

### `pm_transfer_position_operation`（ID 81）
**Auth：** `from` 的 `active`

将下注 `weight` 的全部/部分转给另一账户（不影响市场）。`memo` 为明文或 `#`-前缀 ECIES（标准 VIZ memo）。

### `pm_lazy_deposit_operation`（ID 82）
**Auth：** `account` 的 `active`

存入懒惰流动性池（HF14 仅分配，无杠杆）。铸造池份额（MasterChef 记账，`reward_per_share` 标度 1e9）。

### `pm_lazy_withdraw_operation`（ID 83）
**Auth：** `account` 的 `active`

销毁池份额以提取本金 + 待领奖励。`emergency = true` 在 `unlock_time` 之前对利润施加 `pm_lazy_emergency_penalty_percent`（罚金留在池中，加入 `reward_per_share`）。

---

## 虚拟操作

由 PM 共识逻辑发出——或由已签名操作的求值器（`pm_market_accepted`、各杠杆 vop），或由逐块截止处理器 `process_pm_markets()` 在市场达到**到期 / 截止 / 争议宽限 / 纪元边界**时发出（上限 `pm_processing_cap_per_block`，最早截止优先）。出现在账户历史中，不签名。

| ID | 操作 | 触发 |
|----|------|------|
| 84 | `pm_batch_settle_operation` | 纪元边界：排队下注按纪元开盘快照执行 |
| 85 | `pm_commit_forfeit_operation` | `reveal_deadline` 未揭示：罚金 → `forfeit_pool`，其余退还 |
| 86 | `pm_auto_payout_operation` | 争议宽限到期：同注分彩结算 + 返还 LP 本金 |
| 87 | `pm_dispute_finalize_operation` | 委员会投票结束：计票裁定；预言机罚金；重判/维持 |
| 88 | `pm_dispute_auto_close_operation` | 预言机未响应：防冻结退款，罚没保证金 → DAO |
| 89 | `pm_oracle_missed_penalty_operation` | 预言机错过 `result_expiration`：罚没 → DAO，全额退还下注 |
| 90 | `pm_lazy_recall_operation` | 分阶段回收闲置的懒惰池分配 |
| 94 | `pm_leverage_liquidate_operation` | 求值器——市场进行中的杠杆清算（对向 `0` / 取消 `1` 下注级联） |
| 95 | `pm_leverage_resolve_operation` | 结算——杠杆头寸被强制关闭：`outcome_index`、`won`、`pool_received`/`bettor_received`、`leverage` |
| 96 | `pm_market_accepted_operation` | 求值器——市场上线：预言机接受、自预言机或自动接受；冻结条款 + `self_oracle` 标志 |
| 97 | `pm_payout_operation` | 结算——每个有效下注：`amount`（本金）、`side`/`outcome_index`、`payout`（**输则为 0**） |

---

## 结算（同注分彩，零和）

```
losers_sum   = Σ 输家下注金额
oracle_fee   = floor(losers_sum × oracle_fee_bp   / 10000)     // bp, 10000 = 100%
creator_fee  = floor(losers_sum × creator_fee_bp  / 10000)
liq_fee      = floor(losers_sum × liquidity_fee_bp / 10000)
oracle_fixed = min(oracle_fixed_fee, losers_sum − fees)        // 从池中支付，不增发
winners_pool = losers_sum − oracle_fee − creator_fee − liq_fee − oracle_fixed + forfeit_pool

对每个获胜下注 i（按曲线权重）：
    profit_i  = floor(winners_pool × weight_i / Σ weight)
    penalty_i = floor(profit_i × time_penalty_i / 1_000_000)   // 仅利润 → LP
    payout_i  = amount_i + profit_i − penalty_i

LP：本金无条件返还 + 按比例分得 (liq_fee + Σpenalty + 取整余数)
```

**边界情形：** 全部押中 → `winners_pool` 仅为 `forfeit_pool`（退还本金）；无获胜代币 → 整池转为 LP 奖励；无效结果 → 全额退还 + LP 本金。纯结算逻辑在 `tests/pm/parimutuel_test.cpp` 中按精确守恒做单元测试。

---

## 设计决策：仅用真实深度（不引入虚拟/幻影流动性）

*虚拟*（幻影）流动性偏移——为压平价格冲击而加入定价曲线、但**无真实资本支撑**、并在结算时删除的储备（可由中位数投票调节）——在「下注→取消→结算」的封闭环路中是价值守恒的（即 vAMM 技术），作为冷启动稳定器对薄市场颇具诱惑。**Onix 有意不实现它。** 惰性池自动分配已用**真实**资本提供同样的启动平滑——而且该资本赚取手续费、有可问责的所有者、并按市场跟随需求。

否决幻影深度的原因在于：若使用不慎，它会损害市场结构与信任：

1. **可伪造的深度** —— 薄市场或被操纵的市场可被装扮成看似深而有流动性，侵蚀本应由真实且昂贵的资本承载的价格信号。
2. **扭曲信息聚合** —— 压平权重曲线会削弱对早期正确信息的奖励，并使价格对新消息不敏感（陈旧预测）。合适的深度因市场与成交量而异；单一治理常数无法跟踪。
3. **有条件的偿付能力** —— 仅在从不被赎回、也不被用作抵押时才偿付。一旦它支撑取消、提前撤资或杠杆贷款，就必须处处将其排除，否则会泄漏真实资金（例如按虚假深度计量/回收的杠杆 → 给池存款人带来真实坏账）。
4. **无所有者、无收益、无问责** —— 它不承担风险、也不为任何真实方赚取手续费，会在其触及的市场上删除面向散户的无风险收益产品。

因此 Onix 只保留**真实数字**：每一单位深度都是真实资本（可赎回、可赚取手续费、可问责），通过惰性池（`pm_lazy_deposit` + 自动分配）提供。这一自觉取舍，是为价格信号的完整性与每条真实资金路径的偿付能力，放弃廉价的虚拟稳定器。
