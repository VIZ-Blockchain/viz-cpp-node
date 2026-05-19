# 快照插件

快照插件通过将完整的区块链状态序列化并恢复为 JSON 文件，实现节点的近乎即时启动。节点不需要从区块日志重放数百万个区块，而是加载预构建的快照，并通过 P2P 从快照的区块高度开始同步。

**源码：** [plugins/snapshot/snapshot.cpp](../../plugins/snapshot/snapshot.cpp)

---

## 依赖

```
chain::plugin
```

---

## 配置

### 仅 CLI 选项

| 选项 | 默认值 | 描述 |
|------|--------|------|
| `--snapshot <path>` | — | 从快照文件加载状态（DLT 模式）。如果 `shared_memory.bin` 已存在则跳过导入；成功导入后将文件重命名为 `.used`。 |
| `--snapshot-auto-latest` | `false` | 通过文件名中的区块编号自动发现 `snapshot-dir` 中的最新快照。如果同时指定了 `--snapshot` 则被忽略。 |
| `--replay-from-snapshot` | `false` | 崩溃恢复：导入快照，然后从 DLT 滚动区块日志重放区块。始终清除共享内存；**不**将快照文件重命名为 `.used`。需要 `--snapshot` 或 `--snapshot-auto-latest`。 |
| `--auto-recover-from-snapshot` | `true` | 当区块处理或区块生成期间检测到共享内存损坏时，自动运行时恢复 — 无需重启。需要 `plugin = snapshot` 和 `snapshot-dir` 中存在快照。通过 `--no-auto-recover-from-snapshot` 禁用。 |
| `--create-snapshot <path>` | — | 使用当前数据库状态在给定路径创建快照，然后退出。在 P2P 或验证者激活之前运行。 |
| `--sync-snapshot-from-trusted-peer` | `false` | 当状态为空时，从受信对端下载并加载快照。需要 `trusted-snapshot-peer`。需要显式启用以防止意外状态清除。 |

### 配置文件选项

| 选项 | 默认值 | 描述 |
|------|--------|------|
| `snapshot-at-block` | `0` | 当达到此区块编号时创建快照（0 = 禁用）。 |
| `snapshot-every-n-blocks` | `0` | 每 N 个区块创建一个快照（0 = 禁用）。仅在实时区块上触发 — 初始 P2P 同步期间跳过。 |
| `snapshot-dir` | — | 自动生成快照文件的目录。不存在时自动创建。 |
| `snapshot-max-age-days` | `90` | 创建新快照后删除超过 N 天的旧快照（0 = 禁用）。 |
| `allow-snapshot-serving` | `false` | 启用通过 TCP 向其他节点提供快照服务。 |
| `allow-snapshot-serving-only-trusted` | `false` | 将快照服务限制为仅配置的受信对端。 |
| `snapshot-serve-endpoint` | `0.0.0.0:8092` | 快照服务监听器的 TCP 端点。 |
| `trusted-snapshot-peer` | — | P2P 快照同步的受信对端端点（`IP:port`）；可重复。 |

`dlt-block-log-max-blocks` 选项（在 `chain` 插件配置部分）控制滚动 DLT 区块日志大小，与快照操作密切相关 — 参见下方的 [DLT 滚动区块日志](#dlt-滚动区块日志)。

---

## 创建快照

### 方法 1：一次性（节点已停止）

停止节点，然后使用 `--create-snapshot` 重启。节点打开现有数据库（如需则从区块日志重放），写入快照，然后在 P2P 或验证者激活之前退出：

```bash
vizd --create-snapshot /data/snapshots/viz-snapshot.json --plugin snapshot
```

### 方法 2：在特定区块（无停机）

```ini
plugin = snapshot
snapshot-at-block = 5000000
snapshot-dir = /data/snapshots
```

当应用区块 5,000,000 时，节点写入 `/data/snapshots/snapshot-block-5000000.json` 而不中断运行。

### 方法 3：周期性（推荐）

```ini
plugin = snapshot
snapshot-every-n-blocks = 28800
snapshot-dir = /data/snapshots
snapshot-max-age-days = 90
```

文件命名为 `snapshot-block-<N>.json`。推荐间隔：

| 间隔 | 区块数 | 时间（3 秒/区块） |
|------|--------|-----------------|
| 频繁 | 10,000 | ~8 小时 |
| 每日 | 28,800 | ~24 小时 |
| 每周 | 100,000 | ~3.5 天 |

### 方法 4：结合 at-block 和周期性

两个选项可以同时设置；`snapshot-at-block` 触发一次，`snapshot-every-n-blocks` 重复触发。

### 快照创建工作原理

快照创建是异步的，分为两个阶段以最小化影响：

- **阶段 1（读锁，~1 秒）：** 所有 32 种被跟踪的对象类型被序列化到内存中。此阶段区块处理等待；API 和 P2P 读取并发进行。
- **阶段 2（无锁，~2 秒）：** 压缩、SHA-256 校验和计算和文件 I/O。正常节点操作恢复。

如果创建失败（例如磁盘已满），错误被记录到日志，节点继续运行。

---

## 从快照加载（DLT 模式）

### 首次启动

```bash
vizd --snapshot /path/to/snapshot.json --plugin snapshot
```

首次加载时：
1. 快照插件验证文件头（格式版本、链 ID、SHA-256 校验和）。
2. 共享内存被清除并从快照状态重新初始化。
3. 所有 32 种对象类型被导入。
4. LIB 被提升到 `head_block_num`，使 P2P 同步从快照点开始。
5. 快照文件重命名为 `.used` 以防止重启时重新导入。
6. P2P 插件启动并从快照 head 向前同步。

### 重启安全性

节点使用命令行上仍有 `--snapshot` 的方式重启是安全的（例如，通过 `VIZD_EXTRA_OPTS`）：

| 场景 | 行为 |
|------|------|
| 首次启动（无共享内存，文件存在） | 导入快照；重命名为 `.used` |
| 重启（共享内存存在） | 跳过导入 — 使用现有状态 |
| 重启（共享内存已清除，文件已是 `.used`） | 跳过导入 — "snapshot file not found" |
| 强制重新导入 | 使用 `--resync-blockchain` + 提供新鲜快照文件 |

### DLT 模式

快照导入后，节点以 **DLT 模式**运行：主 `block_log` 为空。单独的 [DLT 滚动区块日志](#dlt-滚动区块日志)存储最近的区块，用于 P2P 服务和本地区块查询。模式在后续重启时自动检测（空 `block_log` + 现有 chainbase 状态）。

---

## 快照文件格式

每个快照是一个 JSON 文件：

```json
{
  "header": {
    "version": 1,
    "chain_id": "...",
    "snapshot_block_num": 12345678,
    "snapshot_block_id": "...",
    "snapshot_block_time": "2025-01-01T00:00:00",
    "last_irreversible_block_num": 12345660,
    "payload_checksum": "sha256...",
    "object_counts": { "account": 50000, ... }
  },
  "state": {
    "dynamic_global_property": [...],
    "account": [...],
    ...
  }
}
```

### 包含的对象类型（共 32 种）

**关键（11 种）** — 共识必需：
`dynamic_global_property`、`witness_schedule`、`hardfork_property`、`account`、`account_authority`、`validator`、`witness_vote`、`block_summary`、`content`、`content_vote`、`block_post_validation`

**重要（15 种）** — 完整运行所需：
`transaction`、`vesting_delegation`、`vesting_delegation_expiration`、`fix_vesting_delegation`、`withdraw_vesting_route`、`escrow`、`proposal`、`required_approval`、`committee_request`、`committee_vote`、`invite`、`award_shares_expire`、`paid_subscription`、`paid_subscribe`、`witness_penalty_expire`

**可选（5 种）** — 元数据和恢复：
`content_type`、`account_metadata`、`master_authority_history`、`account_recovery_request`、`change_recovery_account_request`

---

## DLT 滚动区块日志

在 DLT 模式下，主 `block_log` 为空。**DLT 滚动区块日志**（`dlt_block_log.log` + `dlt_block_log.index`）存储最近的不可逆区块。

这支持：
- **P2P 区块服务** — 对端请求最近区块用于 fork 解决和同步追赶。
- **本地区块查询** — `get_block` 等 API 调用在存储范围内有效。

### 配置

```ini
# 保留最后 100,000 个区块（默认）
dlt-block-log-max-blocks = 100000

# 完全禁用 DLT 区块日志
# dlt-block-log-max-blocks = 0
```

日志使用滚动窗口：当超过 `dlt-block-log-max-blocks` 时，旧区块从前端裁剪。重启时，日志被保留，新区块从上次停止处追加。

### 过时快照检测

如果最新快照的区块编号小于 DLT 日志的起始区块，下载节点无法弥合间隙（快照在区块 N，DLT 日志从 M > N 开始，区块 N+1..M-1 缺失）。插件在启动时检测到这一情况，并在同步完成后的第一个实时区块上创建紧急新鲜快照。

---

## 崩溃恢复：`--replay-from-snapshot`

当 `shared_memory.bin` 损坏且节点无法正常启动时，`--replay-from-snapshot` 将快照导入与 DLT 区块日志重放结合：

```bash
# 明确指定快照
vizd --replay-from-snapshot --snapshot /data/snapshots/snapshot-block-79273800.vizjson --plugin snapshot

# 或自动发现最新快照
vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot
```

**恢复步骤：**
1. 清除共享内存（始终如此 — 假设损坏）。
2. 导入快照。
3. 从快照 head 之后的 `dlt_block_log` 重放区块，恢复到最新可用状态。
4. 从重放的 head 区块恢复 P2P 同步。

### 比较：`--snapshot` 与 `--replay-from-snapshot`

| 方面 | `--snapshot` | `--replay-from-snapshot` |
|------|-------------|--------------------------|
| 用途 | 引导新节点 | 从损坏中恢复 |
| 共享内存检查 | 如已存在则跳过 | 始终清除并重新导入 |
| 快照文件重命名 | 重命名为 `.used` | 不重命名 |
| DLT 区块日志重放 | 否 | 是 |
| 典型用途 | 首次设置 | 崩溃恢复 |

### 恢复示例

DLT 节点在区块 79,274,318 崩溃。状态：
```
snapshots/snapshot-block-79273800.vizjson   ← 落后崩溃点 518 个区块
blockchain/dlt_block_log.log                ← 包含区块 79174319..79274318
blockchain/shared_memory.bin                ← 已损坏
```

恢复命令：`vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot`

```
Loading state from snapshot: snapshot-block-79273800.vizjson
Snapshot loaded at block 79273800, elapsed time 12.3 sec
Replaying dlt_block_log from block 79273801 to 79274318 (518 blocks)...
Done replaying, head_block=79274318, elapsed time: 7.3 sec
```

P2P 同步填补到实时链 head 的间隙。

---

## 自动运行时恢复：`--auto-recover-from-snapshot`

默认启用。当区块处理或区块生成期间检测到共享内存损坏时，节点：

1. 关闭数据库。
2. 在 `snapshot-dir` 中找到最新快照。
3. 清除共享内存，导入快照，重放 `dlt_block_log`。
4. 恢复 P2P 同步 — 无需重启。

**前提条件：**
- 必须启用 `plugin = snapshot`。
- `snapshot-dir` 中必须存在快照。使用 `snapshot-every-n-blocks` 自动创建。
- `dlt_block_log` 应覆盖快照之后的区块以最小化数据损失。

如果恢复失败（未找到快照、导入错误），节点记录错误并干净关闭。

禁用：`--no-auto-recover-from-snapshot`

---

## P2P 快照同步

节点可以通过 TCP 提供和下载快照，实现完全自动化的引导，无需手动文件传输。

### 服务器配置

```ini
plugin = snapshot
allow-snapshot-serving = true
snapshot-serve-endpoint = 0.0.0.0:8092
snapshot-every-n-blocks = 28800
snapshot-dir = /data/snapshots

# 可选：仅限受信对端
# allow-snapshot-serving-only-trusted = true
# trusted-snapshot-peer = 1.2.3.4:8092
```

### 客户端配置

```ini
plugin = snapshot
trusted-snapshot-peer = seed1.viz.world:8092
trusted-snapshot-peer = seed2.viz.world:8092
sync-snapshot-from-trusted-peer = true
```

`sync-snapshot-from-trusted-peer` 默认为 `false` — 必须显式启用以防止意外状态清除。

### 工作原理

1. **查询：** 连接到每个受信对端，发送 `snapshot_info_request`，收集元数据（区块编号、校验和、大小）。
2. **选择：** 选择区块编号最高的对端。
3. **下载：** 以 1 MB 块下载；每 5% 向控制台记录进度。
4. **验证：** 通过流式传输验证 SHA-256 校验和（不将整个文件加载到内存）。
5. **导入：** 清除状态，加载已验证的快照，初始化硬分叉。

所有操作在 `chain::plugin_startup()` 内发生，在 P2P 和验证者激活之前。节点完全阻塞，直到导入完成。

### 安全性

- 最大下载大小：2 GB。
- 每个服务器最多 5 个并发连接，每个连接有 60 秒连接截止时间。
- 按 IP 速率限制。
- 控制消息限制 64 KB；只有数据回复允许最多 64 MB。
- TCP 服务器运行在专用线程上，独立于主 I/O 循环。

---

## 推荐生产配置

```ini
plugin = snapshot

# 每日快照（3 秒/区块时约 24 小时）
snapshot-every-n-blocks = 28800
snapshot-dir = /data/viz-snapshots

# 保留 90 天快照（默认）
snapshot-max-age-days = 90

# DLT 滚动区块日志：最后 100k 个区块（默认）
dlt-block-log-max-blocks = 100000
```

---

## 故障排除

| 症状 | 检查 |
|------|------|
| 节点每次重启都重新导入快照 | `shared_memory.bin` 在重启之间被删除；或快照文件从未被重命名为 `.used` |
| 启动时 `Snapshot file not found` | 文件在之前成功导入时已被重命名为 `.used`；或路径错误 |
| 快照加载时 `Chain ID mismatch` | 快照是从不同链创建的；无法导入 |
| `Checksum mismatch` | 快照文件已损坏或传输不完整 |
| 快照创建从不触发 | 未设置 `snapshot-dir`，或节点仍在 P2P 同步（周期性快照跳过同步模式） |
| 启动时过时快照警告 | 最新快照早于 `dlt_block_log` 起始；节点将在下一个实时区块后创建新鲜快照 |
| 自动恢复触发但失败 | `snapshot-dir` 中无快照；检查 `snapshot-every-n-blocks` 是否已配置 |
| P2P 快照下载失败 | 检查 `trusted-snapshot-peer` 是否在端口 8092 可达；检查服务器上 `allow-snapshot-serving = true` |

---

参见：[快照](../node/snapshot.md)、[验证者插件](./validator.md)、[链插件](./chain.md)、[P2P 概述](../p2p/overview.md)。
