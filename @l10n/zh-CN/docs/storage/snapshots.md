# Snapshot 插件

Snapshot 插件通过将完整区块链状态序列化并恢复为 JSON 快照文件，实现近乎即时的节点启动。节点无需从区块日志重放数百万个区块，而是可以加载预构建的快照并通过 P2P 从该点开始同步。

---

## 启用

```ini
plugin = snapshot
```

或通过命令行：

```bash
vizd --plugin snapshot
```

---

## 配置参考

### 仅限命令行的选项

| 选项 | 类型 | 描述 |
|------|------|------|
| `--snapshot <path>` | string | 从快照文件加载状态（DLT 模式）。若 `shared_memory.bin` 已存在则跳过导入；成功导入后将文件重命名为 `.used`。 |
| `--snapshot-auto-latest` | bool | 按文件名中的区块号自动发现 `snapshot-dir` 中的最新快照。若设置了 `--snapshot` 则忽略此项。 |
| `--replay-from-snapshot` | bool | 崩溃恢复：导入快照后重放 `dlt_block_log` 以达到最新可用状态。总是清除共享内存；不重命名快照。 |
| `--auto-recover-from-snapshot` | bool（默认：`true`） | 检测到共享内存损坏时自动运行时恢复。关闭数据库，找到最新快照，清除共享内存，导入并重放——无需重启。 |
| `--create-snapshot <path>` | string | 从当前数据库状态创建快照，然后退出。 |
| `--sync-snapshot-from-trusted-peer` | bool（默认：`false`） | 当状态为空时从可信对等节点下载并加载快照。必须显式启用。 |

### 配置文件选项

| 选项 | 类型 | 默认值 | 描述 |
|------|------|--------|------|
| `snapshot-at-block` | uint32 | `0` | 到达此区块号时创建快照 |
| `snapshot-every-n-blocks` | uint32 | `0` | 每 N 个区块创建一次快照（0 = 禁用） |
| `snapshot-dir` | string | `""` | 自动生成快照文件的目录 |
| `snapshot-max-age-days` | uint32 | `90` | 删除超过 N 天的快照（0 = 禁用） |
| `allow-snapshot-serving` | bool | `false` | 启用通过 TCP 向其他节点提供快照服务 |
| `allow-snapshot-serving-only-trusted` | bool | `false` | 仅限可信对等节点 |
| `snapshot-serve-endpoint` | string | `0.0.0.0:8092` | 快照服务的 TCP 端点 |
| `trusted-snapshot-peer` | string（多值） | — | 用于快照同步的可信对等节点（`IP:port`）。可重复。 |
| `dlt-block-log-max-blocks` | uint32 | `100000` | DLT 滚动区块日志保留的最近区块数（chain 插件） |

---

## 创建快照

### 方法 1：一次性（节点停止并退出）

```bash
vizd --create-snapshot /data/snapshots/viz-snapshot.json --plugin snapshot
```

节点打开现有数据库，必要时重放，创建快照，然后在 P2P 激活前退出。

### 方法 2：在特定区块创建（无停机时间）

```ini
plugin = snapshot
snapshot-at-block = 5000000
snapshot-dir = /data/snapshots
```

当应用第 5,000,000 个区块时，快照被写入 `/data/snapshots/snapshot-block-5000000.json`（在后台线程中）—— 区块处理仅短暂暂停。

### 方法 3：定期自动快照（推荐）

```ini
plugin = snapshot
snapshot-every-n-blocks = 100000
snapshot-dir = /data/snapshots
```

每 100,000 个区块（约 3.5 天）创建一次快照。同步期间跳过快照——仅在实时区块上触发。

**推荐间隔：**

| 间隔 | 区块数 | 大约时间 |
|------|--------|----------|
| 频繁 | 10,000 | ~8 小时 |
| 每日 | 28,800 | ~24 小时 |
| 每周 | 100,000 | ~3.5 天 |
| 稀少 | 1,000,000 | ~35 天 |

### 快照轮换

每次创建新快照后，超过 `snapshot-max-age-days`（默认 90）的旧快照会被自动删除。禁用方法：

```ini
snapshot-max-age-days = 0
```

---

## 从快照加载（DLT 模式）

```bash
vizd --snapshot /path/to/snapshot.json --plugin snapshot
```

加载过程中发生的事情：

1. Snapshot 插件在 chain 插件上注册加载回调。
2. Chain 插件检查：若 `shared_memory.bin` 已存在 → 跳过导入（重启安全）。若找不到快照文件 → 跳过导入。
3. 通过 `open_from_snapshot()` 打开数据库——清除共享内存，初始化 chainbase。
4. 验证快照（格式版本、chain ID、SHA-256 校验和），导入所有 32 种对象类型。
5. 快照文件重命名为 `.used`。
6. LIB 提升至 `head_block_num`，使 P2P 摘要从快照头开始。
7. Fork 数据库以快照头区块为种子。
8. P2P 从 LIB + 1 开始同步。

### 重启安全性

| 重启场景 | 发生的事情 |
|----------|-----------|
| 首次启动（无 shared_memory，文件存在） | 导入快照，重命名为 `.used` |
| 重启（shared_memory 存在） | 跳过导入 |
| 重启（shared_memory 被清除，文件为 `.used`） | 跳过导入 |
| 强制重新导入 | `--resync-blockchain` + 新鲜快照文件 |

`--snapshot` 标志可以安全地永久留在命令行中。

---

## DLT 滚动区块日志

以 DLT 模式运行时（从快照加载），主 `block_log` 为空。单独的 **DLT 滚动区块日志**（`dlt_block_log`）存储最近的不可逆区块。

- 支持 P2P 区块服务（对等节点可以请求最近的区块用于分叉解决）。
- 支持对最近区块调用 `get_block` 等 API。
- 存储在区块链数据目录的 `dlt_block_log.log` 和 `dlt_block_log.index` 中。
- 滚动窗口：当日志超过 `dlt-block-log-max-blocks` 时，旧区块从前端截断。

```ini
dlt-block-log-max-blocks = 100000
```

---

## 崩溃恢复：`--replay-from-snapshot`

当 `shared_memory.bin` 在非正常关机后损坏时，使用此模式：

```bash
# 显式指定快照路径
vizd --replay-from-snapshot --snapshot /data/snapshots/snapshot-block-79273800.vizjson --plugin snapshot

# 自动发现最新快照
vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot
```

恢复步骤：
1. 清除 `shared_memory.bin`（始终——假设已损坏）。
2. 导入快照状态。
3. 重放快照头之后的 `dlt_block_log` 区块。
4. 从重放的头恢复 P2P 同步。

| 方面 | `--snapshot` | `--replay-from-snapshot` |
|------|-------------|--------------------------|
| 共享内存检查 | 若存在则跳过 | 始终清除 |
| 快照重命名 | 重命名为 `.used` | 不重命名 |
| DLT 区块日志重放 | 否 | 是 |
| 用例 | 引导启动 | 崩溃恢复 |

---

## 自动运行时恢复：`--auto-recover-from-snapshot`

默认启用（`true`）。当在区块处理或区块生成过程中检测到共享内存损坏时，节点：

1. 关闭数据库。
2. 在 `snapshot-dir` 中找到最新快照。
3. 清除共享内存，导入快照，重放 `dlt_block_log`。
4. 恢复 P2P 同步——**无需重启**。

前提条件：
- 必须启用 `plugin = snapshot`。
- `snapshot-dir` 中必须存在快照。

禁用方法（如用于调试）：

```bash
vizd --no-auto-recover-from-snapshot
```

---

## P2P 快照同步

节点可以通过 TCP 直接从可信对等节点下载快照。

### 服务器配置

```ini
plugin = snapshot
allow-snapshot-serving = true
snapshot-serve-endpoint = 0.0.0.0:8092
snapshot-every-n-blocks = 28800
snapshot-dir = /data/viz-snapshots
```

### 客户端配置

```ini
trusted-snapshot-peer = seed1.viz.world:8092
trusted-snapshot-peer = seed2.viz.world:8092
sync-snapshot-from-trusted-peer = true
```

客户端连接到每个可信对等节点，选择区块号最高的那个，以 1 MB 分块下载快照，验证 SHA-256 校验和，然后导入。

安全功能：最大 2 GB 快照大小、可信对等节点列表、速率限制、60 秒连接截止时间、流式校验和验证。

---

## 快照文件格式

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

### 包含的对象类型（共 32 种）

**关键（11 种）：** `dynamic_global_property`, `witness_schedule`, `hardfork_property`, `account`, `account_authority`, `validator`, `witness_vote`, `block_summary`, `content`, `content_vote`, `block_post_validation`

**重要（15 种）：** `transaction`, `vesting_delegation`, `vesting_delegation_expiration`, `fix_vesting_delegation`, `withdraw_vesting_route`, `escrow`, `proposal`, `required_approval`, `committee_request`, `committee_vote`, `invite`, `award_shares_expire`, `paid_subscription`, `paid_subscribe`, `witness_penalty_expire`

**可选（5 种）：** `content_type`, `account_metadata`, `master_authority_history`, `account_recovery_request`, `change_recovery_account_request`

---

## 推荐生产配置

```ini
plugin = snapshot

# 每 ~24 小时创建一次快照
snapshot-every-n-blocks = 28800
snapshot-dir = /data/viz-snapshots

# 自动删除超过 90 天的快照
snapshot-max-age-days = 90

# DLT 滚动区块日志：保留最近 10 万个区块
dlt-block-log-max-blocks = 100000

shared-file-size = 4G
plugin = p2p
p2p-seed-node = seed1.viz.world:2001
```

---

## Docker 快速参考

| 任务 | 命令 |
|------|------|
| 启动并定期创建快照 | 在配置中添加 `snapshot-every-n-blocks`，重启容器 |
| 一次性快照 | `VIZD_EXTRA_OPTS="--create-snapshot /var/lib/vizd/snapshots/snap.json --plugin snapshot"` |
| 从快照加载 | `VIZD_EXTRA_OPTS="--snapshot /var/lib/vizd/snapshots/snap.json --plugin snapshot"` |
| 崩溃恢复 | `VIZD_EXTRA_OPTS="--replay-from-snapshot --snapshot-auto-latest --plugin snapshot"` |
| 自动恢复（默认） | 默认启用；确保已设置 `plugin = snapshot` 和 `snapshot-every-n-blocks` |

---

参见：[Chain 插件](../plugins/chain.md)、[共享内存](./shared-memory.md)、[区块日志](./block-log.md)、[P2P 同步场景](../p2p/sync-scenarios.md)。
