# JSON-RPC API

所有 VIZ 节点 API 使用 JSON-RPC 2.0，通过 HTTP POST 或 WebSocket 访问。

---

## 请求格式

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "api_name.method_name",
  "params": [arg1, arg2]
}
```

- `id` 可以是任意数字或字符串；响应中会原样返回。
- `params` 可以是数组或对象，取决于方法。
- 支持 HTTP POST 和 WebSocket。订阅需要 WebSocket。

---

## 响应格式

**成功：**
```json
{ "jsonrpc": "2.0", "id": 1, "result": { ... } }
```

**错误：**
```json
{ "jsonrpc": "2.0", "id": 1, "error": { "code": -32602, "message": "Invalid params" } }
```

### 错误代码

| 代码 | 含义 |
|------|------|
| `-32700` | 解析错误——无效的 JSON |
| `-32600` | 无效请求 |
| `-32601` | 方法未找到 |
| `-32602` | 无效参数 |
| `-32603` | 内部错误 |
| `-32099` 到 `-32000` | 服务器错误（处理器抛出的异常） |

---

## 插件命名空间

| 命名空间 | 状态 | 描述 |
|---------|------|------|
| `database_api` | 活跃 | 区块、账户、链状态查询 |
| `network_broadcast_api` | 活跃 | 交易和区块广播 |
| `witness_api` | 活跃 | 验证者查询 |
| `account_by_key` | 活跃 | 反向密钥查找 |
| `account_history` | 活跃 | 每账户操作历史 |
| `operation_history` | 活跃 | 区块操作查询 |
| `committee_api` | 活跃 | 委员会请求查询 |
| `invite_api` | 活跃 | 邀请查询 |
| `paid_subscription_api` | 活跃 | 订阅查询 |
| `custom_protocol_api` | 活跃 | 自定义协议元数据 |
| `auth_util` | 活跃 | 权限验证 |
| `block_info` | 活跃 | 扩展区块信息 |
| `raw_block` | 活跃 | 原始区块导出 |
| `follow` | 已弃用 | 关注/动态索引 |
| `tags` | 已弃用 | 按标签发现内容 |
| `social_network` | 已弃用 | 高级内容查询 |
| `private_message` | 已弃用 | 加密消息索引 |
| `debug_node` | 仅开发 | 测试/调试操作 |

---

## `database_api` 方法

| 方法 | 描述 |
|------|------|
| `get_block_header(block_num)` | 给定高度的区块头 |
| `get_block(block_num)` | 完整签名区块 |
| `get_irreversible_block_header(block_num)` | 不可逆时的区块头 |
| `get_irreversible_block(block_num)` | 不可逆时的完整区块 |
| `set_block_applied_callback(callback)` | WebSocket：订阅新区块 |
| `get_config()` | 编译时链常量 |
| `get_dynamic_global_properties()` | 当前链状态 |
| `get_chain_properties()` | 验证者中位数链属性 |
| `get_hardfork_version()` | 当前硬分叉版本字符串 |
| `get_next_scheduled_hardfork()` | 下一个待定硬分叉信息 |
| `get_accounts(names[])` | 完整账户对象 |
| `lookup_account_names(names[])` | 同 get_accounts 但支持 null |
| `lookup_accounts(lower_bound, limit)` | 分页账户名列表 |
| `get_account_count()` | 已注册账户总数 |
| `get_master_history(account)` | 主密钥更改历史 |
| `get_recovery_request(account)` | 待处理账户恢复请求 |
| `get_escrow(from, escrow_id)` | 托管对象 |
| `get_withdraw_routes(account, type)` | 质押提取路由（`"incoming"` / `"outgoing"` / `"all"`） |
| `get_vesting_delegations(account, from, limit, type)` | 委托（`"delegated"` / `"received"`） |
| `get_expiring_vesting_delegations(account, from, limit)` | 返回窗口内的委托 |
| `get_transaction_hex(trx)` | 十六进制编码的序列化交易 |
| `get_required_signatures(trx, available_keys[])` | 签名所需的最小密钥集 |
| `get_potential_signatures(trx)` | 所有可以签名的密钥 |
| `verify_authority(trx)` | 完全签名时返回 `true` |
| `verify_account_authority(name, keys[])` | 密钥满足授权时返回 `true` |
| `get_database_info()` | Chainbase 内存使用统计 |
| `get_proposed_transactions(account, from, limit)` | 需要账户批准的提案 |
| `get_accounts_on_sale(from, limit)` | 挂牌直接出售的账户 |
| `get_accounts_on_auction(from, limit)` | 挂牌拍卖的账户 |
| `get_subaccounts_on_sale(from, limit)` | 挂牌出售的子账户创建权 |

---

## `network_broadcast_api` 方法

| 方法 | 描述 |
|------|------|
| `broadcast_transaction(trx)` | 广播（异步） |
| `broadcast_transaction_synchronous(trx)` | 广播并等待区块包含 |
| `broadcast_transaction_with_callback(callback, trx)` | 带 WebSocket 回调的广播 |
| `broadcast_block(block)` | 广播已签名区块（验证者） |

---

## `witness_api` 方法

| 方法 | 描述 |
|------|------|
| `get_active_witnesses()` | 当前活跃验证者集合（21 个账户） |
| `get_witness_schedule()` | 完整验证者计划对象 |
| `get_witnesses(ids[])` | 按内部 ID 查询验证者 |
| `get_witness_by_account(account)` | 账户的验证者对象 |
| `get_witnesses_by_vote(lower_bound, limit)` | 按投票权重排名的验证者 |
| `get_witnesses_by_counted_vote(lower_bound, limit)` | 按计票排名的验证者 |
| `get_witness_count()` | 已注册验证者总数 |
| `lookup_witness_accounts(lower_bound, limit)` | 列出验证者账户名 |

---

## `account_history` 方法

### `get_account_history(account, from, limit)`

返回涉及 `account` 的操作。`from = -1` 从最近的开始。

```json
{
  "method": "account_history.get_account_history",
  "params": ["alice", -1, 100]
}
```

返回 `{ sequence: { trx_id, block, op: [type_id, data] } }` 的映射。

---

## `operation_history` 方法

| 方法 | 描述 |
|------|------|
| `get_ops_in_block(block_num, only_virtual)` | 区块中的操作 |
| `get_transaction(trx_id)` | 按 ID 查询交易 |

---

## `committee_api` 方法

| 方法 | 描述 |
|------|------|
| `get_committee_request(request_id)` | 请求详情和状态 |
| `get_committee_request_votes(request_id)` | 请求上的投票 |
| `get_committee_requests_list(from, limit)` | 请求 ID 列表 |

---

## `invite_api` 方法

| 方法 | 描述 |
|------|------|
| `get_invites_list(from, limit)` | 所有活跃邀请 ID |
| `get_invite_by_id(id)` | 按内部 ID 查询邀请 |
| `get_invite_by_key(public_key)` | 按公钥查询邀请 |

---

## `paid_subscription_api` 方法

| 方法 | 描述 |
|------|------|
| `get_paid_subscriptions(from, limit)` | 所有订阅服务 |
| `get_paid_subscription_options(account)` | 账户的订阅配置 |
| `get_paid_subscription_status(subscriber, account)` | 订阅状态 |
| `get_active_paid_subscriptions(subscriber, from, limit)` | 活跃订阅 |
| `get_inactive_paid_subscriptions(subscriber, from, limit)` | 已过期订阅 |

---

## WebSocket 订阅

仅在持久 WebSocket 连接上可用。

| 方法 | 回调数据 |
|------|---------|
| `database_api.set_block_applied_callback` | 每个应用区块的区块头 |
| `database_api.set_pending_transaction_callback` | 进入待处理池时的交易 |
| `database_api.cancel_all_subscriptions` | 取消所有订阅 |

---

## 推荐插件集

**最小 API 节点：**
```ini
plugin = chain json_rpc webserver p2p
plugin = database_api network_broadcast_api
```

**完整 API 节点（添加）：**
```ini
plugin = witness_api account_by_key account_history operation_history
plugin = committee_api invite_api paid_subscription_api
```

**验证者节点（添加）：**
```ini
plugin = validator snapshot
```

---

参见：[Database API](../plugins/database-api.md)、[Web 服务器插件](../plugins/webserver.md)、[操作概述](../protocol/operations/overview.md)。
