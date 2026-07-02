# Prediction Markets — Thin-Client Implementation Plan & Prototype Reconciliation

> **Part A** is an implementation plan for a thin client (js / php / python) built **only** from the
> node wire contract in [`prediction-market-library-integration-spec.md`](./prediction-market-library-integration-spec.md).
> **Part B** reconciles that plan against the current PHP+`app.js` prototype documented in
> [`pm-workflow/prototype-frontend-design-document.md`](./pm-workflow/prototype-frontend-design-document.md):
> a per-endpoint correspondence matrix (what we have vs. what the prototype has) plus a prioritized
> list of node-side gaps to close.
>
> Terminology: **op** = signed operation (§3 of the spec); **API** = `prediction_market_api` read
> method (§5); **vop** = virtual operation (§4); bp = basis points.

---

# Part A — Thin-client implementation plan

## A.1 Target architecture

The prototype is a server-mediated app: the browser talks to PHP `/api/*` endpoints, PHP holds a
session, signs/settles on behalf of the user, and returns pre-computed JSON. The thin client
**removes the server**: it holds the user's VIZ keypair, reads chain state directly over JSON-RPC,
and **signs + broadcasts** every state change itself.

```
┌────────────────────────────────────────────────────────────┐
│ UI layer (screens; framework-agnostic)                       │
├────────────────────────────────────────────────────────────┤
│ View-model / aggregation (compose reads, derive badges,      │
│   compute leverage previews, risk & jurisdiction gating)     │
├──────────────────────────┬───────────────────────────────────┤
│ Read client              │ Write client                       │
│  call(prediction_market_ │  tx builder → op array →           │
│  api, method, [args])    │  sign(active/regular key) →         │
│  + generic account/      │  broadcast_transaction[_synchronous]│
│  history reads           │                                     │
├──────────────────────────┴───────────────────────────────────┤
│ Core: JSON-RPC transport · asset codec · key mgmt · ref-block │
└────────────────────────────────────────────────────────────┘
```

Three hard rules carried over from the spec:
- **Asset codec:** VIZ = 3 decimals. Ops take `asset` strings (`"10.000 VIZ"`); read objects return
  `share_type` integers (×1000). One shared codec both ways.
- **Percent scales are not uniform:** most `*_percent` are bp; `time_penalty` is `1e6=100%`; the
  `pm_leverage_*` / `pm_conversion_*` governance knobs are plain percent. See spec §1.1 / §9.
- **No server identity:** replace session/cookie with local key management; the `user` blob becomes
  derived reads (account balances + PM objects).

## A.2 Build layers

1. **Transport & core** — JSON-RPC client (`call`), tx assembly (ref-block, expiration, chain-id),
   secp256k1 canonical signing, `broadcast_transaction_synchronous`. Most of this already exists in
   `viz-js` / the php / python VIZ libs; PM adds no new transport.
2. **Asset & percent codec** — parse/format `"x.yyy VIZ"` ↔ integer milli-VIZ; bp/permille/percent
   helpers. Centralize to avoid the scale traps.
3. **Operation builders (21)** — one builder per user op (spec §3), each emitting
   `["<name>", {fields}]` with `extensions: []` and correct field order. Group: oracle (register,
   update, accept), market (create, resolve, no_contest), betting (place, commit, reveal, cancel,
   transfer), liquidity (add, withdraw), disputes (create, vote, resolve), lazy pool (deposit,
   withdraw), leverage (open, close, convert).
4. **Read client (23)** — thin wrappers over the API methods (spec §5) returning typed models.
5. **View-models** — aggregate multiple reads into the screen shapes the prototype expects
   (enriched market view, oracle profile, positions, boosts). This is where the prototype's
   single fat endpoints get reassembled client-side.
6. **Virtual-op & history decoder** — map account/block history (incl. the 11 vops, spec §4) into
   the 22 balance-history rows the prototype's wallet renders.

## A.3 Milestones

| M | Scope | Ops used | API used | Notes |
|---|---|---|---|---|
| **M0** | Transport, keys, asset codec, tx sign/broadcast | — | `get_pm_chain_properties` | Foundation; cache chain props at boot |
| **M1** | Browse & market detail (read-only) | — | `list_markets`, `list_markets_by_*`, `get_market`, `get_market_outcomes`, `get_market_weight_sums`, `get_market_bets`, `get_market_liquidity`, `get_oracle`, `list_oracles`, `get_market_meta`, `list_markets_by_category` | Compose the enriched detail view client-side |
| **M2** | Betting lifecycle | `pm_place_bet`, `pm_commit_bet`, `pm_reveal_bet`, `pm_cancel_bet`, `pm_transfer_position` | `get_account_positions` | Persist reveals in local storage; pin commit preimage (see gap I) |
| **M3** | Market creation + oracle role | `pm_create_market`, `pm_oracle_register`, `pm_oracle_update`, `pm_oracle_accept_market`, `pm_resolve_market`, `pm_no_contest` | `list_markets_by_oracle` (pending queue), `get_creator_ban` | Fold insurance deposit/withdraw into `pm_oracle_update` |
| **M4** | Disputes & governance | `pm_dispute_create`, `pm_dispute_vote` (committee), `pm_dispute_resolve` (account) | `get_dispute`, `get_dispute_votes` | Two dispute modes — richer than prototype (see B) |
| **M5** | Liquidity + lazy pool | `pm_add_liquidity`, `pm_withdraw_liquidity`, `pm_lazy_deposit`, `pm_lazy_withdraw` | `get_lazy_pool`, `get_lazy_deposit` | Emergency withdraw = `pm_lazy_withdraw(emergency=true)` |
| **M6** | Leverage / Boost | `pm_leverage_open`, `pm_leverage_close`, `pm_leverage_convert` | `get_account_leverage_positions`, `get_market_leverage_positions`, `get_lazy_pool` | **Previews computed client-side** unless node adds them (gap F) |
| **M7** | Charts & history | — | `get_market_kline` | Wallet history from account-history + vops |

## A.4 Cross-cutting concerns (client-owned policy)

- **Jurisdiction** — user country is a client preference (localStorage). Hide markets whose
  `pm_market_meta_object.banned_jurisdictions` contains it, or use the `jurisdiction` arg of
  `list_markets_by_category`. No on-chain per-user jurisdiction.
- **Risk gating** — `list_markets(show_risky)` hides under-insured markets (hardcoded 2.5×
  coverage floor). Any betting-time risk confirmation is pure client UX (no on-chain `risk_confirm`).
- **Commit-reveal resilience** — persist `{commit_id, side/outcome, amount, min_tokens, salt,
  reveal_deadline}` locally and retry `pm_reveal_bet` on load; drop past deadline.
- **Leverage previews** — reimplement the frozen max-leverage / slider-stops / close / convert math
  (see `libraries/chain/include/graphene/chain/pm/leverage.hpp`) unless preview API methods are
  added (gap F). Gate the whole feature on `pm_leverage_enabled`.
- **Charts** — use `get_market_kline` (offset-from-newest paging) instead of the prototype's
  reserve-snapshot probing.

---

# Part B — Prototype ↔ node correspondence

The prototype exposes **58 HTTP endpoints**. Each maps to one of: a node **op**, an **API** read, a
generic (non-PM) VIZ facility, or a client-side computation — or it has **no node equivalent yet**.

Status legend: **✅ direct** (op/method exists, 1:1) · **⚠ partial** (exists but shape/semantics
differ — client adapts) · **🔵 client-side** (no chain op needed; local logic or generic VIZ) ·
**❌ gap** (missing on the node; needs a decision — see B.9).

## B.1 Session, identity, config, preferences

| Prototype endpoint | Node mapping | Status | Comment |
|---|---|---|---|
| `check-session`, `auth-code`, `load-session` | local keypair; reads = `get_accounts` (generic) | 🔵 | Server session replaced by key mgmt; `user` blob = account balances + PM objects |
| `load-settings` | `get_pm_chain_properties` | ✅ | `min_risk_score_listing` → `pm_listing_min_coverage_percent`, `min_risk_score_betting` → `pm_betting_min_coverage_percent` (both now governance, see C). `dispute_grace_hours` = `pm_dispute_grace_sec`/3600 |
| `update-user-preferences` (lang, jurisdiction) | — | 🔵 | Client-side prefs (localStorage / optional account JSON metadata). No PM op |
| `register-oracle` | `pm_oracle_register` / `pm_oracle_update` | ✅ | Fee unit differs: prototype ‰ vs node **bp** — convert. Update path folds settings edits |
| `register-creator` | — | 🔵 | No on-chain creator role and none needed: **any account** may `pm_create_market`. Anti-spam is the per-market `pm_market_creation_fee` (see B.11), not a role. Keep any "creator" gate purely client-side (gap K) |
| `load-oracle-profile` | `get_oracle` (`pm_oracle_api_object`) | ⚠ | Node returns raw counters + `reliability_score` (**0–10000 bp**, not 0–100). `trust_score`, `risk_score` (coverage), `derived{}` rates must be computed client-side |

## B.2 Browse & filter

| Prototype endpoint | Node mapping | Status | Comment |
|---|---|---|---|
| `load-markets` | `list_markets(status, from, limit, show_risky)` | ⚠ | Same risk-floor behavior. No creator/oracle enrichment in one call — join with `get_oracle` client-side |
| `load-markets-catalog` | `list_markets` / `list_markets_by_category` | ✅ **(extended)** | `list_markets_by_category` now takes `subcategory`, `tag`, and `sort` (newest/oldest/volume/expiration) — gap B closed. `list_markets` still status-scoped |
| `load-categories` (taxonomy + counts + hot_tags) | `get_market_categories()` | ✅ **(added)** | Category/subcategory counts + top-20 hot tags over indexed markets — gap A closed |
| `load-oracles` | `list_oracles(from, limit)` | ✅ | Each row already carries fee/insurance/`reliability_score` |
| `load-committees` | — | 🔵/⚠ | "Committee" list = DAO voters (any staked account) for committee-mode, or a chosen resolver account for account-mode. No PM "list committees" method; pick resolver by account name |

## B.3 Market detail & binary betting

| Prototype endpoint | Node mapping | Status | Comment |
|---|---|---|---|
| `load-market-enriched` | `get_market_full(market_id, [account])` | ✅ **(added)** | One call: market + outcomes + weight sums + oracle + meta + the account's per-market bets/leverage/LP (gap G closed). Public `all_bets`/`all_liquidity` still via `get_market_bets`/`get_market_liquidity` |
| `load-market` | `get_market` | ✅ | Lightweight refresh |
| `place-bet` | `pm_place_bet` | ✅ | Handles binary (`side`) + batch (`mode=1`) + `min_tokens`. `risk_confirm` is client-side |
| `commit-bet` | `pm_commit_bet` | ✅ | Commitment preimage now byte-pinned in spec §3.6.1 (binary LE layout + golden vector). Prototype's colon-string preimage is **wrong** for this node |
| `reveal-bet` | `pm_reveal_bet` | ✅ | Same field set; persist for retry |
| `cancel-bet` | `pm_cancel_bet` | ✅ | `min_return` = slippage floor |
| `transfer-position` | `pm_transfer_position` | ✅ | Has `amount` (weight) + `memo` |

## B.4 Multi-outcome markets

| Prototype endpoint | Node mapping | Status | Comment |
|---|---|---|---|
| `create-market-multi` | `pm_create_market` (`market_type=1`, `outcomes[]`, `lmsr_b`) | ✅ | Node **unifies** binary/multi into one op. `lmsr_b` client-computed (spec §3.3) |
| `place-bet-multi` | `pm_place_bet` (`outcome_index`, `side=-1`) | ✅ | Same op as binary |
| `cancel-bet-multi` | `pm_cancel_bet` | ✅ | Same op (prototype's separate multi endpoint + its field-drift bugs disappear) |
| `add-liquidity-multi` | `pm_add_liquidity` | ✅ | Unified; fixes prototype's "multi never calls multi endpoint" bug |
| `withdraw-liquidity-multi` | `pm_withdraw_liquidity` | ✅ | Unified |
| `resolve-market-multi` | `pm_resolve_market` (`winning_outcome`) | ✅ | Unified; single canonical `winning_outcome` key resolves prototype's `outcome`/`winning_outcome` mismatch |

## B.5 Market creation & oracle role

| Prototype endpoint | Node mapping | Status | Comment |
|---|---|---|---|
| `create-market` (binary) | `pm_create_market` (`market_type=0`) | ⚠ | `q`/`resolution`/localized titles/`category`/`tags`/`banned_jurisdictions` go into the free-form `metadata` JSON; only resolution-criteria/title → `url`; outcome labels → `outcomes[]`. Fees ‰→bp. `committee_id` → `dispute_mode`+`dispute_resolver` (see B.6) |
| `oracle-deposit-insurance` / `oracle-withdraw-insurance` | `pm_oracle_update(insurance_delta)` | ✅ | Signed delta; withdraw blocked while active markets / below min |
| `oracle-accept-market` | `pm_oracle_accept_market(accept=true, …)` | ✅ (richer) | Node also lets the oracle **quote** `oracle_fee_percent`/`oracle_fixed_fee` ≤ creator's ceiling; prototype had no negotiation |
| `oracle-reject-market` | `pm_oracle_accept_market(accept=false)` | ✅ | Same op, `accept=false` |
| `load-pending-markets` | `list_markets_by_oracle` + filter `status==0` | ⚠ | No dedicated "pending for oracle" query; filter client-side |
| `resolve-market` (binary) | `pm_resolve_market` | ✅ | Carries `decision_url` + `decision_reason`, both stored on `pm_market_object` and readable via `get_market` (gap J closed) |
| `oracle-no-contest` | `pm_no_contest(reason)` | ✅ | Node has it (prototype never wired a button — add one) |

## B.6 Disputes & governance

| Prototype endpoint | Node mapping | Status | Comment |
|---|---|---|---|
| `create-dispute` | `pm_dispute_create(proposed_outcome, reason)` | ⚠ | Prototype sends only `claim_url`; node requires **`proposed_outcome` + `reason`** (embed the claim link in `reason`). Fee = `pm_dispute_fee` |
| `dispute-oracle-response` (oracle rebuttal text) | `pm_dispute_oracle_respond` | ✅ **(added)** | Oracle writes `response` onto the open dispute (within `oracle_response_deadline`); surfaces in `get_dispute` as `oracle_response` / `oracle_response_time` (gap D closed) |
| `resolve-dispute` (single arbitrator) | **account mode:** `pm_dispute_resolve` · **committee mode:** `pm_dispute_vote` → cron `pm_dispute_finalize` | ⚠ | Prototype = single-arbitrator. Node splits: account-mode `pm_dispute_resolve` matches 1:1 (correct_outcome, penalty_amount, ban_oracle/creator +until); committee-mode is **stake-weighted DAO voting** with auto-finalize — a richer flow the prototype lacks. Client must support both (branch on `market.dispute_mode`). See `get_dispute_votes` projection. **Bans are a regulator/compliance feature, exclusive to account mode**: an account-mode resolver (e.g. a regulator) can bar both the oracle and the creator; committee/DAO mode only slashes + dings reputation, never bans (by design) — see spec §3.15 |
| `unban-user` | `pm_unban(resolver, target, unban_oracle, unban_creator)` | ✅ **(added)** | Only the `banned_by` resolver may lift a ban it set; otherwise bans expire at `banned_until` (gap E closed) |
| `load-committees` | — (see B.2) | ⚠ | Committee-mode voters = staked accounts (DAO); account-mode resolver = a named account |

## B.7 Wallet, history, positions, LP, lazy pool

| Prototype endpoint | Node mapping | Status | Comment |
|---|---|---|---|
| `load-history` (22 types) | account-history plugin + PM vops (spec §4) | ⚠ | Reconstruct rows from generic transfers + PM vops (`pm_payout`, `pm_batch_settle`, `pm_commit_forfeit`, `pm_oracle_missed_penalty`, `pm_leverage_*`, …). No PM-specific history endpoint |
| `load-positions` | `get_account_positions` | ✅ | Returns bet + `expected_payout` + market status |
| `load-payouts` | account-history vops (`pm_payout`/`pm_auto_payout`) | ⚠ | No dedicated payout-list API; derive from vop history |
| `load-market-payouts` | `get_market_bets` (`resolved_amount`) or per-market `pm_payout` vops | ⚠ | No per-market payout method |
| `withdraw-viz` | generic `transfer_operation` | 🔵 | Not PM |
| Deposit / top-up (TON/USDT) | off-chain exchange | 🔵 | Not PM |
| `add-liquidity` / `withdraw-liquidity` | `pm_add_liquidity` / `pm_withdraw_liquidity` | ✅ | Binary + multi unified |
| `lazy-pool-deposit` | `pm_lazy_deposit` | ✅ | |
| `lazy-pool-withdraw` | `pm_lazy_withdraw(shares, emergency=false)` | ✅ | |
| `lazy-pool-emergency-withdraw` | `pm_lazy_withdraw(emergency=true)` | ✅ | Folded into one op |
| `lazy-pool-info` | `get_lazy_pool` + `get_lazy_deposit` + `get_lazy_allocations` / `get_market_lazy_allocation` | ✅ | Pool + user deposit + `active_allocations[]` all covered (gap H closed); oracle penalty stamps ship on `pm_oracle_object` via `get_oracle` |

## B.8 Leverage / Boost

| Prototype endpoint | Node mapping | Status | Comment |
|---|---|---|---|
| `leverage-open` | `pm_leverage_open` | ✅ | `collateral`, `loan`/leverage, `min_tokens`, `max_slippage_percent`. Gated by `pm_leverage_enabled` |
| `leverage-close` | `pm_leverage_close(min_return)` | ✅ | |
| `leverage-convert` | `pm_leverage_convert(conversion_profit_cost)` | ✅ | Must equal median(`pm_conversion_profit_cost_percent`) |
| `leverage-info` | `get_account_leverage_positions` | ✅ | Countdown computed from market expiration − buffer |
| `leverage-pool-state` | `get_lazy_pool` | ⚠ | `free_balance`, `leverage_fund_used` present; `leverage_fund_available` computed from `pm_leverage_fund_percent` client-side |
| `leverage-preview` | `get_leverage_quote(market_id, outcome_index, collateral)` | ✅ **(added)** | Returns max_loan/max_leverage, caps, `failed_constraints[]`, and up to 12 slider stops — same in-node math |
| `leverage-close-preview` | `get_leverage_close_preview(position_id)` | ✅ **(added)** | cancel_value, pool_obligation, bettor_receives, closeable |
| `leverage-convert-preview` | `get_leverage_convert_preview(position_id)` | ✅ **(added)** | cancel_value, current_profit, conversion_fee, total_user_payment (gap F closed) |

## B.9 Node-side gaps & action items (prioritized)

These are the concrete things **missing on our side** relative to the prototype workflow, plus the
decision each needs. "Client-only" means the thin client can proceed without node changes.

| # | Gap | Status | Impact / Recommendation |
|---|---|---|---|
| **F** | Leverage preview/close/convert quote API | ✅ **DONE** | Added `get_leverage_quote` / `get_leverage_close_preview` / `get_leverage_convert_preview` to the `prediction_market_api` plugin, calling the same frozen `pm::leverage::*` math the evaluators use |
| **A** | Category taxonomy / counts / hot-tags API | ✅ **DONE** | Added `get_market_categories` (per-category/subcategory counts + top-20 hot tags) |
| **B** | Sort + subcategory/tag filter in list API | ✅ **DONE** | Extended `list_markets_by_category` with `subcategory`, `tag`, `sort` (newest/oldest/volume/expiration) |
| **I** | Commit-reveal preimage not byte-pinned in spec | ✅ **DONE** | Spec §3.6.1 now documents the exact binary preimage (little-endian ints, 32-byte zero-padded account, raw salt — **not** a colon string) with a golden SHA-256 test vector, from `verify_commit` in `pm_evaluator.cpp` |
| **D** | No oracle dispute-rebuttal op | ✅ **DONE** | Added `pm_dispute_oracle_respond` (op-id 98); response stored on the dispute object (`oracle_response` / `oracle_response_time`), read via `get_dispute`. Open-dispute + within `oracle_response_deadline`; re-post overwrites |
| **E** | No standalone unban op | ✅ **DONE** | Added `pm_unban` (op-id 99). Bans now record `banned_by` (the account-mode resolver); only that resolver may lift, else expiry at `banned_until` |
| **K** | No on-chain creator role | 🔵 by design | Any account may create a market; anti-spam = per-market fee (B.11). Keep creator gating client-side |
| **J** | `pm_resolve_market` has no free-text justification | ✅ **DONE** | Added `decision_reason` to `pm_resolve_market` (≤1024 chars). Both `decision_url` and `decision_reason` are now **stored on `pm_market_object`** (like an oracle's rules_url) and readable via `get_market`; `pm_no_contest.reason` also lands in `decision_reason` |
| **G** | No enriched single-call market view | ✅ **DONE** | Added `get_market_full(market_id, [account])` — market + outcomes + weight sums + oracle + meta + the account's per-market bets/leverage/LP in one call |
| **H** | No API for lazy allocations / oracle penalty stamps | ✅ **DONE** | Added `get_lazy_allocations(from,limit)` + `get_market_lazy_allocation(market_id)`. Oracle penalty stamps already ship on `pm_oracle_object` via `get_oracle` |
| **C** | Risk thresholds hardcoded (2.5×), not governance | ✅ **DONE** | Promoted to governance: `pm_listing_min_coverage_percent` (250) drives `below_risk_floor`; `pm_betting_min_coverage_percent` (150) published for client risk-confirm |

> **Every lettered gap (A–K) is now closed.** F/A/B/G/H were non-consensus plugin reads; C promoted
> the risk floor to governance; I pinned the commit preimage; D/E/J were added as consensus operations
> (pre-hardfork design window). Additionally, ban expiry now emits the `pm_ban_expired` virtual op
> (per-block cron), so ban lifts are observable both ways (manual `pm_unban` op + automatic vop).

**Unit/semantic reconciliations (client adapts, no node change):**
- Fees: prototype **‰ (per-mille)** ↔ node **bp**. Convert (×10).
- `dispute_grace_hours` (prototype hours) ↔ `pm_dispute_grace_sec` (node seconds).
- Oracle `reliability_score`: node **0–10000 bp** ↔ prototype **0–100**.
- `trust_score` / `risk_score` / `derived{}` rates: compute client-side from `pm_oracle_object`
  counters + market coverage.
- Committee semantics: prototype single-arbitrator ↔ node account-mode resolver **or**
  committee-mode stake-weighted DAO vote.
- `leverage-preview` collateral ×1000 double-scaling quirk (prototype bug) — do **not** replicate;
  send `asset`/`share_type` per the spec once.

## B.10 Coverage summary

| Bucket | Count | Endpoints |
|---|---|---|
| ✅ direct op/method | 31 | betting, cancel, transfer, liquidity (all), lazy deposit/withdraw/emergency/**allocations**, leverage open/close/convert/info + **preview ×3**, create (uni), resolve (uni, +justification), no-contest, accept/reject, insurance, oracle register, list-oracles, positions, **categories/sort**, **enriched market**, **oracle-rebuttal**, **unban** |
| ⚠ partial (client adapts) | 10 | oracle-profile (derived rates), create-market metadata mapping, dispute-create (proposed_outcome), resolve-dispute (2 modes), pending queue, history/payouts, leverage-pool-state, market-payouts |
| 🔵 client-side / generic VIZ | 7 | session/auth, preferences, withdraw-viz, deposit, committees list, register-creator |
| ❌ open | 0 | — all lettered gaps closed |

The thin client reaches **full feature parity** for the core lifecycle (browse → bet → create →
resolve → dispute → LP → lazy pool → leverage). Every gap A–K is closed; ban lifts also emit the
`pm_ban_expired` virtual op on automatic expiry. Remaining ⚠ rows are shape/semantic adaptations
the client handles locally, not missing node capability.

## B.11 Market-creation fee flow (answer: where the fee goes)

Any account may `pm_create_market` — there is no on-chain "creator" registration. The economics,
read straight from `pm_create_market_evaluator` / `pm_oracle_accept_market_evaluator`:

1. **At creation**, the creator pays two separate amounts up front:
   - `pm_market_creation_fee` (default **5.000 VIZ**) — deducted immediately and added to
     `committee_fund` (the DAO fund). **Non-refundable anti-spam fee.**
   - `liquidity` (the seed, ≥ `pm_min_liquidity`) — moved into the market as the creator's first LP
     position.
2. **The market starts pending** (`status = 0`) waiting for the named oracle, unless it is a
   self-oracle or matches the oracle's auto-accept policy (then it activates immediately).
3. **If the oracle rejects** (`pm_oracle_accept_market` with `accept=false`): the creator's
   **liquidity seed is refunded in full** (`return_liquidity`) and the market is marked deleted
   (`status = -1`). The **`pm_market_creation_fee` is NOT refunded** — it already went to the DAO
   fund at creation and stays there.

So: *"any user can try to create a market by paying a fee and requesting an oracle; if the oracle
declines, the seed liquidity comes back but the creation fee is forfeited to the DAO fund."* The
`pm_oracle_registration_fee` behaves the same way (→ `committee_fund`, non-refundable). Thin-client
UX should make the non-refundable fee explicit before the user picks an oracle, and ideally steer
them to oracles with an `auto_accept` policy (or a self-oracle) to avoid a wasted fee.

---

# Part C — Thin-client-only UX (not in the node, but required)

These have **no chain operation and no API method** — they are pure client responsibilities the
prototype's server used to cover implicitly. A VIZ thin client must implement them itself; list
them as first-class UI/onboarding work, not afterthoughts.

| Concern | What the client must do | Persist where | Notes |
|---|---|---|---|
| **Key management / entry** | Import or generate the account's VIZ keys (active for spend ops, regular for `pm_dispute_vote`); sign every op locally. Never send keys anywhere | Encrypted local store (never plaintext, never to a server) | Replaces the prototype's server session entirely. Offer key import (WIF), optional password-encrypted keystore, and a clear "log out = wipe keys" |
| **Node endpoint selection** | Let the user choose / edit the JSON-RPC **HTTPS node address** (and a fallback list); validate reachability via a cheap read (e.g. `get_pm_chain_properties`) | Local settings | Default to a known public node but always editable; show which node is active. All reads/broadcasts go here |
| **Language / locale** | UI language switch (en/ru/…); localize market titles/labels from each market's `metadata` JSON | Local settings (localStorage) | No on-chain language pref — `update-user-preferences` disappears. Localization of market content is a client concern (metadata may carry `title_en`/`title_ru`, etc.) |
| **Jurisdiction selection** | User picks their country (ISO code); use it to hide markets whose `pm_market_meta_object.banned_jurisdictions` includes it, or pass to `list_markets_by_category(jurisdiction=…)`; show the "restricted here" interstitial on a banned market | Local settings | No on-chain per-user jurisdiction. Purely a client filter + warning |
| **Risk acknowledgment (betting)** | When a market is under-insured (below the coverage floor, or hidden unless "show risky"), require an explicit confirm before betting | Ephemeral (per action) | No on-chain `risk_confirm`; the node's only risk gate is hiding under-insured markets from the default listing. Betting-time confirmation is pure UX |
| **Legal / risk disclaimer & agreement** | On first run (and on version change) show a **terms/disclaimer** the user must accept: *software used at own risk; user is solely responsible for complying with the laws of their own jurisdiction; user assumes all financial and legal risk; no warranty; the client is non-custodial and cannot recover lost keys or funds* | Local flag (accepted version + timestamp) | Not in the node. Gate app usage on acceptance; re-prompt when the disclaimer text version changes. Pair with the jurisdiction selection above |
| **Non-refundable fee disclosure** | Before create-market / oracle-register, state that the fee is non-refundable and where it goes (DAO fund), and warn that an oracle rejection refunds only the seed liquidity (see B.11) | Ephemeral (per action) | Prevents surprise loss of the creation fee |
| **Commit-reveal persistence** | Persist pending reveals `{commit_id, side/outcome, amount, min_tokens, salt, reveal_deadline}` and retry `pm_reveal_bet` on load | Local store | Consensus-critical salt must survive reloads or the escrow is forfeited (see gap I) |

> These items are the onboarding/compliance surface of a non-custodial client. Treat the **legal
> disclaimer + jurisdiction acceptance** and **secure key handling** as launch blockers — everything
> else in the plan assumes an authenticated, consenting user pointed at a chosen node.
