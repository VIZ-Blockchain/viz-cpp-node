# Fair-DPOS 共识

VIZ Ledger 使用**公平委托权益证明（Fair-DPOS）**——经典 DPoS 算法的扩展，增加了参与度执行和错过惩罚机制，以防止验证者在不实际生产区块的情况下收取奖励。

---

## 经典 DPoS 的工作原理

在标准 DPoS 中：
- 代币持有者为验证者账户投票（权重与其 SHARES 成比例）。
- 票数最高的验证者按轮询顺序被安排生产区块。
- 每 3 秒触发一个槽位；被安排的验证者要么生产区块，要么错过该槽位。

VIZ 每个调度轮次运行 **21 个活跃验证者**。

---

## "公平"扩展

在经典 DPoS 中，验证者可以无限期错过区块，仍然获得投票（有时还有奖励）。Fair-DPOS 增加了：

1. **参与度追踪** — 128 位位掩码追踪最近 128 个槽位。每个槽位标记为 1（已生产）或 0（已错过）。
2. **参与度阈值** — 如果最近槽位中有不到 `required-participation`% 被任何验证者填充（默认 33%），节点将不会生产。这可防止少数派 fork 场景。
3. **错过惩罚** — 错过区块的验证者会累积错过计数。在每次硬分叉评估时，表现最差的验证者可能被从活跃集合中移除。
4. **奖励共享**（HF13）— 验证者区块奖励部分重新分配给其投票者，将委托者激励与验证者表现对齐。

---

## 验证者计划

### 构建计划

每 `CHAIN_WITNESS_SCHEDULE_BLOCK_NUM` 个区块（21 个），链重新计算活跃验证者集合：

1. 选取总投票权重最高的 21 个验证者（委托给他们的 SHARES）。
2. 添加**时间份额验证者**——为排名较低的验证者提供偶尔参与的旋转槽位，防止完全集中。
3. 使用头区块 ID 作为熵种子对结果 21 个槽位进行洗牌（确定性洗牌 = 所有节点结果相同）。

生成的有序列表成为**当前计划**。每个位置对应一个 3 秒槽位。

### 槽位分配

给定墙时钟时间 `T`：

```
slot_num  = (T - genesis_time) / CHAIN_BLOCK_INTERVAL
scheduled = schedule[slot_num % num_scheduled_validators]
```

区块时间戳始终是**确定性槽位时间**，而非原始时钟：
```
block_time = genesis_time + slot_num × 3s
```

### 错过的槽位

当验证者错过其槽位时，`update_global_dynamic_data()` 递增 `current_aslot` 并在参与位掩码中将该槽位标记为已错过。其他验证者不会填充错过的槽位——无论如何，3 秒节奏继续到下一个槽位。

---

## 参与率

参与率为：

```
participation = popcount(recent_slots_filled) / 128
```

其中 `recent_slots_filled` 是槽位结果的 128 位滑动窗口。

**当参与度降至 `required-participation`（默认 33%）以下时，验证者生产被阻止**。这可防止少数派 fork 上的节点在大多数网络不可达时继续生产区块。

配置：
```ini
required-participation = 33   # 最低 %，0–99
```

---

## 最后不可逆区块（LIB）

当超过 2/3 的活跃验证者在某区块之上构建时，该区块变为不可逆。链在 `last_irreversible_block_num` 中追踪这一信息。

```
irreversibility_threshold = ceil(num_scheduled_validators * 2 / 3)
```

21 个验证者时：`ceil(21 × 2/3) = 14` 次确认。一旦 14 个验证者生产了从区块 N 派生的区块，区块 N 就成为 LIB。

**在紧急共识模式期间，LIB 推进被跳过**（参见[紧急共识](./emergency-consensus.md)）。

---

## 硬分叉投票

验证者通过 `validator_update_operation` 设置其上报的 `hardfork_version_vote` 参与硬分叉激活。当以下条件满足时，硬分叉 N 激活：

1. 当前验证者集合中的超级多数（>80%）已发出支持信号。
2. 硬分叉的计划激活时间戳已过。

两个条件必须同时满足。这允许网络运营者即使在计划时间过后也可以通过拒绝投票来阻止不需要的硬分叉。

---

## 少数派 Fork 保护

如果最近 21 个连续区块全部由属于此节点配置的验证者集合的验证者生产，验证者插件判断节点已被隔离，并自动回滚到 LIB。这就是**少数派 fork 保护**。

在以下情况下跳过此检查：
- `enable-stale-production = true`（开发/测试网）
- 紧急共识模式已激活

---

## 紧急共识模式

如果在 `CHAIN_EMERGENCY_CONSENSUS_TIMEOUT_SEC`（默认 1 小时）内没有生产区块，链切换到**紧急模式**：

- 所有 21 个验证者槽位分配给 `CHAIN_EMERGENCY_WITNESS_ACCOUNT`（"committee"）。
- 紧急验证者使用 `CHAIN_EMERGENCY_WITNESS_PUBLIC_KEY` 签名区块。
- 所有验证者惩罚被重置；已关闭的验证者重新启用。
- 紧急期间 LIB 推进暂停。
- 在 `CHAIN_EMERGENCY_EXIT_NORMAL_BLOCKS`（21）个连续正常区块将 LIB 推进到紧急开始区块之后，紧急模式退出。

完整详情参见[紧急共识](./emergency-consensus.md)。

---

## HF12：投票权重 Fork 比较

从硬分叉 12 开始，当同一区块高度存在两个竞争 fork 时，链使用**投票权重 fork 比较**而不是简单的最长链：

1. 对于每个 fork 分支，累加在该分支上生产区块的每个唯一验证者所委托的 SHARES 总量。
2. 对更长的链应用 **+10% 奖励**，以在平局时支持生产连续性。
3. 总投票权重更高的 fork 获胜。
4. 如果仍然平局，则维持当前 fork，直到两级 fork 冲突解决器的 21 次延迟超时触发。

完整的 fork 冲突算法参见[Fork 解决](./fork-resolution.md)。

---

## 配置摘要

| 设置 | 默认值 | 描述 |
|------|-------|------|
| `required-participation` | `33`（33%） | 生产区块所需的最低参与度 |
| `enable-stale-production` | `false` | 绕过参与度检查（仅限测试网） |
| `emergency-private-key` | — | 可选的紧急共识签名密钥 |
| 活跃验证者 | 21 | 在 `CHAIN_MAX_WITNESSES` 中硬编码 |
| 区块间隔 | 3 秒 | `CHAIN_BLOCK_INTERVAL` |
| LIB 阈值 | ⌈21 × 2/3⌉ = 14 | 确认不可逆性所需的区块数 |
| 紧急超时 | 3600 秒 | `CHAIN_EMERGENCY_CONSENSUS_TIMEOUT_SEC` |

---

## 关键源码位置

| 组件 | 文件 |
|------|------|
| 验证者计划构建 | `libraries/chain/database.cpp` — `update_witness_schedule()` |
| 参与位掩码更新 | `libraries/chain/database.cpp` — `update_global_dynamic_data()` |
| LIB 推进 | `libraries/chain/database.cpp` — `update_last_irreversible_block()` |
| 硬分叉投票 | `libraries/chain/database.cpp` — `process_hardforks()` |
| 生产循环 | `plugins/validator/validator.cpp` — `maybe_produce_block()` |
| 紧急模式激活 | `libraries/chain/database.cpp` — `check_emergency_consensus()` |
| HF12 Fork 比较 | `libraries/chain/database.cpp` — `compare_fork_branches()` |

---

参见：[区块处理](./block-processing.md)、[Fork 解决](./fork-resolution.md)、[紧急共识](./emergency-consensus.md)、[验证者节点](../node/validator-node.md)。
