# Database API

`database_api` 插件提供只读 JSON-RPC 方法，用于查询区块链状态：区块、账户、链属性、委托、权限验证和治理。

**源码：** [plugins/database_api/](../../plugins/database_api/)

---

## 依赖

```
json_rpc::plugin, chain::plugin
```

---

## 请求格式

所有方法通过 HTTP POST 或 WebSocket 使用 JSON-RPC 2.0：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "database_api.<method_name>",
  "params": [<arg1>, <arg2>, ...]
}
```

---

## 区块和交易

### `get_block_header(block_num)`

返回给定区块编号的区块头，如果未找到则返回 `null`。

```json
{ "method": "database_api.get_block_header", "params": [12345678] }
```

**返回：** `block_header` — `previous`、`timestamp`、`witness`、`transaction_merkle_root`、`extensions`。

---

### `get_block(block_num)`

返回包含所有交易的完整签名区块。

```json
{ "method": "database_api.get_block", "params": [12345678] }
```

**返回：** `signed_block` — 所有头字段加上包含完整操作数据的 `transactions[]`，以及 `block_id`、`signing_key`、`transaction_ids[]`。

---

### `get_irreversible_block_header(block_num)`

与 `get_block_header` 相同，但仅当区块达到 LIB（不可逆）时才返回。

---

### `get_irreversible_block(block_num)`

与 `get_block` 相同，但仅当区块达到 LIB 时才返回。

---

### `set_block_applied_callback(callback)`

注册 WebSocket 回调，在每个应用的区块上收到通知。回调接收区块头作为 JSON 变体。

**仅 WebSocket。** 通过 `cancel_all_subscriptions` 取消订阅。

---

## 链全局参数

### `get_config()`

返回编译时常量：链 ID、代币符号、区块间隔、最大区块大小、投票周期和所有 `CHAIN_*` 常量。

```json
{ "method": "database_api.get_config", "params": [] }
```

---

### `get_dynamic_global_properties()`

返回实时链状态：当前 head 区块编号和 ID、时间、当前验证者、总 vesting shares、参与率、DPO 基金余额等。

```json
{ "method": "database_api.get_dynamic_global_properties", "params": [] }
```

关键字段：`head_block_number`、`head_block_id`、`time`、`current_witness`、`total_vesting_shares`、`total_vesting_fund_viz`、`committee_fund`、`last_irreversible_block_num`、`participation_count`。

---

### `get_chain_properties()`

返回当前链上治理参数（通过 `chain_properties_update_operation` 设置）：最低账户创建费、最大区块大小、VIZ 账户创建费、带宽预留百分比和奖励参数。

```json
{ "method": "database_api.get_chain_properties", "params": [] }
```

---

### `get_hardfork_version()`

返回当前活跃硬分叉的版本字符串（例如 `"1.0.0"`）。

```json
{ "method": "database_api.get_hardfork_version", "params": [] }
```

---

### `get_next_scheduled_hardfork()`

返回下一个待定硬分叉的版本和计划激活时间。

```json
{ "method": "database_api.get_next_scheduled_hardfork", "params": [] }
```

**返回：** `{ "hf_version": "1.0.0", "live_time": "2025-01-01T00:00:00" }`

---

## 账户

### `get_accounts(account_names)`

返回给定账户名列表的完整账户对象。

```json
{ "method": "database_api.get_accounts", "params": [["alice", "bob"]] }
```

**返回：** `account_api_object` 数组 — 名称、余额、vesting shares、收到的 vesting、委托的 vesting、密钥、恢复账户、创建时间、帖子数、投票权等。

---

### `lookup_account_names(account_names)`

与 `get_accounts` 相同，但对不存在的账户返回 `null`。

```json
{ "method": "database_api.lookup_account_names", "params": [["alice", "nonexistent"]] }
```

**返回：** `optional<account_api_object>` 数组 — 缺失账户为 `null`。

---

### `lookup_accounts(lower_bound_name, limit)`

返回从 `lower_bound_name` 开始的账户名集合，最多 `limit` 个结果（最大 1000）。用于分页账户枚举。

```json
{ "method": "database_api.lookup_accounts", "params": ["alice", 100] }
```

**返回：** 账户名字符串集合。

---

### `get_account_count()`

返回已注册账户的总数。

```json
{ "method": "database_api.get_account_count", "params": [] }
```

---

## 账户状态

### `get_master_history(account)`

返回给定账户的主权限变更历史。

```json
{ "method": "database_api.get_master_history", "params": ["alice"] }
```

**返回：** `master_authority_history_api_object` 数组 — `account`、`previous_master_authority`、`last_valid_time`。

---

### `get_recovery_request(account)`

返回给定账户的待处理账户恢复请求（如有）。

```json
{ "method": "database_api.get_recovery_request", "params": ["alice"] }
```

**返回：** `optional<account_recovery_request_api_object>` — `account_to_recover`、`new_master_authority`、`expires`。

---

### `get_escrow(from, escrow_id)`

返回给定发送者和托管 ID 的托管转账对象。

```json
{ "method": "database_api.get_escrow", "params": ["alice", 1] }
```

**返回：** `optional<escrow_api_object>` — 所有托管字段，包括 `from`、`to`、`agent`、`ratification_deadline`、`escrow_expiration`、金额和批准状态。

---

### `get_withdraw_routes(account, type)`

返回账户的 vesting 提取路由。`type` 为 `"incoming"`、`"outgoing"` 或 `"all"` 之一。

```json
{ "method": "database_api.get_withdraw_routes", "params": ["alice", "outgoing"] }
```

**返回：** `{ "from_account", "to_account", "percent", "auto_vest" }` 数组。

---

### `get_vesting_delegations(account, from, limit, type)`

返回账户的 vesting 委托。`type` 为 `"delegated"`（此账户委托出去的）或 `"received"`（收到的委托）。

```json
{ "method": "database_api.get_vesting_delegations", "params": ["alice", "", 100, "delegated"] }
```

**返回：** `vesting_delegation_api_object` 数组 — `delegator`、`delegatee`、`vesting_shares`、`min_delegation_time`。

---

### `get_expiring_vesting_delegations(account, from, limit)`

返回账户的 vesting 委托到期条目 — 已撤销并等待返还窗口的委托。

```json
{ "method": "database_api.get_expiring_vesting_delegations", "params": ["alice", "1970-01-01T00:00:00", 100] }
```

**返回：** `vesting_delegation_expiration_api_object` 数组 — `delegator`、`vesting_shares`、`expiration`。

---

## 权限和交易验证

### `get_transaction_hex(trx)`

返回交易的十六进制编码序列化二进制形式。用于签名和广播。

```json
{ "method": "database_api.get_transaction_hex", "params": [{ ...交易对象... }] }
```

**返回：** 十六进制字符串。

---

### `get_required_signatures(trx, available_keys)`

给定部分签名的交易和签名者可用的公钥集合，返回授权交易必须签名的最小密钥子集。

```json
{
  "method": "database_api.get_required_signatures",
  "params": [{ ...trx... }, ["VIZ5...", "VIZ6..."]]
}
```

**返回：** 公钥字符串集合。

---

### `get_potential_signatures(trx)`

返回所有可能签署交易的公钥（跨所有涉及的账户和权限级别）。在调用 `get_required_signatures` 之前，用此方法预过滤钱包的密钥集。

```json
{ "method": "database_api.get_potential_signatures", "params": [{ ...trx... }] }
```

**返回：** 公钥字符串集合。

---

### `verify_authority(trx)`

如果交易具有所有必要的签名则返回 `true`；否则抛出错误，描述缺少什么。

```json
{ "method": "database_api.verify_authority", "params": [{ ...signed_trx... }] }
```

---

### `verify_account_authority(name, signers)`

如果给定的公钥集合具有足够的权限代表 `name` 行动则返回 `true`。

```json
{ "method": "database_api.verify_account_authority", "params": ["alice", ["VIZ5..."]] }
```

---

## 数据库信息

### `get_database_info()`

返回 chainbase 共享内存使用统计。

```json
{ "method": "database_api.get_database_info", "params": [] }
```

**返回：**
```json
{
  "total_size": 4294967296,
  "free_size": 1073741824,
  "reserved_size": 0,
  "used_size": 3221225472,
  "index_list": [
    { "name": "account_object", "record_count": 52341 },
    ...
  ]
}
```

---

## 治理

### `get_proposed_transactions(account, from, limit)`

返回需要 `account` 批准的治理提案。

```json
{ "method": "database_api.get_proposed_transactions", "params": ["alice", 0, 100] }
```

**返回：** `proposal_api_object` 数组 — 完整的提案详细信息，包括所需批准、到期时间和操作列表。

---

## 账户市场

### `get_accounts_on_sale(from, limit)`

返回当前挂牌出售的账户（直接出售，非拍卖）。

```json
{ "method": "database_api.get_accounts_on_sale", "params": [0, 100] }
```

**返回：** `account_on_sale_api_object` 数组 — `account`、`account_seller`、`account_offer_price`、`account_on_sale_start_time`、`target_buyer`。

---

### `get_accounts_on_auction(from, limit)`

返回挂牌拍卖的账户。

```json
{ "method": "database_api.get_accounts_on_auction", "params": [0, 100] }
```

**返回：** `account_on_sale_api_object` 数组 — 同上，另加 `current_bid`、`current_bidder`、`current_bidder_key`、`last_bid`。

---

### `get_subaccounts_on_sale(from, limit)`

返回可出售的账户命名空间注册（子账户创建权限）。

```json
{ "method": "database_api.get_subaccounts_on_sale", "params": [0, 100] }
```

**返回：** `subaccount_on_sale_api_object` 数组 — `account`、`subaccount_seller`、`subaccount_offer_price`。

---

## 错误代码

| 代码 | 含义 |
|------|------|
| `-32700` | 解析错误 — 无效 JSON |
| `-32600` | 无效请求 — 缺少必填字段 |
| `-32601` | 方法未找到 |
| `-32602` | 无效参数 |
| `-32603` | 内部错误 |
| `-32099` 至 `-32000` | 服务器错误（方法处理器的异常） |

---

参见：[插件概述](./overview.md)、[validator_api 方法](./overview.md#validator_api)、[JSON-RPC API](../api/json-rpc.md)。
