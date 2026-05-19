# Fork 解决

本页介绍 VIZ Ledger 如何检测、选择和解决竞争性 fork——从基本的 fork 数据库到 HF12 投票加权碰撞解析器。

---

## Fork 数据库

**Fork 数据库**（`fork_database`）是候选链顶端的内存树。通过 P2P 接收的每个区块在应用于链状态之前都插入此处。

关键操作：
- `push_block(b)` — 将 `b` 链接到其父节点；如果父节点未知，缓存在 `_unlinked_index`
- `_push_next(item)` — 父节点到达时，迭代链接所有缓存的子节点
- `fetch_branch_from(a, b)` — 从两个分支追溯到共同祖先
- `set_max_size(n)` — 从 linked 和 unlinked 索引中修剪最旧的区块

### 重复检测

插入前，fork DB 检查具有相同 ID 的区块是否已存在。如果是，则静默忽略。这防止了 P2P 同步期间重新广播的区块被重复处理。

### Unlinked index

父节点尚未在 fork DB 中的区块存储在 `_unlinked_index` 中。当父节点后来到达时：
1. `_push_block(parent)` 链接父节点。
2. `_push_next(parent)` 迭代 `_unlinked_index` 寻找父节点的子节点。
3. 子节点移到 `_index` 并递归链接。
4. fork DB 头可能在一次调用中跳过多个区块。

间隔阈值（100 个区块）防止内存膨胀：父节点未知且比数据库头超前超过 100 个区块的区块在到达 fork DB 之前被静默拒绝。

---

## Fork 选择：最长链规则

插入区块后，fork DB 返回新头。如果新头高于数据库头且与之分叉，则尝试 **fork 切换**。

**HF12 之前：** 简单的最长链规则——区块号最高的 fork 获胜。

---

## HF12：投票加权 Fork 比较

从第 12 次硬分叉开始，当两个竞争 fork 在同一区块高度时，使用 `compare_fork_branches()` 而非简单的最长链：

### 算法

1. 通过 `fetch_branch_from(fork_a_tip, fork_b_tip)` **获取分支**到共同祖先。
2. **按验证者汇总投票权重**——每个分支中每个唯一验证者账户只计算一次。紧急验证者账户（`"committee"`）被排除。
3. 对更长的链**应用 +10% 奖励**。
4. **返回**：如果分支 A 更强则返回 `+1`，B 更强则返回 `-1`，平局返回 `0`。

### Fork 碰撞处理

从验证者插件调用 `compare_fork_branches()` 时：
- 如果一个 fork 明显更强 → 在该 fork 上生产。
- 如果平局或不确定 → 推迟（递增 `fork_collision_defer_count_`）。
- 连续 **21 次推迟**（一个完整的验证者轮次）后 → 超时：调用 `remove_blocks_by_number(height)` 清除过时的竞争区块，然后在规范链上生产。

| 条件 | `peer_needs_sync_items_from_us` 标志 |
|------|-------------------------------------|
| 回复为空 | `false` — 我们的链为空 |
| 回复 = synopsis 中 1 个条目 | `false` — 节点已更新 |
| 回复 >1 个条目，`remaining == 0` | `false` — 节点几乎更新（切换到库存模式） |
| 回复 >1 个条目，`remaining > 0` | `true` — 节点远落后（保持同步模式） |

---

## Fork 切换过程

当节点切换到更好的 fork 时：

```
1. fetch_branch_from(new_head, current_head)
   → branches.first  = [new_tip, ..., common_ancestor]
   → branches.second = [current_tip, ..., common_ancestor]

2. 线性延伸检查：
   branches.second.size() == 1 且 common_ancestor == head
   → 跳过弹出循环；直接应用 branches.first。

3. 实际 fork 切换：
   对 branches.second 中的每个区块（逆序）：
       FORK-SWITCH-POP: pop_block()      ← 保存 txs 到 _popped_tx
   对 branches.first 中的每个区块（逆序）：
       FORK-SWITCH-APPLY: apply_block()

4. 异常时：
   对上面已应用的每个区块：
       FORK-RECOVER-POP: pop_block()     ← 撤销部分应用
   使失败的 fork 无效。
   重新抛出异常。
```

**线性延伸**的区分在 DLT 模式下至关重要，其中 LIB == head：弹出循环将是无限的，因为撤销会话已提交。

---

## 不可逆区块确定

每次区块应用后，`update_last_irreversible_block()` 推进 Last Irreversible Block (LIB)：

1. 收集 21 个计划验证者各自的 `last_supported_block_num`。
2. 排序并取倒数第 `⌈21 × 25%⌉ = 5` 位（即 75% 的验证者处于该值或以上的值）。
3. 结果区块号成为新的 LIB。

区块成为 LIB 后，写入 `block_log`（DLT 模式下为 `dlt_block_log`），其撤销会话被提交。

**紧急共识模式期间 LIB 上限为 HEAD − 1**，以防止提交当前正在应用的区块的撤销会话。

---

## 过时 Fork 修剪

两种机制防止过时数据积累：

1. **`remove_blocks_by_number(num)`** — 删除特定高度的所有区块。在 21 次推迟超时后由 fork 碰撞解析器调用。
2. **`set_max_size(n)`** — 当 fork DB 超过 `n` 个条目时，从 `_index` 和 `_unlinked_index` 修剪最旧的区块。

---

## 少数派 Fork 守护

在每次区块生产之前，验证者插件检查 fork DB 中的最后 21 个区块：

- 如果所有 21 个都由该节点自己配置的验证者生产 → 节点在少数派 fork 上被隔离。
- 操作（`enable-stale-production = false`）：调用 `resync_from_lib()` — 弹出到 LIB，重置 fork DB，重新启动 P2P 同步，重新连接种子节点。
- 操作（`enable-stale-production = true`）：记录警告，继续生产。
- 紧急共识激活 → 跳过检查（预期紧急主节点的所有槽位都是"我们的"）。

---

## Fork 碰撞指标（HF12）

HF12 在 `dynamic_global_property_object` 中添加了两个字段用于链上监控：

| 字段 | 类型 | 描述 |
|------|------|------|
| `fork_collision_count` | `uint32_t` | 自 genesis 以来 fork 碰撞的累计次数 |
| `last_fork_collision_block_num` | `uint32_t` | 最近一次碰撞的区块号 |

通过 `get_dynamic_global_properties` 读取。

---

## Fork DB 诊断

Fork DB 公开 O(1) 访问器用于监控：

| 方法 | 返回 |
|------|------|
| `linked_size()` | linked index 中的区块数量 |
| `unlinked_size()` | unlinked index 中的区块数量 |
| `linked_min_block_num()` | linked index 中的最小区块号 |
| `linked_max_block_num()` | linked index 中的最大区块号 |
| `unlinked_min_block_num()` | unlinked index 中的最小区块号 |
| `unlinked_max_block_num()` | unlinked index 中的最大区块号 |

P2P 统计任务每 5 分钟记录这些：

```
Block storage | dlt_log: [79174319..79274318] | dlt_resizes: 412 | fork_db: linked=18 unlinked=0
```

不断增长且不减少的 `unlinked_size` 表明收到的区块流中存在持续性间隔（P2P 连接问题或节点处于隔离的 fork 上）。

---

## 故障排除

| 症状 | 诊断 |
|------|------|
| 生产结果 `fork_collision` | 目标高度有竞争区块；等待 21 次推迟超时或投票权重解决 |
| 生产结果 `minority_fork` | 节点被隔离；检查 P2P 节点和种子连接 |
| `unlinked_size` 无限增长 | 父区块未到达；检查 P2P 连接 |
| 日志中重复 fork 切换 | 两个验证者子集之间的网络分区；调查它们之间的连接 |
| DLT 模式下头部不推进 | 线性延伸与 fork 切换混淆；检查 `FORK-SWITCH-POP` 日志 |

---

参见：[Fair-DPOS](./fair-dpos.md)、[区块处理](./block-processing.md)、[紧急共识](./emergency-consensus.md)。
