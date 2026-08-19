# Bounding settlement work per block (#432)

Status: fix **A** implemented (default confirmed at 1.000 VIZ), fix **D** landing in stages —
garbage collection is bounded, the settlement phase machine is next. This note records the
problem, the options weighed, and why the chain takes both.

Sibling internal specs: [early-exit-deferred-claim](./early-exit-deferred-claim.md),
[specification](./specification.md) §5 (crons).

## 1. The hole

`pm_processing_cap_per_block` (median-voted, default 200) is the only limiter on the PM cron.
It counts **markets**, not work:

```
while (it != idx.end() && ... && done < cap) {   // §5 auto-payouts
    settle_market(*this, mkt);                    // touches EVERY bet row of the market
    ++done;                                       // ...and costs exactly ONE unit of cap
}
```

`settle_market()` (`libraries/chain/pm_process_markets.cpp`) walks the whole `by_market` bet
range, builds winner/loser vectors, pays each row through `adjust_balance`, flips its status and
pushes one virtual operation per row. There is no cursor and no resume: a market settles
entirely inside one block or not at all. The same shape appears in `gc_market()` (drops the
whole object cluster of a market in one block) and in the void/no-contest branch.

Nothing bounded the number of rows a market can carry:

* every `pm_place_bet` creates a **new** `pm_bet_object` — there is no aggregation by
  (account, market, outcome);
* the instant path had **no minimum bet at all** (`amount > 0` and `tokens_out > 0` were the only
  gates), so a row cost 1 raw = **0.001 VIZ**. Verified by live broadcast on the testnet, with
  both a reject-control and an accept-control, at 0.001 / 0.002 / 0.010 VIZ;
* a **partial** `pm_transfer_position` splits one row into two at no stake cost whatsoever —
  cheaper than betting, and it bypasses any bet-side floor;
* the same class of cap already existed everywhere else — `MAX_PM_DEFERRED_CLAIMS_PER_MARKET`,
  `MAX_PM_DISPUTE_VOTES_PER_MARKET`, `MAX_PM_OPEN_COMMITS_PER_MARKET`, all 10 000. Bet rows were
  the one member of the class left open.

This does not need an attacker. A merely **popular** market walks into it: on the testnet, a toy
bot betting from three accounts every ten minutes had already accumulated 734 rows on one market
(571 and 552 on two others). A mainnet market with thousands of participants is orders of
magnitude larger, and all of that work lands in the single block where the dispute grace expires.

## 2. Options

| | Fix | Bounds work? | Cost |
|---|---|---|---|
| A | minimum bet on the instant path (mirror of `pm_min_batch_bet`) | no — only prices rows | one assert, median-tunable |
| B | hard cap on rows per market | yes | a popular market stops accepting bets: censorship / broken UX |
| C | aggregate bets by (account, market, outcome) | bounded by accounts | invasive: breaks per-bet `weight`, `time_penalty`, `entry_liquidity`, transfer_position, F1 claims |
| D | incremental settlement: bounded rows per block, cursor on the market | yes | largest consensus diff |

**Chosen: A + D.**

* **A alone is not a fix.** It raises the price of a row by three orders of magnitude
  (0.001 → 1.000 VIZ) and it is votable, but the bound it gives is economic, not structural:
  1 000 000 rows at 1 VIZ is 1 000 000 VIZ, which is a lot of money but not an impossible amount —
  and it is *staked*, not spent, so a large share comes back at payout. More importantly, A does
  nothing at all about the legitimate case: a genuinely popular market is not spam and must not be
  punished, yet it is the same block-time problem.
* **D alone is not enough either.** It makes the work per block finite, but leaves rows free, so a
  spammer can still stretch one market's settlement over thousands of blocks and force every node
  to carry the state. A is the cheap economic guard that keeps D's queue short.
* **B is rejected**: refusing bets on a market that is doing well is a user-visible failure of the
  product, and the cap value would have to be guessed.
* **C is rejected**: it is a wider and riskier diff than D for the same benefit, and it destroys
  per-bet properties that the settlement math and the early-exit claims depend on.

## 3. Fix A — minimum bet (implemented)

Two median-voted parameters were added to `chain_properties_pm`:

* `pm_min_bet` — default `1.000 VIZ`, governance floor `0.1 VIZ` (`validate()`), mirror of
  `pm_min_batch_bet`;
* `pm_settle_rows_per_block` — default `2000`, range `[100, 100000]`, consumed by fix D.

Both are wired into the median loop in `database.cpp`. A PM parameter that is declared, reflected
and validated but never enters that loop is silently un-votable and frozen at the code default —
that already happened once with `pm_closed_market_retention_sec`.

Enforcement points (`libraries/chain/pm_evaluator.cpp`):

1. `pm_place_bet`, `mode == 0` (instant) → `amount >= pm_min_bet`;
2. `pm_place_bet`, `mode == 1` (queued batch) → `amount >= pm_min_batch_bet`. This path creates
   rows too and had no floor either — only the *commit* path was covered;
3. `pm_transfer_position`, partial → **both** the transferred part and the remainder must stay at
   or above `pm_min_bet`. A position below the floor is not trapped: it can still be transferred
   whole, which moves the row instead of splitting it.

Known gap, deliberately left to fix D: `pm_add_liquidity` also mints one row per call
(`pm_liquidity_object`, no aggregation per provider) and asserts only `amount > 0`, so LP rows are
a fourth row source with no floor. They are settled by `settle_liquidity`, which walks them twice.
D budgets that walk like every other; whether the floor should also apply to liquidity is a
product decision (it would set a minimum ticket for providing liquidity), so it is not bundled in.

## 4. Fix D — incremental settlement (design)

The market carries its own settlement cursor and the cron spends a **global** per-block row
budget (`pm_settle_rows_per_block`, shared across all settling markets, oldest market first).
`settle_market` becomes a phase machine resumed block after block:

| phase | work per row | budgeted |
|---|---|---|
| 1 force-close | close leveraged positions still open at settlement | yes |
| 2 aggregate | refund queued rows (status 5/6), sum `losers_sum` and winner weight | yes |
| 3 claims | pay outcome-contingent early-exit claims from the bounded bucket | yes |
| 4 payout | pay winners / flip losers, one virtual op per row | yes |
| 5 finalize | fees, LP settlement, dust, `payout_status = 3`, `finalized_time` | yes |

#### Where the resume state lives

Walking the current `settle_market()` end to end gives the exact state a paused settlement has to
carry, and it is more than a cursor: the money split is a two-pass algorithm. Pass one produces the
aggregates (`losers_sum`, total winner weight), pass two turns them into per-row payouts. Cut the
function at any block boundary and both passes need their partial results preserved, plus the
running totals that finalization needs for the dust.

That state does **not** go on `pm_market_object`. It is twelve fields carried by every market that
ever existed, when only the handful currently settling can use them — the object is already ~40
fields wide and markets are the most numerous object on the chain. Instead one
`pm_settlement_object` is created when a market enters settlement, keyed uniquely by market, and
removed at finalization, so the cost is proportional to settlements *in flight*:

| field | phase | meaning |
|---|---|---|
| `phase`, `cursor` | all | current phase and the next row id to process in it |
| `stake_total` | 2 | `losers_sum` (normal) / total active stake (void) |
| `weight_total` | 2 | Σ winner curve weight, 128-bit as in `compute_settlement` |
| `winners_pool`, `uncovered` | set at 3→4 | the split constants, once claims are final |
| `distributed` | 4 | Σ profit paid — finalize routes `winners_pool − distributed` as dust |
| `lp_bonus` | 4 | Σ time-penalty taken from winners |
| `paid_claims` | 3 | drawn from the bounded early-exit bucket |
| `escrow` | all | signed conservation accumulator (below) |

Per-winner payout depends only on `winners_pool`, `weight_total` and the row's own fields, so phase
4 needs no memory of the rows it already paid — that is what makes the cut clean. The void path
reuses the same fields (its two pro-rata distributions accumulate in `distributed` and `lp_bonus`),
keeping "the last participant absorbs the remainder" rounding identical to today's.

Because the object is removed at finalization, a market that is *not* settling has no settlement
row at all, and garbage collection drops it with the rest of the cluster.

Rules that make it safe:

* **Determinism.** Phase, cursor and accumulators live in the settlement object; the budget is a
  median-voted parameter. Every node therefore processes exactly the same rows in the same blocks.
* **The market is closed to everything else while settling.** `payout_status = 4` ("settling")
  keeps §5 from re-entering, keeps `pm_dispute_create` out (it requires `payout_status == 1`), and
  GC cannot fire because `finalized_time` is stamped only in phase 5.
* **Conservation at every block boundary.** Money released from a row but not yet paid out is held
  in an explicit `escrow` accumulator: `+= amount` when a row is released, `-= payout` when someone
  is paid. The PM supply invariant counts it as PM-held, so a snapshot taken mid-settlement
  balances exactly; finalize asserts it reaches zero. The accumulator is **signed**: phase 3 pays
  early-exit claims out of a losing pot whose rows are still standing, so it legitimately goes
  negative before phase 4 releases them. That is not a deficit — the tokens are in real account
  balances and the rows that will fund them are still counted as PM-held, so the two sides of the
  invariant move together either way.
* **Progress.** A market with N rows finishes in about N / budget blocks; the floor of 100 on the
  budget makes starvation impossible.
* **The rows themselves cannot move.** Every operation that creates, splits or removes a bet row —
  `pm_place_bet`, `pm_commit_bet`, `pm_reveal_bet`, `pm_cancel_bet`, `pm_transfer_position` —
  asserts `mkt.status == 1`, and a settling market is at status 3. So the set walked in phase 2 is
  exactly the set paid in phase 4, with no gate to add.

One cross-section interaction does **not** hold automatically and the implementation has to close
it: cron §1 (forfeit of commitments never revealed) adds the penalty to `forfeit_pool` of
*whatever* market the commitment belongs to, without looking at its status. Today that is harmless
— §1 runs earlier in the same block than §5, so settlement reads a final `forfeit_pool` — but a
settlement spanning blocks can have `forfeit_pool` grow *after* phase 3 has already folded it into
`winners_pool`, and those tokens would then belong to no one (orphaned until the GC burn, i.e. the
drift-400 failure mode again). Timing makes it unlikely in practice — a reveal deadline sits at
betting close, long before `result_expiration + grace` — but "unlikely in practice" is precisely
the reasoning that produced the earlier drifts. Phase 5 must therefore route any `forfeit_pool`
that appeared mid-flight instead of assuming it is zero, and the finalize-time escrow assert has to
account for it.

### 4.1 Shipped: bounded garbage collection

Collection went first — it is the same unbounded walk with none of the settlement arithmetic, so it
validates the budget plumbing on its own. `gc_market()` became `gc_market_step(db, mkt, budget)`:
it drops at most `budget` objects, decrements it in place, and returns true only when the whole
cluster (market object included) is gone. A market too large for one block keeps its place at the
head of the sweep — `finalized_time` never changes — and continues next block.

No cursor is needed, unlike settlement: every range is re-entered at its `lower_bound` and the rows
already removed are *gone*, so the sweep resumes exactly where it stopped. Two details make the
pause safe:

* the `forfeit_pool` burn is now zeroed in the same step, otherwise re-entry would burn the same
  tokens again on every block and push `current_supply` below the accounted sum;
* a half-collected market is inert — terminal (`status 3` / `payout_status 3`), so no operation can
  reach it, and the rows being dropped hold no money (bets `2/3`, LP `3`, leverage terminal), so the
  supply invariant is flat across the pause.

Covered by `gc_row_budget_spans_blocks` (consensus_sim): a 143-row cluster with the budget at its
floor of 100 must take more than one block and must never lose more than 100 rows in any block.
Verified against a deliberately unbounded control — with the budget bypassed the same test reports
"a single block removed 143 rows" and "collected in 1 block", i.e. the pre-fix behaviour.

Settlement is served before collection in the block, so a heavy settlement backlog can defer GC.
That is harmless: it only stretches retention, and settlement is finite.

## 5. What a row actually costs

Measured with `tests/consensus_sim/bench/settle_bench.cpp` (`make pm_settle_bench`), which drives
real markets of growing row count through a real chain and times the block that settles them.
Release build, no sanitizers, no account_history plugin:

| rows | idle block | settling block | per row | gc block | gc per row |
|---|---|---|---|---|---|
| 500 | 0.28 ms | 1.24 ms | 1.93 µs | 0.59 ms | 0.62 µs |
| 2 000 | 0.29 ms | 3.61 ms | 1.66 µs | 1.54 ms | 0.62 µs |
| 8 000 | 0.29 ms | 13.95 ms | 1.71 µs | 5.50 ms | 0.65 µs |

Settlement is linear in rows at **~1.7 µs/row**, garbage collection at ~0.62 µs/row. Extrapolating:
roughly **600 000 rows fill one second** of block time and ~1.7 M rows fill the whole three-second
interval. Treat that as a **lower** bound — the benchmark bets from ten accounts (a real market has
thousands, so lookups are less cache-friendly) and the simulated node runs no account_history
plugin, so the virtual operation pushed per row costs almost nothing there while an API node pays
to index it.

That is the shape of the risk with fix A alone: an organic market is nowhere near the limit (the
734-row testnet market settles in ~1.2 ms), but a spammer who is willing to stake 1 VIZ per row can
buy ~1.7 seconds of settlement work in a single block for about a million VIZ. Cheap enough to be
worth closing, which is fix D.

## 6. Tuning

`pm_settle_rows_per_block` trades settlement latency against block time, and the measurement above
is what it should be set from:

* the default **2 000** costs ~3.4 ms of settlement work per block (about 0.1 % of the interval)
  and drains a million-row market in ~500 blocks, i.e. under half an hour;
* raising it to 10 000 costs ~17 ms/block and drains the same market in ~100 blocks;
* the floor of 100 exists so that progress is always guaranteed.

Raising `pm_min_bet` shortens the queue instead, at the price of excluding small bettors — prefer
tuning the budget first.
