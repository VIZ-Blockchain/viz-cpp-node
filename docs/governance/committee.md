# Committee DAO

The committee is VIZ Ledger's decentralized funding mechanism. Any account can create a worker request (funding proposal); all SHARES holders vote to approve or reject it using stake-weighted bipolar voting.

The committee fund accumulates from request creation fees and from the portion of block inflation allocated to it (governed by the `inflation_ratio_committee_vs_reward_fund` chain property).

---

## Operations

| Operation | ID | Auth | Description |
|-----------|----|------|-------------|
| `committee_worker_create_request_operation` | 35 | regular of `creator` | Create a funding request |
| `committee_worker_cancel_request_operation` | 36 | regular of `creator` | Cancel own active request |
| `committee_vote_request_operation` | 37 | regular of `voter` | Vote on a request |

See [Committee Operations](../protocol/operations/committee.md) for full field details.

---

## Voting Mechanics

Each SHARES holder can vote with a `vote_percent` in the range **−10000 to +10000** (basis points):

- Positive vote: supports the request.
- Negative vote: opposes the request.
- Zero: removes the vote.

**Vote weight** = `effective_vesting_shares × vote_percent / 10000`.

Votes can be changed at any time while the request is active.

---

## Approval Calculation

When the request's `end_time` is reached:

```
max_rshares    = SUM(voter.effective_vesting_shares for all voters)
actual_rshares = SUM(voter.effective_vesting_shares × vote_percent / 10000)
```

**Three conditions for approval:**

1. **Participation:** `max_rshares ≥ total_vesting_shares × committee_request_approve_min_percent / 10000`  
   (if not met → rejected, insufficient participation)

2. **Consensus:** `actual_rshares > 0`  
   (if negative → rejected, community opposed)

3. **Minimum payout:**  
   ```
   payout = required_amount_max × (actual_rshares / max_rshares)
   ```
   (if `payout < required_amount_min` → rejected)

If all conditions pass → **approved**, payout = calculated value.

---

## Request Lifecycle

```
[Created, status=0]
    └── voting period (5–30 days)
            ├── Insufficient participation → [Rejected, status=2]
            ├── Net negative / below min   → [Expired, status=3]
            ├── Creator cancels            → [Canceled, status=1]
            └── Approved                  → [Approved, status=4]
                                                └── payouts (every 200 blocks)
                                                    └── [Completed, status=5]
```

---

## Payout Processing

Approved requests (status 4) receive payouts from the committee fund every 200 blocks (~10 minutes). The fund is split equally across all currently approved requests:

```
payment = min(committee_fund / count_approved_requests, remain_payout_amount)
```

Virtual operations fired during lifecycle:

| Virtual Op | Trigger |
|-----------|---------|
| `committee_cancel_request_operation` (ID 38) | Request expires without approval |
| `committee_approve_request_operation` (ID 39) | Approval threshold reached |
| `committee_payout_request_operation` (ID 40) | Payout processed |
| `committee_pay_request_operation` (ID 41) | Worker receives payment |

---

## Querying Committee State

```json
{ "method": "committee_api.get_committee_request", "params": [42] }
{ "method": "committee_api.get_committee_request_votes", "params": [42] }
{ "method": "committee_api.get_committee_requests_list", "params": [0, 100] }
```

---

## Key Properties

- **Bipolar**: every voter expresses support or opposition with fine-grained intensity.
- **Stake-weighted**: votes are weighted by locked tokens, making manipulation expensive.
- **Proportional payout**: stronger consensus → higher payout (up to `required_amount_max`).
- **Self-funding**: creation fees flow into the committee fund.
- **Non-custodial**: no trusted intermediary; the protocol enforces all rules automatically.

---

See also: [Committee Operations](../protocol/operations/committee.md), [Staking and DAO](./staking-and-dao.md), [Chain Properties](./chain-properties.md), [Virtual Operations](../protocol/virtual-operations.md).
