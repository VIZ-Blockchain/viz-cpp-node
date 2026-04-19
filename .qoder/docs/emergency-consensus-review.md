# Emergency Consensus Recovery — Implementation Review

## Status: Implemented (Hardfork 12, version 3.1.0)

Research source: [consensus-emergency-recovery.md](../research/consensus-emergency-recovery.md)

---

## System Overview

Hardfork 12 adds an automatic on-chain **Emergency Consensus Mode** that activates when the VIZ network stalls for >1 hour (no LIB advancement). A well-known committee key (`VIZ75CRHVHPwYiUESy1bgN3KhVFbZCQQRA9jT6TnpzKAmpxMPD6Xv`) becomes the block producer, keeping the chain alive until real witnesses return. The system requires **zero manual intervention** to enter or exit.

### Key Files Modified

| File | Role |
|---|---|
| `libraries/chain/hardfork.d/12.hf` | Hardfork 12 definition |
| `libraries/protocol/include/graphene/protocol/config.hpp` | Emergency constants, version 3.1.0 |
| `libraries/chain/include/graphene/chain/global_property_object.hpp` | DGP fields: `emergency_consensus_active`, `emergency_consensus_start_block` |
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
                    │  seconds_since_lib ≥ 3600?    │
                    │       ├── YES ───────────────►│── Activate Emergency
                    │       │                       │   • emergency_consensus_active = true
                    │       │                       │   • Create/update committee witness
                    │       │                       │   • Reset all penalties
                    │       │                       │   • Override schedule → committee
                    │       │                       │   • fork_db.set_emergency_mode(true)
                    │       └── NO                  │
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

**Why this is extremely unlikely**: The check uses `head_block_time() - LIB_block_timestamp`. For a false trigger, LIB must genuinely stall for 1 hour (1200 blocks). During healthy operation LIB advances every few seconds (3s block interval × ~5 blocks to reach 75% threshold). A time desync large enough would break all consensus, not just emergency detection.

**If it happens**:
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
2. However, `signing_key` reset is **not** reversed by emergency mode. Witnesses with null keys appear in the hybrid schedule but their slots are filled by committee (since `signing_key == null` triggers committee substitution in `update_witness_schedule()`).
3. Witnesses must broadcast `witness_update_operation` to re-register their signing key.

**Recovery**: Emergency blocks **allow transactions** (not forced empty). So witnesses can:
1. Connect their node to the P2P network.
2. Use CLI wallet or web services to broadcast `witness_update_operation` with their signing key.
3. The transaction enters the next emergency block.
4. On the next schedule update, the witness gets their slot back in the hybrid schedule.

**This is intentional**: `signing_key = null` is a consensus-level safety mechanism. Automatically restoring it would bypass the witness's explicit consent to re-enter production.

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
| Attacker floods P2P with emergency blocks | **Mitigated.** P2P anti-spam (`fork_rejected_until`) soft-bans peers sending blocks on rejected forks for 1 hour. Fork collision check limits production to 1 block per slot. | P2P soft-ban + fork collision |

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
- **Activation gating**: Only usable when `emergency_consensus_active == true` (requires 1-hour LIB stall)
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
| **Expected** | Emergency activates at LIB+3600s. Committee produces blocks. Witnesses re-register via `witness_update_operation`. When 16+ produce → LIB advances → emergency exits. |
| **Components** | Activation, hybrid schedule, LIB freeze, penalty reset, exit condition |

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
| **Expected** | Transaction included in emergency block. Witness object updated with new signing key. Next schedule update: witness gets their slot in hybrid schedule instead of committee. Witness begins producing. |
| **Components** | Transaction processing during emergency, hybrid schedule update, penalty reset |

### T9: Snapshot Restore + Emergency Interaction

| | |
|---|---|
| **Precondition** | Snapshot taken during emergency mode (`emergency_consensus_active = true`). |
| **Action** | Restore snapshot on a fresh node. Start node with witness plugin and emergency key. |
| **Expected** | Snapshot import reads `emergency_consensus_active` and `emergency_consensus_start_block` from DGP (forward-compatible). Node resumes in emergency mode. Produces emergency blocks. Standard exit condition applies. |
| **Components** | Snapshot import (forward-compatible fields), emergency state persistence |

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
| **Expected** | Emergency never activates (LIB advances every few seconds). Three-state safety: healthy mode enforces safe defaults regardless of `enable-stale-production` config. Vote-weighted comparison active but functionally equivalent to longest-chain (same witnesses on both sides of any micro-fork). |
| **Components** | No-regression, three-state safety (healthy mode), vote-weighted comparison |

### T12: `enable-stale-production` Ignored in Healthy Mode

| | |
|---|---|
| **Precondition** | HF12 active. All witnesses online. Operator has `enable-stale-production = true` (forgot to revert from pre-HF12). |
| **Action** | Network partition isolates this witness. |
| **Expected** | Participation rate ≥33% → healthy mode → `enable-stale-production` is **ignored**. Witness stops producing when it detects it's isolated (no recent blocks). **This is the core micro-fork prevention feature.** |
| **Components** | Three-state safety (healthy mode auto-enforces safe defaults) |
