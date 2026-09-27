# Pending PM audit fixes (not activated)

This patch reserves `CHAIN_PM_AUDIT_FIX_HARDFORK` (15), but does **not** schedule it. `CHAIN_NUM_HARDFORKS` stays at 14; there is no HF15 time, version, vote, migration or deployment in this patch. Ordinary replay therefore keeps HF14 behavior. The consensus simulation opts in solely by adding a processed marker in its disposable database; this is not a production activation path.

The independent `max_leverage_loan` inclusive-cap correction affects API previews,
not transaction evaluation, and is not hardfork-gated.

The sequential hardfork ID is not the software release number: HF14 currently
maps to protocol version `4.0.0`. A future HF15 could use `4.1.0`, subject to a
separate network decision. `hardfork_version` discards the release/revision
component, so `4.0.1` is not distinguishable from `4.0.0` for hardfork voting.
No version is assigned by this patch.

After a separately approved network upgrade schedules this gate:

- A partial LMSR LP withdrawal decrements the position's `b_share` by exactly the same rounded `b_remove` as `market.lmsr_b`. Closing the remainder does not subtract that share twice. Before the gate, the historical stale-share behavior is unchanged.
- Direct `pm_place_bet` with `mode=1` is rejected rather than silently filled immediately. The existing `pm_commit_bet` → `pm_reveal_bet` flow escrows, queues a status-5 bet and executes it at the batch boundary. `mode=0` retains the instant-bet gate. Historical direct mode-1 transactions remain replayable before HF15.

**Activation prerequisite:** select a coordinated HF15 time/version and review/migrate *already stale* LMSR LP shares and markets before enabling the fix. Historical partial withdrawals may have left `sum(active LP b_share) != market.lmsr_b`; merely gating future operations cannot reconstruct past share attribution. Do not schedule or deploy this stub until a deterministic migration or explicit policy for those states is specified, tested, and reviewed. No claim of profitable MEV exploitation follows from the observed immediate fill alone.
