# HF16 — no-contest fee freeze: activation checklist and backfill semantics

Status: **implemented; activation scheduled for 2026-10-31 00:00:00 UTC.** The fork is registered in both configs
(`CHAIN_NUM_HARDFORKS = 16`, `CHAIN_VERSION = 4.2.0`) and gated by its activation time plus the
validator quorum: a production binary applies it once that time is reached, and nothing runs before
then. This note is the operator-facing checklist — what is frozen, how the two states on a market are
distinguished, what changes in the object layout and the snapshot, how to verify the activation, and
why no seed pass is needed for rows that are already inside the dispute window when the fork lands.

## 1. What the fork changes

**The problem.** `pm_no_contest` only declares the void and opens the `pm_dispute_grace_sec` window;
the penalty is computed at **settlement**. Before HF16 the amount was
`floor(pm_dispute_fee × pm_no_contest_penalty_percent / 10000)` read from the median
`chain_properties_pm` **at settlement time**. Both inputs are median-voted, so a parameter vote
inside the dispute window changed the bill after the oracle had already announced the void: the
oracle announced one number and paid another, with no way to know the final price at announcement.

**The fix: freeze the number at announcement.** `pm_no_contest_evaluator` computes the same formula
from the median of the **announcing block** and stores it on the market as
`pm_market_object::no_contest_fee`. `settle_market(win < 0)` charges exactly that amount instead of
re-reading the median. The insurance cap is unchanged and still applies per settlement:
`penalty = min(frozen_fee, oracle.insurance)`.

**Two states, one flag.** `pm_market_object::no_contest_fee_frozen` says whether the amount was
frozen at all:

* `true` — the void was announced **under HF16**; settlement charges `no_contest_fee` verbatim.
* `false` — the row was announced **before HF16** (or imported from a pre-HF16 snapshot); settlement
  keeps the legacy median computation, exactly the pre-fork behaviour.

The flag is separate from the amount on purpose. A frozen fee of **0** (the penalty percent was voted
to 0 at announcement) must stay 0 even if a later vote raises the percent — "frozen zero" and "never
frozen" are different states, and inferring legacy-ness from `fee == 0` would silently re-price the
former. A market announced under HF16 with no bets also records `frozen = true, fee = 0`; that is
inert, because settlement only charges when `settlement.stake_total > 0`.

**Attribution counter.** `pm_oracle_object::no_contest_slashed_total` is a monotone total of
no-contest penalties charged to the oracle, incremented in the **same** `modify()` that grows
`total_insurance_slashed`. It gives the no-contest class exact attribution without a virtual
operation, and it accumulates on both paths (a legacy row that settles after activation is still a
no-contest slash). It is forward-accumulating from HF16: penalties charged before the fork are **not**
backfilled — the aggregate delta is the only surviving record, and subtracting it later would be a
guess. Display/attribution only; consensus never reads it.

**Object layout.** Both fields are **appended at the end** of their structs and to the corresponding
`FC_REFLECT`:

* `pm_market_object`: `share_type no_contest_fee; bool no_contest_fee_frozen = false;`
* `pm_oracle_object`: `share_type no_contest_slashed_total;`

Snapshot import is `contains()`-guarded for all three, so a pre-HF16 snapshot imports with defaults
(`0` / `false`) — which is precisely the "legacy row" state the settlement expects. Snapshot export is
generic (`EXPORT_INDEX`) and needs no change. `apply_hardfork` has no case for HF16 (`default: break`
is correct): there is no singleton to create and nothing to recompute on activation.

**What ships without the fork.** These read-only additions are useful before activation and are not
gated — they derive from the same state the settlement will read:

* `get_oracle` (and the oracle block in `get_market_full`) gains `no_contest_pending_count`,
  `no_contest_pending_stake`, `no_contest_fee_nominal`, `no_contest_fee_capped` — the price of the
  voids this oracle has announced and that are still inside the dispute window (status 3, payout
  pending, no dispute filed) and carry stake. Nominal and insurance-capped figures are both reported,
  because a cap hit would otherwise hide the nominal cost. Computed on read, per-block cached.
* The market card gains `settlement_charge`: the amount that would be slashed if this market settled
  as a void **now** (0 for anything that is not a pending no-contest with stake).
* `get_pm_chain_properties` keeps every existing field and adds
  `pm_no_contest_fee_per_market = dispute_fee × penalty_percent / 10000` — the nominal unit clients
  and scripts otherwise recompute by hand. The RPC now returns a variant; JSON clients see the same
  object plus one key.

## 2. How the fork is registered and activated

* **Registration is unconditional in both configs.** `0-preamble.hf` sets `CHAIN_NUM_HARDFORKS` to 16;
  `hardfork.d/16.hf` defines `CHAIN_HARDFORK_16` / `CHAIN_PM_NO_CONTEST_FREEZE_HARDFORK` and
  `CHAIN_HARDFORK_16_VERSION`; `database_hardfork.cpp` registers `_hardfork_times[16]` /
  `_hardfork_versions[16]` for every build. The arrays are sized `[CHAIN_NUM_HARDFORKS + 1]`. What gates
  the fork is the timestamp plus quorum, not the build flavour — the same rule as HF15, and for the
  same reason: the shelter testnet is a **production-config** deployment, so a testnet-only fork could
  never activate there. See `pm-audit-fix-upgrade.md` §2 for the full argument.
* **Activation: 2026-10-31 00:00:00 UTC** (`CHAIN_HARDFORK_16_TIME` = 1793404800 in both configs, set
  by the owner on 2026-10-20). The validator announcement must name this moment, and 17 schedule
  slots must vote 4.2.0 before it (only validators with a signing key are scheduled; check with
  `scripts/hardfork_readiness.py`, see [HF16](../consensus/hf16.md#verify-readiness)). Keep the value in the **future**:
  a past timestamp activates the fork in whatever block quorum is reached, with no announceable
  moment. Change it together with the announcement text, never after it is published.
* **Version, not revision.** `CHAIN_VERSION` moves to `4.2.0` in **both** `config.hpp` and
  `config_testnet.hpp`; the middle component is the hardfork version the validators vote, and
  `database_hardfork.cpp` asserts it equals `_hardfork_versions[CHAIN_NUM_HARDFORKS]`. (The two configs
  previously carried 4.1.2 and 4.1.0 — both move to the same 4.2.0.)
* **Activation checklist.** (1) Confirm or replace the provisional date and announce it. (2) Measure
  the population in §4 on a fresh snapshot and settle the question there (expected answer: no action).
  (3) Ship the image; validator votes are automatic. (4) Confirm activation from the node log and run
  the checks in §3. (5) Note the emergency-consensus trap from `pm-audit-fix-upgrade.md` §3: on a chain
  in emergency consensus no fork can be scheduled at all, and the compiled time is a dead knob.
  `testnet_plugin --testnet-hardfork 16` forces the fork at startup on a production-config testnet for
  verification; the plugin does nothing unless both loaded and given a target.
* **Rollback.** Before the timestamp, redeploying the previous image is safe — the fork stays pending.
  After activation the marker is chain state: do not roll back (a pre-HF16 binary would settle pending
  no-contests from the median while the chain says otherwise). Roll forward.

## 3. Verification

**Before activation** (the read-only additions work on the running chain):

* `get_pm_chain_properties` contains `pm_no_contest_fee_per_market`, numerically equal to
  `pm_dispute_fee × pm_no_contest_penalty_percent / 10000`.
* For an oracle with a void in its dispute window and stake on it, `get_oracle` reports
  `no_contest_pending_count ≥ 1` and `no_contest_fee_nominal = count × unit`; the matching market
  card reports the per-market `settlement_charge`.
* No market carries `no_contest_fee_frozen = true` yet — the flag is written only under HF16.

**After activation:**

* a market voided by `pm_no_contest` immediately shows `no_contest_fee_frozen = true` and
  `no_contest_fee` equal to the announcing block's unit;
* vote `pm_dispute_fee` / `pm_no_contest_penalty_percent` while that market sits in its dispute window:
  the eventual slash equals the frozen amount, not the new median;
* a market announced **before** activation with `frozen = false` settles from the then-current median,
  i.e. pre-fork behaviour is preserved for it;
* `oracle.no_contest_slashed_total` grows by exactly the charged amount, in step with
  `total_insurance_slashed`;
* the usual post-redeploy invariants: SHARES delta 0, TOKEN delta equal to the chain's legacy anchor,
  `restarts 0`, listings non-empty.

**Consensus regression.** `tests/consensus_sim/scenarios/test_pm_no_contest_fee.cpp` (BUILD_TESTNET)
runs one chain with three markets announced around the fork point — pre-fork legacy, post-fork frozen,
post-fork frozen at zero — moves the median afterwards, and checks that the insurance drop and
`no_contest_slashed_total` equal `8u + 1u + 0` for the legacy/frozen/zero legs respectively.
Measured 2026-10-19 (BUILD_TESTNET, `consensus_sim_tests --run_test=pm_no_contest_fee_three_states`):
insurance `5 000 000 → 4 100 000` raw, i.e. exactly `9u`; the test passed with no errors.

## 4. Rows already in the dispute window at activation

**No seed pass, and a seed pass would be wrong.** A row that was announced before the fork has no
frozen amount; it must keep the pre-fork rule (median at settlement), because that is the rule under
which it was announced. Writing a frozen amount into it at activation would change the oracle's bill
retroactively — the very defect HF16 removes. The `false` flag is the marker that makes this
automatic: no migration code, no snapshot rewriting, and an old snapshot restores into exactly the
state the settlement expects.

The residual exposure is therefore bounded and explicit: rows announced pre-fork **and** still inside
the dispute window at activation can still be re-priced by a vote that lands between activation and
their settlement. Their number is measurable from the chain; if it is non-zero and the network wants
no retroactive repricing at all, the only clean response is to wait out those windows before any
parameter vote — not a migration.

**Measurement before scheduling.** Walk the oracles' pending no-contest rows (status 3, payout
pending, `resolved_outcome = -1`) and count those with `bets_sum > 0`, plus the stake they carry;
`get_oracle` reports exactly this per oracle (`no_contest_pending_count` / `_stake`), so no new
instrument is needed. Measured on the live production-config testnet on **2026-10-19**: 112 markets in
the dispute window, **0** of them carrying stake — the population that could be affected by a vote
inside the window was empty at that moment. Re-measure on a fresh snapshot right before scheduling;
the number moves with the oracle population and says nothing about the future.
