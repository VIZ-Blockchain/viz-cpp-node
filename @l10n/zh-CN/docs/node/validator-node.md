# 运行验证者节点

验证者（区块生产者）是由 Fair-DPOS 算法计划每 3 秒生产一个区块的账户。运行验证者节点需要已注册的验证者账户、签名密钥和正确配置的节点。

---

## 前置要求

1. 通过 `validator_update_operation` 注册为验证者的 VIZ Ledger 账户。
2. 与链上注册的签名密钥对应的 WIF 私钥。
3. 已同步的全节点（验证者插件需要 chain + p2p + snapshot 插件）。

---

## 配置

使用 `share/vizd/config/config_witness.ini` 作为基础模板。

关键设置：

```ini
# P2P — 允许公共入站连接以传播区块
p2p-endpoint = 0.0.0.0:2001
p2p-seed-node = seed1.viz.world:2001

# RPC — 绑定到 localhost 以确保安全（验证者不需要公共 API）
webserver-http-endpoint = 127.0.0.1:8090
webserver-ws-endpoint   = 127.0.0.1:8091

# 验证者所需插件
plugin = chain p2p webserver json_rpc database_api network_broadcast_api validator witness_api

# 跳过虚拟操作索引以节省内存
skip-virtual-ops = true

# 共享内存 — 对于低内存验证者构建 2G 已足够
shared-file-size = 2G

# ─── 验证者身份 ────────────────────────────────────────────────
# 您的验证者账户名
validator = myvalidator

# 您的 WIF 格式签名私钥
private-key = 5JxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxWIF

# 参与度阈值 — 网络参与度低于此值时停止生产
required-participation = 33

# 主网上不要启用此选项（仅用于测试网/引导）
# enable-stale-production = false
```

---

## NTP 时间同步

精确时间对区块生产至关重要。验证者插件维护自己的 NTP 客户端：

```ini
# NTP 服务器（默认：pool.ntp.org, time.google.com, time.cloudflare.com）
ntp-server = pool.ntp.org
ntp-server = time.cloudflare.com

# 每 15 分钟检查一次 NTP
ntp-request-interval = 900

# 丢弃往返时间 > 150ms 的 NTP 响应
ntp-round-trip-threshold = 150
```

确保服务器操作系统时钟也已同步（通过 `chrony` 或 `systemd-timesyncd`）。

---

## 启动节点

```bash
./vizd --config-file /data/vizd/config.ini --data-dir /data/vizd
```

### Docker

```bash
docker run -d \
  --name vizd-validator \
  --restart unless-stopped \
  -p 2001:2001 \
  -v /data/vizd:/var/lib/vizd \
  -e VIZD_WITNESS=myvalidator \
  -e VIZD_PRIVATE_KEY=5Jxxx... \
  vizblockchain/vizd:latest
```

---

## 注册/更新验证者

使用 `cli_wallet` 或任何兼容钱包广播 `validator_update_operation`：

```json
{
  "type": "validator_update_operation",
  "value": {
    "owner": "myvalidator",
    "url": "https://mysite.example/validator",
    "block_signing_key": "VIZ5hqSa...",
    "props": [3, {
      "account_creation_fee": "1.000 VIZ",
      "maximum_block_size": 65536
    }]
  }
}
```

`block_signing_key` 必须与节点配置中的 `private-key` 匹配。

要禁用验证者（从计划中移除），将 `block_signing_key` 设置为空密钥：

```json
"block_signing_key": "VIZ1111111111111111111111111111111114T1Anm"
```

---

## 生产循环：节点的工作流程

验证者插件在**专用线程**（与 P2P I/O 隔离）上运行 250ms 生产计时器。每次触发时调用 `maybe_produce_block()`，按顺序检查：

1. **同步检查**（DLT 模式）：追赶节点时不生产。
2. **快照检查**：快照创建进行中时不生产。
3. **参与度检查**：网络验证者参与度必须 ≥33%。
4. **槽位分配**：当前槽位是否安排了此节点的验证者？
5. **密钥检查**：节点是否有正确的私钥？
6. **少数派 fork 检测**：如果最后 21 个区块都来自此节点自己的验证者——回滚并重新同步。
7. **Fork 冲突解决**：如果同一高度存在另一个区块，应用投票权重比较。
8. **延迟检查**：如果节点超过槽位边界 >500ms——跳过。
9. **生成并广播**区块。

完整执行流程参见[验证者插件](../plugins/validator.md)。

---

## 生产结果（日志消息）

| 结果 | 含义 |
|------|------|
| `produced` | 区块成功生产并广播 |
| `not_synced` | 节点仍在追赶或快照进行中 |
| `not_time_yet` | 无可用槽位或 NTP 漂移 |
| `not_my_turn` | 此槽位安排了另一个验证者 |
| `no_private_key` | 已配置验证者被安排，但私钥缺失 |
| `low_participation` | 网络参与度低于阈值 |
| `lag` | 超过槽位 >500ms 才醒来——槽位已错过 |
| `fork_collision` | 下一高度存在竞争区块——等待中 |
| `minority_fork` | 节点处于孤立 fork——回滚中 |

---

## 安全机制

### 网络分区保护
如果少于 33% 的验证者在参与，则停止生产以防止脑裂场景。仅在引导/测试网时使用 `enable-stale-production = true` 覆盖。

### 少数派 Fork 检测
如果节点的 fork 数据库显示 21+ 个连续区块全部来自此节点自己的验证者，它会自动回滚到 LIB 并重新同步。这可以捕获网络隔离情况。

### 生产 Watchdog
如果在 `should_be_producing` 为 true 的情况下 180 秒内（紧急主节点为 60 秒）没有生产区块，watchdog 会自动清除卡住的标志（`minority_fork_recovering`、P2P 追赶、链同步）并尝试恢复生产。

### 快照安全
创建快照期间暂停区块生产，以避免写锁冲突。

---

## 监控

注意以下日志模式：

```
# 正常：区块已生产
produced block #123456 ... validator=myvalidator

# 警告：错过槽位
MISSED-SLOT-OUR-validator: ...

# 警告：少数派 fork
MINORITY FORK DETECTED: rolling back to LIB

# 警告：watchdog 触发
WATCHDOG: no production for 180s, clearing flags
```

自动告警参见[监控](./monitoring.md)和[验证者守护](./validator-guard.md)。

---

## 一个节点上的多个验证者

`validator` 和 `private-key` 选项可重复：

```ini
validator = alice
validator = alice.backup
private-key = 5Jxxx...   # Alice 的密钥
private-key = 5Jyyy...   # Alice.backup 的密钥
```

节点将按计划为任何已配置的验证者生产区块。

---

## 紧急共识密钥

对于参与紧急共识恢复的节点：

```ini
emergency-private-key = 5Jzzz...   # 委员会紧急密钥
```

设置后，节点自动将 `CHAIN_EMERGENCY_WITNESS_ACCOUNT` 添加到其验证者集合，并参与紧急区块生产。参见[紧急共识](../consensus/emergency-consensus.md)。

---

## 故障排除

| 问题 | 检查内容 |
|------|---------|
| 未生产区块 | 验证配置中的 `validator` 和 `private-key`；检查链上注册的签名密钥与配置是否匹配 |
| 日志中出现 `no_private_key` | 链上签名密钥与配置中的任何 `private-key` 不匹配 |
| `low_participation` | 网络健康问题——检查节点数量和其他验证者状态 |
| `minority_fork` | 网络隔离——验证到种子节点的连接 |
| NTP 停滞警告 | 检查操作系统 NTP 同步：`chronyc tracking` 或 `timedatectl` |
| 槽位被抢占 | 签名密钥可能被紧急主节点清空；通过 `validator_update_operation` 恢复 |
