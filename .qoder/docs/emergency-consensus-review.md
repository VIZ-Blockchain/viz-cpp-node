# Emergency Consensus Recovery — Implementation Review

## Status: Implemented (Hardfork 12, version 3.1.0) — Bugs Found and Fixed (B1–B12)

Research source: [consensus-emergency-recovery.md](../research/consensus-emergency-recovery.md)

---

## System Overview

Hardfork 12 adds an on-chain **Emergency Consensus Mode** that activates automatically when the VIZ network stalls for >1 hour (no LIB advancement). The activation check is fully deterministic — it uses only block timestamps from the chain state (`b.timestamp - lib_block.timestamp`), ensuring identical results on every node and during every replay. A well-known committee key (`VIZ75CRHVHPwYiUESy1bgN3KhVFbZCQQRA9jT6TnpzKAmpxMPD6Xv`) becomes the block producer, keeping the chain alive until real witnesses return.

On activation, **all real witnesses are disabled** (`signing_key` set to null, penalties reset to zero, `current_run` reset). Only the committee produces blocks initially. Operators must manually re-enable witnesses via `witness_update_operation` transactions. This ensures a clean start where only intentionally re-registered witnesses participate.

The committee witness is a **neutral voter**: it copies the current median chain properties and votes for the currently applied hardfork version (not a future one). This ensures that committee slots in the schedule don't skew governance parameters or push unvoted hardforks.

### Key Files Modified

| File | Role |
|---|---|
| `libraries/chain/hardfork.d/12.hf` | Hardfork 12 definition |
| `libraries/protocol/include/graphene/protocol/config.hpp` | Emergency constants, version 3.1.0 |
| `libraries/chain/include/graphene/chain/database.hpp` | DGP fields, skip flags |
| `libraries/chain/database.cpp` | Activation, hybrid schedule, LIB advancement (capped), startup recovery, vote-weighted fork comparison |
| `libraries/chain/include/graphene/chain/fork_database.hpp` | Emergency mode flag, size increase 1024→2400 |
| `libraries/chain/fork_database.cpp` | Hash tie-breaking, `set_emergency_mode()` |
| `plugins/witness/witness.cpp` | Three-state safety, emergency key config, fork collision |
| `libraries/network/include/graphene/network/peer_connection.hpp` | `fork_rejected_until` soft-ban field |
| `libraries/network/node.cpp` | P2P anti-spam (soft-ban vs disconnect) |
| `plugins/snapshot/plugin.cpp` | Forward-compatible DGP import |
| `share/vizd/config/config_witness.ini` | `emergency-private-key` option |

---

## Architecture Diagram

```
                    ┌──────────────────────────────┐
                    │   update_global_dynamic_data  │
                    │   (every block)               │
                    │                               │
                    │  HF12 active?                 │
                    │  emergency_active == false?    │
                    │       ├── NO ─────────────────│── Skip check
                    │       └── YES                  │
                    │  lib_block available?           │
                    │       ├── NO ─────────────────│── Skip check (snapshot restore)
                    │       └── YES                  │
                    │           seconds_since_lib    │
                    │           = b.timestamp -      │
                    │             lib_block.timestamp │
                    │           ≥ 3600?              │
                    │           ├── YES ─────────────│── Activate Emergency
                    │           │                   │   • emergency_consensus_active = true
                    │           │                   │   • Create/update committee witness
                    │           │                   │   • Disable ALL real witnesses
                    │           │                   │     (signing_key = null, penalties = 0)
                    │           │                   │   • Override schedule → committee
                    │           │                   │   • next_shuffle_block_num = now+N
                    │           │                   │   • fork_db.set_emergency_mode(true)
                    │           └── NO              │
                    └──────────────────────────────┘
                                    │
                                    ▼
                    ┌──────────────────────────────┐
                    │   update_witness_schedule     │
                    │   (every round)               │
                    │                               │
                    │  1. Normal schedule build      │
                    │     (may zero all slots if no  │
                    │      witnesses have valid keys) │
                    │  2. Hybrid schedule override:  │
                    │     • Real witnesses keep slots │
                    │     • Committee fills gaps      │
                    │     • Expand to full 21 slots   │
                    │     • Sync committee props/vote │
                    │  3. update_median_witness_props │
                    │                               │
                    │  Skip committee in:           │
                    │   • Hardfork vote tally       │
                    │   • Median props computation  │
                    │                               │
                    │  Exit check:                  │
                    │   real_witness_slots >= 75%    │
                    │   of CHAIN_MAX_WITNESSES?      │
                    │       ├── YES ─────────────►│── Deactivate Emergency
                    │       │                       │   • emergency_consensus_active = false
                    │       │                       │   • fork_db.set_emergency_mode(false)
                    │       └── NO                  │
                    └──────────────────────────────┘
                                    │
                                    ▼
                    ┌──────────────────────────────┐
                    │ update_last_irreversible_block│
                    │                               │
                    │  During emergency:            │
                    │   • All schedule witnesses    │
                    │     used (including committee) │
                    │   • 75% nth_element threshold  │
                    │   • LIB capped at HEAD−1      │
                    │     (preserves undo session    │
                    │      for current block)        │
                    │   • LIB advances every block   │
                    │     (1 block behind head)      │
                    └──────────────────────────────┘
                                    │
                                    ▼
                    ┌──────────────────────────────┐
                    │   Startup Recovery            │
                    │   (database::open)            │
                    │                               │
                    │  If schedule has empty slots:  │
                    │   • Fill all with committee    │
                    │   • Ensure emergency active    │
                    │   • Restore fork_db flag       │
                    └──────────────────────────────┘
```

---

## Failure & Rollback Procedures

### F1: Emergency Activated Erroneously

**When**: A bug or time desync causes `seconds_since_lib >= 3600` while the network is actually healthy.

**Why this is extremely unlikely**: Emergency activation is fully deterministic: `seconds_since_lib = b.timestamp - lib_block.timestamp`. For a false trigger, LIB must genuinely stall for 1 hour (1200 blocks at 3s intervals). No config flags or skip-flags gate the check — it relies solely on signed block timestamps embedded in the chain, ensuring identical results on every node and during every replay.

**Special case — snapshot restore**: After `open_from_snapshot()`, the block_log is empty, so `fetch_block_by_number(LIB)` returns invalid. If we fell back to `genesis_time`, emergency would activate immediately. This is now prevented: when the LIB block is unavailable, the emergency check is skipped entirely (B7 fix).

**If it happens** (legitimate false activation):
1. Emergency activates, all real witnesses are **disabled** (`signing_key` zeroed).
2. Committee produces blocks alone initially.
3. Since all witnesses had valid keys before, operators quickly re-register via `witness_update_operation`.
4. Once **16+ real witnesses** (75% of 21) have re-registered with valid signing keys in the hybrid schedule, **emergency exits automatically**.
5. **Manual intervention required**: operators must re-register witnesses via transactions.

**Worst case**: If all 21 witness operators are available, recovery takes as fast as they can broadcast `witness_update_operation` transactions.

### F2: Emergency Exit Does Not Trigger

**When**: Emergency is active but the exit condition (75% real witnesses in schedule) never becomes true.

**Root cause**: Fewer than 75% of the 21 schedule slots (i.e., <16) are occupied by real witnesses with valid `signing_key`. Since all witnesses are disabled on activation, operators must manually re-enable them.

**Recovery procedure**:
1. **Check how many witnesses are active**: `get_dynamic_global_properties` → `participation_count`, plus inspect `witness_schedule` to see how many non-committee slots exist.
2. **Activate more witnesses**: Each witness operator re-registers via `witness_update_operation` from CLI wallet or service. The witness only needs a valid `signing_key` — the hybrid schedule will automatically assign their slot on the next schedule update.
3. **No config changes needed**: The `enable-stale-production` and `required-participation` settings are auto-bypassed during emergency. Witnesses just need to be connected to P2P and have their signing key registered.
4. **Threshold**: Once 16+ witnesses have valid signing keys in the schedule, emergency exits on the next `update_witness_schedule()` call.

**If witnesses cannot re-register** (e.g., lost master keys): The network remains in emergency mode indefinitely but still produces blocks. Governance intervention (committee proposals) would be needed to resolve witness account recovery.

### F3: Committee Chain Split (Multiple Emergency Producers)

**When**: Multiple nodes with the emergency private key produce competing blocks at the same slot during emergency.

**Why this is expected and handled**:
1. **Hash tie-breaking** (`fork_database::_push_block()`): When two blocks are at the same height during emergency, the one with the lower `block_id` (SHA256 hash) wins deterministically. All nodes converge to the same block within 1 P2P propagation round.
2. **Fork collision check** (`witness.cpp`): After the first slot, nodes detect that a competing block already exists at the target height and skip production. This reduces multi-producer collisions to a transient 1–2 slot artifact.
3. **No permanent split**: Because all emergency blocks use the same `committee` witness account and the schedule is identical on all nodes, there is no ongoing disagreement. Convergence is guaranteed within seconds.

**If a persistent split occurs** (e.g., network partition during emergency):
- LIB is 1 block behind head on all partitions → most emergency blocks are irreversible.
- When partitions reconnect, **vote-weighted chain comparison** (`push_block()`) resolves the fork:
  - Branches with real witnesses (non-committee) win over pure-committee branches.
  - Among branches with only committee blocks, the longer chain wins, with hash tie-breaking as the final tiebreaker.
- The losing partition unwinds its reversible blocks and syncs from the winner.

### F4: Emergency Lasts Longer Than Undo Window

**When**: This failure mode is now **largely mitigated**. LIB advances every block during emergency (capped at HEAD−1), so the gap between HEAD and LIB stays at exactly 1 block. The 10,000-block undo limit cannot be reached.

**Previous behavior (before LIB advancement fix)**: LIB was frozen during emergency, and the gap grew at 1 block per 3 seconds. After ~8.3 hours (10,000 blocks), the undo limit was hit and block production halted.

**Current behavior**: LIB = HEAD−1 at all times during emergency. `fork_db` size stays at 2 blocks. Emergency can run indefinitely without hitting any undo limits.

**Remaining risk**: If a bug prevents LIB from advancing despite the cap, the old failure mode would resurface. The startup recovery mechanism (B12) provides an additional safety net.

### F5: Witnesses Disabled On Emergency Activation

**When**: On emergency activation, **all real witnesses are disabled**: `signing_key` is set to `public_key_type()` (null), `penalty_percent` reset to 0, `counted_votes` restored to `votes`, `current_run` reset to 0. All `witness_penalty_expire_object` entries are removed.

**Emergency behavior**:
1. On activation, **all real witnesses are immediately disabled** by zeroing their `signing_key`. This is intentional — it ensures a clean start where only the committee produces blocks. Operators must explicitly re-register witnesses.
2. **During emergency, offline witnesses do NOT accumulate new missed-block penalties**. The `update_global_dynamic_data()` penalty/shutdown logic is skipped for witnesses that are not the block producer and not the committee account.
3. Witnesses must broadcast `witness_update_operation` to re-register their signing key.

**Recovery**: Emergency blocks **allow transactions** (not forced empty). So witnesses can:
1. Connect their node to the P2P network.
2. Use CLI wallet or web services to broadcast `witness_update_operation` with their signing key.
3. The transaction enters the next emergency block.
4. On the next schedule update, the witness gets their slot back in the hybrid schedule.

**This is intentional**: Disabling all witnesses on activation ensures that only intentionally re-registered witnesses participate. This prevents stale/crashed witnesses from being included in the schedule and causing production failures.

---

## Bugs Found and Fixed

The following bugs were discovered during code review of the emergency consensus implementation, specifically for the scenario of **1 node running 11 top witnesses** with other witnesses expected to join later.

### B1 (Critical): Hybrid Schedule Doesn't Expand `num_scheduled_witnesses`

**Problem**: `update_witness_schedule()` sets `num_scheduled_witnesses` to the count of witnesses with valid signing keys (e.g., 11). The hybrid override loop only iterated up to `num_scheduled_witnesses`, so empty slots at indices 11-20 were never visited and never assigned to committee. After the first schedule round, committee disappeared from production entirely.

**Fix**: The hybrid override now iterates the full `CHAIN_MAX_WITNESSES` range, reads entries beyond `num_scheduled_witnesses` as empty, assigns committee to all empty/unavailable slots, and sets `num_scheduled_witnesses = CHAIN_MAX_WITNESSES * CHAIN_BLOCK_WITNESS_REPEAT`.

### B2 (Critical): Committee Over-Counted in Hardfork Vote Tally

**Problem**: With `CHAIN_BLOCK_WITNESS_REPEAT = 1`, the hardfork vote tally iterates every schedule slot. Committee filling 10 slots caused `get_witness("committee")` to be called 10 times, incrementing the committee's vote count by 10. The committee's default `hardfork_version_vote = 0.0.0` dominated the tally and blocked any hardfork from reaching `CHAIN_HARDFORK_REQUIRED_WITNESSES = 17`.

**Fix**: The hardfork vote tally now skips `CHAIN_EMERGENCY_WITNESS_ACCOUNT` during emergency mode. Only real witnesses' votes count toward hardfork adoption.

### B3 (High): Committee Skews Median Chain Properties

**Problem**: `update_median_witness_props()` collected all schedule entries including 10 committee copies. The committee's default `chain_properties` (zero fees, zero sizes, zero penalties) skewed the median, enabling spam attacks and removing miss penalties.

**Fix** (two-part):
1. The committee witness is initialized with `props = median_props` (current median), and re-synced every schedule update. This makes committee entries neutral — they reinforce the existing median rather than distorting it.
2. As defense-in-depth, `update_median_witness_props()` skips committee entries during emergency mode. This ensures the median reflects only real witnesses' preferences.

### B4 (High): Offline Witnesses Accumulate Penalties During Emergency

**Problem**: When committee produced a block, the `missed_blocks` loop in `update_global_dynamic_data()` applied penalties to offline witnesses for every missed slot. After 200 missed blocks (~10 minutes), their `signing_key` was set to null again — the same problem that emergency activation's penalty reset tried to solve.

**Fix**: During emergency mode, the penalty/shutdown logic is skipped for witnesses that are not the block producer and not the committee. Only `current_run` is reset; `total_missed`, `penalty_percent`, and `signing_key` shutdown do not apply.

### B5 (Medium): Committee Could Be Selected as Top/Support Witness

**Problem**: The top/support witness selection iterated by `counted_votes`. If the committee witness ever received votes, it could compete for a production slot, displacing a real witness.

**Fix**: Both top and support witness selection loops now explicitly exclude `CHAIN_EMERGENCY_WITNESS_ACCOUNT`.

### B6 (Medium): Committee Hardfork Vote Auto-Injected via Block Extensions

**Problem**: `_generate_block()` auto-injects a `hardfork_version_vote` extension when the witness's on-chain vote doesn't match the binary's configured next hardfork. For the committee witness (which votes for `current_hardfork_version`), this extension overwrote the on-chain vote to the next hardfork version via `process_header_extensions()`, defeating the neutral-voter design.

**Fix**: When the block producer is the emergency committee, hardfork vote auto-injection is skipped entirely. The committee's on-chain vote stays at `current_hardfork_version` and is re-synced every schedule update.

### B7 (Critical): False Emergency Activation After Snapshot Restore

**Problem**: In `update_global_dynamic_data()`, when `fetch_block_by_number(LIB)` returns invalid (block_log empty after `open_from_snapshot()`), `lib_time` fell back to `_dgp.genesis_time`. This made `seconds_since_lib = block_timestamp - genesis_time` — millions of seconds — causing emergency activation on the very first block after snapshot restore.

The false activation triggers catastrophic side effects:
1. Schedule overridden to all-committee, but `next_shuffle_block_num` not updated → hybrid override can't run until the next shuffle boundary.
2. Blocks from real witnesses (via p2p) are rejected because the schedule expects `committee` → `head_block_num()` doesn't advance.
3. `next_shuffle_block_num` is never reached → **deadlock: the node permanently stops syncing**. Probability: ~20/21 (~95%) depending on how close the next shuffle was.
4. Side effects: all witness penalties reset, committee witness object created, consensus state corrupted.

**Fix** (two-part):
1. When `lib_block` is not found (block_log empty after snapshot restore), `lib_time_available` stays `false` and the emergency check is skipped entirely. Emergency cannot activate without a valid LIB timestamp.
2. On emergency activation, `next_shuffle_block_num` is now set to `head_block_num() + num_scheduled_witnesses`, ensuring the hybrid override runs on the next schedule update even after a legitimate activation.

### B8 (Medium): `inhibit_fetching_sync_blocks` Never Reset After Soft-Ban Expires

**Problem**: In `node.cpp`, when a soft-ban is applied (`fork_rejected_until = now + 1h`), `inhibit_fetching_sync_blocks` is also set to `true`. After 1 hour, `fork_rejected_until` expires and blocks are accepted again, but `inhibit_fetching_sync_blocks` remains `true` forever — the node never requests sync inventory from that peer again. During extended emergency operation, the number of peers available for sync gradually decreases as each soft-banned peer's inhibit flag becomes permanent.

**Fix**: In `process_block_during_normal_operation()`, after the `fork_rejected_until` check passes (ban expired), if `inhibit_fetching_sync_blocks` is `true` and `fork_rejected_until` is set and has expired, reset `inhibit_fetching_sync_blocks = false`. The check is targeted: it only resets the flag when `fork_rejected_until` is non-default (i.e., was set by a soft-ban), so `inhibit_fetching_sync_blocks` set for other reasons (missing sync items, old fork) is not affected.

### B9 (Medium): `unlinkable_block_exception` Causes Infinite Resync Instead of Soft-Ban

**Problem**: In `process_block_during_normal_operation()` (`node.cpp`), `unlinkable_block_exception` inherits from `fc::exception` but is caught by a more specific `catch` handler before the general `fc::exception` handler. The specific handler unconditionally sets `restart_sync_exception`, which triggers `start_synchronizing_with_peer()`. The `e.code() == unlinkable_block_exception::code_enum::code_value` check in the `fc::exception` handler was dead code — it could never be reached for unlinkable blocks.

When a peer on a stale fork sends an unlinkable block at or below our head block number, the node enters an infinite resync loop: it requests blocks from the stale peer, cannot link them, requests again, etc.

**Fix** (two-part):
1. In the `unlinkable_block_exception` catch handler, compare the peer's block number against our head. If the block is at or below our head, the peer is on a stale fork — soft-ban for 1 hour and set `inhibit_fetching_sync_blocks = true`. If the block is ahead of us, resync is justified (keep original behavior).
2. Remove the dead `unlinkable_block_exception::code_enum::code_value` check from the `fc::exception` handler, leaving only the `block_num <= head` comparison for the soft-ban decision.

### B10 (Critical): All Real Witnesses Must Be Disabled On Emergency Activation

**Problem**: When emergency consensus activated, real witnesses retained their `signing_key` values. If they had valid signing keys but were offline, the hybrid schedule assigned them slots — but they could never produce blocks at those slots. The schedule had a mix of online committee and offline real witnesses, making block production unreliable.

**Fix**: On emergency activation, **all real witnesses are immediately disabled**: `signing_key` set to `public_key_type()` (null), `penalty_percent` reset to 0, `counted_votes` restored to `votes`, `current_run` reset to 0. All `witness_penalty_expire_object` entries are removed. This ensures the initial schedule is all-committee (100% available). Operators must explicitly re-register witnesses via `witness_update_operation` transactions.

### B11 (Critical): Schedule Crash — `get_witness("")` on Empty Schedule

**Problem**: During emergency, the normal schedule build in `update_witness_schedule()` iterates all witnesses but skips any with null `signing_key`. Since B10 zeroes all keys, `sum_witnesses_count = 0` → all 21 slots are set to `account_name_type()` (empty string). The execution order was:
1. `modify(wso, ...)` — builds normal schedule (all slots zeroed)
2. `update_median_witness_props()` — iterates schedule, calls `get_witness("")` → **crash: `unknown key`**
3. Emergency hybrid override — would have filled empty slots with committee, but runs too late

**Fix**: Moved the emergency hybrid schedule override to execute **before** `update_median_witness_props()`. The hybrid override fills empty/unavailable slots with committee first, so by the time `update_median_witness_props()` runs, all 21 slots contain valid witness names.

### B12 (Critical): Permanently Corrupted Schedule After LIB=HEAD Commit

**Problem**: During emergency, all 21 schedule slots point to the same committee witness. LIB computation via `nth_element` yields `last_supported_block_num == HEAD`. In `_apply_block`, the execution order is:
1. `update_last_irreversible_block()` (line ~4455) — calls `commit(HEAD)`, which **destroys the undo session** for the current block
2. `update_witness_schedule()` (line ~4466) — zeros all slots (pre-B11 fix), then crashes at `get_witness("")`

Since `commit(HEAD)` already consumed the undo session, the zeroed schedule from step 2 was **permanently written** to shared memory with no rollback possible. On node restart, the schedule contained all empty slots, emergency mode was not re-entered, and the node couldn't produce blocks.

**Fix** (three-part):
1. **LIB cap to HEAD−1**: During emergency, `new_last_irreversible_block_num` is capped to `head_block_number - 1`. This ensures `commit()` never catches up to the current block, preserving the undo session.
2. **Schedule override execution order** (B11 fix): Hybrid override runs before `update_median_witness_props()`, preventing the crash entirely.
3. **Startup schedule recovery**: In `database::open()`, after `init_hardforks()`, the schedule is scanned for empty slots. If found: all 21 slots are filled with committee, `emergency_consensus_active` is set to `true`, and `fork_db.set_emergency_mode(true)` is called. If the schedule is OK but emergency is active, the fork_db flag is restored (it's in-memory only and lost on restart).

### Committee Neutral Voter Design

After all fixes, the committee witness has these properties:

| Field | Value | Rationale |
|---|---|---|
| `signing_key` | `CHAIN_EMERGENCY_WITNESS_PUBLIC_KEY` | Required for block production |
| `running_version` | `CHAIN_VERSION` | Matches the binary |
| `hardfork_version_vote` | `current_hardfork_version` | Votes for status quo, not future hardforks |
| `hardfork_time_vote` | `processed_hardforks[last_hardfork]` | Time the current hardfork was applied |
| `props` | `median_props` (current) | Copies current median, doesn't skew it |
| `schedule` | `top` | Required for hybrid schedule assignment |

The committee's props and hardfork vote are re-synced every schedule update (`update_witness_schedule()`) to stay aligned with the latest median and hardfork state.

---

## Emergency Key Threat Model

### Key Distribution

The emergency private key is **publicly known**. It is not a secret. Any node operator can configure it via `emergency-private-key` in their config. The corresponding public key is hardcoded in `config.hpp`:

```
VIZ75CRHVHPwYiUESy1bgN3KhVFbZCQQRA9jT6TnpzKAmpxMPD6Xv
```

### Who Has the Key

**Everyone who wants it.** The key is published in:
- Source code (`config.hpp`)
- Config template (`config_witness.ini`)
- Documentation

Any node running the witness plugin with `emergency-private-key` configured will attempt to produce blocks during emergency mode. This is by design — the goal is maximum availability during a network stall, not access control.

### How Many Nodes Have It

In practice: **every public witness node** should have it configured. During emergency:
- Multiple nodes may attempt to produce at the same slot.
- Hash tie-breaking (lowest `block_id` wins) ensures deterministic convergence.
- Fork collision check causes all but one node to back off after the first slot.
- The result is functionally equivalent to a single producer, achieved through distributed consensus rather than central coordination.

### Compromise Scenario

**The key cannot be "compromised" in the traditional sense** because it is already public. However, there are attack scenarios:

| Attack | Impact | Mitigation |
|---|---|---|
| Attacker produces emergency blocks during normal operation | **None.** Emergency mode only activates when `seconds_since_lib >= 3600`. During normal operation, the schedule does not contain `committee`, so the attacker's blocks are invalid (wrong scheduled witness). | Consensus-level gating |
| Attacker produces blocks during real emergency | Blocks are valid but **compete equally** with other emergency producers. Hash tie-breaking resolves conflicts deterministically. The attacker cannot produce *more* blocks than any other node with the key. | Hash tie-breaking + fork collision check |
| Attacker produces blocks with invalid transactions | **Rejected.** Full consensus validation still applies to emergency blocks. Invalid operations, double-spends, etc. are caught by `apply_block()`. | Standard block validation |
| Attacker floods P2P with emergency blocks | **Mitigated.** P2P anti-spam (`fork_rejected_until`) soft-bans peers sending blocks on rejected forks for 1 hour. Fork collision check limits production to 1 block per slot. After soft-ban expires, `inhibit_fetching_sync_blocks` is automatically reset (B8 fix) so the peer remains available for sync. | P2P soft-ban + fork collision + auto-reset |

### Key Rotation

**No rotation needed or supported.** Rationale:
- The key has no value outside emergency mode (it cannot sign normal blocks).
- Rotation would require a hardfork (the public key is in `config.hpp` as a consensus constant).
- There is zero security benefit from rotation — the key is public by design.
- All nodes must agree on the same key, so any change requires coordinated network upgrade.

### Can Emergency Produce Non-Empty Blocks?

**Yes, and this is required.** Emergency blocks must allow transactions because:
1. Witnesses need to broadcast `witness_update_operation` to re-register their signing key during recovery.
2. If emergency blocks were forced empty, witnesses couldn't re-activate → deadlock where emergency never exits.
3. In practice, most emergency blocks will be empty (low transaction volume during a stall), but the mechanism **must not** prohibit transactions.

### Summary

The emergency key is a **public coordination mechanism**, not a secret. Its security model is:
- **Deterministic activation**: Uses only signed block timestamps (`b.timestamp - lib_block.timestamp ≥ 3600`), no config flags or wall-clock time — identical results on every node and during every replay
- **Activation gating**: Only usable when `emergency_consensus_active == true` (requires 1-hour LIB stall)
- **Snapshot safety**: When LIB block is unavailable (post-snapshot restore), emergency check is skipped to prevent false activation
- **Convergence**: Hash tie-breaking + fork collision → single effective producer
- **Validation**: Full consensus rules apply to emergency blocks
- **Deactivation**: Automatic when 75% (16/21) of schedule slots are real witnesses with valid signing keys
- **Scope**: Delegates can immediately re-activate through wallets/services once their nodes reconnect to the P2P network, because the emergency chain is public and accepts transactions

---

## Test Matrix

All scenarios should be tested before deployment. Each test specifies preconditions, actions, expected outcomes, and the emergency subsystem components exercised.

### T1: Simple Stall → Emergency → Recovery

| | |
|---|---|
| **Precondition** | 21 witnesses active, network healthy |
| **Action** | Shut down all 21 witnesses. Wait >1 hour. Start 1 node with emergency key. Gradually restart witnesses. |
| **Expected** | Emergency activates at LIB+3600s. All real witnesses disabled (signing_key zeroed). Committee produces blocks (full 21-slot schedule). LIB advances every block (capped at HEAD−1). Offline witnesses do NOT accumulate penalties. Committee props synced to median, hardfork vote synced to current version. Witnesses re-register via `witness_update_operation`. When 16+ real witnesses have valid signing keys in schedule (75%) → emergency exits automatically. |
| **Components** | Activation, witness disabling, hybrid schedule (full expansion), LIB advancement (capped at HEAD−1), penalty skip, committee neutral voter, exit condition (75% real witnesses) |

### T2: 2-Way Partition (Majority/Minority)

| | |
|---|---|
| **Precondition** | 21 witnesses, healthy network |
| **Action** | Partition: 16 witnesses on side A, 5 on side B. |
| **Expected** | Side A: participation >75% → continues normally, no emergency. Side B: participation drops to ~24% → production stops (below 33% threshold). After 1 hour, emergency activates on side B. On reconnect, side A's chain wins (higher vote weight from 16 real witnesses). Side B unwinds emergency blocks. |
| **Components** | Three-state safety (healthy vs distressed), vote-weighted comparison, fork resolution |

### T3: 2-Way Partition (Even Split, Both Enter Emergency)

| | |
|---|---|
| **Precondition** | 21 witnesses, healthy network |
| **Action** | Partition: 10 witnesses on A, 11 on B. Neither side has 75% → both stall. Wait 1 hour → both enter emergency. Reconnect after 2 hours. |
| **Expected** | Both sides produce emergency+hybrid blocks. LIB advances (capped at HEAD−1) on both. On reconnect, vote-weighted comparison: side with higher total `votes` wins. Losing side unwinds. Emergency exits when 16+ real witnesses have valid keys in the merged schedule. |
| **Components** | LIB advancement (capped), vote-weighted comparison, fork_db expansion, partition merge |

### T4: 3-Way Partition

| | |
|---|---|
| **Precondition** | 21 witnesses, healthy network |
| **Action** | Partition into 3 groups: 7+7+7 witnesses. All stall → all enter emergency. Reconnect sequentially (A↔B first, then AB↔C). |
| **Expected** | Each partition produces emergency blocks independently. LIB advances (capped at HEAD−1) on all. First merge (A↔B): vote-weighted comparison picks winner. Second merge (AB↔C): vote-weighted comparison again. Final chain has highest cumulative vote weight. |
| **Components** | Multi-partition merge, vote-weighted comparison, cascading fork resolution, LIB advancement (capped) |

### T5: Late Peer Rejoin

| | |
|---|---|
| **Precondition** | Emergency active for 2 hours. 10 witnesses already back. |
| **Action** | Witness #11 comes online with stale chain (2 hours behind). |
| **Expected** | Peer syncs from the emergency chain. Once synced, witness re-registers via `witness_update_operation`. Their slot appears in hybrid schedule on next update. Witness produces at its assigned slot. LIB on the emergency chain is at HEAD−1, so only 1 reversible block exists — sync is fast. |
| **Components** | P2P sync during emergency, hybrid schedule, LIB advancement (capped), witness re-registration |

### T6: Conflicting Emergency Producers

| | |
|---|---|
| **Precondition** | Emergency active. 5 nodes with emergency key, well-connected. |
| **Action** | All 5 attempt to produce at slot N simultaneously. |
| **Expected** | Each produces a valid block with different `block_id`. Nodes receive competing blocks. Hash tie-breaking: lowest `block_id` wins on all nodes. Fork collision check: at slot N+1, nodes see existing block → only 1 (or 0) produces. By slot N+2, a single producer dominates. |
| **Components** | Hash tie-breaking, fork collision check, deterministic convergence |

### T7: Long Emergency > Undo Horizon

| | |
|---|---|
| **Precondition** | All witnesses offline. Emergency active. |
| **Action** | Let emergency run for 8+ hours (well beyond old undo limit). |
| **Expected** | LIB advances every block (capped at HEAD−1), so HEAD−LIB gap stays at exactly 1. fork_db size stays at 2 blocks. Emergency runs indefinitely without hitting any undo limits. No degradation over time. |
| **Components** | LIB advancement (capped at HEAD−1), fork_db sizing, indefinite emergency operation |

### T8: Witness Shutdown + Re-registration During Emergency

| | |
|---|---|
| **Precondition** | Emergency active. All witnesses had `signing_key` nullified by missed-block shutdown. |
| **Action** | Witness operator broadcasts `witness_update_operation` via CLI wallet during emergency. |
| **Expected** | Transaction included in emergency block. Witness object updated with new signing key. Next schedule update: witness gets their slot in hybrid schedule instead of committee. Witness begins producing. Offline witnesses do NOT get `signing_key` nullified again during emergency (penalty/shutdown skipped). |
| **Components** | Transaction processing during emergency, hybrid schedule update, penalty skip during emergency |

### T9: Snapshot Restore + Emergency Interaction

| | |
|---|---|
| **Precondition** | Snapshot taken during emergency mode (`emergency_consensus_active = true`). |
| **Action** | Restore snapshot on a fresh node. Start node with witness plugin and emergency key. |
| **Expected** | Snapshot import reads `emergency_consensus_active` and `emergency_consensus_start_block` from DGP (forward-compatible). Node resumes in emergency mode. Produces emergency blocks. Standard exit condition applies. **No false activation**: block_log is empty after snapshot restore, so `fetch_block_by_number(LIB)` returns invalid, but the emergency check is skipped (not triggered by fallback to genesis_time). |
| **Components** | Snapshot import (forward-compatible fields), emergency state persistence, B7 fix (no false activation on empty block_log) |

### T10: Snapshot From Pre-HF12 Node

| | |
|---|---|
| **Precondition** | Snapshot taken before HF12 (no emergency fields in DGP). |
| **Action** | Restore on HF12 node. |
| **Expected** | Snapshot import defaults `emergency_consensus_active = false`, `emergency_consensus_start_block = 0`. Node starts in normal mode. Emergency detection works normally after HF12 activation. |
| **Components** | Snapshot forward-compatibility, default field handling |

### T11: Healthy Network After HF12 (No Regression)

| | |
|---|---|
| **Precondition** | HF12 active. All 21 witnesses online. Network healthy. |
| **Action** | Normal operation for extended period. Occasional witness restarts. |
| **Expected** | Emergency never activates (LIB advances every few seconds). Three-state safety: healthy mode enforces safe defaults regardless of `enable-stale-production` config. Vote-weighted comparison active but functionally equivalent to longest-chain (same witnesses on both sides of any micro-fork). Committee exclusion in hardfork tally and median props is a no-op (committee not in schedule). |
| **Components** | No-regression, three-state safety (healthy mode), vote-weighted comparison |

### T12: `enable-stale-production` Ignored in Healthy Mode

| | |
|---|---|
| **Precondition** | HF12 active. All witnesses online. Operator has `enable-stale-production = true` (forgot to revert from pre-HF12). |
| **Action** | Network partition isolates this witness. |
| **Expected** | Participation rate ≥33% → healthy mode → `enable-stale-production` is **ignored**. Witness stops producing when it detects it's isolated (no recent blocks). **This is the core micro-fork prevention feature.** |
| **Components** | Three-state safety (healthy mode auto-enforces safe defaults) |

### T13: Partial Witness Set (11 Top Witnesses on 1 Node)

| | |
|---|---|
| **Precondition** | Network stalled >1 hour. 1 node with 11 top witnesses + emergency key. 10 other witnesses offline. |
| **Action** | Emergency activates. 11 real witnesses produce at their slots. Committee fills the other 10 slots. Over time, other witnesses re-join. |
| **Expected** | All witnesses initially disabled (signing_key zeroed). 11 witnesses re-register via `witness_update_operation`. Hybrid schedule expands to full 21 slots (11 real + 10 committee). Committee witness has `props = median_props` and `hardfork_version_vote = current_hardfork_version` — neutral voter. Hardfork vote tally excludes committee (only 11 real votes counted). Median props computed from real witnesses only. Offline witnesses don't accumulate penalties. When 16+ real witnesses have valid signing keys in schedule (75% of 21) → emergency exits automatically. |
| **Components** | Witness disabling, hybrid schedule expansion, committee neutral voter, hardfork tally exclusion, median props exclusion, penalty skip, exit condition (75% real witnesses) |

### T14: Committee Hardfork Vote Neutrality

| | |
|---|---|
| **Precondition** | Emergency active. Binary version includes a pending hardfork (e.g., HF13) that has not been applied on-chain yet. 11 real witnesses running HF13 binary. |
| **Action** | Committee produces blocks. Verify committee's on-chain `hardfork_version_vote` stays at the current applied version. |
| **Expected** | Committee's block headers do NOT contain `hardfork_version_vote` extensions. `process_header_extensions()` does not update the committee's on-chain vote. Committee vote stays at `current_hardfork_version` (e.g., HF12). Only real witnesses' votes count toward HF13 adoption (need 17 of them). Committee props/hardfork vote re-synced every schedule update. |
| **Components** | Hardfork vote auto-injection skip, process_header_extensions, committee props sync |

### T15: Snapshot Restore Does Not False-Activate Emergency

| | |
|---|---|
| **Precondition** | Network healthy. Snapshot taken from a healthy state (`emergency_consensus_active = false`). |
| **Action** | Restore snapshot on a fresh node (block_log empty). Start syncing from p2p. |
| **Expected** | First block from p2p: `fetch_block_by_number(LIB)` returns invalid (block_log empty). `lib_time_available = false`. Emergency check is **skipped**. Node processes the block normally. No false activation, no committee witness created, no penalties reset. Node syncs normally. |
| **Components** | B7 fix (lib_time_available guard), snapshot restore, block_log interaction |

### T16: Soft-Ban `inhibit_fetching_sync_blocks` Reset After Expiry

| | |
|---|---|
| **Precondition** | Emergency active. Node soft-banned a peer 1 hour ago (`fork_rejected_until` set, `inhibit_fetching_sync_blocks = true`). |
| **Action** | The soft-ban expires. Peer sends a valid block. |
| **Expected** | `fork_rejected_until` is in the past → block is processed (not discarded). `inhibit_fetching_sync_blocks` is reset to `false` (B8 fix). Node resumes requesting sync inventory from this peer. Gradual peer loss during extended emergency is prevented. |
| **Components** | B8 fix (inhibit flag reset on soft-ban expiry), P2P soft-ban lifecycle, sync operations |

### T17: Startup Schedule Recovery (B12 Fix)

| | |
|---|---|
| **Precondition** | Node crashed during emergency with corrupted schedule (all empty witness slots in shared memory). |
| **Action** | Restart the node. |
| **Expected** | `database::open()` detects empty slots in schedule. Fills all 21 slots with committee. Sets `emergency_consensus_active = true` if not already set. Restores `fork_db.set_emergency_mode(true)`. Node resumes producing emergency blocks. LIB advances normally (capped at HEAD−1). |
| **Components** | Startup recovery (B12), schedule repair, fork_db flag restoration |

### T18: Emergency fork_db Flag Restored On Normal Restart

| | |
|---|---|
| **Precondition** | Emergency active, schedule is healthy (all committee slots). Node restarted cleanly. |
| **Action** | Restart the node. |
| **Expected** | `database::open()` sees schedule is OK but `emergency_consensus_active == true` in DGP. Calls `fork_db.set_emergency_mode(true)` to restore the in-memory flag. Node continues in emergency mode without interruption. |
| **Components** | Startup recovery (fork_db flag), DGP state persistence |
