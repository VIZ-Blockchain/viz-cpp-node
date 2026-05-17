# VIZ Staking (Vesting Shares) and DAO Self-Governance

## What is Staking in VIZ

In VIZ, **staking** means converting liquid VIZ tokens into **SHARES** (vesting shares). Staked tokens are locked and cannot be transferred directly, but they grant the holder **governance power** — the ability to vote, award, delegate, and influence every aspect of the blockchain.

Every VIZ account has three vesting fields:

| Field | Meaning |
|---|---|
| `vesting_shares` | SHARES owned by the account |
| `delegated_vesting_shares` | SHARES delegated to other accounts (reduces your power) |
| `received_vesting_shares` | SHARES received via delegation from others (increases your power) |

**Effective vesting shares** — the actual governance power used everywhere:

```
effective_vesting_shares = vesting_shares − delegated_vesting_shares + received_vesting_shares
```

This is the number that determines your weight in every vote, award, and governance action.

---

## Staking Operations

### Stake: `transfer_to_vesting_operation`

Converts liquid VIZ to SHARES. Can stake to yourself or to another account.

```json
["transfer_to_vesting", {
    "from": "alice",
    "to": "bob",
    "amount": "1000.000 VIZ"
}]
```

### Unstake: `withdraw_vesting_operation`

Initiates gradual withdrawal of SHARES back to liquid VIZ. The withdrawal happens over **28 daily intervals** (28 days total), where each day `vesting_shares / 28` is converted to liquid tokens.

```json
["withdraw_vesting", {
    "account": "alice",
    "vesting_shares": "1000.000000 SHARES"
}]
```

Setting `vesting_shares` to 0 cancels the withdrawal.

### Withdrawal Routes: `set_withdraw_vesting_route_operation`

Directs withdrawn SHARES to another account, optionally re-staking them automatically (`auto_vest = true`). Up to 10 routes per account.

```json
["set_withdraw_vesting_route", {
    "from_account": "alice",
    "to_account": "bob",
    "percent": 5000,
    "auto_vest": true
}]
```

### Delegate: `delegate_vesting_shares_operation`

Transfers voting power (not ownership) to another account. The delegator keeps ownership, the delegatee gets governance power.

```json
["delegate_vesting_shares", {
    "delegator": "alice",
    "delegatee": "bob",
    "vesting_shares": "500.000000 SHARES"
}]
```

When removing delegation, the SHARES enter a 5-day expiration period (matching energy regeneration time) to prevent "double-spend" of voting energy.

---

## Where Staked VIZ (SHARES) Are Used

SHARES are the universal governance token. Every meaningful action in VIZ is weighted by `effective_vesting_shares`:

### 1. Witness Voting

Witnesses produce blocks and set chain parameters. Every account can vote for up to 100 witnesses.

```json
["account_witness_vote", {
    "account": "alice",
    "witness": "node1",
    "approve": true
}]
```

**Fair-DPOS weighting**: vote weight is divided equally across all witnesses the account votes for:

```
fair_weight = (vesting_shares + proxied_votes) / witnesses_voted_for
```

This prevents concentration of power — if you vote for 10 witnesses, each gets 1/10 of your stake weight, not the full amount.

Accounts can also set a **proxy** (`account_witness_proxy_operation`), delegating their witness voting to another account.

### 2. Committee DAO Voting

Any account can create a **worker request** (funding proposal) and all SHARES holders vote to approve or reject it:

```json
["committee_vote_request", {
    "voter": "alice",
    "request_id": 42,
    "vote_percent": 7500
}]
```

- `vote_percent` ranges from **−10000** (strong oppose) to **+10000** (strong support)
- Weight: `effective_vesting_shares × vote_percent / 10000`
- The payout amount is proportional to net positive consensus
- Requires minimum participation threshold (% of total staked supply)

See [committee-dao-and-prediction-markets.md](committee-dao-and-prediction-markets.md) for full details.

### 3. Awards (Social Capital Distribution)

The **award operation** is VIZ's unique mechanism for distributing rewards from the shared reward fund. Any account can award any other account, spending regenerating **energy**:

```json
["award", {
    "initiator": "alice",
    "receiver": "bob",
    "energy": 1000,
    "custom_sequence": 0,
    "memo": "Great work!",
    "beneficiaries": []
}]
```

**How energy works:**
- Every account has energy in range 0–10000 (0%–100%)
- Energy regenerates linearly over 5 days (`CHAIN_ENERGY_REGENERATION_SECONDS = 432000`)
- Each award consumes the specified energy amount

**How SHARES determine reward size:**

```
rshares = effective_vesting_shares × used_energy / 10000
```

The `rshares` value determines how much of the global reward fund is claimed. An account with 10× more SHARES creates 10× more reward for the same energy expenditure. Rewards are distributed as SHARES to the receiver (and beneficiaries).

A `fixed_award_operation` variant lets you specify an exact reward amount — the system calculates the required energy.

### 4. Chain Parameter Governance

Witnesses publish their preferred chain parameters, and the **median** value across all active witnesses becomes the consensus setting. Since witnesses are elected by stake-weighted voting, the chain parameters are indirectly controlled by all SHARES holders.

Parameters governed this way include:
- `account_creation_fee` — cost to create new accounts
- `maximum_block_size` — throughput limit
- `min_delegation` — minimum delegation amount
- `committee_request_approve_min_percent` — participation threshold for DAO proposals
- `vote_accounting_min_rshares` — minimum award impact threshold
- `bandwidth_reserve_percent` — network bandwidth allocation
- `min_curation_percent`, `max_curation_percent` — content reward distribution
- `withdraw_intervals` — vesting withdrawal period
- And others

### 5. Bandwidth Allocation

Network bandwidth (transaction throughput) is allocated proportionally to `effective_vesting_shares`. Accounts with more stake can send more transactions per unit of time. Accounts below 500 SHARES get an additional 10% bandwidth reserve.

### 6. Account Creation via Delegation

New accounts can be created by delegating SHARES to them (at 10× ratio for 30 days), making account creation accessible without spending liquid tokens.

---

## VIZ as a DAO: Every Member Controls Their Share

### The DAO Analogy

VIZ is not just a blockchain — it is a **Decentralized Autonomous Organization** where every SHARES holder is a member with governance rights proportional to their stake. Think of it this way:

| Traditional DAO | VIZ Blockchain |
|---|---|
| DAO treasury | Committee fund + reward fund |
| DAO shares / governance tokens | SHARES (vesting shares) |
| Proposal voting | Committee worker requests |
| Board of directors | Elected witnesses |
| Director elections | Witness voting (Fair-DPOS) |
| Dividend distribution | Award mechanism (reward fund) |
| Bylaws / parameters | Chain properties (median governance) |
| Delegation of voting rights | `delegate_vesting_shares` + witness proxy |

### How Each Member "Controls Their Share of the DAO"

#### 1. Direct Financial Governance

Every SHARES holder can vote on **how the DAO treasury (committee fund) is spent**. Worker proposals request funding for development, marketing, infrastructure, or any community purpose. Your vote weight is your exact share of the total staked supply.

If you hold 1% of all SHARES, your vote carries exactly 1% weight in every committee decision. You can vote **for** (up to +100%) or **against** (down to −100%) with fine-grained intensity. You don't just approve or reject — you express **how strongly** you feel.

#### 2. Electing Leadership

Witnesses are the "board of directors" — they produce blocks, validate transactions, and **set chain parameters** through median voting. Every SHARES holder votes for witnesses, and Fair-DPOS ensures your voting power is split equally across your chosen witnesses, preventing vote concentration.

If you don't want to vote directly, you can **set a proxy** — delegate your witness voting to someone you trust, just like a proxy vote in a shareholder meeting.

#### 3. Direct Value Distribution

The **award mechanism** lets every member distribute value from the shared reward fund to any other member. This is unique to VIZ — there's no proposal process needed, no minimum threshold. If you hold SHARES and have energy, you can reward anyone, anytime.

The reward is proportional to your stake: more SHARES = more reward per energy unit. This is like having a share of the DAO's dividend pool that you can direct to anyone you choose.

#### 4. Power Delegation

Through `delegate_vesting_shares`, you can lend your governance power to another account without transferring ownership. This enables:

- **Empowering new members**: delegate SHARES to newcomers so they can participate in governance
- **Specialization**: delegate to accounts that focus on specific governance tasks
- **Service providers**: delegate to bots or services that vote on your behalf
- **Account creation**: bootstrap new accounts by delegating the minimum required stake

The delegatee gains your voting power for awards, committee votes, and bandwidth, while you retain full ownership and can revoke at any time (with a 5-day cooldown).

#### 5. Rule-Setting

Chain parameters are set by the **median** of witness-published values. Since you elect witnesses, you indirectly control:
- How expensive it is to create accounts
- How the reward fund is distributed between content creators and curators
- What the minimum participation threshold is for committee proposals
- How long vesting withdrawals take
- Network capacity and transaction limits

If the current parameters don't serve your interests, you vote for witnesses who share your vision. The median mechanism ensures no single witness (or voter) can impose extreme values — only the community consensus prevails.

### Why This Works as Self-Governance

1. **Proportional representation**: 1 SHARES = 1 unit of influence everywhere. No special privileges, no tiered membership.

2. **Bipolar voting**: negative votes are first-class citizens. Opposing a bad proposal is just as powerful as supporting a good one. This prevents apathy-driven approval.

3. **Continuous governance**: there are no "governance seasons" or "voting periods" for witnesses — you can change your votes at any time. Committee proposals have time-bounded voting, but you can update your vote throughout.

4. **Skin in the game**: SHARES are locked tokens. To gain governance power, you must commit capital. To exit, you wait 28 days. This ensures voters have long-term alignment with the ecosystem.

5. **No trusted intermediaries**: all governance rules are enforced by protocol code, not by humans. Committee payouts, witness elections, parameter changes — everything is automatic and verifiable.

6. **Delegation without custody**: you can amplify others' power without giving them your tokens. Revocation is always possible. This enables trust hierarchies without centralization.

### The Governance Cycle

```
Stake VIZ → Get SHARES → Governance Power
    ├── Vote for witnesses     → Control block production & chain parameters
    ├── Vote on committee      → Control treasury spending
    ├── Award other accounts   → Distribute value from reward fund
    ├── Delegate to others     → Amplify allies' governance power
    └── Set chain parameters   → Shape the rules (via witness election)
         ↓
    Witnesses produce blocks → Rewards generated → Reward fund grows
         ↓
    Committee fund grows → Proposals funded → Ecosystem develops
         ↓
    VIZ value increases → More staking incentive → Cycle continues
```

This is a complete, self-sustaining DAO where every participant's influence is exactly proportional to their committed stake, and every aspect of the system — from treasury spending to network parameters to value distribution — is governed by the collective will of SHARES holders.

---

## Constants Reference

| Constant | Value | Description |
|---|---|---|
| `CHAIN_VESTING_WITHDRAW_INTERVALS` | 28 | Number of daily withdrawal installments |
| `CHAIN_VESTING_WITHDRAW_INTERVAL_SECONDS` | 86400 (1 day) | Time between withdrawal installments |
| `CHAIN_MAX_WITHDRAW_ROUTES` | 10 | Maximum withdrawal routing destinations |
| `CHAIN_ENERGY_REGENERATION_SECONDS` | 432000 (5 days) | Full energy regeneration period |
| `CHAIN_100_PERCENT` | 10000 | Basis points (100.00%) |
| `CHAIN_MIN_DELEGATION` | 1 VIZ | Minimum delegation amount |
| `CHAIN_CREATE_ACCOUNT_DELEGATION_RATIO` | 10× | Delegation multiplier for account creation |
| `CHAIN_CREATE_ACCOUNT_DELEGATION_TIME` | 30 days | Minimum delegation lock for account creation |
| `CHAIN_MAX_ACCOUNT_WITNESS_VOTES` | 100 | Maximum witnesses per account |
| `CONSENSUS_VOTE_ACCOUNTING_MIN_RSHARES` | 5000000 | Minimum rshares for award to have effect |
| `CONSENSUS_COMMITTEE_REQUEST_APPROVE_MIN_PERCENT` | 1000 (10%) | Default participation threshold for proposals |
| `CONSENSUS_BANDWIDTH_RESERVE_BELOW` | 500 SHARES | Threshold for bandwidth reserve |
| `CONSENSUS_BANDWIDTH_RESERVE_PERCENT` | 1000 (10%) | Extra bandwidth for small accounts |
