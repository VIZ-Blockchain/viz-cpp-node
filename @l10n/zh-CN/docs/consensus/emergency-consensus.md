# 紧急共识模式

紧急共识模式（在 HF12 中引入）在网络停滞 1 小时后自动激活。特殊的"committee"验证者接管区块生产，以在真实验证者恢复其签名密钥之前维持链的连续性。

---

## 关键常量

| 常量 | 值 | 含义 |
|------|---|------|
| `CHAIN_EMERGENCY_CONSENSUS_TIMEOUT_SEC` | 3600 秒 | 激活前的不活动时间 |
| `CHAIN_EMERGENCY_VALIDATOR_ACCOUNT` | `"committee"` | 紧急区块生产者账户 |
| `CHAIN_EMERGENCY_VALIDATOR_PUBLIC_KEY` | `VIZ75CR...` | 确定性紧急签名密钥 |
| `CHAIN_EMERGENCY_EXIT_NORMAL_BLOCKS` | 21 | 退出所需的连续真实验证者区块数 |
| `CHAIN_IRREVERSIBLE_THRESHOLD` | 75% | 退出所需的计划槽位比例 |
| `CHAIN_MAX_VALIDATORS` | 21 | 最大验证者槽位数 |

### `dynamic_global_property_object` 中的状态字段

| 字段 | 默认值 | 含义 |
|------|-------|------|
| `emergency_consensus_active` | `false` | 紧急模式已激活 |
| `emergency_consensus_start_block` | `0` | 激活时的区块号 |

---

## 激活

`update_global_dynamic_data()` 在每个应用的区块上运行并检查：

1. **HF12 门控** — 如果硬分叉尚未激活则跳过。
2. **已激活** — 如果紧急模式已经开启则跳过。
3. **block log 中有 LIB 区块** — 如果 LIB 区块不在 block log 中则跳过（快照恢复后的 DLT 节点有空的 block log；缺少 LIB 区块将计算数百万过时秒数并触发错误的激活死锁）。
4. **超时** — 计算 `seconds_since_lib = current_block.timestamp − lib_block.timestamp`。如果 `< 3600` 则跳过。

所有检查仅使用区块内嵌的时间戳——没有系统时钟，没有跳过标志。这保证了在每个节点上，包括重放，具有相同的确定性激活。

### 激活序列

当超过超时阈值时：

1. 设置 `dgp.emergency_consensus_active = true` 和 `dgp.emergency_consensus_start_block = block_num`。
2. 创建或更新"committee"验证者对象：
   - `signing_key = CHAIN_EMERGENCY_VALIDATOR_PUBLIC_KEY`
   - `props = current_median_props`
   - 硬分叉投票设置为当前应用版本（中性投票者）。
3. 禁用所有真实验证者：设置 `signing_key = zero`，重置 `penalty_percent = 0`，`current_run = 0`。
4. 删除所有 `witness_penalty_expire` 对象。
5. 覆盖验证者计划：所有 `CHAIN_MAX_VALIDATORS` 槽位 → "committee"。
6. 通知 fork DB：`_fork_db.set_emergency_mode(true)`（启用确定性哈希平局决胜）。
7. 日志：`"EMERGENCY CONSENSUS MODE activated at block #N. No blocks for X seconds since LIB Y."`

---

## 紧急操作

### 区块生产——主节点 vs. 从节点

在 `config.ini` 中配置了 `emergency-private-key` 的节点是**紧急主节点**；所有其他节点是**从节点**。

```ini
# 仅限紧急主节点
emergency-private-key = 5Jzzz...   # CHAIN_EMERGENCY_VALIDATOR_ACCOUNT 私钥
```

| 角色 | DLT 同步检查 | 少数派 fork 检查 | 生产 |
|------|------------|---------------|------|
| 主节点 | 绕过（会死锁） | 跳过（预期所有区块都是"我们的"） | 为所有槽位生产区块 |
| 从节点 | 正常 | 21 区块检查（1 个完整轮次） | 为自己计划的槽位标准验证者生产 |

### Fork DB 确定性平局决胜

紧急期间，多个主节点（地理冗余）可能在同一高度以相同密钥生产竞争区块。到达顺序因 P2P 拓扑而异。

`fork_database::_push_block()` 解决平局：
```
item->num == _head->num 且 _emergency_consensus_active：
    item->id < _head->id  →  _head = item  （较小的区块哈希获胜）
    否则                  →  保留当前 _head
```

所有节点收敛到同一链顶，无论它们先看到哪个区块。

### LIB 推进

`update_last_irreversible_block()` 正常计算 LIB，但在紧急期间**将其上限限制为 HEAD − 1**。没有上限，由于所有 21 个槽位都是"committee"且 `committee.last_supported_block_num == HEAD`，`nth_element` 计算返回 HEAD——这将在应用过程中提交当前区块的撤销会话，导致不可逆的损坏。

3 个 committee 区块（`CHAIN_IRREVERSIBLE_SUPPORT_MIN_RUN`）后，LIB 每个区块推进一次，保持 fork DB 窗口小。

### 混合验证者计划

`update_witness_schedule()` 在每个轮次构建混合计划：

- 真实验证者 `signing_key` 非零的槽位：保留真实验证者。
- 空或零密钥槽位：填充"committee"。

这允许真实验证者逐步返回。每次真实验证者通过 `validator_update_operation` 恢复其签名密钥时，下一次计划重建包含它们。

Committee 被排除在硬分叉版本统计和中位链属性计算之外（它复制当前中位数，因此按槽位计算会扭曲中位数）。

---

## 停用

每次计划重建检查退出条件：

```
real_witness_slots >= CHAIN_MAX_VALIDATORS × 75%
```

21 个验证者时：`21 × 0.75 = 15.75 → 15` 个真实验证者槽位需要。

当满足条件时：
1. `dgp.emergency_consensus_active = false`。
2. `_fork_db.set_emergency_mode(false)`。
3. 日志：`"EMERGENCY CONSENSUS MODE deactivated at block #N. R real validators active."`。

网络在下一个计划周期恢复正常运行。

---

## 启动恢复

`database::open()` 在重放后检查验证者计划。如果任何槽位为空（字符串 `""`），紧急状态在节点关闭时正在进行：

1. 如果 `emergency_consensus_active` 尚未设置 → 设置它并设置 fork DB 紧急标志。
2. 用"committee"填充所有槽位。
3. 日志：`"schedule repaired, all N slots set to committee"`。

这确保在紧急模式期间不干净关机后计划始终一致。

---

## 验证者守护集成

`validator_guard` 插件在紧急期间继续运行，实际上更加关键：

- 真实验证者在激活时被禁用（签名密钥设为 null）。
- 验证者守护自动广播 `validator_update_operation`，一旦在链上检测到 null 密钥就恢复每个验证者的签名密钥。
- 验证者守护中的 `enable-stale-production` 防护**不阻止**紧急模式下的密钥恢复（"紧急共识处理自己的恢复，密钥恢复可能仍然需要"）。
- 一旦 15 个验证者恢复密钥，退出条件触发。

参见[验证者守护](../node/validator-guard.md)。

---

## P2P 交互防护

几个 P2P 安全机制知道紧急模式：

| 防护 | 紧急期间的行为 |
|------|--------------|
| `resync_from_lib()` | **完全跳过** — 紧急期间弹出 LIB 附近的区块会崩溃 |
| `stale_sync_check_task()` | 如果主节点头部推进 → 重置计时器，跳过恢复；如果从节点头部卡住 → 允许恢复 |
| `handle_block()`（DLT，同步模式，间隔 0–2） | 视为正常（非同步）以防止生产循环中断 |
| 快照停滞同步检测 | 与停滞同步检查相同的逻辑 |

`resync_from_lib()` 防护最为关键：紧急期间，LIB 接近 HEAD。将区块弹回 LIB 并重置 fork DB 会导致来自真实网络的节点区块链接到重新播种的 LIB，触发 fork 切换，弹出到已提交的 LIB 以下，要么崩溃要么损坏状态。

---

## 区块验证——宽松的槽位映射

`verify_signing_witness()` 通常断言区块生产者与计划的验证者完全匹配。紧急期间：

```
如果 block.validator != scheduled_witness：
    dlog("Emergency mode: accepting block from BW at slot scheduled for SW")
    → 无论如何接受（签名仍然针对 block.validator 的 signing_key 验证）
```

这允许紧急主节点生产区块，即使待定计划中的几个槽位仍然分配给真实验证者。

---

## 快照兼容性

紧急状态字段以向前兼容的默认值包含在快照中：

- 快照中缺少 `emergency_consensus_active` → 默认为 `false`。
- 缺少 `emergency_consensus_start_block` → 默认为 `0`。

在活跃紧急期间创建的快照正确保留状态；在 HF12 之前创建的快照作为非紧急导入。

---

## 防护摘要

| # | 位置 | 防护 |
|---|------|------|
| 1 | `update_global_dynamic_data` | 仅在 HF12 + 未激活 + LIB 区块可用时激活 |
| 2 | `update_witness_schedule` | 混合覆盖 + 真实验证者 ≥75% 时的退出检查 |
| 3 | `update_last_irreversible_block` | 紧急期间将 LIB 上限设为 HEAD − 1 |
| 4 | `verify_signing_witness` | 放宽槽位到验证者的映射 |
| 5 | `fork_db._push_block` | 确定性哈希平局决胜 |
| 6 | `maybe_produce_block`（主节点） | 绕过同步、过时、参与；跳过少数派 fork |
| 7 | `maybe_produce_block`（从节点） | 必须先同步；21 区块隔离检查 |
| 8 | `resync_from_lib` | 紧急期间**完全跳过** |
| 9 | `stale_sync_check_task` | 主节点头部推进时跳过；从节点卡住时允许 |
| 10 | `handle_block` | DLT 紧急中几乎追上的区块视为正常 |
| 11 | `database::open` | 启动计划修复 |
| 12 | `validator_guard` | 紧急期间不抑制密钥恢复 |
| 13 | `snapshot import` | 向前兼容的字段处理 |
| 14 | `update_witness_schedule` | 从硬分叉版本统计中排除 committee |
| 15 | `update_median_witness_props` | 从中位数计算中排除 committee |

---

## 关键不变量

1. **确定性激活** — 仅使用区块内嵌的时间戳；在每个节点和每次重放上相同。
2. **DLT 快照安全** — 如果 LIB 区块不在 block log 中则跳过激活。
3. **紧急 fork 不变性** — `resync_from_lib()` 在紧急期间拒绝执行。
4. **主/从区分** — 只有具有 `--emergency-private-key` 的节点是主节点。
5. **Fork DB 收敛** — 确定性哈希平局决胜确保所有节点选择相同的区块。
6. **LIB 安全** — 上限为 HEAD − 1 以保留撤销保护。
7. **中性 committee 投票** — committee 为当前应用的硬分叉版本投票，复制中位数属性。

---

参见：[Fair-DPOS](./fair-dpos.md)、[Fork 解决](./fork-resolution.md)、[验证者节点](../node/validator-node.md)、[验证者守护](../node/validator-guard.md)。
