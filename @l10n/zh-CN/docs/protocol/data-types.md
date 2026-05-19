# 公共数据类型

VIZ Ledger 协议操作和虚拟操作中使用的所有共享数据类型。

---

## 原始类型

| C++ 类型 | JSON 表示 | 描述 |
|---------|----------|------|
| `string` | `string` | UTF-8 字符串 |
| `bool` | `boolean` | `true` / `false` |
| `uint8_t` | `integer` | 无符号 8 位整数 |
| `uint16_t` | `integer` | 无符号 16 位整数（0–65535） |
| `int16_t` | `integer` | 有符号 16 位整数（−32768–32767） |
| `uint32_t` | `integer` | 无符号 32 位整数 |
| `int32_t` | `integer` | 有符号 32 位整数 |
| `uint64_t` | `string` 或 `integer` | 无符号 64 位整数——在 JavaScript 中使用字符串以避免溢出 |
| `int64_t` | `string` 或 `integer` | 有符号 64 位整数 |
| `share_type` | `integer` | `safe<int64_t>` 的别名——以最小单位表示的代币数量 |
| `time_point_sec` | `string` | ISO 8601 UTC 日期时间：`"2024-01-15T12:00:00"`（无时区后缀） |

---

## `account_name_type`

标识账户的固定长度字符串（最多 16 字节）。规则：

- 点号分隔的标签；每个标签至少 3 个字符。
- 以字母开头，以字母或数字结尾。
- 仅小写字母（`a`–`z`）、数字（`0`–`9`）、连字符（`-`）。
- 最小长度：2 个字符（`CHAIN_MIN_ACCOUNT_NAME_LENGTH`）。
- 最大长度：16 个字符（`CHAIN_MAX_ACCOUNT_NAME_LENGTH`）。

**JSON：** 普通字符串——`"alice"`、`"alice.bob"`

---

## `public_key_type`

以 base58check 编码并带有 `VIZ` 前缀的 secp256k1 压缩公钥。

**JSON：** 字符串——`"VIZ5hqSa4NkEZGAMUpoH5EaEr64mBJuMcPpGjvk8qb7hcPFTbXSQ9"`

- 前缀必须是 `VIZ`（不是 `STM`、`GLS` 或其他）。
- 由 33 字节压缩公钥 + 4 字节校验和 = 共 37 字节进行 base58 编码。

---

## `asset`

表示带有符号的代币数量。在 JSON API 响应和操作参数中序列化为人类可读的字符串：

```
"10.000 VIZ"
"5.000000 SHARES"
```

### 代币符号

| 符号 | 字符串 | 小数位数 | 描述 |
|------|-------|---------|------|
| `TOKEN_SYMBOL` | `VIZ` | 3 | 主要流动代币 |
| `SHARES_SYMBOL` | `SHARES` | 6 | 质押份额（已质押的 VIZ） |

构建操作时，始终使用字符串格式。通过空格分割解析：左边是数量，右边是符号。VIZ 使用 3 位小数；SHARES 使用 6 位。

---

## `authority`

控制账户权限级别的多重签名授权结构。

```json
{
  "weight_threshold": 1,
  "account_auths": [
    ["alice", 1]
  ],
  "key_auths": [
    ["VIZ5hqSa4NkEZGAMUpoH5EaEr64mBJuMcPpGjvk8qb7hcPFTbXSQ9", 1]
  ]
}
```

| 字段 | 类型 | 描述 |
|------|------|------|
| `weight_threshold` | `uint32_t` | 满足授权所需的最低总权重 |
| `account_auths` | `[[account_name, weight], ...]` | 基于账户的签名者 |
| `key_auths` | `[[public_key, weight], ...]` | 基于密钥的签名者 |

满足的签名者权重之和必须 ≥ `weight_threshold`。空授权：`{ "weight_threshold": 0, "account_auths": [], "key_auths": [] }`。

### 授权级别

| 级别 | 用于 |
|------|------|
| `master` | 最高安全性——更换密钥、账户恢复 |
| `active` | 代币操作——转账、质押、验证者投票 |
| `regular` | 社交操作——内容、奖励、委员会投票 |

---

## `beneficiary_route_type`

指定内容支付的受益人及其奖励份额。

```json
{ "account": "alice", "weight": 2500 }
```

| 字段 | 类型 | 描述 |
|------|------|------|
| `account` | `account_name_type` | 受益人账户 |
| `weight` | `uint16_t` | 以基点表示的份额（10000 = 100%） |

- 所有受益人权重之和不得超过 10000。
- 受益人必须在数组中按账户名升序排列。
- 每个受益人账户必须在链上存在。

---

## `extensions_type`

当前未使用——始终序列化为空数组。

```json
"extensions": []
```

---

## `versioned_chain_properties`

包含链属性某一版本的静态变体。序列化为 2 元素数组 `[type_index, object]`。

| 索引 | 类型 |
|------|------|
| 0 | `chain_properties_init` |
| 1 | `chain_properties_hf4` |
| 2 | `chain_properties_hf6` |
| 3 | `chain_properties_hf9`（当前） |

每个版本的完整字段参考参见[链属性](../governance/chain-properties.md)。

---

## `operation`（静态变体）

每个操作序列化为 2 元素数组：`[type_id, operation_object]`。

### 常规操作（用户广播）

| ID | 操作 |
|----|------|
| 0 | `vote_operation` *（已弃用）* |
| 1 | `content_operation` *（已弃用）* |
| 2 | `transfer_operation` |
| 3 | `transfer_to_vesting_operation` |
| 4 | `withdraw_vesting_operation` |
| 5 | `account_update_operation` |
| 6 | `witness_update_operation` |
| 7 | `account_witness_vote_operation` |
| 8 | `account_witness_proxy_operation` |
| 9 | `delete_content_operation` *（已弃用）* |
| 10 | `custom_operation` |
| 11 | `set_withdraw_vesting_route_operation` |
| 12 | `request_account_recovery_operation` |
| 13 | `recover_account_operation` |
| 14 | `change_recovery_account_operation` |
| 15 | `escrow_transfer_operation` |
| 16 | `escrow_dispute_operation` |
| 17 | `escrow_release_operation` |
| 18 | `escrow_approve_operation` |
| 19 | `delegate_vesting_shares_operation` |
| 20 | `account_create_operation` |
| 21 | `account_metadata_operation` |
| 22 | `proposal_create_operation` |
| 23 | `proposal_update_operation` |
| 24 | `proposal_delete_operation` |
| 25 | `chain_properties_update_operation` |
| 35 | `committee_worker_create_request_operation` |
| 36 | `committee_worker_cancel_request_operation` |
| 37 | `committee_vote_request_operation` |
| 43 | `create_invite_operation` |
| 44 | `claim_invite_balance_operation` |
| 45 | `invite_registration_operation` |
| 46 | `versioned_chain_properties_update_operation` |
| 47 | `award_operation` |
| 50 | `set_paid_subscription_operation` |
| 51 | `paid_subscribe_operation` |
| 54 | `set_account_price_operation` |
| 55 | `set_subaccount_price_operation` |
| 56 | `buy_account_operation` |
| 58 | `use_invite_balance_operation` |
| 60 | `fixed_award_operation` |
| 61 | `target_account_sale_operation` |

### 虚拟操作（区块链生成，不可广播）

| ID | 操作 |
|----|------|
| 26 | `author_reward_operation` |
| 27 | `curation_reward_operation` |
| 28 | `content_reward_operation` |
| 29 | `fill_vesting_withdraw_operation` |
| 30 | `shutdown_witness_operation` |
| 31 | `hardfork_operation` |
| 32 | `content_payout_update_operation` |
| 33 | `content_benefactor_reward_operation` |
| 34 | `return_vesting_delegation_operation` |
| 38 | `committee_cancel_request_operation` |
| 39 | `committee_approve_request_operation` |
| 40 | `committee_payout_request_operation` |
| 41 | `committee_pay_request_operation` |
| 42 | `witness_reward_operation` |
| 48 | `receive_award_operation` |
| 49 | `benefactor_award_operation` |
| 52 | `paid_subscription_action_operation` |
| 53 | `cancel_paid_subscription_operation` |
| 57 | `account_sale_operation` |
| 59 | `expire_escrow_ratification_operation` |
| 62 | `bid_operation` |
| 63 | `outbid_operation` |

---

## 交易构建

已签名交易包含：

| 字段 | 值 |
|------|---|
| `ref_block_num` | `head_block_number & 0xFFFF` |
| `ref_block_prefix` | `block_id` 的第 4–7 字节（小端 `uint32`） |
| `expiration` | UTC 时间字符串；建议从广播时间起最多 ~60 秒 |
| `operations` | `[type_id, object]` 对的数组 |
| `extensions` | 始终为 `[]` |
| `signatures` | 紧凑十六进制编码的 ECDSA 签名数组 |

**签名：** `sha256(chain_id + serialized_transaction_body)` → secp256k1 上的紧凑 ECDSA 签名。

**私钥：** WIF 格式（base58check，版本字节 `0x80`）。

---

## 能量系统

能量由奖励类型操作使用。

- 以基点存储：0–10000（0%–100%）。
- 每 24 小时恢复 100%（`CHAIN_ENERGY_REGENERATION_SECONDS = 86400`）。
- 当前能量：`min(10000, last_energy + elapsed_seconds × 10000 / 86400)`。

---

参见：[操作概述](./operations/overview.md)、[虚拟操作](./virtual-operations.md)、[链属性](../governance/chain-properties.md)。
