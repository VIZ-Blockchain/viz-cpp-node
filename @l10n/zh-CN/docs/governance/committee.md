# 委员会 DAO

委员会是 VIZ Ledger 的去中心化资助机制。任何账户都可以创建工作者请求（资金提案）；所有 SHARES 持有者通过股权加权的双极投票来批准或拒绝它。

委员会基金从请求创建费用中积累，以及从分配给它的区块通胀份额中积累（由 `inflation_ratio_committee_vs_reward_fund` 链属性管理）。

---

## 操作

| 操作 | ID | 授权 | 描述 |
|------|----|----|------|
| `committee_worker_create_request_operation` | 35 | `creator` 的 regular | 创建资金请求 |
| `committee_worker_cancel_request_operation` | 36 | `creator` 的 regular | 取消自己的活跃请求 |
| `committee_vote_request_operation` | 37 | `voter` 的 regular | 对请求投票 |

完整字段详情参见[委员会操作](../protocol/operations/committee.md)。

---

## 投票机制

每个 SHARES 持有者可以在 **−10000 到 +10000**（基点）范围内用 `vote_percent` 投票：

- 正票：支持请求。
- 负票：反对请求。
- 零：移除投票。

**投票权重** = `effective_vesting_shares × vote_percent / 10000`。

在请求活跃期间可随时更改投票。

---

## 批准计算

当请求的 `end_time` 到达时：

```
max_rshares    = SUM(voter.effective_vesting_shares，所有投票者)
actual_rshares = SUM(voter.effective_vesting_shares × vote_percent / 10000)
```

**批准的三个条件：**

1. **参与度：** `max_rshares ≥ total_vesting_shares × committee_request_approve_min_percent / 10000`  
   （未满足 → 拒绝，参与度不足）

2. **共识：** `actual_rshares > 0`  
   （负数 → 拒绝，社区反对）

3. **最低支付：**  
   ```
   payout = required_amount_max × (actual_rshares / max_rshares)
   ```
   （如果 `payout < required_amount_min` → 拒绝）

所有条件满足 → **批准**，支付 = 计算值。

---

## 请求生命周期

```
[已创建，status=0]
    └── 投票期（5–30 天）
            ├── 参与度不足 → [已拒绝，status=2]
            ├── 净负数/低于最低值 → [已过期，status=3]
            ├── 创建者取消 → [已取消，status=1]
            └── 已批准    → [已批准，status=4]
                                └── 支付（每 200 个区块）
                                    └── [已完成，status=5]
```

---

## 支付处理

已批准请求（status 4）每 200 个区块（~10 分钟）从委员会基金获得支付。基金在所有当前已批准请求之间均等分配：

```
payment = min(committee_fund / count_approved_requests, remain_payout_amount)
```

生命周期中触发的虚拟操作：

| 虚拟操作 | 触发条件 |
|---------|---------|
| `committee_cancel_request_operation`（ID 38） | 请求在未批准情况下过期 |
| `committee_approve_request_operation`（ID 39） | 达到批准阈值 |
| `committee_payout_request_operation`（ID 40） | 支付已处理 |
| `committee_pay_request_operation`（ID 41） | 工作者获得付款 |

---

## 查询委员会状态

```json
{ "method": "committee_api.get_committee_request", "params": [42] }
{ "method": "committee_api.get_committee_request_votes", "params": [42] }
{ "method": "committee_api.get_committee_requests_list", "params": [0, 100] }
```

---

## 关键属性

- **双极性**：每个投票者以细粒度的强度表达支持或反对。
- **股权加权**：投票由锁定代币加权，使操纵代价高昂。
- **比例支付**：共识越强 → 支付越高（最高为 `required_amount_max`）。
- **自我资助**：创建费用流入委员会基金。
- **非托管**：无受信中介；协议自动强制执行所有规则。

---

参见：[委员会操作](../protocol/operations/committee.md)、[质押和 DAO](./staking-and-dao.md)、[链属性](./chain-properties.md)、[虚拟操作](../protocol/virtual-operations.md)。
