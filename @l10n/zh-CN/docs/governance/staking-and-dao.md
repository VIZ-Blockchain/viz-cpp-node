# 质押和 DAO 治理

## 质押（SHARES）

质押将流动 VIZ 代币转换为 **SHARES**（归属份额）。质押的代币被锁定，不能直接转让，但授予与质押数量成比例的治理权力。

每个账户有三个归属字段：

| 字段 | 含义 |
|------|------|
| `vesting_shares` | 账户拥有的 SHARES |
| `delegated_vesting_shares` | 委托给他人的 SHARES（减少权力） |
| `received_vesting_shares` | 通过委托收到的 SHARES（增加权力） |

**有效归属份额**——所有加权操作中使用的治理权力：

```
effective_vesting_shares = vesting_shares − delegated_vesting_shares + received_vesting_shares
```

---

## 质押操作

### 质押：`transfer_to_vesting_operation`（ID 3）

将流动 VIZ 转换为 SHARES。可以归属到另一个账户的余额。

```json
[3, {"from": "alice", "to": "alice", "amount": "1000.000 VIZ"}]
```

### 解质押：`withdraw_vesting_operation`（ID 4）

通过 `withdraw_intervals` 次每日分期付款启动逐步提取（由链治理，默认 28 天）。设置为 `"0.000000 SHARES"` 可取消。

```json
[4, {"account": "alice", "vesting_shares": "1000.000000 SHARES"}]
```

每个间隔代币释放时，虚拟 `fill_vesting_withdraw_operation` 触发一次。

### 提取路由：`set_withdraw_vesting_route_operation`（ID 11）

将一定百分比的提取路由到另一个账户，可选择立即重新归属。

```json
[11, {"from_account": "alice", "to_account": "bob", "percent": 5000, "auto_vest": true}]
```

每个账户最多 10 条路由；所有路由的总百分比不得超过 10000。

### 委托：`delegate_vesting_shares_operation`（ID 19）

将治理权力（非所有权）转让给另一个账户。设置为 `"0.000000 SHARES"` 可移除。

```json
[19, {"delegator": "alice", "delegatee": "bob", "vesting_shares": "500.000000 SHARES"}]
```

移除委托时，SHARES 进入 7 天返回窗口。返回时触发虚拟 `return_vesting_delegation_operation`。

---

## SHARES 的使用场景

SHARES 是通用治理代币。每个有意义的操作都按 `effective_vesting_shares` 加权。

### 1. 验证者投票（Fair-DPOS）

```json
[7, {"account": "alice", "witness": "bob", "approve": true}]
```

投票权重在账户投票的所有验证者之间**均等分配**：

```
fair_weight = effective_vesting_shares / validators_voted_for
```

这防止了集中——为 10 个验证者投票，每人获得你权重的 1/10。账户也可以设置代理（`account_validator_proxy_operation`），将所有验证者投票委托给另一个账户。

### 2. 委员会 DAO 投票

```json
[37, {"voter": "alice", "request_id": 42, "vote_percent": 7500}]
```

投票权重：`effective_vesting_shares × vote_percent / 10000`。  
范围：−10000（强烈反对）到 +10000（强烈支持）。

### 3. 奖励（社交奖励分配）

```json
[47, {"initiator": "alice", "receiver": "bob", "energy": 1000, ...}]
```

奖励大小与以下比例：
```
rshares = effective_vesting_shares × energy / 10000
```

拥有 10× 更多 SHARES 的账户以相同能量创造 10× 更大的奖励。

### 4. 链参数治理

验证者发布首选链参数；区块链应用中位数。由于验证者由股权加权投票选出，所有链参数间接由 SHARES 持有者治理。

### 5. 交易带宽

网络带宽按 `effective_vesting_shares` 比例分配。低于 500 SHARES 的账户获得额外 10% 带宽预留。

### 6. 通过委托创建账户

可以通过以 10× 比例将 SHARES 委托给新账户来引导账户创建（锁定 30 天），使无需流动代币即可创建账户。

---

## VIZ 作为 DAO

| 传统 DAO | VIZ 区块链 |
|---------|-----------|
| DAO 国库 | 委员会基金 + 奖励基金 |
| 治理代币 | SHARES |
| 提案投票 | 委员会工作者请求 |
| 董事会 | 当选验证者 |
| 董事选举 | 验证者投票（Fair-DPOS） |
| 股息分配 | 奖励机制（奖励基金） |
| 章程/规则 | 链属性（中位数治理） |
| 代理投票 | `delegate_vesting_shares` + 验证者代理 |

### 治理属性

1. **比例代表制**：1 SHARES = 各处 1 单位影响力。
2. **双极投票**：负票积极反对，而非仅仅弃权。
3. **持续治理**：没有固定投票季节——投票可随时更改。
4. **利益绑定**：SHARES 被锁定；退出需要 28 天。长期对齐。
5. **无受信中介**：所有规则由协议代码强制执行。
6. **无托管委托**：治理权力可随时借出和撤回。

### 治理循环

```
质押 VIZ → 获得 SHARES → 治理权力
    ├── 为验证者投票   → 区块生产和链参数
    ├── 委员会投票     → 国库支出
    ├── 奖励其他账户   → 从奖励基金分配价值
    └── 委托给他人     → 放大盟友的治理权力
```

---

## 关键常量

| 常量 | 值 | 描述 |
|------|---|------|
| `CHAIN_VESTING_WITHDRAW_INTERVALS` | 28 | 每日提取分期数 |
| `CHAIN_VESTING_WITHDRAW_INTERVAL_SECONDS` | 86400（1天） | 分期间隔 |
| `CHAIN_MAX_WITHDRAW_ROUTES` | 10 | 每账户最大提取路由数 |
| `CHAIN_ENERGY_REGENERATION_SECONDS` | 432000（5天） | 完全能量恢复时间 |
| `CHAIN_100_PERCENT` | 10000 | 基点分母 |
| `CHAIN_MAX_ACCOUNT_VALIDATOR_VOTES` | 100 | 每账户最大验证者投票数 |

---

参见：[链属性](./chain-properties.md)、[委员会 DAO](./committee.md)、[奖励](../protocol/operations/awards.md)、[转账](../protocol/operations/transfers.md)。
