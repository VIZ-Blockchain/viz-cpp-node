# VIZ Blockchain — Committee Operations

Spec for implementing committee (worker proposal) operations in PHP/Node.js libraries.

The committee mechanism allows community governance: anyone can create worker requests for funding, and VIZ SHARES holders vote to approve or reject them.

---

## `committee_worker_create_request_operation`

**Type ID:** `35`
**Required authority:** `regular` of `creator`

Creates a new committee worker request (funding proposal).

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `creator` | `account_name_type` | yes | Account creating the request |
| `url` | `string` | yes | URL describing the work/proposal |
| `worker` | `account_name_type` | yes | Account that will receive the payout |
| `required_amount_min` | `asset` (VIZ) | yes | Minimum acceptable payout |
| `required_amount_max` | `asset` (VIZ) | yes | Maximum acceptable payout |
| `duration` | `uint32_t` | yes | Request duration in seconds |

### Constraints

| Parameter | Value | Description |
|---|---|---|
| `COMMITTEE_MIN_DURATION` | 5 days | Minimum duration |
| `COMMITTEE_MAX_DURATION` | 30 days | Maximum duration |
| `COMMITTEE_MAX_REQUIRED_AMOUNT` | chain configured | Max tokens per request |

### JSON Example

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

### PHP Example

```php
$op = [
    'type' => 'committee_worker_create_request_operation',
    'value' => [
        'creator'             => 'alice',
        'url'                 => 'https://alice.example.com/proposal',
        'worker'              => 'alice',
        'required_amount_min' => '100.000 VIZ',
        'required_amount_max' => '500.000 VIZ',
        'duration'            => 604800,
    ],
];
```

### Node.js Example

```js
const op = ['committee_worker_create_request', {
    creator: 'alice',
    url: 'https://alice.example.com/proposal',
    worker: 'alice',
    required_amount_min: '100.000 VIZ',
    required_amount_max: '500.000 VIZ',
    duration: 604800,
}];
```

### Checklist
- [ ] `url.size()` must be > 0 and < 256 characters
- [ ] `required_amount_min.symbol` must be `VIZ`
- [ ] `required_amount_max.symbol` must be `VIZ`
- [ ] `required_amount_min.amount` >= 0
- [ ] `required_amount_max.amount` > `required_amount_min.amount`
- [ ] `duration` in range `[COMMITTEE_MIN_DURATION, COMMITTEE_MAX_DURATION]`
- [ ] Fee (`committee_create_request_fee`) is charged to creator
- [ ] Sign with `creator`'s regular key

---

## `committee_worker_cancel_request_operation`

**Type ID:** `36`
**Required authority:** `regular` of `creator`

Cancels an existing committee worker request before it expires.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `creator` | `account_name_type` | yes | Creator of the request |
| `request_id` | `uint32_t` | yes | ID of the request to cancel |

### JSON Example

```json
[36, {
  "creator": "alice",
  "request_id": 42
}]
```

### PHP Example

```php
$op = [
    'type' => 'committee_worker_cancel_request_operation',
    'value' => [
        'creator'    => 'alice',
        'request_id' => 42,
    ],
];
```

### Node.js Example

```js
const op = ['committee_worker_cancel_request', {
    creator: 'alice',
    request_id: 42,
}];
```

### Checklist
- [ ] Only the `creator` of the request can cancel it
- [ ] `request_id` must refer to an existing active request
- [ ] Sign with `creator`'s regular key

---

## `committee_vote_request_operation`

**Type ID:** `37`
**Required authority:** `regular` of `voter`

Votes on a committee worker request. Positive = support, negative = oppose.

### Fields

| Field | Type | Required | Description |
|---|---|---|---|
| `voter` | `account_name_type` | yes | Account casting the vote |
| `request_id` | `uint32_t` | yes | ID of the request to vote on |
| `vote_percent` | `int16_t` | yes | Vote weight (-10000 to 10000) |

### JSON Example

```json
[37, {
  "voter": "bob",
  "request_id": 42,
  "vote_percent": 10000
}]
```

### PHP Example

```php
$op = [
    'type' => 'committee_vote_request_operation',
    'value' => [
        'voter'        => 'bob',
        'request_id'   => 42,
        'vote_percent' => 10000,
    ],
];
```

### Node.js Example

```js
const op = ['committee_vote_request', {
    voter: 'bob',
    request_id: 42,
    vote_percent: 10000,
}];
```

### Checklist
- [ ] `vote_percent` range: -10000 (strong oppose) to 10000 (strong support)
- [ ] `vote_percent == 0` removes vote
- [ ] Voting power weighted by voter's SHARES
- [ ] Request is approved when net vote percent >= `committee_request_approve_min_percent`
- [ ] Sign with `voter`'s regular key
