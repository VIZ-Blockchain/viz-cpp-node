# 验证者守护（Validator Guard）

`validator_guard` 插件为验证者账户自动化签名密钥恢复。当验证者的签名密钥被重置为 null（禁用区块生产）时，插件检测到此变化并广播 `witness_update_operation` 以恢复密钥——无需手动干预。

---

## 何时使用此插件

- 您的验证者账户的签名密钥可能被紧急共识主节点、安全协议或手动操作清空。
- 没有此插件，您必须手动监控链上密钥并在计划槽位到来前恢复它。
- 有了此插件，节点每 N 个区块监视一次 null 密钥并自动恢复。

---

## 启用插件

```ini
plugin = validator_guard
```

---

## 配置

| 选项 | 默认值 | 描述 |
|------|-------|------|
| `validator-guard-enabled` | `true` | 全局启用或禁用插件。 |
| `validator-guard-interval` | `20` | 检查间隔，以区块为单位（按 3 秒/区块计约 60 秒）。 |
| `validator-guard-validator` | — | JSON 三元组 `[name, signing_wif, active_wif]`。可重复。 |
| `validator-guard-disable` | `5` | 同一验证者连续生产的区块数，超过后自动禁用。`0` = 禁用。 |

插件还从验证者插件配置中读取 `enable-stale-production`。

### 示例

```ini
plugin = validator_guard

# 监控一个验证者
validator-guard-validator = ["alice", "5K_SIGNING_WIF", "5K_ACTIVE_WIF"]

# 监控第二个验证者
validator-guard-validator = ["alice.backup", "5J_SIGNING_WIF", "5J_ACTIVE_WIF"]

# 每 10 个区块检查一次
validator-guard-interval = 10
```

> **安全说明：** 活跃私钥以明文存储在 `config.ini` 中。限制文件权限（`chmod 600 config.ini`），避免将文件暴露给不受信任的进程。

---

## 工作原理

### 启动

1. 解析并验证所有配置的 WIF 密钥。
2. 如果 `enable-stale-production = true`，自动恢复启动时处于禁用状态（参见安全机制）。
3. 链数据库打开后，针对链上权限验证每个配置的活跃密钥。账户未找到或密钥不匹配的验证者将从监控中删除并发出警告。
4. 运行立即检查；缓存结果以与周期性计划对齐。

### 每区块处理程序

每个区块时：

1. **连续区块自动禁用**：如果被监控的验证者连续生产了 `validator-guard-disable` 个区块，广播带 null 密钥的 `witness_update_operation` 以禁用它，并将该验证者标记为自动禁用。*其他*验证者的任何区块都会重置所有连续计数器。
2. **交易确认**：扫描区块中待处理的恢复交易 ID。匹配时，将恢复标记为已确认并清除跟踪状态。
3. **前瞻调度**：如果任何被监控的验证者在接下来的 3 个槽位内有安排，触发立即检查，以便在槽位到来前恢复密钥。
4. **周期性检查**：否则，每 `validator-guard-interval` 个区块运行核心检查。节点启动后仍在追赶时，每 10 个区块检查一次。

### 核心检查

每次检查（按顺序）：

1. **过时生产防护**：如果 `enable-stale-production` 激活且网络参与度 < 33%，跳过所有恢复。参与度达到 ≥ 33% 时自动清除。
2. **同步检查**：如果头块时间比系统时钟落后超过 2 个区块间隔，则跳过。
3. **长 fork 安全**：如果 LIB 超过 200 秒，则跳过。
4. **过期清理**：清理过期的进行中恢复尝试，以便可以重试。
5. **每验证者密钥检查**：读取链上签名密钥。
   - 密钥存在 → 清除待处理恢复状态和自动禁用标志。
   - 密钥为 null + 验证者已自动禁用 → 跳过自动恢复（操作员必须调查）。
   - 密钥为 null + 无进行中的恢复 → 调用 `send_witness_update`。

### 恢复交易

1. 构建 `witness_update_operation`，保留当前链上 URL，并将签名密钥设置为配置的公钥。
2. 包装为 `signed_transaction`，30 秒到期时间，引用当前头块。
3. 使用配置的活跃私钥签名。
4. 通过 P2P 广播。
5. 在 `_pending_confirmations` 中跟踪交易 ID 以防止重复发送。

---

## 安全机制

| 机制 | 行为 |
|------|------|
| **过时生产** | 当 `enable-stale-production = true` 时，自动恢复被禁用以避免在少数派 fork 上广播。参与度 ≥ 33% 时自动清除。 |
| **紧急模式** | 紧急共识期间，过时生产防护被绕过——密钥恢复可能是恢复所需的。 |
| **同步检查** | 仅在节点同步时运行。 |
| **长 fork 检测** | 如果 LIB 超过 200 秒则跳过。 |
| **权限验证** | 启动时针对链上权限验证活跃密钥。 |
| **连续区块自动禁用** | 同一验证者连续生产 N 个区块后自动清空签名密钥。操作员手动修复密钥前，自动恢复被抑制。 |
| **重复防护** | 进行中的恢复以过期方式跟踪；不发送重复交易。 |

---

## 日志消息

| 消息 | 含义 |
|------|------|
| `monitoring validator 'alice' (signing key: VIZ...)` | 插件已为此验证者启动 |
| `enable-stale-production detected — auto-restore is DISABLED` | 过时生产模式激活；恢复被抑制 |
| `network is healthy (XX%), auto-clearing stale production override` | 过时生产防护已解除 |
| `'alice' has null signing key on-chain — initiating restore` | 检测到 null 密钥，即将广播 |
| `broadcasting witness_update [ID: ...] for 'alice' — restoring key to VIZ...` | 恢复交易已发送 |
| `CONFIRMED restoration for 'alice' in block #N` | 恢复已在链上确认 |
| `POTENTIAL LONG FORK DETECTED! LIB #N is Xs old. Skipping restoration.` | 由于 LIB 过期而跳过恢复 |
| `validator 'alice' produced N consecutive blocks — auto-disabling` | 连续区块阈值已达到 |
| `'alice' was auto-disabled (consecutive block limit), skipping auto-restore` | 自动禁用后抑制自动恢复 |
| `witness_update FAILED for 'alice': [error]` | 广播失败 |

---

## 故障排除

| 问题 | 检查 |
|------|------|
| 恢复未触发 | 验证 `validator-guard-enabled = true`；确保节点已同步；确认账户是已注册的验证者 |
| `enable-stale-production = true` 时禁用 | 预期行为——等待网络参与度 ≥ 33% |
| 交易失败 | 验证 `active_wif` 是否与账户的活跃权限匹配。检查启动时关于密钥不匹配的警告 |
| 配置解析错误 | 每个条目必须是有效的 3 元素 JSON 数组：`["name", "signing_wif", "active_wif"]` |
| 验证者已自动禁用且未恢复 | 连续区块阈值已触发。调查原因，手动在链上恢复签名密钥；一旦密钥检测为非 null，自动禁用标志将清除 |
| 启动时权限警告 | `WARNING: Configured active key ... does NOT have authority on-chain` — 在配置中更新密钥 |

---

参见：[验证者节点](./validator-node.md) — 签名密钥设置；[验证者插件](../plugins/validator.md) — 生产循环内部机制。
