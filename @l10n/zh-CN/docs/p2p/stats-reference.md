# P2P 统计参考

DLT P2P 层为监控输出两条周期性日志行：

| 日志前缀 | 默认间隔 | 用途 |
|---------|---------|------|
| `DLT Status \|` | ~30 秒 | 紧凑单行，用于 tail/grep 健康监控 |
| `=== DLT P2P Stats \|` | ~120 秒（可通过 `dlt-stats-interval-sec` 配置） | 完整的逐对端详细信息 |

---

## 紧凑状态行

```
DLT Status | FORWARD | head=#79881136 lib=#79881130 | dlt_range=79000000-79881136 | peers=6active/8conn | uptime=2h15m43s | flags=...
```

| 字段 | 示例 | 含义 |
|------|------|------|
| 模式 | `FORWARD` | 节点运行模式（`SYNC` 或 `FORWARD`） |
| `head=#N` | `head=#79881136` | 当前 head 区块编号 |
| `lib=#N` | `lib=#79881130` | 最后不可逆区块编号 |
| `dlt_range=A-B` | `dlt_range=79000000-79881136` | 滚动 DLT 区块日志中存储的区块范围 |
| `peers=Xactive/Yconn` | `peers=6active/8conn` | 启用交换的对端 / 总 TCP 连接数 |
| `uptime` | `2h15m43s` | 节点启动以来的时间 |
| `flags` | 各种 | 活跃标志（snapshot、paused、catchup 等） |

---

## 完整统计 — 标题行

```
=== DLT P2P Stats | status=FWD fork=NORMAL head=79881136 lib=79881130 peers=6 conn=4 paused=no uptime=0h20m30s ===
```

### `status` — 节点运行模式

| 值 | 含义 |
|----|------|
| `SYNC` | 追赶中 — 从对端拉取区块；不广播交易 |
| `FWD` | 已追上 — 实时生产和中继区块及交易 |

**`SYNC` 的原因：** 节点刚启动；在停机期间落后；检测到少数派 fork 并正在重新同步；head 在有领先对端的情况下停滞超过 30 秒。

**`FWD` 的原因：** 节点已追上网络 head；所有区块通过实时广播到达。

### `fork` — Fork 状态

| 值 | 含义 |
|----|------|
| `NORMAL` | 在多数派 fork 上 — 无冲突 |
| `LOOKING` | 检测到竞争 tips；正在比较分支（阈值：42 个区块 = 2 个完整轮次） |
| `MINORITY` | 确认在少数派 fork 上；等待切换 |

**非 `NORMAL` 的原因：** 两个验证者在同一槽位生产；网络分区将验证者分散到不同 tips；收到替代 fork 区块。

### `head` 和 `lib`

- **`head`** — 当前链 tip 的区块编号
- **`lib`** — 最后不可逆区块；此级别及以下的区块已最终确定

在正常 DLT 运行中，head-to-lib 差距通常为 1–10 个区块。

### `peers` 和 `conn`

- **`peers`** — 对端表中的总对端条目（活跃 + 连接中 + 断开连接，追踪以便重连）
- **`conn`** — 当前活跃 TCP 连接数

当 `peers` 显著超过 `conn` 时，节点有断开连接的对端在退避队列中等待。

### `paused`

| 值 | 含义 |
|----|------|
| `no` | 区块处理活跃 |
| `YES` | 区块接收暂时停止（快照导出进行中） |

暂停期间，P2P 连接正常继续。收到的区块在 fork DB 中排队，恢复时应用。过时同步和转发停滞计时器重置，以避免暂停期间发生虚假模式转换。

---

## 完整统计 — 逐对端行

### 活跃对端

```
62.109.17.82:2001 | ACTIVE | exch=YES | head=79881136 lib=79880729 | range=79869724-79880729 | peer_fork=NORM peer_node=FWD | spam=0 | +align+emrg+sync
```

#### 生命周期状态

| 标签 | 含义 |
|------|------|
| `CONNECT` | TCP 连接进行中（5 秒超时） |
| `HANDSHAKE` | Hello/hello-reply 交换进行中（10 秒超时） |
| `SYNCING` | 从此对端下载区块范围 |
| `ACTIVE` | 完全运行；交换已建立 |
| `DISC` | 已断开连接；退避后将重连 |
| `BANNED` | 软封禁；封禁到期前不重连 |

#### `exch` — 交换状态

| 值 | 含义 |
|----|------|
| `YES` | 区块和交易交换已启用；双方在同一 fork 上 |
| `no` | 交换已禁用；fork 对齐未确认 |

**`no` 的原因：** 握手刚完成，fork 对齐尚未验证；对端的 head/LIB 不在我们的 fork DB 中；对端报告 fork 不匹配；SYNC → FORWARD 转换尚未传播。

**交换如何变为 `YES`：**
1. Hello 握手：接收方调用 `is_block_known(peer.head_block_id)` — 匹配时在 hello 回复中设置 `exchange_enabled=true`。
2. 区块接受：当来自此对端的区块应用到我们的链时，交换被启用。
3. FORWARD 转换：对端广播带 `node_status=FORWARD` 的 `dlt_fork_status_message`，触发 fork 对齐重新检查。

#### `head` / `lib`（对端值）

对端最后报告的 head 和 LIB 区块编号 — 来自最后一次 hello、fork_status 消息或区块中继的**快照**。对端的实际链可能领先这些值，尤其是在快速出块期间的 FORWARD 模式下。

#### `range` — DLT 区块日志范围

`earliest-latest`：对端滚动 DLT 区块日志中可用的区块编号。

如果间隙填补或初始同步所需的区块低于 `earliest`，此对端无法服务这些区块。节点会搜索其范围覆盖缺失区块的另一对端。

#### `peer_fork` — 对端自报 fork 状态

| 标签 | 含义 |
|------|------|
| `NORM` | 对端报告在多数派 fork 上 |
| `LOOK` | 对端正在解决 fork 冲突 |
| `MINO` | 对端报告在少数派 fork 上 |

报告 `MINO` 的对端可能正在切换 fork，其 head 可能很快改变。来自它们的区块不应视为规范。

#### `peer_node` — 对端运行模式

| 标签 | 含义 |
|------|------|
| `SYNC` | 对端正在追赶；不会广播交易 |
| `FWD` | 对端已追上并实时中继区块 |

#### `spam` — 反垃圾计数器

自上次有效数据包以来累积的计数。软封禁在 **10 次**时触发。在任何有效数据包、成功重连或封禁到期时重置。

**触发计数：** 反序列化失败；协议违规（当前状态下的意外消息）；dead-fork 区块（宽限期后）。

**注意：** 范围回复中的重复区块和乱序区块**不**增加计数。

#### 标志

| 标志 | 含义 |
|------|------|
| `+align` | Fork 对齐已验证 — 来自此对端的区块可干净地应用到我们的链 |
| `+emrg` | 对端报告紧急共识已激活 |
| `+ekey` | 对端持有紧急委员会私钥（紧急主节点候选） |
| `+sync` | 与此对端的区块范围同步待进行或进行中 |

---

### 断开连接的对端

```
138.201.117.201:2001 | DISC | disconnected=74s | backoff=480s | reconnect_in=502s | spam=0
```

| 字段 | 含义 |
|------|------|
| `disconnected` | 连接丢失后的秒数 |
| `backoff` | 当前重连间隔；每次失败翻倍：30 → 60 → 120 → … → 3600 秒 |
| `reconnect_in` | 下次重连尝试前的秒数 |
| `spam` | 上一会话残留的计数 |

当连接稳定超过 5 分钟时，退避重置为初始值（30 秒）。

---

### 被封禁的对端

```
1.2.3.4:2001 | BANNED | ban_remaining=1800s | reason=spam strike threshold exceeded
```

| 字段 | 含义 |
|------|------|
| `ban_remaining` | 封禁到期前的秒数（默认封禁：3600 秒） |
| `reason` | 在 `dlt_soft_ban_message` 中发送的人类可读封禁原因 |

到期后条目恢复为 DISCONNECTED，正常退避重连恢复。

---

## 场景解读

| 症状 | 可能原因 | 措施 |
|------|---------|------|
| 所有对端 `exch=no` | 握手刚完成；fork 不匹配；节点在 SYNC 中且对端未识别 | 等待 FORWARD 转换触发重新评估；检查 `fork` 状态 |
| `status=SYNC` 不前进 | 对端 DLT 范围存在间隙；没有可用桥接对端 | 检查对端的 `range`；可能需要导入快照 |
| 多个对端 `peer_fork=MINO` | 网络范围的 fork 分裂 | 观察；协议自动收敛 |
| 断开连接对端的 `backoff` 很高 | 反复连接失败；网络不稳定 | 检查端口 2001 的连通性；高退避是预期的，成功时重置 |
| `paused=YES` 意外出现 | 快照在导出期间卡住或崩溃 | 检查快照插件日志 |
| `fork=LOOKING` 未解决 | Fork 持续 > 42 个区块没有明确多数 | 检查验证者连通性；检查两个 tips 上的链 |
| 一个对端的 `spam` 增长 | 协议不匹配；对端在不兼容 fork 上 | 10 次计数时自动封禁；检查对端软件版本 |
| SYNC ↔ FWD 快速振荡 | 没有领先对端；所有对端在同一 head | 隔离 60 秒后触发 `emergency_peer_reset()`；也检查日志中的 P53 修复 |

---

## 协议常量

| 常量 | 值 | 描述 |
|------|----|----|
| `SPAM_STRIKE_THRESHOLD` | 10 | 软封禁前的计数 |
| `BAN_DURATION_SEC` | 3600 | 默认软封禁持续时间（1 小时） |
| `INITIAL_RECONNECT_BACKOFF_SEC` | 30 | 首次重连延迟 |
| `MAX_RECONNECT_BACKOFF_SEC` | 3600 | 最大重连延迟（1 小时） |
| `STABLE_CONNECTION_RESET_SEC` | 300 | 退避重置前的连接持续时间（5 分钟） |
| `PEER_EXCHANGE_MAX_REQUESTS` | 3 | 每滑动窗口最大对端交换请求数 |
| `PEER_EXCHANGE_WINDOW_SEC` | 300 | 对端交换速率限制窗口（5 分钟） |
| `CONNECTING_TIMEOUT` | 5 秒 | TCP 连接超时 |
| `HANDSHAKING_TIMEOUT` | 10 秒 | Hello 交换超时 |
| `PEER_REMOVAL_HOURS` | 8 小时 | 此时间后移除无响应对端 |
| `ISOLATION_RESET_SEC` | 60 | 零活跃对端到触发 `emergency_peer_reset()` 的秒数 |
| `GAP_FILL_MAX_BLOCKS` | 100 | 每次间隙填补请求的最大区块数 |
| `GAP_FILL_COOLDOWN_SEC` | 5 | 间隙填补请求之间的最小间隔 |
| `GAP_FILL_TIMEOUT_SEC` | 15 | 间隙填补进行中标志超时 |
| `FORWARD_STAGNATION_SEC` | 30 | FORWARD 模式下 head 不前进阈值 |
| `SYNC_STAGNATION_SEC` | 30 | SYNC 模式下未收到区块阈值 |
| `FORK_RESOLUTION_BLOCK_THRESHOLD` | 42 | 触发 fork 解决前的区块数（2 × CHAIN_MAX_WITNESSES） |
| `FORK_RESOLUTION_CONFIRMATION_BLOCKS` | 6 | 确认 fork 解决的连续区块数 |

---

参见：[P2P 概述](./overview.md)、[消息](./messages.md)、[同步场景](./sync-scenarios.md)。
