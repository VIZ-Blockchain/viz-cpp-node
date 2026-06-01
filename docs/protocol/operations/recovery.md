# Account Recovery Operations

The recovery mechanism allows a pre-designated trusted account (the *recovery account*) to help restore access to a compromised account using a recent valid master authority.

**Recovery flow:**
```
request_account_recovery  →  recover_account  (within 24 hours)
change_recovery_account   (30-day delay before taking effect)
```

---

## `request_account_recovery_operation` (ID 12)

**Auth:** `active` of `recovery_account`

Initiates an account recovery request. The recovery account proposes a new master authority for the compromised account; the account owner has 24 hours to confirm with `recover_account_operation`.

| Field | Type | Description |
|-------|------|-------------|
| `recovery_account` | `account_name_type` | The trusted recovery account |
| `account_to_recover` | `account_name_type` | Compromised account to recover |
| `new_master_authority` | `authority` | New master authority to assign after confirmation |
| `extensions` | `extensions_type` | Always `[]` |

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

- Only the designated recovery account of `account_to_recover` may send this.
- Only one active recovery request per account is permitted; sending again updates the request and resets the 24-hour window.
- To cancel: set `new_master_authority.weight_threshold` to `0`.

---

## `recover_account_operation` (ID 13)

**Auth:** Signatures satisfying **both** `new_master_authority` AND `recent_master_authority`

Confirms recovery by proving past ownership. Must be broadcast within 24 hours of the recovery request.

| Field | Type | Description |
|-------|------|-------------|
| `account_to_recover` | `account_name_type` | Account being recovered |
| `new_master_authority` | `authority` | New master authority (must exactly match the recovery request) |
| `recent_master_authority` | `authority` | A master authority that was valid within the past 30 days |
| `extensions` | `extensions_type` | Always `[]` |

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

- The transaction must be signed by keys satisfying **both** the new and recent authorities simultaneously.
- `new_master_authority` must exactly match the one in the pending recovery request.
- After recovery the old master key is invalidated.

---

## `change_recovery_account_operation` (ID 14)

**Auth:** `master` of `account_to_recover`

Changes the recovery account. The change takes effect after a **30-day delay** to prevent attackers from substituting the recovery account during an active attack.

| Field | Type | Description |
|-------|------|-------------|
| `account_to_recover` | `account_name_type` | Account changing its recovery account |
| `new_recovery_account` | `account_name_type` | New recovery account name |
| `extensions` | `extensions_type` | Always `[]` |

```json
[14, {
  "account_to_recover": "alice",
  "new_recovery_account": "new-recovery-service",
  "extensions": []
}]
```

- `new_recovery_account` must be an existing account.
- If `new_recovery_account` is `""`, the top-voted validator becomes the recovery account.

---

See also: [Data Types](../data-types.md), [Operations Overview](./overview.md), [Accounts](./accounts.md).
