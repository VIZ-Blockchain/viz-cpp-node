# 链插件

链插件是每个 VIZ Ledger 节点的核心组件。它管理 chainbase 共享内存数据库，接受并验证区块和交易，维护 fork 数据库和区块日志状态，并协调与快照和 P2P 插件的启动。所有其他插件都依赖它。

**源码：** [plugins/chain/plugin.cpp](../../plugins/chain/plugin.cpp)

---

## 依赖

```
json_rpc::plugin
```

链插件必须是第一个初始化的域插件；`json_rpc` 是它唯一的正式依赖，由 AppBase 框架首先加载。

---

## 配置

### 仅 CLI 标志

这些是一次性恢复或维护操作；它们使节点在启动时执行特定操作，不能在 `config.ini` 中设置。

| 标志 | 描述 |
|------|------|
| `--replay-from-snapshot <path>` | 崩溃恢复：清除共享内存，导入快照，然后重放 DLT 滚动区块日志。参见[快照插件](./snapshot.md)。 |
| `--snapshot-auto-latest` | 与 `--replay-from-snapshot` 一起：自动发现 `snapshot-dir` 中的最新快照，而不是手动指定路径。 |
| `--auto-recover-from-snapshot` | 默认 `true`。当区块处理或生成期间检测到共享内存损坏时，无需重启即可自动运行时恢复。通过 `--no-auto-recover-from-snapshot` 禁用。 |
| `--resync-blockchain` | 清除共享内存和区块日志；从 genesis 或快照重新开始。具有破坏性 — 仅在从完全数据损失中恢复时使用。 |
| `--check-locks` | 验证锁排序（仅开发用）。 |
| `--validate-database-invariants` | 在每个区块上运行数据库一致性检查（非常慢；仅开发用）。 |

### 配置文件选项

#### 共享内存

| 选项 | 默认值 | 描述 |
|------|--------|------|
| `shared-file-dir` | `state` | 共享内存文件的目录（绝对路径，或相对于数据目录）。 |
| `shared-file-size` | `2G` | 初始共享内存大小。根据链龄和对象数量，生产节点使用 `4G`–`16G`。 |
| `inc-shared-file-size` | `2G` | 当可用空间低于最小阈值时的增长增量。 |
| `min-free-shared-file-size` | `500M` | 当可用共享内存低于此值时自动增长。 |
| `block-num-check-free-size` | `1000` | 每 N 个区块检查一次可用空间。 |
| `flush-state-interval` | `10000` | 每 N 个区块强制完整刷新到磁盘。较高的值以不干净关闭后需要重放更多数据为代价提高吞吐量。 |

#### 区块日志和 DLT

| 选项 | 默认值 | 描述 |
|------|--------|------|
| `dlt-block-log-max-blocks` | `100000` | 在 DLT 滚动区块日志（`dlt_block_log.log`）中保留的最近区块数。仅在 DLT 模式下有效（快照导入后）。设为 `0` 禁用。 |
| `checkpoint` | — | 重放期间必须匹配的区块编号/区块 ID 对；可多次指定。 |

#### 性能

| 选项 | 默认值 | 描述 |
|------|--------|------|
| `single-write-thread` | `false` | 通过专用 io_service 线程路由所有写操作。在高并发下提高一致性；略微降低吞吐量。 |
| `skip-virtual-ops` | `false` | 跳过虚拟操作处理。减少内存使用；破坏索引虚拟操作的插件（`account_history`、`operation_history`）。 |
| `enable-plugins-on-push-transaction` | `false` | 当交易进入待处理池时通知观察者插件（区块应用之前）。 |
| `read-wait-micro` | *(db 默认)* | 读锁超时（微秒）。 |
| `max-read-wait-retries` | *(db 默认)* | 读锁超时变为致命前的重试次数。 |
| `write-wait-micro` | *(db 默认)* | 写锁超时（微秒）。 |
| `max-write-wait-retries` | *(db 默认)* | 写锁超时变为致命前的重试次数。 |

---

## 启动顺序

```
plugin_initialize()    ← 解析 CLI 和配置选项；验证快照路径
plugin_startup()       ← 打开或创建数据库
  ├─ --resync          → 清除共享内存 + 区块日志；初始化 genesis
  ├─ --snapshot        → 导入快照；启动 DLT 模式
  ├─ --replay-from-snapshot → 导入快照；重放 dlt_block_log
  └─ 正常重启          → 打开现有共享内存；修订不匹配时重放
emit on_sync()         ← P2P 和验证者插件激活
```

所有快照加载在 `plugin_startup()` 内发生，在 P2P 或验证者看到数据库之前。

---

## 区块接受

`chain::plugin::accept_block()` 是所有传入区块（来自 P2P 和验证者）的入口点。它：

1. 验证区块时间戳不会太超前于未来。
2. 在写锁下调用 `database::push_block()`。
3. 更新 fork 数据库和区块日志。
4. 向所有订阅者插件发出 `applied_block` 信号。
5. 在 `shared_memory_corruption_exception` 时，如果启用了自动恢复，则调用 `attempt_auto_recovery()`。

交易接受（`accept_transaction()`）通过 `database::push_transaction()` 遵循相同路径。

---

## 共享内存

chainbase 数据库存在于 `shared-file-dir` 中的单个内存映射文件（`shared_memory.bin`）中。关键大小指导：

- 对于从近期快照加载的节点，从 `shared-file-size = 4G` 开始。
- 当可用空间低于 `min-free-shared-file-size` 时，数据库自动增长 `inc-shared-file-size`。
- 干净关闭后，文件缩小到实际使用大小。
- 崩溃后，使用 `--replay-from-snapshot --snapshot-auto-latest` 重建一致状态。

---

## 故障排除

| 症状 | 操作 |
|------|------|
| 启动时 `FC_ASSERT` 或 `database_revision_exception` | 修订不匹配 — 运行 `--replay-from-snapshot --snapshot-auto-latest` |
| Chainbase 打开因损坏错误而失败 | 运行 `--replay-from-snapshot --snapshot-auto-latest` |
| `--resync-blockchain` 后节点卡在 genesis | 区块日志也被清除；提供 `--snapshot` 从快照加载状态 |
| 共享内存无限增长 | 检查 `inc-shared-file-size` 和 `min-free-shared-file-size` 设置；验证链正常应用区块 |
| `write lock timeout` 错误 | 另一个进程持有写锁；检查过时的 `vizd` 进程 |
| 自动恢复反复触发 | 底层存储可能有硬件故障；检查磁盘健康状况；同时验证 `snapshot-every-n-blocks` 已配置，以便存在新鲜快照 |

---

参见：[快照插件](./snapshot.md)、[验证者插件](./validator.md)、[P2P 概述](../p2p/overview.md)、[区块处理](../consensus/block-processing.md)。
