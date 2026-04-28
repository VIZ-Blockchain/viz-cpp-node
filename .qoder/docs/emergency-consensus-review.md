# Emergency Consensus Recovery — Implementation Review

## Status: Implemented (Hardfork 12, version 3.1.0) — Bugs Found and Fixed (B1–B8)

Research source: [consensus-emergency-recovery.md](../research/consensus-emergency-recovery.md)

---

## System Overview

Hardfork 12 adds an on-chain **Emergency Consensus Mode** that activates automatically when the VIZ network stalls for >1 hour (no LIB advancement). The activation check is fully deterministic — it uses only block timestamps from the chain state (`b.timestamp - lib_block.timestamp`), ensuring identical results on every node and during every replay. A well-known committee key (`VIZ75CRHVHPwYiUESy1bgN3KhVFbZCQQRA9jT6TnpzKAmpxMPD6Xv`) becomes the block producer, keeping the chain alive until real witnesses return.

The committee witness is a **neutral voter**: it copies the current median chain properties and votes for the currently applied hardfork version (not a future one). This ensures that committee slots in the schedule don't skew governance parameters or push unvoted hardforks.

### Key Files Modified

| File | Role |
|---|---|
| `libraries/chain/hardfork.d/12.hf` | Hardfork 12 definition |
| `libraries/protocol/include/graphene/protocol/config.hpp` | Emergency constants, version 3.1.0 |
| `libraries/chain/include/graphene/chain/database.hpp` | DGP fields, skip flags |
| `libraries/chain/database.cpp` | Activation, hybrid schedule, LIB freeze, vote-weighted fork comparison |
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
                    │           │                   │   • Reset all penalties
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
                    │  Hybrid schedule:             │
                    │   • Real witnesses keep slots │
                    │   • Committee fills gaps      │
                    │   • Expand to full 21 slots   │
                    │   • Sync committee props/vote │
                    │                               │
                    │  Skip committee in:           │
                    │   • Hardfork vote tally       │
                    │   • Median props computation  │
                    │                               │
                    │  Exit check:                  │
                    │   LIB > emergency_start_block?│
                    │       ├── YES ───────────────►│── Deactivate Emergency
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
                    │   • Count only real witnesses │
                    │   • 0 real → LIB frozen       │
                    │   • N real → 75% of N needed  │
                    │   • LIB advances → triggers   │
                    │     exit in schedule update   │
                    └──────────────────────────────┘
```

---

## Failure & Rollback Procedures

### F1: Emergency Activated Erroneously

**When**: A bug or time desync causes `seconds_since_lib >= 3600` while the network is actually healthy.

**Why this is extremely unlikely**: Emergency activation is fully deterministic: `seconds_since_lib = b.timestamp - lib_block.timestamp`. For a false trigger, LIB must genuinely stall for 1 hour (1200 blocks at 3s intervals). No config flags or skip-flags gate the check — it relies solely on signed block timestamps embedded in the chain, ensuring identical results on every node and during every replay.

**Special case — snapshot restore**: After `open_from_snapshot()`, the block_log is empty, so `fetch_block_by_number(LIB)` returns invalid. If we fell back to `genesis_time`, emergency would activate immediately. This is now prevented: when the LIB block is unavailable, the emergency check is skipped entirely (B7 fix).

**If it happens** (legitimate false activation):
1. Emergency activates, committee fills offline witnesses' slots (hybrid schedule).
2. Real witnesses are still online → they produce at their normal slots.
3. LIB continues advancing via real witnesses (75% threshold on real witnesses only).
4. **LIB passes `emergency_consensus_start_block` within 1 round (~63 seconds)** → emergency exits automatically.
5. **No manual intervention required**. The exit is self-healing because LIB is already moving.

**Worst case**: If all 21 witnesses are healthy and LIB is advancing, emergency cannot persist for more than 1 schedule round.

### F2: Emergency Exit Does Not Trigger

**When**: Emergency is active but the exit condition (`LIB > emergency_consensus_start_block`) never becomes true.

**Root cause**: Fewer than 75% of the 21 scheduled witnesses (i.e., <16) have returned and are actively producing. The LIB computation during emergency uses only real (non-committee) witnesses at a 75% threshold of real witnesses, but LIB must advance past `emergency_consensus_start_block`, which requires sustained production.

**Recovery procedure**:
1. **Check how many witnesses are active**: `get_dynamic_global_properties` → `participation_count`, plus inspect `witness_schedule` to see how many non-committee slots exist.
2. **Activate more witnesses**: Each witness operator re-registers via `witness_update_operation` from CLI wallet or service. The witness only needs a valid `signing_key` — the hybrid schedule will automatically assign their slot.
3. **No config changes needed**: The `enable-stale-production` and `required-participation` settings are auto-bypassed during emergency. Witnesses just need to be connected to P2P and have their signing key registered.
4. **Threshold**: Once 16+ witnesses are producing, LIB advances. Once LIB passes `emergency_consensus_start_block`, emergency exits on the next `update_witness_schedule()` call.

**If witnesses cannot re-register** (e.g., lost master keys): The network remains in emergency mode indefinitely but still produces blocks. Governance intervention (committee proposals) would be needed to resolve witness account recovery.

### F3: Committee Chain Split (Multiple Emergency Producers)

**When**: Multiple nodes with the emergency private key produce competing blocks at the same slot during emergency.

**Why this is expected and handled**:
1. **Hash tie-breaking** (`fork_database::_push_block()`): When two blocks are at the same height during emergency, the one with the lower `block_id` (SHA256 hash) wins deterministically. All nodes converge to the same block within 1 P2P propagation round.
2. **Fork collision check** (`witness.cpp`): After the first slot, nodes detect that a competing block already exists at the target height and skip production. This reduces multi-producer collisions to a transient 1–2 slot artifact.
3. **No permanent split**: Because all emergency blocks use the same `committee` witness account and the schedule is identical on all nodes, there is no ongoing disagreement. Convergence is guaranteed within seconds.

**If a persistent split occurs** (e.g., network partition during emergency):
- LIB is frozen on all partitions → all emergency blocks are reversible.
- When partitions reconnect, **vote-weighted chain comparison** (`push_block()`) resolves the fork:
  - Branches with real witnesses (non-committee) win over pure-committee branches.
  - Among branches with only committee blocks, the longer chain wins, with hash tie-breaking as the final tiebreaker.
- The losing partition unwinds its reversible blocks and syncs from the winner.

### F4: Emergency Lasts Longer Than Undo Window

**When**: Emergency mode persists for >8.3 hours (>10,000 blocks), reaching `CHAIN_MAX_UNDO_HISTORY`.

**Mechanism**: During emergency, `update_last_irreversible_block()` dynamically expands `fork_db._max_size` up to `CHAIN_MAX_UNDO_HISTORY` (10,000 blocks). The formula: `min(head_block_number - last_irreversible_block_num + 1, 10000)`.

**What happens at the limit**:
- The undo history check in `database.cpp` enforces `head - LIB < CHAIN_MAX_UNDO_HISTORY`.
- If this limit is hit, the node cannot apply new blocks without committing (advancing LIB).
- Since LIB is frozen (no real witnesses), **block production halts**.

**Recovery procedure**:
1. **Before the limit**: Operators have ~8.3 hours to bring witnesses back. This is the hard deadline.
2. **If the limit is hit**: The node stops producing. Operators must:
   a. Bring enough witnesses online (16+) so LIB can advance.
   b. If that's impossible, a coordinated restart with `--replay` may be needed after witnesses are available.
3. **Prevention**: The 8.3-hour window is generous. In practice, any network stall >2 hours triggers community response (social media alerts, monitoring systems).

**Note**: The `fork_db` size increase from 1024 to 2400 blocks (~2 hours) is the *standard* limit. During emergency, dynamic expansion goes up to 10,000 blocks. These are separate mechanisms.

### F5: Witnesses Lost Their signing_key

**When**: Witnesses were shut down by the `CHAIN_MAX_WITNESS_MISSED_BLOCKS` (200 blocks) mechanism, which sets `signing_key = public_key_type()` (null key).

**Emergency behavior**:
1. On emergency activation, **all penalties are reset**: `penalty_percent = 0`, `counted_votes = votes`, `current_run = 0`. All `witness_penalty_expire_object` entries are removed.
2. **During emergency, offline witnesses do NOT accumulate new missed-block penalties**. The `update_global_dynamic_data()` penalty/shutdown logic is skipped for witnesses that are not the block producer and not the committee account. This prevents the `signing_key = null` shutdown from re-triggering after the initial penalty reset.
3. However, `signing_key` reset from **before** emergency activation is **not** reversed. Witnesses with null keys have their slots filled by committee (since `signing_key == null` triggers committee substitution in `update_witness_schedule()`).
4. Witnesses must broadcast `witness_update_operation` to re-register their signing key.

**Recovery**: Emergency blocks **allow transactions** (not forced empty). So witnesses can:
1. Connect their node to the P2P network.
2. Use CLI wallet or web services to broadcast `witness_update_operation` with their signing key.
3. The transaction enters the next emergency block.
4. On the next schedule update, the witness gets their slot back in the hybrid schedule.

**This is intentional**: `signing_key = null` is a consensus-level safety mechanism. Automatically restoring it would bypass the witness's explicit consent to re-enter production.

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
- **Deactivation**: Automatic when real witnesses return (LIB advancement)
- **Scope**: Delegates can immediately re-activate through wallets/services once their nodes reconnect to the P2P network, because the emergency chain is public and accepts transactions

---

## Test Matrix

All scenarios should be tested before deployment. Each test specifies preconditions, actions, expected outcomes, and the emergency subsystem components exercised.

### T1: Simple Stall → Emergency → Recovery

| | |
|---|---|
| **Precondition** | 21 witnesses active, network healthy |
| **Action** | Shut down all 21 witnesses. Wait >1 hour. Start 1 node with emergency key. Gradually restart witnesses. |
| **Expected** | Emergency activates at LIB+3600s. Committee produces blocks (full 21-slot hybrid schedule). Offline witnesses do NOT accumulate penalties. Committee props synced to median, hardfork vote synced to current version. Witnesses re-register via `witness_update_operation`. When 16+ produce → LIB advances → emergency exits. |
| **Components** | Activation, hybrid schedule (full expansion), LIB freeze, penalty skip, committee neutral voter, exit condition |

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
| **Expected** | Both sides produce emergency+hybrid blocks. LIB frozen on both. On reconnect, vote-weighted comparison: side with higher total `votes` wins. Losing side unwinds. Emergency exits when LIB advances. |
| **Components** | LIB freeze, vote-weighted comparison, fork_db expansion, partition merge |

### T4: 3-Way Partition

| | |
|---|---|
| **Precondition** | 21 witnesses, healthy network |
| **Action** | Partition into 3 groups: 7+7+7 witnesses. All stall → all enter emergency. Reconnect sequentially (A↔B first, then AB↔C). |
| **Expected** | Each partition produces emergency blocks independently. LIB frozen on all. First merge (A↔B): vote-weighted comparison picks winner. Second merge (AB↔C): vote-weighted comparison again. Final chain has highest cumulative vote weight. |
| **Components** | Multi-partition merge, vote-weighted comparison, cascading fork resolution |

### T5: Late Peer Rejoin

| | |
|---|---|
| **Precondition** | Emergency active for 2 hours. 10 witnesses already back. |
| **Action** | Witness #11 comes online with stale chain (2 hours behind). |
| **Expected** | Peer syncs from the emergency chain. Once synced, witness's slot appears in hybrid schedule. Witness produces at its assigned slot. No special handling needed — standard P2P sync. |
| **Components** | P2P sync during emergency, hybrid schedule, fork_db size (2400 standard) |

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
| **Action** | Let emergency run for 8+ hours (approaching 10,000 block undo limit). |
| **Expected** | fork_db dynamically expands up to `CHAIN_MAX_UNDO_HISTORY` (10,000). At the limit, block production halts because new blocks can't be applied without LIB advancement. Recovery requires bringing 16+ witnesses online. |
| **Components** | Dynamic fork_db expansion, undo history limit, LIB freeze |

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
| **Expected** | Hybrid schedule expands to full 21 slots (11 real + 10 committee). Committee witness has `props = median_props` and `hardfork_version_vote = current_hardfork_version` — neutral voter. Hardfork vote tally excludes committee (only 11 real votes counted). Median props computed from real witnesses only. Offline witnesses don't accumulate penalties. When 16+ real witnesses are producing, LIB advances past `emergency_consensus_start_block` → emergency exits. |
| **Components** | Hybrid schedule expansion, committee neutral voter, hardfork tally exclusion, median props exclusion, penalty skip, LIB computation with partial witnesses |

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
