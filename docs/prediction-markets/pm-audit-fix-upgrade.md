# HF15 — PM audit fixes: activation checklist and stale-state migration

Status: the two fixes below are **implemented but not scheduled on mainnet**. The fork is registered
in both configs (`CHAIN_NUM_HARDFORKS = 15` everywhere) and gated by its activation time plus the
validator quorum, so a production binary applies it once that time is reached — nothing runs before
then, and no migration is needed either way. §2 lists the two compiled timestamps. This note is the
operator-facing checklist: what is gated, how the fork is registered and activated, what to verify,
and what to decide about state the legacy path already left behind.

## 1. What the fork changes

Both gates are read as `has_hardfork(CHAIN_PM_AUDIT_FIX_HARDFORK)` in `pm_evaluator.cpp`; before the
fork the historical behaviour is kept byte-for-byte, so ordinary replay is unaffected.

**A. Partial LMSR liquidity withdrawal (`pm_withdraw_liquidity`, `market_type == 1`).** The curve
keeps a per-market `lmsr_b`, and each LP row keeps its own `b_share` — the row's slice of it. A
*partial* withdrawal used to subtract a floored `b_remove` from `market.lmsr_b` while leaving the
row's `b_share` untouched, so the row went on claiming more curve than the market still held. The
next withdrawal from that row (typically the full exit) then took the whole stale share and could
drive `lmsr_b` to `<= 0`. Post-fix both records shrink by the same `b_remove`, and a withdrawal
whose `b_remove` would exceed `market.lmsr_b` is **refused** (`FC_ASSERT`) instead of being clamped.

Why refuse rather than clamp: `lmsr_b <= 0` is not a flat curve. `lmsr_q96` fails soft — `lmsr_price`,
`lmsr_buy_cost` and `lmsr_tokens_for_amount` all return `0` for `b <= 0` without ever reaching
`validate_domain` — so every outcome prices at zero and **a bet costs nothing** while the market still
holds the remaining LP capital and the bettors' stakes. Clamping to zero would hand that state to the
next caller; refusing keeps the pricing curve alive, and the refused LP keeps the principal (see §4,
it is returned in full at settlement).

**B. Direct `mode = 1` bets (`pm_place_bet`).** A market with `allow_instant_bet = false` and
`allow_batch = true` exists to force the front-run-resistant flow, and the supported way in is
`pm_commit_bet` → `pm_reveal_bet` (escrow, a status-5 row, execution at the batch boundary). A direct
`pm_place_bet` with `mode = 1` reached the *instant* fill path instead, i.e. it bypassed the very
protection the market opted into, paying the instant gate's price. Post-fix it is rejected. History is
untouched: `mode = 1` transactions in already-produced blocks still replay under the pre-fork rules.
This closes the bypass; it does not by itself establish that the immediate fill was profitable to
whoever used it.

Neither fix changes an object layout and neither needs a schema bump: `apply_hardfork` has no `case`
for HF15 (the `default: break` path is correct here), snapshot import needs no new section, and there
is nothing to recompute on activation.

## 2. How the fork is registered and activated

* **Registration is unconditional, in both configs.** `0-preamble.hf` sets `CHAIN_NUM_HARDFORKS` to
  15 for every build, `hardfork.d/15.hf` always defines `CHAIN_HARDFORK_15` /
  `CHAIN_PM_AUDIT_FIX_HARDFORK` and `CHAIN_HARDFORK_15_VERSION`, and `database_hardfork.cpp`
  registers `_hardfork_times[15]` / `_hardfork_versions[15]` unconditionally. The arrays are sized
  `[CHAIN_NUM_HARDFORKS + 1]`, so the indices stay in bounds in both builds. What gates the fork is
  the activation timestamp plus the validator quorum — not the build flavour.
* **Why production carries it too.** The public testnet (`testnet.viz.world`, the node our parsers,
  oracles and clients talk to) is a **production-config** deployment: it reports
  `CHAIN_NAME "VIZ"`, `CHAIN_ID = sha256("VIZ")` and `CHAIN_HARDFORK_REQUIRED_VALIDATORS = 17`.
  `config_testnet.hpp` would change `CHAIN_NAME` to `VIZTEST` and therefore the chain id, and drop
  the quorum to 1 — deploying that image to the existing testnet is not an option, the chain would
  stop being the chain its snapshot and clients belong to. A fork registered only under
  `BUILD_TESTNET` can therefore **never** activate on the testnet we actually use: `has_hardfork(15)`
  would be permanently false there and the deployment would prove nothing. Registering in both
  configs, with the time as the only difference, makes testnet-first verification possible on the
  very artifact that production ships.
* **The activation time is the one knob, and it is compiled in.** `15.hf` carries two timestamps:
  `CHAIN_HARDFORK_15_TIME` = 2026-09-27 08:33:20 UTC for `BUILD_TESTNET` (a fresh testnet-config
  chain may activate immediately) and = **2026-10-05 00:00:00 UTC** for production. The production
  value is **provisional** — it is not a scheduled mainnet date, it exists so the production-config
  testnet can reach activation and be observed; see the checklist below. Changing it is a one-line
  build-and-ship cycle, so it must be re-confirmed when the HF14+HF15 release is actually planned.
  Keep it in the **future**: with a past timestamp the fork applies in whatever block the 17th
  validator happens to upgrade in, with no announceable moment (the same warning `14.hf` carries).
* **Version, not revision.** `version(m, h, r)` packs the hardfork version into the middle component
  and `CHAIN_HARDFORK_VERSION` is that component. A new fork must therefore move `CHAIN_VERSION`
  itself — both `config.hpp` and `config_testnet.hpp` are at `4.1.0` now — because
  `hardfork_version` discards the revision, so `4.0.1` would be indistinguishable from `4.0.0` for
  voting. `database_hardfork.cpp` asserts `CHAIN_HARDFORK_VERSION == _hardfork_versions[CHAIN_NUM_HARDFORKS]`,
  which is exactly why the version and `CHAIN_NUM_HARDFORKS` have to move together in both builds.
* **Voting is automatic.** `database.cpp::_generate_block` injects `hardfork_version_vote` whenever
  the validator's recorded vote differs from the binary's next fork, and `process_hardforks()` applies
  the fork once `CHAIN_HARDFORK_REQUIRED_VALIDATORS` agree (17 on the production config the testnet
  runs, **1** on a testnet-config build) **and** the activation timestamp is reached. The testnet's
  21 validator slots are all driven by one account, so quorum there is immediate and the timestamp
  alone decides the block.
* **Mainnet activation checklist.** (1) Confirm or replace the provisional date and announce it well
  ahead of the timestamp — that is the only remaining scheduling decision. (2) Run the stale-state
  detector from §4 and settle the migration question there. (3) Ship the image and let validators
  update; the vote is automatic. (4) Confirm the activation from the node log and re-run the checks
  in §3 against the live chain. Note that HF14's own mainnet date (2026-08-28) is already in the
  past, so a first mainnet deployment carrying both forks activates them together in one block
  (`process_hardforks` walks while `_hardfork_versions[last] < next_hardfork`); the testnet, whose
  snapshot carries HF14 already processed, is the only place HF15 can be exercised on its own — but only
  if that chain is not in emergency consensus (see §3: an emergency committee neither votes nor is
  counted, so the fork never even becomes pending there).
* **Rollback.** Before the activation timestamp, redeploying the previous image is safe: the fork
  simply stays pending (the state keeps a voted-but-unapplied fork; rolling the new image back in
  clears it). After activation the marker is chain state, so do not roll back — a pre-HF15 binary does
  not know the fork and would evaluate the gated operations under the old rules while the chain says
  otherwise. Roll forward instead.

## 3. Verification

Pre-activation (after deploying the new image and before the timestamp), read the state with the two
RPC methods this build actually exposes — `database_api.get_hardfork_property` is **not** registered on
VIZ, so a call for it fails with `Could not find method`:

* `database_api.get_hardfork_version` — the **applied** fork version (`4.0.0` while HF14 is current);
* `database_api.get_next_scheduled_hardfork` — `hf_version` / `live_time` of the fork the validator
  tally has scheduled. Once the new image's validators vote, this is the HF15 version and the compiled
  activation time; the detector in §4 (`scripts/pm_stale_bshare_detect.py`) reports on the state that is
  about to be gated.

**A production-config testnet in emergency consensus cannot get there at all, and this is the trap to
know about.** While `dynamic_global_property_object.emergency_consensus_active` is true the validator
schedule is filled with `CHAIN_EMERGENCY_VALIDATOR_ACCOUNT` (= `committee`, `database.cpp:575`), and
that account is excluded from the fork vote in **both** directions: `database.cpp:2811` skips the vote
injection for the producing validator, and the tally loop (`database.cpp:3413`) skips its slots so a
single entity holding many slots cannot inflate its own weight. The observable consequence, measured on
the testnet 2026-09-27 after deploying 4.1.0 over a chain sitting at 4.0.0: every block the node
produces carries an **empty `extensions`** array (no `hardfork_version_vote`), and
`get_next_scheduled_hardfork` keeps returning `4.0.0` with the *previous* fork's time. The tally is
empty, `process_hardforks` pins `next_hardfork` to `current_hardfork_version`, and **no** hardfork can
be scheduled — the compiled activation time is a dead knob. Exiting emergency consensus needs
`CHAIN_HARDFORK_REQUIRED_VALIDATORS` real validators to update (HF12's exit rule), which a testnet whose
validators are imported mainnet history will not reach.

So on such a chain "deploy first, watch the fork activate" verifies the deployment (image runs, snapshot
imports, invariants hold) but **not** the activation — the tally there can never produce a result. Two
ways out: a fresh `BUILD_TESTNET` build (quorum 1, an ordinary validator votes and applies the fork on its
own), or `testnet_plugin` below, which forces the fork on the production-config chain itself.

### Forcing the fork at startup: `testnet_plugin`

`testnet_plugin` exists for exactly this case and needs no consensus change. It adds one startup command:

    --testnet-hardfork <version|number>        # e.g. 4.1.0, or 15

With the plugin loaded (`plugin = testnet_plugin`, already in `config_testnet.ini`) **and** given a target,
it applies every hardfork up to the requested one as soon as the chain state is loaded — after the snapshot
import, before block production starts — through `database::set_hardfork(n, true)`. The validator tally is
bypassed entirely, so it also works while `emergency_consensus_active` is true. That is what makes
"verify the activation on the production-config testnet, ahead of the production date" possible.

Safety: the plugin does nothing unless it is both loaded and given a target, so the production image can
carry it; the production `config.ini` never enables it. A forced fork cannot be rolled back — point it at a
chain you own, never at mainnet.

Operator sequence (shelter testnet): deploy the image that **contains** the plugin first — a config line
naming a plugin the binary does not register aborts startup with `unable to find plugin: testnet_plugin` —
then restart the container with `VIZD_EXTRA_OPTS="--testnet-hardfork 15"`, or put `testnet-hardfork = 15`
in the config. The node logs

    *** testnet_plugin: FORCING HARDFORK 15 (requested '15') at head=#... ***
    *** testnet_plugin: hardfork 15 applied at head=#...: last_hardfork=15, current_hardfork_version=4.1.0 ***

and `get_hardfork_version` reports `4.1.0` from then on. Drop the option after the run: the fork is chain
state now, and forcing it again on a restart from an older snapshot is harmless but noisy.

One trap when checking the deployment: `--testnet-hardfork` is declared as a *config-file* option, which
appbase also accepts on the command line (it parses argv against `cli + cfg` merged) — but `--help` prints
the command-line options only, so the flag is **not** listed there. Verify the flag by starting the node and
reading the `FORCING HARDFORK` banner, not by grepping `--help`. An option declared in *both* descriptions
is worse than invisible: boost then refuses every start with
`option '--testnet-hardfork' is ambiguous and matches different versions of '--testnet-hardfork'`.

Post-activation:

* the fork appears in `processed_hardforks` and the node log shows the activation;
* a direct `pm_place_bet(mode = 1)` on an `allow_instant_bet = false` market is rejected
  (`mode=1` no longer reaches an instant fill), while `pm_commit_bet` → `pm_reveal_bet` still works;
* an LMSR partial withdrawal leaves `Σ b_share` over the market's active rows equal to
  `market.lmsr_b`;
* a full exit of a row that the legacy path left stale is refused with the “would drain the LMSR
  pricing curve” assert, and the market keeps pricing;
* the usual post-redeploy invariants: SHARES delta 0, TOKEN delta equal to the chain's legacy anchor,
  `restarts 0`, listings non-empty.

## 4. Stale state: what to detect and what to decide

Post-fix invariant, per LMSR market: **`Σ b_share` over its active LP rows equals `market.lmsr_b`.**
The legacy divergence is one-directional — the curve lost `b_remove` while the row kept its full
`b_share`, so rows end up claiming *more* than the market holds. Detection is therefore a single walk
over `market_type == 1` markets, summing the active rows' `b_share` and comparing against `lmsr_b`;
any market where the sum is larger has at least one stale row, and its next full exit is exactly the
operation the new gate refuses.

Only markets whose LP performed a partial withdrawal before activation can be in this state; anything
created after the fork cannot, and a market where every LP exit was all-or-nothing is consistent by
construction.

### Detector

`scripts/pm_stale_bshare_detect.py <snapshot-block-NNNN.vizjson>` walks the snapshot and prints, per
market, `Σ b_share` over the active (`status 0`) rows against `lmsr_b`. It is read-only, needs no
node and no chain access, and is verdict-first: **exit 0** = every LMSR market satisfies the invariant
(nothing to migrate), **1** = at least one market diverges (the list is printed), **2** = the snapshot
could not be read or the sections were not found. A `.vizjson` snapshot is zlib-compressed JSON, so the
file has to be the node's own snapshot, not a re-serialized export.

Snapshot location: with `--snapshot-auto-latest` the node writes `snapshot-block-*.vizjson` into its
vizhome (`/var/lib/vizd/snapshots/` inside the container, i.e. `<vizhome>/snapshots/` on the host;
every 15 minutes on the current testnet). On the shelter box that is
`/root/testnethome/snapshots/`, readable only via `sudo` — copy it out first:
`sudo cp <snap> /tmp/snap.vizjson && sudo chown $USER /tmp/snap.vizjson`.

Measured 2026-09-27 on testnet snapshot block **83748900** (the state HF15 is about to gate):
129 806 markets, 9 615 of them LMSR; 8 251 have active LP rows and **all 8 251 satisfy the invariant
exactly** (`Σ b_share == lmsr_b`), 0 diverging. So on this chain the legacy partial-withdraw path left
no residue: the "refuse the stale exit" behaviour has nothing to refuse, and the one-shot attribution
repair from the options below is not needed. Re-run the detector against a fresh snapshot right before
scheduling the fork — a chain that has served more partial LMSR withdrawals since can differ.

Options, and what the chain does about each:

* **Do nothing (current behaviour).** The stale row is refused on exit, so the LP cannot pull its
  remaining `b_share` out early. Nothing is lost: the principal is returned in full at settlement,
  where the live-market liquidity floor no longer applies, and the market keeps pricing in the
  meantime. Cost: the LP's early-exit option is dead for that row, and the inconsistency is visible
  only through the refusal. This is the conservative, no-consensus-change option, and it is the
  default because the divergence can be measured but not *reconstructed* — the chain holds current
  state only, and there is no per-withdrawal history to rebuild the true attribution from.
* **Repair the attribution once.** For each diverging market, shrink the active rows' `b_share`
  pro-rata down to `market.lmsr_b` (floored, remainder to the last row) so the rows agree with the
  curve again and full exits work. This is deterministic and idempotent given the same state, but it
  is a consensus-visible state mutation and needs its own fork-gated migration, its own tests and its
  own review — i.e. it is a separate change, not a rider on this one.
* **Per-market operator action** (nudge the affected LPs, or resolve/settle the market) is not a fix:
  settlement restores the principal anyway, so the only thing at stake is the early-exit window.

The decision the network has to make before scheduling HF15 is therefore: is "refuse the stale exit,
return the principal at settlement" acceptable, or does the network want the one-shot attribution
repair as well? The detector output should drive it — if no LMSR market diverges, the question is
moot and the fork can be scheduled as-is.
