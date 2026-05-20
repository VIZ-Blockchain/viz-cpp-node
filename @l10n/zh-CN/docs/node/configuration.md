# 节点配置

VIZ Ledger 节点通过 INI 文件进行配置。仓库在 `share/vizd/config/` 中提供了几个模板：

| 模板 | 使用场景 |
|------|---------|
| `config.ini` | 带公共 RPC 的完整主网节点 |
| `config_witness.ini` | 验证者节点（本地 RPC，区块生产） |
| `config_testnet.ini` | 测试网/开发环境 |
| `config_stock_exchange.ini` | 市场数据消费者（最少插件） |
| `config_debug.ini` | 调试模式 |

---

## 网络和 P2P

```ini
# P2P 连接监听地址（标准端口 2001）
p2p-endpoint = 0.0.0.0:2001

# 最大节点连接数（未设置则不限制）
p2p-max-connections = 200

# 引导连接的种子节点（可重复）
p2p-seed-node = seed1.viz.world:2001
p2p-seed-node = seed2.viz.world:2001

# 检查点：受信任的 (block_num, block_id) 对（可重复）
# checkpoint = [12345,"0003039..." ]
```

---

## Web 服务器和 RPC

```ini
# HTTP JSON-RPC 端点
webserver-http-endpoint = 0.0.0.0:8090

# WebSocket JSON-RPC 端点
webserver-ws-endpoint = 0.0.0.0:8091

# RPC 线程池大小
webserver-thread-pool-size = 2
```

> **安全说明：** 对于验证者节点，绑定到 `127.0.0.1` 以防止外部访问：
> ```ini
> webserver-http-endpoint = 127.0.0.1:8090
> webserver-ws-endpoint   = 127.0.0.1:8091
> ```

---

## 锁定和并发

```ini
# 重试前等待读锁的微秒数
read-wait-micro = 500000

# 最大读锁重试次数
max-read-wait-retries = 2

# 重试前等待写锁的微秒数
write-wait-micro = 500000

# 最大写锁重试次数
max-write-wait-retries = 3

# 在单线程上序列化所有写操作（推荐）
single-write-thread = true

# 在 push_transaction 上运行插件通知（增加延迟；默认 false）
enable-plugins-on-push-transaction = false
```

---

## 共享内存（数据库）

区块链状态存储在内存映射文件（`shared_memory.bin`）中。

```ini
# 共享内存文件的初始大小
shared-file-size = 4G

# 触发调整大小前的最小可用空间
min-free-shared-file-size = 500M

# 调整大小时文件的增长量
inc-shared-file-size = 2G

# 每 N 个区块检查一次可用空间
block-num-check-free-size = 1000
```

根据链的大小调整 `shared-file-size`。主网建议从 `4G` 开始并监控增长情况。

---

## 插件激活

```ini
# 每个 'plugin' 行添加一个插件（可重复）
# 完整 API 节点的最小集合：
plugin = chain p2p webserver json_rpc database_api network_broadcast_api

# 额外的索引插件（低内存节点上注释掉）：
plugin = social_network tags follow account_history account_by_key
plugin = committee_api invite_api paid_subscription_api custom_protocol_api

# 仅用于验证者节点：
plugin = validator witness_api
```

### 按节点类型划分的插件集

| 节点类型 | 插件 |
|---------|------|
| 全节点 | 以上所有 |
| 验证者 | `chain p2p webserver json_rpc database_api network_broadcast_api validator witness_api` |
| 低内存种子 | `chain p2p` |
| 交易所 | `chain p2p webserver json_rpc database_api network_broadcast_api account_history` |

---

## 历史记录和追踪

```ini
# 丢弃此区块之前的投票对象（节省内存，0 = 保留所有）
clear-votes-before-block = 0

# 跳过虚拟操作的索引（在验证者上节省内存）
skip-virtual-ops = false

# 仅追踪范围内账户的历史记录（可选）
# track-account-range = ["alice","alice.zzz"]

# 历史记录的操作类型白名单/黑名单
# history-whitelist-ops = transfer_operation
# history-blacklist-ops = custom_operation

# 从此区块号开始索引历史记录
# history-start-block = 1000000

# 每个账户的最大动态信息条目数（follow 插件）
follow-max-feed-size = 500

# 私信追踪范围（可选）
# pm-account-range = ["alice","alice.zzz"]
```

---

## 验证者（区块生产）

非验证者节点请不要设置这些参数。

```ini
# 允许在链过期时生产（仅用于开发/测试网）
enable-stale-production = false

# 生产区块所需的最低参与度 % (0–99)
required-participation = 33

# 验证者账户名（可重复，用于一个节点上的多个验证者）
validator = alice

# 验证者的 WIF 签名密钥
private-key = 5JxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxWIF

# 紧急共识私钥（可选）
# emergency-private-key = 5Jxxx...
```

完整的验证者配置参见[验证者节点](./validator-node.md)。

---

## 日志

```ini
# 控制台日志记录器（输出到 stderr）
log.console_appender.stderr.stream = std_error

# P2P 消息的文件日志记录器
log.file_appender.p2p.filename = logs/p2p/p2p.log

# 日志级别：all, debug, info, warn, error, off
logger.default.level = warn
logger.default.appenders = stderr

logger.p2p.level = warn
logger.p2p.appenders = p2p
```

---

## 完整参考

按源文件列出的所有选项：

| 源文件 | 涵盖的选项 |
|--------|----------|
| `plugins/chain/plugin.hpp` | `shared-file-size`, `min-free-shared-file-size`, `inc-shared-file-size`, `block-num-check-free-size`, `single-write-thread`, `enable-plugins-on-push-transaction`, `read-wait-micro`, `max-read-wait-retries`, `write-wait-micro`, `max-write-wait-retries`, `skip-virtual-ops`, `clear-votes-before-block`, `track-account-range`, `history-whitelist-ops`, `history-blacklist-ops`, `history-start-block` |
| `plugins/p2p/p2p_plugin.hpp` | `p2p-endpoint`, `p2p-max-connections`, `p2p-seed-node`, `checkpoint` |
| `plugins/webserver/webserver_plugin.hpp` | `webserver-http-endpoint`, `webserver-ws-endpoint`, `webserver-thread-pool-size` |
| `plugins/validator/validator.hpp` | `enable-stale-production`, `required-participation`, `validator`, `private-key`, `emergency-private-key`, `fork-collision-timeout-blocks`, `ntp-server`, `ntp-request-interval`, `debug-block-production` |
| `plugins/follow/` | `follow-max-feed-size` |
| `plugins/private_message/` | `pm-account-range` |
