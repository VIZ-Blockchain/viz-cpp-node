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
6. `check_block_post_validation_chain()` — 如果 ≥14 个验证者已为 LIB 上方的下一个区块发送区块后验证签名，则推进 LIB（快速路径，终结性约 4 秒）。参见下文[区块后验证：快速 LIB 终结性](#区块后验证快速-lib-终结性)。
7. `update_last_irreversible_block()` — 经典 DPOS 回退：基于 ≥14 个验证者在目标区块之后生产的区块推进 LIB（慢速路径，约 63 秒）。

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

## 区块后验证：快速 LIB 终结性

经典 Fair-DPOS 只有在 2/3 的验证者**生产**了目标区块之后的区块才推进 LIB——21 个验证者、每 3 秒一个区块，需要约 63 秒。区块后验证用显式的带外确认消息取代了这一机制，将终结时间缩短至约 4 秒。

### 工作原理

```
apply_block(N) 完成后：
  create_block_post_validation(N, block_id, producer)
    → 在 chainbase 中存储 validator_confirmation_object
    → 删除 LIB 以下的条目
    → 将列表上限设为 CHAIN_MAX_BLOCK_POST_VALIDATION_COUNT（20）条

验证者插件定时器触发：
  对每个持有已加载私钥的已调度验证者：
    confirmations = get_validator_confirmations(validator)
    对每条确认：
      sig = sign(chain_id + block_id)   ← 使用验证者签名密钥的 secp256k1
      p2p.broadcast_block_post_validation(block_id, validator, sig)
                                         ← fire-and-forget，非阻塞

接收对等节点（p2p_plugin handle_message，消息类型 6009）：
  从 sig 恢复公钥
  与 on-chain 的 validator.signing_key 比较
  如果匹配 → db.apply_block_post_validation(block_id, validator)
    → 标记该验证者已确认该区块
    → 调用 check_block_post_validation_chain()

check_block_post_validation_chain()：
  从（LIB + 1）开始遍历 validator_confirmation_index
  统计已确认每个区块的唯一已调度验证者数量
  如果确认数 ≥ ⌈2/3 × num_scheduled_validators⌉（≥ 14/21）：
    推进 last_irreversible_block_num
    提交至新 LIB 的撤销会话
    对下一个区块重复
```

### 线格消息

`block_post_validation_message`（类型 **6009**，legacy graphene 协议）：

```cpp
struct block_post_validation_message {
    block_id_type  block_id;
    std::string    witness_account;   // 验证者名称
    signature_type witness_signature; // sign(chain_id + block_id)
};
```

### 时序

| 阶段 | 耗时 |
|------|------|
| 区块生产并传播 | 0 – 1 秒 |
| 验证者签名并广播确认 | 约 0 秒（下一个插件 tick，250 毫秒） |
| 确认消息传播至所有对等节点 | 1 – 2 秒 |
| `check_block_post_validation_chain()` 收集 ≥14 个签名 | 1 – 2 秒 |
| **总终结时间** | **约 3 – 5 秒** |

经典 DPOS 路径（约 63 秒）仍作为回退保持活跃，应对确认消息丢失或尚未收到的情况。

### 约束条件

- 只有**当前打乱排列的调度**中的验证者才计入 2/3 阈值；调度外的验证者被跳过。
- 在**紧急共识**期间（`emergency_consensus_active = true`），`check_block_post_validation_chain()` 立即返回——LIB 仅通过经典路径推进，以避免恢复过程死锁。
- 确认列表上限为 **20 条**（`CHAIN_MAX_BLOCK_POST_VALIDATION_COUNT`）。低于当前 LIB 的条目在每个区块后删除。

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
