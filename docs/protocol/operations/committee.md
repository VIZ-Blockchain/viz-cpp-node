# Committee Operations

The committee (worker proposal) system lets community members request funding from the committee fund. SHARES holders vote to approve or reject requests; approved requests receive a payout from the fund.

---

## `committee_worker_create_request_operation` (ID 35)

**Auth:** `regular` of `creator`

Creates a new funding request. A `committee_create_request_fee` is charged to the creator on submission.

| Field | Type | Description |
|-------|------|-------------|
| `creator` | `account_name_type` | Account creating the request |
| `url` | `string` | URL describing the proposal (non-empty, max 255 bytes) |
| `worker` | `account_name_type` | Account that will receive the payout |
| `required_amount_min` | `asset` (VIZ) | Minimum acceptable payout |
| `required_amount_max` | `asset` (VIZ) | Maximum acceptable payout |
| `duration` | `uint32_t` | Request duration in seconds |

```json
[35, {
  "creator": "alice",
  "url": "https://alice.example.com/proposal",
  "worker": "alice",
  "required_amount_min": "100.000 VIZ",
  "required_amount_max": "500.000 VIZ",
  "duration": 604800
}]
```

**Constraints:**

| Parameter | Value |
|-----------|-------|
| Minimum duration | 5 days (432000 s) |
| Maximum duration | 30 days (2592000 s) |
| `required_amount_max` | Must be > `required_amount_min` |

- `required_amount_min` ≥ 0; `required_amount_max` > `required_amount_min`.
- `worker` may differ from `creator`.

---

## `committee_worker_cancel_request_operation` (ID 36)

**Auth:** `regular` of `creator`

Cancels an existing funding request before it expires.

| Field | Type | Description |
|-------|------|-------------|
| `creator` | `account_name_type` | Creator of the request |
| `request_id` | `uint32_t` | ID of the request to cancel |

```json
[36, {
  "creator": "alice",
  "request_id": 42
}]
```

- Only the `creator` of the request can cancel it.
- `request_id` must refer to an existing active request.

---

## `committee_vote_request_operation` (ID 37)

**Auth:** `regular` of `voter`

Votes on a funding request. Voting power is proportional to the voter's SHARES stake.

| Field | Type | Description |
|-------|------|-------------|
| `voter` | `account_name_type` | Account casting the vote |
| `request_id` | `uint32_t` | ID of the request |
| `vote_percent` | `int16_t` | Vote weight in basis points (−10000 to 10000) |

```json
[37, {
  "voter": "bob",
  "request_id": 42,
  "vote_percent": 10000
}]
```

- `vote_percent` > 0 → support; `vote_percent` < 0 → oppose; `vote_percent` = 0 → remove vote.
- A request is approved when weighted net vote percent ≥ `committee_request_approve_min_percent` chain property.

**Virtual operations triggered by committee lifecycle:**

| Virtual Op | Trigger |
|-----------|---------|
| `committee_cancel_request_operation` | Request expires without approval |
| `committee_approve_request_operation` | Request reaches approval threshold |
| `committee_payout_request_operation` | Payout is processed |
| `committee_pay_request_operation` | Worker receives payment |

---

See also: [Data Types](../data-types.md), [Operations Overview](./overview.md), [Virtual Operations](../virtual-operations.md), [Committee Governance](../../governance/committee.md).
