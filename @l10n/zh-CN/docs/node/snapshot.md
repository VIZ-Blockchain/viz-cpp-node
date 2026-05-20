# 快照

快照插件通过将完整区块链状态序列化为 JSON 文件，实现节点的近乎即时启动。节点无需从 block log 重放数百万个区块，而是加载快照并仅同步快照创建后生产的区块。

从快照加载的节点以 **DLT 模式**运行：主 block log 保持为空，紧凑的 **DLT 滚动 block log**（`dlt_block_log`）保存最近的不可逆区块。

---

## 快速参考

| 目标 | 命令 / 配置 |
|------|-----------|
| 一次性创建快照（停止节点） | `vizd --create-snapshot /path/snap.json --plugin snapshot` |
| 在第 N 个区块创建快照 | `snapshot-at-block = N` + `snapshot-dir = /path` |
| 每 N 个区块创建快照 | `snapshot-every-n-blocks = N` + `snapshot-dir = /path` |
| 从文件引导新节点 | `vizd --snapshot /path/snap.json --plugin snapshot` |
| 崩溃恢复 | `vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot` |
| P2P 自动引导 | `sync-snapshot-from-trusted-peer = true` + `trusted-snapshot-peer = host:8092` |

---

## 启用插件

```ini
plugin = snapshot
```

---

## 配置参考

### 仅 CLI 标志

| 标志 | 描述 |
|------|------|
| `--snapshot <path>` | 从快照文件引导（DLT 模式）。如果 `shared_memory.bin` 已存在或文件已被导入（重命名为 `.used`），则跳过。 |
| `--snapshot-auto-latest` | 通过解析文件名中的区块号自动发现 `snapshot-dir` 中的最新快照。与 `--replay-from-snapshot` 配合使用。设置了 `--snapshot` 时忽略此项。 |
| `--replay-from-snapshot` | 崩溃恢复：始终清除共享内存，导入快照，重放 `dlt_block_log`。不重命名快照文件。需要 `--snapshot` 或 `--snapshot-auto-latest`。 |
| `--auto-recover-from-snapshot` | （默认：`true`）检测到共享内存损坏时自动恢复运行时——无需重启。通过 `--no-auto-recover-from-snapshot` 禁用。 |
| `--create-snapshot <path>` | 使用当前数据库在指定路径创建快照，然后退出。 |
| `--sync-snapshot-from-trusted-peer` | （默认：`false`）状态为空时从受信任节点下载快照。必须明确启用。 |

### 配置文件选项

| 选项 | 默认值 | 描述 |
|------|-------|------|
| `snapshot-at-block` | `0` | 到达此区块号时创建快照（0 = 禁用）。 |
| `snapshot-every-n-blocks` | `0` | 每 N 个区块创建一次快照（0 = 禁用）。仅对实时区块触发——P2P 追赶期间跳过。 |
| `snapshot-dir` | — | 自动生成快照的目录。不存在时自动创建。 |
| `snapshot-max-age-days` | `90` | 创建新快照后删除超过 N 天的旧快照（0 = 禁用）。 |
| `allow-snapshot-serving` | `false` | 通过 TCP 向其他节点提供快照服务。 |
| `allow-snapshot-serving-only-trusted` | `false` | 仅向受信任节点提供服务。 |
| `snapshot-serve-endpoint` | `0.0.0.0:8092` | 快照服务器的 TCP 端点。 |
| `trusted-snapshot-peer` | — | P2P 快照同步的受信任节点地址（可重复）。 |
| `dlt-block-log-max-blocks` | `100000` | DLT 模式下滚动 block log 的大小（chain 插件选项）。0 = 禁用。 |

---

## 创建快照

### 方法 1：一次性（停止节点，创建文件，退出）

首先停止运行中的节点，然后：

```bash
vizd --create-snapshot /data/snapshots/viz-snapshot.json --plugin snapshot
```

节点打开现有数据库，必要时进行重放，写入快照，然后在 P2P 或验证者插件激活之前退出。

### 方法 2：在特定区块创建快照（无停机）

```ini
plugin = snapshot
snapshot-at-block = 5000000
snapshot-dir = /data/snapshots
```

应用第 5,000,000 个区块时，快照写入 `/data/snapshots/snapshot-block-5000000.json`，节点不会停止。

### 方法 3：周期性快照（推荐用于生产环境）

```ini
plugin = snapshot
snapshot-every-n-blocks = 28800   # 按 3 秒/区块计约 24 小时
snapshot-dir = /data/snapshots
snapshot-max-age-days = 90
```

文件自动命名为 `snapshot-block-<N>.json`。快照创建是异步的：

- **阶段 1**（读锁，约 1 秒）：将所有数据库对象序列化到内存。
- **阶段 2**（无锁，约 2 秒）：压缩、校验和、写入磁盘。

只有阶段 1 期间暂停区块处理；API 和 P2P 读取全程不受影响。

**推荐间隔：**

| 频率 | 区块数 | 大约时间 |
|------|-------|---------|
| 频繁 | 10,000 | 约 8 小时 |
| 每日 | 28,800 | 约 24 小时 |
| 每周 | 100,000 | 约 3.5 天 |

### 方法 4：结合 at-block 和周期性

两个设置可同时激活：

```ini
snapshot-at-block = 5000000
snapshot-every-n-blocks = 100000
snapshot-dir = /data/snapshots
```

---

## 引导：从快照加载（DLT 模式）

将快照文件传输到新节点，然后：

```bash
vizd \
  --snapshot /data/snapshots/viz-snapshot.json \
  --plugin snapshot \
  --plugin p2p \
  --p2p-seed-node seed1.viz.world:2001
```

节点在数秒内加载状态，并从快照区块高度开始 P2P 同步。

### 加载过程

1. `chain::plugin_startup()` 检测到 `--snapshot`。
2. 三项安全检查（按顺序）：共享内存已存在 → 跳过；文件未找到（已为 `.used`）→ 跳过；否则继续。
3. 通过 `open_from_snapshot()` 打开数据库（清除并重新初始化 chainbase）。
4. 验证快照 JSON（格式版本、chain ID、SHA-256 校验和），导入全部 32 种对象类型。
5. 快照文件重命名为 `.used` 以防止重启时重复导入。
6. LIB 提升至 `head_block_num`，使 P2P synopsis 从快照头部开始。
7. Fork database 以快照的头部区块为种子。
8. P2P 插件启动，从 `LIB + 1` 开始同步区块。

### 重启安全性

| 场景 | 结果 |
|------|------|
| 首次启动（无 `shared_memory.bin`，文件存在） | 导入快照，重命名为 `.used` |
| 重启（shared_memory 存在） | 跳过导入——使用现有状态 |
| 重启（shared_memory 已清除，文件已为 `.used`） | 跳过导入——文件未找到 |
| 强制重新导入 | `--resync-blockchain` + 新快照文件 |

初始导入后无需从命令行或 Docker `VIZD_EXTRA_OPTS` 中删除 `--snapshot`。

---

## 快照文件格式

快照是单个 JSON 文件：

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
    "dynamic_global_property": [ ... ],
    "account": [ ... ],
    ...
  }
}
```

### 32 种包含的对象类型

**关键（11 种）** — 共识必需：
`dynamic_global_property`、`witness_schedule`、`hardfork_property`、`account`、`account_authority`、`validator`、`witness_vote`、`block_summary`、`content`、`content_vote`、`block_post_validation`

**重要（15 种）** — 完整运行所需：
`transaction`、`vesting_delegation`、`vesting_delegation_expiration`、`fix_vesting_delegation`、`withdraw_vesting_route`、`escrow`、`proposal`、`required_approval`、`committee_request`、`committee_vote`、`invite`、`award_shares_expire`、`paid_subscription`、`paid_subscribe`、`witness_penalty_expire`

**可选（5 种）** — 元数据和恢复：
`content_type`、`account_metadata`、`master_authority_history`、`account_recovery_request`、`change_recovery_account_request`

---

## DLT 滚动 block log

DLT 模式下主 `block_log` 保持为空。`dlt_block_log`（文件 `dlt_block_log.log` + `dlt_block_log.index`）存储最近的不可逆区块用于：

- **P2P 区块服务** — 节点可以请求最近的区块用于 fork 解决。
- **API 访问** — `get_block` 对滚动窗口内的区块有效。

```ini
dlt-block-log-max-blocks = 100000   # 保留最近约 3.5 天的区块
```

当日志超过此大小时，旧区块从前端修剪。实现独立于内存映射文件跟踪逻辑文件大小，以防止数千次调整大小循环后出现过期大小错误。

---

## 崩溃恢复：`--replay-from-snapshot`

当 `shared_memory.bin` 损坏（不干净关机、磁盘满、硬件故障）时使用此模式。DLT 模式下普通的 `--replay-blockchain` 不可用，因为 `block_log` 为空。

```bash
# 明确指定快照路径
vizd --replay-from-snapshot --snapshot /data/snapshots/snapshot-block-79273800.json --plugin snapshot

# 或自动发现最新快照
vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot
```

恢复步骤：
1. 始终清除 `shared_memory.bin`（假设已损坏）。
2. 导入快照状态。
3. 从 `snapshot_head + 1` 开始重放 `dlt_block_log`。
4. 发出 `on_sync` — P2P 填补到实时链头的剩余差距。

快照文件**不**重命名为 `.used`（可能再次需要）。

### 恢复场景示例

DLT 节点在区块 79,274,318 处崩溃，配置了 `snapshot-every-n-blocks = 100000` 和 `dlt-block-log-max-blocks = 100000`：

```
/data/viz-snapshots/snapshot-block-79273800.json   ← 最新快照
/blockchain/dlt_block_log.*                         ← 包含区块 79174319..79274318
/blockchain/shared_memory.bin                       ← 已损坏
```

```bash
vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot
```

```
Loading state from snapshot: .../snapshot-block-79273800.json  (12.3 s)
Replaying dlt_block_log from block 79273801 to 79274318...
  100%  518 of 518  (block 79274318, elapsed 7.2 s)
Recovery complete. Started on blockchain with 79274318 blocks.
```

节点现在处于区块 79,274,318；P2P 同步提供其余部分。

---

## 自动运行时恢复：`--auto-recover-from-snapshot`

默认启用（`true`）。当运行时区块处理或生成过程中检测到损坏时，节点：

1. 在 `snapshot-dir` 中找到最新快照。
2. 关闭数据库。
3. 使用与 `--replay-from-snapshot` 相同的代码路径清除并重新导入。
4. 恢复 P2P 同步——无需重启。

**前提条件：** `plugin = snapshot` 已启用且 `snapshot-dir` 中存在快照。

禁用（用于调试）：

```bash
vizd --no-auto-recover-from-snapshot
```

---

## P2P 快照同步

节点可以通过自定义 TCP 协议从受信任节点下载快照，无需手动传输文件。

### 快照服务器

```ini
plugin = snapshot
allow-snapshot-serving = true
snapshot-serve-endpoint = 0.0.0.0:8092
snapshot-every-n-blocks = 28800
snapshot-dir = /data/viz-snapshots
```

### 新节点引导

```ini
plugin = snapshot
trusted-snapshot-peer = seed1.viz.world:8092
trusted-snapshot-peer = seed2.viz.world:8092
sync-snapshot-from-trusted-peer = true
```

当节点以 0 个区块启动且 `sync-snapshot-from-trusted-peer = true` 时，它查询所有受信任节点，选择快照最高的节点，以 1 MB 块下载，验证 SHA-256 校验和并导入——所有这些在 P2P 或验证者插件激活之前完成。

### 安全性

- 超过 2 GB 的下载被拒绝。
- 通过流式 SHA-256 验证校验和（从不完全加载到内存）。
- 速率限制，最多 5 个并发连接，每个连接 60 秒截止时间。
- `allow-snapshot-serving-only-trusted = true` 限制为 `trusted-snapshot-peer` 列表。

---

## Docker

设置 `VIZD_EXTRA_OPTS` 传递快照标志：

```bash
# 从快照引导
docker run -e VIZD_EXTRA_OPTS="--snapshot /var/lib/vizd/snapshots/snap.json --plugin snapshot" ...

# 崩溃恢复
docker run -e VIZD_EXTRA_OPTS="--replay-from-snapshot --snapshot-auto-latest --plugin snapshot" ...
```

通过 `config.ini` 配置周期性快照（无需 `VIZD_EXTRA_OPTS`）：

```ini
plugin = snapshot
snapshot-every-n-blocks = 28800
snapshot-dir = /var/lib/vizd/snapshots
```

快照文件可在挂载卷路径的主机上访问。

| 任务 | 方法 |
|------|------|
| 周期性快照 | config 中的 `snapshot-every-n-blocks` |
| 一次性快照 | 通过 `VIZD_EXTRA_OPTS` 的 `--create-snapshot` |
| 引导新节点 | 通过 `VIZD_EXTRA_OPTS` 的 `--snapshot` |
| 崩溃恢复 | 通过 `VIZD_EXTRA_OPTS` 的 `--replay-from-snapshot --snapshot-auto-latest` |
| 自动恢复 | 默认——确保 `plugin = snapshot` 且设置了 `snapshot-dir` |
| P2P 自动引导 | config 中的 `sync-snapshot-from-trusted-peer = true` + `trusted-snapshot-peer` |

---

## 推荐生产配置

```ini
plugin = snapshot

# 每约 24 小时创建快照
snapshot-every-n-blocks = 28800
snapshot-dir = /data/viz-snapshots
snapshot-max-age-days = 90

# DLT 滚动 block log：保留最近约 3.5 天
dlt-block-log-max-blocks = 100000

shared-file-size = 4G
plugin = p2p
p2p-seed-node = seed1.viz.world:2001
```

---

## 过期快照检测

如果服务节点的最新快照早于 `dlt_block_log` 起始区块，下载快照的新节点将无法同步缺失的区块。启动时插件检测到此情况并在下一个实时区块自动创建新快照——无需手动干预。

---

## 故障排除

| 问题 | 检查 |
|------|------|
| 节点每次重启都重新导入快照 | 快照文件未重命名为 `.used` — 检查快照目录的写入权限 |
| 节点报告 `item_not_available` | DLT block log 可能未覆盖所宣告的区块——验证 `dlt-block-log-max-blocks` 是否足够大 |
| 快照加载后 P2P 同步停滞 | 检查 config 中的 `[logger.sync]`；验证导入后 LIB 是否已提升至头部 |
| 快照创建失败 | 检查 `snapshot-dir` 的磁盘空间；节点在失败时继续运行 |
| 自动恢复意外触发 | 检查磁盘错误；查看日志中的 `shared_memory_corruption_exception` |
| P2P 下载被拒绝（>2 GB） | 快照过大——增加服务节点上的 `dlt-block-log-max-blocks` 以减小快照大小 |

---

参见：[快照插件](../plugins/snapshot.md) — 完整实现参考；[P2P 概述](../p2p/overview.md) — DLT 同步协议详情。
