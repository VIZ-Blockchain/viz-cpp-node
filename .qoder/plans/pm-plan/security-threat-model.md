# Onix Protocol: Security and Threat Model

**Purpose:** Threat model for the Onix Protocol prediction market system. Structured for security audit review. Each threat follows: Attack → Preconditions → Impact → Mitigation → Residual Risk.

---

## 1. Asset Inventory

| Asset | Location | Value at Risk |
|-------|----------|--------------|
| User bet balances | Market reserves (CPMM/LMSR) | Full bet amount |
| Oracle insurance deposits | Oracle account balance | Min 5,000 VIZ per oracle |
| LP principal | Market reserves | Full deposit amount |
| LP fee earnings | Fee pool (computed at resolution) | Variable (losers' pool × fee‰) |
| Dispute escrow | Dispute record | 1,000 VIZ per dispute |
| Lazy Pool deposits | Pool free balance + market allocations | Total pool value |
| DAO fund | System account | Accumulated fees and penalties |

## 2. Trust Assumptions

| Assumption | Scope | Consequence if Violated |
|------------|-------|------------------------|
| CPMM `x * y = k` invariant holds through all operations | Core protocol | LP principal guarantee fails |
| `floor()` rounding is consistent across all fee calculations | Core protocol | Rounding dust exploits |
| Oracle insurance ≥ potential manipulation profit | Economic security | Oracle manipulation becomes rational |
| Dispute resolver acts honestly within 14 days | Dispute system | Auto-close fallback activates (bounded damage) |
| Validators validate all `pm_*` operations correctly | VIZ DLT consensus | State divergence between nodes |
| Virtual operations are deterministic across all nodes | VIZ DLT consensus | Consensus fork |

## 3. Threat Actors

| Actor | Motivation | Capabilities |
|-------|-----------|-------------|
| **Malicious Oracle** | Profit from misresolution | Controls resolution outcome; can create alt accounts for self-betting |
| **Griefing Bettor** | Disrupt oracle/market operations | Can place bets, file disputes, inflate risk scores |
| **Compromised Resolver** | Profit or sabotage | Can issue incorrect verdicts, delay indefinitely |
| **Front-Runner** (VIZ DLT) | Extract value from pending transactions | Can observe mempool, submit transactions with favorable ordering |
| **LP Sniper** | Disproportionate fee extraction | Can deposit small amounts immediately after market creation |
| **Opportunity-Cost Attacker** | Lock Lazy Pool capital in idle markets | Can create markets as oracle, never generate volume |

---

## 4. Threats

### 4.1 Oracle Manipulation (Self-Betting + Misresolution)

**Attack:** Oracle accepts a market, bets heavily on one side via alt accounts, then resolves the market in favor of that side regardless of the actual outcome.

**Preconditions:**
- Oracle's potential bet winnings exceed their insurance bond
- Market has a dispute resolver, but the resolver may be slow or compromised
- Oracle can create alt accounts (sybil)

**Impact:** Bettors on the honest side lose their full stakes. Oracle profits from misresolution minus insurance penalty.

**Mitigation:**
- Insurance slashing: committee can seize the oracle's full insurance (up to entire bond)
- Dispute system: any bettor can challenge within 12h grace period
- Reputation scoring: dispute losses permanently damage the oracle's reliability score
- Committee ban powers: permanent or time-limited oracle ban
- Risk score system: markets where oracle insurance < total bets are flagged/hidden

**Residual risk:** If `bet_winnings > oracle_insurance`, manipulation is economically rational. The protocol relies on the dispute resolver catching the manipulation and the insurance bond being proportional to market volume. **Markets without adequate insurance-to-volume ratio remain vulnerable.**

### 4.2 Oracle Collusion with Resolver

**Attack:** Oracle and dispute resolver collude. Oracle misresolves; resolver rejects any disputes (oracle was "right"), causing disputers to lose their fees.

**Preconditions:**
- Resolver is a single account (not a genuine multisig)
- Resolver and oracle share economic interests

**Impact:** Bettors lose stakes AND dispute fees. Oracle and resolver split profits.

**Mitigation:**
- Recommended multisig for resolver (collegial decision)
- Platform-curated resolver whitelist (prototype)
- Delegate-curated on-chain registry (VIZ DLT)
- 14-day auto-close: if resolver is truly inactive, dispute auto-closes with refunds
- Community can vote out delegates who approve colluding resolvers

**Residual risk:** A compromised multisig (majority of signers colluding) defeats this protection. The protocol has no algorithmic defense against resolver corruption — it relies on social accountability and governance.

### 4.3 Risk Score Griefing

**Attack:** Adversary inflates `total_bets` on an oracle's markets to push the global risk score (`oracle_insurance / total_bets_all_markets`) below the listing threshold (default 2.5×), hiding all of the oracle's markets from default listing.

**Preconditions:**
- Attacker has capital to bet (locked until resolution, not necessarily lost)
- Oracle's insurance is not vastly larger than existing bets

**Impact:** All of the target oracle's active markets are hidden from default listing. Censorship attack on competitor oracles.

**Mitigation:**
- Markets are hidden, not deleted — "Show risky markets" checkbox reveals them
- Oracle can counter by depositing more insurance (instant, no fee)
- Attacker's capital is locked until resolution
- Bet cancellation reverses the attack effect
- Only active markets (status=1) are filtered

**Residual risk:** Attack reduces discoverability. Sophisticated users are unaffected (they can toggle the filter), but casual users may not find the markets. **Planned mitigation:** per-market risk score floor, rate-limiting via EMA, reputation-weighted listing exemptions.

### 4.4 Front-Running (VIZ DLT)

**Attack:** Attacker observes a pending `pm_place_bet` in the mempool, submits their own bet first (at a better price), then allows the victim's bet to execute (at a worse price). Classic sandwich attack.

**Preconditions:**
- VIZ DLT mempool is observable
- Attacker can submit transactions with favorable ordering (e.g., via delegate collusion or network latency advantage)

**Impact:** Victim receives fewer tokens than expected. Attacker profits from the price difference.

**Mitigation:**
- `min_tokens` parameter on `pm_place_bet`: rejects if tokens received < user's minimum
- `min_return` parameter on `pm_cancel_bet`: rejects if returned amount < user's minimum
- **Planned:** commit-reveal scheme (commit hash first, reveal bet after block confirmation)
- **Under evaluation:** batch auction model (collect bets over N seconds, execute at uniform clearing price)

**Residual risk:** `min_tokens` mitigates but doesn't prevent — attacker can still extract value within the user's slippage tolerance. **Commit-reveal is essential for on-chain fairness** and is high priority for VIZ DLT implementation.

### 4.5 LP Sniping (Time-Weight Farming)

**Attack:** Alt accounts deposit small amounts immediately after market creation to capture disproportionate time-weighted fee shares (high `sec_to_expiration` with minimal capital).

**Preconditions:**
- Market is newly created (maximum `sec_to_expiration`)
- No minimum LP lock period

**Impact:** Small deposits earn disproportionate absolute fees per VIZ relative to their capital commitment.

**Mitigation:**
- Market creator is already the first LP with maximum time-weight
- Capital is locked for the full market duration (opportunity cost)
- Fee earnings are proportional to `amount × sec_to_expiration` — small deposits earn small absolute fees
- Minimum liquidity floor (100 VIZ) prevents dust deposits from fragmenting the pool

**Residual risk:** The attack is self-limiting (small capital = small absolute returns), but it does dilute the creator's fee share. **Planned mitigations:** minimum LP lock period, sigmoid time-weight curve, maximum time-weight multiplier cap.

### 4.6 LP Withdrawal Timing Attack

**Attack:** LP observes the likely outcome (based on price movement near expiration) and withdraws liquidity to avoid being in the pool for an unfavorable resolution.

**Preconditions:**
- LP can withdraw while betting is open
- LP has information about likely outcome

**Impact:** Remaining participants face reduced liquidity depth. LP avoids potential losses.

**Mitigation:**
- **Hard block after betting expiration:** LP withdrawal is only allowed while `time < betting_expiration`. Once betting closes, all LP positions are locked until resolution.
- **No impermanent loss:** LP principal is safe regardless of outcome in both Onix Binary and Multi — there is no incentive to withdraw based on odds shifting.
- **Time-ratio penalty:** Early exit receives only `time_ratio`-discounted fees.
- **Minimum liquidity floor:** Cannot reduce `liquidity_sum` below 100 VIZ.
- **Reserve depletion guard:** Cannot withdraw if reserves would reach 0.

**Residual risk:** During the betting period (before expiration), LPs can still withdraw. However, since LP principal is guaranteed regardless of outcome, the rational motivation to withdraw is limited to opportunity cost, not loss avoidance.

### 4.7 Dispute Denial-of-Resolution

**Attack:** Compromised or inactive resolver never acts on disputes, freezing all payouts indefinitely.

**Preconditions:**
- Resolver is inactive, compromised, or deliberately stalling
- Dispute is filed and payouts are frozen

**Impact:** All funds (bets + LP) frozen for the duration of the dispute.

**Mitigation:**
- **14-day auto-close:** `dispute_auto_close_days` guarantees disputes are resolved automatically if the resolver is inactive
- Auto-close: all bets and LP refunded, oracle penalized, disputer's fee returned
- Oracle is penalized even in auto-close (the dispute wouldn't exist without a questionable resolution)

**Residual risk:** Funds are frozen for up to 14 days. This is a bounded inconvenience, not a permanent attack. The 14-day parameter is delegate-adjustable.

### 4.8 Oracle Insurance Depletion

**Attack:** Multiple simultaneous disputes across different markets drain the oracle's insurance to zero. Subsequent disputes have no insurance to slash.

**Preconditions:**
- Oracle has many active markets
- Multiple disputes filed simultaneously

**Impact:** Later disputes have reduced or zero reward pool (insurance exhausted). Dispute incentives break down.

**Mitigation:**
- Insurance checks use current balance at dispute resolution time
- `reward_pool = min(dispute_fee × multiplier, oracle_insurance)` — capped at available insurance
- Oracle cannot withdraw insurance while active markets exist
- Low insurance triggers risk score warnings and listing filters

**Residual risk:** An oracle with many markets and marginal insurance can have their economic security diluted across disputes. **Users should evaluate the oracle's insurance-to-total-volume ratio** (shown as risk score) before betting.

### 4.9 Cancellation Slippage Exploitation

**Attack:** Not a protocol attack, but a user-experience risk. Users cancel bets after price movement and receive significantly less than their original stake, perceiving this as a platform error.

**Preconditions:**
- Market price has moved since the user's bet (other bets placed)
- User cancels expecting a full refund

**Impact:** User receives less than original stake. Generates support tickets and trust erosion.

**Mitigation:**
- Mandatory confirmation modal showing exact return amount, slippage %, and loss
- Red loss warning when slippage exceeds 2%
- `min_return` parameter prevents cancellation if price moved since preview
- Explicit explanation: "This is price movement (slippage), not a platform error"

**Residual risk:** Users who don't read the modal may still be surprised. This is inherent to market-priced position exit and exists in every AMM and exchange.

### 4.10 Lazy Pool Opportunity-Cost Attack

**Attack:** Malicious oracle creates N long-duration markets with subjective questions, attracting lazy pool allocations that lock pool capital in zero-volume markets.

**Preconditions:**
- Oracle can create markets that the pool auto-allocates to
- Markets have long expiration and generate no betting volume

**Impact:** Pool capital locked in idle markets, earning zero fees. Depositors suffer opportunity cost.

**Mitigation:**
- **Graduated early recall:** Idle markets lose 10% allocation per 10% of duration with insufficient volume
- **Active market penalty:** 5% recursive reduction per active market from the same oracle
- **Fault penalty stamps:** Zero-volume resolutions generate stamps that reduce future allocations (expire after 10 days)
- `max_total_allocation` (70%) ensures pool always retains reserves

**Residual risk:** Pool capital is still locked temporarily (until recall kicks in). A determined attacker can force the pool to hold ~40% of allocation on idle markets for their full duration. The graduated recall limits but does not eliminate the opportunity cost.

### 4.11 Resolver Trust and Self-Judging

**Attack:** Market created with `committee_id=0` (no resolver) or with a resolver controlled by the oracle.

**Preconditions:**
- `committee_id=0` is allowed, or resolver is not genuinely independent

**Impact:** No dispute recourse. Oracle can misresolve with impunity.

**Mitigation:**
- **`committee_id=0` is forbidden:** Every market must have a valid resolver
- **Platform-curated whitelist:** UI only shows trusted, pre-approved resolvers
- **Default resolver:** `predict-market-resolver` multisig operated by the platform
- **No self-judging:** Resolver is always a third party with no financial stake in market outcome

**Residual risk:** In the prototype, the whitelist is centrally managed. On VIZ DLT, it transitions to a delegate-curated on-chain registry. The trust anchor shifts from platform admin to elected delegates.

### 4.12 VIZ DLT Consensus Security

**Attack:** Malicious delegate(s) include invalid `pm_*` operations or produce incorrect virtual operations.

**Preconditions:**
- Attacker controls one or more delegates (validators)
- Other nodes do not validate correctly

**Impact:** Incorrect market state, invalid payouts, consensus fork.

**Mitigation:**
- Every `pm_*` operation is validated by **every** node, not just the validator
- Invalid operations are rejected at block inclusion — a single corrupt delegate cannot force invalid state
- Virtual operations are deterministic (same code, same state → same output on every node)
- Stakeholders can vote out compromised delegates

**Residual risk:** If a **majority** of delegates collude, they could theoretically alter consensus rules via a coordinated hard fork. This is the fundamental DPoS trust assumption — identical to EOS, Hive, Tron, and every other DPoS chain.

---

## 5. Security Invariants

The following invariants should be verified in a security audit:

| Invariant | Scope | Verification |
|-----------|-------|-------------|
| CPMM `k = reserve_a × reserve_b` maintained through all bet/cancel operations | Onix Binary | k only changes on liquidity add/withdraw |
| `reserve_a + reserve_b ≥ initial_liquidity` after any sequence of bets | Onix Binary (LP safety) | AM-GM inequality |
| LP subsidy returned unconditionally at resolution | Onix Multi | Subsidy is architecturally separate from payout pool |
| `Σ price(i) = 1` for all market states | Onix Multi | Softmax property |
| No integer overflow/underflow in fee calculations | All | All amounts use `intval()` and milli-VIZ precision |
| Time penalty applies only to profit, never principal | All | `net_payout ≥ bet_amount` for all winners |
| Dispute recalculation produces identical results to fresh resolution with correct outcome | Dispute system | Mathematical equivalence |
| LP withdrawal cannot deplete reserves below zero | Liquidity | Safety check: `new_reserve_a > 0 && new_reserve_b > 0` |
| Oracle insurance cannot be withdrawn while active markets exist | Oracle | Enforced in `oracle-withdraw-insurance` |
| Multiple disputes cannot drain insurance below zero | Oracle | Insurance checks use current balance |
| `committee_id=0` markets cannot be created | Resolver trust | Enforced at market creation |
| Bet cancellation cannot return more than total reserves | Cancellation | Safety floor: `amount_returned = max(0, ...)` |
| All `floor()` rounding is consistent; dust goes to DAO fund | Fee calculation | Verified per payout cycle |
| Virtual operations produce identical results on all nodes | VIZ DLT | Deterministic consensus code |

---

## 6. Audit Trail

Every state-changing operation on a market is recorded in the `market_log` table with before/after snapshots:

- Fields: reserves, k value, liquidity sum, operation type, amounts, user IDs
- Logged actions: `bet`, `cancel`, `liquidity_add`, `liquidity_withdraw`, `accept`, `reject`, `resolution`, `dispute`, `dispute_resolve`, `payout`, `penalty`
- Purpose: complete, immutable audit trail for dispute investigation and post-mortem analysis
- On VIZ DLT: all operations and virtual operations appear in the block stream, providing native auditability
