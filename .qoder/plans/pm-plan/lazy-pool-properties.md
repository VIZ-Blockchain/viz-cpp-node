# Lazy Pool: Properties, Accounting & State Transitions

## 1. Overview

The Lazy Liquidity Pool is a **singleton** smart-contract-like entity (single DB row, `id=1`) that pools VIZ from multiple depositors and automatically deploys it as:

1. **LP liquidity** — provides market-making capital to active prediction markets
2. **Leverage loans** — loans VIZ to bettors opening boosted (leveraged) positions

All monetary values use **milli-VIZ precision** (1 VIZ = 1000 internal units, `PRECISION = 1000`).

The pool uses **Lazy Accounting** (MasterChef/Compound pattern): a single global accumulator (`reward_per_share`) enables O(1) profit distribution regardless of participant count. When profit arrives, **only** `reward_per_share` is updated — no per-user writes. Users read their rewards via `shares × (rps − snapshot) / 10^9`.

---

## 2. Pool Properties

### 2.1 Stored Properties (Database Columns)

| Column | Type | Precision | Description |
|--------|------|-----------|-------------|
| `total_shares` | bigint | milli-VIZ | Total share tokens outstanding. 1:1 on first deposit; subsequent deposits price against **pool equity**: `new_shares = amount × total_shares / pool_equity` (see `pool_equity` below) |
| `free_balance` | bigint | milli-VIZ | VIZ not currently deployed — available for new allocations, leverage loans, and withdrawals. This is the **working capital** of the pool |
| `allocated_balance` | bigint | milli-VIZ | VIZ currently deployed as LP liquidity on active markets. Returned to `free_balance` on market resolution or recall |
| `earned_balance` | bigint | milli-VIZ | Cumulative total VIZ earned by the pool from all sources (LP profits, leverage profits, penalties, fees). **Monotonically non-decreasing** — only grows, never shrinks |
| `leverage_fund_used` | bigint | milli-VIZ | Total active leverage loans outstanding. A sub-allocation **cap** on `free_balance`, not a separate pool |
| `pending_withdrawals` | bigint | milli-VIZ | VIZ owed to queued withdrawers not yet paid (Σ of the FIFO request queue). A **first claim** on returning capital; guarantees `free_balance` never goes negative (see Transition 12) |
| `reward_per_share` | bigint | 10^9 | Accumulated reward per share (LAZY_POOL_PRECISION). **Monotonically non-decreasing** — only grows. The Lazy Accounting accumulator |

### 2.2 Computed Properties (Derived at Query Time)

| Property | Formula | Description |
|----------|---------|-------------|
| `total_value` | `free_balance + allocated_balance` | Total VIZ controlled by the pool |
| `pool_equity` | `free_balance + allocated_balance − pending_withdrawals` | VIZ that actually backs the **outstanding** shares (excludes capital already owed to queued withdrawers). **This is the share-pricing basis** — deposits and per-share value use it, NOT `free_balance` alone |
| `invested_liquidity` | `allocated_balance` | VIZ deployed as LP on active markets |
| `invested_leverage` | `leverage_fund_used` | VIZ deployed as leverage loans |
| `free_amount` | `free_balance − leverage_fund_used` | Truly free VIZ — not on markets, not loaned for leverage. Available for new allocations and leverage loans |
| `leverage_fund_total` | `free_balance × F% / 100` | Cap on how much of `free_balance` may be used for leverage loans |
| `leverage_fund_available` | `leverage_fund_total − leverage_fund_used` | Remaining leverage loan capacity |
| `per_share_value` | `pool_equity / total_shares` | Current VIZ value of one share (deposit-pricing basis) |
| `pool_profit_ratio` | `earned_balance / total_value` | Lifetime pool profitability (monitoring metric) |

### 2.3 Example State

```
lazy_pool:
  free_balance       = 10,000,000 mVIZ  (10,000 VIZ)
  allocated_balance  =  7,000,000 mVIZ  ( 7,000 VIZ)
  earned_balance     =  1,500,000 mVIZ  ( 1,500 VIZ)  ← cumulative earnings
  leverage_fund_used =    100,000 mVIZ  (   100 VIZ)  ← active leverage loans
  total_shares       =  8,000,000       ( 8,000 shares)
  reward_per_share   = 187,500,000     (high-precision accumulator)

Computed:
  total_value        = 10,000,000 + 7,000,000 = 17,000,000 mVIZ (17,000 VIZ)
  pool_equity        = 10,000,000 + 7,000,000 − 0 = 17,000,000 mVIZ (no pending withdrawals here)
  invested_liquidity =  7,000,000 mVIZ  (7,000 VIZ on active markets)
  invested_leverage  =    100,000 mVIZ  (  100 VIZ in leverage loans)
  free_amount        = 10,000,000 − 100,000 = 9,900,000 mVIZ (9,900 VIZ)
  per_share_value    = pool_equity / total_shares = 17,000,000 / 8,000,000 = 2.125 VIZ/share

Leverage fund (F% = 10%):
  leverage_fund_total     = 10,000,000 × 10% = 1,000,000 mVIZ (1,000 VIZ)
  leverage_fund_available = 1,000,000 − 100,000 = 900,000 mVIZ (900 VIZ)
```

---

## 3. Invariants

These must hold after every state transition:

```
1. free_balance + allocated_balance = total_value       (conservation of value)
2. free_balance ≥ leverage_fund_used                    (loans cannot exceed free capital)
3. earned_balance is monotonically non-decreasing        (cumulative earnings only grow)
4. reward_per_share is monotonically non-decreasing      (accumulated rewards only grow)
5. total_shares > 0 iff pool_equity > 0                 (no shares without backing capital; equity, not free, since capital may be deployed)
6. free_balance ≥ 0                                     (never pay out capital not held — LEDGER INTEGRITY)
7. Σ withdrawal_queue.amount = pending_withdrawals       (queue liability is exact)
```

---

## 4. User-Level Properties

Each user has the following lazy pool fields on the `users` table:

| Field | Type | Description |
|-------|------|-------------|
| `lazy_pool_shares` | bigint | User's share tokens (sum across all deposits) |
| `lazy_pool_balance` | bigint | User's principal VIZ deposited (not including earnings) |
| `lazy_pool_reward_snapshot` | bigint | Snapshot of `pool.reward_per_share` at last user action |
| `lazy_pool_pending_rewards` | bigint | Settled but unclaimed rewards (accumulated at deposit/withdraw) |

**Computed per-user:**

```
live_unsettled_reward = lazy_pool_shares × (pool.reward_per_share − lazy_pool_reward_snapshot) / 10^9
total_user_reward     = lazy_pool_pending_rewards + live_unsettled_reward
user_share_value      = lazy_pool_shares × pool.free_balance / pool.total_shares
user_total_value      = user_share_value + total_user_reward
user_earned           = pool.earned_balance × (lazy_pool_shares / pool.total_shares)
user_principal        = user_total_value − user_earned
```

---

## 5. Share Calculation

### 5.1 Deposit — New Shares

```
pool_equity = free_balance + allocated_balance − pending_withdrawals   (capital backing shares)
First depositor / empty pool:  new_shares = amount                     (1:1 ratio)
Subsequent:                    new_shares = amount × total_shares / pool_equity
```

Shares are priced against **pool_equity**, not `free_balance` alone. `pool_equity` is the VIZ that
actually backs outstanding shares: it counts capital deployed in markets (`allocated_balance`) and
excludes what is already owed to queued withdrawers (`pending_withdrawals`). This keeps the reward
weight of a new deposit proportional to the capital it contributes, regardless of how much of the
pool is currently deployed.

> **Fix (2026-07-11).** Previously the denominator was `free_balance` alone. When capital was
> deployed (`allocated_balance > 0`), `free_balance` was small, so a new depositor minted a
> disproportionately large share/reward weight for the same VIZ. Using `pool_equity` removes that
> distortion. If `pool_equity ≤ 0` (empty or insolvent pool), minting falls back to 1:1.

### 5.2 Withdrawal — Value Returned

A withdrawal returns the depositor's **stored principal** (pro-rated to the shares burned) plus their
accrued rewards (`reward_per_share` accumulator) — it is principal-preserving, not a share-price sale.
The payout is bounded by `free_balance` and queued when short (Transitions 9/11/12). Because principal
is tracked per deposit, `per_share_value` (`pool_equity / total_shares`) is used only to price new
deposits, not to compute withdrawals.

---

## 6. State Transitions

All state changes are **atomic** (single SQL transaction). Each transition is shown with its effect on every pool column.

### Transition 1: User Deposit

```
User deposits amount A into the pool.

BEFORE:  settle user rewards (pending += shares × (rps − snapshot) / 10^9; snapshot = rps)

lazy_pool:
  free_balance       += A
  total_shares       += new_shares    (A × total_shares / pool_equity before deposit; pool_equity = free + allocated − pending_withdrawals)
  allocated_balance  — no change
  earned_balance     — no change
  leverage_fund_used — no change
  reward_per_share   — no change

Fund flow:  user.balance → lazy_pool.free_balance
```

### Transition 2: Auto-Allocation to Market

```
Pool allocates alloc_amount to a new market (when oracle accepts).

lazy_pool:
  free_balance       −= alloc_amount
  allocated_balance  += alloc_amount
  total_shares       — no change
  earned_balance     — no change
  leverage_fund_used — no change
  reward_per_share   — no change

Fund flow:  free_balance → market CPMM reserves (as LP position, user=0)
Effect:     invested_liquidity increases. total_value unchanged.
```

### Transition 3: Market Resolves — Principal Return (No Profit)

```
Market resolves, pool LP position returns exactly the allocation amount.

lazy_pool:
  free_balance       += alloc_amount
  allocated_balance  −= alloc_amount
  total_shares       — no change
  earned_balance     — no change
  leverage_fund_used — no change
  reward_per_share   — no change

Fund flow:  market CPMM reserves → lazy_pool.free_balance
Effect:     invested_liquidity decreases. total_value unchanged.
```

### Transition 4: Market Resolves — With Profit

```
Market resolves, pool earns profit on LP position.
  lp_return = amount returned from market (principal + profit)
  lp_profit = lp_return − alloc_amount

lazy_pool:
  free_balance       += lp_return                    (principal + profit both return to free)
  allocated_balance  −= alloc_amount                  (no longer allocated)
  earned_balance     += lp_profit                     (track cumulative earnings)
  reward_per_share   += lp_profit × 10^9 / total_shares  (distribute to LP investors)
  total_shares       — no change
  leverage_fund_used — no change

Fund flow:  market CPMM reserves → lazy_pool.free_balance
            profit portion also recorded in earned_balance + distributed via reward_per_share
Effect:     principal: invested_liquidity → free
            profit: adds to free, earned, and reward_per_share
```

### Transition 5: Graduated Recall (Idle Market)

```
Cron recalls recall_amount from an idle market's allocation.

lazy_pool:
  free_balance       += recall_amount
  allocated_balance  −= recall_amount
  total_shares       — no change
  earned_balance     — no change
  leverage_fund_used — no change
  reward_per_share   — no change

Fund flow:  market CPMM reserves → lazy_pool.free_balance
Effect:     Partial invested_liquidity → free. total_value unchanged.
```

### Transition 6: Leverage Position Opened

```
Bettor opens a boosted position with loan L from the pool.
  C = bettor's collateral, L = pool's loan

lazy_pool:
  free_balance       −= (C + L)     (collateral C + loan L placed into AMM)
  leverage_fund_used += L            (track active loans — only L, not C)
  total_shares       — no change
  earned_balance     — no change
  allocated_balance  — no change
  reward_per_share   — no change

Fund flow:  lazy_pool.free_balance → market CPMM reserves
Note:       Only L counts toward leverage_fund_used (not C, the bettor's own collateral).
            free_balance decreases by full (C+L), but leverage_fund_used only increases by L.
            The bettor's collateral C reduces free_amount but is not a leverage obligation.
```

### Transition 7: Leverage Position Closed (Liquidation / Voluntary Close / Force-Close)

```
Pool recovers loan + profit from a closed leverage position.
  pool_received = loan_return + pool_profit
  pool_profit   = pool_received − L

lazy_pool:
  free_balance       += pool_received                (loan + profit return to free)
  leverage_fund_used −= L                             (free up loan capacity)
  earned_balance     += pool_profit                   (track cumulative earnings)
  reward_per_share   += pool_profit × 10^9 / total_shares  (distribute to LP investors)
  total_shares       — no change
  allocated_balance  — no change

Fund flow:  market CPMM reserves → lazy_pool.free_balance
Effect:     invested_leverage decreases (leverage_fund_used −= L)
            profit adds to free, earned, and reward_per_share
            leverage_fund_used decreases, freeing capacity for new loans
```

### Transition 7a: Leverage Position Liquidated After Cancel-Bet (Bad Debt)

When a cancel-bet on the same outcome as a leveraged position pushes it below the liquidation threshold, the cancel-bet executes first (transaction initiator gets expected price), then the position is liquidated at post-cancel-bet reserves. The pool may absorb a shortfall.

**Cascade behavior:** Liquidating one position changes AMM reserves (tokens returned, VIZ removed), which can push other same-side positions below their threshold. The system uses a recursive cascade loop (`leverage_cascade_liquidate`) that re-evaluates all remaining positions after each liquidation until no more positions qualify. This prevents hidden bad debt from positions that become underwater due to the reserve changes caused by earlier liquidations in the same cascade. See [Liquidation Cascade](../.qoder/docs/leverage-risk-off-strategy.md#liquidation-cascade-long-squeeze) for full analysis.

```
  cancel_value < liquidation_threshold (position pushed below by cancel-bet)
  pool_received = cancel_value           (entire amount to pool)
  shortfall     = liquidation_threshold − cancel_value  (bad debt)

lazy_pool:
  free_balance       += cancel_value      (remaining value returns to free)
  free_balance       −= shortfall          (bad debt absorbed by pool)
  leverage_fund_used −= L                  (free up loan capacity)
  earned_balance     — no change            (no earnings on a loss)
  reward_per_share   — no change            (no profit to distribute)
  total_shares       — no change
  allocated_balance  — no change

Effect: The pool absorbs the shortfall from free_balance. This is a bounded, rare event:
  - Bounded by SL% (cancel-bet's price impact cap)
  - Rare: only when position near threshold + cancel-bet on same outcome is the trigger
  - Fairness: the cancel-bettor (transaction initiator) is not penalized
  See Leverage Risk-Off Strategy, Section 5 (Case B) for full specification.
```

### Transition 8: Leverage Position Resolved (Loss — Outcome Risk)

```
Bettor's outcome loses at resolution. Pool loses the loan L.

lazy_pool:
  leverage_fund_used −= L                             (loan gone, free up capacity)
  free_balance       — no change                      (loan was already in AMM, now lost)
  earned_balance     — no change                      (no earnings from this position)
  total_shares       — no change
  allocated_balance  — no change
  reward_per_share   — no change

Effect:     invested_leverage decreases. The loss is absorbed by the pool's total_value
            (which implicitly decreased when the bet was placed into the AMM and the
            outcome lost). free_balance doesn't change because the loan was already
            removed from free_balance at opening — it was in the AMM reserves and is now
            distributed to winning bettors at resolution.
```

### Transition 8a: Leverage Position Resolved (Win — Outcome Reward)

Bettor's outcome wins at resolution. Pool receives loan + profit.

**Note:** When multiple leveraged positions are force-closed together (e.g., Cron Job 11 or Case A opposing bet), the system uses a recursive cascade loop. Each force-close changes AMM reserves, potentially affecting subsequent positions. See Transition 7a cascade behavior.

```
pool_obligation = L × (1 + R%/100)   (= liquidation_threshold)
payout = redeem(tokens, outcome=winning)
pool_received = min(payout, pool_obligation) = pool_obligation  (payout ≥ threshold)
bettor_received = payout − pool_obligation
pool_profit = pool_received − L = L × R%/100

lazy_pool:
  free_balance       += pool_received     (loan + profit return to free)
  leverage_fund_used −= L                  (free up loan capacity)
  earned_balance     += pool_profit        (track cumulative earnings)
  reward_per_share   += pool_profit × 10^9 / total_shares  (distribute to LP investors)
  total_shares       — no change
  allocated_balance  — no change

Fund flow:  market AMM → lazy_pool.free_balance (via resolution payout)
Effect:     invested_leverage decreases. Pool earns L × R% profit.
            Bettor receives (payout − pool_obligation) from resolution.
```

### Transition 9: User Withdrawal (Planned)

Allowed only after the lock elapses; partial allowed (`shares == 0` ⇒ all). The payout is **queued and
bounded by `free_balance`** exactly like the emergency path — see Transitions 11–12. It is instant when
the pool is liquid, and paid in parts (oldest-first) when it is not; `free_balance` never goes negative.

```
User withdraws shares from the pool.
  principal_out  = principal × shares_to_burn / total_shares
  reward_portion = accrued_rewards × shares_to_burn / total_shares
  owed           = principal_out + reward_portion       (no penalty when unlocked)

BEFORE:  settle user rewards (pending += shares × (rps − snapshot) / 10^9; snapshot = rps)

lazy_pool:
  total_shares        −= shares_to_burn                (shares burned)
  pending_withdrawals += owed                          (first-claim liability)
  allocated_balance / earned_balance / leverage_fund_used / reward_per_share — no change

THEN: create request { account, amount = owed } and run Transition 12 (pays from free_balance, FIFO).
Effect:     User receives their share of principal + accumulated rewards, bounded by liquid funds.
```

### Transition 10: Convert Leveraged Position to Normal Bet

A bettor converts their boosted position into a normal (non-leveraged) bet, paying the pool from their balance. This allows holding the position to resolution and capturing the full outcome payout.

```
Bettor converts leveraged position to normal bet.
  pool_obligation     = L × (1 + R%/100)             (= liquidation_threshold)
  current_profit      = cancel_value − pool_obligation  (bettor's unrealized gain)
  conversion_fee      = current_profit × conversion_profit_cost% / 100
  total_user_payment  = pool_obligation + conversion_fee
  pool_profit         = (pool_obligation − L) + conversion_fee

lazy_pool:
  free_balance       += total_user_payment    (obligation + fee from bettor's balance)
  leverage_fund_used −= L                      (free up loan capacity)
  earned_balance     += pool_profit             (track cumulative earnings)
  reward_per_share   += pool_profit × 10^9 / total_shares  (distribute to LP investors)
  total_shares       — no change
  allocated_balance  — no change

User:
  balance            −= total_user_payment

Position:
  status → converted (5)
  Bet record updated: remove leveraged_position, keep as normal bet (100% bettor-owned)

Fund flow:  user.balance → lazy_pool.free_balance
Effect:     Pool receives full loan repayment + profit + conversion fee.
            The bettor assumes 100% outcome risk at resolution.
            conversion_profit_cost is committee-configurable (default 50%).
```

See [Leverage Risk-Off Strategy](../.qoder/docs/leverage-risk-off-strategy.md) — Convert to Normal Bet (`pm_leverage_convert`) for full specification.

### Transition 11: Emergency Withdrawal (With Penalty)

Emergency withdrawal is allowed any time (penalised while still locked) and supports a **partial
amount** (`shares == 0` ⇒ whole position, else burn exactly `shares`). Like the planned withdrawal
(Transition 9) it **never pays out more than the pool holds liquid** — the amount owed is registered
as a first-claim liability (`pending_withdrawals`) and paid FIFO from `free_balance` (Transition 12).

```
User emergency-withdraws (partial allowed); penalty on the withdrawn profit while locked.
  principal_out = principal × burn_shares / user_total_shares
  pending_out   = accrued_rewards × burn_shares / user_total_shares
  penalty       = locked ? pending_out × emergency_penalty% / 10000 : 0
  owed          = principal_out + pending_out − penalty

BEFORE:  settle user rewards (accrue rps delta into pending)

lazy_pool:
  total_shares        −= burn_shares                    (shares burned now)
  pending_withdrawals += owed                           (first-claim liability, not yet paid)
  reward_per_share    += penalty × 10^9 / remaining_shares   (penalty redistributed, if locked)
  free_balance        — unchanged HERE (payout happens in Transition 12)
  allocated_balance / leverage_fund_used / earned_balance — no change

THEN: create a withdrawal request { account, amount = owed } and run Transition 12.

Effect:  Shares burn immediately; the exiting LP becomes a fixed-VIZ creditor. Penalty stays in the
         pool and compensates remaining LPs. The payout is bounded by free_balance — see Transition 12.
```

### Transition 12: Withdrawal Queue Servicing (Ledger-Integrity Fix, 2026-07-11)

The **critical invariant**: the pool may never pay out capital it does not hold liquid — `free_balance`
must never go negative. A withdrawal (planned or emergency) whose `owed` exceeds the liquid
`free_balance` is paid in part now and the rest **waits in a FIFO queue**, settled as capital returns
from markets and leverage. This is run at every point `free_balance` increases (LP return —
Transitions 3/4, leverage repay — 7/8/10, and new deposits — 1) so queued withdrawers have **first
claim** on returning capital and nothing is redeployed ahead of them.

```
service_queue():                     # run after every free_balance increase
  while free_balance > 0 and queue not empty:
    req  = oldest request (lowest id = FIFO)
    pay  = min(free_balance, req.amount)
    user.balance        += pay
    free_balance        −= pay
    pending_withdrawals −= pay
    if pay == req.amount:  remove req            # fully settled
    else:                  req.amount −= pay; break   # free exhausted, keep remainder queued

Invariant preserved:  free_balance ≥ 0  and  Σ queue.amount == pending_withdrawals
Effect:  A withdrawal that IS fully covered is created and cleared within the same operation
         (instant, unchanged UX). When capital is short, the withdrawer is paid in parts, oldest
         first, as markets resolve and leverage repays — never overdrawing the ledger.
```

**Why:** before this fix, emergency withdrawal paid `principal + rewards` from `free_balance`
unconditionally. When the principal was still deployed (`allocated_balance`), `free_balance` went
negative — the pool handed out capital that had not yet returned. The queue makes the liability
explicit and bounds every payout by liquid funds.

---

## 7. Leverage Fund Allocation

The leverage fund is a **sub-allocation (cap)** on `free_balance`, not a separate pool with its own balance. It defines how much of the free balance may be used for leverage loans.

```
leverage_fund_total     = free_balance × F% / 100     (cap, not a separate balance)
leverage_fund_available = leverage_fund_total − leverage_fund_used
max_loan_per_position   = leverage_fund_available × P% / 100
```

**Key principle:** All leverage returns (loan repayment + profit) flow into `lazy_pool.free_balance`. The `leverage_fund_used` counter only tracks outstanding loan obligations — decrementing it frees capacity for new loans but does not move money to a separate account. The profit (L × R%) is distributed to lazy pool investors via `reward_per_share`, exactly like any other pool earnings.

**`leverage_fund_used` tracking** — updated atomically within the same transaction as the position status change:

| Event | `leverage_fund_used` | Fund flow |
|-------|----------------------|-----------|
| `pm_leverage_open` | `+= loan` | `free_balance −= (C + L)` (bet placed into AMM) |
| `pm_leverage_liquidate` | `−= loan` | `free_balance += pool_received` (loan + profit); `reward_per_share += profit` |
| `pm_leverage_resolve` (win) | `−= loan` | `free_balance += pool_received` (loan + profit); `reward_per_share += profit` |
| `pm_leverage_resolve` (loss) | `−= loan` | Pool loses L (absorbed by pool); `leverage_fund_used` still decremented |
| `pm_leverage_close` (voluntary) | `−= loan` | `free_balance += pool_received` (loan + profit); `reward_per_share += profit` |

---

## 8. earned_balance and Withdrawal

`earned_balance` tracks the **cumulative lifetime earnings** of the pool. It is used during withdrawal to determine what portion of a user's payout is **earned profit** (withdrawable immediately) vs. **principal return** (subject to lock period rules).

```
User's earned portion at withdrawal:
  user_earned    = earned_balance × (user_shares / total_shares)
  user_principal = user_total_value − user_earned

Where user_total_value = user_shares × pool_equity / total_shares + pending_rewards
```

This separation matters for:

- **Tax reporting**: earned income vs. capital return may have different tax treatment
- **Emergency withdrawal penalty**: penalty applies only to profit on locked shares
- **Pool health monitoring**: `earned_balance / total_value` indicates pool profitability over time

### When earned_balance Changes

| Transition | earned_balance change | Source |
|------------|----------------------|--------|
| Market resolves with profit | `+= lp_profit` | LP fee earnings on market |
| Leverage position closed with profit | `+= pool_profit` | Leverage interest (L × R%) |
| Emergency withdrawal penalty | — no change | Penalty goes to `reward_per_share`, not `earned_balance` |
| User deposit | — no change | |
| User withdrawal | — no change | Cumulative: never decreases |

---

## 9. Precision Reference

| Constant | Value | Used For |
|----------|-------|----------|
| `PRECISION` | 1000 | Milli-VIZ (all monetary values in DB) |
| `LAZY_POOL_PRECISION` | 10^9 (1,000,000,000) | `reward_per_share` accumulator integer math |
| `PRICE_PRECISION` | 1,000,000 | Probability display (6 decimal places) |

**Why 10^9 for reward_per_share?**

Shares are denominated in milli-VIZ (1 VIZ = 1000 units), displayed as `1.000000`. The 10^9 multiplier is an internal integer math precision ensuring that even tiny profits distribute non-zero increments:

```
Example: 1 mVIZ profit on 1,000,000 shares
  reward_per_share increment = 1 × 10^9 / 1,000,000 = 1000 (non-zero)

If we used 10^6 instead:
  reward_per_share increment = 1 × 10^6 / 1,000,000 = 1 (non-zero, but very coarse)

If we used 10^3 (same as PRECISION):
  reward_per_share increment = 1 × 10^3 / 1,000,000 = 0 (DUST — profit lost!)
```

The 10^9 factor provides 6 orders of magnitude between the smallest unit (1 mVIZ) and the accumulator, ensuring accurate distribution across up to ~10^6 shares without dust loss.

---

## 10. Auxiliary Tables

### 10.1 lazy_pool_deposits (Per-Deposit Lock Tracking)

| Column | Type | Description |
|--------|------|-------------|
| `id` | bigint PK | Auto-increment |
| `user` | bigint | User ID |
| `time` | int | Deposit unixtime |
| `amount` | bigint | Principal (milli-VIZ) |
| `shares` | bigint | Share tokens received (milli-VIZ) |
| `unlock_time` | int | Unixtime when deposit unlocks |
| `status` | tinyint | 0=locked, 1=unlocked, 2=withdrawn, 3=emergency_withdrawn |

Each user has at most: **1 unlocked record** (status=1, consolidated) + **N locked records** (status=0). When locked deposits expire, they merge into the single unlocked record.

### 10.2 lazy_pool_allocations (Per-Market Allocation Tracking)

| Column | Type | Description |
|--------|------|-------------|
| `id` | bigint PK | Auto-increment |
| `market` | bigint | Market ID (unique key) |
| `amount` | bigint | Current allocated amount (milli-VIZ, decreases with recalls) |
| `original_amount` | bigint | Original allocated amount before recalls |
| `time` | int | Allocation unixtime |
| `status` | tinyint | 0=active, 1=returned |
| `returned_amount` | bigint | Amount returned including profit |
| `bets_sum_at_check` | bigint | Cumulative bets_sum at last recall check |
| `check_step` | int | Which 10% duration step (0=just allocated, 1..10) |
| `last_check_time` | int | Unix timestamp of last recall check |
| `recalled_amount` | bigint | Total amount recalled so far |

---

## 11. Complete Transition Matrix

Summary of all pool column changes across transitions:

| # | Transition | `free_balance` | `allocated_balance` | `earned_balance` | `leverage_fund_used` | `reward_per_share` | `total_shares` |
|---|-----------|----------------|---------------------|------------------|----------------------|--------------------|----------------|
| 1 | Deposit | `+= A` | — | — | — | — | `+= new_shares` |
| 2 | Allocate | `−= alloc` | `+= alloc` | — | — | — | — |
| 3 | Resolve (no profit) | `+= alloc` | `−= alloc` | — | — | — | — |
| 4 | Resolve (with profit) | `+= return` | `−= alloc` | `+= profit` | — | `+= profit×10^9/N` | — |
| 5 | Recall | `+= recall` | `−= recall` | — | — | — | — |
| 6 | Leverage open | `−= (C+L)` | — | — | `+= L` | — | — |
| 7 | Leverage close | `+= received` | — | `+= profit` | `−= L` | `+= profit×10^9/N` | — |
| 7a | Leverage close (bad debt) | `+= cancel·−shortfall` | — | — | `−= L` | — | — |
| 8 | Leverage resolve (loss) | — | — | — | `−= L` | — | — |
| 8a | Leverage resolve (win) | `+= received` | — | `+= profit` | `−= L` | `+= profit×10^9/N` | — |
| 9 | Withdraw | `−= payout` | — | — | — | — | `−= burned` |
| 10 | Leverage convert | `+= total_payment` | — | `+= pool_profit` | `−= L` | `+= profit×10^9/N` | — |
| 11 | Emergency withdraw | `−= (payout−penalty)` | — | — | — | `+= penalty×10^9/N'` | `−= all_shares` |

Legend: `N` = total_shares, `N'` = remaining_shares after burn, `—` = no change
