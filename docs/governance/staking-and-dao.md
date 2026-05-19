# Staking and DAO Governance

## Staking (SHARES)

Staking converts liquid VIZ tokens into **SHARES** (vesting shares). Staked tokens are locked and cannot be transferred directly, but they grant governance power proportional to the amount staked.

Every account has three vesting fields:

| Field | Meaning |
|-------|---------|
| `vesting_shares` | SHARES owned by the account |
| `delegated_vesting_shares` | SHARES delegated out to others (reduces power) |
| `received_vesting_shares` | SHARES received via delegation (increases power) |

**Effective vesting shares** — the governance power used in all weighted operations:

```
effective_vesting_shares = vesting_shares − delegated_vesting_shares + received_vesting_shares
```

---

## Staking Operations

### Stake: `transfer_to_vesting_operation` (ID 3)

Converts liquid VIZ to SHARES. Can vest into another account's balance.

```json
[3, {"from": "alice", "to": "alice", "amount": "1000.000 VIZ"}]
```

### Unstake: `withdraw_vesting_operation` (ID 4)

Initiates gradual withdrawal over `withdraw_intervals` daily installments (chain-governed, default 28 days). Set to `"0.000000 SHARES"` to cancel.

```json
[4, {"account": "alice", "vesting_shares": "1000.000000 SHARES"}]
```

Virtual `fill_vesting_withdraw_operation` fires once per interval as tokens are released.

### Withdrawal Routes: `set_withdraw_vesting_route_operation` (ID 11)

Routes a percentage of withdrawals to another account, optionally re-vesting immediately.

```json
[11, {"from_account": "alice", "to_account": "bob", "percent": 5000, "auto_vest": true}]
```

Up to 10 routes per account; total percent across all routes must not exceed 10000.

### Delegate: `delegate_vesting_shares_operation` (ID 19)

Transfers governance power (not ownership) to another account. Set to `"0.000000 SHARES"` to remove.

```json
[19, {"delegator": "alice", "delegatee": "bob", "vesting_shares": "500.000000 SHARES"}]
```

When removing delegation, SHARES enter a 7-day return window. Virtual `return_vesting_delegation_operation` fires when they return.

---

## Where SHARES Are Used

SHARES are the universal governance token. Every meaningful action is weighted by `effective_vesting_shares`.

### 1. Validator Voting (Fair-DPOS)

```json
[7, {"account": "alice", "witness": "bob", "approve": true}]
```

Vote weight is **divided equally** among all validators the account votes for:

```
fair_weight = effective_vesting_shares / witnesses_voted_for
```

This prevents concentration — voting for 10 validators gives each 1/10 of your weight. Accounts may also set a proxy (`account_witness_proxy_operation`) to delegate all validator voting to another account.

### 2. Committee DAO Voting

```json
[37, {"voter": "alice", "request_id": 42, "vote_percent": 7500}]
```

Vote weight: `effective_vesting_shares × vote_percent / 10000`.  
Range: −10000 (strong oppose) to +10000 (strong support).

### 3. Awards (Social Reward Distribution)

```json
[47, {"initiator": "alice", "receiver": "bob", "energy": 1000, ...}]
```

Reward size is proportional to:
```
rshares = effective_vesting_shares × energy / 10000
```

An account with 10× more SHARES creates 10× more reward for the same energy.

### 4. Chain Parameter Governance

Validators publish preferred chain parameters; the blockchain applies the median. Since validators are elected by stake-weighted votes, all chain parameters are indirectly governed by SHARES holders.

### 5. Transaction Bandwidth

Network bandwidth is allocated proportionally to `effective_vesting_shares`. Accounts below 500 SHARES get an additional 10% bandwidth reserve.

### 6. Account Creation via Delegation

New accounts can be bootstrapped by delegating SHARES to them at a 10× ratio (locked for 30 days), making account creation accessible without liquid tokens.

---

## VIZ as a DAO

| Traditional DAO | VIZ Blockchain |
|-----------------|----------------|
| DAO treasury | Committee fund + reward fund |
| Governance tokens | SHARES |
| Proposal voting | Committee worker requests |
| Board of directors | Elected validators |
| Director elections | Validator voting (Fair-DPOS) |
| Dividend distribution | Award mechanism (reward fund) |
| Bylaws / rules | Chain properties (median governance) |
| Proxy voting | `delegate_vesting_shares` + validator proxy |

### Governance Properties

1. **Proportional representation**: 1 SHARES = 1 unit of influence everywhere.
2. **Bipolar voting**: negative votes actively oppose, not just abstain.
3. **Continuous governance**: no fixed voting seasons — votes can be changed anytime.
4. **Skin in the game**: SHARES are locked; exiting takes 28 days. Long-term alignment.
5. **No trusted intermediaries**: all rules enforced by protocol code.
6. **Delegation without custody**: governance power can be lent and revoked at any time.

### Governance Cycle

```
Stake VIZ → Get SHARES → Governance Power
    ├── Vote for validators  → Block production & chain parameters
    ├── Vote on committee    → Treasury spending
    ├── Award other accounts → Distribute value from reward fund
    └── Delegate to others   → Amplify allies' governance power
```

---

## Key Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `CHAIN_VESTING_WITHDRAW_INTERVALS` | 28 | Daily withdrawal installments |
| `CHAIN_VESTING_WITHDRAW_INTERVAL_SECONDS` | 86400 (1d) | Time between installments |
| `CHAIN_MAX_WITHDRAW_ROUTES` | 10 | Max withdrawal routes per account |
| `CHAIN_ENERGY_REGENERATION_SECONDS` | 432000 (5d) | Full energy regeneration |
| `CHAIN_100_PERCENT` | 10000 | Basis points denominator |
| `CHAIN_MAX_ACCOUNT_WITNESS_VOTES` | 100 | Max validators per account |

---

See also: [Chain Properties](./chain-properties.md), [Committee DAO](./committee.md), [Awards](../protocol/operations/awards.md), [Transfers](../protocol/operations/transfers.md).
