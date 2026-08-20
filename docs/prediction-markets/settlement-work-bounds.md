# Bounding settlement work per block (#432)

Status: fix **A** implemented (default confirmed at 1.000 VIZ) and fix **D** complete — garbage
collection, settlement and the void refunds all run on a metered row budget. This note records the
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

4. `pm_add_liquidity` → `pm_min_liquidity`. Liquidity is the fourth row source and was the one that
   got away initially: every call mints its own `pm_liquidity_object` (contributions are not merged
   per provider) and the evaluator asserted only `amount > 0`, so rows could be minted at 1 raw
   apiece while the bet paths were floored. The floor is the same one that already gates creating a
   market, so the minimum ticket for putting up liquidity does not depend on whether you open the
   market or top it up later — a product decision, taken deliberately rather than by default.

## 4. Fix D — incremental settlement (design)

The market carries its own settlement cursor and the cron spends a **global** per-block row
budget (`pm_settle_rows_per_block`, shared across all settling markets, oldest market first).
`settle_market` becomes a phase machine resumed block after block:

| phase | work per row | budgeted |
|---|---|---|
| 1 force-close | close leveraged positions still open at settlement | no — see below |
| 2 aggregate | refund queued rows (status 5/6), sum `losers_sum` and winner weight | yes |
| 3 claims | pay outcome-contingent early-exit claims from the bounded bucket | yes |
| 4 payout | pay winners / flip losers, one virtual op per row | yes |
| 5 finalize | fees, LP settlement, dust, `payout_status = 3`, `finalized_time` | no — see below |

Only the bet walk is metered, because only a bet row is cheap. Phases 1 and 5 iterate leveraged
positions and liquidity rows, and each of *those* costs `pm_min_liquidity` (100 VIZ) to create —
a hundred times the price of a bet row. Their work is bounded economically, by what an attacker
would have to stake to create the rows, so metering them would add cursors and resume state for a
threat that fix A already prices out. If that ever changes (a cheaper way to mint an LP or leverage
row), those phases need the same treatment and the same `escrow` discipline.

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

### 4.2 Shipped: incremental settlement

`settle_market()` became `settle_market_step(db, mkt, budget)`, driven by the `pm_settlement_object`
described above and spending the same global row budget as collection. Three things the
implementation had to get right, none of them visible from the design sketch:

* **"Am I the last row?" cannot be answered by looking ahead.** Today's code hands the rounding
  remainder to the last participant, which it recognises by peeking at the rest of the market. Under
  resume that peek is both wrong (the rows already paid are still in the range, just terminal) and
  quadratic. Phase 2 therefore counts the rows it aggregates into `rows_total` and phase 4 counts
  what it has paid into `rows_done`; the last row is `rows_done == rows_total`, in O(1) and stable
  across a pause.
* **The void branch has to release what it burns.** When a voided market has no participants left to
  absorb the leftover pots, the leftover is burned — and burning it without subtracting it from
  `escrow` trips the finalize assert. Conservation accounting has to cover the destruction path, not
  only the payment paths.
* **Per-block work must not repeat one-shot side effects.** The workload gauge
  `markets_in_dispute_window` was decremented at the call site, which now runs on every block of the
  flight; it moved into the branch that runs once, when the market first enters settlement
  (`payout_status != 4`). Same class of bug as the `forfeit_pool` burn in collection.

Covered by `settle_row_budget_spans_blocks` (consensus_sim): a 140-row market settles with the
budget at its floor of 100, so it must take more than one block, must never terminate more than 100
rows in a block, must show `payout_status = 4` in flight, and must end with every row paid, the
settlement object gone and the bettors' balances moved by exactly the sum the rows recorded.
Verified against a deliberately unbounded control — with the budget bypassed the same test reports
"a single block paid 140 rows" and "settled in 1 block", i.e. the pre-fix behaviour.

### 4.3 Shipped: incremental void refunds

The two void paths — cron §2 (missed resolution) and §3 (dispute auto-close) — had the same hole
with a second edge: `refund_all_bets()` walked every row of the market in one block *and* built an
in-memory vector holding every participant, because the forfeit pool is shared pro-rata and the
denominator was only known at the end of the walk.

`refund_market_step(db, mkt, budget)` replaces both. It runs two metered passes over the same
predicate (`status` 0/5/6): pass one only measures (stake total, row count), pass two refunds the
stake and pays each row its slice of the forfeit pool. Because pass one changes nothing, pass two
re-walks *exactly* the set pass one counted — which is how "who is being refunded by this void" stays
answerable across a pause without tagging rows or holding a vector.

The market wears `payout_status = 4` from the first block of the flight, and that flag is now a
gate, not just a display value:

* `pm_resolve_market`, `pm_no_contest` and `pm_transfer_position` refuse it, so a late oracle call
  cannot overtake a refund that is halfway through the market;
* cron §4 (dispute voting finalize) steps over disputes whose market is already being voided by §3;
* cron §6 (batch epoch settle) skips it, so queued rows cannot move between the two passes.

One ordering bug fell out of writing this, and it predates the change: the old path drained
`forfeit_pool` *before* `return_liquidity()`, which force-closes leveraged positions and routes
their curve residual straight back **into** `forfeit_pool`. Those tokens then rode on the market row
until GC dropped it — stranded in `current_supply` with no owner, exactly the leak the void routing
exists to prevent. Liquidity is now returned first and the leftover accounted for after.

Covered by `void_refund_row_budget_spans_blocks`: 140 rows, budget at its floor of 100, no oracle
ever resolves; the void must span blocks, stay under the budget per block, end with `status = 3`,
`resolved_outcome = -1`, every row refunded, and the bettors' balances up by exactly the stake.

### 4.4 The batch executor (cron §6)

Cron §6 filled every row queued into a market's current epoch inside one block, at the price of one
unit of the market-counting cap — the same shape as the settlement bug, and after fix A a queued row
costs `pm_min_bet` (1 VIZ), exactly what a bet row costs. On an LMSR market each row additionally
pays for a curve quote, making it *dearer* per row than settlement.

It now draws on the same shared `pm_settle_rows_per_block` budget, charged per row **visited** (not
merely executed — a visit is the work the block does). Two consequences follow from the fact that
leftover rows are matched *by epoch*:

* the epoch counter advances only once the queue is drained; bumping it mid-drain would leave the
  remaining rows unreachable with their stake already debited;
* the executor therefore also runs **off** the epoch boundary while a pass is in flight
  (`pm_batch_settle_bet_cursor != 0`), instead of making already-revealed stakes wait a whole epoch
  window for the next boundary.

The resume point is a second cursor in the dynamic global properties,
`pm_batch_settle_bet_cursor`, consumed by the first market the round-robin scan visits (which is
`pm_batch_settle_cursor` by construction). Losing it — e.g. an old snapshot without the field — is
safe: the pass restarts at the head of the epoch and skips the rows it already executed by status,
costing one idle walk and no money. Covered by `batch_queue_row_budget_spans_blocks`.

One guard is load-bearing rather than cosmetic: §6 is entered only when `row_budget > 0`. It runs
last and the budget is shared, so a settle-heavy block can reach it with nothing left; entering
anyway would run zero iterations and then fall through to the persist step, which — seeing no
mid-market stop — would write a zero row cursor over the parked one. The next pass would restart at
the head of the epoch and spend budget re-visiting rows it had already executed.

### 4.5 The deadline sweeps re-read settled markets (found 2026-08-19, fixed)

`by_result_expiration` was keyed `(status, result_expiration, id)`. A settled market keeps
`status == 3` and its `result_expiration` stays in the past, so it sat at the **head** of the range
the §5 settle sweep walks — and skipping it costs no `cap`, so the loop never stopped early on it.
Every block therefore re-read the whole settled backlog before reaching real work: measured on the
testnet snapshot of block 82641602, **48 971 iterations of which 48 942 were pure `continue`**, with
only 29 markets actually owing a settlement. The backlog is bounded by GC retention (5 d default),
so it is not a leak — but it is proportional to turnover, and an attacker can inflate it directly by
creating and resolving markets.

The fix keys the index `(status, finalized_time, result_expiration, id)`. `finalized_time` is
stamped exactly once, at finalization, so `finalized_time == 0` means "still owes work"; both sweeps
(§2 missed resolution, §5 settle) `lower_bound` into that group, and a market leaves it the moment
it is settled. `payout_status` is deliberately *not* in the key — the settle sweep flips it 1 → 4
mid-flight and must not move the row it is resuming. Same trick as `by_oracle_finalized`. Index
keys are not serialized, so this needs no snapshot migration.

### 4.6 The dispute tally (cron §4, found 2026-08-19, fixed)

The same shape once more, in the one sweep the earlier passes never looked at. Cron §4 finalizes
disputes whose voting window closed; for each it walks **every ballot** of the disputed market to
build the stake-weighted tally, and charges the market a single unit of the market-counting `cap`.
A ballot is not a cheap row either — each one costs an account lookup plus a lazy-pool deposit
lookup, the same order as the settlement row measured in §5 below.

M3 already caps ballots at `MAX_PM_DISPUTE_VOTES_PER_MARKET` (10 000) per market, and the comment
there reasoned that this made the finalize walk safe. It does not: the cap bounds *one* market,
while §4 may finalize `cap` of them in a block, so the ceiling was `cap × 10 000` = 2 000 000 rows —
three orders of magnitude above the budget every other sweep now respects. Filling it is slow (a
ballot needs a distinct account per market, and 200 disputes cost 200 × `pm_dispute_fee` in escrow)
but the ballots are durable state: the cost is spread over hours of chain time and the work is
replayed in the single block where the voting windows expire.

Unlike settlement, a tally **cannot** be resumed: the verdict needs every ballot at once, and
parking the partial per-outcome sums would mean carrying a vector on the dispute row. So the budget
is enforced *between* disputes — a dispute starts only while budget is left, and is then charged for
the ballots it walked. Worst case per block becomes `row_budget` + one market's ballot cap instead
of `cap` × ballot cap. Deferring a finalize by a block is economically inert: `pm_dispute_vote`
refuses ballots past `voting_end_time`, so the electorate is already final when §4 gets there.
Covered by `dispute_tally_row_budget_defers_next`.

The ordering property this shares with §5 and §6 is worth stating once: the budget is spent in
section order, so a block saturated by the void paths can leave nothing for the sweeps behind them.
That is deliberate — the backlogs are finite work that drains — but it means "how long until my
dispute finalizes" is bounded by the *total* PM work in flight, not by §4 alone.

The related per-transaction cost is fixed alongside it. `pm_dispute_vote` used to enforce the ballot
cap by counting the market's existing ballots on every *new* ballot (walk bounded at cap+1): bounded
per transaction, but O(n) per ballot, O(n²) to fill a market, and work no cron budget covers — the
same antipattern M4 removed from the commit path with `open_commits`. The count now lives on
`pm_dispute_object.ballots`, incremented when a ballot row is created and left alone when a voter
*revises* one (a revision overwrites the row, so the counter tracks rows, not votes). Ballots are
never deleted individually — GC drops the whole cluster — so the counter only grows.

Snapshots need one extra step here that `open_commits` did not. Disputes are imported *before* their
ballots, so a `contains`-guarded read of the key cannot repair a pre-field snapshot on its own:
`reconcile_pm_dispute_ballots()` runs after the ballot import and makes every counter agree with the
rows actually present. That both seeds old snapshots (key absent → 0 → rebuilt) and catches drift in
new ones, at the cost of one pass over an index the import just walked anyway. Covered by
`dispute_ballot_counter_matches_rows`, which checks the counter against a live row count after every
ballot and pins the revision path.

### 4.7 Section order is priority order (cron §8, found 2026-08-19, fixed)

Every section of `process_pm_markets()` charges the same counter, `done`, against the same
`pm_processing_cap_per_block`. That makes section order a priority order, which is intended for the
sweeps that do real work — but it also means a section that reliably exhausts the budget turns
everything behind it into dead code.

Section 7, the lazy-pool recall step, is exactly such a section. It walks the status-0 allocation
index from the head every block and charges `done` for **every row it visits**, including the ones it
only inspects and leaves untouched (`idle, steps remain, but this step isn't due yet`). Charging for
inspection is deliberate — that is what keeps the section bounded — but the working set is large and
long-lived: on the testnet at block 82646702 there were **34 548** status-0 allocations against a cap
of **200**. The loop therefore always runs until `done == cap`.

Behind it sat section 8, the ban-expiry sweep. It never executed. Temporary oracle and creator bans
kept a stale `banned_until` forever and `pm_ban_expired` was never emitted. The damage is bounded:
enforcement compares `banned_until` against `now` rather than testing the field for emptiness, so no
account stayed blocked past its term — what broke is the stored state and the history event, and any
client that reads "banned" as "field is non-zero". No ban existed on the testnet while this was true,
so nothing was observably stuck; the defect is that the section could not run at all.

The fix gives the sweep its own counter (`ban_done`) rather than moving it or enlarging the shared
cap. That is safe because the sweep is self-clearing: a visit sets `banned_until` to 0, which drops
the row out of the swept range permanently. Per-block work is therefore the number of bans that just
expired, and the private cap bounds even a synchronised burst of them.

`ban_expiry_survives_saturated_cron_budget` reproduces the starvation in miniature — cap 2, three
live allocations to saturate it, one short creator ban that must still lapse. With the sweep back on
the shared counter the test fails on exactly that assertion.

The general rule this leaves behind: **a new section appended to this cron is dead on arrival unless
it either sits ahead of section 7 or carries its own budget.**

### 4.8 The lazy-pool withdraw queue (per-tx, found 2026-08-19, fixed 2026-08-20)

`service_lazy_withdraw_queue()` used to drain the pool's FIFO withdraw queue **in full** on every
call — it looped until `free_balance` ran out — and it is called from six places, four of them inside
evaluators (deposit, withdraw, leverage close, leverage convert) plus the two capital-return paths
in the cron. There is no floor per queue row: `pm_lazy_withdraw` creates a **new** request object on
every partial withdrawal while `owed > 0` (one raw is enough), and rows of the same account are
never merged. The asymmetry is that the queue is filled one transaction per row and drained by one
unrelated transaction later — at the testnet's 150 k VIZ of free balance a single call could pay out
up to 150 million rows.

Fix (owner q#678=A): the drain is now budgeted. `service_lazy_withdraw_queue(db, row_limit)` returns
how many rows it processed; the per-transaction call-sites pass `1` (pay just the FIFO head — the
bulk is picked up by the cron), and the new cron section 9 drains the rest up to the shared per-block
`pm_settle_rows_per_block` row budget whenever `pending_withdrawals > 0`. That section is the
liveness backstop: the queue keeps progressing at up to `row_budget` rows per block even when no
capital returns to `free_balance`, so it cannot stall. The rows the cron pays still charge the shared
budget honestly (the return value is subtracted from `row_budget`).

`lazy_withdraw_queue_row_budget_spans_blocks` reproduces the old behaviour in miniature — 250 one-raw
rows against a 100-row budget — and asserts the queue spans several blocks, ≤ `row_budget` per block,
FIFO order, `free_balance ≥ 0` and `pending_withdrawals → 0`. With the pre-fix unbounded drain the
control drains all 250 in the first block and fails the per-block bound. No layout change → the
testnet does not need a redeploy.

### 4.9 The liquidation cascade runs per transaction (found 2026-08-20, open)

Everything above bounds work **per block**. `cascade_liquidate()` breaks that frame because it is
reached from evaluators — `pm_place_bet` (both binary branches), `pm_cancel_bet` and
`pm_withdraw_liquidity` — so its cost is paid per *transaction*, and a block holds as many
transactions as it has room for.

The scan itself is unavoidable in shape: for each round it walks the market's status-0 leverage
positions and evaluates `cancel_value()` against each one's threshold, stopping at the first victim.
When nothing is liquidatable — the normal case, and the one the comment describes as a "cheap index
probe" — it still visits **every open position on that market** before concluding there is no work.
`pm_min_bet` (1 VIZ) is all it takes to trigger one such sweep, and nothing caps how many bets a
block may carry.

How large the swept set can grow is fixed by pool economics rather than by any explicit cap. Every
open position locks at least `pm_min_liquidity` (100 VIZ) of the leverage fund (the #536 floor), the
fund is `pm_leverage_fund_percent` of the pool's free balance, and `free_balance` itself shrinks as
loans go out, so the fixed point is roughly `N ≤ free / 1100` at today's 10 %. A pool holding ~11 M
VIZ therefore supports ~10 000 open positions, and constraint 3 caps only the size of an individual
position, not how many of them share one market. At the measured ~1.7 µs per visited row that is
~17 ms of work bought by a single 1 VIZ bet, repeated for every bet in the block.

Worth noting where that floor came from: `pm_min_liquidity` was imposed on the loan by the #536
audit fix, and its own comment states the intent — "bounds the global open-position count to
`fund_total / pm_min_liquidity`". That reasoning is sound for work measured **per block**, which is
what every sweep above is. It does not carry to a scan that runs once per transaction: bounding the
set says nothing about how many times the set is re-walked, and nothing caps the re-walks.

Two honest qualifications. First, the outer loop re-scans from the head of the range after each
liquidation (`O(K·N)` for K liquidations), but K is self-damping: liquidating a position sells its
tokens back into the curve, which moves the price *toward* the remaining same-side positions and
makes them safer, so mass cascades are not the expected shape. The per-bet `O(N)` scan is the part
that does not depend on anything going wrong. Second, none of this is reachable on the testnet right
now: `pm_leverage_max_per_position_bp` (20 bp) against the current fund makes the per-position cap
(~30 VIZ) smaller than the 100 VIZ loan floor, so no position can be opened at all — the known #536
conflict, left as-is by the owner (q#568). Resolving that conflict in favour of smaller loans would
widen this scan proportionally; the two decisions are coupled.

**Decision (owner q#679=D, 2026-08-20): not fixing.** The lever is a niche product, the exposed set is
economically capped (fund / pool bound above), and an attacker already pays the collateral and
funding for every position the cascade has to scan. Goal #440 closed.

### 4.10 Checked and rejected: the open-position sweep (cron §2c)

The section that force-closes positions once betting is over walks **all** status-0 positions every
block through `by_lev_funding_due` and charges `done` only for the closes, so at first reading it
looks like §4.5 all over again: a full scan whose skipped rows cost no budget.

It is not the same defect, and the difference is worth stating because it is the line between the two
families. In §4.5 the head of the range filled with markets that would **never** need work again, so
the idle scan grew with turnover without bound. Here every scanned row is a live obligation — an open
loan the chain must eventually close — and the row leaves the range permanently the moment it does
(status 0 → 1). The working set is therefore the same economically capped `N` as in §4.9, not a
backlog of corpses. Making it cheaper would mean storing a force-close deadline on the position and
keying an index on it: a layout change and a snapshot migration for a constant-factor win on a set
that is already bounded. Not worth it; recorded so the next audit does not re-open it. Goal #441
closed on this reasoning.

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
