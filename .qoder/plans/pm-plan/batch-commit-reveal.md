# Plan: Opt-in Batch & Commit-Reveal Betting (Front-Running Protection)

> Implementation plan to extend the Onix Protocol with **optional** anti-front-running execution, while keeping the current **instant per-bet** execution as the default. To be folded into [onix-protocol-specification.md](onix-protocol-specification.md).
>
> 🇷🇺 Russian: [plan_batch_commit_reveal_betting-ru.md](plan_batch_commit_reveal_betting-ru.md). Background: [.qoder/docs/theory_concepts/FORECASTER-FIT.md](../.qoder/docs/theory_concepts/FORECASTER-FIT.md) → "Mitigations for the reflexivity family".

## 1. Goal

The current CPMM is **path-dependent**: a bet's tokens depend on reserves at execution time, so a public mempool tx can be sandwiched/front-run. We add **two opt-in execution modes** that the bettor chooses *per bet* (checkboxes in UI), without changing the default flow or the LP guarantee:

- **Instant (mode 0, default)** — unchanged. Immediate per-bet execution against the live `a·b=k` curve. Best UX, no protection.
- **Batch (mode 1)** — bet is deferred to the next epoch boundary and settled together with all other batch bets at a **single uniform price**. Solves intra-batch ordering/front-running.
- **Commit-Reveal (mode 2)** — bet is **hidden** (hash commit) then revealed; revealed bets settle in the batch. Strongest protection: a front-runner can't even see direction/size. If the commit is **not revealed**, a penalty is taken and **added to the winners' pool** at resolution.

> // NOTE: mode 2 is mode 1 + confidentiality. Both share the same batch-settlement engine.

## 2. UI

At bet time, two checkboxes (default OFF → instant):

- ☐ **Protect from front-running (batch)** → `mode = 1`
- ☐ **Hide my bet until reveal (commit-reveal)** → `mode = 2` (implies batch). Surfaces the salt/escrow flow.

> // NOTE: For high-`endogeneity_tier` markets (tier 3, political/social) the client MAY default commit-reveal ON. Protocol stays neutral.

## 3. New / changed parameters

### Global (governance, `settings` table)

| Key | Default | Unit | Description |
|-----|---------|------|-------------|
| `batch_epoch_blocks` | 20 | blocks | Epoch length (~60s at 3s blocks). Batch settlement runs at each boundary. |
| `reveal_window_blocks` | 200 | blocks | Grace margin to reveal after commit (~10 min). NOT a deliberation window — reveal is normally auto-immediate; this is liveness slack for a dropped tx / offline client. Generous on purpose (see §5.3 NOTE). |
| `commit_no_reveal_penalty_permille` | 200 | ‰ | No-reveal fee (**20%**) on escrow → **winners' pool**. Delegate-voted; the value is **carried in `pm_commit_bet` and consensus-checked** (see §5.2). |
| `min_batch_bet` | 1,000 | mVIZ | Minimum amount for batch/commit-reveal (anti-dust spam in queues). |
| `commit_reveal_enabled` | 1 | 0/1 | Global kill-switch for hidden mode. |
| `batch_price_band_permille` | 20 | ‰ | **Binary only**: max deviation of a batch fill's live execution price from the epoch-open snapshot price; beyond it the fill auto-refunds (anti-manipulation). See §6.1. |

### Per-market (creator-set, oracle-confirmed)

| Param | Type | Description |
|-------|------|-------------|
| `allow_batch` | 0/1 | Enable batch & commit-reveal for this market (default 1). Set 0 for instant-only markets. |
| `allow_instant_bet` | 0/1 | Enable instant `mode=0` for this market (default 1). Set 0 to force every order through batch / commit-reveal (anti-MEV). At creation we require `allow_instant_bet OR allow_batch == 1`, otherwise the market is unbettable. Multi-outcome (LMSR) markets force this back to 1 server-side because batch settlement is binary-only today. |
| `endogeneity_tier` | 1/2/3 | Reflexive-risk tag (1 econ-data, 2 sports/scheduled, 3 political/social). Display + client policy. |

## 4. Epochs & lifecycle

```
Epoch N (batch_epoch_blocks long)
  ├─ INSTANT bets:        execute live against current reserves (unchanged)
  ├─ BATCH bets:          queued with epoch=N
  ├─ COMMIT bets:         escrow locked, commitment stored, reveal_deadline = now + reveal_window_blocks
  └─ REVEAL ops:          attach revealed bet to the batch of the FIRST epoch boundary at/after the reveal
                          (clients SHOULD reveal a few blocks BEFORE that boundary — see §5.3 / §11.1)
Epoch boundary (block % batch_epoch_blocks == 0)
  └─ VIRTUAL pm_batch_settle(market, N): settle all queued bets for the market in one curve update
reveal_deadline reached without reveal
  └─ VIRTUAL pm_commit_forfeit(commit_id): penalty → market.forfeit_pool, refund the rest
```

> // NOTE: batch settlement runs **per market that has a non-empty queue**, not globally — keeps virtual-op cost bounded.

## 5. Operations

### 5.1 `pm_place_bet` — add `mode` field

```
pm_place_bet { market_id, side, amount, min_tokens, mode }
  mode = 0 (INSTANT):  current behavior, unchanged.
                       Reject if market.allow_instant_bet == 0 (creator opted out of instant settlement).
  mode = 1 (BATCH):    require market.allow_batch == 1, amount >= min_batch_bet.
                       Lock `amount`, enqueue { bet, side, amount, min_tokens, submit_time, epoch }.
                       Bet status = 5 (queued). No reserve change yet.
```

### 5.2 `pm_commit_bet` — commit-reveal phase 1

```
pm_commit_bet { market_id, commitment, escrow_amount, no_reveal_fee_permille }
  require commit_reveal_enabled == 1 && market.allow_batch == 1
  require escrow_amount >= min_batch_bet
  // CONSENSUS CHECK (agreement with the chain): the user must declare the exact penalty rate
  //   currently mandated by delegates. Reject the tx if it disagrees.
  require no_reveal_fee_permille == settings.commit_no_reveal_penalty_permille
  commitment = H(market_id || account || side || amount || min_tokens || salt)   // binds everything
  Lock escrow_amount.
  Store { commit_id, market, account, commitment, escrow_amount, commit_time,
          no_reveal_fee_permille,                              // RATE LOCKED at commit time
          reveal_deadline = now + reveal_window_blocks, status = COMMITTED }
```

> // NOTE: `commitment` binds account + market so a commit can't be replayed on another market/user.
> // `escrow_amount` must be ≥ the eventual `amount`; surplus is refunded at reveal.
> // WHY carry the fee in the tx: it makes the penalty an explicit *agreement with consensus* — the
>   user signs the rate they accept, the node validates it equals the live delegate-voted value, and the
>   rate is then **snapshotted on the commitment** so a later governance change can't alter it retroactively.

### 5.3 `pm_reveal_bet` — commit-reveal phase 2

```
pm_reveal_bet { commit_id, side, amount, salt, min_tokens }
  require status == COMMITTED && now <= reveal_deadline
  require H(market_id || account || side || amount || min_tokens || salt) == commitment   // verify
  require amount <= escrow_amount
  refund (escrow_amount - amount) to account
  enqueue { bet, side, amount, min_tokens, submit_time = commit_time, epoch = FIRST boundary at/after reveal }
  status = REVEALED
```

> // INVARIANT: a mismatched reveal (wrong hash) is rejected; the commit then forfeits at deadline (§5.5).
> // NOTE (reveal timing — matters for front-run resistance, see §11.1): the public reveal→settlement gap is
>   the ONLY residual front-run window, so the client SHOULD reveal a few blocks BEFORE the next boundary to
>   keep that gap ≈ 1–2 blocks (NOT immediately after commit — that maximises the exposed window).
> // `reveal_window_blocks` (200) is a liveness FALLBACK only: if the client was offline and missed its target
>   boundary it can still reveal later (settling at a subsequent boundary, accepting more exposure) instead of
>   forfeiting. The **20% no-reveal fee** (not the window length) is what deters "commit-then-decide" optionality,
>   and `min_tokens` (baked into the hash) auto-refunds the bet if price drifted past the floor during the gap.

### 5.4 `pm_batch_settle` — VIRTUAL, at epoch boundary

See §6 for the math. Deterministic, generated at block time, validated by every node.

### 5.5 `pm_commit_forfeit` — VIRTUAL, at reveal_deadline if unrevealed

```
// use the rate LOCKED on the commitment, not the current chain value
penalty = floor(escrow_amount * commitment.no_reveal_fee_permille / 1000)
market.forfeit_pool += penalty            // boosts the winners' pool at resolution (per design)
refund (escrow_amount - penalty) to account
status = FORFEITED
```

## 6. Batch settlement math (uniform price, LP-safe)

**Requirements:** (a) one uniform price per side within a batch (no ordering edge), (b) order-independent / deterministic, (c) the LP invariant `reserve_a + reserve_b ≥ L` MUST still hold.

**Method — aggregate each side into ONE CPMM op, in a fixed canonical order.** Because each step is a standard CPMM transition, the AM-GM proof (spec §5) and `k`-preservation are **inherited for free**.

```
// pm_batch_settle(market, epoch)
(Ra, Rb, k) = market.reserves            // AFTER any instant bets in this epoch
queued = bets where market_id == market && epoch == epoch && status in {5 queued, REVEALED}
A_in = Σ amount for queued on side A
B_in = Σ amount for queued on side B

// Canonical order → deterministic & order-independent. Process larger aggregate first; tie → A.
order = (A_in >= B_in) ? [A, B] : [B, A]

for side in order where side_total > 0:
    if side == A:
        new_Rb = Rb + A_in;  new_Ra = floor(k / new_Rb);  T = Ra - new_Ra
    else:
        new_Ra = Ra + B_in;  new_Rb = floor(k / new_Ra);  T = Rb - new_Rb
    (Ra, Rb) = (new_Ra, new_Rb)          // k unchanged: betting, not a liquidity event
    uniform_price = side_total / T        // identical for every bettor on this side

    // Slippage pre-pass (single recompute): drop bettors whose share < min_tokens, refund them,
    //   then recompute T/uniform_price once over the survivors.
    // TODO: confirm single-pass is sufficient; alternative = iterate to fixpoint (costlier).
    for bettor i on side:
        tokens_i = floor(T * amount_i / side_total)
        if tokens_i < bettor_i.min_tokens: reject & refund (bet not placed)
        else: record bet { weight = tokens_i, time_penalty by submit_time }   // §8
    dust = T - Σ tokens_i → DAO fund       // consistent with spec §7 rounding

market.reserves = (Ra, Rb)
// INVARIANT (assert): Ra + Rb >= L   — holds by inheritance from standard CPMM steps
```

> // NOTE (fairness): the side processed second is priced after the first side moved the curve.
>   Within each side everyone is uniform, and **no external actor can insert between individual bets** →
>   front-running is eliminated. Cross-side price impact within a batch is accepted as a minor tradeoff.
> // OPTIONAL (advanced, later): internally MATCH opposing flow at the clearing price and push only the
>   net residual through the curve, for tighter pricing. Leave as future work — must re-prove the invariant.

**Onix Multi (LMSR):** identical idea — aggregate per-outcome `Δ` and settle via one combined `C(q+Δ)−C(q)` cost step per epoch, uniform price per outcome. `b` and the parimutuel settlement are untouched.

### 6.1 Reconciling instant and batch flow (epoch-open snapshot)

> **✅ [Unified Parimutuel Settlement](plan_unified_parimutuel_binary.md) is now implemented** (binary settles parimutuel in code), so this section simplifies: payout is capped at `losers_pool` regardless of weights for **both** types → **both use snapshot pricing** for full manipulation immunity, and `min_tokens` gates slippage. The binary-specific "live curve + price band" carve-out (and `batch_price_band_permille`) below is **no longer needed** — kept only as historical context for the pre-unification design.

To stop instant bets from front-running the batch, the batch is priced against a **snapshot of the curve taken at the epoch's open** (= the previous epoch's closing reserves), frozen *before* any of this epoch's instant flow — so no intra-epoch instant bet can move the batch's clearing price. This is the user's intuition: **the batch fires on the previous epoch's parameters.**

```
boundary (E-1 → E):  snapshot S_E = (A0,B0) / q0      // FROZEN — batch of E prices here
during E:            instant bets run live on C_live (k preserved); batch bets queue
boundary (E → E+1):  pm_batch_settle prices the queue at S_E, reconciles into C_live, opens E+1
```

Merging the two flows (instant on `C_live`, batch on `S_E`) into one E+1 curve **without breaking the LP invariant** is done differently per market type:

**Onix Multi — snapshot pricing is fully safe.** Parimutuel solvency is *independent of token counts*:
`disbursed = winning_bets + (losing_bets − fees) + subsidy = all_VIZ_held − fees ≤ held`, for ANY token totals. Snapshot mispricing only shifts the *ratio* that splits `winners_pool`; it never threatens solvency or the (unconditionally returned) LP subsidy. So:
- price each batch outcome's `Δ` at snapshot `q0`, mint tokens, collect the real VIZ into the pool;
- open E+1 with `q = q_live_after_instant + batch_Δ`.
→ Full intra-epoch manipulation immunity, zero invariant risk.

**Onix Binary — `weight` is a claim funded by losers, so settle on the live curve (do NOT force snapshot tokens).** There is no "token emission": `weight` is the bettor's claim — their own principal plus a profit (`weight − amount`) **paid by the losers**. `k` is constant under betting (spec §5; it only changes on liquidity add/withdraw) and maker liquidity `L` keeps `k>0`, so bets just slide the reserves along the curve — there is no "k→0". The real point: the snapshot (pre-move) price is **better than the live price**, so handing it to a batch bettor creates a **larger winner claim than curve-pricing would** — and on-curve pricing is exactly what guarantees the spec's surplus condition `loser_bets − fees ≥ winner_weights − winner_amounts` (betting-rules §Resolution). Off-curve claims can exceed what losers funded, forcing either pro-rating winners (no longer "payout = weight" — breaks the binary model) or tapping LP principal (breaks the guarantee). (Harmless for Multi, where `weight` only sets the split ratio of the losers' pool.) So for binary:
- settle the batch as a **standard aggregated CPMM op on the live curve** (§6) → reserves stay valid, `k` unchanged, AM-GM invariant inherited automatically;
- filter each order by its committed **`min_tokens`**: orders whose floor isn't met at the live settlement price drop out and refund; survivors are aggregated and applied (`reserve_b += Σamount`, `reserve_a −= Σtokens`).
- This gives manipulation **resistance**, not immunity: an attacker who moved the price before the boundary either (a) only degrades the fill within the victim's own slippage tolerance, or (b) gets the victim's bet refunded and is left holding a pumped position with no victim flow to exit against (unprofitable). Optional extra: a **snapshot price band** `batch_price_band_permille` to refund fills that deviate too far from the epoch-open price (largely redundant with `min_tokens`).

> // RESULT: Multi = full snapshot immunity. Binary = invariant-safety + band-bounded manipulation
>   (+ the ~1-block reveal window of §11.1 + min_tokens). Full binary immunity needs mode 3 (encrypted bids).
> // TODO (binary): formal invariant proof IF we ever want a pure-snapshot binary variant (full immunity w/o crypto).

**Liquidity ops:** `k`/`b`-changing ops apply to `C_live` and are picked up by the NEXT epoch's snapshot; they do not retroactively alter a frozen `S_E` (an in-flight batch prices at pre-add reserves — acceptable, minor). // see §8 block-ordering TODO

## 7. Non-reveal → winners' pool (resolution change)

Add `forfeit_pool` to the market; merge it into the winners' pool at resolution:

```
winners_pool = losers_sum − oracle_fee − creator_fee − liq_fee + market.forfeit_pool
// distributed pro-rata by winning tokens, exactly like losers_sum
```

> // NOTE: no profitable attack — a forfeiter pays the penalty and receives **zero tokens**, so they can't
>   recapture it; the penalty only helps the (other) winners. This also deters the "commit optionality,
>   reveal only the favorable side" game.
> // EDGE: if there are no winning tokens (edge cases in spec §6), `forfeit_pool` follows the same path as
>   undistributed `winners_pool` (LP bonus / DAO). Align with the existing edge-case table.

## 8. Interaction with existing mechanics

| Mechanic | Behavior |
|----------|----------|
| **Time penalty (spec §8)** | Computed from **submit/commit time**, NOT settlement time → users aren't penalized for the batch/reveal delay. // important |
| **Slippage** | `min_tokens` enforced at settlement; failure → refund (bet not placed). For mode 2 it's inside the commitment hash → un-changeable. |
| **Cancellation (spec §11)** | A queued batch bet (status 5) MAY be cancelled before settlement. A COMMITTED bet CANNOT be free-cancelled before `reveal_deadline` (else commit-reveal = free optionality); the only exits are reveal+settle or forfeit. // TODO confirm policy |
| **Liquidity add/withdraw** | `k`-changing events still only on liquidity ops; must be sequenced **before** `pm_batch_settle` within a block to keep `k` consistent for the batch. // TODO define block-level op ordering |
| **Dispute / resolution** | Unaffected, except `winners_pool` now includes `forfeit_pool`. |
| **Self-oracle / lazy pool** | Unaffected. |

## 9. Database schema changes

```sql
-- bets: execution mode + queueing
ALTER TABLE `bets`
  ADD COLUMN `mode` tinyint NOT NULL DEFAULT 0,        -- 0 instant, 1 batch, 2 commit-reveal
  ADD COLUMN `epoch` int NOT NULL DEFAULT 0,           -- settlement epoch
  ADD COLUMN `submit_time` int NOT NULL DEFAULT 0;     -- for time-penalty basis
  -- status extended: 5 = queued, 6 = revealed-pending  (existing: 0 active,1 cancelled,...)

-- commitments (commit-reveal phase 1)
CREATE TABLE `bet_commitments` (
  `id` bigint NOT NULL AUTO_INCREMENT,
  `market` bigint NOT NULL,
  `account` bigint NOT NULL,
  `commitment` char(64) NOT NULL,        -- hex hash
  `escrow_amount` bigint NOT NULL,
  `no_reveal_fee_permille` smallint NOT NULL,  -- rate snapshotted at commit (consensus-checked in tx)
  `commit_time` int NOT NULL,
  `reveal_deadline` int NOT NULL,
  `status` tinyint NOT NULL DEFAULT 0,   -- 0 committed, 1 revealed, 2 forfeited
  `revealed_bet_id` bigint DEFAULT NULL,
  PRIMARY KEY (`id`),
  KEY `market_status` (`market`,`status`),
  KEY `reveal_deadline` (`reveal_deadline`)
);

-- markets: forfeit accumulator + opt-in flags + tier
ALTER TABLE `markets`
  ADD COLUMN `forfeit_pool` bigint NOT NULL DEFAULT 0,
  ADD COLUMN `allow_batch` tinyint NOT NULL DEFAULT 1,
  ADD COLUMN `allow_instant_bet` tinyint NOT NULL DEFAULT 1,
  ADD COLUMN `endogeneity_tier` tinyint NOT NULL DEFAULT 2,
  ADD COLUMN `current_epoch` int NOT NULL DEFAULT 0;
```

## 10. Audit trail

`market_log` gains actions: `commit`, `reveal`, `batch_settle` (with `A_in`, `B_in`, before/after reserves, dust), `commit_forfeit` (penalty, refund). Keeps the full before/after-reserves discipline of spec §3.

## 11. Attack & corner cases

| Case | Handling |
|------|----------|
| Commit on both sides, reveal only favorable | Reveals don't apply until epoch close (nothing to react to); non-reveal penalty makes optionality costly. |
| Spam empty commits | `min_batch_bet` escrow + non-reveal penalty. |
| Settle an empty queue | No-op; no virtual op emitted. |
| Front-runner uses instant bets to move curve before a batch settles | The residual vector — see §11.1 for the full threat model and the layered defenses. |
| Reveal after deadline | Rejected; commit forfeits. |
| min_tokens fails for some batch bettors | Single recompute pre-pass over survivors (§6). |

### 11.1 What commit-reveal actually protects (threat model)

The hash hides a bet **only during commit → reveal**. After reveal, side+amount are public and the bet **does** enter the batch. So the protection is layered, not "the batch hides it":

1. **No mempool sandwich of your specific order** — while it is a hash, no one can read it or execute immediately ahead of it. (Primary protection.)
2. **Uniform-price batch** — no intra-batch ordering edge; a front-runner can't get a strictly better price than you inside the same batch (§6).
3. **Batch-composition uncertainty** — other hidden commits (possibly on the opposite side) may be in the same batch, so an instant front-run is a *bet under uncertainty*, not a sure sandwich.

**Residual vector (real):** between reveal and settlement the bet is public, and an **instant** bet executes against the live curve immediately → it can move the price the batch settles at. A profitable sandwich requires ALL of: (a) a long reveal→settlement gap, (b) `allow_cancellation=1` (attacker can exit), (c) a loose victim `min_tokens`.

**Defenses, by increasing strength:**

- **Shrink the gap.** Settle at the first boundary at/after reveal; clients reveal a few blocks before the boundary (§5.3) → gap ≈ 1–2 blocks. `reveal_window_blocks` is only a liveness fallback.
- **`min_tokens` (in the hash).** A pump pushes the victim's fill below the floor → the bet is auto-rejected/refunded, so the attacker's pump backfires (left long with no victim flow).
- **Non-cancellable markets.** `allow_cancellation=0` → attacker cannot exit → front-run becomes pure directional risk, not a riskless sandwich.
- **STRUCTURAL — epoch-open snapshot (specified in §6.1):** price the batch against the reserves frozen at the epoch's open, so same-epoch instant bets cannot move its clearing price. Fully manipulation-immune & solvency-safe for **Multi**; for **Binary** use live-curve settlement + a snapshot price band (token=VIZ claim makes pure-snapshot pricing unsafe). Removes the instant-front-run vector without cryptography.
- **FULL elimination (mode 3, roadmap):** **threshold/timelock-encrypted sealed bids** (Shutter/Penumbra-style) — the preimage is decrypted only at the boundary, so there is no public actionable window at all.

> // BOTTOM LINE: commit-reveal+batch turns front-running from a *trivial mempool sandwich* into "guess the
>   hidden batch, race a ~1-block window, bear directional risk, and risk the victim's min_tokens canceling
>   the bet." A true zero requires an encrypted mempool (mode 3).

## 12. Open questions (// TODO)

- [ ] Block-level ordering of `liquidity_*`, instant `pm_place_bet`, and `pm_batch_settle` within one block.
- [ ] Slippage rejection: single pre-pass vs. iterate-to-fixpoint.
- [ ] Cancellation policy for COMMITTED (not yet revealed) bets.
- [ ] Optional cross-side internal matching at clearing price (advanced pricing) + invariant re-proof.
- [x] Epoch-open snapshot reconciliation — specified in §6.1 (Multi: snapshot pricing; Binary: live + price band). Remaining: formal invariant proof for a pure-snapshot *binary* variant, only if full crypto-free binary immunity is wanted.
- [ ] Mode 3: threshold/timelock-encrypted sealed bids — whether/when to add (fully closes the residual).
- [ ] Whether `endogeneity_tier` is purely advisory or can *force* commit-reveal at protocol level.

## 13. Phased rollout

1. **Phase 1 — Batch (mode 1).** Epochs + `pm_batch_settle` + uniform-price aggregation. Biggest front-running win, no crypto.
2. **Phase 2 — Commit-Reveal (mode 2).** `pm_commit_bet`/`pm_reveal_bet` + `pm_commit_forfeit` + `forfeit_pool`. Adds confidentiality (endogeneity mitigation).
3. **Phase 3 — Tiering & client policy.** `endogeneity_tier` defaults, optional advanced cross-side matching.

## 14. Sections to add/modify in `onix-protocol-specification.md`

| Spec section | Change |
|--------------|--------|
| §3 System Parameters | Add the 5 global + 2 per-market params (this doc §3). |
| §4 Market State Machine | Add bet statuses 5 (queued) / 6 (revealed-pending); add `pm_batch_settle` & `pm_commit_forfeit` virtual ops to transitions. |
| §5 Onix Binary | Add §5.x "Batch settlement" with the uniform-price aggregation math (§6). |
| §6 Onix Multi | Add the LMSR batch analogue. |
| §7 Fee Structure | Add `forfeit_pool` to the `winners_pool` formula. |
| §8 Time Penalty | Clarify basis = submit/commit time for deferred modes. |
| §10 Resolution & Payout | Include `forfeit_pool` in winners distribution + edge cases. |
| §11 Bet Cancellation | Add cancellation rules for queued/committed bets. |
| New §18 "Execution Modes" | Full description of instant/batch/commit-reveal + ops §5. |
| §17 Database Schema | Add the tables/columns from this doc §9. |
