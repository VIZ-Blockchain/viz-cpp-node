# Committee DAO System and Prediction Market Dispute Resolution

## Part 1: How the VIZ Committee System Works

### Overview

The VIZ committee is a **decentralized autonomous governance** (DAO) mechanism built directly into the blockchain protocol. It enables community-funded worker proposals: any account can create a funding request, and all VIZ SHARES holders vote to approve or reject it using **stake-weighted bipolar voting** (positive and negative).

The committee fund accumulates from request creation fees. Approved requests are paid out proportionally from this fund.

### Core Operations

| Operation | Description |
|---|---|
| `committee_worker_create_request` | Create a funding proposal with URL, min/max amounts, duration (5–30 days) |
| `committee_worker_cancel_request` | Creator cancels their own active proposal |
| `committee_vote_request` | Any account votes on an active proposal (−10000 to +10000) |

### Voting Mechanics

Every VIZ account can vote on any active committee request. The vote is expressed as `vote_percent` in range **−10000 to +10000** (basis points, i.e. −100% to +100%).

- **Positive vote** (+1 to +10000): supports the request
- **Negative vote** (−10000 to −1): opposes the request
- **Vote weight** is proportional to the voter's `effective_vesting_shares` (staked VIZ)

Each voter can change their vote at any time while the request is active. Only one vote per account per request is stored; updating replaces the previous vote.

### Stake-Weighted Calculation

When the request's `end_time` is reached, the blockchain evaluates votes:

```
max_rshares  = SUM( voter.effective_vesting_shares )              // for all voters
actual_rshares = SUM( voter.effective_vesting_shares * vote_percent / 10000 )  // signed sum
```

**Three thresholds must be met for approval:**

1. **Participation threshold**: `max_rshares >= total_vesting_shares * committee_request_approve_min_percent / 10000`
   - If insufficient participation → status = 2 (rejected, not enough voters)

2. **Consensus threshold**: `actual_rshares > 0`
   - If net negative → status = 3 (rejected, community says no)

3. **Minimum payout**: `calculated_payment >= required_amount_min`
   - Payout formula: `calculated_payment = required_amount_max * (actual_rshares / max_rshares)`
   - If payout too low → status = 3 (rejected, below minimum)
   - If all pass → status = 4 (approved)

### Payout Processing

Approved requests (status=4) receive payouts from `committee_fund` every 10 minutes (200 blocks). The fund is split equally across all approved requests. Each cycle:

```
max_payment_per_request = committee_fund / count_of_approved_requests
current_payment = min(max_payment_per_request, remain_payout_amount)
```

When `remain_payout_amount` reaches 0, status becomes 5 (completed).

### Request Lifecycle

```
[Created, status=0] → voting period (5–30 days)
    → Insufficient participation    → [Rejected, status=2]
    → Net negative votes            → [Expired, status=3]
    → Payout below minimum          → [Expired, status=3]
    → Creator cancels               → [Canceled, status=1]
    → Approved                      → [Approved, status=4] → payouts → [Completed, status=5]
```

### Key Properties

1. **Bipolar**: every voter can express support OR opposition with fine-grained intensity
2. **Stake-weighted**: votes are weighted by locked tokens, making Sybil attacks expensive
3. **Proportional payout**: stronger consensus → higher payout (up to `required_amount_max`)
4. **Self-funding**: creation fees flow into the committee fund
5. **Non-custodial**: no trusted intermediary; the protocol enforces all rules

---

## Part 2: Applying the Committee Model to Prediction Markets

### Why Committee Voting is Useful for Dispute Resolution

The committee system solves a fundamental governance problem: **how to make a collective, weighted decision where the outcome is proportional to conviction**. This is exactly what prediction markets need when:

- An oracle reports a result that participants dispute
- A market creator sets up an unfair or fraudulent market
- The correct outcome is ambiguous and requires human judgment

The committee model is useful for dispute resolution because:

1. **It's already battle-tested** on the VIZ blockchain for DAO funding
2. **Negative votes matter** — they aren't just "abstain", they actively oppose, creating a true signal
3. **Weighted by stake** — actors with more skin in the game have proportionally more say
4. **Proportional outcomes** — the result isn't binary "yes/no" but a gradient, which maps well to penalties/rewards
5. **Participation threshold** — prevents tiny minorities from dictating outcomes

### Prediction Market Dispute Resolution: Implementation Proposal

#### Market Structure

A prediction market has:
- **Market creator**: account that defines the event and outcomes
- **Oracle**: account responsible for reporting the correct outcome
- **Outcomes**: N possible results (e.g., "Team A wins", "Team B wins", "Draw")
- **Oracle insurance fund**: a deposit the oracle stakes to guarantee honest reporting
- **Participants**: accounts that buy outcome shares

#### Dispute Flow

```
[Oracle reports outcome] → [Challenge period starts]
    → No disputes filed                → [Outcome finalized]
    → Dispute filed (fee required)     → [Committee vote opens]
        → Vote concludes              → [Resolution applied]
```

#### New Operations (Conceptual)

##### `prediction_market_create_operation`

```
creator:             account_name_type    // who creates the market
url:                 string               // market description
outcomes:            vector<string>       // e.g. ["Team A", "Team B", "Draw"]
oracle:              account_name_type    // designated result reporter
oracle_insurance:    asset                // oracle's deposit (staked as guarantee)
resolution_duration: uint32_t            // voting period if disputed (seconds)
penalty_percent:     int16_t             // −10000 to +10000 (see below)
oracle_ban_type:     uint8_t             // 0=no ban, 1=temporary, 2=permanent
creator_ban_type:    uint8_t             // 0=no ban, 1=temporary, 2=permanent
ban_duration:        uint32_t            // seconds (if temporary)
```

**About `penalty_percent`:**

| Value | Meaning |
|---|---|
| +5000 (+50%) | If dispute succeeds, oracle loses 50% of insurance fund |
| +10000 (+100%) | Oracle loses entire insurance fund |
| 0 | No penalty (dispute just corrects the outcome) |
| −5000 (−50%) | Oracle is actually **rewarded** 50% bonus from dispute fee pool — use this for markets where oracles face genuine ambiguity and shouldn't be punished for honest mistakes |

A negative `penalty_percent` means: "we understand this market is hard to judge, so we don't penalize the oracle even if the community overrides the result." This is critical for markets involving subjective outcomes (e.g., "Was the product delivered satisfactorily?").

##### `prediction_dispute_operation`

```
disputer:       account_name_type
market_id:      uint32_t
proposed_outcome: uint16_t            // which outcome the disputer believes is correct (index into outcomes[])
```

Filing a dispute requires a fee (similar to committee request creation fee) to prevent spam. This opens a **committee-style voting period**.

##### `prediction_dispute_vote_operation`

```
voter:          account_name_type
market_id:      uint32_t
vote_outcome:   uint16_t             // which outcome this voter believes is correct (1 of N)
vote_percent:   int16_t              // −10000 to +10000 (conviction strength)
```

**Key difference from committee voting**: here the voter selects **one correct outcome** from multiple options AND expresses conviction strength.

- `vote_outcome` = the outcome index the voter believes is correct
- `vote_percent` = how confident they are (positive = "I'm sure this is right", negative = "I'm sure the oracle was actually correct, reject the dispute")

#### Resolution Algorithm (Weighted Decision)

When the dispute voting period ends:

```cpp
// Step 1: Calculate stake-weighted votes per outcome
for each vote in dispute_votes:
    voter_weight = voter.effective_vesting_shares
    if vote_percent > 0:
        // Voter supports changing the outcome
        outcome_rshares[vote_outcome] += voter_weight * vote_percent / 10000
        total_change_rshares += voter_weight * vote_percent / 10000
    else:
        // Voter opposes the dispute (supports oracle's original result)
        oracle_defense_rshares += voter_weight * abs(vote_percent) / 10000
    max_rshares += voter_weight

// Step 2: Participation check
approve_min_shares = total_vesting_shares * dispute_approve_min_percent / 10000
if max_rshares < approve_min_shares:
    // Not enough participation — oracle's result stands
    finalize_with_oracle_result()
    return

// Step 3: Compare oracle defense vs total change votes
if oracle_defense_rshares >= total_change_rshares:
    // Community supports oracle — dispute rejected
    finalize_with_oracle_result()
    // Dispute fee goes to oracle as compensation
    return

// Step 4: Find winning outcome among change votes
winning_outcome = argmax(outcome_rshares)
winning_rshares = outcome_rshares[winning_outcome]

// Step 5: Calculate penalty proportional to conviction
//   consensus_strength = winning_rshares / max_rshares (0.0 to 1.0)
//   This makes the penalty proportional to how strongly the community disagrees
consensus_strength = winning_rshares * CHAIN_100_PERCENT / max_rshares

if penalty_percent > 0:
    actual_penalty = oracle_insurance * penalty_percent / 10000
    // Scale by consensus strength — weak consensus = smaller penalty
    actual_penalty = actual_penalty * consensus_strength / CHAIN_100_PERCENT
    // Deduct from oracle insurance, distribute to dispute participants
    oracle_insurance -= actual_penalty
elif penalty_percent < 0:
    // Negative penalty = oracle gets rewarded even when overridden
    oracle_bonus = dispute_fee_pool * abs(penalty_percent) / 10000
    oracle_balance += oracle_bonus

// Step 6: Apply bans based on consensus strength
if oracle_ban_type == 1 and consensus_strength > BAN_THRESHOLD:
    // Temporary ban — duration scaled by consensus strength
    oracle.banned_until = now + ban_duration * consensus_strength / CHAIN_100_PERCENT
elif oracle_ban_type == 2 and consensus_strength > PERMANENT_BAN_THRESHOLD:
    oracle.permanently_banned = true

// Same logic for creator bans (for fraudulent market setup)
if creator_ban_type == 1 and consensus_strength > BAN_THRESHOLD:
    creator.banned_until = now + ban_duration * consensus_strength / CHAIN_100_PERCENT
elif creator_ban_type == 2 and consensus_strength > PERMANENT_BAN_THRESHOLD:
    creator.permanently_banned = true

// Step 7: Override result
finalize_with_outcome(winning_outcome)
```

#### Making the Decision Weighted

The key insight from the committee system is that **all decisions should be proportional, not binary**. Here's how each aspect is weighted:

##### 1. Outcome Selection is Weighted

Unlike a simple majority vote, the winning outcome must accumulate more stake-weighted support than ALL other options combined (including oracle defense). This prevents a small but passionate minority from overriding the oracle.

##### 2. Penalty is Weighted by Consensus Strength

If `penalty_percent = +10000` (100%) but the community only barely overrides the oracle (51% vs 49%), the actual penalty applied is:

```
actual_penalty = insurance * 10000/10000 * 5100/10000 = insurance * 51%
```

Strong consensus (90% agreement) → nearly full penalty. Weak consensus → mild penalty. This is fair because a close call suggests the oracle's mistake was understandable.

##### 3. Ban Duration is Weighted

A temporary ban isn't fixed-length — it scales with `consensus_strength`:

```
effective_ban = ban_duration * consensus_strength / 10000
```

If `ban_duration = 30 days` and consensus is 70%, the ban is 21 days.

##### 4. Negative Penalty Protects Good-Faith Oracles

Setting `penalty_percent = -5000` means: "even if the community overrides this oracle, give the oracle a bonus from the dispute fees." This is useful for:
- Subjective markets ("Best movie of 2025")
- Markets where the ground truth is genuinely ambiguous
- Encouraging oracles to participate in difficult markets

##### 5. Creator Accountability

The `creator_ban_type` field allows the community to also penalize market creators who set up misleading or fraudulent markets. The same weighted logic applies — a temporary ban scaled by consensus strength, or a permanent ban only with overwhelming agreement.

### Comparison: Committee DAO vs Prediction Market Disputes

| Aspect | Committee DAO | Prediction Market Dispute |
|---|---|---|
| **What is voted on** | Whether to fund a worker | Which outcome is correct |
| **Vote type** | Single scalar (−100% to +100%) | Outcome choice + conviction (−100% to +100%) |
| **Positive vote** | "Fund this worker" | "This outcome is correct" |
| **Negative vote** | "Don't fund" | "Oracle was right, reject dispute" |
| **Payout calculation** | `max * (actual_rshares / max_rshares)` | Penalty: `insurance * penalty% * consensus_strength` |
| **Participation threshold** | `approve_min_percent` of total vesting | Same mechanism |
| **Result** | Proportional payout from committee fund | Outcome correction + proportional penalty |
| **Self-funding** | Creation fees → committee fund | Dispute fees → resolution pool |

### Summary

The VIZ committee system provides a proven, battle-tested model for **stake-weighted collective decision-making** with bipolar voting. Applying the same principles to prediction market dispute resolution gives us:

1. **Fair outcome selection**: 1 correct result chosen from N options, weighted by stake
2. **Proportional penalties**: oracle insurance fund penalty scales with community conviction
3. **Flexible punishment**: negative penalty percent to protect good-faith oracles in ambiguous markets
4. **Graduated bans**: temporary or permanent, with duration proportional to consensus strength
5. **Creator accountability**: same weighted mechanism applies to market creators
6. **Sybil resistance**: all votes weighted by staked VIZ, making manipulation expensive

The fundamental advantage of this approach over binary "guilty/not guilty" arbitration systems is that **every parameter of the resolution is a gradient**, not a switch. This produces fairer outcomes in a decentralized setting where absolute truth is often elusive.
