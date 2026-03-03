# VIZ Blockchain — Escrow Operations

Spec for implementing escrow-related operations in PHP/Node.js libraries.

Escrow allows conditional transfers: funds are held in escrow until approved by both parties, or resolved by an agent in case of dispute.

---

## Escrow Flow

```
escrow_transfer  →  escrow_approve (by agent & to)
                 →  [escrow_dispute]  →  escrow_release (by agent)
                 →  escrow_release (by from or to)
                 (expire) → expire_escrow_ratification_operation [virtual]
```

---

## `escrow_transfer_operation`

**Type ID:** `15`
**Required authority:** `active` of `from`

Creates an escrow transfer proposal. Funds leave `from` into escrow balance. Both `agent` and `to` must approve before funds can be released.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `from` | `account_name_type` | yes | Sender |
| `to` | `account_name_type` | yes | Intended recipient |
| `agent` | `account_name_type` | yes | Escrow agent (arbitrator) |
| `escrow_id` | `uint32_t` | yes | Unique ID (chosen by sender), default 30 |
| `token_amount` | `asset` (VIZ) | yes | Amount held in escrow |
| `fee` | `asset` (VIZ) | yes | Agent fee (paid on approval) |
| `ratification_deadline` | `time_point_sec` | yes | Deadline for agent & to to approve |
| `escrow_expiration` | `time_point_sec` | yes | When escrow expires if not released |
| `json_metadata` | `string` | yes | Optional metadata / terms |

### JSON Example

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

### PHP Example

```php
$op = [
    'type' => 'escrow_transfer_operation',
    'value' => [
        'from'                   => 'alice',
        'to'                     => 'bob',
        'agent'                  => 'charlie',
        'escrow_id'              => 1001,
        'token_amount'           => '100.000 VIZ',
        'fee'                    => '1.000 VIZ',
        'ratification_deadline'  => '2024-06-01T00:00:00',
        'escrow_expiration'      => '2024-07-01T00:00:00',
        'json_metadata'          => json_encode(['description' => 'payment for work']),
    ],
];
```

### Node.js Example

```js
const op = ['escrow_transfer', {
    from: 'alice',
    to: 'bob',
    agent: 'charlie',
    escrow_id: 1001,
    token_amount: '100.000 VIZ',
    fee: '1.000 VIZ',
    ratification_deadline: '2024-06-01T00:00:00',
    escrow_expiration: '2024-07-01T00:00:00',
    json_metadata: JSON.stringify({ description: 'payment for work' }),
}];
```

### Checklist
- [ ] `token_amount.symbol` must be `VIZ`
- [ ] `fee.symbol` must be `VIZ`
- [ ] `token_amount.amount` must be > 0
- [ ] `ratification_deadline` must be before `escrow_expiration`
- [ ] Both deadlines must be in the future at time of broadcast
- [ ] `escrow_id` must be unique for the `from` account
- [ ] If not approved before `ratification_deadline`, virtual `expire_escrow_ratification_operation` fires and funds return
- [ ] Sign with `from`'s active key

---

## `escrow_approve_operation`

**Type ID:** `18`
**Required authority:** `active` of `who`

Approves (or rejects) an escrow transfer. Both `to` and `agent` must approve. Once approved, approval cannot be revoked.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `from` | `account_name_type` | yes | Original escrow sender |
| `to` | `account_name_type` | yes | Original escrow recipient |
| `agent` | `account_name_type` | yes | Escrow agent |
| `who` | `account_name_type` | yes | Who is approving (`to` or `agent`) |
| `escrow_id` | `uint32_t` | yes | Escrow ID |
| `approve` | `bool` | yes | `true` to approve, `false` to reject |

### JSON Example

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

### Checklist
- [ ] `who` must be either `to` or `agent`
- [ ] Once approved, cannot be undone
- [ ] If `approve: false` → escrow is cancelled, funds returned to `from`
- [ ] Escrow is only active once both `to` and `agent` have approved
- [ ] Must be done before `ratification_deadline`
- [ ] Sign with `who`'s active key

---

## `escrow_dispute_operation`

**Type ID:** `16`
**Required authority:** `active` of `who`

Raises a dispute on an approved escrow. Once disputed, only the `agent` can release funds.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `from` | `account_name_type` | yes | Original escrow sender |
| `to` | `account_name_type` | yes | Original escrow recipient |
| `agent` | `account_name_type` | yes | Escrow agent |
| `who` | `account_name_type` | yes | Who is raising the dispute (`from` or `to`) |
| `escrow_id` | `uint32_t` | yes | Escrow ID |

### JSON Example

```json
[16, {
  "from": "alice",
  "to": "bob",
  "agent": "charlie",
  "who": "alice",
  "escrow_id": 1001
}]
```

### Checklist
- [ ] Dispute can only be raised on an **approved** escrow (both parties approved)
- [ ] Dispute can be raised before or on the `escrow_expiration` deadline
- [ ] `who` must be `from` or `to`
- [ ] Once disputed, only `agent` can release via `escrow_release_operation`
- [ ] Sign with `who`'s active key

---

## `escrow_release_operation`

**Type ID:** `17`
**Required authority:** `active` of `who`

Releases escrow funds to `receiver`.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `from` | `account_name_type` | yes | Original escrow sender |
| `to` | `account_name_type` | yes | Original escrow recipient |
| `agent` | `account_name_type` | yes | Escrow agent |
| `who` | `account_name_type` | yes | Account releasing funds |
| `receiver` | `account_name_type` | yes | Account that receives the funds |
| `escrow_id` | `uint32_t` | yes | Escrow ID |
| `token_amount` | `asset` (VIZ) | yes | Amount to release |

### JSON Example

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

### Checklist
- [ ] `token_amount.symbol` must be `VIZ`
- [ ] Release permission rules:
  - No dispute, before expiration: `from` can release to `to`; `to` can release to `from`
  - No dispute, after expiration: either party can release to either party
  - Disputed: only `agent` can release to either party
- [ ] `receiver` must be `from` or `to`
- [ ] `token_amount` can be partial — remaining stays in escrow
- [ ] Sign with `who`'s active key
