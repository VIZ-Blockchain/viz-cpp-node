# P2P 网络概述

VIZ Ledger 使用自定义 DLT P2P 协议，取代了旧版基于 synopsis 的 graphene 网络层。新设计针对 DLT 模式（基于快照的节点，带滚动 block log）进行了优化，用更简单的基于范围的区块交换替代了复杂的 graphene 祖先 synopsis。

---

## 架构

```
┌─────────────────────────────────────────────────────────┐
│ p2p_plugin（AppBase 插件）                              │
│   └─ dlt_delegate（实现 dlt_p2p_delegate）              │
│        └─ 桥接链状态：db()、fork_db、block_log          │
├─────────────────────────────────────────────────────────┤
│ dlt_p2p_node                                            │
│   ├─ 接受循环（传入 TCP 连接）                          │
│   ├─ 周期性任务（5s 滴答：重连、统计、间隔）           │
│   └─ dlt_peer_state × N（每个已连接节点一个）          │
├─────────────────────────────────────────────────────────┤
│ 线格式：原始 TCP，头部（类型 + 长度）+ 数据             │
│ 纤程模型：所有 I/O 在一个 fc::thread，协作              │
└─────────────────────────────────────────────────────────┘
```

### 设计决策

| 决策 | 理由 |
|------|------|
| 委托模式 | `dlt_p2p_node` 仅链接 `fc` + `graphene_protocol`。链访问通过 `dlt_p2p_delegate` 暴露，避免循环依赖。 |
| 原始 TCP（无 STCP 加密） | DLT 紧急模式同时切换所有验证者——不需要向后兼容的加密。更简单的线格式。 |
| 协作纤程（fc::thread） | 所有 I/O 使用 `readsome()`/`writesome()`，让出纤程。一个线程上多个节点，无互斥锁。 |
| 独立 P2P mempool | 链的 `_pending_tx` 仅在接受后生效。P2P mempool 在推送到链之前按过期、TaPoS 和大小过滤，减少评估浪费。 |
| 就地插件替换 | 插件名称仍为 `"p2p"`，端口仍为 `2001`/`4243`，公共 API 不变。旧协议和新协议不兼容；双模式会创建隔离的子网。 |

---

## 节点生命周期

每个节点连接经历以下状态：

```
CONNECTING ──（TCP 已建立）──► HANDSHAKING
              5s 超时 ↓            ↓ 10s 超时
              DISCONNECTED    hello/hello_reply
                    ▲               ↓
                    │          SYNCING ──（已追上）──► ACTIVE
                    │                                     │
                    └──（断开/错误）──────────────────────┘
                                                          │
                    BANNED ◄──（spam_strikes ≥ 10）───────┘
```

**超时值：**
- Connecting → DISCONNECTED：**5 秒**
- Handshaking → DISCONNECTED：**10 秒**

**重连退避：** 30 秒 → 60 秒 → … → 3600 秒，带 ±25% 抖动。稳定连接 >5 分钟后退避重置。无响应 8 小时的节点永久移除。

**紧急节点重置：** 如果所有节点都被隔离（零活跃连接）持续 60 秒，`emergency_peer_reset()` 清除所有软封禁并将所有退避重置为初始值，立即尝试重连。

---

## Hello 握手

连接时，发起节点发送包含以下内容的 `dlt_hello_message`：

- `head_block_num` / `head_block_id`
- `lib_block_num` / `lib_block_id`
- `dlt_earliest_block_num` — 节点滚动 DLT block log 中可用的最旧区块
- `node_status` — SYNC 或 FORWARD

接收节点以包含以下内容的 `dlt_hello_reply_message` 响应：

- `fork_alignment` — 区块是否在同一 fork 上重叠
- `exchange_enabled` — 响应节点是否认为发送者已追上

### Fork 对齐检查（DLT 范围感知）

由于 DLT 节点修剪旧区块，朴素的 head-ID 比较会错误地将同链节点标记为"不同 fork"。检查是多层的：

| 情况 | 检查 |
|------|------|
| 节点没有区块（`head_num == 0`） | 已对齐 |
| 节点头在我们的 DLT 范围内 | `is_block_known(peer.head_id)` |
| 节点头 + 1 == 我们最早的区块 | 读取我们最早的区块，验证 `previous == peer.head_id` |
| 回退 | `is_block_known(peer.lib_id)` |

---

## 同步模式

每个节点在任何时刻处于以下两种模式之一：

### SYNC 模式（拉取）

当节点落后于网络时使用。节点以每次最多 **200 个区块**的范围向节点请求区块：

```
我们                        节点
 │──dlt_get_block_range──►│
 │◄──dlt_block_range_reply─│
 │   （最多 200 个区块）   │
 │──应用每个区块──►chain   │
 │                          │
 │  （当 is_last=true 时）  │
 │──transition_to_forward   │
```

**间隔检测：** 如果 `our_head + 1 < peer.dlt_earliest`（缺失的区块不再在节点的滚动日志中），节点搜索另一个能够弥合间隔的节点。如果没有节点能服务该间隔，建议导入快照。

**停滞保护：** 如果 30 秒内未收到区块，节点最多重试 3 次，然后带警告转换到 FORWARD 模式。

### FORWARD 模式（推送）

当节点已追上时使用。区块通过 `dlt_block_message` 传播。每个区块广播到所有共享相同 fork 的**启用了交换**的节点。

**FORWARD → SYNC 回退：** 如果节点的头在 **30 秒**内没有推进（`check_forward_stagnation`）且至少一个节点领先，节点重新进入 SYNC 模式。

### SYNC ↔ FORWARD 转换

| 转换 | 触发器 |
|------|-------|
| SYNC → FORWARD | 带 `is_last=true` 的区块范围回复 |
| SYNC → FORWARD | `check_sync_catchup()`：我们的头 ≥ 所有节点 |
| SYNC → FORWARD | 3 次重试后停滞 |
| FORWARD → SYNC | `check_forward_stagnation()`：头部 30s 停滞且节点领先 |
| FORWARD → SYNC | 间隔填充失败且无可用节点 |

SYNC → FORWARD 时，节点向所有已连接节点广播 `node_status=FORWARD` 的 `dlt_fork_status_message`，使其重新评估该节点的 `exchange_enabled`。

---

## 间隔填充

间隔填充是一种轻量级机制，用于获取少量特定区块而不进入完整 SYNC 模式。使用两种专用消息类型（`dlt_gap_fill_request` / `dlt_gap_fill_reply`），在三个地方触发：

1. 当乱序区块到达时（`on_dlt_block_reply`）
2. 每 5 秒从 `periodic_task()` 触发
3. 快照暂停完成后（`resume_block_processing()`）

**规则：**
- 每次请求最多 **100 个区块**（`GAP_FILL_MAX_BLOCKS`）；较大的间隔使用分块请求。
- 间隔填充请求之间 **5 秒冷却**。
- 请求节点选择头部区块号最高的活跃节点。
- 服务节点从其 DLT block log 读取区块；日志范围外的请求被拒绝。
- SYNCING 生命周期节点是合格候选（不仅限于 ACTIVE）。
- 如果找不到合适的节点，节点立即转换到 SYNC 模式。

---

## Mempool

DLT P2P 层维护自己的 mempool，独立于链的 `_pending_tx`。这允许在将交易推送到链评估器之前进行早期过滤。

**准入检查：**
- 按 `tx_id` 重复——收到时去重
- 过期——拒绝已过期的
- TaPoS（`tapos_block_num`）——如果引用区块未知则拒绝
- 大小——如果 `tx.size > dlt-mempool-max-tx-size`（默认 64 KB）则拒绝
- 到期范围——如果到期时间超过 `dlt-mempool-max-expiration-hours`（默认 24 小时）则拒绝

**驱逐：** 当 mempool 超过 `dlt-mempool-max-tx`（默认 10,000）或 `dlt-mempool-max-bytes`（默认 100 MB）时，最近到期的条目首先被驱逐。

**生命周期：**
- SYNC 期间接收的交易标记为**临时**，在转换到 FORWARD 时重新验证（TaPoS 区块现在可能已知）。
- 区块应用时，包含的交易被修剪（`remove_transactions_in_block`）。
- fork 切换时，TaPoS 无效条目被修剪（`prune_mempool_on_fork_switch`）。
- `periodic_mempool_cleanup()` 在每个周期删除过期和 TaPoS 无效条目。

---

## Fork 解决

DLT P2P 层以 **42 区块阈值**（2 个完整验证者轮次 = `CHAIN_MAX_WITNESSES × 2`）跟踪 fork 状态。

`track_fork_state()` 在每次区块应用后被调用。当检测到持续 ≥ 42 个区块的竞争 fork 时，`resolve_fork()` 通过总投票权重计算**最重的分支**。候选分支必须积累 **6 个连续确认区块**（`dlt_fork_resolution_state::CONFIRMATION_BLOCKS`）才能切换节点（滞后效应）。

当前 fork 状态通过 `is_on_majority_fork()` 暴露，验证者插件用它来决定是否生产区块。

---

## 反垃圾

每个节点有一个 **`spam_strikes`** 计数器：

- 在以下情况递增：无效区块、无效交易、协议违规
- 在以下情况重置：任何有效数据包
- 软封禁阈值：**10 次罚分**

软封禁的节点在连接关闭前收到 `dlt_soft_ban_message`（包含 `ban_duration_sec` 和人类可读的原因）。被封禁的节点进入 BANNED 状态，持续指定时间，到期前不会重连。

**按 IP 连接去重**防止来自同一节点的多个连接：
- `accept_loop()` 拒绝来自已有活跃条目的 IP 的传入连接。
- `connect_to_peer()` 如果目标 IP 已有活跃条目则跳过出站连接。
- 广播（`send_to_all_our_fork_peers`）跟踪 `set<ip::address>` 并跳过本次广播已发送的 IP。

**重复/乱序区块容忍度：**
- 已应用的区块静默跳过（不计为垃圾）。
- 范围回复中的乱序区块转到 `fork_db` 而不是触发软封禁。
- 反序列化错误不增加 spam strikes。
- 来自旧协议节点的超大消息触发断开连接而不增加退避时间。

---

## 节点交换

节点共享节点地址以辅助发现。

**速率限制：** 每个节点每 **5 分钟窗口 3 个请求**。

**共享节点地址前应用的过滤器：**
- 最小运行时间：**600 秒**
- 子网多样性：每 **/24** 子网最多 **2 个节点**
- 临时端口排除：`is_incoming` 节点从不共享（其端口是临时的）

**每次回复限制：** `dlt-peer-exchange-max-per-reply`（默认 10）。

---

## 区块处理暂停/恢复

快照插件（及其他需要独占访问的插件）可以通过 `pause_block_processing()` 暂停 P2P 区块摄取。暂停期间：

- `periodic_task()` 跳过需要数据库读锁的操作：`sync_stagnation_check()`、`periodic_peer_exchange()`、`log_peer_stats()`。
- 停滞同步和前向停滞计时器被重置，以便节点不进入不必要的模式转换。
- 无 DB 的日常工作继续：重连、生命周期管理、mempool 清理、封禁节点解封。

在 `resume_block_processing()` 时，节点在回退到 SYNC 模式之前尝试间隔填充。

---

## 配置

| 选项 | 默认值 | 描述 |
|------|-------|------|
| `p2p-endpoint` | `0.0.0.0:2001` | 监听地址和端口 |
| `seed-node` | — | 静态种子节点地址 |
| `p2p-max-connections` | — | 最大同时节点连接数 |
| `dlt-block-log-max-blocks` | 100000 | 滚动 DLT block log 容量 |
| `dlt-peer-max-disconnect-hours` | 8 | N 小时后移除无响应节点 |
| `dlt-mempool-max-tx` | 10000 | mempool 条目数硬上限 |
| `dlt-mempool-max-bytes` | 104857600 | mempool 总内存硬上限（100 MB） |
| `dlt-mempool-max-tx-size` | 65536 | 拒绝大于此值的交易（64 KB） |
| `dlt-mempool-max-expiration-hours` | 24 | 拒绝到期时间超过 N 小时的交易 |
| `dlt-peer-exchange-max-per-reply` | 10 | 每次节点交换回复的最大地址数 |
| `dlt-peer-exchange-max-per-subnet` | 2 | 每 /24 子网共享的最大节点数 |
| `dlt-peer-exchange-min-uptime-sec` | 600 | 共享地址前节点的最小运行时间 |
| `dlt-stats-interval-sec` | 300 | 节点统计日志输出间隔（最小 30 秒） |

---

## 节点统计日志

每 `dlt-stats-interval-sec`（默认 5 分钟），节点记录节点统计摘要：

```
[DLT-P2P] node=FORWARD head=#79274318 lib=#79274297 fork=MAJORITY
  peer 192.168.1.10:2001 ACTIVE  head=#79274318 exch=YES  dlt=[79174319..79274318] strikes=0
  peer 192.168.1.11:2001 SYNCING head=#79274100 exch=no   dlt=[79174319..79274100] strikes=0
  peer 192.168.1.12:2001 BANNED  ban_remaining=3540s
```

字段说明：
- `exch=YES/no` — 是否与此节点启用了区块/交易交换
- `dlt=[min..max]` — 节点可服务的 DLT block log 范围
- `strikes` — 当前 spam strike 计数（任何有效数据包重置）
- `ban_remaining` — 软封禁到期前的秒数

统计间隔可通过 `set_stats_log_interval()` 在运行时更新。

---

## 诊断摘要

| 症状 | 可能原因 |
|------|---------|
| 节点卡在 SYNC，头部不推进 | 我们的头和节点的 DLT 范围之间存在间隔——节点无法弥合；考虑快照导入 |
| SYNC ↔ FORWARD 快速振荡 | 没有节点领先，或所有节点被隔离——检查 `emergency_peer_reset` 日志条目 |
| 所有节点显示 `exch=no` | FORWARD 转换没有通知节点；应在下一个 `broadcast_chain_status` 周期自行解决 |
| `spam_strikes` 在所有节点上增长 | 可能是 fork 分歧——通过 hello 日志检查 fork 对齐 |
| fork_db 中 `unlinked_size` 增长 | 父区块未到达；间隔填充应在 5 秒内恢复 |
| 统计中 `peer_head_num` 看起来过时 | 预期——`peer_head_num` 是来自最后一次 hello/fork_status 交换的快照，不是实时的 |

---

参见：[消息](./messages.md)、[同步场景](./sync-scenarios.md)、[前向模式](./forward-mode.md)、[统计参考](./stats-reference.md)、[快照](../node/snapshot.md)、[Fork 解决](../consensus/fork-resolution.md)。
