# 转发模式 — 区块和交易交换

转发模式（`DLT_NODE_STATUS_FORWARD`）是节点追上网络后的正常运行状态。节点不再从对端拉取区块范围，而是在新区块和交易到达时**推送**给所有 fork 对齐的对端。

---

## 交付门控：`exchange_enabled`

所有转发模式流量通过每个对端的两个标志过滤：

| 标志 | 含义 |
|------|------|
| `exchange_enabled == true` | 对端已 fork 对齐 — 其 head 区块为我们所知（或我们的为其所知） |
| `lifecycle_state == ACTIVE` | 对端已完成握手 |

两者都必须为 true，对端才能接收区块和交易广播。

中央广播函数 `send_to_all_our_fork_peers()` 迭代所有连接对端，跳过未通过任一检查的对端。

---

## 设置 `exchange_enabled`

### 初始设置（hello 握手）

在 hello 期间，接收方调用 `check_fork_alignment()` — 一个多级的 DLT 范围感知检查：

| 情况 | 检查 |
|------|------|
| 对端无区块（`head_num == 0`） | → 对齐 |
| 对端 head 在我们的 DLT 范围内 | `is_block_known(peer.head_id)` |
| 对端 head + 1 == 我们的 DLT 最早区块 | 读取我们最早的区块；验证 `previous == peer.head_id` |
| LIB 回退 | `is_block_known(peer.lib_id)` |

任一检查通过 → `exchange_enabled = true`。

### OR 组合

双方互发 hello 消息，各自独立计算 `exchange_enabled`。对端的最终值是双方判定的**逻辑 OR**。如果任一方识别对方的链，交换即被启用。

head 落后于 master DLT 范围的 slave 的 `check_fork_alignment` 失败（它尚未应用 master 的区块），但 master 的检查成功（它知道 slave 的 head）。OR 确保即使在这种不对称情况下也启用交换。

### 重新评估触发器

当节点对对端区块的了解发生变化时，`exchange_enabled` 被重新评估：

| 触发器 | 时机 |
|--------|------|
| `transition_to_forward()` | 对每个 `exchange_enabled=false` 的对端；重新检查 `is_block_known(peer_head_id)` |
| `on_dlt_fork_status()` | 对端从 SYNC 过渡到 FORWARD；重新检查 fork 对齐 |
| 接受来自对端的区块 | 如果区块应用到我们的链，立即为该对端启用交换 |

---

## 区块广播

### 自产区块

当验证者生产区块时：

```
validator.cpp → p2p_plugin.broadcast_block(block)
              → dlt_p2p_node.broadcast_block(block)
              → send_to_all_our_fork_peers(dlt_block_reply_message, exclude=none, block_id=block.id())
```

区块发送给**所有** ACTIVE 且启用交换的对端。回声抑制防止向已有该区块的对端重复发送。

### 中继收到的区块

当区块从对端 X 到达时：

1. 记录 X 拥有此区块（`state.record_known_block(block.id())`）。
2. 将区块应用到链上。
3. `send_to_all_our_fork_peers(block_reply, exclude=X, block_id=block.id())` — 发送给所有其他启用交换的 ACTIVE 对端。

---

## 区块回声抑制

没有抑制时，区块会通过中继链回到生产者：

```
A 生产区块 N → 发送给 B、C
B 将 N 中继给 A、C
C 将 N 中继给 A、B
A 从 B 和 C 收回自己的区块 N — 浪费带宽
```

每个对端状态维护一个**最近 20 个区块 ID 的环形缓冲区**（`known_blocks`）。在向对端发送区块之前，节点检查 `peer.has_block(block_id)`。如果已知，则跳过发送。

对端在两种情况下被记录为"拥有"区块：
- **我们刚发送给它** — 在 `send_to_all_our_fork_peers` 发送后记录。
- **它发送给了我们** — 在 `on_dlt_block_reply` 接收时记录。

中继日志显示回声过滤计数：
```
Relay block_reply to 3 peers (0 skipped: no_exchange, 0 skipped: not_active, 1 skipped: echo)
```

---

## 交易广播

### 自发起（通过 API）

通过 `network_broadcast_api` 提交的交易 → 添加到 P2P 内存池 → `dlt_transaction_message` 发送给所有启用交换的 ACTIVE 对端。

### 中继收到的交易

交易从对端 X 到达 → 添加到内存池 → 中继给所有启用交换的 ACTIVE 对端（**除 X 外**）。

### 内存池预过滤

在交易被接受进内存池或转发之前，必须通过：

| 检查 | 失败 |
|------|------|
| 重复（`trx_id` 已在内存池中） | 静默跳过 |
| 过期（`expiration < now`） | 拒绝；如来自对端则增加 spam 计数 |
| 到期太远（未来 >24 小时） | 拒绝；增加 spam 计数 |
| 过大（>`dlt-mempool-max-tx-size`，默认 64 KB） | 拒绝；增加 spam 计数 |
| TaPoS 无效（引用区块未知） | 拒绝；增加 spam 计数 |
| 内存池已满 | 驱逐最早到期的条目，然后添加 |

**临时条目：** SYNC 模式期间收到的交易被标记为 `is_provisional = true` — 在本地存储但不转发给对端。过渡到 FORWARD 时，临时条目根据当前 head 重新验证，无效的被清除。

---

## SYNC → FORWARD 过渡

### 触发器

| 触发器 | 条件 |
|--------|------|
| 带 `is_last=true` 的区块范围回复 | 且至少应用了一个区块（不全是 dead-fork） |
| `check_sync_catchup()` | `our_head >= 所有活跃对端 head` 且至少有一个活跃对端 |
| 停滞超时 | 30 秒无区块，3 次重试耗尽 |

`check_sync_catchup()` 在每次区块接受后和周期任务每 5 秒运行一次。

**隔离防护：** 当零个活跃对端存在时，`check_sync_catchup()` **不**声称已追上。而是启动 60 秒隔离计时器；到期后触发 `emergency_peer_reset()`（见下文）。

### 过渡时的操作

1. 通知所有连接对端：向每个活跃/同步中的对端（不仅限于启用交换的）广播带 `node_status=FORWARD` 的 `dlt_fork_status_message`。这让对端立即重新评估我们的 `exchange_enabled`。
2. 为所有对端重新评估 `exchange_enabled`。
3. 重新验证并清除无效的临时内存池条目。
4. 重置 `_sync_stagnation_retries = 0`。
5. 重置 `_last_block_received_time = now`，使转发停滞计时器重新开始。

---

## FORWARD → SYNC 回退

如果区块在 FORWARD 模式下停止到达，节点回退到 SYNC：

| 触发器 | 条件 |
|--------|------|
| Hello 回复显示对端遥遥领先 | 收到 hello_reply 时 `peer_head_num > our_head + 2` |
| 周期检查 | `check_forward_behind()`：任何活跃对端有 `peer_head_num > our_head + 2` |
| 停滞 | `check_forward_stagnation()`：head 停滞 30 秒且至少有一个对端在前面 |

**当没有对端在前时的无操作：** 当所有连接对端有相同的 head 时，`check_forward_stagnation()` **不**过渡到 SYNC。没有东西可同步；过渡只会造成振荡。停滞计时器重置，节点保持在 FORWARD。

过渡到 SYNC 时，`_last_block_received_time` 重置为 `now`，使同步停滞计时器重新开始（而不是从 FORWARD 阶段继承）。

---

## 对端隔离恢复

当所有对端都断开连接或被封禁时（例如快照暂停后），正常的 SYNC/FORWARD 模式过渡无意义地循环。**60 秒**零活跃连接后：

`emergency_peer_reset()`：
1. 将所有 BANNED 对端移回 DISCONNECTED 状态；清除 `spam_strikes`。
2. 将所有 DISCONNECTED 对端的退避重置为 30 秒（`INITIAL_RECONNECT_BACKOFF_SEC`），`next_reconnect_attempt = now`。
3. 清除停滞重试计数器。
4. 在下一个周期任务 tick（~5 秒），`periodic_reconnect_check()` 立即重连。

---

## 不被转发的内容

| 场景 | 流量 |
|------|------|
| 对端有 `exchange_enabled=false` | 无区块，无交易 |
| 节点在 SYNC 模式 | 无广播；仅范围请求和 gap fill 请求 |
| 区块处理暂停（`_block_processing_paused=true`） | 接收区块并排队，但跳过周期性的数据库访问任务 |

---

## 交付摘要

| 事件 | 接收者 | 排除 | 回声过滤 |
|------|--------|------|---------|
| 节点生产区块 | 所有 ACTIVE 且 `exchange_enabled=true` 的对端 | （无） | `known_blocks` 中有区块的对端 |
| 节点从 X 收到区块 | 所有 ACTIVE 且 `exchange_enabled=true` 的对端 | X | `known_blocks` 中有区块的对端 |
| 节点发起交易 | 所有 ACTIVE 且 `exchange_enabled=true` 的对端 | （无） | （无） |
| 节点从 X 收到交易 | 所有 ACTIVE 且 `exchange_enabled=true` 的对端 | X | （无） |

---

## `peer_head_num` 是过时快照

[统计](./stats-reference.md)中显示的 `peer_head_num` 从以下来源更新：
- Hello 握手
- `dlt_fork_status_message` 交换
- 区块中继（收到区块 N 意味着 `peer_head_num ≥ N`）

在这些事件之间，对端的实际链 head 可能显著更高。不要将 `peer_head_num` 视为实时值。

---

参见：[P2P 概述](./overview.md)、[同步场景](./sync-scenarios.md)、[统计参考](./stats-reference.md)、[消息](./messages.md)。
