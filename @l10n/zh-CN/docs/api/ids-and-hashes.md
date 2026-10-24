# 交易与区块 ID

VIZ 的 ID 类型继承自 Graphene（BitShares，随后是 Steem）。源码中它们声明为 `fc::ripemd160`，旧文档和库也称之为“ripemd160 ID”。这只是存放它们的 20 字节容器的名字：**这两个 ID 都不是 ripemd160 哈希。** 节点通过别名 `id160_type` 明确说明这一点。

| 值 | 计算方式 | 大小 |
|---|---|---|
| `transaction_id` | `sha256(序列化的交易)` 的前 20 字节，**不**包含签名 | 20 字节 |
| `block_id` | `sha224(序列化的区块头)` 的前 20 字节，再把第 0–3 字节替换为区块号（大端序） | 20 字节 |
| `transaction_merkle_root` | 对已签名交易构建的 sha256 树的根做 `ripemd160` | 20 字节 |

Merkle 根是唯一真正的 ripemd160 值。

## 对客户端的影响

* 计算交易 ID：序列化交易时**不含** `signatures`，取 sha256 的前 20 字节。不要使用 ripemd160，否则结果永远与节点不符。
* 区块号可直接从 block id 读出：前 4 字节，大端序。TaPoS（`ref_block_num` / `ref_block_prefix`）取该编号的低 16 位和 block id 的第 4–7 字节。
* 在所有 API 响应、block log 和账户历史中，ID 都是 20 字节。更换哈希（改为真正的 ripemd160 或完整的 32 字节 sha256）将是一次硬分叉，会把历史分成两种 ID 格式，却没有安全收益：160 位对标识符已经足够，签名针对的是完整的 sha256 摘要而非 ID。

源码：`libraries/protocol/transaction.cpp` 中的 `transaction::id()`，`libraries/protocol/block.cpp` 中的 `signed_block_header::id()` 与 `signed_block::calculate_merkle_root()`，类型定义见 `libraries/protocol/include/graphene/protocol/types.hpp`。
