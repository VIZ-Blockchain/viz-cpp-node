# 区块日志

VIZ 将区块存储在二进制日志文件中。存在两种变体：

| 变体 | 文件 | 用途 |
|------|------|------|
| `block_log` | `block_log` + `block_log.index` | 完整历史（归档节点） |
| `dlt_block_log` | `dlt_block_log` + `dlt_block_log.index` | 滚动窗口（DLT/快照节点） |

两者共享相同的数据文件格式；索引格式略有不同。

---

## 二进制序列化（`fc::raw`）

所有数据使用小端序编码。

| 类型 | 格式 |
|------|------|
| `uint8_t` – `uint64_t` | 固定宽度小端序 |
| `fc::unsigned_int` | 可变长度（varint）：每字节 7 个数据位 + 1 个续接位 |
| `string` | `[varint: 长度][UTF-8 字节]` |
| `vector<T>` | `[varint: 数量][元素...]` |
| `optional<T>` | `[uint8: 0 或 1][值（若为 1）]` |
| `static_variant` | `[varint: 类型索引][序列化值]` |

---

## 数据文件布局

`block_log` 和 `dlt_block_log` 使用相同格式：

```
[区块 1 二进制][uint64 LE: 区块 1 位置]
[区块 2 二进制][uint64 LE: 区块 2 位置]
...
```

每个条目 = 序列化的 `signed_block`，后跟其自身起始偏移量（`uint64_t`）。

**读取头区块：** 跳转到最后 8 个字节，读取偏移量，跳转到该位置，反序列化。

---

## 索引文件

### `block_log.index`

每个条目是指向 `block_log` 的 8 字节 `uint64_t` 偏移量。

```
offset = 8 × (block_num − 1)
```

### `dlt_block_log.index`

以 8 字节头部开始：

```
[uint64 LE: start_block_num][uint64 LE: start_block_num 的偏移量][...]
```

```
offset = 8 + 8 × (block_num − start_block_num)
```

---

## `signed_block` 结构

```
block_header:
  [20 字节: 前一区块 ID（ripemd160）]
  [4 字节:  时间戳（uint32 Unix 秒）]
  [varint + string: 验证者账户名]
  [20 字节: transaction_merkle_root（ripemd160）]
  [varint + vector: extensions]

signed_block_header（附加）:
  [65 字节: witness_signature（1 字节恢复 + 32 字节 r + 32 字节 s）]

signed_block（附加）:
  [varint + vector<signed_transaction>: 交易列表]
```

区块号**不直接存储**。推导方式：
```
block_num = num_from_id(previous) + 1
num_from_id = block_id 的前 4 字节，按 uint32_t LE 解读
```
（创世区块：`previous` 全为零 → `block_num = 1`。）

---

## `signed_transaction` 结构

```
transaction:
  [2 字节:  ref_block_num（uint16 LE）]
  [4 字节:  ref_block_prefix（uint32 LE）]
  [4 字节:  expiration（uint32 Unix 秒）]
  [varint + vector<operation>: 操作列表]
  [varint + extensions_type: extensions]

signed_transaction（附加）:
  [varint + vector<signature_type>: 签名列表]
```

---

## 操作序列化

`operations` 向量中的每个操作是一个静态变体：

```
[varint: type_id][操作特定字段...]
```

类型 ID：参见[操作概述](../protocol/operations/overview.md)。

**资产线格格式：**
```
[int64: amount][uint64: symbol]
```
Symbol 是压缩的 `uint64`：字节 0 = 小数位数，字节 1–6 = ASCII 名称，字节 7 = 0x00。

| 符号 | 十六进制（LE） |
|------|--------------|
| VIZ（3 位小数） | `03 56 49 5A 00 00 00 00` |
| SHARES（6 位小数） | `06 53 48 41 52 45 53 00` |

**公钥线格格式：** 33 个原始字节（压缩 secp256k1）：`[0x02 或 0x03][32 字节 x]`。

---

## 区块头扩展

| 索引 | 类型 | 数据 |
|------|------|------|
| 0 | `void_t` | （无） |
| 1 | `version` | `uint32_t` 版本号（major 8 \| hf 8 \| release 16 位） |
| 2 | `hardfork_version_vote` | `uint32_t` hf_version + `uint32_t` hf_time |

---

## `dlt_block_log` 滚动窗口

DLT 日志仅保留最近的区块窗口；旧区块被修剪。从 `start_block_num > 1` 开始。使用快照的节点使用此文件进行崩溃恢复（从快照 + dlt_block_log 重放）。

---

## 区块日志查看器

工具集中包含一个终端区块日志查看器（`block-log-viewer.js`）：

```
node block-log-viewer.js <path> [--dlt]
```

主要命令：`f` 第一个，`l` 最后一个，`n`/`p` 下一个/上一个，`g <N>` 跳转到区块 N，`o` 显示操作，`s <type>` 按操作类型搜索，`S <str>` 按内容搜索，`scan` 构建快速导航位掩码。

`scan` 命令构建位掩码文件（`block_log.bitmask`），标记包含非空操作的区块，实现即时 `N`/`P` 跳转。

---

参见：[共享内存](./shared-memory.md)、[快照](./snapshots.md)、[Chain 插件](../plugins/chain.md)。
