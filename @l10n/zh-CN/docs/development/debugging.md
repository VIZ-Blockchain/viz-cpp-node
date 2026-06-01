# 调试

VIZ Ledger 节点提供多种调试工具：交易签名工具用于加密诊断，P2P 插件使用 ANSI 颜色代码进行网络分析，以及调试配置模板。

---

## 交易签名工具

### sign_transaction

从 stdin 读取 JSON 签名请求（每行一个），计算交易摘要和签名，并输出结果：

```bash
echo '{"ref_block_num":1234,"ref_block_prefix":5678,...}' | ./sign_transaction
```

输出包括 `digest`、`sig_digest`、`key`（公钥）和 `signature`。

**诊断签名失败：**
1. 使用 `sign_transaction` 计算 `sig_digest`。
2. 与钱包的 `sig_digest(chain_id)` 进行比较。
3. 验证 WIF 密钥对应于声明的签名密钥。

### sign_digest

使用 WIF 密钥对原始 SHA-256 摘要进行签名：

```bash
echo '{"digest":"abc123...","wif":"5K..."}' | ./sign_digest
```

用于确认 chain ID 正确性并隔离签名可塑性问题。

---

## 网络调试（P2P 日志）

P2P 插件使用 ANSI 颜色代码在控制台输出中进行视觉区分：

| 颜色 | ANSI 代码 | 内容 |
|------|-----------|------|
| 白色 | `\033[97m` | 区块处理：交易数量、延迟 |
| 青色 | `\033[96m` | 对等节点统计：连接数、字节数、RTT |
| 灰色 | `\033[90m` | 详细调试上下文：DLT 模式、同步状态 |
| 橙色 | — | 连接警告和终止通知 |
| 红色 | — | 关键连接终止事件 |

**解读 P2P 日志：**
- **白色**：快速了解区块处理活动和交易量。
- **青色**：实时监控对等节点数量和连接健康状况。
- **灰色**：调查 DLT 模式和同步协议详情。
- **橙色/红色**：识别连接失败和对等节点封禁事件。

### 网络专用记录器

同步协商消息通过 `"sync"` 记录器发送。在 `config.ini` 中启用：

```ini
[logger.sync]
level = info
appenders = stderr
```

P2P 节点消息使用 `"p2p"` 记录器（不是默认记录器）：

```ini
[logger.p2p]
level = info
appenders = stderr
```

---

## 调试配置

`share/vizd/config/config_debug.ini` 是为调试调优的配置模板：

- 更大的共享内存大小和增长阈值，用于长时间重放。
- 单写入线程，用于确定性区块生成。
- 调优的读/写锁重试计数。

关键设置：

```ini
shared-file-size = 12G
shared-file-full-threshold = 97
shared-file-scale-rate = 3
chainbase-check-locking = 0
```

---

## 调试工作流

### 交易验证失败

1. 对失败的交易 JSON 运行 `sign_transaction`。
2. 将计算得出的 `sig_digest` 与钱包生成的值进行比较。
3. 验证 WIF 密钥对应于账户的权限。
4. 检查区块日志中的验证异常。

### 共识停滞

1. 检查**白色日志**了解区块摄取空缺。
2. 通过 `database_api.get_validator_schedule` 检查验证者调度。
3. 检查**橙色/红色日志**了解影响同步的对等节点断连。

### 网络连接问题

1. 检查**青色日志**了解对等节点数量和连接健康状况。
2. 检查**白色日志**了解区块摄取延迟和空缺。
3. 检查**灰色日志**了解快照同步期间的 DLT 同步状态。
4. 检查**橙色/红色日志**了解终止事件和对等节点封禁。
5. 将区块推送异常与日志中的特定区块号关联。

### 区块重放

从快照重放区块以重现问题：

```bash
./vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot --data-dir /data/vizd
```

---

参见：[构建](./building.md)、[测试](./testing.md)、[P2P 概述](../p2p/overview.md)、[插件概述](../plugins/overview.md)。
