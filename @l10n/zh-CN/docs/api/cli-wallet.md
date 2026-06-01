# CLI 钱包

`cli_wallet` 可执行文件提供交互式命令行界面，用于管理账户、签名和广播交易以及查询区块链。

---

## 连接

```bash
cli_wallet --server-rpc-endpoint="ws://127.0.0.1:8091"
```

首次运行时设置密码：
```
new >>> set_password "yourpassword"
```

然后解锁：
```
locked >>> unlock "yourpassword"
```

---

## 钱包管理

| 命令 | 描述 |
|------|------|
| `is_new` | 如果尚未设置密码则返回 `true` |
| `is_locked` | 如果钱包已锁定则返回 `true` |
| `lock` | 锁定钱包 |
| `unlock "password"` | 解锁钱包 |
| `set_password "password"` | 设置或更改密码 |
| `load_wallet_file "file.json"` | 加载钱包文件（`""` = 重新加载当前） |
| `save_wallet_file "file.json"` | 保存钱包到文件 |
| `set_transaction_expiration 60` | 设置交易 TTL（秒） |
| `quit` | 退出钱包 |
| `help` | 列出所有命令 |
| `gethelp "command"` | 某个命令的详细帮助 |

---

## 密钥管理

| 命令 | 描述 |
|------|------|
| `import_key "5K..."` | 导入 WIF 私钥 |
| `suggest_brain_key` | 生成带公私钥对的助记密钥 |
| `list_keys` | 列出钱包中的所有私钥（WIF） |
| `get_private_key "VIZpubkey..."` | 获取已知公钥的 WIF |
| `get_private_key_from_password "account" "role" "password"` | 从凭据派生密钥 |
| `normalize_brain_key "words..."` | 规范化助记密钥字符串 |

---

## 查询

| 命令 | 描述 |
|------|------|
| `info` | 当前区块链状态 |
| `database_info` | 数据库对象统计 |
| `get_block 1000000` | 区块数据 |
| `get_ops_in_block 1000000 false` | 区块中的操作（`true` = 仅虚拟操作） |
| `get_active_validators` | 活跃验证者集合 |
| `get_account "alice"` | 账户对象 |
| `list_accounts "" 100` | 分页账户列表 |
| `list_my_accounts` | 钱包中有密钥的账户 |
| `get_account_history "alice" -1 100` | alice 的最后 100 个操作 |
| `get_transaction "txid..."` | 按 ID 查询交易 |
| `get_master_history "alice"` | 主密钥更改历史 |
| `get_withdraw_routes "alice" "all"` | 提取路由（`"incoming"`、`"outgoing"`、`"all"`） |
| `get_proposed_transactions "alice" 0 100` | 需要 alice 批准的提案 |

---

## 账户操作

| 命令 | 描述 |
|------|------|
| `create_account "creator" "1.000 VIZ" "10.000000 SHARES" "newaccount" "{}" true` | 使用自动生成的密钥创建账户 |
| `create_account_with_keys "creator" "1.000 VIZ" "10.000000 SHARES" "newaccount" "{}" "VIZmaster..." "VIZactive..." "VIZregular..." "VIZmemo..." true` | 使用指定密钥创建账户 |
| `update_account "account" "{}" "VIZm..." "VIZa..." "VIZr..." "VIZmemo..." true` | 更新所有密钥和元数据 |
| `update_account_auth_key "account" "active" "VIZnewkey..." 1 true` | 向授权添加密钥（权重 0 = 删除） |
| `update_account_auth_account "account" "active" "guardian" 1 true` | 向授权添加账户 |
| `update_account_auth_threshold "account" "active" 2 true` | 设置授权权重阈值 |
| `update_account_meta "account" "{\"key\":\"value\"}" true` | 更新 JSON 元数据（regular 授权） |
| `update_account_memo_key "account" "VIZnewmemo..." true` | 更新 memo 密钥 |
| `delegate_vesting_shares "alice" "bob" "100.000000 SHARES" true` | 委托 SHARES（0 = 移除） |

---

## 转账和质押

| 命令 | 描述 |
|------|------|
| `transfer "alice" "bob" "10.000 VIZ" "memo" true` | 转账 VIZ（memo 前缀 `#` = 加密） |
| `transfer_to_vesting "alice" "alice" "100.000 VIZ" true` | 将 VIZ 质押为 SHARES |
| `withdraw_vesting "alice" "100.000000 SHARES" true` | 开始解质押（0 = 取消） |
| `set_withdraw_vesting_route "alice" "bob" 5000 false true` | 将 50% 提取路由到 bob 作为 VIZ |

---

## 验证者操作

| 命令 | 描述 |
|------|------|
| `list_validators "" 100` | 列出验证者 |
| `get_validator "validatorname"` | 验证者对象 |
| `update_validator "myvalidator" "https://url" "VIZsigningkey..." true` | 注册/更新验证者 |
| `update_chain_properties "myvalidator" {...} true` | 对链属性投票（init 格式） |
| `versioned_update_chain_properties "myvalidator" {...} true` | 对版本化链属性投票（hf9 格式） |
| `vote_for_validator "alice" "myvalidator" true true` | 为验证者投票（`false` = 撤销投票） |
| `set_voting_proxy "alice" "proxy" true` | 设置投票代理（`""` = 移除） |

---

## 托管操作

| 命令 | 描述 |
|------|------|
| `escrow_transfer "alice" "bob" "agent" 1 "100.000 VIZ" "1.000 VIZ" "2024-06-01T00:00:00" "2024-07-01T00:00:00" "{}" true` | 创建托管 |
| `escrow_approve "alice" "bob" "agent" "bob" 1 true true` | 批准托管（who = `"bob"` 或 `"agent"`） |
| `escrow_dispute "alice" "bob" "agent" "alice" 1 true` | 发起争议（who = `"alice"` 或 `"bob"`） |
| `escrow_release "alice" "bob" "agent" "agent" "bob" 1 "100.000 VIZ" true` | 释放资金 |

---

## 恢复操作

| 命令 | 描述 |
|------|------|
| `request_account_recovery "recovery" "victim" {"weight_threshold":1,...} true` | 以恢复账户身份请求恢复 |
| `recover_account "victim" {"recent_master_auth"} {"new_master_auth"} true` | 确认恢复 |
| `change_recovery_account "account" "new_recovery" true` | 更改恢复账户（30 天延迟） |

---

## 委员会操作

| 命令 | 描述 |
|------|------|
| `committee_worker_create_request "creator" "https://url" "worker" "100.000 VIZ" "500.000 VIZ" 604800 true` | 创建资金请求 |
| `committee_worker_cancel_request "creator" 123 true` | 取消请求 |
| `committee_vote_request "voter" 123 10000 true` | 投票（+10000 = 完全支持，-10000 = 完全反对，0 = 撤销） |

---

## 邀请操作

| 命令 | 描述 |
|------|------|
| `create_invite "creator" "10.000 VIZ" "VIZinvitekey..." true` | 创建邀请 |
| `claim_invite_balance "initiator" "receiver" "5Kinvitesecret..." true` | 领取邀请余额 |
| `invite_registration "initiator" "newaccount" "5Kinvitesecret..." "VIZnewaccountkey..." true` | 从邀请创建账户 |
| `use_invite_balance "initiator" "receiver" "5Kinvitesecret..." true` | 使用邀请余额（可能归属为 SHARES） |

---

## 奖励操作

| 命令 | 描述 |
|------|------|
| `award "alice" "bob" 1000 0 "Great work!" [] true` | 基于能量的奖励 |
| `fixed_award "alice" "bob" "10.000000 SHARES" 5000 0 "Reward" [] true` | 固定 SHARES 数量奖励 |

受益人格式：`[{"account":"charlie","weight":2000}]`

---

## 订阅操作

| 命令 | 描述 |
|------|------|
| `set_paid_subscription "account" "https://url" 3 "10.000 VIZ" 30 true` | 创建订阅（3 级，10 VIZ/期，30 天周期） |
| `paid_subscribe "subscriber" "account" 2 "20.000 VIZ" 1 true true` | 订阅级别 2 |

---

## 账户市场

| 命令 | 描述 |
|------|------|
| `set_account_price "account" "account" "100.000 VIZ" true true` | 挂牌出售 |
| `set_subaccount_price "account" "account" "50.000 VIZ" true true` | 挂牌出售子账户创建权 |
| `buy_account "buyer" "account" "100.000 VIZ" "VIZnewkey..." "0.000 VIZ" true` | 购买账户 |
| `target_account_sale "account" "account" "targetbuyer" "100.000 VIZ" true true` | 定向销售 |

---

## 自定义操作

```bash
custom [] ["alice"] "my_app" "{\"action\":\"follow\",\"target\":\"bob\"}" true
```

参数：`required_active_auths` `required_regular_auths` `id` `json` `broadcast`

---

## 交易构建器

构建并签名包含多个操作的自定义交易：

```bash
begin_builder_transaction           # 返回句柄（例如 0）
add_operation_to_builder_transaction 0 [2,{"from":"alice","to":"bob","amount":"10.000 VIZ","memo":""}]
sign_builder_transaction 0 true     # 签名并广播
```

| 命令 | 描述 |
|------|------|
| `begin_builder_transaction` | 开始新交易（返回句柄） |
| `add_operation_to_builder_transaction handle [type_id, op]` | 添加操作 |
| `replace_operation_in_builder_transaction handle idx [type_id, op]` | 替换操作 |
| `preview_builder_transaction handle` | 预览交易 JSON |
| `sign_builder_transaction handle broadcast` | 签名（并可选广播） |
| `propose_builder_transaction handle author title memo expiry review broadcast` | 包装为提案 |
| `remove_builder_transaction handle` | 丢弃 |
| `get_prototype_operation "transfer_operation"` | 获取空操作模板 |
| `serialize_transaction {trx}` | 获取十六进制序列化 |
| `sign_transaction {trx} broadcast` | 签名任意交易 |

---

## NS DNS 助手

在账户元数据中存储 DNS 记录：

```bash
ns_set_records "myaccount" {"a_records":["188.120.231.153"],"ssl_hash":"4a4613...","ttl":28800} true
ns_get_summary "myaccount"
ns_extract_a_records "myaccount"
ns_remove_records "myaccount" true
```

验证助手：`ns_validate_ipv4`、`ns_validate_sha256_hash`、`ns_validate_ttl`、`ns_validate_ssl_txt_record`、`ns_validate_metadata`、`ns_create_metadata`。

---

## 私信

```bash
get_encrypted_memo "alice" "bob" "#secret message"
decrypt_memo "#encrypteddata..."
get_inbox "myaccount" "2024-01-15T00:00:00" 100 0
get_outbox "myaccount" "2024-01-15T00:00:00" 100 0
```

---

参见：[JSON-RPC API](./json-rpc.md)、[操作概述](../protocol/operations/overview.md)、[数据类型](../protocol/data-types.md)。
