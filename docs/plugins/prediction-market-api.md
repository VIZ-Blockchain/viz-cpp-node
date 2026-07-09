# `prediction_market_api` Plugin

Read-only JSON-RPC access to HF14 prediction-market state (markets, bets, oracles, liquidity, disputes, the lazy pool, and the v5 chain properties). The plugin returns the raw consensus `pm_*` objects directly, plus a few computed DTOs for values that are derived rather than stored.

**Enable:** add `prediction_market_api` to the node's plugin list (registered in `vizd` by default). Depends on `chain` + `json_rpc`.

All list methods page with `from` (skip count) and `limit` (`≤ 1000`).

---

## Methods

### Markets

| Method | Args | Returns |
|--------|------|---------|
| `get_market` | `market_id` | `pm_market_object` |
| `list_markets` | `status, from, limit, [show_risky]` | `pm_market_object[]` |
| `list_markets_by_oracle` | `oracle, from, limit` | `pm_market_object[]` |
| `list_markets_awaiting_resolution` | `oracle, from, limit` | `pm_market_object[]` |
| `list_markets_by_creator` | `creator, from, limit` | `pm_market_object[]` |
| `get_market_outcomes` | `market_id` | `pm_outcome_object[]` |
| `get_market_weight_sums` | `market_id` | `pm_market_weight_sums` (computed) |
| `get_market_bets` | `market_id, from, limit` | `pm_bet_object[]` |
| `get_market_liquidity` | `market_id, from, limit` | `pm_liquidity_object[]` |
| `get_market_full` | `market_id, [account]` | `pm_market_full` (computed) |

`status` for `list_markets`: `-1` deleted, `0` waiting, `1` active, `2` closed, `3` resolved.

`list_markets_awaiting_resolution` returns the markets that need **this oracle's result now**: active (`status 1`) markets whose betting window has already closed (`betting_expiration ≤ head_block_time`) and are therefore not yet resolved. "Awaiting" is not a distinct status — a market stays `active` from open through betting-close until it is resolved — so it cannot be isolated by `status` alone. This method walks the `by_betting_expiration` index (keyed `status, betting_expiration, id`) over just the bounded prefix of active markets whose betting has passed, avoiding a full scan of the oracle's (mostly resolved) history that an Oracle Console would otherwise have to do client-side.

`get_market_full` is a **one-call enriched view** for a market-detail screen: it returns the market + outcomes + weight sums + oracle (with reliability) + parsed metadata, and — when the optional `account` is given — that account's bets, leverage positions and LP **on this market**. Saves the thin client several round-trips.

**Risk listing filter** (security-threat-model §4.3): by default `list_markets` hides under-insured markets — those whose oracle insurance covers less than **2.5×** the market's betting volume. Pass `show_risky = true` to reveal them. Markets are only hidden, never blocked/deleted (betting is always permitted on-chain — a consensus bet-block would be a censorship vector). The lazy-pool exposure penalties (active-market 5% recursive + fault stamps) are enforced in consensus on allocation.

### Market metadata (off-chain parsed)

Each market carries a free-form, consensus-opaque `metadata` JSON string. This plugin parses the keys it
indexes (category / subcategory / tags / banned jurisdictions) into a `pm_market_meta_object` for
discovery and jurisdiction filtering — **display/indexing only, never consensus**.

| Method | Args | Returns |
|--------|------|---------|
| `get_market_meta` | `market_id` | `pm_market_meta_object` (or error if none) |
| `list_markets_by_category` | `category, from, limit, [jurisdiction], [subcategory], [tag], [sort]` | `pm_market_meta_object[]` |
| `get_market_categories` | — | `pm_market_categories` (computed) |

`list_markets_by_category` excludes markets whose `banned_jurisdictions` contains the optional
`jurisdiction` ISO code — so a regulated client passes its own jurisdiction to get only the markets it
may list. Optional `subcategory` (exact) and `tag` (CSV membership) narrow the set; `sort` ∈ `newest`
(market id desc, default) · `oldest` · `volume` (`bets_sum` desc) · `expiration` (`betting_expiration`
asc). `get_market_categories` returns the live taxonomy — per-category / per-subcategory counts plus the
top 20 hot tags (jurisdiction-* excluded) — aggregated over currently indexed markets, so a browse UI can
build its filter chips without hard-coding a taxonomy. The meta object:
```
{ market: pm_object_id,
  category, subcategory, tags,          // strings; tags comma-joined
  banned_jurisdictions,                 // comma-joined ISO codes; empty = allowed everywhere
  expiry }                              // pruned after the dispute window closes + TTL
```

### Positions & oracles

| Method | Args | Returns |
|--------|------|---------|
| `get_account_positions` | `account, from, limit` | `pm_position[]` (bet + `expected_payout`) |
| `get_account_leverage_positions` | `account, from, limit` | `pm_leverage_position_object[]` |
| `get_market_leverage_positions` | `market_id, from, limit` | `pm_leverage_position_object[]` |
| `get_creator_ban` | `account` | `pm_creator_ban_object` (or error if none) |
| `get_oracle` | `owner` | `pm_oracle` (object + `reliability_score`) |
| `list_oracles` | `from, limit` | `pm_oracle_object[]` |

### Leverage previews (Boost)

Read-only quotes that call the **same in-node margin math** the evaluators use, so a preview matches what the corresponding `pm_leverage_*` op would compute at the head block. They are non-consensus estimates (reserves move between the read and the broadcast — always send the on-chain slippage guards).

| Method | Args | Returns |
|--------|------|---------|
| `get_leverage_quote` | `market_id, outcome_index, collateral` | `pm_leverage_quote` (computed) |
| `get_leverage_close_preview` | `position_id` | `pm_leverage_close_preview` (computed) |
| `get_leverage_convert_preview` | `position_id` | `pm_leverage_convert_preview` (computed) |

`get_leverage_quote` mirrors `pm_leverage_open`: it returns the max solvent loan and resulting max leverage, the pool/position caps, up to 12 slider stops (each with tokens, threshold, current & worst-case cancel value), and — when leverage is not possible — `available = false` with a `failed_constraints[]` list. `get_leverage_close_preview` / `get_leverage_convert_preview` mirror `pm_leverage_close` / `pm_leverage_convert` at the current reserves (cancel value, pool obligation, what the bettor receives, whether it is closeable/convertible, and the conversion fee at the current median `pm_conversion_profit_cost_percent`).

> Per-bettor settlement is emitted as the `pm_payout` virtual op (stake, side/outcome, realized payout —
> `0` on a loss); a leveraged position's settlement is the `pm_leverage_resolve` virtual op (with
> `outcome_index`, `won`, `leverage`). Both appear in `account_history`; the leverage-position objects
> themselves are queryable via the two methods above.

### Disputes, lazy pool, governance

| Method | Args | Returns |
|--------|------|---------|
| `get_dispute` | `market_id` | `pm_dispute_object` |
| `get_dispute_votes` | `market_id` | `pm_dispute_votes` (votes + live tally) |
| `get_lazy_pool` | — | `pm_lazy_pool_object` |
| `get_lazy_deposit` | `account` | `pm_lazy_deposit_object` |
| `get_lazy_allocations` | `from, limit` | `pm_lazy_allocation_object[]` |
| `get_market_lazy_allocation` | `market_id` | `pm_lazy_allocation_object` (or error if none) |
| `get_pm_chain_properties` | — | `chain_properties_pm` (median, v5) |

`get_lazy_allocations` lists the lazy pool's per-market allocation records (for a pool dashboard); `get_market_lazy_allocation` fetches the one for a given market. Oracle penalty stamps need no separate method — they ship on `pm_oracle_object` (`penalty_stamps`, `last_penalty_stamp_time`) via `get_oracle`.

### Charts — kline / weight history

A time series for plotting how each outcome's weight evolves. The plugin appends one point **every time a market's per-outcome weights change** — a bet, a cancel, a liquidation, a batch settle, a leverage open, or a leverage settlement — as a timestamped snapshot of the parimutuel weight (staked amount) on every outcome. This is **non-consensus** plugin state (kept in chainbase, undo/redo-safe, never part of the state hash); history accrues from the moment the plugin is first enabled on the node.

**Retention:** the kline history is pruned **together with the market's metadata**, on the same schedule — `result_expiration` + dispute grace + `pmm-ttl-days` (default 7). So a market's full chart is available throughout its life and for the retention window after settlement, then both indexes are cleaned up (draining over several blocks for very long histories) to keep node storage bounded.

| Method | Args | Returns |
|--------|------|---------|
| `get_market_kline` | `market_id, [from], [limit]` | `pm_kline[]` (ascending by `seq`) |

Pagination is **offset-from-newest** (kept deliberately simple for thin clients): `from` is how many of the **newest** points to skip, `limit ≤ 1000` is the page size.
- `(market_id, 0, 1000)` → the latest ≤ 1000 changes.
- `(market_id, 1000, 1000)` → the previous 1000 (one page further back) — repeat with `from += 1000` to lazy-load older history.

Plot it as: x = `timestamp` (unix seconds), and one line per outcome `i` with y = `weights[i]` (or normalized `weights[i] / Σweights` for the implied probability).

---

## Computed DTOs

These wrap raw objects with values derived at read time (non-consensus).

**`pm_position`** — a bet plus its parimutuel payout:
```
{ bet: pm_bet_object,
  expected_payout: share_type,   // payout if this side wins (or realized once settled)
  market_status: int8,
  resolved_outcome: int16 }
```
`expected_payout` byte-mirrors `settle_market`: for an active bet it is the conditional payout if the chosen side wins (`amount + winners_pool × weight / Σweight − time_penalty`); once settled it is the realized `resolved_amount`.

**`pm_oracle`** — oracle object + `reliability_score` (bp `[0..10000]`, API heuristic blending the resolution-success ratio with the dispute-win ratio, minus a per-ban penalty). Non-consensus.

**`pm_market_weight_sums`** — per side/outcome `bets_sum` and `weight_sum` (weight sums are computed by scanning bets, since they are not stored):
```
{ market_type: uint8, bets_sum: share_type,
  outcomes: [ { outcome_index, label, bets_sum, weight_sum } ] }
```

**`pm_dispute_votes`** — the committee tally **plus a stake-weighted projection of the finalize cron**,
so a caller can show the live quorum status and the verdict that would be applied under the current votes:
```
{ votes: pm_dispute_vote_object[],
  // legacy rough tally (weight = |vote_percent|, NOT stake) — kept for compatibility
  uphold_weight, challenge_weight, total_weight,
  challenger_leads: bool,                          // ≥ pm_dispute_approve_min_percent (rough)
  proposed_outcome: int16,
  // ── accurate stake-weighted projection (mirrors pm_dispute_finalize) ──
  // every *_shares value is vesting-shares: effective_vesting_shares + lazy-pool stake → shares
  participation_shares,                            // Σ weight of accounts that voted (= max_rshares)
  electorate_shares,                               // total_vesting_shares + pool_NAV→shares (quorum base)
  quorum_required_shares,                          // electorate × pm_dispute_approve_min_percent
  quorum_percent_bp: int32,                        // participation / electorate (bp, 10000 = 100.00%)
  quorum_reached: bool,                            // participation_shares ≥ quorum_required_shares
  oracle_defense_shares, change_shares,            // rshares defending the oracle vs. backing a change
  outcome_change_shares: int64[],                  // per-outcome backing rshares (size = outcome_count)
  expected_uphold: bool,                           // true ⇒ oracle resolution stands if finalized now
  expected_outcome: int16,                         // outcome that would be set at finalize now
  expected_consensus_strength_bp: int32 }          // winning / participation (bp); 0 when uphold
```
> The projection uses the **same** stake weighting and lazy-pool→shares bridge as the on-chain
> `pm_dispute_finalize`, so `expected_outcome` / `quorum_reached` match what the cron will apply at
> `voting_end_time` *given the votes cast so far* (votes are revisable until then — see
> [dispute operations](../protocol/operations/prediction-markets.md)).

**`pm_kline`** — one charting point (per-outcome weight snapshot at a moment in time):
```
{ seq: uint32,           // 0-based, contiguous, monotonic per market (the change index)
  timestamp: uint32,     // unix seconds — x coordinate
  reason: uint8,         // 0 bet, 1 cancel, 2 liquidation, 3 batch settle, 4 leverage open, 5 leverage resolve
  bets_sum: share_type,  // total staked across all outcomes at this point
  weights: share_type[] }// per-outcome staked weight (y values), index = outcome_index
```

**`pm_market_full`** — one-call enriched market view (`oracle`/`meta` are `null` when absent; the `my_*`
arrays are empty unless an `account` argument was supplied):
```
{ market: pm_market_object,
  outcomes: pm_outcome_object[],            // empty for binary markets
  weight_sums: pm_market_weight_sums,
  oracle: pm_oracle | null,
  meta: pm_market_meta_object | null,
  my_positions: pm_position[],              // account's bets on THIS market
  my_leverage_positions: pm_leverage_position_object[],
  my_liquidity: pm_liquidity_object[] }
```

**`pm_leverage_quote`** — leverage-open preview (from `pm::leverage::*`, the same math the evaluator runs):
```
{ available: bool, outcome_index, collateral,
  max_loan, max_leverage_x100,              // 100 = 1.00×
  pool_free_amount, fund_available, per_position_cap, market_position_cap,
  pool_profit_percent, safety_margin_percent, max_slippage_percent, m_factor_percent,
  expiration_buffer_sec, auto_close_time,   // betting_expiration − buffer
  stops: [ { leverage_x100, loan, total_bet, expected_tokens, pool_profit,
             liquidation_threshold, current_cancel_value, worst_case_cancel_value } ],
  failed_constraints: [ { constraint, reason } ] }   // populated when !available
```
**`pm_leverage_close_preview`** — `{ position_id, outcome_index, cancel_value, pool_obligation, bettor_receives, collateral, loan, pool_profit_charge, closeable: bool, loss_vs_collateral, loss_percent_bp }`.
**`pm_leverage_convert_preview`** — `{ position_id, outcome_index, cancel_value, pool_obligation, current_profit, conversion_profit_cost_percent, conversion_fee, total_user_payment, convertible: bool }`.

**`pm_market_categories`** — browse taxonomy with live counts:
```
{ categories: [ { category, count, subcategories: [ { subcategory, count } ] } ],  // sorted by count desc
  hot_tags:   [ { tag, count } ] }                                                 // top 20 (jurisdiction-* excluded)
```

---

## Example

Fetch a bettor's positions:
```bash
curl -s --data '{"jsonrpc":"2.0","id":1,"method":"call",
  "params":["prediction_market_api","get_account_positions",["alice",0,100]]}' \
  http://127.0.0.1:8090
```

Fetch the latest 1000 chart points for market `42`, then the previous 1000:
```bash
# newest page
curl -s --data '{"jsonrpc":"2.0","id":1,"method":"call",
  "params":["prediction_market_api","get_market_kline",[42,0,1000]]}' http://127.0.0.1:8090
# one page older
curl -s --data '{"jsonrpc":"2.0","id":1,"method":"call",
  "params":["prediction_market_api","get_market_kline",[42,1000,1000]]}' http://127.0.0.1:8090
```

Thin-client charting (lazy-load older history on scroll-back), turning each point into
per-outcome series of `{ x: unixtime, y: weight }`:
```js
async function call(method, params) {
  const r = await fetch('http://127.0.0.1:8090', { method: 'POST',
    body: JSON.stringify({ jsonrpc: '2.0', id: 1, method: 'call',
      params: ['prediction_market_api', method, params] }) });
  return (await r.json()).result;
}

// Pull pages of 1000 from newest backwards until we have `want` points (or run out).
async function loadKline(marketId, want = 3000) {
  const points = [];
  for (let from = 0; points.length < want; from += 1000) {
    const page = await call('get_market_kline', [marketId, from, 1000]);
    if (!page.length) break;            // reached the start of history
    points.unshift(...page);            // pages are ascending; prepend older pages
    if (page.length < 1000) break;
  }
  return points;
}

// One {x,y} series per outcome — feed straight into any charting lib.
function toSeries(points, outcomeCount) {
  const series = Array.from({ length: outcomeCount }, () => []);
  for (const p of points)
    for (let i = 0; i < outcomeCount; i++)
      series[i].push({ x: p.timestamp, y: Number(p.weights[i]) });
  return series;
}
```

See [Prediction Market Operations](../protocol/operations/prediction-markets.md) for the on-chain objects these methods expose, and [Chain Properties](../governance/chain-properties.md) for the v5 governance parameters.
