# 账户恢复操作

恢复机制允许预先指定的受信账户（*恢复账户*）使用最近有效的主权限帮助恢复对被入侵账户的访问。

**恢复流程：**
```
request_account_recovery  →  recover_account  （24 小时内）
change_recovery_account   （30 天延迟后生效）
```

---

## `request_account_recovery_operation`（ID 12）

**授权：** `recovery_account` 的 `active`

发起账户恢复请求。恢复账户为被入侵账户提议新的主权限；账户所有者有 24 小时通过 `recover_account_operation` 确认。

| 字段 | 类型 | 描述 |
|------|------|------|
| `recovery_account` | `account_name_type` | 受信恢复账户 |
| `account_to_recover` | `account_name_type` | 要恢复的被入侵账户 |
| `new_master_authority` | `authority` | 确认后要分配的新主权限 |
| `extensions` | `extensions_type` | 始终为 `[]` |

```json
[12, {
  "recovery_account": "recover-service",
  "account_to_recover": "alice",
  "new_master_authority": {
    "weight_threshold": 1,
    "account_auths": [],
    "key_auths": [["VIZ5newkey...", 1]]
  },
  "extensions": []
}]
```

- 只有 `account_to_recover` 的指定恢复账户才能发送此操作。
- 每个账户只允许一个活跃恢复请求；再次发送会更新请求并重置 24 小时窗口。
- 取消：将 `new_master_authority.weight_threshold` 设置为 `0`。

---

## `recover_account_operation`（ID 13）

**授权：** 同时满足 `new_master_authority` **和** `recent_master_authority` 的签名

通过证明过去的所有权确认恢复。必须在恢复请求后 24 小时内广播。

| 字段 | 类型 | 描述 |
|------|------|------|
| `account_to_recover` | `account_name_type` | 正在恢复的账户 |
| `new_master_authority` | `authority` | 新的主权限（必须与恢复请求完全匹配） |
| `recent_master_authority` | `authority` | 过去 30 天内有效的主权限 |
| `extensions` | `extensions_type` | 始终为 `[]` |

```json
[13, {
  "account_to_recover": "alice",
  "new_master_authority": {
    "weight_threshold": 1,
    "account_auths": [],
    "key_auths": [["VIZ5newkey...", 1]]
  },
  "recent_master_authority": {
    "weight_threshold": 1,
    "account_auths": [],
    "key_auths": [["VIZ5oldkey...", 1]]
  },
  "extensions": []
}]
```

- 交易必须由同时满足新旧两种权限的密钥签名。
- `new_master_authority` 必须与待处理恢复请求中的完全一致。
- 恢复后，旧的主密钥失效。

---

## `change_recovery_account_operation`（ID 14）

**授权：** `account_to_recover` 的 `master`

更改恢复账户。更改在 **30 天延迟**后生效，以防止攻击者在活跃攻击期间替换恢复账户。

| 字段 | 类型 | 描述 |
|------|------|------|
| `account_to_recover` | `account_name_type` | 更改其恢复账户的账户 |
| `new_recovery_account` | `account_name_type` | 新的恢复账户名 |
| `extensions` | `extensions_type` | 始终为 `[]` |

```json
[14, {
  "account_to_recover": "alice",
  "new_recovery_account": "new-recovery-service",
  "extensions": []
}]
```

- `new_recovery_account` 必须是现有账户。
- 如果 `new_recovery_account` 为 `""`，得票最多的验证者成为恢复账户。

---

参见：[数据类型](../data-types.md)、[操作概述](./overview.md)、[账户](./accounts.md)。
