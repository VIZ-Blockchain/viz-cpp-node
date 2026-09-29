# 区块归档插件

`block_archive` 是一个**可选的非共识**插件。它在磁盘上永久保存每个不可逆区块的副本，并按固定大小拆分为区间文件。之所以需要它，是因为 DLT 节点的 `dlt_block_log` 只保留一个滚动窗口（`dlt-block-log-max-blocks`，默认 100 000 个区块），更早的区块会被丢弃。

该插件不改变共识，不需要硬分叉，也不影响区块处理：启用与未启用它的节点完全兼容。

**源码：** [plugins/block_archive/plugin.cpp](../../plugins/block_archive/plugin.cpp)

---

## 依赖

```
chain::plugin
```

---

## 启用

插件默认关闭。在 `config.ini` 中添加（放在 `[logger.*]` 各节**之前**——追加到文件末尾的行会落入最后一节并被静默忽略）：

```ini
plugin = block_archive
block-archive-dir = block-archive
block-archive-range = 10000
```

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `block-archive-dir` | `block-archive` | 归档目录。相对路径以节点数据目录为基准。 |
| `block-archive-range` | `10000` | 每个文件的区块数，最小 100。已有归档不支持修改——请使用新目录。 |

启动时插件会在日志中输出设置和游标：

```
block_archive: /var/lib/vizd/block-archive, range 10000, last archived block 83792999
```

---

## 工作原理

1. 每应用一个区块后，插件将其游标（最后归档的区块）与最后不可逆区块（LIB）比较。
2. 从 `游标 + 1` 到 LIB 的所有区块从节点的区块日志读取，并追加到所属区间的文件中。只归档不可逆区块，因此归档中不会出现可能因分叉切换而回滚的区块。
3. 区间 `k` 覆盖区块 `[k × N, k × N + N − 1]`，其中 `N` 为 `block-archive-range`。当前区间写入 `<dir>/partial/blocks-<区间起点>`。
4. 区间的最后一个区块写入后，文件被**封存**：以最终文件名移到目录顶层。顶层文件是完整的，之后不再改变。

```
block-archive/
├── blocks-0083790000-0083799999.log         ← 已封存，不可变
├── blocks-0083790000-0083799999.log.index
├── blocks-0083800000-0083809999.log
├── blocks-0083800000-0083809999.log.index
└── partial/
    ├── blocks-0083810000                    ← 当前区间
    └── blocks-0083810000.index
```

### 文件格式

每个区间是一对 `dlt_block_log` 格式的文件（见 [区块日志](../storage/block-log.md)）：

- `.log` —— 序列化的 `signed_block` 记录，每条后跟其 8 字节起始偏移量；
- `.log.index` —— 8 字节头（首个区块号），之后每个区块一个 8 字节偏移量。

区块 `n` 通过一次 seek 读取：`offset = index[8 + (n − first) × 8]`。文件名中的数字补零至 10 位，因此按字母排序即按区块顺序。

### 首个文件与起点

归档从首次启用插件时的 LIB 开始——**不会回填更早的区块**。文件名中的 `<first>` 是实际保存的第一个区块，因此从快照启动的节点，其第一个文件短于 `N`（例如 `blocks-0083792402-0083792499`）。

### 重启

游标从磁盘恢复：编号最大的已封存文件加上 `partial/` 中文件的末尾。重启或崩溃后，插件从下一个区块继续，不留空洞也不重复。已封存区间在 `partial/` 中残留的文件会作为过期文件删除。

### 错误

归档只是旁路副本，绝不能影响节点：

- 任何写入错误都会以一行 `elog`（`block_archive: …, archiving stopped`）停止归档；节点继续运行并应用区块；
- 如果下一个区块已无法读取（节点离线时间超过 `dlt_block_log` 窗口，或长时间停用后重新启用插件），归档会停止而不是写入空洞。由于游标保存在磁盘上，重启后仍保持停止。要继续，请从其他归档补齐空洞，或移走该目录并开始新的归档。

请在节点日志中关注 `archiving stopped`。

---

## 读取归档

使用 `programs/util/block-log/` 中的工具——见 [区块日志工具](../storage/block-log-tools.md)：

```bash
node programs/util/block-log/block-archive.cjs info   /var/lib/vizd/block-archive
node programs/util/block-log/block-archive.cjs search /var/lib/vizd/block-archive --op=transfer --account=alice
```

已封存文件不可变，因此可以在节点运行时复制、压缩、备份或对外提供。不要读取或移动 `partial/` 中的文件。

---

## 磁盘占用

归档随链增长，节点不会清理。大小取决于区块内容：可用 `dlt_block_log` 的大小除以其窗口，再乘以计划保存的区块数来估算。旧的已封存文件可自由移至冷存储——插件只关注编号最大的已封存文件和 `partial/`。
