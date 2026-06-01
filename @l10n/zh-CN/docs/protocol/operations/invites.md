# 邀请操作

邀请允许现有 VIZ 用户引入新账户，而接收方无需拥有预先存在的账户。邀请是一次性使用的密钥对，用 VIZ 代币资助；私钥充当邀请秘密。

**邀请流程：**
```
create_invite  →  invite_registration_operation  （创建新账户）
               →  claim_invite_balance_operation  （将余额转入现有账户）
               →  use_invite_balance_operation    （替代性领取）
```

---

## `create_invite_operation`（ID 43）

**授权：** `creator` 的 `active`

通过生成密钥并将 VIZ 代币锁定在其中来创建邀请。

| 字段 | 类型 | 描述 |
|------|------|------|
| `creator` | `account_name_type` | 创建邀请的账户 |
| `balance` | `asset`（VIZ） | 锁定在邀请中的 VIZ |
| `invite_key` | `public_key_type` | 邀请密钥对的公钥 |

```json
[43, {
  "creator": "alice",
  "balance": "5.000 VIZ",
  "invite_key": "VIZ5invite..."
}]
```

- 生成一个随机的 secp256k1 密钥对。**公钥**放入 `invite_key`；**私钥**（WIF）成为要分享的邀请秘密。
- `balance` 必须 ≥ 链属性 `create_invite_min_balance`。

---

## `claim_invite_balance_operation`（ID 44）

**授权：** `initiator` 的 `active`

从邀请中领取 VIZ 余额，将其转给 `receiver`。邀请被使用，无法再次使用。

| 字段 | 类型 | 描述 |
|------|------|------|
| `initiator` | `account_name_type` | 领取邀请的现有账户 |
| `receiver` | `account_name_type` | 接收 VIZ 余额的账户 |
| `invite_secret` | `string` | 邀请的 WIF 私钥 |

```json
[44, {
  "initiator": "bob",
  "receiver": "bob",
  "invite_secret": "5Ky1MXn..."
}]
```

- `receiver` 可以与 `initiator` 不同 — 余额可以被重定向。
- `invite_secret` 是邀请密钥对的 WIF 编码私钥。

---

## `invite_registration_operation`（ID 45）

**授权：** `initiator` 的 `active`

使用邀请创建新的区块链账户。邀请余额被转换为 SHARES 并分配给新账户。

| 字段 | 类型 | 描述 |
|------|------|------|
| `initiator` | `account_name_type` | 触发注册的现有账户 |
| `new_account_name` | `account_name_type` | 新账户名 |
| `invite_secret` | `string` | 邀请的 WIF 私钥 |
| `new_account_key` | `public_key_type` | 新账户的主、活跃、常规和 memo 密钥 |

```json
[45, {
  "initiator": "bob",
  "new_account_name": "carol",
  "invite_secret": "5Ky1MXn...",
  "new_account_key": "VIZ5newacct..."
}]
```

- `new_account_key` 应用于所有四个权限槽（master、active、regular、memo）。
- 邀请余额被转换为 SHARES（而非流动 VIZ）分配给新账户。
- 使用后邀请被消耗。

---

## `use_invite_balance_operation`（ID 58）

**授权：** `initiator` 的 `active`

替代性邀请领取，可能将余额转换为 SHARES 给接收者而非流动 VIZ。

| 字段 | 类型 | 描述 |
|------|------|------|
| `initiator` | `account_name_type` | 使用邀请的账户 |
| `receiver` | `account_name_type` | 接收余额的现有账户 |
| `invite_secret` | `string` | 邀请的 WIF 私钥 |

```json
[58, {
  "initiator": "bob",
  "receiver": "bob",
  "invite_secret": "5Ky1MXn..."
}]
```

- `receiver` 必须是现有账户。
- 使用后邀请被消耗。

---

参见：[数据类型](../data-types.md)、[操作概述](./overview.md)、[账户](./accounts.md)。
