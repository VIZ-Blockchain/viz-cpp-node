# P2P 消息参考

DLT P2P 使用原始 TCP 上的二进制协议。每条消息以 4 字节小端头帧定，包含消息类型 ID，后跟通过 FC 反射序列化的长度前缀数据载荷。

**头文件：** [libraries/network/include/graphene/network/dlt_p2p_messages.hpp](../../libraries/network/include/graphene/network/dlt_p2p_messages.hpp)

---

## 消息类型摘要

| 类型 ID | 名称 | 方向 | 用途 |
|---------|------|------|------|
| 5100 | `dlt_hello_message` | 发起方 → 接收方 | 初始握手 — 链状态和能力 |
| 5101 | `dlt_hello_reply_message` | 接收方 → 发起方 | 握手回复 — fork 对齐，交换状态 |
| 5102 | `dlt_range_request_message` | 任意 | 询问对端是否有特定区块 |
| 5103 | `dlt_range_reply_message` | 任意 | 响应：区块可用范围 |
| 5104 | `dlt_get_block_range_message` | 同步中 → 对端 | 请求一段区块（批量同步） |
| 5105 | `dlt_block_range_reply_message` | 对端 → 同步中 | 批量区块交付 |
| 5106 | `dlt_get_block_message` | 任意 | 请求单个区块 |
| 5107 | `dlt_block_reply_message` | 任意 | 单个区块交付 |
| 5108 | `dlt_not_available_message` | 任意 | 请求的区块不可用 |
| 5109 | `dlt_fork_status_message` | 任意 | 实时链状态更新（head、LIB、fork、DLT 范围） |
| 5110 | `dlt_peer_exchange_request` | 任意 | 请求对端地址列表 |
| 5111 | `dlt_peer_exchange_reply` | 任意 | 对端地址列表响应 |
| 5112 | `dlt_peer_exchange_rate_limited` | 任意 | 对端交换请求的速率限制响应 |
| 5113 | `dlt_transaction_message` | 任意 | 交易广播 |
| 5114 | `dlt_soft_ban_message` | 任意 → 被封禁方 | 断开前的软封禁通知 |
| 5115 | `dlt_gap_fill_request` | 任意 | 请求特定区块以填补间隙 |
| 5116 | `dlt_gap_fill_reply` | 任意 | 间隙填补区块交付 |

---

## 枚举类型

### `dlt_node_status`

| 值 | 含义 |
|----|------|
| `DLT_NODE_STATUS_SYNC` (0) | 节点落后；正在主动从对端拉取区块 |
| `DLT_NODE_STATUS_FORWARD` (1) | 节点已追上；通过广播交换区块 |

### `dlt_fork_status`

| 值 | 含义 |
|----|------|
| `DLT_FORK_STATUS_NORMAL` (0) | 在多数派 fork 上 |
| `DLT_FORK_STATUS_LOOKING_RESOLUTION` (1) | 检测到 fork；运行解决算法 |
| `DLT_FORK_STATUS_MINORITY` (2) | 确认在少数派 fork 上 |

### `dlt_peer_lifecycle_state`

| 值 | 含义 |
|----|------|
| `DLT_PEER_LIFECYCLE_CONNECTING` (0) | TCP 连接进行中（5 秒超时） |
| `DLT_PEER_LIFECYCLE_HANDSHAKING` (1) | Hello 交换进行中（10 秒超时） |
| `DLT_PEER_LIFECYCLE_SYNCING` (2) | 交换同步区块（`we_need_sync_items` 或对端需要我们的区块） |
| `DLT_PEER_LIFECYCLE_ACTIVE` (3) | 完全同步；正常区块/交易交换 |
| `DLT_PEER_LIFECYCLE_DISCONNECTED` (4) | 未连接；退避后可重连 |
| `DLT_PEER_LIFECYCLE_BANNED` (5) | 软封禁；封禁到期前不重连 |

---

## 详细消息参考

### 5100 — `dlt_hello_message`

建立 TCP 连接后立即发送。携带发起节点的完整链状态和能力。

```cpp
struct dlt_hello_message {
    uint16_t      protocol_version;    // 当前为 1
    block_id_type head_block_id;
    uint32_t      head_block_num;
    block_id_type lib_block_id;
    uint32_t      lib_block_num;
    uint32_t      dlt_earliest_block;  // 我们滚动 DLT 日志中最旧的区块（无则为 0）
    uint32_t      dlt_latest_block;    // 我们 DLT 日志中最新的区块
    bool          emergency_active;    // 紧急共识当前是否激活
    bool          has_emergency_key;   // 我们是否持有紧急委员会私钥
    uint8_t       fork_status;         // dlt_fork_status 枚举
    uint8_t       node_status;         // dlt_node_status 枚举（SYNC 或 FORWARD）
};
```

**注意事项：**
- `dlt_earliest_block` 对于 DLT 范围感知的 fork 对齐至关重要。接收方使用它来避免请求不在发起方滚动窗口中的区块。
- `has_emergency_key` 标识紧急主节点。在紧急共识模式下，其他节点可优先从此对端同步。

---

### 5101 — `dlt_hello_reply_message`

接收方响应 5100 发送。完成握手。

```cpp
struct dlt_hello_reply_message {
    bool          exchange_enabled;    // 如果认为发起方已追上则为 true
    bool          fork_alignment;      // 如果发起方在同一 fork 上则为 true
    block_id_type initiator_head_seen; // 回显：我们看到的发起方 head_block_id
    block_id_type initiator_lib_seen;  // 回显：我们看到的发起方 lib_block_id
    uint32_t      our_dlt_earliest;    // 我们最早的 DLT 区块
    uint32_t      our_dlt_latest;      // 我们最新的 DLT 区块
    uint8_t       our_fork_status;     // dlt_fork_status 枚举
    uint8_t       our_node_status;     // dlt_node_status 枚举
};
```

**Fork 对齐检查分多级**以处理 DLT 裁剪的区块范围：

| 情况 | 执行的检查 |
|------|-----------|
| 对端无区块（`head_num == 0`） | → 对齐 |
| 对端 head 在我们的 DLT 范围内 | `is_block_known(peer.head_block_id)` |
| 对端 head + 1 == 我们的最早区块 | 读取 `our_earliest_block.previous == peer.head_block_id` |
| 回退 | `is_block_known(peer.lib_block_id)` |

**`exchange_enabled`** 当接收方的 fork_db 包含发起方的 head 区块时为 `true`（即发起方在交换窗口内且在同一 fork 上）。只有启用交换的对端才能接收区块和交易广播。

---

### 5102 — `dlt_range_request_message`

询问对端是否有特定编号和/或 ID 的区块。

```cpp
struct dlt_range_request_message {
    uint32_t      block_num;
    block_id_type block_id;   // 被询问区块的哈希
};
```

---

### 5103 — `dlt_range_reply_message`

5102 的响应。返回对端可用的服务范围。

```cpp
struct dlt_range_reply_message {
    uint32_t  range_start;   // 对端能服务的最早区块
    uint32_t  range_end;     // 对端能服务的最新区块
    bool      has_blocks;    // 如果对端完全没有区块则为 false
};
```

---

### 5104 — `dlt_get_block_range_message`

SYNC 模式下请求连续区块范围。每次请求最多 200 个区块。

```cpp
struct dlt_get_block_range_message {
    uint32_t      start_block_num;
    uint32_t      end_block_num;
    block_id_type prev_block_id;  // 区块 (start_block_num - 1) 的哈希；用于验证链连续性
};
```

**注意事项：**
- 服务对端在发送前验证 `blocks[0].previous == prev_block_id`。
- `start_block_num` 与服务对端 `dlt_earliest_block` 之间的间隙可能需要桥接对端（参见[P2P 概述 — 间隙检测](./overview.md#sync-mode)）。

---

### 5105 — `dlt_block_range_reply_message`

5104 的响应。包含最多 200 个区块。

```cpp
struct dlt_block_range_reply_message {
    std::vector<signed_block> blocks;
    uint32_t                  last_block_next_available;  // 此批次后下一个可用区块
    bool                      is_last;  // 如果此对端没有更多区块则为 true
};
```

**`is_last = true`** 在接收方节点追上后触发 `transition_to_forward()`。

---

### 5106 — `dlt_get_block_message`

按编号请求单个区块。

```cpp
struct dlt_get_block_message {
    uint32_t      block_num;
    block_id_type prev_block_id;  // (block_num - 1) 的哈希，用于链接验证
};
```

---

### 5107 — `dlt_block_reply_message`

5106 的响应。

```cpp
struct dlt_block_reply_message {
    signed_block  block;
    uint32_t      next_available;  // 对端能服务的下一个区块编号（在 head 时为 0）
    bool          is_last;         // 如果这是对端的 head 区块则为 true
};
```

---

### 5108 — `dlt_not_available_message`

当对端无法服务请求的区块时发送（区块在 DLT 日志范围外，或区块未知）。

```cpp
struct dlt_not_available_message {
    uint32_t  block_num;
};
```

请求节点应寻找范围覆盖该区块的另一对端，或触发间隙填补/SYNC 模式。

---

### 5109 — `dlt_fork_status_message`

实时链状态更新。当节点的 head、LIB、DLT 窗口或 fork 状态发生变化时发送，以及在 SYNC → FORWARD 转换时发送。

```cpp
struct dlt_fork_status_message {
    uint8_t       fork_status;         // dlt_fork_status 枚举
    block_id_type head_block_id;
    uint32_t      head_block_num;
    block_id_type lib_block_id;
    uint32_t      lib_block_num;
    uint32_t      dlt_earliest_block;
    uint32_t      dlt_latest_block;
    uint8_t       node_status;         // dlt_node_status 枚举
};
```

**主要用例：** 当节点从 SYNC 转换为 FORWARD 时，向所有连接对端广播此消息，使它们能立即重新评估此节点的 `exchange_enabled`，而无需等待下次 hello 周期。

接收节点更新发送方的本地 `dlt_peer_state` 并重新检查 fork 对齐和交换资格。

---

### 5110 — `dlt_peer_exchange_request`

请求已知对端列表的空消息。

```cpp
struct dlt_peer_exchange_request {
    // 空
};
```

速率限制：每个对端每 **5 分钟滑动窗口 3 次请求**。违规者收到 `dlt_peer_exchange_rate_limited`（5112）。

---

### 5111 — `dlt_peer_exchange_reply`

5110 的响应。包含已知对端的端点信息。

```cpp
struct dlt_peer_endpoint_info {
    fc::ip::endpoint  endpoint;
    node_id_t         node_id;
};

struct dlt_peer_exchange_reply {
    std::vector<dlt_peer_endpoint_info> peers;
};
```

**纳入回复前应用的过滤器：**
- 对端正常运行时间 ≥ `dlt-peer-exchange-min-uptime-sec`（默认 600 秒）
- 每个 /24 子网最多 `dlt-peer-exchange-max-per-subnet`（默认 2）个对端
- 排除 `is_incoming` 对端（临时源端口）
- 每次回复最多 `dlt-peer-exchange-max-per-reply`（默认 10）个对端

---

### 5112 — `dlt_peer_exchange_rate_limited`

当超出请求速率限制时，代替 5111 发送。

```cpp
struct dlt_peer_exchange_rate_limited {
    uint32_t  wait_seconds;  // 请求方重试前应等待的时间
};
```

---

### 5113 — `dlt_transaction_message`

携带用于内存池传播的已签名交易。

```cpp
struct dlt_transaction_message {
    signed_transaction trx;
};
```

收到的交易在推送到链的 `_pending_tx` 前被添加到 P2P 内存池（按到期时间、TaPoS、大小过滤）。成功接受的交易被转发给所有启用交换的对端。

---

### 5114 — `dlt_soft_ban_message`

因垃圾消息或协议违规即将关闭连接前发送给对端。接收对端进入 BANNED 状态持续 `ban_duration_sec`，封禁到期前不会尝试重连。

```cpp
struct dlt_soft_ban_message {
    uint32_t    ban_duration_sec;  // 封禁持续时间（秒）
    std::string reason;            // 人类可读的原因
};
```

常见记录原因（双方均以橙色/黄色标记）：
- `"spam_strikes_exceeded"` — 10 次无效数据包计数
- `"dead_fork_blocks"` — 对端反复发送来自死亡 fork 的区块
- `"protocol_violation"` — 意外的消息类型或格式错误的数据

---

### 5115 — `dlt_gap_fill_request`

请求特定区块以填补节点区块流中检测到的间隙。在 SYNC 和 FORWARD 模式下均可工作。

```cpp
struct dlt_gap_fill_request {
    std::vector<uint32_t> block_nums;  // 请求的特定区块编号
};
```

**约束：**
- 每次请求最多 **100 个区块**（`GAP_FILL_MAX_BLOCKS`）。
- 同一节点请求之间 **5 秒冷却时间**（`GAP_FILL_COOLDOWN_SEC`）。
- 较大的间隙以 100 个区块为单位分批请求；后续批次在下一个周期任务循环时触发。
- 服务对端从其 `dlt_block_log` 读取区块；服务范围外的区块编号返回 5108。
- 服务接受来自任何活跃对端的请求（不仅限于启用交换的对端）。

从三个位置触发：
1. `on_dlt_block_reply()` — 检测到乱序区块
2. `periodic_task()` — 每 5 秒的主动间隙检查
3. `resume_block_processing()` — 快照暂停完成后

---

### 5116 — `dlt_gap_fill_reply`

5115 的响应。包含请求的区块（如果某些不可用，可能是子集）。

```cpp
struct dlt_gap_fill_reply {
    std::vector<signed_block> blocks;  // 请求的区块；可能是部分
};
```

---

## 握手流程

```
发起方                                接收方
    │                                     │
    │──TCP 连接────────────────────────►│
    │                                     │
    │  5100 dlt_hello_message             │
    │──────────────────────────────────►│
    │  (head/lib, DLT 范围,               │
    │   emergency_active, node_status)    │
    │                                     │ 检查 fork 对齐
    │                                     │ 设置 exchange_enabled
    │  5101 dlt_hello_reply_message       │
    │◄──────────────────────────────────│
    │  (exchange_enabled, fork_alignment, │
    │   我们的 DLT 范围, our_node_status) │
    │                                     │
    ├── exchange_enabled = true ──────────┼── 开始 FORWARD 或同步交换
    └── exchange_enabled = false ─────────┴── 发起方进入 SYNC 模式
```

---

## 同步模式流程

```
同步中节点                            服务对端
    │                                     │
    │  5104 dlt_get_block_range           │
    │  (start=our_head+1, end=+200,       │
    │   prev_block_id=our_head_id)        │
    │──────────────────────────────────►│
    │                                     │ 读取 dlt_block_log
    │  5105 dlt_block_range_reply         │
    │◄──────────────────────────────────│
    │  (blocks=[N+1..N+200], is_last)     │
    │                                     │
    │  应用每个区块                        │
    │  如果 is_last → transition_to_forward│
    │  否则 → 请求下一批                   │
```

如果 `our_head + 1` 与服务对端 `dlt_earliest_block` 之间存在间隙，节点在请求前先搜索桥接对端。

---

## 转发模式广播

```
区块生产者              对端 A                  对端 B（启用交换）
    │                        │                        │
    │ 生产区块                │                        │
    │ ─5109 fork_status──────►│                        │
    │ （通过转换通知）          │                        │
    │                          │                        │
    │ ─区块广播 ───────────────►│（启用交换）             │
    │                          │ ─区块广播 ─────────────►│
    │                          │                        │ push_block()
    │                          │                        │ ─5109 fork_status─► 对端
```

只有处于 `DLT_FORK_STATUS_NORMAL` 或 `DLT_FORK_STATUS_LOOKING_RESOLUTION` 状态的启用交换对端才能接收区块广播。

---

## 间隙填补流程

```
节点（FORWARD，检测到间隙）            对端（拥有间隙区块）
    │                                     │
    │  5115 dlt_gap_fill_request          │
    │  (block_nums=[N+1, N+2, N+3])       │
    │──────────────────────────────────►│
    │                                     │ 读取 dlt_block_log
    │  5116 dlt_gap_fill_reply            │
    │◄──────────────────────────────────│
    │  (blocks=[N+1, N+2, N+3])          │
    │                                     │
    │  应用区块 → head 前进               │
```

---

## 线路格式

每条消息写为：

```
[ 4 字节：类型 ID (uint32 LE) ][ 4 字节：载荷长度 (uint32 LE) ][ N 字节：FC 序列化载荷 ]
```

读取使用 `fc::tcp_socket::readsome()` / `writesome()`（非阻塞，fiber 让出）。没有加密层 — 同一链上的所有对端共享公共网络身份，消息不进行逐消息签名。

---

参见：[P2P 概述](./overview.md)、[同步场景](./sync-scenarios.md)、[统计参考](./stats-reference.md)。
