# 验证者插件

验证者插件负责区块签名和生产。它在自己的 OS 线程上运行专用的 250ms 定时器循环，在每次触发时执行一系列安全检查，并在满足所有条件时调用 `database::generate_block()`。

**源码：** [plugins/validator/validator.cpp](../../plugins/validator/validator.cpp)

---

## 依赖项

```
chain::plugin, p2p::p2p_plugin, snapshot::snapshot_plugin
```

---

## 配置

### 区块生产

| 选项 | 默认值 | 描述 |
|------|-------|------|
| `validator` / `-w` | — | 验证者账户名；可重复 |
| `private-key` | — | 用于签名的 WIF 私钥；可重复 |
| `emergency-private-key` | — | 紧急共识的 WIF 密钥；自动将 `CHAIN_EMERGENCY_VALIDATOR_ACCOUNT` 添加到验证者集合 |
| `enable-stale-production` | `false` | 绕过参与度和同步检查（仅用于测试网/网络恢复） |
| `required-participation` | `3300` | 最低验证者参与度（**基点**，3300 = 33%） |
| `fork-collision-timeout-blocks` | `21` | 强制生产前的连续 fork 冲突延迟次数（一个完整的验证者轮次） |

### NTP 同步

| 选项 | 默认值 | 描述 |
|------|-------|------|
| `ntp-server` | `pool.ntp.org`, `time.google.com`, `time.cloudflare.com` | NTP 服务器；可重复 |
| `ntp-request-interval` | `900` | 正常同步间隔（秒） |
| `ntp-retry-interval` | `300` | 无 NTP 响应时的重试间隔 |
| `ntp-round-trip-threshold` | `150` | 丢弃往返时间 > N ms 的 NTP 响应 |
| `ntp-history-size` | `5` | NTP 增量平滑的移动平均窗口 |

### 调试

| 选项 | 默认值 | 描述 |
|------|-------|------|
| `debug-block-production` | `false` | 在链数据库中启用详细调试日志 |

---

## 生产定时器

生产循环运行在**专用的 `production_io_service_`** 和其自己的 OS 线程上——与 AppBase/P2P 共享的 io_service 完全分离。这可防止 P2P 活动（节点断开、TLS 握手、发送队列清空）延迟 250ms 定时器回调。

**触发对齐：**
```
定时器每 250ms 触发，与墙钟 250ms 边界对齐
最短休眠：50ms（吸收 OS 抖动）
```

**前瞻：** `now = ntp_time + 250ms` — 将生产决策向前移动，使在 `T_slot - 250ms` 的触发恰好对齐到槽位边界：
```
T=6.000s 的槽位：
  T=5.750 触发 → now=6.000 → 槽位匹配 → lag=0ms 时生产
  T=6.000 触发 → now=6.250 → lag=250ms → 仍在 500ms 阈值内
```

**延迟跳过：** 在 `lag` 结果之后，同一槽位在剩余 3 秒槽位间隔内每次触发时都会重新激活。保护机制跳过到下一个槽位边界，使循环释放 CPU 而不是空转。

---

## `maybe_produce_block()` — 安全检查序列

以下检查在每次 `slot > 0` 的触发时**按顺序**运行。

| # | 检查 | 失败结果 |
|---|------|---------|
| 1 | DLT 同步门控（仅 DLT 模式）：`chain().is_syncing()` 为 false，或此节点是紧急主节点 | `not_synced` |
| 2 | 快照暂停门控：`snapshot().is_snapshot_in_progress()` 为 false | `not_synced` |
| 3 | P2P 追赶门控：`p2p().is_catching_up_after_pause()` 为 false | `not_synced` |
| 4 | HF12 三态安全（见下文） | `not_synced` / `low_participation` |
| 5 | `slot = db.get_slot_at_time(now) > 0` | `not_time_yet` |
| 6 | 计划的验证者在我们配置的集合中 | `not_my_turn` |
| 7 | 槽位尚未填充（`scheduled_time > head_block_time`） | `not_time_yet` |
| 8 | 验证者链上 `signing_key` 非零 | `not_my_turn` |
| 9 | `signing_key` 的私钥已加载 | `no_private_key` |
| 10 | HF12 之前：参与度 ≥ 阈值 | `low_participation` |
| 11 | `|scheduled_time - now| ≤ 500ms` | `lag` |
| 12 | Fork 冲突检查（见下文） | `fork_collision` |
| 13 | 第二次快照暂停检查（竞争窗口） | `not_time_yet` |
| 14 | `db.generate_block()` + `p2p().broadcast_block()` | `produced` |

### HF12 三态安全（检查 #4）

**紧急共识激活：**
- 紧急主节点（有 `emergency-private-key` + 计划中有 committee）：无条件继续。
- 从节点：生产前需要 `get_slot_time(1) >= now`（链未过期）。

**正常模式（HF12+）：**
- 参与度 ≥ 33%：健康网络；通过 `get_slot_time(1)` 进行同步检查。
- 参与度 < 33%：受损网络；应用参与度 vs `required-participation` 阈值。
- `enable-stale-production=true`：绕过参与度和同步检查。

**HF12 之前：** 通过 `get_slot_time(1)` 进行简单同步检查。

### Fork 冲突解决（检查 #12）

当 `head_block_num + 1` 处存在竞争区块时：

1. **投票权重比较（HF12+）：** `compare_fork_branches()` 计算委托给每个分支的总 SHARES。如果我们的分支更重，则继续并删除竞争区块。若平局或更轻，则延迟。
2. **卡住头超时：** 经过 `fork-collision-timeout-blocks` 次连续延迟（默认 21 = 63 秒）后，竞争区块被删除并恢复生产。这处理来自断开节点的死 fork 区块。

**紧急模式：** 任何竞争区块都触发延迟；不使用投票权重路径。

---

## 少数派 Fork 检测

在每次生产尝试之前（在 HF12 安全检查之后），插件遍历 `fork_db` 中最后 21 个区块。如果所有 21 个都由节点自己配置的验证者生产，则节点被隔离在少数派 fork 上。

- **默认操作：** 调用 `p2p().resync_from_lib()` — 回滚到 LIB，重置 fork DB，重新启动 P2P 同步，重新连接种子节点。返回 `minority_fork`。
- **使用 `enable-stale-production=true`：** 记录警告，继续生产。
- **跳过时机：** 紧急共识激活时（committee 区块总会匹配我们配置的集合）。在紧急模式下，DLT 特定的从节点隔离检查取代它。

---

## NTP 停滞检测

如果 `get_slot_at_time(now)` 返回 0（NTP 落后于链时间），`_slot_zero_streak` 计数器递增：

| 连续次数 | 时间 | 动作 |
|---------|------|------|
| 3 | ~750ms | 警告 |
| 10 | ~2.5s | 强制 NTP 重新同步 |
| 60 | ~15s | 长期停滞警告 |
| 120 | ~30s | 严重错误 |

计数器在任何非零槽位结果时重置。

---

## 生产 Watchdog

如果节点曾经生产过区块且 `should_be_producing` 为 true（从链实时状态推导：参与度 ≥ 33% 或使用我们密钥的紧急共识激活），但在以下时间内没有生产区块：
- 紧急主节点：**60 秒**
- 普通验证者：**180 秒**

Watchdog 每 30 秒触发一次并记录诊断。如果满足恢复条件（头部在过去 30 秒内推进，未同步，有节点连接，链上有非零签名密钥），它会强制清除阻塞条件：

1. 清除 `_minority_fork_recovering` 标志。
2. 调用 `p2p().clear_catchup_flag()` — 清除 P2P 暂停后追赶标志。
3. 调用 `chain().clear_syncing()` — 清除链同步标志。

生产在下一次触发时自动恢复。

---

## `on_block_applied()` — 信号处理器

连接到 `database::applied_block`。对每个传入区块运行。

### 错过槽位检测

当 `block_num > prev_num + 1`（区块流中有间隙）时，处理器确定我们的验证者是否被计划在任何错过的槽位，并记录完整诊断状态（生产标志、NTP 偏移、同步状态、签名密钥状态、下一槽位时间）。

### 槽位劫持检测（DLT 紧急共识模式）

当紧急共识激活时，紧急主节点可能清空我们验证者的签名密钥，并在我们计划的槽位中生产 committee 区块。处理器通过 `_slot_hijack_count` 追踪这一情况。当我们自己的验证者之一生产区块时重置。

---

## 公共 API

### `is_witness_scheduled_soon()`

如果本地控制的验证者被计划在接下来 4 个槽位（~12 秒）内生产，则返回 `true`。快照插件在计划快照之前调用此方法，以在生产即将进行时延迟。

### `is_emergency_master()`

在以下条件下返回 `true`：
1. 已配置 `emergency-private-key`（`_witnesses` 中的 `CHAIN_EMERGENCY_VALIDATOR_ACCOUNT`）。
2. "committee" 账户在当前验证者计划中。

只有满足两个条件的节点才应在紧急模式下独自生产；其他节点是从属节点，必须先同步。

### `is_emergency_key_configured()`

如果已配置 `emergency-private-key`，则返回 `true`，不考虑当前计划。用于 P2P 握手消息（`has_emergency_key` 字段）。

### `get_production_diagnostics()`

返回紧凑诊断字符串：
```
validator[skip_flags=0x0 catching_up=0 head=#79881136 last_prod=45s_ago minority_rcv=0 slot_hijacks=0]
```
当节点在没有前方节点的情况下卡住时，包含在 P2P FORWARD 停滞日志中。

---

## 关键不变量

1. **DLT 模式同步时绝不生产** — 在过期头上创建区块，导致 fork 振荡。
2. **快照进行中绝不生产** — 写锁死锁。
3. **槽位已填充时绝不生产** — 创建微型 fork。
4. **紧急主节点必须始终生产** — 它是唯一的区块生产者；等待会造成死锁。
5. **从节点在紧急模式下生产前必须同步** — 在过期头上生产 = 少数派 fork。
6. **参与度 < 33% 停止生产** — 网络分区保护（可覆盖）。
7. **21 个连续自有验证者区块 → 回滚到 LIB** — 少数派 fork 恢复。
8. **所有数据库读取均为最新** — 无状态缓存；紧急模式可在任何区块激活/停用。

---

## 故障排除

| 症状 | 检查内容 |
|------|---------|
| `not_synced` 日志 | DLT 同步激活或快照进行中——等待；卡住时 watchdog 会自动清除 |
| `not_time_yet` 重复出现 | NTP 落后于链时间；检查 `_slot_zero_streak` 警告和 NTP 偏移 |
| 我们的槽位出现 `not_my_turn` | 链上签名密钥被清空；发送 `validator_update_operation` 恢复 |
| `no_private_key` | 配置中缺少链上注册的签名密钥对应的 `private-key` |
| `low_participation` | 网络参与度 < 33%；检查节点连接或设置 `enable-stale-production=true` |
| `fork_collision` | 下一高度有竞争区块；等待投票权重解决或 21 次延迟超时 |
| `minority_fork` | 已隔离；插件自动重新同步到 LIB |
| Watchdog 重复触发 | 同步或追赶标志卡住；头部推进时 watchdog 会自动清除 |
| `SLOT-HIJACK` 日志 | 紧急主节点清空了我们的密钥；通过 `validator_update_operation` 恢复 |

---

参见：[验证者守护](../node/validator-guard.md)、[Fair-DPOS](../consensus/fair-dpos.md)、[紧急共识](../consensus/emergency-consensus.md)、[区块处理](../consensus/block-processing.md)。
