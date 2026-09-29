# 区块日志工具

`programs/util/block-log/` 中的 Node.js 脚本可离线读取区块文件：完整的 `block_log`、DLT 节点的 `dlt_block_log`，以及[区块归档插件](../plugins/block-archive.md)的区间文件。无依赖——Node.js 18+ 即可。脚本使用 `.cjs` 扩展名，因为仓库根目录声明为 ES 模块。

| 文件 | 用途 |
|------|------|
| `block-archive.cjs` | CLI：归档概览、单个区块、搜索、导出为 JSONL |
| `block-log-viewer.cjs` | 单个区块文件的交互式终端查看器 |
| `block-log-reader.cjs` | 二者共用的解析库（`BlockLogReader`、`DltBlockLogReader`、`readSignedBlock`） |
| `op-layouts.json` | 64 号及以后操作的字段布局 |
| `gen-op-layouts.cjs` | 从 viz-js-lib 重新生成 `op-layouts.json` |

---

## `block-archive.cjs`

```
node block-archive.cjs <命令> <目标> [选项]
```

`<目标>` 为归档目录、含 `dlt_block_log` 的节点数据目录，或单个 `.log` / `dlt_block_log` 文件。

### `info`

列出区间文件、其区块范围和数量，并报告空洞：

```
$ node block-archive.cjs info /var/lib/vizd/block-archive
blocks-0083792402-0083792499.log  83792402-83792499  98 blocks
blocks-0083792500-0083792599.log  83792500-83792599  100 blocks
…
total 83792402-83792999
```

`GAP a-b` 一行表示缺少区块 `a..b`（见 [错误](../plugins/block-archive.md#错误)）。

### `get`

以 JSON 输出单个区块：

```bash
node block-archive.cjs get /var/lib/vizd/block-archive 83792500
```

### `search`

每个匹配的操作输出一行 JSON：

```bash
node block-archive.cjs search /var/lib/vizd/block-archive --op=pm_place_bet,transfer --account=alice --from=83790000 --to=83799999
```

```json
{"block":83792517,"timestamp":"2026-09-28T21:14:03.000Z","tx":0,"op_in_tx":0,"type":"transfer","virtual":false,"data":{"from":"alice","to":"bob","amount":{…},"memo":"…"}}
```

### `export`

过滤条件相同，以 JSONL 写入文件或 stdout。默认导出操作；`--blocks` 导出完整区块（带过滤条件时只导出含匹配操作的区块）：

```bash
node block-archive.cjs export /var/lib/vizd/block-archive --from=83790000 --to=83799999 --out=ops.jsonl
node block-archive.cjs export /var/lib/vizd/block-archive --blocks --out=blocks.jsonl
```

写入的记录数输出到 stderr，stdout 保持干净，便于管道处理。

### 过滤条件

| 选项 | 含义 |
|------|------|
| `--from=N`、`--to=N` | 区块范围（默认：目标中的全部） |
| `--op=a,b` | 操作名，不带 `_operation` 后缀 |
| `--account=name` | 操作中任一等于该名称的字符串字段（`from`、`to`、`account`、`author`、`creator` 等） |
| `--text=s` | 操作 JSON 的子串 |
| `--out=file` | `search` / `export` 的输出文件 |
| `--blocks` | `export` 导出完整区块而非操作 |

区块按顺序读取，只打开与 `--from/--to` 相交的文件，因此大型归档请缩小范围。参考速度：约每秒 4 500 个区块（11 秒读取 50 000 个测试网区块）。

---

## `block-log-viewer.cjs`

单个文件的交互式查看器：

```bash
node block-log-viewer.cjs /var/lib/vizd/blockchain/block_log
node block-log-viewer.cjs /var/lib/vizd/blockchain/dlt_block_log --dlt
node block-log-viewer.cjs /var/lib/vizd/block-archive/blocks-0083790000-0083799999.log --dlt
```

归档的区间文件为 DLT 格式，请加 `--dlt` 打开。

---

## 操作覆盖范围

0–63 号操作由手写读取器解析。64 号及以后的操作（预测市场、代理访问、`set_reward_sharing` 等）依据 `op-layouts.json` 解析，该文件由 viz-js-lib 的序列化器生成，并已与节点逐字节核对。在节点和 viz-js-lib 中新增操作后，请重新生成：

```bash
node programs/util/block-log/gen-op-layouts.cjs ../viz-js-lib/src/auth/serializer/src/operations.js
```

未知的操作 id 会使整个区块无法读取——字节流无法安全跳过——因此请保持 `op-layouts.json` 与节点同步。

区块头字段与节点 JSON 一致：`validator`、`validator_signature`。
