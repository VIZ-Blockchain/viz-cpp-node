# Account Operations

---

## `account_create_operation` (ID 20)

**Auth:** `active` of `creator`

Creates a new blockchain account. The fee is converted to SHARES for the new account.

| Field | Type | Description |
|-------|------|-------------|
| `fee` | `asset` (VIZ) | Creation fee ≥ chain `account_creation_fee` |
| `delegation` | `asset` (SHARES) | Initial SHARES delegation to new account |
| `creator` | `account_name_type` | Account paying the fee |
| `new_account_name` | `account_name_type` | Name for the new account |
| `master` | `authority` | Master authority |
| `active` | `authority` | Active authority |
| `regular` | `authority` | Regular authority |
| `memo_key` | `public_key_type` | Memo public key |
| `json_metadata` | `string` | JSON metadata (may be `""`) |
| `referrer` | `account_name_type` | Referrer account (may be `""`) |
| `extensions` | `extensions_type` | Always `[]` |

```json
[20, {
  "fee": "1.000 VIZ",
  "delegation": "10.000000 SHARES",
  "creator": "alice",
  "new_account_name": "bob",
  "master":  { "weight_threshold": 1, "account_auths": [], "key_auths": [["VIZ5...", 1]] },
  "active":  { "weight_threshold": 1, "account_auths": [], "key_auths": [["VIZ5...", 1]] },
  "regular": { "weight_threshold": 1, "account_auths": [], "key_auths": [["VIZ5...", 1]] },
  "memo_key": "VIZ5...",
  "json_metadata": "",
  "referrer": "",
  "extensions": []
}]
```

- All three authorities are required (even if identical keys are used).
- `fee.symbol` must be `VIZ`; `delegation.symbol` must be `SHARES`.

---

## `account_update_operation` (ID 5)

**Auth:** `master` of `account` (if `master` field present), otherwise `active`

Updates an account's keys and metadata.

| Field | Type | Description |
|-------|------|-------------|
| `account` | `account_name_type` | Account to update |
| `master` | `optional<authority>` | New master authority (omit if not changing) |
| `active` | `optional<authority>` | New active authority |
| `regular` | `optional<authority>` | New regular authority |
| `memo_key` | `public_key_type` | New memo key (required even if unchanged) |
| `json_metadata` | `string` | New JSON metadata |

```json
[5, {
  "account": "alice",
  "active": { "weight_threshold": 1, "account_auths": [], "key_auths": [["VIZ5new...", 1]] },
  "memo_key": "VIZ5new...",
  "json_metadata": "{\"profile\":\"updated\"}"
}]
```

- If `master` is present → sign with current **master** key.
- If `master` is absent → sign with current **active** key.
- `memo_key` is always required.

---

## `account_metadata_operation` (ID 21)

**Auth:** `regular` of `account`

Updates only the account's JSON metadata. Lower bandwidth cost than `account_update`.

| Field | Type | Description |
|-------|------|-------------|
| `account` | `account_name_type` | Account to update |
| `json_metadata` | `string` | New JSON metadata string |

```json
[21, {
  "account": "alice",
  "json_metadata": "{\"name\":\"Alice\",\"about\":\"Hello!\"}"
}]
```

---

See also: [Data Types](../data-types.md), [Operations Overview](./overview.md).
