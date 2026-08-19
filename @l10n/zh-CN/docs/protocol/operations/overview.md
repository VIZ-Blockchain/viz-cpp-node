# 操作概述

VIZ Ledger 操作是包含在交易中的原子状态变更动作。每个操作在已签名交易内序列化为 2 元素数组 `[type_id, object]`。

---

## 常规操作

这些是用户发起的可以广播到网络的操作。

| ID | 操作 | 授权级别 | 参考 |
|----|------|---------|------|
| 0 | `vote_operation` *（已弃用）* | regular | [内容](./content.md) |
| 1 | `content_operation` *（已弃用）* | regular | [内容](./content.md) |
| 2 | `transfer_operation` | active（VIZ）/ master（SHARES） | [转账](./transfers.md) |
| 3 | `transfer_to_vesting_operation` | active | [转账](./transfers.md) |
| 4 | `withdraw_vesting_operation` | active | [转账](./transfers.md) |
| 5 | `account_update_operation` | master / active | [账户](./accounts.md) |
| 6 | `validator_update_operation` | active | [验证者](./validators.md) |
| 7 | `account_validator_vote_operation` | active | [验证者](./validators.md) |
| 8 | `account_validator_proxy_operation` | active | [验证者](./validators.md) |
| 9 | `delete_content_operation` *（已弃用）* | regular | [内容](./content.md) |
| 10 | `custom_operation` | active / regular | [内容](./content.md) |
| 11 | `set_withdraw_vesting_route_operation` | active | [转账](./transfers.md) |
| 12 | `request_account_recovery_operation` | active | [恢复](./recovery.md) |
| 13 | `recover_account_operation` | master（×2） | [恢复](./recovery.md) |
| 14 | `change_recovery_account_operation` | master | [恢复](./recovery.md) |
| 15 | `escrow_transfer_operation` | active | [托管](./escrow.md) |
| 16 | `escrow_dispute_operation` | active | [托管](./escrow.md) |
| 17 | `escrow_release_operation` | active | [托管](./escrow.md) |
| 18 | `escrow_approve_operation` | active | [托管](./escrow.md) |
| 19 | `delegate_vesting_shares_operation` | active | [转账](./transfers.md) |
| 20 | `account_create_operation` | active | [账户](./accounts.md) |
| 21 | `account_metadata_operation` | regular | [账户](./accounts.md) |
| 22 | `proposal_create_operation` | active | [提案](./proposals.md) |
| 23 | `proposal_update_operation` | 不定 | [提案](./proposals.md) |
| 24 | `proposal_delete_operation` | active | [提案](./proposals.md) |
| 25 | `chain_properties_update_operation` | active | [验证者](./validators.md) |
| 35 | `committee_worker_create_request_operation` | regular | [委员会](./committee.md) |
| 36 | `committee_worker_cancel_request_operation` | regular | [委员会](./committee.md) |
| 37 | `committee_vote_request_operation` | regular | [委员会](./committee.md) |
| 43 | `create_invite_operation` | active | [邀请](./invites.md) |
| 44 | `claim_invite_balance_operation` | active | [邀请](./invites.md) |
| 45 | `invite_registration_operation` | active | [邀请](./invites.md) |
| 46 | `versioned_chain_properties_update_operation` | active | [验证者](./validators.md) |
| 47 | `award_operation` | regular | [奖励](./awards.md) |
| 50 | `set_paid_subscription_operation` | active | [订阅](./subscriptions.md) |
| 51 | `paid_subscribe_operation` | active | [订阅](./subscriptions.md) |
| 54 | `set_account_price_operation` | master | [账户市场](./account-market.md) |
| 55 | `set_subaccount_price_operation` | master | [账户市场](./account-market.md) |
| 56 | `buy_account_operation` | active | [账户市场](./account-market.md) |
| 58 | `use_invite_balance_operation` | active | [邀请](./invites.md) |
| 60 | `fixed_award_operation` | regular | [奖励](./awards.md) |
| 61 | `target_account_sale_operation` | master | [账户市场](./account-market.md) |
| 64 | `set_reward_sharing_operation` | active | [验证者](./validators.md) |
| 66 | `pm_oracle_register_operation` | active | [预测市场](./prediction-markets.md) |
| 67 | `pm_oracle_update_operation` | active | [预测市场](./prediction-markets.md) |
| 68 | `pm_create_market_operation` | active | [预测市场](./prediction-markets.md) |
| 69 | `pm_oracle_accept_market_operation` | active | [预测市场](./prediction-markets.md) |
| 70 | `pm_place_bet_operation` | active | [预测市场](./prediction-markets.md) |
| 71 | `pm_commit_bet_operation` | active | [预测市场](./prediction-markets.md) |
| 72 | `pm_reveal_bet_operation` | active | [预测市场](./prediction-markets.md) |
| 73 | `pm_cancel_bet_operation` | active | [预测市场](./prediction-markets.md) |
| 74 | `pm_add_liquidity_operation` | active | [预测市场](./prediction-markets.md) |
| 75 | `pm_withdraw_liquidity_operation` | active | [预测市场](./prediction-markets.md) |
| 76 | `pm_resolve_market_operation` | active | [预测市场](./prediction-markets.md) |
| 77 | `pm_no_contest_operation` | active | [预测市场](./prediction-markets.md) |
| 78 | `pm_dispute_create_operation` | active | [预测市场](./prediction-markets.md) |
| 79 | `pm_dispute_vote_operation` | regular | [预测市场](./prediction-markets.md) |
| 80 | `pm_dispute_resolve_operation` | active | [预测市场](./prediction-markets.md) |
| 81 | `pm_transfer_position_operation` | active | [预测市场](./prediction-markets.md) |
| 82 | `pm_lazy_deposit_operation` | active | [预测市场](./prediction-markets.md) |
| 83 | `pm_lazy_withdraw_operation` | active | [预测市场](./prediction-markets.md) |
| 91 | `pm_leverage_open_operation` | active | [预测市场](./prediction-markets.md) |
| 92 | `pm_leverage_close_operation` | active | [预测市场](./prediction-markets.md) |
| 93 | `pm_leverage_convert_operation` | active | [预测市场](./prediction-markets.md) |
| 98 | `pm_dispute_oracle_respond_operation` | active | [预测市场](./prediction-markets.md) |
| 99 | `pm_unban_operation` | active | [预测市场](./prediction-markets.md) |

> ID 是链上单一 `operation` 变体中的固定索引（仅追加）。本表中的空缺为按 ID 交错的**虚拟**操作（见下文）—— 例如 62–63、65、84–90、94–97、100。

---

## 虚拟操作

虚拟操作由区块链本身在区块处理过程中生成。它们从不由用户广播——它们仅出现在账户历史和区块数据中供参考。

| ID | 操作 | 触发条件 | 参考 |
|----|------|---------|------|
| 26 | `author_reward_operation` | 内容支付 | [虚拟操作](../virtual-operations.md) |
| 27 | `curation_reward_operation` | 内容支付 | [虚拟操作](../virtual-operations.md) |
| 28 | `content_reward_operation` | 内容支付 | [虚拟操作](../virtual-operations.md) |
| 29 | `fill_vesting_withdraw_operation` | 提取间隔触发 | [虚拟操作](../virtual-operations.md) |
| 30 | `shutdown_validator_operation` | 验证者停用 | [虚拟操作](../virtual-operations.md) |
| 31 | `hardfork_operation` | 硬分叉激活 | [虚拟操作](../virtual-operations.md) |
| 32 | `content_payout_update_operation` | 内容支付更新 | [虚拟操作](../virtual-operations.md) |
| 33 | `content_benefactor_reward_operation` | 内容支付 | [虚拟操作](../virtual-operations.md) |
| 34 | `return_vesting_delegation_operation` | 委托返回窗口结束 | [虚拟操作](../virtual-operations.md) |
| 38 | `committee_cancel_request_operation` | 委员会请求过期 | [虚拟操作](../virtual-operations.md) |
| 39 | `committee_approve_request_operation` | 委员会请求获批 | [虚拟操作](../virtual-operations.md) |
| 40 | `committee_payout_request_operation` | 委员会支付处理 | [虚拟操作](../virtual-operations.md) |
| 41 | `committee_pay_request_operation` | 委员会工作者已付款 | [虚拟操作](../virtual-operations.md) |
| 42 | `validator_reward_operation` | 区块已生产 | [虚拟操作](../virtual-operations.md) |
| 48 | `receive_award_operation` | 奖励已接收 | [虚拟操作](../virtual-operations.md) |
| 49 | `benefactor_award_operation` | 带受益人的奖励 | [虚拟操作](../virtual-operations.md) |
| 52 | `paid_subscription_action_operation` | 订阅支付 | [虚拟操作](../virtual-operations.md) |
| 53 | `cancel_paid_subscription_operation` | 订阅取消/过期 | [虚拟操作](../virtual-operations.md) |
| 57 | `account_sale_operation` | 账户已售出 | [虚拟操作](../virtual-operations.md) |
| 59 | `expire_escrow_ratification_operation` | 托管截止日期错过 | [虚拟操作](../virtual-operations.md) |
| 62 | `bid_operation` | 拍卖出价 | [虚拟操作](../virtual-operations.md) |
| 63 | `outbid_operation` | 拍卖被超价 | [虚拟操作](../virtual-operations.md) |
| 65 | `stakeholder_reward_operation` | 向利益相关者的收益分成结算 | [验证者](./validators.md) |
| 84 | `pm_batch_settle_operation` | 批量纪元边界结算 | [预测市场](./prediction-markets.md) |
| 85 | `pm_commit_forfeit_operation` | 提交-揭示托管被没收（未揭示） | [预测市场](./prediction-markets.md) |
| 86 | `pm_auto_payout_operation` | 市场结算（按市场赔付标记） | [预测市场](./prediction-markets.md) |
| 87 | `pm_dispute_finalize_operation` | 委员会投票计票完成 | [预测市场](./prediction-markets.md) |
| 88 | `pm_dispute_auto_close_operation` | 争议防冻结自动关闭 | [预测市场](./prediction-markets.md) |
| 89 | `pm_oracle_missed_penalty_operation` | 预言机错过裁定截止 | [预测市场](./prediction-markets.md) |
| 90 | `pm_lazy_recall_operation` | 懒惰池渐进式召回步 | [预测市场](./prediction-markets.md) |
| 94 | `pm_leverage_liquidate_operation` | 杠杆头寸被清算 | [预测市场](./prediction-markets.md) |
| 95 | `pm_leverage_resolve_operation` | 杠杆头寸在裁定时结算 | [预测市场](./prediction-markets.md) |
| 96 | `pm_market_accepted_operation` | 市场上线（预言机接受 / 自预言机 / 自动） | [预测市场](./prediction-markets.md) |
| 97 | `pm_payout_operation` | 每下注者的同注分彩赔付 | [预测市场](./prediction-markets.md) |
| 100 | `pm_ban_expired_operation` | 临时预言机/创建者封禁失效 | [预测市场](./prediction-markets.md) |

---

## 交易构建

```json
{
  "ref_block_num": 12345,
  "ref_block_prefix": 678901234,
  "expiration": "2024-01-15T12:01:00",
  "operations": [
    [2, { "from": "alice", "to": "bob", "amount": "1.000 VIZ", "memo": "" }]
  ],
  "extensions": [],
  "signatures": ["1f2a3b..."]
}
```

- `ref_block_num` = `head_block_number & 0xFFFF`
- `ref_block_prefix` = `block_id` 的第 4–7 字节（小端 `uint32`）
- `expiration` = 当前 UTC 时间 + TTL（最长建议：60 秒）
- 签名：`sha256(chain_id || serialized_tx)` → 紧凑 secp256k1 ECDSA 签名

---

参见：[数据类型](../data-types.md)、[虚拟操作](../virtual-operations.md)、[JSON-RPC API](../../api/json-rpc.md)。
