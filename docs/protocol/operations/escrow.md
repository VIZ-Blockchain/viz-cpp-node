# Escrow Operations

Escrow holds VIZ tokens in a conditional transfer: funds are released only after approval by both the recipient and a neutral agent, or arbitrated by the agent in case of dispute.

**Escrow flow:**
```
escrow_transfer  →  escrow_approve (by both "to" and "agent")
                 →  escrow_release (by "from" or "to")
                 →  [escrow_dispute]  →  escrow_release (by "agent" only)
                 ↓
           (ratification deadline missed)
                 →  expire_escrow_ratification_operation [virtual — funds return]
```

---

## `escrow_transfer_operation` (ID 15)

**Auth:** `active` of `from`

Creates an escrow. Funds leave `from` immediately into escrow balance; both `agent` and `to` must approve before release is possible.

| Field | Type | Description |
|-------|------|-------------|
| `from` | `account_name_type` | Sender |
| `to` | `account_name_type` | Intended recipient |
| `agent` | `account_name_type` | Neutral escrow agent (arbitrator) |
| `escrow_id` | `uint32_t` | Unique ID chosen by sender (default 30) |
| `token_amount` | `asset` (VIZ) | Amount to hold in escrow |
| `fee` | `asset` (VIZ) | Agent fee — paid to agent upon approval |
| `ratification_deadline` | `time_point_sec` | Deadline for both parties to approve |
| `escrow_expiration` | `time_point_sec` | Expiry if escrow is never released |
| `json_metadata` | `string` | Optional terms / metadata |

```json
[15, {
  "from": "alice",
  "to": "bob",
  "agent": "charlie",
  "escrow_id": 1001,
  "token_amount": "100.000 VIZ",
  "fee": "1.000 VIZ",
  "ratification_deadline": "2024-06-01T00:00:00",
  "escrow_expiration": "2024-07-01T00:00:00",
  "json_metadata": "{\"description\":\"payment for work\"}"
}]
```

- `ratification_deadline` must be before `escrow_expiration`.
- Both timestamps must be in the future at broadcast time.
- `escrow_id` must be unique per `from` account.
- If not approved before `ratification_deadline`, the virtual `expire_escrow_ratification_operation` fires and funds return to `from`.

---

## `escrow_approve_operation` (ID 18)

**Auth:** `active` of `who`

Approves or rejects the escrow. Both `to` and `agent` must approve for the escrow to become active.

| Field | Type | Description |
|-------|------|-------------|
| `from` | `account_name_type` | Original escrow sender |
| `to` | `account_name_type` | Original escrow recipient |
| `agent` | `account_name_type` | Escrow agent |
| `who` | `account_name_type` | Who is approving: must be `to` or `agent` |
| `escrow_id` | `uint32_t` | Escrow ID |
| `approve` | `bool` | `true` to approve, `false` to reject |

```json
[18, {
  "from": "alice",
  "to": "bob",
  "agent": "charlie",
  "who": "bob",
  "escrow_id": 1001,
  "approve": true
}]
```

- `who` must be either `to` or `agent`.
- Once approved, cannot be revoked.
- If `approve: false` — escrow is cancelled and funds return to `from`.
- Must be broadcast before `ratification_deadline`.

---

## `escrow_dispute_operation` (ID 16)

**Auth:** `active` of `who`

Raises a dispute on an approved escrow. After a dispute, only the `agent` can release funds.

| Field | Type | Description |
|-------|------|-------------|
| `from` | `account_name_type` | Original escrow sender |
| `to` | `account_name_type` | Original escrow recipient |
| `agent` | `account_name_type` | Escrow agent |
| `who` | `account_name_type` | Who is disputing: must be `from` or `to` |
| `escrow_id` | `uint32_t` | Escrow ID |

```json
[16, {
  "from": "alice",
  "to": "bob",
  "agent": "charlie",
  "who": "alice",
  "escrow_id": 1001
}]
```

- Can only be raised on an **approved** escrow (both `to` and `agent` have approved).
- Must be raised before `escrow_expiration`.

---

## `escrow_release_operation` (ID 17)

**Auth:** `active` of `who`

Releases escrow funds to `receiver`. Partial releases are allowed.

| Field | Type | Description |
|-------|------|-------------|
| `from` | `account_name_type` | Original escrow sender |
| `to` | `account_name_type` | Original escrow recipient |
| `agent` | `account_name_type` | Escrow agent |
| `who` | `account_name_type` | Account releasing the funds |
| `receiver` | `account_name_type` | Account receiving the funds (must be `from` or `to`) |
| `escrow_id` | `uint32_t` | Escrow ID |
| `token_amount` | `asset` (VIZ) | Amount to release (may be partial) |

```json
[17, {
  "from": "alice",
  "to": "bob",
  "agent": "charlie",
  "who": "alice",
  "receiver": "bob",
  "escrow_id": 1001,
  "token_amount": "100.000 VIZ"
}]
```

**Release permission rules:**

| State | Who can release | To whom |
|-------|----------------|---------|
| No dispute, before expiration | `from` or `to` | The other party |
| No dispute, after expiration | `from` or `to` | Either party |
| Disputed | `agent` only | Either party |

- Partial releases are allowed; the remainder stays in escrow.

---

See also: [Data Types](../data-types.md), [Operations Overview](./overview.md), [Virtual Operations](../virtual-operations.md).
