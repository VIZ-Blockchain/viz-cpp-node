# DLT P2P 架构

VIZ Ledger 的 P2P 层已从基于 Graphene 摘要的旧协议（`node.cpp`）重新设计为专用的 DLT 原生协议（`dlt_p2p_node.cpp`）。公共插件 API 保持不变——只替换了内部实现。

---

## 概述

```
之前：  p2p_plugin → graphene::network::node    (node.cpp, 6978 行, STCP, 库存 gossip)
之后：  p2p_plugin → dlt_p2p_node               (dlt_p2p_node.cpp, 2627 行, 原始 TCP, 范围式同步)
```

替换**就地**完成：相同的插件名称 `"p2p"`、相同端口（2001/4243）、相同的公共 API。所有依赖插件（验证者、snapshot 等）无需任何更改。

---

## 线格协议

原始 TCP——无 STCP 加密层。线格上的每条消息：

```
[4 字节: 数据大小 (uint32_t)] [4 字节: msg_type (uint32_t)] [N 字节: fc::raw::pack(T)]
```

### 消息类型（5100–5116）

| 类型 | ID | 描述 |
|------|----|------|
| `dlt_hello_message` | 5100 | 握手：协议版本、head/LIB、DLT 范围、节点/分叉状态 |
| `dlt_hello_reply_message` | 5101 | 握手回复：exchange_enabled、fork_alignment |
| `dlt_range_request_message` | 5102 | 请求区块 ID 范围 |
| `dlt_range_reply_message` | 5103 | 回复可用区块范围 |
| `dlt_get_block_range_message` | 5104 | 获取 start..end 的区块，带 prev_block_id 检查 |
| `dlt_block_range_reply_message` | 5105 | 回复：区块向量 + is_last 标志 |
| `dlt_get_block_message` | 5106 | 按 ID 获取单个区块 |
| `dlt_block_reply_message` | 5107 | 回复：区块 + next_available + is_last |
| `dlt_not_available_message` | 5108 | 区块不可用 |
| `dlt_fork_status_message` | 5109 | 向对等节点广播当前分叉/节点状态 |
| `dlt_peer_exchange_request` | 5110 | 请求已知对等节点列表 |
| `dlt_peer_exchange_reply` | 5111 | 回复对等节点列表 |
| `dlt_peer_exchange_rate_limited` | 5112 | 速率限制通知：等待 N 秒 |
| `dlt_transaction_message` | 5113 | 广播已签名交易 |
| `dlt_soft_ban_message` | 5114 | 断开封禁对等节点前的通知 |
| `dlt_gap_fill_request` | 5115 | 请求特定区块号以填补空缺 |
| `dlt_gap_fill_reply` | 5116 | 回复请求的区块 |

---

## Fiber 架构

所有 I/O 在单个 `fc::thread` 上使用协作式 fiber 运行——共享状态无需互斥锁：

| Fiber | 角色 |
|-------|------|
| 接受循环 | 等待传入连接；拒绝重复 IP |
| 读取循环（每对等节点） | 读取消息；分发到 `on_message()` |
| 周期性任务 | 重新连接、检查停滞、对等节点统计、mempool 清理 |

Fiber 在阻塞 I/O（`readsome()`、`writesome()`）时让出控制权，允许在单线程上处理多个对等节点而无需争用。

---

## 节点状态和对等节点生命周期

**节点状态：** `SYNC`（追赶中）/ `FORWARD`（实时，交换区块）

**对等节点生命周期状态：**
```
CONNECTING → HANDSHAKING → SYNCING → ACTIVE → DISCONNECTED → BANNED
```

超时：connecting=5s，handshaking=10s。重连退避：30s → 60s → … → 3600s，带 ±25% 抖动，稳定运行 5 分钟后重置。8 小时无响应后移除对等节点。

---

## 区块同步：SYNC 模式

处于 SYNC 模式的节点从 head 更高的对等节点顺序获取区块：

1. `request_blocks_from_peer()` — 发送 `dlt_get_block_range_message`，请求我们 head 之后最多 200 个区块。
2. `on_dlt_block_range_reply()` — 验证 `prev_block_id` 哈希链，应用每个区块。
3. `check_sync_catchup()` — 将我们的 head 与所有对等节点的 head 比较；赶上后切换到 FORWARD。
4. `sync_stagnation_check()` — 30s 无新区块后最多重试 3 次，然后切换到 FORWARD 并发出警告。

### 空缺填补

当我们的 head 与同步对等节点最早可用区块之间存在连续空缺时，`request_gap_fill()` 向 DLT 范围覆盖该空缺的任意对等节点发送 `dlt_gap_fill_request`（每次请求最多 100 个区块）。空缺填补在 SYNC 和 FORWARD 模式下均可工作：

- 由 `on_dlt_block_reply()`（检测到乱序区块）和 `periodic_task()`（每 5s）触发。
- 从启用交换的对等节点回退到任何 head 更高的活跃对等节点。
- 如果没有对等节点拥有所需区块，回退到 SYNC 模式。
- 大空缺以 100 个区块为块处理，请求间有 5s 冷却时间。

---

## 区块交换：FORWARD 模式

在 FORWARD 模式下，对等节点交换实时区块和交易：

- `exchange_enabled` 标志控制对等节点是否从我们这里接收新区块。
- 切换到 FORWARD 时，`dlt_fork_status_message` 发送到**所有**对等节点（不仅仅是启用交换的），通知它们我们已就绪。
- `on_dlt_fork_status()` 在对等节点从 SYNC 切换到 FORWARD 时重新评估 `exchange_enabled`。
- `check_forward_stagnation()` — 如果 head 30s 未推进且至少一个对等节点领先，切换到 SYNC。

---

## 分叉对齐和交换资格

在握手期间，`check_fork_alignment()` 执行多层区块 ID 匹配以确定对等节点是否在同一分叉：

| 检查 | 条件 |
|------|------|
| 空对等节点 | `head_block_num == 0` → 已对齐（新节点） |
| 范围重叠 | 我们的 DLT 日志覆盖对等节点的 head → `is_block_known(head_id)` |
| 边界链接 | `peer_head + 1 == our_earliest` → 检查我们最早区块的 `previous == peer_head_id` |
| LIB 回退 | 始终检查 `is_block_known(lib_id)` |

这种多层检查防止了 DLT 模式下的误"不同分叉"断连，在 DLT 模式中旧区块已被修剪，旧的单 ID 检查对同一链上的对等节点会失败。

---

## 分叉解决

分叉解决子系统跟踪竞争链的顶端：

- **阈值：** 42 个区块的分歧触发 `resolve_fork()`（= `CHAIN_MAX_VALIDATORS × 2`，一个完整的调度轮次）。
- **选择：** 按投票权重最重的分支。
- **迟滞：** 切换前需要连续 6 个区块作为赢家（`CONFIRMATION_BLOCKS`）。
- **状态：** `_fork_status` 通过 `is_on_majority_fork()` 暴露，供验证者插件在生产区块前检查。

---

## 反垃圾

| 机制 | 描述 |
|------|------|
| `spam_strikes` 计数器 | 每个对等节点一个计数器；好数据包时重置；阈值=10 时软封禁 |
| 软封禁 | 设置 BANNED 状态持续 3600s；关闭前发送 `dlt_soft_ban_message` |
| 按 IP 去重 | 拒绝来自同一 IP 的重复连接（入站和出站均适用） |
| 广播去重 | `send_to_all_our_fork_peers()` 跟踪 `std::set<ip::address>` 跳过重复 IP |

来自范围回复的重复区块和乱序区块会被静默跳过——不计为垃圾。反序列化错误不增加 spam strikes 计数。

---

## P2P Mempool

独立的进程内 mempool（区别于链的 `_pending_tx`），在链接受前提供早期交易过滤：

- 按 `tx_id` **去重**。
- 达到限制时按最旧到期**驱逐**。
- **限制**（可配置）：最多 10,000 条、总计 100 MB、每笔交易 64 KB。
- **临时条目**在 SYNC 模式下标记；切换到 FORWARD 时重新验证。
- 收到区块时（`remove_transactions_in_block`）和分叉切换时（`prune_mempool_on_fork_switch`）**清理**。

---

## 对等节点交换

速率受限的对等节点发现：

- 每个对等节点每 5 分钟窗口最多 3 次请求。
- 子网多样性过滤器：每个回复中每个 `/24` 前缀最多 2 个对等节点。
- 只共享运行时间 ≥600s 的对等节点。
- 入站对等节点（临时端口）从交换回复中排除。

---

## 恢复机制

### 对等节点隔离（P53）

当 60 秒内没有活跃对等节点时，`emergency_peer_reset()`：
- 清除所有软封禁（BANNED → DISCONNECTED，重置 spam strikes）。
- 将所有断开连接的对等节点退避重置为最小值，立即重连。

### 区块处理暂停/恢复

`pause_block_processing()` / `resume_block_processing()` 允许 snapshot 插件在状态序列化期间暂停 P2P 区块接收。周期性任务在暂停时跳过访问数据库的操作。

### 启动宽限期（P22）

启动后前 60 秒内，head 10 个区块以内的区块被视为 `FORK_DB_ONLY` 而非 `DEAD_FORK`——防止 fork_db 从区块日志重建时的级联拒绝。

---

## 区块接受结果

`dlt_block_accept_result` 枚举替代旧的布尔返回：

| 值 | 含义 |
|----|------|
| `ACCEPTED` | 区块已应用到链（成为新 head） |
| `FORK_DB_ONLY` | 存储在 fork_db 但未应用（无法链接、竞争分叉） |
| `DEAD_FORK` | 死分叉中处于/低于 head 的区块——对等节点被软封禁 |
| `ALREADY_KNOWN` | 已有此区块（重复、`block_too_old_exception`） |
| `REJECTED` | 验证完全失败 |

---

## 配置参考

| 选项 | 默认值 | 描述 |
|------|--------|------|
| `dlt-block-log-max-blocks` | 100,000 | DLT 滚动区块日志中的最大区块数 |
| `dlt-peer-max-disconnect-hours` | 8 | 无响应后多少小时移除对等节点 |
| `dlt-mempool-max-tx` | 10,000 | Mempool 条目硬限制 |
| `dlt-mempool-max-bytes` | 100 MB | Mempool 总内存硬限制 |
| `dlt-mempool-max-tx-size` | 64 KB | 拒绝超大交易 |
| `dlt-mempool-max-expiration-hours` | 24 | 拒绝远期到期交易 |
| `dlt-peer-exchange-max-per-reply` | 10 | 每次交换回复的最大对等节点数 |
| `dlt-peer-exchange-max-per-subnet` | 2 | 反 Sybil：每 /24 最多 2 个对等节点 |
| `dlt-peer-exchange-min-uptime-sec` | 600 | 共享对等节点前的最小运行时间 |
| `dlt-stats-interval-sec` | 300 | 对等节点统计日志间隔（最小 30） |

---

## 彩色日志

| 颜色 | 含义 |
|------|------|
| 绿色 | 同步进度和区块生产 |
| 白色 | 正常区块交换 |
| 红色 | 分叉事件 |
| 深灰色 | 交易处理 |
| 橙色 | 警告（软封禁、停滞、空缺） |
| 青色 | 对等节点统计输出 |

---

## 委托模式

网络库只链接 `fc` 和 `graphene_protocol`——不链接 `graphene_chain`。`dlt_p2p_delegate` 抽象接口弥补了这一差距：

```
dlt_p2p_node（网络库）  ←→  dlt_p2p_delegate（接口）  ←→  dlt_delegate（p2p_plugin）
```

`p2p_plugin.cpp` 中的 `dlt_delegate` 实现：
- `read_block_by_num()` — 检查 dlt_block_log，然后检查 fork_db。
- `accept_block()` — 调用 `push_block()`；捕获 `unlinkable_block_exception` → 存储到 fork_db。
- `get_fork_branch_tips()` — 从当前 head 周围的 fork_db 获取。
- `is_tapos_block_known()` — 委托给 `db.is_known_block()`。

---

参见：[P2P 概述](../p2p/overview.md)、[同步场景](../p2p/sync-scenarios.md)、[Snapshot 插件](../storage/snapshots.md)、[区块日志](../storage/block-log.md)。
