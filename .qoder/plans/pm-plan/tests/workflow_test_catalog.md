# Workflow Test Catalog

> Pure-math simulation — no DB required.
> Run: `php tests/workflow_test.php`
> File: `tests/workflow_test.php` (≈5 000 lines, 56 numbered scenarios, ~100 `assert` / echo-test checks)

---

## Quick Reference

| # | Scenario | Key Assertions |
|---|---------|----------------|
| 1 | Happy path: oracle resolves correctly, auto-payout | resolution → payouts → auto-payout flow |
| 2 | Oracle rejects market: liquidity returned | status=-1, LP refunded |
| 3 | Self-oracle: auto-approve (no pending queue) | status=1 at creation |
| 4 | Time penalty: early/mid/late/very-late bets | quadratic penalty deductions |
| 5 | Oracle misses resolution | refund all + penalty distribution |
| 6 | Dispute — oracle WRONG | recalculate payouts, plaintiff rewarded |
| 7 | Dispute — oracle RIGHT | committee gets fee, original payouts kept |
| 8 | Bet cancellation | CPMM reverse + slippage |
| 9 | Multiple LPs: time-weighted fee distribution | early LP earns more |
| 10 | Penalty curve comparison | linear vs quadratic |
| 11 | Low-volume LP solvency | 1 small bet, one-sided, LP principal guaranteed |
| 12 | Oracle re-registration | profile update without fee |
| 13 | Dispute + extra penalty + permanent oracle ban | insurance slashed, ban enforced |
| 14 | Dispute + time-limited creator ban | ban_until timestamp |
| 15 | Fractional LP withdrawal | partial + remainder, position stays active |
| 16 | Risk score | oracle insurance coverage (betting block + listing filter) |
| 17 | Oracle voluntary no-contest | grace period, pending refund payouts, auto-payout |
| 18 | Dispute against abusive no-contest | 3-outcome resolution: A/B/no-contest + sanctions |
| 19 | Auto-close stale dispute | 14-day timeout, refund all + oracle penalty |
| 20 | Lazy Pool: deposit + shares + lock period | MasterChef share accounting |
| 21 | Lazy Pool: share calculation after profit | reward_per_share accumulator |
| 22 | Lazy Pool: late depositor fairness | no old rewards for new depositor |
| 23 | Lazy Pool: deposit unlock consolidation | unlock_time merging |
| 24 | Lazy Pool: planned withdrawal (full) | shares burned, principal + reward |
| 25 | Lazy Pool: emergency withdrawal | penalty on locked profit |
| 26 | Lazy Pool: emergency no profit = no penalty | zero-profit edge case |
| 27 | Lazy Pool: market auto-allocation | profit distribution via rps |
| 28 | Lazy Pool: edge cases | 1:1 first deposit, zero reward, partial withdrawal |
| 29 | LMSR Math unit tests | prices, cost, buy/sell roundtrip, tokens_for_amount, b_from_liquidity |
| 30 | Onix Multi Settlement | losers forfeit, fees deducted, winners split by tokens |
| 31 | Creator Fee in Binary | creator receives fee from losers_sum |
| 32 | Multi-Market Lifecycle | create → bet → resolve → verify payouts |
| 33 | LP Principal Guarantee | subsidy architecturally separate |
| 34 | Edge Cases | all-on-winner, no-on-winner, zero-volume, single bettor |
| 35 | Position Transfers | full, partial, to-self (fail), excessive (fail), transfer+resolve |
| 36 | Graduated Early Recall | idle market loses allocation, active market keeps it |
| 37 | Active Market Penalty | recursive 5% per oracle active market |
| 38 | Fault Penalty Stamps | stamps on faults, auto-healing after 10 days, combined B+C |
| 39 | Market Metadata | category/subcategory/tags validation |
| 40 | Metadata JSON | build + localization structure |
| 41 | Localization fallback | resolve_i18n simulation |
| 42 | Jurisdiction filtering | banned/allowed tag logic |
| 43 | Category structure | i18n completeness check |
| 44 | Market creation with metadata | simulated API flow |
| 45 | i18n JSON files | key consistency between en.json and ru.json |
| 46 | Leverage cancel_value | CPMM calculation (3 sub-tests) |
| 47 | Liquidation threshold | safety margin (2 sub-tests) |
| 48 | Max leverage binary search | CPMM (3 sub-tests) |
| 49 | Leveraged position lifecycle | open → close (2 sub-tests) |
| 50 | Cancel-bet liquidation waterfall | Case B: bad debt + bettor (4 sub-tests) |
| 51 | LMSR max bet amount | Constraint 2 (3 sub-tests) |
| 52 | Convert to Normal Bet | profit/fee/total calculation (4 sub-tests) |
| **53** | **Batch Commit-Reveal** | commit → reveal → batch settle + forfeit (4 sub-tests) |
| **54** | **Account-Mode Dispute** | dispute_mode=1 centralized resolver (5 sub-tests) |
| **55** | **allow_instant_bet=false** | instant rejected, batch/commit accepted (5 sub-tests) |
| **56** | **Oracle Fixed Fee Transfer** | external vs self-oracle, insufficient balance (9 sub-tests) |

---

## Detailed Breakdown

### Oracle & Market Lifecycle (1–5, 12)

| # | Scenario | What it tests |
|---|---------|--------------|
| 1 | Happy path | Oracle registers, deposits insurance, creator creates market, oracle accepts, bets placed, oracle resolves to A, auto-payout distributes winnings |
| 2 | Oracle rejects | Oracle rejects pending market → liquidity returned to creator, status=-1 |
| 3 | Self-oracle | Creator is their own oracle → market auto-approves (status=1, no pending queue) |
| 4 | Time penalty | Bets at T+1h (early, no penalty), T+24h (mid), T+46.7h (late, quadratic), T+47.9h (very late). Verifies penalty_ratio, quadratic curve, deduction from profit |
| 5 | Oracle missed | Oracle misses `result_expiration` → cron penalty: 5% of insurance slashed, all bets+LPs refunded pro-rata |
| 12 | Oracle re-registration | Already-registered oracle updates fee/fixed_fee without paying registration fee again |

### Disputes — Committee Mode (6–7, 13–14, 17–19)

| # | Scenario | What it tests |
|---|---------|--------------|
| 6 | Oracle WRONG | Bettor disputes → committee sides with plaintiff → payouts recalculated for correct outcome, oracle insurance slashed (reward_pool) |
| 7 | Oracle RIGHT | Dispute filed but committee upholds oracle → dispute_fee split: voters 50%, oracle 50% |
| 13 | Extra penalty + oracle ban | Committee adds 2000 VIZ extra penalty + permanent oracle ban; verifies ban enforcement on next accept |
| 14 | Creator ban | Time-limited creator ban (30 days); `ban_until` timestamp set |
| 17 | Oracle no-contest | Oracle voluntarily declares no-contest → penalty = 50% of dispute_fee from insurance → all bets/LPs refunded → grace period → auto-payout |
| 18 | Dispute against no-contest | Oracle abuses no-contest → resolver picks correct outcome (A/B/no-contest) with sanctions |
| 19 | Auto-close stale dispute | 14-day timeout → full refund + oracle penalty + disputer fee return |

### Disputes — Account Mode (54)

| # | Scenario | Sub-tests | What it tests |
|---|---------|-----------|--------------|
| 54 | Account-mode dispute | 54.1–54.5 | `dispute_mode=1` with designated resolver account. Non-resolver rejected; resolver flips outcome, applies penalty + oracle ban; payout recalculated |

### Betting & Cancellation (8, 35, 55)

| # | Scenario | What it tests |
|---|---------|--------------|
| 8 | Bet cancellation | C bets on A, D bets on B (shifts reserves), C cancels → slippage effect |
| 35 | Position transfers | Full transfer, partial transfer, to-self (fail), excessive amount (fail), transfer+resolve |
| 55 | allow_instant_bet=false | `mode=0` rejected (`pm_instant_bet_disabled`), `mode=1` batch accepted, `mode=2` commit-reveal accepted, mutual constraint (`allow_instant_bet OR allow_batch`), batch settle |

### Batch Commit-Reveal (53)

| # | Scenario | Sub-tests | What it tests |
|---|---------|-----------|--------------|
| 53 | Commit-reveal lifecycle | 53.1–53.4 | 3 commits (2 revealed, 1 forfeited). Hash commitment + escrow lock. Reveal verifies hash, refunds surplus. Forfeit: 20% penalty → `forfeit_pool`, 80% refunded. Batch settle: aggregate CPMM op, pro-rata tokens. `forfeit_pool` boosts winners |

### Oracle Fixed Fee (56)

| # | Scenario | Sub-tests | What it tests |
|---|---------|-----------|--------------|
| 56 | Fixed fee at accept | 56.1–56.9 | External oracle: 15 VIZ transferred creator→oracle. Self-oracle: transfer skipped entirely. Insufficient balance: partial transfer = available balance |

### Liquidity (9, 11, 15, 33)

| # | Scenario | What it tests |
|---|---------|--------------|
| 9 | Multiple LPs time-weighted | 3 LPs (at creation, T+24h, T+48h-1s) — early LP earns much more fee per VIZ |
| 11 | Low-volume LP solvency | 1 VIZ bet on A, no bets on B → LP principal guaranteed |
| 15 | Fractional LP withdrawal | Withdraw 50% (partial), position stays active, then full withdrawal of remainder |
| 33 | LP principal guarantee | Heavy one-sided betting → subsidy always returned, architecturally separate from losers' pool |

### Lazy Pool (20–28, 36)

| # | Scenario | What it tests |
|---|---------|--------------|
| 20 | Deposit + shares | First depositor gets 1:1 shares, lock period enforced |
| 21 | Reward accumulator | `reward_per_share` increases after profit, user gets correct share |
| 22 | Late depositor fairness | New depositor doesn't get old rewards |
| 23 | Unlock consolidation | Multiple deposits merge unlock times |
| 24 | Planned withdrawal | Full withdrawal: shares burned, principal + reward returned |
| 25 | Emergency withdrawal | Penalty applied only to locked profit |
| 26 | Emergency no profit | Zero profit → zero penalty |
| 27 | Market auto-allocation | Pool allocates % to new market, profit distributed via rps |
| 28 | Edge cases | 1:1 first deposit, zero reward, partial withdrawal |
| 36 | Graduated early recall | Idle market loses allocation over time; active market keeps it |

### LMSR Math & Multi-Outcome (29–32, 34, 51)

| # | Scenario | What it tests |
|---|---------|--------------|
| 29 | LMSR unit tests | Equal-q → equal prices, cost monotonicity, buy/sell roundtrip, tokens_for_amount, b_from_liquidity, price shift after buy |
| 30 | Multi settlement | Losers forfeit, oracle/creator/LP fees deducted, winners split by token weight |
| 31 | Creator fee binary | Creator receives permille of losers_sum |
| 32 | Multi lifecycle | 4-outcome LMSR: create → bet all outcomes → resolve → verify payouts |
| 34 | Edge cases | All-on-winner, no-on-winner, zero-volume, single bettor |
| 51 | LMSR max bet | Constraint 2: max bet within slippage cap |

### Leverage (46–50, 52)

| # | Scenario | What it tests |
|---|---------|--------------|
| 46 | Cancel_value calculation | CPMM cancel_value > 0, symmetric, slippage |
| 47 | Liquidation threshold | `threshold = loan * 1.10`, safe threshold |
| 48 | Max leverage search | Binary search for max leverage at given R |
| 49 | Position lifecycle | Open leveraged position → voluntary close |
| 50 | Liquidation waterfall | Cancel-bet shifts reserves → leveraged position liquidated if CV < threshold |
| 52 | Convert to normal bet | Profit calculation, 50% conversion fee, total payment |

### Risk & Reputation (16, 37–38)

| # | Scenario | What it tests |
|---|---------|--------------|
| 16 | Risk score | `risk_score = insurance / total_bets`; bet blocked below threshold; listing hidden |
| 37 | Active market penalty | 5% recursive penalty per active market accepted by oracle |
| 38 | Fault penalty stamps | Stamps on faults, auto-healing after 10 days, combined scenarios |

### Metadata & i18n (39–45)

| # | Scenario | What it tests |
|---|---------|--------------|
| 39 | Market metadata | Category/subcategory/tags validation rules |
| 40 | Metadata JSON | Build + localization structure |
| 41 | Localization fallback | `resolve_i18n` simulation |
| 42 | Jurisdiction filtering | Banned/allowed tag logic |
| 43 | Category structure | i18n completeness check |
| 44 | Market creation with metadata | Simulated API flow |
| 45 | i18n JSON files | Key consistency between `en.json` and `ru.json` |

---

## Helper Functions

| Function | Simulates | Notes |
|----------|-----------|-------|
| `make_user()` | — | Test user factory |
| `register_oracle()` | `pm_oracle_register` | Fee + insurance setup |
| `oracle_deposit_insurance()` | — | Lock insurance bond |
| `register_creator()` | — | Creator registration |
| `create_market()` | `pm_create_market` | Binary CPMM market |
| `oracle_accept()` | `pm_oracle_accept_market` | Basic accept (no fixed fee) |
| `oracle_accept_with_fixed_fee()` | `pm_oracle_accept_market` | With fixed fee transfer |
| `oracle_reject()` | — | Reject + refund liquidity |
| `place_bet()` | `pm_place_bet` (mode=0) | Instant CPMM bet |
| `place_bet_batch()` | `pm_place_bet` (mode=1) | Queued batch bet |
| `commit_bet()` | `pm_commit_bet` | Hash commitment + escrow |
| `reveal_bet()` | `pm_reveal_bet` | Hash verify + enqueue |
| `commit_forfeit()` | `pm_commit_forfeit` (virtual) | Penalty → forfeit_pool |
| `batch_settle()` | `pm_batch_settle` (virtual) | Aggregate CPMM settlement |
| `cancel_bet()` | `pm_cancel_bet` | CPMM reverse |
| `add_liquidity()` | `pm_add_liquidity` | Proportional reserve add |
| `withdraw_liquidity()` | `pm_withdraw_liquidity` | Principal-safe withdrawal |
| `resolve_market()` | `pm_resolve_market` | Parimutuel settlement |
| `auto_payout()` | `pm_auto_payout` (virtual) | Credit winners |
| `oracle_penalty()` | `pm_oracle_missed_penalty` (virtual) | Slash + refund |
| `create_dispute()` | `pm_dispute_create` | Escrow dispute fee |
| `resolve_dispute_oracle_wrong()` | `pm_dispute_finalize` (committee) | Flip outcome + slash oracle |
| `resolve_dispute_oracle_right()` | `pm_dispute_finalize` (committee) | Uphold oracle + split fee |
| `resolve_dispute_account_mode()` | `pm_dispute_resolve` (account) | Centralized resolver decides |
| `oracle_no_contest()` | `pm_no_contest` | Voluntary no-contest |
| `calc_risk_score()` | — | Insurance/bets ratio |
| `fmt()` | — | Format satoshi → VIZ string |
| `balances()` | — | Snapshot all user balances |
