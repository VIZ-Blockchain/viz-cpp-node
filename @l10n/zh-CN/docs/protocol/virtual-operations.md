# 虚拟操作

虚拟操作由区块链自身在区块处理期间生成。它们**从不由用户广播** — 仅出现在账户历史和区块数据中，用于信息展示。

虚拟操作与普通操作共享相同的操作变体，但类型 ID 更高。可通过 `account_history` API 或区块流观察它们。

---

## 内容奖励 *（已弃用）*

### `author_reward_operation`（ID 26）

**触发条件：** 内容帖子结算

当作者从内容结算中获得其奖励份额时触发。

| 字段 | 类型 | 描述 |
|------|------|------|
| `author` | `account_name_type` | 内容作者 |
| `permlink` | `string` | 内容 permlink |
| `token_payout` | `asset`（VIZ） | 流动 VIZ 部分 |
| `vesting_payout` | `asset`（SHARES） | 锁仓部分 |

---

### `curation_reward_operation`（ID 27）

**触发条件：** 内容帖子结算

当策展人获得其策展奖励时触发。

| 字段 | 类型 | 描述 |
|------|------|------|
| `curator` | `account_name_type` | 策展人账户 |
| `reward` | `asset`（SHARES） | 策展奖励 |
| `content_author` | `account_name_type` | 被策展内容的作者 |
| `content_permlink` | `string` | 被策展内容的 permlink |

---

### `content_reward_operation`（ID 28）

**触发条件：** 内容帖子结算

当帖子到达结算时间时触发。

| 字段 | 类型 | 描述 |
|------|------|------|
| `author` | `account_name_type` | 内容作者 |
| `permlink` | `string` | 内容 permlink |
| `payout` | `asset` | 总结算金额 |

---

### `content_payout_update_operation`（ID 32）

**触发条件：** 内容结算重新计算（如投票变化后）

| 字段 | 类型 | 描述 |
|------|------|------|
| `author` | `account_name_type` | 内容作者 |
| `permlink` | `string` | 内容 permlink |

---

### `content_benefactor_reward_operation`（ID 33）

**触发条件：** 内容帖子结算 — 为每个受益人触发

| 字段 | 类型 | 描述 |
|------|------|------|
| `benefactor` | `account_name_type` | 受益人账户 |
| `author` | `account_name_type` | 内容作者 |
| `permlink` | `string` | 内容 permlink |
| `reward` | `asset` | 受益人奖励份额 |

---

## 锁仓提取

### `fill_vesting_withdraw_operation`（ID 29）

**触发条件：** 每个锁仓提取间隔触发

每个活跃提取路径每个间隔触发一次。

| 字段 | 类型 | 描述 |
|------|------|------|
| `from_account` | `account_name_type` | 提取账户 |
| `to_account` | `account_name_type` | 目标账户（可通过提取路由不同） |
| `withdrawn` | `asset`（SHARES） | 本间隔提取的 SHARES 数量 |
| `deposited` | `asset` | 存入 `to_account`（VIZ，或在 `auto_vest = true` 时为 SHARES） |

```json
[29, {
  "from_account": "alice",
  "to_account": "alice",
  "withdrawn": "35.714285 SHARES",
  "deposited": "10.000 VIZ"
}]
```

---

### `return_vesting_delegation_operation`（ID 34）

**触发条件：** `delegate_vesting_shares_operation` 零额度后 7 天返回窗口结束

| 字段 | 类型 | 描述 |
|------|------|------|
| `account` | `account_name_type` | 接收返还 SHARES 的账户 |
| `vesting_shares` | `asset`（SHARES） | 从临时状态返还的 SHARES |

---

## 验证者操作

### `shutdown_validator_operation`（ID 30）

**触发条件：** 验证者因票权不足被停用

| 字段 | 类型 | 描述 |
|------|------|------|
| `owner` | `account_name_type` | 被停用的验证者 |

---

### `validator_reward_operation`（ID 42）

**触发条件：** 生产区块 — 验证者获得区块奖励

| 字段 | 类型 | 描述 |
|------|------|------|
| `witness` | `account_name_type` | 验证者账户 |
| `shares` | `asset`（SHARES） | 区块奖励 |

```json
[42, {
  "witness": "alice",
  "shares": "1.234567 SHARES"
}]
```

---

## 网络事件

### `hardfork_operation`（ID 31）

**触发条件：** 网络硬分叉激活

| 字段 | 类型 | 描述 |
|------|------|------|
| `hardfork_id` | `uint32_t` | 硬分叉编号 |

---

## 奖励

### `receive_award_operation`（ID 48）

**触发条件：** `award_operation` 或 `fixed_award_operation`

为奖励的主要接收者触发。

| 字段 | 类型 | 描述 |
|------|------|------|
| `initiator` | `account_name_type` | 发起奖励的账户 |
| `receiver` | `account_name_type` | 接收奖励的账户 |
| `custom_sequence` | `uint64_t` | 奖励操作中的应用程序序列号 |
| `memo` | `string` | 奖励操作的备注 |
| `shares` | `asset`（SHARES） | 收到的 SHARES |

```json
[48, {
  "initiator": "alice",
  "receiver": "bob",
  "custom_sequence": 0,
  "memo": "great article!",
  "shares": "5.000000 SHARES"
}]
```

---

### `benefactor_award_operation`（ID 49）

**触发条件：** `award_operation` 或 `fixed_award_operation` 含受益人

每个受益人触发一次。

| 字段 | 类型 | 描述 |
|------|------|------|
| `initiator` | `account_name_type` | 发起奖励的账户 |
| `benefactor` | `account_name_type` | 受益人账户 |
| `receiver` | `account_name_type` | 奖励的主要接收者 |
| `custom_sequence` | `uint64_t` | 应用程序序列号 |
| `memo` | `string` | 奖励操作的备注 |
| `shares` | `asset`（SHARES） | 受益人收到的 SHARES |

---

## 委员会

### `committee_cancel_request_operation`（ID 38）

**触发条件：** 委员会资金请求在未达到批准阈值的情况下到期

| 字段 | 类型 | 描述 |
|------|------|------|
| `request_id` | `uint32_t` | 已取消请求的 ID |

---

### `committee_approve_request_operation`（ID 39）

**触发条件：** 委员会资金请求达到所需批准阈值

| 字段 | 类型 | 描述 |
|------|------|------|
| `request_id` | `uint32_t` | 已批准请求的 ID |

---

### `committee_payout_request_operation`（ID 40）

**触发条件：** 委员会请求结算处理完成

| 字段 | 类型 | 描述 |
|------|------|------|
| `request_id` | `uint32_t` | 已结算请求的 ID |

---

### `committee_pay_request_operation`（ID 41）

**触发条件：** 工作者从委员会基金收到付款

| 字段 | 类型 | 描述 |
|------|------|------|
| `worker` | `account_name_type` | 工作者账户 |
| `request_id` | `uint32_t` | 委员会请求 ID |
| `tokens` | `asset`（VIZ） | 支付金额 |

```json
[41, {
  "worker": "alice",
  "request_id": 42,
  "tokens": "250.000 VIZ"
}]
```

---

## 付费订阅

### `paid_subscription_action_operation`（ID 52）

**触发条件：** 执行 `paid_subscribe_operation` 或处理自动续订付款

| 字段 | 类型 | 描述 |
|------|------|------|
| `subscriber` | `account_name_type` | 订阅者账户 |
| `account` | `account_name_type` | 订阅提供者 |
| `level` | `uint16_t` | 订阅等级 |
| `amount` | `asset`（VIZ） | 支付金额 |
| `period` | `uint16_t` | 周期数 |
| `summary_duration_sec` | `uint64_t` | 订阅累计时长（秒） |
| `summary_amount` | `asset`（VIZ） | 迄今总支付金额 |

---

### `cancel_paid_subscription_operation`（ID 53）

**触发条件：** 订阅到期或自动续订余额不足

| 字段 | 类型 | 描述 |
|------|------|------|
| `subscriber` | `account_name_type` | 订阅者账户 |
| `account` | `account_name_type` | 订阅提供者 |

---

## 账户市场

### `account_sale_operation`（ID 57）

**触发条件：** `buy_account_operation` 成功完成

| 字段 | 类型 | 描述 |
|------|------|------|
| `account` | `account_name_type` | 已出售的账户 |
| `price` | `asset`（VIZ） | 出售价格 |
| `buyer` | `account_name_type` | 买家账户 |
| `seller` | `account_name_type` | 卖家（付款接收方） |

```json
[57, {
  "account": "alice",
  "price": "1000.000 VIZ",
  "buyer": "bob",
  "seller": "alice"
}]
```

---

### `bid_operation`（ID 62）

**触发条件：** 对拍卖中的账户出现新竞价

| 字段 | 类型 | 描述 |
|------|------|------|
| `account` | `account_name_type` | 被竞价的账户 |
| `bidder` | `account_name_type` | 出价账户 |
| `bid` | `asset`（VIZ） | 出价金额 |

---

### `outbid_operation`（ID 63）

**触发条件：** 之前的出价被更高出价取代

为被超出的账户触发；之前的出价金额将返还。

| 字段 | 类型 | 描述 |
|------|------|------|
| `account` | `account_name_type` | 被竞价的账户 |
| `bidder` | `account_name_type` | 被超出的账户 |
| `bid` | `asset`（VIZ） | 返还的出价金额 |

---

## 托管

### `expire_escrow_ratification_operation`（ID 59）

**触发条件：** 错过托管 `ratification_deadline` — `to` 和 `agent` 均未及时批准

所有锁定资金返还给 `from`。

| 字段 | 类型 | 描述 |
|------|------|------|
| `from` | `account_name_type` | 原始托管发送方 |
| `to` | `account_name_type` | 预期接收方 |
| `agent` | `account_name_type` | 托管代理人 |
| `escrow_id` | `uint32_t` | 托管 ID |
| `token_amount` | `asset`（VIZ） | 返还的代币金额 |
| `fee` | `asset`（VIZ） | 返还的手续费（托管未批准，代理人不获付款） |
| `ratification_deadline` | `time_point_sec` | 错过的截止时间 |

---

参见：[操作概述](./operations/overview.md)、[奖励](./operations/awards.md)、[委员会](./operations/committee.md)。
