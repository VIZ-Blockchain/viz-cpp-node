# Invite Operations

Invites allow existing VIZ users to onboard new accounts without the recipient needing a prior account. An invite is a one-time-use keypair funded with VIZ tokens; the private key serves as the invite secret.

**Invite flow:**
```
create_invite  →  invite_registration_operation  (create new account)
               →  claim_invite_balance_operation  (credit balance to existing account)
               →  use_invite_balance_operation    (alternative claim)
```

---

## `create_invite_operation` (ID 43)

**Auth:** `active` of `creator`

Creates an invite by generating a key and locking VIZ tokens in it.

| Field | Type | Description |
|-------|------|-------------|
| `creator` | `account_name_type` | Account creating the invite |
| `balance` | `asset` (VIZ) | VIZ to lock in the invite |
| `invite_key` | `public_key_type` | Public key of the invite keypair |

```json
[43, {
  "creator": "alice",
  "balance": "5.000 VIZ",
  "invite_key": "VIZ5invite..."
}]
```

- Generate a random secp256k1 keypair. The **public key** goes into `invite_key`; the **private key** (WIF) becomes the invite secret to share.
- `balance` must be ≥ `create_invite_min_balance` chain property.

---

## `claim_invite_balance_operation` (ID 44)

**Auth:** `active` of `initiator`

Claims the VIZ balance from an invite, transferring it to `receiver`. The invite is consumed and cannot be reused.

| Field | Type | Description |
|-------|------|-------------|
| `initiator` | `account_name_type` | Existing account claiming the invite |
| `receiver` | `account_name_type` | Account to receive the VIZ balance |
| `invite_secret` | `string` | WIF private key of the invite |

```json
[44, {
  "initiator": "bob",
  "receiver": "bob",
  "invite_secret": "5Ky1MXn..."
}]
```

- `receiver` may differ from `initiator` — the balance can be redirected.
- `invite_secret` is the WIF-encoded private key of the invite keypair.

---

## `invite_registration_operation` (ID 45)

**Auth:** `active` of `initiator`

Uses an invite to create a new blockchain account. The invite balance is converted to SHARES and assigned to the new account.

| Field | Type | Description |
|-------|------|-------------|
| `initiator` | `account_name_type` | Existing account triggering registration |
| `new_account_name` | `account_name_type` | Name for the new account |
| `invite_secret` | `string` | WIF private key of the invite |
| `new_account_key` | `public_key_type` | Key set as master, active, regular, and memo for the new account |

```json
[45, {
  "initiator": "bob",
  "new_account_name": "carol",
  "invite_secret": "5Ky1MXn...",
  "new_account_key": "VIZ5newacct..."
}]
```

- `new_account_key` is applied to all four authority slots (master, active, regular, memo).
- The invite balance is converted to SHARES (not liquid VIZ) for the new account.
- Invite is consumed after use.

---

## `use_invite_balance_operation` (ID 58)

**Auth:** `active` of `initiator`

Alternative invite claim that may convert the balance to SHARES for the receiver rather than liquid VIZ.

| Field | Type | Description |
|-------|------|-------------|
| `initiator` | `account_name_type` | Account using the invite |
| `receiver` | `account_name_type` | Existing account receiving the balance |
| `invite_secret` | `string` | WIF private key of the invite |

```json
[58, {
  "initiator": "bob",
  "receiver": "bob",
  "invite_secret": "5Ky1MXn..."
}]
```

- `receiver` must be an existing account.
- Invite is consumed after use.

---

See also: [Data Types](../data-types.md), [Operations Overview](./overview.md), [Accounts](./accounts.md).
