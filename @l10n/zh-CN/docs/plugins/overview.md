# 插件概述

VIZ Ledger 使用 **AppBase** 插件框架。每个插件都有生命周期（`plugin_initialize` → `plugin_startup` → `plugin_shutdown`），可以注册 JSON-RPC 方法，可以在 chainbase 数据库中存储数据，并可以订阅链信号（如 `applied_block`）。

---

## 插件类别

| 类别 | 描述 |
|------|------|
| **核心** | 任何节点操作所必需 |
| **基础设施** | 网络、Web 服务器、快照 |
| **API** | 为客户端公开 JSON-RPC 端点 |
| **索引** | 将链数据索引到 chainbase 以进行快速查询 |
| **生产** | 区块签名和生产 |
| **调试/测试** | 仅用于开发；不用于生产 |

---

## 插件清单

### 核心

| 插件 | 状态 | 依赖 | JSON-RPC |
|------|------|------|---------|
| `chain` | 必需 | `json_rpc` | — |
| `json_rpc` | 必需 | — | — |

### 基础设施

| 插件 | 状态 | 依赖 | JSON-RPC |
|------|------|------|---------|
| `webserver` | API 必需 | `json_rpc` | — |
| `p2p` | 网络必需 | `chain` | — |
| `snapshot` | 推荐 | `chain` | — |
| `witness_guard` | 验证者推荐 | `chain`, `p2p` | — |

### API

| 插件 | 状态 | 依赖 | JSON-RPC |
|------|------|------|---------|
| `database_api` | 活跃 | `json_rpc`, `chain` | 是 |
| `network_broadcast_api` | 活跃 | `json_rpc`, `chain`, `p2p` | 是 |
| `witness_api` | 活跃 | `json_rpc`, `chain` | 是 |
| `account_by_key` | 活跃 | `json_rpc`, `chain` | 是 |
| `account_history` | 活跃 | `json_rpc`, `chain`, `operation_history` | 是 |
| `operation_history` | 活跃 | `json_rpc`, `chain` | 是 |
| `committee_api` | 活跃 | `json_rpc`, `chain` | 是 |
| `invite_api` | 活跃 | `json_rpc`, `chain` | 是 |
| `paid_subscription_api` | 活跃 | `json_rpc`, `chain` | 是 |
| `custom_protocol_api` | 活跃 | `json_rpc`, `chain` | 是 |
| `auth_util` | 活跃 | `json_rpc`, `chain` | 是 |
| `block_info` | 活跃 | `json_rpc`, `chain` | 是 |
| `raw_block` | 活跃 | `json_rpc`, `chain` | 是 |
| `follow` | 已弃用 | `json_rpc`, `chain` | 是 |
| `tags` | 已弃用 | `json_rpc`, `chain`, `follow` | 是 |
| `social_network` | 已弃用 | `json_rpc`, `chain` | 是 |
| `private_message` | 已弃用 | `json_rpc`, `chain` | 是 |

### 生产

| 插件 | 状态 | 依赖 | JSON-RPC |
|------|------|------|---------|
| `validator` | 活跃 | `chain`, `p2p` | — |

### 调试/测试

| 插件 | 状态 | 依赖 | JSON-RPC |
|------|------|------|---------|
| `debug_node` | 仅开发 | `chain` | 是 |
| `test_api` | 仅测试 | `json_rpc` | — |

---

## 核心插件

### `chain`

管理 chainbase 数据库，应用区块和交易，并向所有其他插件发出信号。

**关键配置选项：**

| 选项 | 默认值 | 描述 |
|------|-------|------|
| `shared-file-size` | `2G` | 共享内存文件初始大小 |
| `shared-file-dir` | `state` | 共享内存文件目录 |
| `inc-shared-file-size` | `2G` | 空间不足时的增长量 |
| `min-free-shared-file-size` | `500M` | 触发自动增长的阈值 |
| `flush-state-interval` | `10000` | 每 N 个区块强制刷新到磁盘 |
| `skip-virtual-ops` | `false` | 跳过虚拟操作（减少内存） |
| `dlt-block-log-max-blocks` | `100000` | 滚动 DLT 区块日志容量 |

**关键 CLI 标志：**

| 标志 | 描述 |
|------|------|
| `--replay-blockchain` | 清除 chainbase 并从区块日志重放 |
| `--force-replay-blockchain` | 同上，忽略损坏检查 |
| `--replay-from-snapshot` | 导入快照然后重放 DLT 区块日志（崩溃恢复） |
| `--auto-recover-from-snapshot` | 启用共享内存损坏时的自动恢复 |
| `--resync-blockchain` | 清除 chainbase 和区块日志；从创世或快照重新开始 |

---

### `json_rpc`

框架插件；无配置。注册并调度所有 JSON-RPC 方法。必须首先加载。

所有 JSON-RPC 请求使用 2.0 格式：
```json
{
  "jsonrpc": "2.0",
  "method": "api_name.method_name",
  "params": {},
  "id": 1
}
```

---

## 基础设施插件

### `webserver`

将请求转发到 `json_rpc` 的 HTTP 和 WebSocket 服务器。包含只读响应缓存。

**关键配置选项：**

| 选项 | 默认值 | 描述 |
|------|-------|------|
| `webserver-http-endpoint` | — | HTTP 监听地址（如 `0.0.0.0:8090`） |
| `webserver-ws-endpoint` | — | WebSocket 监听地址（如 `0.0.0.0:8091`） |
| `webserver-thread-pool-size` | `32` | HTTP/WS 处理工作线程 |
| `webserver-cache-enabled` | `true` | 启用响应缓存 |
| `webserver-cache-size` | `10000` | 最大缓存条目数 |

缓存键由 `method + params`（非 `id`）派生，防止通过轮换请求 `id` 来绕过。变更方法（`network_broadcast_api.*`、`debug_node.*`）永不缓存。每个新应用区块时缓存清空。

完整详情参见 [Web 服务器](./webserver.md)。

---

### `p2p`

DLT P2P 网络——区块和交易传播、节点管理、少数派 fork 恢复。

**关键配置选项：**

| 选项 | 默认值 | 描述 |
|------|-------|------|
| `p2p-endpoint` | — | 监听地址（如 `0.0.0.0:2001`） |
| `seed-node` | — | 静态种子节点 |
| `p2p-max-connections` | — | 最大同时连接数 |
| `dlt-block-log-max-blocks` | `100000` | 滚动 DLT 日志容量 |
| `dlt-stats-interval-sec` | `300` | 节点统计日志间隔 |

完整 P2P 架构参见 [P2P 概述](../p2p/overview.md)。

---

### `snapshot`

快照创建、加载和 P2P 快照同步，用于快速引导和崩溃恢复。

详情参见[快照](../node/snapshot.md)和[插件：快照](./snapshot.md)。

---

## API 插件

### `database_api`

主要读取 API。查询区块、交易、账户、链状态、硬分叉版本、委托、提案。

完整方法参考参见 [Database API](./database-api.md)。

---

### `network_broadcast_api`

提交和广播已签名的交易和区块。

| 方法 | 描述 |
|------|------|
| `broadcast_transaction` | 提交交易（异步） |
| `broadcast_transaction_synchronous` | 提交并等待区块包含 |
| `broadcast_transaction_with_callback` | 提交并在包含或过期时回调 |
| `broadcast_block` | 提交已签名区块（仅限验证者） |

---

### `witness_api`

查询验证者状态：活跃集合、计划、单个验证者、投票排名。

| 方法 | 描述 |
|------|------|
| `get_active_witnesses` | 当前 21 验证者活跃集合 |
| `get_witness_schedule` | 完整计划对象 |
| `get_witnesses` | 按数据库 ID 查询验证者 |
| `get_witness_by_account` | 按账户名查询单个验证者 |
| `get_witnesses_by_vote` | 按总投票权重排序的验证者 |
| `get_witnesses_by_counted_vote` | 按计票权重排序的验证者 |
| `get_witness_count` | 已注册验证者总数 |
| `lookup_witness_accounts` | 按前缀列出验证者账户名 |

---

### `account_by_key`

按公钥反向查找账户。

| 方法 | 描述 |
|------|------|
| `get_key_references` | 获取使用给定公钥的账户名 |

---

### `account_history`

账户操作历史，分页显示。

| 方法 | 描述 |
|------|------|
| `get_account_history(account, from, limit)` | 获取操作；`from=-1` 返回最新；每次最多 1000 条 |

**配置选项：**
- `track-account-range` — 索引的账户名范围（默认：所有账户）
- `history-count-blocks` — 保留 N 个区块的历史

---

### `operation_history`

用于区块级和交易查询的所有操作索引。

| 方法 | 描述 |
|------|------|
| `get_ops_in_block(block_num, virtual_ops)` | 区块中的操作；`virtual_ops=true` 包含虚拟操作 |
| `get_transaction(tx_id)` | 按 ID 查询交易 |

**配置选项：**
- `history-whitelist-ops` / `history-blacklist-ops` — 过滤存储的操作类型
- `history-start-block` — 从此区块号开始索引
- `history-count-blocks` — 保留 N 个区块的历史

---

### `committee_api`

查询委员会工作者请求和投票。

| 方法 | 描述 |
|------|------|
| `get_committee_request(id)` | 按 ID 查询请求 |
| `get_committee_request_votes(id)` | 请求上的投票 |
| `get_committee_requests_list(from, limit, status)` | 分页请求列表 |

---

### `invite_api`

查询活跃邀请码。

| 方法 | 描述 |
|------|------|
| `get_invites_list` | 所有邀请 ID |
| `get_invite_by_id(id)` | 按数据库 ID 查询邀请 |
| `get_invite_by_key(pub_key)` | 按公钥查询邀请 |

---

### `paid_subscription_api`

查询订阅服务和订阅者状态。

| 方法 | 描述 |
|------|------|
| `get_paid_subscriptions` | 所有活跃订阅服务 |
| `get_paid_subscription_options(account)` | 账户的订阅配置 |
| `get_paid_subscription_status(subscriber, account)` | 特定订阅的状态 |
| `get_active_paid_subscriptions(subscriber)` | 订阅者的活跃订阅 |
| `get_inactive_paid_subscriptions(subscriber)` | 已过期订阅 |

---

### 已弃用的 API 插件

| 插件 | 方法 | 说明 |
|------|------|------|
| `follow` | 关注者/关注、动态、博客、转发 | 仍可用；不建议新集成使用 |
| `tags` | 按标签的热门/最新内容 | 仍可用；不建议新集成使用 |
| `social_network` | 内容、投票、回复 | 封装委员会/邀请查询；仍可用 |
| `private_message` | 加密消息的收件箱/发件箱 | 基于 `custom_operation`；仍可用 |

---

## 生产插件

### `validator`

区块签名和生产。运行 250ms 定时器循环；当下一个槽位分配给已配置的验证者账户时生产区块。

**关键配置选项：**

| 选项 | 默认值 | 描述 |
|------|-------|------|
| `validator` | — | 验证者账户名 |
| `private-key` | — | 签名用 WIF 私钥 |
| `emergency-private-key` | — | 紧急共识签名密钥 |
| `enable-stale-production` | `false` | 链过期时也生产（仅限测试网） |
| `required-participation` | `3300` | 最低参与度（基点，3300 = 33%） |
| `fork-collision-timeout-blocks` | `21` | 冲突时强制生产前的延迟次数 |

`required-participation` 始终以**基点**表示（0–10000 = 0%–100%）。

完整生产时序和 fork 处理详情参见[验证者插件](./validator.md)。

---

## 推荐插件集

### 最小 API 节点

```ini
plugin = chain
plugin = json_rpc
plugin = webserver
plugin = p2p
plugin = database_api
plugin = network_broadcast_api
```

### 完整 API 节点

```ini
plugin = chain
plugin = json_rpc
plugin = webserver
plugin = p2p
plugin = database_api
plugin = network_broadcast_api
plugin = witness_api
plugin = account_by_key
plugin = account_history
plugin = operation_history
plugin = committee_api
plugin = invite_api
plugin = paid_subscription_api
```

### 验证者节点

```ini
plugin = chain
plugin = p2p
plugin = validator
plugin = json_rpc
plugin = webserver
plugin = database_api
plugin = network_broadcast_api
plugin = witness_api
plugin = snapshot

snapshot-every-n-blocks = 28800
snapshot-dir = /data/snapshots
dlt-block-log-max-blocks = 100000
```

---

参见：[Chain 插件](./chain.md)、[验证者插件](./validator.md)、[快照插件](./snapshot.md)、[P2P 概述](../p2p/overview.md)、[JSON-RPC API](../api/json-rpc.md)。
