# Leverage Risk-Off Strategy with Lazy Pool

## Table of Contents

1. [Concept](#concept)
2. [Architecture Overview](#architecture-overview)
3. [Leverage Fund Allocation](#leverage-fund-allocation)
4. [Mathematical Model](#mathematical-model)
   - [CPMM Bet & Token Calculation](#cpmm-bet--token-calculation)
   - [Cancel Value (Position Exit Price)](#cancel-value-position-exit-price)
   - [Worst-Case Cancel Value After Opposing Bet](#worst-case-cancel-value-after-opposing-bet)
   - [Liquidation Threshold](#liquidation-threshold)
   - [Safety Margin](#safety-margin)
   - [Maximum Available Leverage — Full Constraint System](#maximum-available-leverage--full-constraint-system)
5. [Atomic Liquidation at Block Level](#atomic-liquidation-at-block-level)
6. [Liquidation Outcome Distribution](#liquidation-outcome-distribution)
7. [Resolution Handling](#resolution-handling)
   - [Market Status Trigger for Force-Close](#market-status-trigger-for-force-close)
8. [Protocol Settings](#protocol-settings)
9. [Database Schema](#database-schema)
10. [API Endpoints](#api-endpoints)
11. [Frontend: Pre-Calculation, Slider & Cancel Warning](#frontend-pre-calculation-slider--cancel-warning)
12. [Protocol Operations (VIZ DLT)](#protocol-operations-viz-dlt)
13. [Numerical Examples](#numerical-examples)
14. [Risk Analysis](#risk-analysis)
15. [Implementation Notes](#implementation-notes)

---

## 1. Concept

**Leverage** allows a bettor to amplify their market exposure beyond their available balance. The Lazy Pool provides the additional capital as a **co-investment** — the pool contributes a loan L, the bettor contributes collateral C, and together they place a bet of (C + L) on the chosen outcome. The pool earns a fixed profit percentage on every loan.

> **⚠️ Critical distinction — leverage is a bet on market price movement, NOT on the outcome result.** All leveraged positions are force-closed before the market reaches resolution. You are betting that the market price of your chosen outcome will move in your favor *during the betting period* — you will never hold a leveraged position to resolution. Your profit or loss comes entirely from the change in market price (social belief shift), not from whether the outcome actually occurs.

### Naming Convention: "Boost" (UI) vs. "Leverage" (Protocol)

The user-facing term is **Boost**. The protocol/API term remains **leverage**.

| Context | Term | Examples |
|---------|------|----------|
| **UI / Frontend / Marketing** | **Boost** | "Boost your bet", "5× Boost", "Open Boosted Position", "Close Boost" |
| **Protocol / API / Database** | **leverage** | `pm_leverage_open`, `leverage-preview`, `leveraged_positions` table |

**Why "Boost" and not "Leverage" in the UI?**

1. "Leverage" (кредитное плечо) implies holding a position through dips to expiration — classic margin trading. Our positions are **force-closed before resolution** with an automatic stop-loss. This is fundamentally different.
2. "Boost" implies a **temporary enhancement** — you boost your bet, the market moves, you capture the change. This matches the actual mechanism.
3. "5× Boost" is more intuitive for non-traders than "5× Leverage".
4. "Leverage" carries the expectation of "I can wait it out". "Boost" carries the expectation of "the position is amplified temporarily" — which is correct.
5. In Russian i18n: "Усиление ставки" (bet boost) is clearer than "Кредитное плечо" (credit shoulder) for non-finance users.

All internal documentation, API endpoints, protocol operations, and database schemas retain the term **leverage** for technical precision and DeFi convention. Only the **user-facing** language changes.

### Two Categories of Risk

| Risk Type | Pool Exposure | Status |
|-----------|--------------|--------|
| **Price-movement risk** (opposing bets, slippage during betting period) | Pool loses money if forced to liquidate at unfavorable price | **Structural zero** — eliminated by atomic pre-check + constraint system |
| **Outcome risk** (bettor's outcome loses at resolution) | Pool loses the loan L | **Not eliminated** — inherent to all betting. The pool is a co-investor on the bettor's chosen outcome |

The pool is protected from price-movement risk by:
- **Pre-execution slippage calculation** — the exact CPMM formula gives deterministic price impact of any opposing bet.
- **Atomic liquidation at block level** — if an incoming bet would push a leveraged position below its safe threshold, the protocol liquidates the position *first* (at current prices), then processes the bet. Gap risk is eliminated.
- **Dynamic leverage caps** — maximum leverage is calculated per-market from liquidity depth, and per-pool from leverage fund availability.
- **Safety margin** — applied during position opening to prevent positions that are too close to the liquidation edge.
- **Force-close before expiration** — Cron Job 11 closes all leveraged positions before betting expires, converting outcome risk into price-movement risk (which is zero). The pool recovers L × (1 + R%) from every position.

---

## 2. Architecture Overview

> **Note:** The percentages below (90% / 10%) are defaults for `leverage_fund_percent` (committee-configurable). The actual split depends on the current governance setting.

```
┌──────────────────────────────────────────────────────────────┐
│                     LAZY LIQUIDITY POOL                       │
│                                                              │
│  Stored:                                                     │
│    free_balance       = 10,000,000 mVIZ  (10,000 VIZ)        │
│    allocated_balance  =  7,000,000 mVIZ  ( 7,000 VIZ)        │
│    earned_balance     =  1,500,000 mVIZ  ( 1,500 VIZ)        │
│    leverage_fund_used =    100,000 mVIZ  (   100 VIZ)        │
│    total_shares       =  8,000,000        (8,000 shares)      │
│    reward_per_share   =  187,500,000     (10^9 precision)    │
│                                                              │
│  Computed:                                                   │
│    total_value     = free + allocated = 17,000 VIZ           │
│    invested_liquidity = allocated_balance  (7,000 VIZ)        │
│    invested_leverage  = leverage_fund_used (  100 VIZ)        │
│    free_amount     = free_balance − leverage_fund_used        │
│                   = 10,000 − 100 = 9,900 VIZ                │
│       │                                                      │
│       ├── LP allocations → invested_liquidity (7,000 VIZ)    │
│       │                                                      │
│       └── F% of free_balance → Leverage Fund (1,000 VIZ)     │
│                  │                                           │
│                  ├── Active loans: 100 VIZ (invested_leverage)│
│                  └── Available: 900 VIZ                       │
│                       │                                      │
│                       └── Per-position max: P% = 1,800 VIZ   │
└──────────────────────────────────────────────────────────────┘

┌─ Bettor places leveraged bet ────────────────────────────────┐
│                                                              │
│  Bettor collateral:     360 VIZ  (from bettor's balance)     │
│  Pool loan:            1800 VIZ  (from leverage fund)        │
│  Total bet:            2160 VIZ  (placed on outcome A)       │
│  Leverage:                  5×                               │
│  Pool profit (R%):       180 VIZ  (paid if bet wins/liquid.) │
│                                                              │
│  ┌─ Frontend slider ────────────────────────────────────┐   │
│  │  Leverage:  [1.0×]────●────[3.0×]────[5.0×]          │   │
│  │  Loan:      1800 VIZ                                  │   │
│  │  Total bet: 2160 VIZ                                  │   │
│  │  Liq. at:   cancel_value ≤ 1980 VIZ (1800+180)       │   │
│  │  Current cancel_value: 2155 VIZ  ✅                   │   │
│  └──────────────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────────────┘
```

---

## 3. Leverage Fund Allocation

The leverage fund is a **sub-allocation** of the lazy pool's `free_balance`. It is not a separate pool — it's a cap on how much of the free balance can be used for leveraged loans.

For a complete reference on all lazy pool properties (stored and computed) and state transitions, see [Lazy Pool: Properties, Accounting & State Transitions](../../docs/lazy-pool-properties.md).

| Parameter | Symbol | Default | Description |
|-----------|--------|---------|-------------|
| `leverage_fund_percent` | `F%` | 10% | Percentage of lazy pool `free_balance` allocated to leverage fund |
| `max_leverage_per_position_percent` | `P%` | 0.2% | Maximum percentage of **available** leverage fund per single position |
| `leverage_pool_profit_percent` | `R%` | 10% | Pool's profit on each leveraged loan (paid on win or liquidation) |
| `leverage_safety_margin_percent` | `S%` | 1% | Safety margin applied during position opening |
| `leverage_max_slippage_percent` | `SL%` | 10% | Maximum price impact of any single bet (protocol-enforced) |

**Fund state calculation:**

```
leverage_fund_total    = lazy_pool.free_balance × F% / 100
leverage_fund_used     = Σ(active leveraged loans)
leverage_fund_available = leverage_fund_total - leverage_fund_used

max_loan_per_position  = leverage_fund_available × P% / 100
```

**Example:**

```
lazy_pool.free_balance  = 10,000,000 VIZ
F% = 10%  →  leverage_fund_total    = 1,000,000 VIZ
active loans             =   100,000 VIZ
leverage_fund_available  =   900,000 VIZ
P% = 0.2% →  max_loan_per_position =     1,800 VIZ
```

**`leverage_fund_used` tracking** — updated atomically within the same transaction as the position status change:

| Event | `leverage_fund_used` | `earned_balance` | Fund flow |
|-------|----------------------|------------------|-----------|
| `pm_leverage_open` | `+= loan` | — | `free_balance -= (C + L)` (bet placed into AMM) |
| `pm_leverage_liquidate` | `-= loan` | `+= pool_profit` | `free_balance += pool_received` (loan + profit); `reward_per_share += pool_profit` |
| `pm_leverage_resolve` (win) | `-= loan` | `+= pool_profit` | `free_balance += pool_received` (loan + profit); `reward_per_share += pool_profit` |
| `pm_leverage_resolve` (loss) | `-= loan` | — | Pool loses L (absorbed by pool); `leverage_fund_used` still decremented |
| `pm_leverage_close` (voluntary) | `-= loan` | `+= pool_profit` | `free_balance += pool_received` (loan + profit); `reward_per_share += pool_profit` |

> **Key principle:** The leverage fund is NOT a separate pool with its own balance — it is a **cap** on how much of `lazy_pool.free_balance` can be used for loans. All funds (loan repayment + profit) flow into `lazy_pool.free_balance`. The `leverage_fund_used` counter only tracks how much loan capacity is currently occupied; decrementing it frees up capacity for new loans but does not move money to a separate account. The profit (`pool_profit = pool_received − loan`) is recorded in `earned_balance` (cumulative pool earnings) and distributed to lazy pool investors via `reward_per_share`, exactly like any other pool earnings.

---

## 4. Mathematical Model

### 4.1 CPMM Bet & Token Calculation

All values in milli-VIZ (1 VIZ = 1000 internal units). Precision constants:

```
PRECISION             = 1000          (milli-VIZ — all monetary values in DB)
PRICE_PRECISION       = 1000000       (probability display, 6 decimal places)
LAZY_POOL_PRECISION   = 10^9          (reward_per_share accumulator — internal integer math)
```

| Constant | Value | Used For | Why |
|----------|-------|----------|-----|
| `PRECISION` | 1,000 | All DB monetary columns | 1 VIZ = 1000 milli-VIZ (3 decimal places) |
| `PRICE_PRECISION` | 1,000,000 | Probability display | 6 decimal places (0.000001 precision) |
| `LAZY_POOL_PRECISION` | 10^9 | `reward_per_share` accumulator | Ensures 1 mVIZ profit distributes non-zero across ≤10^6 shares |

> **Precision note:** The lazy pool's `reward_per_share` uses `LAZY_POOL_PRECISION = 10^9` (1,000,000,000). This is an **internal integer math multiplier** for the reward accumulator — not the display precision of shares. Shares are denominated in milli-VIZ (same as `PRECISION = 1000`), displayed as `1.000000` (6 decimal places). The `10^9` multiplier ensures that `reward_per_share` has enough resolution to distribute small profits accurately across many shares: `reward_per_share += leverage_profit_mVIZ × (10^9 / total_shares)`. When a user claims rewards: `user_reward = shares × (reward_per_share − snapshot) / 10^9`. The 10^9 factor is chosen so that even a 1 mVIZ profit on 1,000,000 shares produces a non-zero `reward_per_share` increment (1000 / 10^9 × 10^6 = 1, avoidable dust).

**Binary CPMM (Onix Binary):**

```
k = reserve_a × reserve_b

Bet amount B on outcome A:
  reserve_b' = reserve_b + B
  reserve_a' = floor(k / reserve_b')
  tokens_X   = reserve_a − reserve_a'      (weight received)
  price      = floor(B × PRICE_PRECISION / tokens_X)
```

**Multi LMSR (Onix Multi):**

```
q_i          = net quantity for outcome i
b            = liquidity parameter

price(i)     = exp(q_i / b) / Σ_j exp(q_j / b)

buy_cost(Δ, i) = C(q + Δ·e_i) − C(q)
  where C(q) = b × ln(Σ_j exp(q_j / b))

tokens_for_amount(B, i) = binary_search:
  find max Δ where buy_cost(Δ, i) ≤ B
```

### 4.2 Cancel Value (Position Exit Price)

The cancel value is the VIZ returned when selling tokens back to the AMM via reverse CPMM/LMSR.

**Binary CPMM cancel:**

```
Given: tokens X on outcome A, current reserves (reserve_a, reserve_b), k

Return X tokens to reserve_a:
  reserve_a' = reserve_a + X
  reserve_b' = floor(k / reserve_a')
  cancel_value = reserve_b − reserve_b'

Safety floor:
  if cancel_value ≤ 0 → cancel_value = 0
```

**Multi LMSR cancel:**

```
sell_return(Δ, i) = C(q) − C(q − Δ·e_i)
```

### 4.3 Worst-Case Cancel Value After Opposing Bet

Given a leveraged position of X tokens on outcome A (already placed at total_bet = C + L), and a protocol-enforced maximum bet `M_max` on the opposing side B:

```
M_max = floor(min(reserve_a, reserve_b) × SL% / 100)
```

> **Skewed markets:** On a 95/5 market, `min(reserve_a, reserve_b)` is very small, giving a tiny M_max. This is correct — on skewed markets, even a small opposing bet moves the price significantly, so the slippage cap correctly limits the bet size. The leverage constraint becomes more restrictive on skewed markets, which is the desired behavior (thin opposing side = less leverage available).

The cancel value after an opposing bet of size M on side B:

```
Binary CPMM:
  reserve_a_M  = reserve_a + M
  reserve_b_M  = floor(k / reserve_a_M)
  reserve_a_MX = reserve_a_M + X
  reserve_b_MX = floor(k / reserve_a_MX)
  cancel_value(M) = reserve_b_M − reserve_b_MX

Closed form:
  cancel_value(M) = k × X / ((reserve_a + M) × (reserve_a + M + X))
  (with floor rounding for integer precision)
```

> **Important:** `reserve_a` and `reserve_b` in these formulas refer to the market reserves **after** the leveraged bet of (C + L) has been placed (i.e., `reserve_a'` and `reserve_b'` from Section 4.1), not the original market reserves. `cancel_value_worst(L)` is computed for a position already opened at total_bet = C + L.

This is a monotonically decreasing function of M. Therefore:

```
cancel_value_worst = cancel_value(M_max)
```

**LMSR Multi worst-case:**

For multi-outcome markets, the worst case is a maximum bet concentrated on the **single opposing outcome** that causes the largest decrease in the leveraged position's sell return. This is typically the opposing outcome with the **highest current price** (closest to the leveraged outcome in probability space).

```
For a leveraged position on outcome i with Δ tokens:
  M_max = lmsr_max_bet_amount(b, q, SL%)

  worst_opposing = argmax_j (j ≠ i) decrease_in_sell_return(Δ, i, M_max on j)

  cancel_value_worst = sell_return(Δ, i) after bet of M_max on worst_opposing

  The binary search in Constraint 2 must evaluate all opposing outcomes
  and select the one producing the lowest cancel_value.
```

### 4.4 Liquidation Threshold

```
liquidation_threshold = L × (1 + R% / 100)

where:
  L = loan amount from pool
  R% = pool profit percentage
```

**Example:** L = 1800 VIZ, R% = 10% → liquidation_threshold = 1980 VIZ.

A position is **liquidatable** when:

```
cancel_value_current ≤ liquidation_threshold
```

### 4.5 Safety Margin

The safety margin `S%` is applied **only during position opening** — it prevents positions that are too close to the liquidation edge. Once a position is open, liquidation triggers at the real `liquidation_threshold` (without margin). The margin ensures there is always a buffer between the current cancel value and the threshold at opening time.

```
safe_liquidation_threshold = liquidation_threshold × (1 + S% / 100)
```

Applied when:
- Computing max available leverage (Constraint 2)
- Checking whether a position can be opened (Validation Rule 8)

**Not** applied when:
- Triggering liquidation (Section 5 uses `liquidation_threshold` directly)

The frontend **displays** the liquidation threshold without safety margin (user sees the real number). The frontend adds a subtle note:

> ℹ️ A 1% safety buffer is included during position opening to protect against simultaneous requests.

### 4.6 Maximum Available Leverage — Full Constraint System

For a bettor with collateral C, choosing outcome A, the maximum leverage is the minimum of four independent constraints:

```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
CONSTRAINT 1: Leverage Fund Availability
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  L_max_fund = leverage_fund_available × P% / 100
  leverage_max_fund = L_max_fund / C

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
CONSTRAINT 2: Market Liquidity (Slippage Cap)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  M_max = floor(min(reserve_a, reserve_b) × SL% / 100)

  Find maximum L such that:
    cancel_value_worst(L) ≥ L × (1 + R%/100) × (1 + S%/100) + ε

  where ε = 0 for CPMM binary markets
        ε = max(lmsr_rounding_margin, b × lmsr_b_scale_factor) for LMSR Multi
        (dynamic: accounts for small-b markets where rounding scales with b)
        (see Section 14 — Integer Rounding for rationale)

  where cancel_value_worst(L) = cancel value after max opposing bet,
  computed for a total bet of (C + L) already placed in the reserves.

  This is solved via binary search over L ∈ [0, L_max_fund].

  leverage_max_market = L_market / C
```

> **Defense-in-depth note:** Constraint 2 is deliberately conservative for the **prototype** (non-atomic PHP + MySQL). It protects against the scenario where the full M_max is applied in one shot **without** triggering atomic liquidation (e.g., multi-block price drift where no single bet crosses the threshold). Since atomic liquidation fires *before* the bet that crosses the threshold (Section 5), the actual cancel_value at liquidation is always HIGHER than cancel_value_worst(M_max).
>
> **VIZ DLT relaxation plan:** On VIZ DLT with native atomic liquidation, the full-M_max constraint is unnecessarily conservative and limits leverage to unattractive levels (e.g., 1.1×–1.3× on a deep 500K/500K market when the fund allows 5×). The following relaxation is planned for the VIZ DLT migration:
>
> **Option A: Fixed relaxation factor** (recommended for initial VIZ DLT deployment)
> ```
> M_effective = M_max × VIZ_DLT_M_FACTOR
> leverage_constraint2_cancel_worst = cancel_value after M_effective (not M_max)
> ```
> Where `VIZ_DLT_M_FACTOR` is a committee-configurable parameter (default: 0.5 = 50% of M_max). This doubles the available leverage compared to the prototype while still maintaining a 50% buffer. The atomic liquidation guarantee ensures the pool never loses money — the constraint exists only to prevent positions that are *too close* to the threshold at opening.
>
> **Option B: Dynamic depth factor** (future optimization)
> ```
> depth_factor = f(liquidity_sum, active_positions, price_impact_per_unit)
> M_effective = M_max × depth_factor
> ```
> Where `depth_factor` approaches 1.0 on thin markets (conservative) and 0.3–0.5 on deep markets (relaxed). This adapts to market conditions automatically. Requires more research and simulation.
>
> **Impact comparison** (500K/500K market, collateral = 360 VIZ, F% = 10%, P% = 0.2%):
>
> | Constraint 2 mode | M_used | Max leverage | User experience |
> |-------------------|--------|-------------|----------------|
> | Prototype (full M_max) | 50,000 VIZ | ~2.5× | Overly restrictive on deep markets |
> | VIZ DLT Option A (50%) | 25,000 VIZ | ~4.0× | Good balance of safety and utility |
> | VIZ DLT Option A (30%) | 15,000 VIZ | ~5.0× | Matches fund capacity |
> | No Constraint 2 | 0 | Fund-limited only | Unsafe — no worst-case protection |

```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
CONSTRAINT 3: Maximum Position Size Relative to Market
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  total_bet ≤ liquidity_sum × leverage_max_position_ratio / 100
  → (C + L) ≤ liquidity_sum × POS% / 100

  L_max_size = floor(liquidity_sum × POS% / 100) − C
  if L_max_size < 0: leverage unavailable (collateral alone exceeds limit)

  leverage_max_size = L_max_size / C

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
CONSTRAINT 4: Market Minimum Liquidity
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  if liquidity_sum < leverage_min_market_liquidity:
      leverage_max = 0  (leverage unavailable on this market)

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
FINAL: Maximum Leverage
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

  leverage_max = floor(min(leverage_max_fund,
                           leverage_max_market,
                           leverage_max_size) × 100) / 100
  (rounded down to 0.01× precision for slider)
```

If `leverage_max < 1.1`, leverage is **unavailable** for this market/bettor combination.

---

## 5. Atomic Liquidation at Block Level

The core risk-elimination mechanism: liquidation happens **before** the opposing bet changes reserves.

### Block Processing Order (VIZ DLT Plugin)

```
┌─ Block N ─────────────────────────────────────────────────┐
│                                                           │
│  For each transaction in block, in order:                 │
│                                                           │
│  CASE A: pm_place_bet(outcome≠leveraged_outcome)          │
│  ─────────────────────────────────────────────────────     │
│  Opposing bet moves price against leveraged position.     │
│  Liquidate BEFORE the bet (pool guaranteed whole).        │
│                                                           │
│    1. SCAN: find all leveraged positions that would be    │
│       liquidated by this bet.                             │
│       (For multi-outcome: ANY bet not on the leveraged    │
│        outcome triggers the check.)                       │
│                                                           │
│       For each leveraged position:                        │
│         cancel_after = cancel_value_after_bet(M, position)│
│         if cancel_after ≤ position.liquidation_threshold: │
│           → mark for pre-liquidation                      │
│                                                           │
│    2. REPEAT (cascade loop):                              │
│       a. EXECUTE one pre-liquidation (virtual operation):  │
│          → pm_leverage_liquidate(position)                 │
│          → Cancel the bet at CURRENT reserves              │
│          → Distribute: pool gets min(cancel_value, L+R)   │
│          → Bettor gets remainder (if any)                  │
│          → Return L to leverage_fund_available             │
│       b. Re-read market reserves (changed by liquidation)  │
│       c. Re-scan all active leveraged positions at new     │
│          reserves. If any new position ≤ threshold:        │
│          → add to pre-liquidation queue, go to (a)        │
│       UNTIL no more positions below threshold.             │
│       (See “Liquidation Cascade” below.)                  │
│                                                           │
│    3. EXECUTE the original pm_place_bet                   │
│       (at reserves after all pre-liquidations)             │
│                                                           │
│  ─────────────────────────────────────────────────────     │
│                                                           │
│  CASE B: pm_cancel_bet that moves price against           │
│          a leveraged position                             │
│          (same outcome for CPMM; any outcome for LMSR)    │
│  ─────────────────────────────────────────────────────     │
│  Cancel-bet moves price against leveraged position.       │
│  Execute cancel-bet FIRST (transaction initiator gets     │
│  the exact price they submitted at — not penalized by     │
│  someone else's leveraged position). Then liquidate.     │
│                                                           │
│    1. EXECUTE pm_cancel_bet at current reserves           │
│       Cancel-bettor receives exact expected payout.       │
│                                                           │
│    2. REPEAT (cascade loop):                              │
│       a. SCAN: re-evaluate all active leveraged positions  │
│          on this outcome at the CURRENT reserves.          │
│          (For multi-outcome: check ALL leveraged positions,│
│           not just on the same outcome.)                  │
│       b. If any position has cancel_value ≤ threshold:    │
│          → EXECUTE post-liquidation (virtual operation):   │
│            → pm_leverage_liquidate(position)               │
│            → Cancel the bet at current reserves            │
│            → Distribute: pool gets cancel_value (may be   │
│              < L+R — see "Bad debt" below)                │
│            → Bettor gets 0 (cancel_value ≤ threshold)     │
│            → Return L to leverage_fund_available           │
│          → Re-read market reserves, go to (a)             │
│       UNTIL no more positions below threshold.             │
│       (See “Liquidation Cascade” below.)                  │
│                                                           │
│       Bad debt handling (per liquidation):                │
│         shortfall = liquidation_threshold − cancel_value  │
│         if shortfall > 0:                                 │
│           lazy_pool.free_balance -= shortfall             │
│           (absorbed by pool — bounded, rare event)        │
│                                                           │
│  ELSE:                                                    │
│    Process normally                                       │
│                                                           │
└───────────────────────────────────────────────────────────┘
```

**Key invariant (Case A — opposing bet):** Step 2 liquidates at pre-bet reserves. The opposing bettor in Step 3 gets a marginally better price (the market moved toward A because X tokens were returned to reserve_a). The pool is guaranteed whole — `cancel_value ≥ liquidation_threshold` at pre-bet reserves. The cascade loop in Step 2 ensures that liquidating one position (which changes reserves) doesn't leave other positions underwater — each iteration re-evaluates all remaining positions at the updated reserves.

**Key invariant (Case B — cancel-bet):** The cancel-bettor gets the exact price at pre-liquidation reserves. Liquidation happens AFTER at potentially lower `cancel_value`. If `cancel_value < liquidation_threshold`, the pool absorbs the shortfall as bad debt. This is a bounded, rare event — the position was already near liquidation, and the cancel-bet just happened to be the final push. The transaction initiator must never be penalized by another user's leveraged position. The cascade loop ensures that if liquidating one position pushes others below threshold (due to reserve changes), those are caught immediately rather than left as hidden bad debt.

### Liquidation Rebalancing Spread (MEV Note)

When a leveraged position on A is liquidated (tokens returned to reserve_a), the opposing bet on B in Step 3 executes at a slightly better price than it would have without the liquidation. This "rebalancing spread" creates a small MEV incentive:

```
For a position of size X tokens (total_bet = C + L) on a market with
liquidity_sum = LS:

  Approximate spread = (C + L) / LS  (fraction of market)

  With POS% = 5%:  spread ≈ 5%  (price improvement for opposing bettor)

  This means the opposing bettor gets ~5% better execution than they
  would have without the liquidation.
```

**Assessment:** This is NOT a security risk for the pool (the pool recovers L + R regardless). It's a minor information asymmetry — bots that can predict which bets will trigger liquidations get a small advantage. The magnitude is bounded by POS% and is acceptable for the prototype.

**Known limitation (prototype):** The opposing bettor who triggers liquidation automatically receives a ~POS% price improvement — they don't need to do anything special. This creates a systematic incentive for bots to place opposing bets that deliberately target leveraged positions near their liquidation threshold. The bot's bet on B triggers liquidation, then executes at post-liquidation price (better for the bot). This is a zero-sum transfer from the liquidated bettor to the opposing bettor — the pool is unaffected.

**Mitigation on VIZ DLT:** Fix the opposing bet's price at pre-liquidation reserves ("price lock" at the time the bet was submitted). This eliminates the spread advantage entirely — the opposing bettor pays the price they expected, and the liquidation rebalancing does not create a systematic arbitrage opportunity.

### Liquidation Sandwich MEV

There is a second MEV vector related to liquidation — an attacker can **front-run the liquidating bet** with a bet on the leveraged outcome, slightly reducing the cancel_value received by the liquidated position:

```
Mempool:
  Tx 1: pm_place_bet(B, amount)  ← will trigger liquidation of position on A
  Tx 2: pm_place_bet(A, amount2) ← unrelated bet on A

Attacker sees Tx 1 in mempool, inserts their own bet on A BEFORE it:
  Block order: [attacker bet on A] → [Tx 1 on B — triggers liquidation] → [Tx 2 on A]

Step-by-step:
  1. Attacker's bet on A executes normally (buys A tokens at current price)
  2. Tx 1 (bet on B) arrives → atomic check fires → position liquidated
     BUT: liquidation cancel_value is computed at reserves AFTER attacker's bet
     (attacker's bet on A shifted reserves slightly, reducing cancel_value)
  3. Liquidated position returns X tokens to reserve_a → market shifts back toward A
  4. Tx 2 (bet on A) executes at post-liquidation price (slightly better for Tx 2)

Net effect on liquidated position:
  cancel_value is slightly LOWER than it would be without the attacker's bet
  The attacker's bet on A shifted the price, reducing what the liquidated
  position gets back.

Maximum impact:
  Attacker's bet is bounded by SL% (leverage_max_slippage_percent)
  Maximum cancel_value reduction ≈ SL% × (C + L) / liquidity_sum
  With SL% = 10% and POS% = 5%:  impact ≈ 0.5% of total_bet
  On a 2160 VIZ position:  ≈ 10.8 VIZ reduction in cancel_value
```

**Assessment:**

- **Pool risk:** NONE. The pool recovers L × (1 + R%) regardless — the cancel_value reduction comes entirely from the bettor's portion (`bettor_received`).
- **Bettor risk:** Small. The maximum impact is bounded by `SL% × (C + L) / LS`. On typical markets this is <1 VIZ. The bettor was already being liquidated (position near threshold), so the additional loss is marginal.
- **Mitigation on VIZ DLT:** The delegate can lock the liquidation cancel_value at the reserves that existed **before** any front-running transaction in the same block. This eliminates the attack entirely at the consensus level. The liquidation price is computed at the reserves from the *previous block*, not at the intra-block reserves.
- **Prototype:** Acceptable without mitigation. The impact is bounded and the bettor was already near liquidation.

> **Comparison with opening MEV (Section 12):** The `max_slippage_percent` protection for `pm_leverage_open` is **bettor-initiated** — the user chooses their tolerance. Liquidation sandwich MEV has no user opt-in because liquidation is **protocol-initiated**. Mitigation must come from the protocol level (price lock at previous block reserves on VIZ DLT).

### Liquidation Cascade (Long Squeeze)

When a leveraged position on outcome A is liquidated, its tokens are returned to the AMM reserves (`reserve_a ↑`, `reserve_b ↓` for CPMM). This makes outcome A cheaper — the same directional move as the trigger that caused the liquidation. Other leveraged positions **on the same outcome** see their `cancel_value` decrease, potentially falling below their own `liquidation_threshold`.

This creates a **cascade** — a self-reinforcing chain of liquidations known as a "long squeeze" (analogous to a short squeeze in traditional markets, but in the opposite direction: longs are force-closed as the price drops, amplifying the downward move).

**Why a cascade (not a short squeeze):**

A true short squeeze is an *upward* cascade — shorts forced to buy back → price UP → more shorts forced. In this system, the cascade is a *downward* cascade — longs forced to sell → price DOWN → more longs forced. An upward cascade is **structurally impossible** because liquidating a position on A returns tokens to `reserve_a` (increasing supply), making A *cheaper*, not more expensive. For the opposing side B, `cancel_value` actually *increases* after A's liquidation — B positions are strengthened, not weakened.

**Why the cascade is one-directional:**

| Event | Effect on same-side (A) | Effect on opposite side (B) |
|-------|------------------------|-----------------------------|
| Liquidate position on A | `reserve_a ↑`, `reserve_b ↓` → cancel_value ↓ (worse) | cancel_value ↑ (better) |
| Liquidate position on B | `reserve_b ↑`, `reserve_a ↓` → cancel_value ↓ (worse) | cancel_value ↑ (better) |

Conclusion: liquidating any position only harms **same-side** positions. Cross-side positions are always strengthened. The cascade only flows in one direction — toward more same-side liquidations.

**Cascade scenario:**

```
Market: CPMM binary, A at ~0.55, B at ~0.45

Three leveraged positions on outcome A:
  P1: cancel_value = 1,990 VIZ, threshold = 1,980 VIZ  (margin: 10)
  P2: cancel_value = 2,010 VIZ, threshold = 2,000 VIZ  (margin: 10)
  P3: cancel_value = 2,100 VIZ, threshold = 1,990 VIZ  (margin: 110)

Opposing bet on B arrives → triggers Case A check:
  Iteration 1: P1 below threshold → liquidate at pre-bet reserves
    → P1's tokens return to reserve_a → A gets cheaper
    → P2's cancel_value drops from 2,010 → 1,995 (below P2's threshold!)
  Iteration 2: P2 below threshold → liquidate at current reserves
    → P2's tokens return to reserve_a → A gets even cheaper
    → P3's cancel_value drops from 2,100 → 2,050 (still above threshold)
  No more positions below threshold → cascade terminates

Result: P1 and P2 liquidated, P3 survives (still above threshold)
```

**Guaranteed termination:** The cascade loop always terminates because:
1. Each iteration liquidates ≥1 position (otherwise the loop breaks)
2. There are a finite number of active positions
3. Positions can only be liquidated once (status changes from 0 to 1/4)

In practice, with `max_leverage_per_position_percent = 5%`, each position's price impact is at most 5% of the market. Typical markets have 2–5 leveraged positions, so the cascade rarely exceeds 2–3 iterations.

**Pool safety in cascade:**

- **Case A (opposing bet):** Every liquidation in the cascade occurs at pre-bet reserves. Each individual liquidation satisfies `cancel_value ≥ liquidation_threshold`. The pool is guaranteed whole on every iteration. The cascade is purely beneficial for the pool — more positions are caught before they can deteriorate further.
- **Case B (cancel-bet):** Each successive liquidation may have a larger shortfall (bad debt) because reserves move further from the position's opening price. However, the total bad debt across the entire cascade is bounded by: `∑ shortfalls ≤ n × cancel_value_before × SL% / 100`, where `n` is the number of cascaded positions. This is because each position was at `cancel_value ≥ threshold` before the cascade began, and each liquidation's price impact is bounded by the slippage cap.

**Implementation:** The cascade is implemented as `leverage_cascade_liquidate()` in `module/lazy_pool_helpers.php`. It accepts a `max_iterations` parameter (default 50) as a safety limit. The function is called by:
- **Cancel-bet handler** (`api.php`, Case B): after the cancel-bet executes, with `reason = 'cancel_bet_liquidated'`
- **Cron Job 11** (`cron_worker.php`): when force-closing positions near expiration, with `reason = 'force_close'`
- **Case A (opposing bet)** on VIZ DLT: the `block_pre_apply` hook will implement the cascade loop as part of the plugin

**No upward cascade (short squeeze) is possible:** Liquidating a position returns tokens to the AMM, increasing supply and reducing the token's value. This can only push same-side positions further toward liquidation (downward), never pull them back from it (upward). Opposite-side positions are always improved by the liquidation.

### Virtual Operation: pm_leverage_liquidate

Generated deterministically by the plugin when a leveraged position must be closed:

```
pm_leverage_liquidate {
  position_id:   uint64,
  market_id:     uint64,
  cancel_value:  uint64,  // VIZ returned by reverse CPMM/LMSR
  pool_received: uint64,  // min(cancel_value, L × (1 + R%/100))
  bettor_received: uint64, // max(0, cancel_value − pool_received)
  reason:        enum { opposing_bet_threshold, cancel_bet_threshold, expiration_approaching }
}
```

### Cancel-Bet Liquidation Waterfall

A `pm_cancel_bet` on the **same outcome** as a leveraged position moves the price against that position — returning tokens to the AMM increases supply and reduces the per-token sell value. This is the same directional effect as an opposing bet. Without the post-check, a large cancel-bet could push a leveraged position below its liquidation threshold with no liquidation triggered.

**Principle:** The transaction initiator must never get a worse price because of someone else's leveraged position. The cancel-bet executes FIRST at current reserves. Liquidation happens AFTER.

**Note:** The post-liquidation check now uses a **cascade loop** (see [Liquidation Cascade](#liquidation-cascade-long-squeeze) above). Each liquidation changes reserves, potentially pushing other positions below threshold. The loop re-evaluates all remaining positions after each liquidation until stable.

**Waterfall scenario:**

```
Market: CPMM binary, reserve_a = 550,000, reserve_b = 450,000
  (A at ~0.55, B at ~0.45)

Leveraged position on A:
  collateral = 360 VIZ, loan = 1,800 VIZ (5× boost)
  cancel_value = 2,100 VIZ, liquidation_threshold = 1,980 VIZ
  (margin above threshold: 120 VIZ)

User cancels bet on A — returns 500,000 mVIZ worth of A tokens:
  reserve_a += X_tokens (tokens returned)
  reserve_b -= VIZ_paid_out

Effect on leveraged position:
  cancel_value drops from 2,100 → 1,950 VIZ
  1,950 < liquidation_threshold (1,980) → POSITION LIQUIDATED
```

**With Case B (cancel-bet first, then cascade liquidation):**

```
Block processing:
  Transaction: pm_cancel_bet(outcome=A)
  ──────────────────────────────────────
  Step 1: EXECUTE pm_cancel_bet
    Cancel-bettor returns A tokens, receives VIZ at CURRENT reserves.
    Cancel-bettor gets the exact expected payout — not affected by
    the leveraged position at all.
    Reserves now: reserve_a' > reserve_a, reserve_b' < reserve_b

  Step 2: CASCADE LOOP
    Iteration 1:
      Re-evaluate all leveraged positions on A at CURRENT reserves.
      Position P1: cancel_value = 1,950 ≤ 1,980 → LIQUIDATE
        cancel_value = 1,950 VIZ
        pool_received = 1,950 VIZ (cancel_value < threshold → all to pool)
        bettor_received = 0
        shortfall = 1,980 − 1,950 = 30 VIZ (bad debt)
        lazy_pool.free_balance -= 30 VIZ
        leverage_fund_used -= 1,800 VIZ (loan returned)
      → Reserves changed (P1 tokens returned to reserve_a)

    Iteration 2:
      Re-read market reserves. Re-evaluate remaining positions.
      No more positions below threshold → CASCADE TERMINATES

  (If there were a P2 near threshold, iteration 2 would catch it too.)
```

**Bad debt analysis:**

When liquidation happens after the cancel-bet (Case B), `cancel_value` may be below `liquidation_threshold`. The shortfall is covered by the lazy pool's `free_balance`:

```
shortfall = liquidation_threshold − cancel_value
pool_received = cancel_value  (entire amount to pool)
bettor_received = 0
lazy_pool.free_balance -= shortfall  (bad debt charge)
lazy_pool.earned_balance unchanged  (no earnings on a loss)
```

This bad debt is **bounded and rare**:
- **Bounded:** The position was at `cancel_value ≥ liquidation_threshold` before the cancel-bet. The cancel-bet's price impact is limited by the slippage cap (SL%). So `shortfall ≤ cancel_value_before × SL% / 100`.
- **Rare:** Only occurs when a position is near liquidation AND a cancel-bet on the same outcome is the trigger. Most positions are liquidated by opposing bets (Case A, pool guaranteed whole).
- **Acceptable:** The pool earns R% on every position. Occasional bad debt from cancel-bet-triggered liquidations is more than offset by the pool's aggregate earnings. This is the cost of fairness to transaction initiators.

**Why this order (cancel-bet first) is correct:**

The cancel-bettor is an independent market participant exercising a normal right (sell tokens back). They should not be penalized — receive less VIZ — because someone else's leveraged position happens to be near liquidation. The leveraged position's near-threshold state is the bettor's own risk, not the cancel-bettor's problem.

In contrast, for opposing bets (Case A), the opposing bettor actually *benefits* from the pre-liquidation order (gets better price). There is no fairness issue to fix — only a small MEV concern (addressed by price lock on VIZ DLT).

**CPMM vs. LMSR multi-outcome:**
- **CPMM binary:** Only cancel-bets on the *same* outcome as the leveraged position can push it toward liquidation. Cancel-bets on the opposing outcome *increase* the leveraged position's cancel_value (beneficial).
- **LMSR multi-outcome:** ANY cancel-bet can affect the leveraged outcome's marginal price, so ALL cancel-bets are processed as Case B (execute first, then check and liquidate).

---

## 6. Liquidation Outcome Distribution

### Case 1: cancel_value ≥ liquidation_threshold (normal case)

```
pool_obligation = L × (1 + R% / 100)   (= liquidation_threshold)
pool_received   = min(cancel_value, pool_obligation)
bettor_received = cancel_value − pool_received

In this case, cancel_value ≥ pool_obligation, so:
  pool_received   = pool_obligation  = loan + full profit
  bettor_received = cancel_value − pool_obligation  (≥ 0)
```

### Case 2: cancel_value < liquidation_threshold (mathematically impossible during betting period)

```
This case is prevented by the atomic block-level check (Section 5).
The position is always liquidated BEFORE the opposing bet that would push
cancel_value below the threshold. Liquidation executes at pre-bet reserves,
where cancel_value ≥ liquidation_threshold by construction.

If this case were to occur (e.g., due to a consensus-layer bug):
  pool_received   = min(cancel_value, pool_obligation) = cancel_value
  bettor_received = 0
  → This would be a protocol fault, not an economic risk.
```

---

## 7. Resolution Handling

### The CPMM/LMSR Reality

When a leveraged bet of (C + L) is placed on outcome A:
1. The amount (C + L) is added to the AMM reserves immediately.
2. The bettor receives tokens X in return.
3. At resolution, if A wins, tokens are redeemable. If A loses, tokens are worth 0.

**The bet amount is already in the AMM reserves — it cannot be "reclaimed" from a forfeited pool.** The "priority claim on forfeited stakes" model from earlier drafts was incorrect. In CPMM/LMSR, there is no separate forfeited pool — value is distributed through the token pricing mechanism.

### Correct Model: Co-Investment

The leveraged position is a **co-investment** between the bettor (C) and the pool (L):

```
┌─ At opening ──────────────────────────────────────────┐
│  Bettor puts in:  C (collateral)                       │
│  Pool puts in:    L (loan)                             │
│  Together:        C + L → placed on outcome A via AMM  │
│  Tokens received: X                                    │
└────────────────────────────────────────────────────────┘

┌─ At resolution — A wins ──────────────────────────────┐
│  Tokens X are worth: payout = redeem(X, outcome=A)    │
│                                                        │
│  Pool receives:    min(payout, L × (1 + R%/100))      │
│                    (priority claim — pool gets paid 1st)│
│  Bettor receives:  max(0, payout − L × (1 + R%/100))  │
│  leverage_fund_used -= L                               │
│  Position status → resolved_won                        │
└────────────────────────────────────────────────────────┘

┌─ At resolution — A loses ─────────────────────────────┐
│  Tokens X are worth: 0                                 │
│                                                        │
│  Pool receives:    0        (loan L is lost)           │
│  Bettor receives:  0        (collateral C is lost)     │
│  leverage_fund_used -= L                               │
│  Position status → resolved_lost                       │
│                                                        │
│  Pool loss = L                                         │
│  → This is OUTCOME RISK — the pool co-invested on     │
│    the bettor's chosen outcome and lost.              │
└────────────────────────────────────────────────────────┘
```

### Outcome Risk Mitigation: Force-Close Before Expiration

Outcome risk is mitigated by Cron Job 11 (Section 15), which force-closes **all** leveraged positions before the betting period expires. This converts outcome risk into price-movement risk:

```
Before expiration:
  cancel-bet the position → receive cancel_value
  cancel_value ≥ liquidation_threshold (by atomic check guarantee)
  Pool receives: L × (1 + R%)    (full recovery)
  Bettor receives: cancel_value − L × (1 + R%)

No outcome risk remains — the pool's price-movement guarantee (structural zero)
covers every position that is force-closed before resolution.
```

This is why the expiration buffer is critical: it ensures the pool recovers from every leveraged position via cancel-bet rather than leaving outcome risk open at resolution.

### Market Status Trigger for Force-Close

When a market transitions to a status where new leverage bets are no longer accepted (e.g., `market.status` changes from `active` to `closing`), the protocol blocks new boosted positions **immediately** but does NOT force-close existing positions right away. A grace period protects users from instant loss.

**Market status timeline:**

```
active  →  closing  →  (grace period)  →  force-close all
   │          │                              │
   │          │                              └─ All remaining positions
   │          │                                 force-closed at current price
   │          │
   │          └─ New boost positions BLOCKED immediately
   │             Existing positions continue (can close voluntarily)
   │             Grace period starts = leverage_expiration_buffer_hours
   │
   └─ Normal operation: new boost positions allowed
```

**Why a grace period?** Without it, a committee member could set `closing` status 10 seconds after a user opens a position. The position would be force-closed immediately at a guaranteed loss (the pool profit charge of L × R%), even though the market didn't move against the bettor at all. This is unfair and would destroy user trust.

**Trigger conditions:**

```
When market.status changes to 'closing':
  1. IMMEDIATELY: block all new pm_leverage_open on this market
  2. IMMEDIATELY: notify all users with active positions (frontend + optional push)
  3. AFTER grace period: force-close all remaining leveraged positions

Grace period = leverage_expiration_buffer_hours (default: 24 hours)
  - Same parameter used for time-based expiration buffer
  - Gives users time to close voluntarily (potentially at a better price)
  - Gives the market time to move in the bettor's favor

Alternative: force-close when betting_expiration − leverage_expiration_buffer_hours
  is reached (Cron Job 11), whichever comes FIRST.
```

**Core principle — pool always recovers, bettor bears their own risk:**

| Position State | Pool | Bettor |
|----------------|------|--------|
| In profit (cancel_value > liquidation_threshold by margin) | Receives L × (1 + R%) — full loan + profit | Receives cancel_value − L × (1 + R%) — **keeps profit** |
| Near liquidation (cancel_value ≈ liquidation_threshold) | Receives L × (1 + R%) — full loan + profit | Receives ≈ 0 — **bears the loss** |

> **Key invariant:** The pool recovers its collateral (loan L) and profit (L × R%) in **every** case. If the position is profitable for the bettor, it's profitable for everyone. If the position is at a loss, only the bettor who placed the bet bears that loss — the pool is never exposed.

**Processing order — after grace period expires:**

```
1. Find all active leveraged positions on this market (status = 0)
2. For each position:
   a. Calculate current cancel_value at market reserves
   b. Execute force-close:
      pool_received   = min(cancel_value, L × (1 + R%/100))
      bettor_received = max(0, cancel_value − pool_received)
   c. lazy_pool.free_balance += pool_received
      lazy_pool.earned_balance += (pool_received − loan)  ← track cumulative earnings
      lazy_pool.reward_per_share += (pool_received − loan) × 10^9 / total_shares  ← profit to LP investors
      leverage_fund_used -= loan  ← free up loan capacity
   d. Credit bettor_received to bettor's balance
   e. Position status → closed_voluntary
3. No leveraged positions remain active on the market
```

**Why force-close at all (instead of letting profitable positions ride to resolution)?** Allowing positions to survive past the grace period would reintroduce outcome risk for the pool — if the bettor's outcome loses at resolution, the pool loses L. The force-close converts outcome risk into price-movement risk (which is zero by the structural guarantee). The grace period ensures this conversion happens fairly: users get a reasonable window to capture profit or limit losses voluntarily, and the pool is protected from outcome exposure.

**Frontend notification when status → 'closing':**

```
┌─────────────────────────────────────────────────────────────┐
│  ⚠️ MARKET IS CLOSING                                        │
│                                                             │
│  This market is no longer accepting new boosted positions.  │
│  Your boosted position(s) will be auto-closed in:           │
│                                                             │
│       23h 45m remaining                                     │
│                                                             │
│  You can close voluntarily now to lock in your current      │
│  value, or wait for the market to move in your favor.       │
│                                                             │
│  [ CLOSE MY POSITION NOW ]    [ I'LL WAIT ]                │
└─────────────────────────────────────────────────────────────┘
```

### Expected Profit Model

With force-close before expiration, the pool's expected profit per leveraged position:

```
If the market moves against the bettor (cancel_value drops toward threshold):
  → Atomic liquidation or force-close fires
  → Pool receives L × (1 + R%)  ✅ full profit

If the market moves in the bettor's favor:
  → Force-close at cancel_value > threshold
  → Pool receives L × (1 + R%)  ✅ full profit
  → Bettor receives cancel_value − L × (1 + R%) (bettor profits too)

In both cases: pool profit = L × R%  (guaranteed by force-close + atomic check)
```

> **Key distinction:** The pool's "structural zero" guarantee applies to **price-movement risk** during the betting period. Outcome risk at resolution is **eliminated** by force-closing all positions before expiration. The only scenario where the pool loses L is if force-close fails (cron malfunction) or a position is somehow still open at resolution.

---

## 8. Protocol Settings

All parameters are committee-governed (delegate median vote on VIZ DLT). Stored in `settings` table in the prototype.

| Key | Default | Unit | Description |
|-----|---------|------|-------------|
| `leverage_fund_percent` | 10 | % | Percentage of lazy pool `free_balance` allocated to leverage fund |
| `max_leverage_per_position_percent` | 0.2 | % | Maximum percentage of **available** leverage fund per single position |
| `leverage_pool_profit_percent` | 10 | % | Pool's fixed profit on each leveraged loan |
| `leverage_safety_margin_percent` | 1 | % | Safety buffer applied during position opening |
| `leverage_max_slippage_percent` | 10 | % | Maximum price impact per bet (protocol-enforced) |
| `leverage_min_market_liquidity` | 5000 | VIZ | Markets below this liquidity: leverage disabled |
| `leverage_max_position_ratio` | 5 | % | Maximum leveraged position as % of market liquidity_sum |
| `leverage_expiration_buffer_hours` | 24 | hours | Leverage disabled N hours before betting_expiration |
| `lmsr_rounding_margin` | 50 | mVIZ | Rounding safety margin for LMSR Multi Constraint 2 (0 for CPMM). Floor value for dynamic ε |
| `lmsr_b_scale_factor` | 0.001 | ratio | LMSR Multi — ε = max(lmsr_rounding_margin, b × factor). Covers small-b edge cases |
| `viz_dlt_m_factor` | 1.0 (prototype) / 0.5 (VIZ DLT) | ratio | **REQUIRED for VIZ DLT.** M_effective = M_max × factor. 1.0 = full M_max (prototype, conservative); 0.5 = 50% M_max (VIZ DLT, relaxed). Without this parameter, leverage is uncompetitively low on most markets (1.1×–1.3× on deep 500K/500K markets). See Constraint 2 Defense-in-depth note |
| `conversion_profit_cost` | 50 | % | Percentage of bettor's unrealized profit charged as conversion fee when converting boosted position to normal bet. 0% = free conversion; 50% = bettor pays half their unrealized gain |

> **Note:** `lmsr_rounding_margin`, `lmsr_b_scale_factor`, and `viz_dlt_m_factor` are specified in the protocol design but not yet included in the prototype migration (`005_leverage.sql`). They will be added when LMSR leverage support is implemented and when the VIZ DLT migration begins. The prototype uses `viz_dlt_m_factor = 1.0` (full M_max) hardcoded in the constraint calculation.

---

## 9. Database Schema

### New Table: `leveraged_positions`

```sql
CREATE TABLE `leveraged_positions` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `market` bigint NOT NULL,
  `user` bigint NOT NULL,
  `outcome_index` tinyint NOT NULL DEFAULT 0 COMMENT '0=A, 1=B, or index for multi',
  `collateral` bigint NOT NULL COMMENT 'bettor contribution, milli-VIZ',
  `loan` bigint NOT NULL COMMENT 'pool loan, milli-VIZ',
  `total_bet` bigint NOT NULL COMMENT 'collateral + loan, milli-VIZ',
  `tokens` bigint NOT NULL COMMENT 'weight/tokens received from AMM',
  `bet_id` bigint NOT NULL COMMENT 'FK to bets table',
  `pool_profit` bigint NOT NULL COMMENT 'loan × R% / 100, milli-VIZ',
  `liquidation_threshold` bigint NOT NULL COMMENT 'loan + pool_profit, milli-VIZ',
  `status` tinyint NOT NULL DEFAULT 0 COMMENT '0=active, 1=liquidated, 2=resolved_won, 3=resolved_lost, 4=closed_voluntary, 5=converted',
  `liquidated_at` int NOT NULL DEFAULT 0 COMMENT 'unix timestamp',
  `liquidated_by_bet` bigint NOT NULL DEFAULT 0 COMMENT 'bet_id that triggered liquidation',
  `cancel_value_at_liquidation` bigint NOT NULL DEFAULT 0,
  `pool_received` bigint NOT NULL DEFAULT 0,
  `bettor_received` bigint NOT NULL DEFAULT 0,
  `time` int NOT NULL,
  `update` int NOT NULL,
  PRIMARY KEY (`id`),
  KEY `market_status` (`market`,`status`),
  KEY `user_status` (`user`,`status`),
  KEY `bet_id` (`bet_id`)
);
```

### New Columns on `lazy_pool`

```sql
ALTER TABLE `lazy_pool`
  ADD COLUMN `leverage_fund_used` bigint NOT NULL DEFAULT 0 COMMENT 'Total active leveraged loans',
  ADD COLUMN `earned_balance` bigint NOT NULL DEFAULT 0 COMMENT 'Cumulative lifetime earnings (LP profit + leverage profit + penalties). Monotonically non-decreasing';
```

> **Note:** `earned_balance` is also incremented by LP profits (market resolution) and emergency withdrawal penalties, not just leverage profits. It tracks the pool's **cumulative lifetime earnings** from all sources. See [Lazy Pool Properties](../../docs/lazy-pool-properties.md) for complete state transition documentation.

### New History Types

> **Conflict check:** Existing types in the codebase are 0–20 (bet_place, bet_cancel, liquidity_add/withdraw, fees, penalties, disputes). Types 30–34 have no overlap.

| Type | Name | Description |
|------|------|-------------|
| 30 | `leverage_open` | Leveraged position opened |
| 31 | `leverage_liquidate` | Position force-closed by protocol |
| 32 | `leverage_resolve_win` | Position closed at resolution (bettor won) |
| 33 | `leverage_resolve_loss` | Position closed at resolution (bettor lost) |
| 34 | `leverage_convert` | Leveraged position converted to normal bet |

> **Implementation note:** In the current prototype, the `leverage_force_close_position()` function uses type 30 for both voluntary close and force-close events (rather than a dedicated close type). This should be updated to use type 31 (liquidate) for all close events, or a new type should be added for voluntary close. The `leverage-open` API correctly uses type 30, and the `leverage-convert` API correctly uses type 34. Types 32 and 33 will be used when leverage resolution handling is implemented.

### Migration

Database changes should be packaged as `migrations/005_leverage.sql` following the existing migration convention (001–004).

---

## 10. API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `leverage-preview` | POST | Pre-calculate leverage options for a market. Returns constraints and slider data |
| `leverage-open` | POST | Open a leveraged position on a market |
| `leverage-close` | POST | Bettor voluntarily closes their leveraged position (early exit) |
| `leverage-close-preview` | POST | Pre-calculate the close result: what bettor receives, what pool receives, loss breakdown |
| `leverage-convert-preview` | POST | Pre-calculate the convert result: total payment, conversion fee, comparison with voluntary close |
| `leverage-convert` | POST | Convert leveraged position to normal bet (bettor pays buyout from balance) |
| `leverage-info` | POST | Get all active leveraged positions for current user |
| `leverage-pool-state` | POST | Get leverage fund state (available, used, total) |

### `leverage-preview` — Request

```json
{
  "method": "leverage-preview",
  "market_id": 42,
  "collateral": 360000,
  "outcome_index": 0
}
```

### `leverage-preview` — Response

```json
{
  "ok": true,
  "market_liquidity": 5000000,
  "slippage_cap_viz": 500,
  "leverage_fund_total": 1000000000,
  "leverage_fund_available": 900000000,
  "max_loan_from_fund": 1800000,
  "max_loan_from_market": 2250000,
  "max_loan_from_size": 2140000,
  "max_loan": 1800000,
  "max_leverage": 5.0,
  "pool_profit_percent": 10,
  "safety_margin_percent": 1,
  "recommended_slippage_percent": 2,
  "danger_zone_leverage": 4.3,
  "auto_close_date": "2025-03-14T18:00:00Z",
  "unavailable_reason": null,
  "failed_constraints": [],
  "slider_stops": [
    {
      "leverage": 1.0,
      "loan": 0,
      "total_bet": 360000,
      "pool_profit": 0,
      "liquidation_cancel_value": 0,
      "current_cancel_value": 359500,
      "worst_case_cancel_value": 340000,
      "expected_tokens": 359800,
      "min_tokens_at_2pct": 352604,
      "safe": true
    },
    {
      "leverage": 2.0,
      "loan": 360000,
      "total_bet": 720000,
      "pool_profit": 36000,
      "liquidation_cancel_value": 396000,
      "current_cancel_value": 718500,
      "worst_case_cancel_value": 560000,
      "expected_tokens": 718200,
      "min_tokens_at_2pct": 703836,
      "safe": true
    },
    {
      "leverage": 5.0,
      "loan": 1800000,
      "total_bet": 2160000,
      "pool_profit": 180000,
      "liquidation_cancel_value": 1980000,
      "current_cancel_value": 2155000,
      "worst_case_cancel_value": 1990000,
      "expected_tokens": 2150300,
      "min_tokens_at_2pct": 2107294,
      "safe": true
    }
  ]
}
```

### `leverage-preview` — Response (unavailable)

When leverage is unavailable, `max_leverage` is < 1.1, `slider_stops` is empty, and `unavailable_reason` + `failed_constraints` explain why:

```json
{
  "ok": true,
  "market_liquidity": 1001000,
  "slippage_cap_viz": 1,
  "leverage_fund_total": 1000000000,
  "leverage_fund_available": 900000000,
  "max_loan_from_fund": 1800000,
  "max_loan_from_market": 0,
  "max_loan_from_size": 14050,
  "max_loan": 0,
  "max_leverage": 0.0,
  "pool_profit_percent": 10,
  "safety_margin_percent": 1,
  "recommended_slippage_percent": 2,
  "unavailable_reason": "Opposing side too thin for leveraged positions. Maximum safe opposing bet (M_max) = 1 VIZ is insufficient — any loan would fail the worst-case safety check.",
  "failed_constraints": [
    {
      "constraint": 2,
      "name": "market_liquidity",
      "detail": "M_max = 1 VIZ (reserve_b = 1 VIZ × 10% slippage cap). cancel_value_worst = 0 for any loan > 0. Even the smallest leveraged position cannot survive a maximum opposing bet."
    }
  ],
  "slider_stops": []
}
```

**`failed_constraints` values:**

| constraint | name | When it fails | Typical `unavailable_reason` |
|------------|------|---------------|----------------------------|
| 2 | `market_liquidity` | `cancel_value_worst(M_max) < L × (1+R%) × (1+S%)` for all L > 0 | "Opposing side too thin for leveraged positions" |
| 3 | `position_size` | `collateral alone exceeds max position size` | "Your collateral exceeds the maximum position size for this market" |
| 4 | `min_market_liquidity` | `liquidity_sum < leverage_min_market_liquidity` | "Market liquidity below minimum threshold (5,000 VIZ)" |
| 1 | `fund_availability` | `leverage_fund_available × P% / 100 < minimum loan` | "Leverage fund is fully utilized. Try again later." |

> **Note:** Constraint 2 is the most common failure on skewed markets. When `min(reserve_a, reserve_b)` is very small, M_max approaches zero, making it impossible for any leveraged position to survive the worst-case check. This is **correct** behavior — it means the market is too thin on one side for safe leverage — but the user needs to understand *why*.

```json
{
  "method": "leverage-open",
  "market_id": 42,
  "outcome_index": 0,
  "collateral": 360000,
  "leverage": 5.0,
  "max_slippage_percent": 2
}
```

The backend computes `loan = collateral × (leverage − 1)` and `min_tokens = expected_tokens × (1 − max_slippage_percent / 100)` from the current market reserves. Both are passed to the on-chain `pm_leverage_open` operation.

### `leverage-close-preview` — Response

```json
{
  "ok": true,
  "position_id": 7,
  "cancel_value": 2050000,
  "pool_obligation": 1980000,
  "pool_receives": 1980000,
  "bettor_receives": 70000,
  "collateral": 360000,
  "loan": 1800000,
  "loss_vs_collateral": 290000,
  "loss_breakdown": {
    "market_movement_loss": 110000,
    "pool_profit_charge": 180000,
    "total_loss": 290000,
    "total_loss_pct": 80.6
  }
}
```

---

## 11. Frontend: Pre-Calculation, Slider & Cancel Warning

### Data Flow

```
┌─ Frontend Loads ────────────────────────────────────────────┐
│                                                             │
│  1. Fetch market reserves (load-markets / load-market)      │
│  2. Fetch leverage-pool-state                               │
│  3. User enters collateral amount                           │
│  4. Call leverage-preview(collateral, market_id, outcome)   │
│  5. Render slider from response                             │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Slider UI Component

```
┌─────────────────────────────────────────────────────────────┐
│  🚀 BOOST YOUR BET                                          │
│                                                             │
│  Your collateral:  360.000 VIZ                              │
│                                                             │
│  Boost:  [1.0×]─────────●──────────[5.0×]                  │
│              ▲                                          ▲   │
│           no boost                             max avail   │
│                                                             │
│  Selected: 3.5×                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │ Pool loan:      900.000 VIZ  (interest: 90.000 VIZ)  │   │
│  │ Total bet:     1260.000 VIZ                          │   │
│  │                                                     │   │
│  │ Current position value:  1255.000 VIZ                │   │
│  │ Auto-close at:            990.000 VIZ  ←─────────    │   │
│  │                                                     │   │
│  │ ⚠️ If market moves against you, your boosted        │   │
│  │    position will auto-close at 990.000 VIZ.         │   │
│  │    You will lose your 360.000 VIZ collateral.        │   │
│  │    The pool is guaranteed to recover its loan.       │   │
│  │                                                     │   │
│  │ ℹ️ Boost is a bet on PRICE MOVEMENT (social belief  │   │
│  │    shift), not on the outcome result. Your position │   │
│  │    will be auto-closed on 2025-03-14 at 18:00 UTC,  │   │
│  │    24h before market resolution. You profit from    │   │
│  │    market price changes, not from the final outcome. │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  Slippage tolerance:  [0.5%] [1%] [2%] [3%]   ← auto: 2%  │
│                                                             │
│  ☐ I understand: this position will be auto-closed         │
│    on 2025-03-14 at 18:00 UTC (24h before resolution).     │
│    This is a bet on price movement, not on the outcome.    │
│                                                             │
│  [ OPEN BOOSTED POSITION ]  ← disabled until checkbox ✓    │
└─────────────────────────────────────────────────────────────┘
```

### Cancel-Bet Warning Dialog

When a bettor clicks "Cancel bet" on a leveraged position, the frontend calls `leverage-close-preview` and shows a detailed warning:

```
┌─────────────────────────────────────────────────────────────┐
│  ⚠️ CLOSE BOOSTED POSITION                                 │
│                                                             │
│  You are about to close your 3.5× boosted position.        │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Position value (cancel):   2,050.000 VIZ            │   │
│  │                                                     │   │
│  │  Pool receives:              1,980.000 VIZ           │   │
│  │    ├─ Loan repayment:        1,800.000 VIZ           │   │
│  │    └─ Pool profit (10%):       180.000 VIZ           │   │
│  │                                                     │   │
│  │  You receive:                   70.000 VIZ           │   │
│  │                                                     │   │
│  │  ───────────────────────────────────────────────    │   │
│  │  Your collateral:             360.000 VIZ            │   │
│  │  Your loss:                  −290.000 VIZ  (−80.6%)  │   │
│  │                                                     │   │
│  │  Loss breakdown:                                    │   │
│  │    Market moved against you:  −110 VIZ              │   │
│  │    Pool profit charge:        −180 VIZ              │   │
│  │                                                     │   │
│  │  ⚠️ With 1× (no boost), your loss would be          │   │
│  │     only −110 VIZ. Boost increased your loss        │   │
│  │     by 180 VIZ (the pool's profit charge).          │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  [ CANCEL ]                           [ CONFIRM CLOSE ]     │
└─────────────────────────────────────────────────────────────┘
```

**Key UX requirement:** The dialog must clearly show:
1. **How much the bettor receives** vs. their original collateral
2. **The pool's profit charge** — this is the extra loss from leverage (would not exist at 1×)
3. **Comparison with 1× loss** — "Without leverage, you'd only lose X"
4. **Loss as percentage** of collateral
5. **Auto-close notice** — the specific auto-close date/time must appear in **four places**:
   - **`leverage-preview` response** — `auto_close_date` field (ISO 8601). Frontend uses this for all displays below.
   - **Open position dialog** — before the user can submit, they must acknowledge with a checkbox: *"I understand: this position will be auto-closed on [date] at [time] UTC, 24h before resolution. This is a bet on price movement, not on the outcome."* The **OPEN BOOSTED POSITION** button is disabled until the checkbox is checked.
   - **`leverage-info` (active positions list)** — each position card shows: *"Auto-closes: [date] at [time] UTC"* with a countdown timer.
   - **Market card** (market list / market detail) — if the user has active boosted positions on this market, the card shows a prominent *"⚠️ Your boost auto-closes in [countdown]"* banner. This is the most visible touchpoint — users see it when browsing markets, not only when opening the boost dialog.

   The date is computed as `market.betting_expiration − leverage_expiration_buffer_hours`.

### Slider Behavior

- **Discrete stops** at 0.01× intervals from 1.00× to max_leverage (e.g., 1.00, 1.01, 1.02, ..., 2.50).
- Each stop pre-calculated by the backend in `leverage-preview`.
- **Performance note:** The binary search in Constraint 2 runs only **once** to find `leverage_max`. Each slider stop is a **direct formula evaluation** (O(1) arithmetic for CPMM, or LMSR computation) — not a separate binary search. For max_leverage = 5.0× at 0.01× steps, that's 400 direct evaluations plus 1 binary search, not 400 binary searches. A single `leverage-preview` call completes in <50ms.
- **Implementation:** The current prototype generates **all** 0.01× stops server-side in `leverage_compute_max_leverage()` and returns them as a dense array in the `slider_stops` response. The frontend iterates over this array for the slider UI. A future optimization may switch to sparse stops with client-side interpolation for reduced response size, but the dense approach keeps the frontend logic simpler.
- **Danger zone:** The response includes `danger_zone_leverage` — the exact leverage value where `current_cancel_value` first comes within 5% of `liquidation_threshold`. This is the safe→unsafe boundary. The frontend colors the slider **red** for all leverage values ≥ `danger_zone_leverage`. If `danger_zone_leverage` is not present (or > max_leverage), no red zone exists on this market.
- **"Boost unavailable"** state: `max_leverage < 1.1` → slider disabled, `unavailable_reason` displayed to the user with a clear explanation of which constraint failed and why. Example: *"Boost unavailable: opposing side too thin (reserve = 1 VIZ). Maximum safe opposing bet = 0.1 VIZ, insufficient for any leveraged position."*
- **Multiple constraints** can fail simultaneously. The frontend shows the **most relevant** reason (highest priority: min_market_liquidity → market_liquidity → fund_availability → position_size). The full `failed_constraints` array is available for a "show details" expandable section.

### Safety Margin Display

The 1% safety margin is applied **server-side** during position opening. The user sees the **real** liquidation threshold. The frontend adds a subtle note:

> ℹ️ A 1% safety buffer is included during position opening to protect against simultaneous requests.

---

## 12. Protocol Operations (VIZ DLT)

### Dedicated Operations (consensus-validated)

| Operation | Fields | Description |
|-----------|--------|-------------|
| `pm_leverage_open` | `market`, `outcome_index`, `collateral`, `loan`, `min_tokens`, `max_slippage_percent` | Open leveraged position. Protocol validates: collateral ≥ 0, loan within limits, position passes worst-case safety check, actual slippage ≤ max_slippage_percent |
| `pm_leverage_close` | `position_id`, `min_return` | Bettor voluntarily closes position early. **Rejected if cancel_value < liquidation_threshold** |
| `pm_leverage_convert` | `position_id`, `conversion_profit_cost` | Convert leveraged position to normal bet. Bettor pays `L × (1+R%) + current_profit × conversion_profit_cost%` from balance. **Rejected if cancel_value < liquidation_threshold or current_profit ≤ 0** |
| `pm_leverage_liquidate` | *virtual* | Generated by protocol when opposing bet triggers liquidation threshold. See [Section 5](#atomic-liquidation-at-block-level) |
| `pm_leverage_resolve` | *virtual* | Generated at market resolution. See [Section 7](#resolution-handling) |

### Voluntary Close (`pm_leverage_close`)

A bettor may voluntarily close their leveraged position only when `cancel_value ≥ liquidation_threshold`. This preserves the pool's structural zero guarantee for price-movement risk.

```
cancel_value = current cancel value of the position
pool_obligation = L × (1 + R%/100)   (= liquidation_threshold)

REJECTED if cancel_value < pool_obligation:
  → Structural impossibility on VIZ DLT. This condition NEVER occurs:
    if any opposing bet pushed cancel_value below the threshold,
    the position would have already been atomically liquidated
    BEFORE that bet was processed (Section 5) — this is guaranteed
    at the block level by the VIZ DLT delegate.
  → By the time a voluntary close request reaches consensus,
    the position either exists with cancel_value ≥ threshold,
    or it was already liquidated (close fails with "position
    no longer active").
  → The rejection guard exists for the prototype (non-atomic PHP)
    where race conditions are possible. On VIZ DLT it is redundant.

ACCEPTED if cancel_value ≥ pool_obligation:   (the only possible path on VIZ DLT)
  pool_received   = pool_obligation   (pool gets loan + profit)
  bettor_received = cancel_value − pool_obligation  (≥ 0)
  leverage_fund_used -= L
  status → closed_voluntary

Race with Cron Job 11 (prototype):
  If the cron job force-closes the position between the user's
  cancel_value read and the status UPDATE, the user's UPDATE
  returns affected_rows = 0. Instead of showing an error:

  IF affected_rows = 0:
    re-read position status
    IF status IN (4, 1, 2, 3):   (closed_voluntary, liquidated, resolved_won, resolved_lost)
      return SUCCESS with actual bettor_received from the position record
      (the cron job or liquidation already credited the bettor)
    ELSE:
      return ERROR "position not found"
  END IF

  The user should NEVER see an error for a valid close request —
  only a confirmation with the actual payout amount (which may
  differ slightly from the preview if the market moved between
  the preview and the actual close).
```

> **Why keep the guard in the spec?** On VIZ DLT, `cancel_value < liquidation_threshold` is **structurally impossible** for active positions — the atomic block-level check (Section 5) guarantees this when each new block is produced. If an opposing bet would push cancel_value below the threshold, the position is liquidated *first* at pre-bet reserves. So the only two outcomes for a `pm_leverage_close` request on VIZ DLT are: **succeed** (cancel_value ≥ threshold) or **fail with "position no longer active"** (already liquidated by an earlier transaction in this or a previous block). The guard is retained for the prototype (PHP + MySQL, no atomic execution) and as a correctness assertion on VIZ DLT.

### Validation Rules (pm_leverage_open)

The operation is **rejected at consensus level** if any of the following fail:

```
1. market.status == 1 (active only — 'closing' and 'closed' are rejected)
2. current_time < market.betting_expiration - leverage_expiration_buffer_hours × 3600
3. market.liquidity_sum ≥ leverage_min_market_liquidity
4. collateral ≥ 0 AND collateral ≤ user.balance
5. loan ≤ leverage_fund_available × P% / 100
6. loan / collateral ≤ leverage_max_market  (from constraint system)
7. total_bet ≤ market.liquidity_sum × leverage_max_position_ratio / 100
8. cancel_value_worst(total_bet) ≥ loan × (1 + R%/100) × (1 + S%/100) + ε
   where ε = 0 for CPMM
         ε = max(lmsr_rounding_margin, b × 0.001) for LMSR Multi
         (dynamic: accounts for small-b markets where rounding scales with b)
9. actual_slippage ≤ max_slippage_percent
```

Rule 8 is the **hard safety guarantee** (pool protection). If it fails, the operation is rejected — the position cannot be opened.

Rule 9 is the **front-running protection** (bettor protection). This is a user-specified slippage tolerance — standard AMM practice.

### Front-Running Protection (MEV) for Position Opening

When a large `pm_leverage_open` transaction appears in the mempool, an attacker can attempt a **sandwich attack**:

```
1. Attacker sees pm_leverage_open(total_bet = 2160 VIZ on A) in mempool
2. Attacker inserts pm_place_bet(B, 500 VIZ) BEFORE the open tx  (front-run)
   → shifts price against A, reducing tokens the leveraged bettor receives
3. Attacker inserts pm_place_bet(A, 500 VIZ) AFTER the open tx   (back-run)
   → shifts price back toward A, profiting from the round-trip

Net result: the leveraged bettor gets fewer tokens than expected (worse price).
This is NOT a threat to the pool (the pool's structural zero is unaffected),
but it IS a threat to the user.
```

**Mitigation: `max_slippage_percent` parameter**

The user specifies the maximum acceptable price impact when opening. The protocol calculates the actual slippage at execution time and rejects the operation if it exceeds the limit:

```
expected_tokens = tokens calculated at current reserves (at submission time)
actual_tokens  = tokens calculated at reserves at execution time (including
                 any front-running bets processed earlier in the block)

slippage_pct = (expected_tokens - actual_tokens) / expected_tokens × 100

if slippage_pct > max_slippage_percent:
  REJECT — "Price moved too much. Try again with higher slippage tolerance
            or wait for market conditions to stabilize."
```

**Recommended defaults:**

| Market State | Recommended `max_slippage_percent` | Rationale |
|--------------|--------------------------------------|----------|
| Normal liquidity | 1% | Small positions on deep markets rarely see >1% slippage |
| Medium liquidity | 2% | Standard AMM practice for moderate-sized positions |
| Low liquidity / high leverage | 3-5% | Large positions relative to market depth cause more slippage |

> **Why not use `min_tokens`?** The operation already has a `min_tokens` field (minimum acceptable tokens received). `max_slippage_percent` is a more user-friendly interface — the user thinks in percentages ("I accept up to 2% worse price") rather than absolute token counts. The frontend converts `max_slippage_percent` to `min_tokens` internally using the current market reserves from `leverage-preview`.
>
> Both parameters are included in the on-chain operation for compatibility: `min_tokens` is the consensus-level check, `max_slippage_percent` is the user-facing parameter. The frontend sets `min_tokens = expected_tokens × (1 - max_slippage_percent / 100)`.

---

## 13. Numerical Examples

### Example 1: Deep Market, Leverage Capped by Market Depth

> This example shows that even with ample leverage fund capacity (5× available), market depth can limit the effective leverage to 2.5×. The 5× target fails the safety check.

```
Market state:
  reserve_a = 500,000,000  (500,000 VIZ in milli-VIZ)
  reserve_b = 500,000,000  (500,000 VIZ in milli-VIZ)
  k = 250,000,000,000,000,000  (reserve_a × reserve_b)
  liquidity_sum = 1,000,000 VIZ
  SL% = 10% → M_max = 50,000 VIZ (50,000,000 mVIZ)

Lazy pool:
  free_balance = 10,000,000 VIZ
  F% = 10% → leverage_fund_total = 1,000,000 VIZ
  leverage_fund_used = 100,000 VIZ
  leverage_fund_available = 900,000 VIZ
  P% = 0.2% → max_loan_per_position = 1,800 VIZ

Bettor: collateral = 360 VIZ

Constraint 1 (fund):  L_max = 1,800 VIZ → leverage = 1800/360 = 5.0×
Constraint 3 (size):  1,000,000 × 5% = 50,000 VIZ > 2160 → no limit
Constraint 2 (market): Binary search for L...

  Try L = 1800 VIZ, total_bet = 2160 VIZ (2,160,000 mVIZ):

  Tokens received (bet 2160 VIZ on A):
    reserve_b' = 500,000,000 + 2,160,000 = 502,160,000
    reserve_a' = 250,000,000,000,000,000 / 502,160,000 ≈ 497,849,700
    X = 500,000,000 − 497,849,700 ≈ 2,150,300 tokens

  Current cancel_value:
    reserve_a_c = 497,849,700 + 2,150,300 = 500,000,000
    reserve_b_c = 250,000,000,000,000,000 / 500,000,000 = 500,000,000
    cancel_value = 502,160,000 − 500,000,000 = 2,160,000 mVIZ = 2160 VIZ

  Worst-case cancel_value (M_max = 50,000 VIZ = 50,000,000 mVIZ on B):
    cancel_worst = k × X / ((reserve_a' + M_max) × (reserve_a' + M_max + X))
                 ≈ 2.5e17 × 2,150,300 / ((497,849,700 + 50,000,000) × (497,849,700 + 50,000,000 + 2,150,300))
                 ≈ 5.376e23 / (547,849,700 × 550,000,000)
                 ≈ 5.376e23 / 3.013e17
                 ≈ 1,784,000 mVIZ = 1784 VIZ

  Safety check:
    liquidation_threshold = 1800 × 1.10 = 1980 VIZ
    safe_threshold = 1980 × 1.01 = 1999.8 VIZ
    cancel_worst = 1784 VIZ < 1999.8 VIZ  ❌ FAIL

  → Leverage 5.0× NOT SAFE at this market depth. Reduce.

  Try L = 900 VIZ, total_bet = 1260 VIZ:

  X ≈ 1,258,000 tokens (similar calculation)
  cancel_worst ≈ 1245 VIZ
  liquidation_threshold = 900 × 1.10 = 990 VIZ
  safe_threshold = 990 × 1.01 = 999.9 VIZ
  cancel_worst = 1245 > 999.9  ✅ PASS

  → leverage_max_market = 900/360 = 2.5×

Constraint 4: liquidity_sum = 1,000,000 ≥ 5,000 ✅

FINAL: leverage_max = min(5.0, 2.5, unlimited) = 2.5×
```

### Example 2: Thin Market — Leverage Unavailable

```
Market state:
  reserve_a = 100,000  (100 VIZ)
  reserve_b = 100,000  (100 VIZ)
  liquidity_sum = 200 VIZ

Constraint 4 check:
  200 < 5000 → leverage_unavailable = true

Response: "Leverage is not available for this market.
          Minimum required liquidity: 5,000 VIZ."
```

### Example 3: Leverage Fund Limit Hit

```
leverage_fund_available = 500 VIZ
P% = 0.2% → max_loan_per_position = 1 VIZ

Bettor collateral = 100 VIZ
max_leverage = 1 VIZ / 100 VIZ = 0.01× → unavailable

Response: "Leverage is temporarily unavailable.
          The leverage fund is nearly fully utilized.
          Try again later or with a smaller position."
```

### Example 4: Cancel Bet with Profit for User

This example shows a leveraged position where the market moved in the bettor's favor. When the market status changes to "closing", the position is force-closed and the bettor receives a profit.

```
Market state (deep, same as Example 1):
  reserve_a = 500,000,000 mVIZ  (500,000 VIZ)
  reserve_b = 500,000,000 mVIZ  (500,000 VIZ)
  k = 250,000,000,000,000,000

Bettor opens 5× leveraged position on outcome A:
  collateral = 360 VIZ = 360,000 mVIZ
  loan       = 1,800 VIZ = 1,800,000 mVIZ
  total_bet  = 2,160 VIZ = 2,160,000 mVIZ
  R% = 10%  → pool_profit = 180 VIZ
  liquidation_threshold = 1,980 VIZ

After placing bet on A:
  reserve_b' = 500,000,000 + 2,160,000 = 502,160,000
  reserve_a' ≈ 497,849,700
  X ≈ 2,150,300 tokens
  cancel_value at open ≈ 2,160 VIZ (≈ total bet, as expected)

── Market moves in bettor's favor ──
50,000 VIZ (50,000,000 mVIZ) in additional bets placed on outcome A
by other users. This shifts the market toward outcome A, increasing
the value of the bettor's tokens:

  reserve_b'' = 502,160,000 + 50,000,000 = 552,160,000
  reserve_a'' = floor(k / 552,160,000) ≈ 452,813,305

── Market status → "closing" ──
Force-close triggered for all leveraged positions.

Cancel value at current reserves (return X = 2,150,300 tokens):
  reserve_a_c = 452,813,305 + 2,150,300 = 454,963,605
  reserve_b_c = floor(k / 454,963,605) ≈ 549,525,305
  cancel_value = 552,160,000 − 549,525,305 = 2,634,695 mVIZ ≈ 2,635 VIZ

Distribution:
  cancel_value            = 2,635 VIZ
  liquidation_threshold   = 1,980 VIZ  (= L × 1.10)

  Pool receives:   1,980 VIZ  (1,800 loan + 180 profit)
  Bettor receives: 2,635 − 1,980 = 655 VIZ

  Original collateral:  360 VIZ
  Net profit:           655 − 360 = +295 VIZ  (+81.9% on collateral)

┌─ Close Summary ──────────────────────────────────────────┐
│                                                          │
│  Position value (cancel):     2,635.000 VIZ              │
│                                                          │
│  Pool receives:               1,980.000 VIZ              │
│    ├─ Loan repayment:         1,800.000 VIZ              │
│    └─ Pool profit (10%):        180.000 VIZ              │
│                                                          │
│  You receive:                   655.000 VIZ              │
│                                                          │
│  ─────────────────────────────────────────────────────   │
│  Your collateral:               360.000 VIZ              │
│  Your profit:                +295.000 VIZ  (+81.9%)      │
│                                                          │
│  ✅ Market moved in your favor.                          │
│     After pool charges (180 VIZ), you still profit.      │
│                                                          │
└──────────────────────────────────────────────────────────┘

Compare with the loss case (Section 11 — Cancel Warning Dialog):
  In that example, the market moved AGAINST the bettor.
  cancel_value = 2,050 VIZ → bettor receives 70 VIZ (loss of 290 VIZ).

The key difference: market direction determines the bettor's outcome,
but the pool ALWAYS recovers L × (1 + R%) regardless of direction.

If the position is in profit → it is profit for everyone:
  Pool gets loan + profit, bettor keeps the surplus.
If the position is at a loss → only the bettor bears it:
  Pool gets loan + profit, bettor loses part or all of collateral.
```

---

## 14. Risk Analysis

### Eliminated Risks

| Risk | How Eliminated |
|------|---------------|
| **Gap risk** (price jumps past liquidation threshold) | Atomic block-level check: liquidation executes *before* the triggering bet. Cancel value is taken at pre-bet reserves |
| **Pool principal loss (price movement)** | Position cannot be opened unless `cancel_value_worst ≥ loan × (1+R%) × (1+S%)`. Voluntary close rejected if cancel_value < threshold. Force-close before expiration converts outcome risk to price risk |
| **Fund overshoot** | Per-position limit (`P%`) bounds any single loan. Total fund cap (`F%`) bounds aggregate exposure |
| **Thin market exploitation** | `leverage_min_market_liquidity` blocks leverage on markets below threshold |
| **Last-minute manipulation** | `leverage_expiration_buffer_hours` blocks new leveraged positions near expiration |
| **Outcome risk (normal operation)** | Cron Job 11 force-closes ALL leveraged positions before expiration, converting outcome risk into price-movement risk (which is zero) |

### Why Residual Risks Don't Exist (Price-Movement)

**Multiple opposing bets in one block — impossible by design:**

On VIZ DLT, the delegate processes transactions sequentially within the block they sign. The atomic liquidation check fires *before each opposing bet individually*:

```
Block N:
  Tx 1: pm_place_bet(B, 40 VIZ)  →  check → cancel_after > threshold ✅ → execute bet
  Tx 2: pm_place_bet(B, 50 VIZ)  →  check → cancel_after ≤ threshold → LIQUIDATE FIRST at current reserves → execute bet
```

After Tx 1 executes, Tx 2's check runs against the NEW reserves (post-Tx 1). If Tx 2 would push the position below threshold, liquidation fires *before* Tx 2 — at reserves that already include Tx 1's impact, but STILL above the liquidation threshold (because Tx 1 passed its own check). The position is never liquidated at a disadvantageous price.

**Integer rounding (floor dust):**

**CPMM (binary markets):** `floor()` operations discard <1 mVIZ per operation. A full lifecycle (open → liquidate) involves ~5 floor calls, losing at most ~5 mVIZ = 0.005 VIZ. On a 100 VIZ loan with 10% profit (10 VIZ), this is 0.05% of profit. Negligible. The pool earns 9.995% instead of 10% — acceptable.

**LMSR Multi — larger rounding error:** The LMSR calculation involves significantly more floating-point→integer conversions:

```
Sources of rounding error in LMSR:
  1. exp(q_i / b) computation: each call loses ≤1 mVIZ
  2. C(q) = b × ln(Σ exp(q_j / b)): floor after ln, floor after × b
  3. Binary search in tokens_for_amount(): stops at ±1 token precision
  4. sell_return(Δ, i) = C(q) − C(q − Δ·e_i): two C() calls, each with own rounding

Per-operation error:  ~2-5 mVIZ  (vs. ~1 mVIZ for CPMM)
Full lifecycle:       ~10-25 mVIZ = 0.010-0.025 VIZ
Worst case (10 outcomes, small b): up to ~50 mVIZ = 0.050 VIZ
```

On a 100 VIZ loan with 10% profit (10 VIZ), the worst case of 0.050 VIZ is 0.5% of profit. Still acceptable for the pool — but this error can affect **Constraint 2** on thin markets where cancel_value_worst is close to the threshold.

**Mitigation: rounding margin ε in Constraint 2 for LMSR**

For LMSR Multi markets, an additional rounding margin is added to Constraint 2:

```
For CPMM:
  cancel_value_worst(L) ≥ L × (1 + R%/100) × (1 + S%/100)
  (no rounding margin needed — error is negligible)

For LMSR Multi:
  cancel_value_worst(L) ≥ L × (1 + R%/100) × (1 + S%/100) + ε

  where ε = max(lmsr_rounding_margin, b × 0.001)

  Dynamic rationale:
    lmsr_rounding_margin = 50 mVIZ (default) covers typical markets
    b × 0.001 covers edge cases where b is small and rounding scales:
      b = 100 VIZ → b × 0.001 = 100 mVIZ > 50 mVIZ → ε = 100 mVIZ
      b = 10,000 VIZ → b × 0.001 = 10,000 mVIZ = 10 VIZ > 50 mVIZ → ε = 10 VIZ
      b = 50 VIZ → b × 0.001 = 50 mVIZ = 50 mVIZ → ε = 50 mVIZ (no change)

  Note: leverage_min_market_liquidity (5,000 VIZ) provides partial protection —
  markets with very small b are typically below the minimum liquidity threshold.
  However, liquidity_sum in LMSR ≠ b directly, so the dynamic ε is a safety net.
```

This ensures that even with worst-case LMSR rounding, the pool still recovers its full obligation. The ε margin is conservative — in practice, the error is usually <25 mVIZ, but 50 mVIZ accounts for edge cases with many outcomes and small `b`.

| Parameter | Default | Applies to |
|-----------|---------|------------|
| `lmsr_rounding_margin` | 50 mVIZ (0.050 VIZ) | LMSR Multi — minimum ε floor |
| `lmsr_b_scale_factor` | 0.001 (0.1%) | LMSR Multi — ε = max(margin, b × factor) |
| CPMM rounding margin | 0 (not needed) | CPMM binary markets |

**Simultaneous openings (fund overshoot):**

Protocol validates `leverage_fund_used + new_loan ≤ leverage_fund_total` at consensus level. If two `pm_leverage_open` operations arrive in the same block, the second one sees the updated `leverage_fund_used` from the first and is rejected if the fund is exhausted. No overshoot possible.

### Outcome Risk (Eliminated by Force-Close)

Leverage does not protect against **outcome risk**: if the bettor's chosen outcome loses at resolution, the pool loses the loan L. However:

- **Cron Job 11 force-closes ALL positions before expiration** → pool recovers L × (1 + R%) via cancel-bet → outcome risk is converted to price-movement risk (which is zero)
- The only scenario where the pool loses L is if force-close **fails** (cron malfunction, database error)

**Cron Job 11 is the single non-structural failure point.** Unlike atomic liquidation (guaranteed by VIZ DLT at block level), force-close depends on an external process. Mitigation layers:

| Layer | Mechanism | Effect |
|-------|-----------|--------|
| **1. Redundant execution** | Run Cron Job 11 on ≥ 2 independent servers (different hosting providers). Each instance checks and force-closes independently — no coordination needed (idempotent by position_id) | Eliminates single-server failure |
| **2. On-chain fallback (VIZ DLT)** | Delegate plugin includes a `block_pre_apply` hook that checks: if `block.timestamp > market.betting_expiration − buffer` AND leveraged positions still exist → auto-force-close within the block itself | Makes force-close a consensus guarantee, not just a cron job. Cron Job 11 becomes a "nice to have" for early closure |
| **3. Pool profit absorbs losses** | The pool earns R% on every position. Across many positions, total profit = Σ(L_i × R%). If Cron Job 11 fails and the pool loses L on a position, the loss is absorbed directly by the lazy pool's free_balance — the same pool that earns the leverage profit. No separate insurance fund needed — the profit itself is the buffer. Net effect over many positions: pool still profits as long as total R% earnings exceed occasional L losses | No extra fund, no diversion. The lazy pool's profit from successful positions naturally offsets rare losses |
| **4. Monitoring & alerting** | Prometheus/Grafana alert: `active_leveraged_positions > 0 AND time_to_expiration < buffer` → immediate escalation | Reduces time-to-detection from hours to minutes |
| **5. Market status trigger** | When `market.status → 'closing'` (Section 7), all positions are force-closed regardless of time-to-expiration. This trigger is application-level (not cron-dependent) and fires as soon as the oracle/committee sets the closing status | Provides an additional non-cron trigger |

> **On VIZ DLT (post-migration):** Layer 2 makes Cron Job 11 failure irrelevant — the delegate enforces force-close as a consensus rule within each block. The cron job becomes a convenience for early closure, not a safety-critical component. The pool's structural zero guarantee extends from price-movement risk to outcome risk as well, since no position can survive past the expiration buffer.

### Liquidation Rebalancing Spread

When a leveraged position is liquidated, the opposing bettor gets a marginally better price (~POS% better execution). This creates a small MEV incentive but does not affect pool safety. See Section 5 for detailed analysis.

**Known limitation (prototype):** The opposing bettor who triggers liquidation automatically receives the price improvement — no front-running required. This creates a systematic incentive for bots to target leveraged positions near their threshold. Zero-sum transfer from the liquidated bettor to the opposing bettor. Mitigated on VIZ DLT by price lock at pre-liquidation reserves.

### Liquidation Sandwich MEV

An attacker can front-run the liquidating bet with a bet on the leveraged outcome, slightly reducing the cancel_value at liquidation. Maximum impact bounded by `SL% × (C + L) / LS`. Not a pool risk (pool recovers L + R regardless) — the small reduction comes from the bettor's portion. Mitigated on VIZ DLT by locking liquidation price at previous block reserves. See Section 5 (Liquidation Sandwich MEV) for full analysis.

### Market Integrity: cancel_value = 0 (Total Price Collapse)

In extreme cases, a market outcome's price can collapse to near-zero (e.g., `reserve_b → 0` after massive betting on A). In this scenario, `cancel_value = 0` for all positions on the worthless side.

**For leveraged positions:** If `cancel_value = 0`, the position would be liquidated immediately by the atomic check (since `0 < liquidation_threshold`). The pool receives `pool_received = min(0, L × (1 + R%)) = 0` — the pool loses the entire loan L.

**Assessment:** This is not a specification risk — it's a market integrity risk that affects all participants equally. A market where one side's price is 0 means that side has been fully resolved by market forces. The same loss (L) would occur for any co-investor on the losing side, leveraged or not.

**Mitigation:** The constraint system (Constraint 2 + Constraint 4) prevents leverage on markets thin enough for this to occur rapidly. The `leverage_min_market_liquidity` (5,000 VIZ) ensures a minimum depth. On deep markets, a price collapse to 0 requires massive coordinated selling — which triggers atomic liquidation at intermediate prices, limiting the pool's loss to a fraction of L.

### Constraint 2 Conservatism

Constraint 2 (cancel_value_worst ≥ threshold after M_max) is deliberately conservative for the prototype — atomic liquidation fires before the full M_max is applied, so the actual cancel_value at liquidation is always higher. This means effective leverage could be higher than the constraint allows.

**Practical impact:** On deep markets (500K/500K VIZ), the full-M_max constraint can limit leverage to 1.1×–1.3× even when the fund allows 5×. This makes the product unattractive — users don't understand why they can only get 1.5× on a deep market.

**Mitigation:** The conservatism is intentional for the prototype (non-atomic PHP). On VIZ DLT, a `VIZ_DLT_M_FACTOR` parameter (default 0.5) reduces M_max to `M_effective = M_max × factor`, allowing 2–3× more leverage while maintaining pool safety via atomic liquidation. See Section 4.6 (Constraint 2, Defense-in-depth note) for the full relaxation plan.

### Profit Distribution

```
leverage_profit = pool_received − loan  (at liquidation, force-close, or resolution)

Fund flow on position close (with profit):
  pool_received → lazy_pool.free_balance      (all funds return to the common pool)
  leverage_profit → lazy_pool.earned_balance   (track cumulative pool earnings)
  leverage_profit → lazy_pool.reward_per_share  (profit distributed to LP investors)
  leverage_fund_used -= loan                   (free up capacity for new loans)
```

No separate leverage fund balance, no diversion. The leverage fund is a **cap** on `free_balance`, not a separate account. All funds return to the lazy pool; `earned_balance` tracks cumulative lifetime earnings; `reward_per_share` distributes profit to investors; `leverage_fund_used` is merely a capacity counter.

**On position loss (resolved_lost):** `reward_per_share` does NOT change. The loss of loan L is already reflected in `free_balance` (which decreased when the bet was placed into the AMM and the outcome lost). The loss is absorbed by the pool's total_value — it reduces the per-share value for all LP investors proportionally, but is NOT distributed as a negative `reward_per_share` (the accumulator is monotonically non-decreasing). This is consistent with how LP losses work in any AMM: losses reduce the pool's NAV, not the reward accumulator.

---

## 15. Implementation Notes

### Pool Operation Atomicity

**All operations that modify `lazy_pool` columns** (`free_balance`, `allocated_balance`, `earned_balance`, `leverage_fund_used`, `reward_per_share`, `total_shares`) **MUST execute within a single database transaction with a row-level lock on `lazy_pool` (id = 1).**

This guarantees:
- `free_balance` and `leverage_fund_used` change atomically (no partial state where `free_balance` decreased but `leverage_fund_used` not yet incremented)
- `earned_balance` and `reward_per_share` update together with `free_balance` (no inconsistent snapshots)
- Concurrent `pm_leverage_open` operations see the latest `leverage_fund_used` (preventing fund overshoot)

```sql
-- Required pattern for all lazy pool mutations:
START TRANSACTION;
SELECT * FROM `lazy_pool` WHERE `id` = 1 FOR UPDATE;  -- row-level lock
-- ... compute values ...
UPDATE `lazy_pool` SET `free_balance` = ..., `leverage_fund_used` = ... WHERE `id` = 1;
COMMIT;
```

On VIZ DLT, this atomicity is provided by the consensus engine — all state changes within a single operation are applied atomically within the block.

### Backend (prototype)

All leverage calculations live in `module/lazy_pool_helpers.php` (shared with the cron worker):

```php
function lazy_pool_settle_rewards(&$user_arr, $pool)            // Settle accumulated rewards into user's pending_rewards
function lazy_pool_oracle_multiplier($db, $oracle_id)           // Oracle allocation multiplier based on active markets + fault stamps
function lazy_pool_add_fault_stamp($db, $oracle_id, ...)         // Add fault penalty stamp for an oracle
function lazy_pool_expire_fault_stamps($db)                      // Expire fault stamps whose time has passed
function lazy_pool_graduated_recall_check($db, $alloc, $market) // Graduated recall check for idle market allocations
function leverage_distribute_profit($db, $pool_profit, $total_shares)  // Atomically distribute leverage profit to lazy pool
function leverage_fund_state($db)                                // Get current leverage fund state from lazy_pool
function leverage_compute_cancel_value_cpmm($market, $tokens, $outcome_index)  // Cancel value for CPMM binary
function leverage_compute_cancel_value_lmsr($market, $tokens, $outcome_index) // Cancel value for LMSR multi
function leverage_compute_cancel_value($market, $tokens, $outcome_index)       // Dispatch to CPMM or LMSR
function leverage_compute_cancel_value_worst($market, $collateral, $loan, $outcome_index, $M_max)  // Worst-case cancel value (Constraint 2)
function leverage_compute_max_leverage($db, $market, $collateral, $outcome_index)  // Full constraint system + slider stops
function leverage_estimate_tokens_cpmm($market, $amount, $outcome_index)  // Estimate tokens for CPMM bet
function leverage_force_close_position($db, $position, $market, $reason)  // Force-close a leveraged position (shared for cron, voluntary, liquidation)
function leverage_cascade_liquidate($db, $market_id, $outcome_index, $reason, $max_iterations)  // Recursive cascade liquidation loop
```

API endpoints are in `module/api.php` (8 endpoints: `leverage-preview`, `leverage-open`, `leverage-close-preview`, `leverage-close`, `leverage-convert-preview`, `leverage-convert`, `leverage-info`, `leverage-pool-state`).

The `load-market-enriched` API response includes `my_leveraged_positions` — the user's active leveraged positions on the market with auto-close countdown data.

### Frontend (app.js)

Boost UI implemented inline in `app.js` (not a separate module):
- State variables: `boost_preview_data`, `boost_active_market_ids`, `boost_active_loaded`
- `render_boost_form(market_id, mtype, outcomes)` — generate boost form HTML with slider, collateral input, slippage chips, auto-close checkbox
- `boost_fetch_preview(form)` — call `leverage-preview` API, configure slider, set auto-close date
- `boost_render_detail(form, leverage_01, data)` — render position details at given leverage level; interpolate between slider stops
- `boost_format_failed_constraints(constraints)` — format failure messages by priority
- `boost_open_action(el)` — call `leverage-open` API with collateral, leverage, max_slippage_percent
- `boost_close_action(position_id)` — call `leverage-close-preview`, show modal with loss breakdown
- `boost_close_confirmed(el)` — call `leverage-close` API with min_return
- `boost_convert_action(position_id)` — call `leverage-convert-preview`, show modal with fee breakdown
- `boost_convert_confirmed(el)` — call `leverage-convert` API with conversion_profit_cost
- `load_boost_info()` — call `leverage-info` API, populate boost positions table in profile
- CSS styles in `app.css` (`.boost-form`, `.boost-slider`, `.boost-banner`, `.boost-positions-table`)
- i18n strings in `i18n/en.json` and `i18n/ru.json` under `boost.*` namespace (98 keys each)

### Cron Job

**Job 11 — Leverage expiration check**: Runs every 5 minutes. Finds leveraged positions on markets where `betting_expiration − leverage_expiration_buffer_hours` has passed, **or** where `market.status` has changed to `closing` (after the grace period). Force-closes **all** of them (cancel-bet at current price). This converts outcome risk into price-movement risk, ensuring the pool recovers L × (1 + R%) from every position.

**Idempotency mechanism:** When running on ≥ 2 servers (Section 14, Layer 1), each instance must atomically claim the position before processing it. This prevents double-close (two servers trying to close the same position simultaneously):

```sql
-- Step 1: Atomically claim the position (status 0 = active → 4 = closed_voluntary)
UPDATE leveraged_positions
SET status = 4, `update` = UNIX_TIMESTAMP()
WHERE id = ? AND status = 0;

-- Step 2: Check if we claimed it
IF affected_rows = 0 THEN
  -- Position already processed by another instance (or liquidated/resolved)
  SKIP this position
END IF

-- Step 3: We claimed it — now compute cancel_value and distribute funds
-- (SELECT current reserves, compute cancel_value, credit pool/bettor)
```

The `WHERE status = 0` condition ensures that:
- If another cron instance already closed the position → `affected_rows = 0` → skip
- If the position was atomically liquidated by a bet → `affected_rows = 0` → skip
- If the position was voluntarily closed by the bettor → `affected_rows = 0` → skip

Only one process ever transitions the position from `status = 0` to `status = 4`. No coordination between servers needed — the database row-level lock on the UPDATE provides the atomic guarantee.

> **Transaction requirement:** The UPDATE + cancel_value calculation + fund distribution must all be within a single database transaction. If the transaction fails (e.g., deadlock), the position remains at `status = 0` and will be picked up on the next cron run (5 minutes later). No data corruption possible.

> **Design choice:** Close ALL positions when the market enters "closing" status, not just those where cancel_value < threshold. This eliminates outcome risk entirely for the pool. The bettor receives cancel_value − pool_obligation, which may be positive (if the market moved in their favor) or zero (if near liquidation). See Section 7 (Market Status Trigger for Force-Close) and Example 4 for a profit-case walkthrough.

### Convert to Normal Bet (`pm_leverage_convert`)

A bettor may convert their leveraged position into a normal (non-leveraged) bet. This allows them to hold the position to resolution and capture the full outcome payout, instead of being force-closed before expiration. The pool is paid in full plus a conversion fee.

**Mechanics:**

```
Given: leveraged position with loan L, pool profit R%, current cancel_value

pool_obligation     = L × (1 + R%/100)           (= liquidation_threshold)
current_profit      = cancel_value − pool_obligation  (bettor's unrealized gain)
conversion_fee      = current_profit × conversion_profit_cost% / 100

total_user_payment  = pool_obligation + conversion_fee
```

**The bettor pays from their balance:**
1. **Loan repayment + pool profit**: `L × (1 + R%)` — the pool's full claim (same as liquidation)
2. **Conversion fee**: `current_profit × conversion_profit_cost%` — a fee for the privilege of holding to resolution

**Fund flow:**
```
lazy_pool.free_balance      += total_user_payment
lazy_pool.earned_balance     += (pool_obligation − L) + conversion_fee
lazy_pool.reward_per_share   += ((pool_obligation − L) + conversion_fee) × 10^9 / total_shares
leverage_fund_used           -= L
user.balance                 -= total_user_payment

Position status → converted (5)
Bet record updated: remove leveraged_position, keep as normal bet (100% bettor-owned)
```

**Why the conversion fee?** Without it, a bettor whose position is in profit would always convert for free — they'd pay `L × (1 + R%)` from their balance, which is what the pool would get from a force-close anyway. The conversion fee (default 50% of unrealized profit) ensures the pool captures additional revenue for giving up its guaranteed position. This is fair: the bettor is buying the *option* to hold to resolution, and paying a share of their unrealized gain for that privilege.

**Validation rules:**
```
1. position.status == 0 (active)
2. cancel_value ≥ pool_obligation  (position must be in or near profit)
3. current_profit > 0  (conversion only makes sense when position has unrealized gain)
4. user.balance ≥ total_user_payment  (bettor can afford the buyout)
5. conversion_profit_cost == settings.conversion_profit_cost  (bettor must specify the correct fee %)
6. market.status == 1 (active — conversion on 'closing' markets also allowed, see below)
```

**On market status → 'closing':** Conversion is allowed during the grace period. This gives the bettor a choice: let the cron force-close (receive `cancel_value − pool_obligation` in VIZ), or convert (pay `pool_obligation + conversion_fee` from balance, hold tokens to resolution). The bettor picks whichever is more favorable given their conviction about the outcome.

**Comparison with voluntary close:**

| Action | Pool receives | Bettor receives | Bettor risk at resolution |
|--------|--------------|-----------------|--------------------------|
| Voluntary close | `L × (1 + R%)` | `cancel_value − L × (1 + R%)` in VIZ | None — already cashed out |
| Convert to normal | `L × (1 + R%) + conversion_fee` | Tokens held to resolution | Outcome risk: if loses, tokens worth 0 |
| Force-close (cron) | `L × (1 + R%)` | `cancel_value − L × (1 + R%)` in VIZ | None — already cashed out |

**Numerical example** (continuing Example 4):

```
Position: 5× boost on A, collateral = 360 VIZ, loan = 1800 VIZ
Market moved in bettor's favor:
  cancel_value = 2,635 VIZ
  pool_obligation = 1800 × 1.10 = 1,980 VIZ
  current_profit = 2,635 − 1,980 = 655 VIZ

Option 1 — Voluntary close:
  Bettor receives: 655 VIZ (in hand now)
  If A wins at resolution: misses additional upside

Option 2 — Convert to normal (conversion_profit_cost = 50%):
  conversion_fee = 655 × 50% = 327.5 VIZ
  total_user_payment = 1,980 + 327.5 = 2,307.5 VIZ
  Bettor pays 2,307.5 VIZ from balance
  Bettor keeps tokens worth full outcome payout at resolution

  If A wins: tokens redeemable at ~1.0 per token → payout > cancel_value
    Net result: payout − 2,307.5 (could be much more than 655 VIZ)
  If A loses: tokens worth 0 → net loss = 360 (collateral) + 2,307.5 (payment) = −2,667.5 VIZ
    (vs. voluntary close where bettor would have +655 VIZ guaranteed)

  The bettor is betting their 327.5 VIZ conversion fee (plus the 655 VIZ they'd
  have gotten from voluntary close) on the outcome actually occurring.
```

**Frontend dialog:**

```
┌─────────────────────────────────────────────────────────────┐
│  🔄 CONVERT BOOST TO NORMAL BET                             │
│                                                             │
│  Your 5× boosted position is currently in profit.          │
│  You can convert it to a normal bet and hold to resolution.│
│                                                             │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  Current position value:     2,635.000 VIZ            │   │
│  │  Your unrealized profit:       655.000 VIZ            │   │
│  │                                                     │   │
│  │  You pay from balance:                                │   │
│  │    Loan + pool profit:       1,980.000 VIZ            │   │
│  │    Conversion fee (50%):       327.500 VIZ            │   │
│  │    ─────────────────────────────────────────────      │   │
│  │    Total:                    2,307.500 VIZ            │   │
│  │                                                     │   │
│  │  After conversion:                                    │   │
│  │    ✓ You own 100% of the position                     │   │
│  │    ✓ No auto-close — held to resolution               │   │
│  │    ✓ Full payout if your outcome wins                 │   │
│  │    ✗ Full loss if your outcome loses                  │   │
│  └─────────────────────────────────────────────────────┘   │
│                                                             │
│  ⚠️ Converting means you take on outcome risk.             │
│  If you're wrong, you lose your payment + collateral.     │
│                                                             │
│  [ KEEP AS BOOSTED ]     [ CONVERT TO NORMAL BET ]        │
└─────────────────────────────────────────────────────────────┘
```

**New position status:**

| Status | Name | Description |
|--------|------|-------------|
| 0 | active | Position open, leverage active |
| 1 | liquidated | Force-closed by atomic liquidation |
| 2 | resolved_won | Resolved at market end, bettor won |
| 3 | resolved_lost | Resolved at market end, bettor lost |
| 4 | closed_voluntary | Bettor voluntarily closed (cancel-bet) |
| 5 | converted | Bettor converted to normal bet (tokens held to resolution) |

**New history type:**

| Type | Name | Description |
|------|------|-------------|
| 34 | `leverage_convert` | Leveraged position converted to normal bet |

### VIZ DLT Roadmap

Required and planned changes for the VIZ DLT migration of the leverage system:

| Priority | Feature | Description | Status |
|----------|---------|-------------|--------|
| **P0** | `viz_dlt_m_factor` | Set to 0.5 (default) on VIZ DLT. Without this, leverage is uncompetitively low (1.1×–1.3× on deep markets). **Required for launch.** | Specified |
| **P0** | Atomic liquidation in `block_pre_apply` | Delegate plugin enforces liquidation before opposing bet. Makes force-close a consensus guarantee. | Specified |
| **P0** | On-chain force-close fallback | `block_pre_apply` hook auto-force-closes positions past expiration buffer. Eliminates Cron Job 11 as single point of failure. | Specified |
| **P1** | Liquidation price lock | Lock liquidation cancel_value at previous block reserves. Eliminates liquidation sandwich MEV and rebalancing spread MEV. | Specified |
| **P1** | `pm_leverage_open` slippage protection | `max_slippage_percent` / `min_tokens` validated at consensus level. | Specified |
| **P2** | Convert to Normal Bet | Allow bettors to buy out the pool's share and hold to resolution. Key retention feature for advanced users. | **Specified** |
| **P2** | Dynamic depth factor | `depth_factor = f(liquidity_sum, active_positions)` for Constraint 2 relaxation. Adapts to market conditions automatically. | Future work |

---

## Summary

| Property | Value |
|----------|-------|
| **Implementation status** | ✅ **Implemented** (prototype). Migration 005, 8 API endpoints, frontend UI, cron job, test coverage (scenarios 46–52) |
| **Pool risk (price movement)** | Structural zero — eliminated by atomic pre-check + voluntary close rejection + force-close |
| **Pool risk (outcome)** | Eliminated by force-close before expiration (cron job). Residual: cron failure = operational risk |
| **Pool risk (cancel-bet liquidation)** | Bounded bad debt — cancel-bettor executes first (fairness). Rare shortfall absorbed by free_balance. Bounded by SL% |
| **Gap risk** | Eliminated by atomic block-level pre-check |
| **Liquidation MEV** | Small rebalancing spread (~POS%) for opposing bettors. Not a pool risk |
| **Max leverage** | Dynamic: min of fund availability, market depth, position size ratio |
| **Safety margin** | 1% buffer during position opening only |
| **Fund cap** | 10% of lazy pool free_balance (`leverage_fund_total = free_balance × F% / 100`) |
| **Free amount** | `free_balance − leverage_fund_used` (truly available capital, not loaned) |
| **Earned** | `earned_balance` tracks cumulative lifetime pool earnings (never decreases) |
| **Per-position cap** | 0.2% of available leverage fund |
| **Min market liquidity** | 5,000 VIZ (committee-configurable) |
| **Max position size** | 5% of market liquidity_sum (committee-configurable) |
| **Expiration buffer** | 24 hours (no new positions near expiration; force-close all existing) |
| **Voluntary close** | Only when cancel_value ≥ liquidation_threshold (preserves structural zero) |
| **Frontend** | Slider 1.0×–max. Real-time liquidation price. Cancel-bet warning with loss breakdown. Auto-close notice in 4 places. Convert-to-normal dialog. 98 i18n strings per language |
| **Backend** | `module/lazy_pool_helpers.php` (16 functions), `module/api.php` (8 endpoints) |
| **Migration** | `migrations/005_leverage.sql` |
| **VIZ DLT P0** | `viz_dlt_m_factor = 0.5` (required), atomic liquidation, on-chain force-close |
