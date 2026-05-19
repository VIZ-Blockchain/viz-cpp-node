# 监控

本页介绍 VIZ Ledger 节点的健康检查、日志模式、P2P 节点统计以及与外部监控系统的集成。

---

## 健康检查：节点同步

查询节点的动态全局属性以验证其正在运行和同步：

```bash
curl -s -X POST http://localhost:8090 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"call","params":["database_api","get_dynamic_global_properties",[]],"id":1}' \
  | python3 -m json.tool
```

检查 `head_block_number` — 同步时应每 3 秒增加一次。检查 `time` — 应与系统时钟相差几秒以内。

简单的存活探测脚本：

```bash
#!/bin/bash
RESPONSE=$(curl -sf -X POST http://localhost:8090 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"call","params":["database_api","get_dynamic_global_properties",[]],"id":1}')
if [ $? -ne 0 ]; then echo "CRIT: RPC unreachable"; exit 2; fi

HEAD=$(echo "$RESPONSE" | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['head_block_number'])")
echo "OK: head_block_number=$HEAD"
```

---

## 日志模式

### 区块生产（验证者节点）

```
# 正常：槽位已生产
produced block #123456 @2025-01-01T00:00:03 validator=alice sz=2048

# 错过槽位
MISSED-SLOT-OUR-validator: alice missed slot at 2025-01-01T00:00:06

# 检测到少数派 fork
MINORITY FORK DETECTED: rolling back to LIB #123400

# 看门狗触发
WATCHDOG: no production for 180s, clearing flags
```

### P2P 连接

```
# 新节点已连接
New peer is connected (203.0.113.10:2001), now 8 active peers

# 节点软封禁
soft-banning peer 203.0.113.10:2001 for 300s: reason=only_fork_db_blocks_no_progress

# 同步完成
Sync: peer 203.0.113.10 says we're up-to-date
```

### 快照和恢复

```
# 快照已创建
Snapshot created at block 5000000 in 14.2s: /data/snapshots/snapshot-block-5000000.json

# 自动恢复已触发
shared_memory_corruption_exception detected — starting auto-recovery
Recovery complete. Resumed from block 4999500
```

### 同步日志（DLT 模式）

启用 `sync` 日志记录器查看同步协商详情：

```ini
[logger.sync]
level = info
appenders = stderr
```

关键消息：
- `Starting sync with peer ...` — 同步已启动
- `on_blockchain_item_ids_inventory: ...` — 收到区块 ID 批次
- `Sync: peer X says we're up-to-date` — 与此节点的同步完成
- `DEFERRED_RESIZE: sync block #N deferred` — 因共享内存调整大小而延迟的同步区块
- `auto-clearing stuck peer_needs_sync_items_from_us` — 30 秒安全网清除了卡住的标志

---

## 日志配置

日志在 `config.ini` 中配置：

```ini
# 控制台输出
log.console_appender.stderr.stream = std_error

# P2P 日志文件
log.file_appender.p2p.filename = logs/p2p/p2p.log

# 日志级别：all, debug, info, warn, error, off
logger.default.level = warn
logger.default.appenders = stderr

logger.p2p.level = warn
logger.p2p.appenders = p2p
```

> **注意：** `node.cpp` 将所有 `ilog`/`wlog` 调用路由到 `p2p` 日志记录器。要查看 P2P 消息，请将 `p2p` 日志记录器级别配置为 `info`。

通过 `logrotate` 进行日志轮换（示例 `/etc/logrotate.d/vizd`）：

```
/data/vizd/logs/p2p/p2p.log {
    daily
    rotate 14
    compress
    missingok
    copytruncate
}
```

---

## P2P 节点统计

P2P 插件每 5 分钟记录一次节点健康指标（可配置）。在 `config.ini` 中启用：

```ini
p2p-stats-enabled = true
p2p-stats-interval = 300   # 秒
```

示例日志输出：

```
P2P peer | ip: 203.0.113.10  | port: 2001 | latency: 45ms  | bytes_in: 12345 | blocked: false | reason:
P2P peer | ip: 198.51.100.5  | port: 2001 | latency: 120ms | bytes_in: 8765  | blocked: true  | reason: soft_ban
Block storage | dlt_log: [79174319..79274318] | dlt_resizes: 412 | fork_db: linked=18 unlinked=0
```

字段说明：
- `latency` — 往返延迟（毫秒）
- `bytes_in` — 自上次测量以来接收的字节增量
- `blocked` / `reason` — 软封禁或限制状态及原因
- `Block storage` — DLT block log 范围、调整大小计数、fork_db 状态

高 `dlt_resizes` 计数加上缩小的 `dlt_log` 范围可能表明映射文件自愈已运行。`reason: soft_ban` 的节点可能处于 fork 上或仅发送过时数据。

---

## Prometheus 和 Grafana

节点不原生暴露 Prometheus 端点。使用 [Node Exporter](https://github.com/prometheus/node_exporter) 获取操作系统级指标，并使用自定义导出器抓取 JSON-RPC 端点：

```python
# 最简示例：抓取 head_block_number
import requests, time
from prometheus_client import Gauge, start_http_server

g = Gauge('viz_head_block_number', 'Current head block')

def collect():
    r = requests.post('http://localhost:8090', json={
        "jsonrpc": "2.0", "method": "call",
        "params": ["database_api", "get_dynamic_global_properties", []],
        "id": 1
    }, timeout=5)
    g.set(r.json()['result']['head_block_number'])

start_http_server(9100)
while True:
    collect()
    time.sleep(3)
```

**推荐仪表板面板：**

| 面板 | 指标 / 来源 |
|------|-----------|
| 头块 | `viz_head_block_number`（同步时每 3 秒增加） |
| 区块延迟 | `time() - viz_head_block_time`（落后系统时钟的秒数） |
| 节点数 | 从 P2P 统计日志解析 |
| 节点延迟 | P2P 统计日志，按节点 IP |
| 共享内存空闲 | 自定义导出器的 `viz_shared_memory_free_mb` |
| CPU / RAM | Node Exporter 标准指标 |
| 磁盘 I/O | Node Exporter `node_disk_*` |

---

## ELK / 集中日志记录

将节点日志转发到中央收集器。Filebeat 示例：

```yaml
# filebeat.yml
filebeat.inputs:
  - type: log
    paths:
      - /data/vizd/logs/p2p/p2p.log
    fields:
      service: vizd
      node: validator-1

output.logstash:
  hosts: ["logstash:5044"]
```

解析关键字段（Logstash grok 或 Elasticsearch 摄取管道）：

```
MISSED-SLOT-OUR-validator: %{WORD:validator} missed slot at %{TIMESTAMP_ISO8601:slot_time}
produced block #%{NUMBER:block_num} @%{TIMESTAMP_ISO8601:block_time} validator=%{WORD:producer}
```

---

## 验证者专项监控

### 需要告警的关键指标

| 条件 | 严重程度 | 操作 |
|------|---------|------|
| 日志中出现 `MISSED-SLOT-OUR-validator` | 警告 | 检查 NTP、网络延迟、CPU 负载 |
| `MINORITY FORK DETECTED` | 严重 | 验证与种子节点的 P2P 连接 |
| `WATCHDOG: no production for 180s` | 严重 | 检查验证者密钥和节点健康状况 |
| 结果代码 `no_private_key` | 严重 | 签名密钥不匹配——检查配置 |
| 结果代码 `low_participation` | 警告 | 网络健康状况下降 |
| 头块停止推进 | 严重 | 节点可能已停滞 |
| 节点数降至 0 | 严重 | 网络分区或防火墙问题 |

### NTP 检查

```bash
chronyc tracking | grep "System time"
# 或
timedatectl | grep "NTP synchronized"
```

验证者插件使用自己的 NTP 客户端（通过 config 中的 `ntp-server` 配置），但操作系统时钟同步也很重要。漂移 >200ms 可能导致错过槽位。

---

## 数据库维护

### 共享内存大小

监控日志中的空间不足警告：

```
chainbase: shared memory low — resizing from 4G to 6G
```

在 `config.ini` 中主动配置增长参数：

```ini
shared-file-size = 4G
min-free-shared-file-size = 500M
inc-shared-file-size = 2G
block-num-check-free-size = 1000
```

### 快照备份验证

创建快照后，在测试节点上验证它能正常加载：

```bash
vizd --create-snapshot /tmp/verify-snap.json --plugin snapshot
# 预期：正常退出并显示 "Snapshot created at block N"
```

定期测试崩溃恢复：

```bash
vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot
# 预期：导入快照，重放 dlt_block_log，输出 "Recovery complete"
```

---

## 事件响应清单

**节点未同步：**
1. 检查节点数（`p2p-stats-enabled` 日志或 RPC `get_info`）。
2. 验证防火墙允许 TCP 端口 2001 入站。
3. 检查 `p2p-seed-node` 设置——尝试备用种子节点。
4. 在 P2P 统计中查找 `soft_ban` 条目——节点可能处于 fork 上。

**验证者不生产区块：**
1. 检查 `config.ini` 中的 `validator` 和 `private-key` 是否与链上签名密钥匹配。
2. 验证 `low_participation` 是否是原因（网络健康状况）。
3. 检查 NTP 同步。
4. 查找 `MINORITY FORK DETECTED` — 节点可能需要重新同步。

**节点崩溃 / 共享内存损坏：**
1. 如果 `--auto-recover-from-snapshot` 已启用（默认）且快照存在，节点自动恢复——检查日志。
2. 手动恢复：`vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot`。
3. 如果没有快照：`vizd --replay-blockchain`（需要完整 block log；DLT 模式下不可用）。

**RPC 不可达：**
1. 检查 `webserver-http-endpoint` 绑定——验证者默认使用 `127.0.0.1:8090`。
2. 检查防火墙或反向代理配置。
3. 验证插件列表包含 `webserver json_rpc database_api`。

---

参见：[验证者节点](./validator-node.md)、[验证者守护](./validator-guard.md)、[快照](./snapshot.md)、[配置](./configuration.md)。
