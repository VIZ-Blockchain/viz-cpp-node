# 代理权限（HF15）

主账户可以登记命名代理公钥，允许其代表账户签署明确列出的操作。代理是账户上的记录，不是独立账户；无需交出主账户私钥，可随时撤销。

## `set_agent_permission_operation`（ID 105）

需要 `account` 的 **active** 权限；HF15 前拒绝。

| 字段 | 类型 | 含义 |
|---|---|---|
| `account` | account_name_type | 授权主账户 |
| `agent_name` | string | 同账户内唯一，1–32 字节，字符 `[a-z0-9_-]`；超长拒绝，不截断 |
| `agent_key` | public_key_type | 授权时必填，撤销时忽略 |
| `operations` | `flat_set<string>` | 允许操作的 wire 名称，如 transfer、pm_place_bet |
| `expiration` | time_point_sec | 1970-01-01T00:00:00 为永久；过去时间撤销 |
| `addons` | `flat_set<string>` | 链外服务 scope，最多 10 个，每个小于 64 字节且不含逗号；共识不解释、不授予链上权限 |
| `extensions` | extensions_type | 始终为 [] |

```json
["set_agent_permission", {"account":"alice","agent_name":"trade-bot","agent_key":"VIZ6MyX5QiXAXRZk7SYCiqpi6Mtm8UbHWDFSV8HPpt7FJyahCnc2T","operations":["pm_place_bet","transfer"],"expiration":"2027-01-01T00:00:00","addons":["vizhub"],"extensions":[]}]
```

operations 为空、addons 非空可创建只供链外服务登录的代理。两个列表都为空即撤销；撤销也可使用空公钥。相同 agent_name 更新记录；同一主账户不能把同一代理公钥分给不同名称。最多 16 个代理，授权前清除已过期代理。

## 权限边界

明确授权可满足主账户的直接顶层 active/regular 要求；每个操作、每个主账户分别判断。普通权限优先，含 master 回退；多余未使用签名仍非法。旧有禁止混合 regular 与 active/master 操作的规则不变。

代理不能满足 master、任意 other 权限或嵌套 account_auths，代理签名不产生账户批准，也不能授权同交易中的另一个账户。proposal_create 的明确授权只允许保存提案，不自动批准或执行内部操作；proposal_delete 仍须满足原 evaluator。proposal_update 禁止用于代理，不能形成持久账户/公钥批准。

每个操作执行前重新检查依赖代理的要求；前序撤销、限制、换代理密钥或清空权限会立刻使后续代理证明失效。普通进入交易时的权限证明行为不变。

master 或 active 字段出现在 account_update 时，即使密钥值相同，也会清空所有代理；恢复、出售、拍卖成交同样清空。仅改 regular 不清空。只更新 regular/memo 的客户端必须省略 master/active；PHP build_account_update 传 null 表示省略。

## 查询与钱包

`database_api.get_agent_permissions(account)` 返回代理列表。get_potential_signatures 提供候选密钥；get_required_signatures 提供当前可用贡献，不代表所有主账户权限已满足。verify_authority 和实际应用交易仍检查完整权限、操作范围和过期状态。

WebVIZWallet 的 Agents 页面可生成密钥（私钥仅展示一次）、配置操作/到期/addons、查看和撤销代理，授权由主账户 active 签署。Vizonator 向网站提供同类操作并显示确认窗口。代理登录使用主账户名和代理私钥，客户端核对链上授权，显示 🤖、允许操作及有效期，禁用越权动作。

网站 `domain:auth:account:regular:time:nonce` 登录签名是独立链外流程，不是链上操作；代理会话不代答普通 regular 登录请求。PHP VIZ Auth / JS viz.auth 只有在代理持有所需网站 addon 时才接受代理链外签名；链外授权来自 addon，不是 operations 列表。master/active 更新或恢复/出售后须重新授权代理。
