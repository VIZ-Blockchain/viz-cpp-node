# 区块处理

区块应用、待处理交易管理和 fork 切换的内部机制。

---

## 概述

当节点通过 P2P 接收到新区块时，chain 插件调用 `database::push_block()`。序列为：

1. 临时从数据库中移除待处理（mempool）交易。
2. 应用传入的区块。
3. 重新应用未包含在区块中的待处理交易。

这由 `db_with.hpp` 中的 `without_pending_transactions` 辅助类管理。

---

## 关键数据结构

| 结构 | 类型 | 用途 |
|------|------|------|
| `_pending_tx` | `vector<signed_transaction>` | Mempool：等待包含在区块中的已接收交易 |
| `_popped_tx` | `deque<signed_transaction>` | 来自弹出区块（fork 切换期间）的交易；切换后重新应用 |
| `_pending_tx_session` | `optional<session>` | 覆盖所有待处理交易状态变化的撤销会话 |

---

## 区块应用流程

```
push_block(new_block)
  └─ without_pending_transactions(db, skip, _pending_tx, callback)
       ├─ pending_transactions_restorer ctor: clear_pending()
       ├─ callback: _push_block(new_block)      ← 应用传入的区块
       └─ ~pending_transactions_restorer()       ← 恢复待处理交易
```

### `_push_block()` 内部逐步说明

1. **早期拒绝检查**（见下文）。
2. 将区块推送到 `fork_db`。
3. 如果新 fork 头直接延伸当前头（`new_block.previous == head_block_id()`）：
   - 跳过 fork 切换逻辑，直接执行 `apply_block()`。
4. 如果新头更高且偏离当前头：
   - **投票加权 fork 比较**（HF12）— 参见 [Fork 解决](./fork-resolution.md)。
   - 弹出旧 fork 区块直到共同祖先。
   - 按顺序应用新 fork 区块。
5. `apply_block()` 运行交易评估器，更新动态全局属性，处理虚拟操作。
6. `update_last_irreversible_block()` — 如果 ≥14 个验证者已确认，则推进 LIB。

---

## 待处理交易恢复

`~pending_transactions_restorer()` 析构函数在新区块应用后按顺序处理两个列表。

### 步骤 1：重新应用 `_popped_tx`（来自 fork 切换）

```
对 _popped_tx 中的每个 tx：
    如果 time_elapsed > 200ms → 推迟（推回 _pending_tx）
    否则如果 is_known_transaction(tx) → 跳过（已在链中）
    否则 → _push_transaction(tx) → applied_txs++
```

### 步骤 2：重新应用 `_pending_transactions`（原始 mempool）

```
对 _pending_transactions 中的每个 tx：
    如果 time_elapsed > 200ms → 推迟
    否则如果 is_known_transaction(tx) → 跳过
    否则 → _push_transaction(tx) → applied_txs++
           遇到 transaction_exception → 丢弃（无效）
           遇到 fc::exception → 静默丢弃
```

### 步骤 3：记录摘要

如果有任何交易被推迟：
```
Postponed N pending transactions. M were applied.
```

---

## 时间限制

**`CHAIN_PENDING_TRANSACTION_EXECUTION_LIMIT` = 200 毫秒**

从恢复开始计时，超时后所有剩余交易推回 `_pending_tx` 而不应用。这防止节点在大型 mempool 上阻塞。

限制在以下情况触发：
- 有大量待处理交易的高吞吐量区块
- CPU 密集型操作
- 系统负载下

---

## 生成期间的区块大小限制

**`CHAIN_BLOCK_GENERATION_POSTPONED_TX_LIMIT` = 5**

在 `_generate_block()` 期间，会跳过超出 `maximum_block_size` 的交易。连续 5 个超大交易后，生成循环中断。这些交易保留在 `_pending_tx` 中等待下一个区块。

日志：
```
Postponed N transactions due to block size limit
```

---

## Fork DB 头部播种

在推送区块之前，`_push_block()` 确保当前数据库头块存在于 `fork_db` 中：

```
如果 new_block.previous == head_block_id()
   且 head_block_id() 不在 fork_db 中：
     从 block log 获取头块（DLT 模式下从 dlt_block_log）
     fork_db.start_block(head_block)
```

没有这个播种，有效的下一个区块会抛出 `unlinkable_block_exception`，因为它们的 `previous` 不在 `fork_db` 中。这也修复了生成自己区块的验证者节点 — `generate_block()` 将 `pending_block.previous = head_block_id()`。

---

## 直接延伸绕过

将区块推送到 `fork_db` 后，如果区块直接延伸数据库头：

```
如果 new_block.previous == head_block_id():
    → 跳过 fork 切换，直接执行 apply_block()
```

这处理了 `fork_db._head` 指向来自之前失败同步周期的过时更高区块的情况。没有此绕过，过时的头会触发 fork 切换逻辑，静默丢弃有效的下一个区块。

---

## 早期区块拒绝

`_push_block()` 应用几个早期拒绝检查以避免不必要的工作并防止无限同步循环：

| 检查 | 条件 | 操作 |
|------|------|------|
| 已应用 | `block.num ≤ head` 且 ID 与现有区块匹配 | 静默忽略（重复） |
| 不同 fork | `block.num ≤ head`，不同 ID，父节点不在 fork_db | 静默拒绝 |
| 远超前，间隔 > 100 | `block.num > head`，父节点未知，间隔 > 100 个区块 | 静默拒绝（内存保护） |
| 远超前，间隔 ≤ 100 | `block.num > head`，父节点未知，间隔 ≤ 100 | 允许进入 fork_db（缓存在 unlinked index 中） |
| 直接下一个区块 | `block.previous == head_block_id()` | 始终允许 |

100 个区块间隔阈值防止来自死 fork 链的内存膨胀，同时允许 P2P 同步期间正常的乱序区块处理。

---

## Fork 切换

当节点切换到不同的 fork 时：

1. `pop_block()` 移除当前头块；其交易移到 `_popped_tx`。
2. 重复直到到达共同祖先。
3. 从共同祖先到新头，按顺序应用新 fork 区块。
4. `~pending_transactions_restorer()` 先重新应用 `_popped_tx`，然后是原始 mempool。

已在新链中的交易通过 `is_known_transaction()` 静默跳过。

### 线性延伸 vs. 实际 fork

当父节点到来时，`fork_db` 中的 `_push_next()` 可以自动链接多个孤儿区块，导致 `fork_db._head` 在一次 `push_block()` 调用中跳过比数据库头多几个区块。代码区分：

- **线性延伸**（`branches.second.size() == 1` 且共同祖先 == 当前头）：不需要弹出操作；区块直接应用。
- **实际 fork 切换**（分叉分支）：完整的弹出并重新应用序列。

这种区别在 DLT 模式下至关重要，其中 LIB == head 且撤销会话已提交——线性延伸上的弹出循环将是无限的。

---

## 孤儿区块处理（Unlinked Index）

当父节点未知的区块到达时，`fork_db` 将其存储在 `_unlinked_index` 中。当缺失的父节点后来到达时：

1. `_push_block(parent)` 将父节点链接到链。
2. `_push_next(parent)` 迭代 `_unlinked_index` 寻找 `parent` 的子节点。
3. 子节点移到 `_index` 并递归链接。
4. `fork_db._head` 可能在一次调用中前进多个区块（触发线性延伸路径）。

---

## 基于罚分的节点软封禁

节点不会因发送不可链接的区块而立即被封禁。计数器累积：

| 路径 | 阈值 | 重置条件 |
|------|------|---------|
| 正常操作：头部或以下的不可链接区块 | 20 次罚分 | 同一节点接受有效区块 |
| 同步路径：通用区块拒绝 | 20 次罚分 | 同一节点接受有效区块 |
| 死 fork / 区块过老 | 立即封禁 | — |

诚实的节点可以从瞬时错误中恢复（快照重载、时序竞争、短暂的 micro-fork）。

---

## 验证者区块生产时序

验证者插件使用带 250ms 前瞻的 250ms 定时器：

1. 定时器每 **250ms** 触发一次（对齐到 250ms 系统时钟边界，最小睡眠 50ms）。
2. `maybe_produce_block()` 计算 `now = NTP_time + 250ms`。
3. `get_slot_at_time(now)` 找到当前槽位。
4. 如果槽位属于已配置的验证者且 `|scheduled_time - now| ≤ 500ms`，以确定性的 `scheduled_time` 作为时间戳生产区块。

```
槽位在 T=6.000，定时器在 T=5.750：
  now = 5.750 + 0.250 = 6.000 → 槽位匹配 → 生产
```

这在延迟阈值对面提供了 500ms 安全边际。

### 生产条件（按顺序检查）

| 条件 | 失败结果 |
|------|---------|
| 链已同步（或 `enable-stale-production`） | `not_synced` |
| `get_slot_at_time(now) > 0` | `not_time_yet` |
| 计划的验证者在我们配置的集合中 | `not_my_turn` |
| 链上非 null 签名密钥 | `not_my_turn` |
| 签名密钥的私钥在内存中 | `no_private_key` |
| 网络参与度 ≥ 阈值（HF12 之前） | `low_participation` |
| `|scheduled_time - now| ≤ 500ms` | `lag` |
| fork_db 中同高度无竞争区块 | `fork_collision` |
| 最后 21 个区块不全来自我们的验证者 | `minority_fork` |

---

## 配置常量

| 常量 | 值 | 描述 |
|------|---|------|
| `CHAIN_PENDING_TRANSACTION_EXECUTION_LIMIT` | 200 毫秒 | 区块推送后重新应用待处理交易的最大时间 |
| `CHAIN_BLOCK_GENERATION_POSTPONED_TX_LIMIT` | 5 | 生成期间跳过的最大连续超大交易数 |
| `CHAIN_BLOCK_SIZE` | 65536 字节 | 硬区块大小限制 |
| `maximum_block_size` | 动态（验证者中位数） | 软区块大小限制 |
| `CHAIN_BLOCK_INTERVAL` | 3 秒 | 区块生产间隔 |

---

## 调试日志前缀

| 前缀 | 含义 |
|------|------|
| `FORK-SWITCH-POP: popping head #H` | 正常 fork 切换——弹出旧 fork 区块 |
| `FORK-RECOVER-POP: popping head #H` | 错误恢复——回滚失败的 fork 切换 |
| `POP_BLOCK: db_head=#X fork_db_head=#Y` | 每次 `pop_block()` 调用前的状态 |
| `Fork switch: new_head=#X branches.first=N branches.second=M` | fork 切换前的分支；`M=0` 表示线性延伸 |

---

参见：[Fair-DPOS](./fair-dpos.md)、[Fork 解决](./fork-resolution.md)、[验证者节点](../node/validator-node.md)。
