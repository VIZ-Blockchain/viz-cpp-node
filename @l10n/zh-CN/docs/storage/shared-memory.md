# 共享内存

VIZ 节点的所有区块链状态存储在由 **chainbase** 库管理的单一内存映射文件（`shared_memory.bin`）中。没有该文件节点无法运行。

---

## 架构

```
vizd 进程
├── block_log / dlt_block_log  — 原始区块字节（磁盘）
└── shared_memory.bin (mmap)   — 所有链状态（chainbase）
    ├── account_index
    ├── witness_index
    ├── transaction_index
    └── ... （所有其他对象索引）
```

API 线程（Web 服务器线程池）获取**共享读锁**；区块应用持有**独占写锁**。多个读者可以共存；写入时阻塞所有读者。

---

## 配置

所有选项在 `config.ini` 中设置。

### 大小选项

| 选项 | 默认值 | 描述 |
|------|--------|------|
| `shared-file-dir` | `state` | `shared_memory.bin` 的目录（相对于数据目录或绝对路径） |
| `shared-file-size` | `2G` | 初始分配大小。若文件已存在且此值更大，文件将增长，不触发重放。 |
| `inc-shared-file-size` | `2G` | 空闲空间低于阈值时的自动增长步长 |
| `min-free-shared-file-size` | `500M` | 触发自动增长的空闲空间阈值 |

**规则：** `min-free-shared-file-size` 必须小于 `inc-shared-file-size`，否则会发生级联大小调整。

### 锁超时选项

| 选项 | 默认值 | 描述 |
|------|--------|------|
| `read-wait-micro` | `500000`（500 毫秒） | 每次读锁尝试的超时时间 |
| `max-read-wait-retries` | `3` | 出错前最大读取尝试次数 |
| `write-wait-micro` | `500000`（500 毫秒） | 每次写锁尝试的超时时间 |
| `max-write-wait-retries` | `3` | 出错前最大写入尝试次数 |

### 性能选项

| 选项 | 默认值 | 描述 |
|------|--------|------|
| `single-write-thread` | `false` | 序列化所有区块/交易推送。**生产环境推荐。** |
| `block-num-check-free-size` | `1000` | 每 N 个区块检查一次空闲空间 |
| `flush-state-interval` | — | 每 N 个区块将共享内存刷新到磁盘 |
| `clear-votes-before-block` | `0` | 丢弃早于此区块的投票（0 = 保留全部）。减少内存使用。 |
| `skip-virtual-ops` | `false` | 跳过虚拟操作通知。重放时节省 CPU。 |

---

## 推荐配置

**验证者节点（生产环境）：**
```ini
shared-file-size = 4G
inc-shared-file-size = 2G
min-free-shared-file-size = 500M
single-write-thread = true
```

**API 节点（高读取吞吐量）：**
```ini
shared-file-size = 8G
inc-shared-file-size = 2G
min-free-shared-file-size = 500M
single-write-thread = true
read-wait-micro = 1000000
max-read-wait-retries = 10
webserver-thread-pool-size = 256
```

**重放 / 初始同步：**
```ini
shared-file-size = 8G
inc-shared-file-size = 4G
min-free-shared-file-size = 500M
block-num-check-free-size = 10
skip-virtual-ops = true
```

---

## 自动调整大小

当空闲空间降至 `min-free-shared-file-size` 以下时，数据库自动增长。每次调整大小时：

1. 写入 `resize_in_progress` 崩溃标记文件。
2. 将所有脏页刷新到磁盘（`flush()`）。
3. 暂停所有操作（包括区块生产和 API 请求）。
4. 销毁当前内存映射。
5. 按 `inc-shared-file-size` 扩展文件。
6. 重新映射文件并重建所有索引指针。
7. 验证关键对象（如 `dynamic_global_property_object`）在重映射后完好无损。
8. 删除崩溃标记。

### 安全机制

- **调整前刷新：** 在销毁映射之前将脏页写入磁盘，确保在增长过程中发生任何故障时磁盘上的文件保持一致。
- **崩溃标记：** 在破坏性重映射之前写入 `resize_in_progress` 文件，成功后删除。如果进程在调整大小期间崩溃，标记会保留并在下次启动时触发自动恢复。
- **调整后验证：** 重映射后，节点验证 `max_memory()` 是否与预期大小匹配，以及关键对象（如 `dynamic_global_property_object`）是否完好。损坏会被及早发现，而不是导致后续令人困惑的故障。
- **bad_alloc 安全：** 如果在区块应用期间共享内存耗尽，撤销会话会被安全丢弃（而不是尝试注定失败的撤销，这将通过 `std::terminate` 导致进程崩溃）。延迟调整大小将安排在下一个区块进行。

预先充裕地分配 `shared-file-size` 以最小化调整大小频率。每次调整大小都会导致延迟峰值。

---

## 大小规划

VIZ 主网全节点的大致使用量：

| 组件 | 估计大小 |
|------|----------|
| 账户索引（约 14K 个账户） | ~50 MB |
| 验证者索引 | ~5 MB |
| 操作历史（operation_history 插件） | 200–500 MB |
| 账户历史（account_history 插件） | 100–300 MB |
| 其他索引 | 100–200 MB |
| **推荐起始大小** | **4–8 GB** |

---

## 启动序列

```
1. 打开 shared_memory.bin（若 shared-file-size 更大则扩展）
2. 获取独占文件锁
3. 检查 resize_in_progress 崩溃标记 → 若发现则触发恢复
4. 初始化索引
5. 若缺少 genesis → init_genesis()
6. 打开 block_log 或 dlt_block_log
7. undo_all() → 回滚到最后一个不可逆区块
8. 验证头区块与区块日志匹配
```

---

## 恢复

| 症状 | 操作 |
|------|------|
| `CRITICAL: validator X account object MISSING` | 损坏 — 使用 `--replay-from-snapshot --snapshot-auto-latest` |
| `Could not modify object, uniqueness constraint violated` | 损坏 — 使用 `--replay-from-snapshot --snapshot-auto-latest` |
| `Unable to acquire READ lock` | 锁竞争 — 增大 `read-wait-micro` / 启用 `single-write-thread` |
| `Shared memory corrupted: previous resize() crashed` | 中断的调整大小 — 使用 `--replay-from-snapshot --snapshot-auto-latest` |
| `dynamic_global_property_object missing after resize` | 调整后损坏 — 使用 `--replay-from-snapshot --snapshot-auto-latest` |
| 节点启动时循环崩溃 | 文件损坏 — `--replay-from-snapshot --snapshot-auto-latest` |

恢复选项：

- `--replay-from-snapshot --snapshot-auto-latest` — 删除共享内存，导入最新快照，重放 dlt_block_log。
- `--resync-blockchain` — 删除共享内存和区块日志，从对等节点同步。
- `--snapshot <path>` — 从指定快照加载，在其之上重放 dlt_block_log。

---

参见：[Chain 插件](../plugins/chain.md)、[Snapshot 插件](../plugins/snapshot.md)、[区块日志](./block-log.md)。
