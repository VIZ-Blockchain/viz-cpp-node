# Research: Consensus Emergency Recovery & Micro-Fork Prevention

## Status: Research + Proposal

## Problem Statement

When a significant portion of VIZ witnesses go offline, the network stalls. The current recovery procedure requires operators to manually set `enable-stale-production = true` and `required-participation = 0`, but:

1. **Operators forget to revert** these settings after recovery, causing micro-forks during subsequent network partitions
2. **fork_db depth = 1024 blocks (~51 min)** means if a partitioned witness produces blocks alone for >51 min, fork resolution fails
3. **No automatic detection or recovery** exists — everything depends on manual operator intervention
4. **Witness shutdown after 200 missed blocks (~10 min)** ([config.hpp:32](../../libraries/protocol/include/graphene/protocol/config.hpp#L32)) removes witnesses from the schedule, making recovery even harder
5. **Penalties accumulate** during the stall, further reducing witness `counted_votes` and their scheduling priority

---

## Current Mechanism Analysis

### 1. Block Production Safeguards (witness.cpp)

Two safeguards prevent isolated block production:

| Safeguard | Check | Condition | Effect |
|---|---|---|---|
| Stale check | `_production_enabled` | `get_slot_time(1) >= now` | Stops production if node hasn't received recent blocks |
| Participation check | `prate < _required_witness_participation` | 33% default threshold | Stops production if < 33% of slots are filled |

Source: [witness.cpp:333-339](../../plugins/witness/witness.cpp#L333), [witness.cpp:436-439](../../plugins/witness/witness.cpp#L436)

Both are **bypassed** by emergency settings. There is no time-based automatic re-enablement of these safeguards.

### 2. Witness Shutdown Mechanism (database.cpp:4046-4051)

```cpp
if (head_block_num() - w.last_confirmed_block_num > CHAIN_MAX_WITNESS_MISSED_BLOCKS) {
    w.signing_key = public_key_type();  // Zero key = disabled
    push_virtual_operation(shutdown_witness_operation(w.owner));
}
```

`CHAIN_MAX_WITNESS_MISSED_BLOCKS = 200` (~10 min). After a witness misses 200 blocks, their `signing_key` is set to null, removing them from the schedule. This is **irreversible on-chain** — the witness must submit a new `witness_update_operation` to re-enable their key.

**Problem during stall**: If the network stalls for >10 min, ALL witnesses get shut down. Recovery then requires external intervention (CLI wallet) to re-register all witness keys.

### 3. Penalty Accumulation (database.cpp:4035-4043)

During any gap, missed blocks trigger penalties:

```cpp
w.penalty_percent += consensus.median_props.witness_miss_penalty_percent;  // 1%
// penalty expires after witness_miss_penalty_duration (1 day)
```

During a prolonged stall, penalties accumulate massively. Even after recovery, witnesses are penalized for blocks they couldn't possibly produce — their `counted_votes` drops, affecting schedule position.

### 4. Fork Database Depth (fork_database.hpp:117)

```cpp
uint32_t _max_size = 1024;  // ~51 minutes at 3s intervals
```

The fork_db dynamically resizes to `head - LIB + 1` during normal operation. But during solo production:
- LIB doesn't advance (no 75% witness validation possible)
- fork_db stays at 1024
- After 1024 blocks of solo production, older blocks are pruned
- Reconnection with the main chain becomes impossible without full replay

### 5. Last Irreversible Block (LIB) Advancement

LIB advances via two mechanisms:
1. **`update_last_irreversible_block()`** — uses `nth_element` of `last_supported_block_num` at 75% threshold
2. **Block post-validation** — requires 75% of `num_scheduled_witnesses` to sign off

During a stall or solo production, neither mechanism works. LIB freezes at the last block where 75% of witnesses were participating.

### 6. Witness Scheduling

```cpp
account_name_type database::get_scheduled_witness(uint32_t slot_num) const {
    uint64_t current_aslot = dpo.current_aslot + slot_num;
    return wso.current_shuffled_witnesses[current_aslot % wso.num_scheduled_witnesses];
}
```

The schedule is deterministic — `current_aslot % num_scheduled_witnesses`. With fewer witnesses (after shutdowns), the schedule wraps faster, but only among remaining active witnesses.

### 7. Current Emergency Procedure (Manual)

| Step | Action | Risk |
|---|---|---|
| 1 | Set `enable-stale-production = true` | Bypasses sync check permanently |
| 2 | Set `required-participation = 0` | Removes participation safeguard permanently |
| 3 | Restart node | Node produces blocks even if isolated |
| 4 | Re-register shut-down witnesses via CLI | Manual, error-prone |
| 5 | **FORGET to revert settings** | **Micro-forks on next partition** |

---

## Root Cause Analysis

The fundamental problem is that **safeguards are binary and manual**:

1. `enable-stale-production` is either `true` or `false` — no time-based auto-revert
2. `required-participation` has no automatic adjustment based on network conditions
3. Witness shutdown (`signing_key = null`) is permanent on-chain — no automatic recovery
4. No on-chain concept of "emergency mode" — all recovery is off-chain configuration

### The Micro-Fork Chain of Events

```
Network Stall
    │
    ├─ Witnesses miss blocks → penalties accumulate
    ├─ After 200 missed blocks → signing_key = null (shutdown)
    ├─ Participation rate → 0% → production stops
    │
    ├─ Operator manually sets enable-stale=true, required-participation=0
    ├─ One witness starts producing alone
    ├─ Other witnesses re-register via CLI, rejoin
    ├─ Network recovers, operator FORGETS to revert settings
    │
    ├─ Later: network partition
    ├─ Isolated witness: enable-stale=true → keeps producing
    ├─ Isolated witness: required-participation=0 → never stops
    ├─ Isolated witness: skip_undo_history_check → no LIB gap limit
    │
    ├─ After 51 min (1024 blocks): fork_db prunes old blocks
    ├─ Reconnection: fork resolution FAILS
    └─ Full replay required → extended downtime
```

---

## Proposal: On-Chain Emergency Consensus Mode

### Core Concept

Add a **consensus-level emergency mode** that activates automatically when the network has been unable to produce blocks for a configurable duration. In emergency mode, a single well-known committee key becomes the sole block producer, ensuring chain continuity without requiring operators to disable safety checks.

### Design Principles

1. **Automatic activation** — no manual intervention needed to enter emergency mode
2. **Automatic deactivation** — emergency mode exits when normal witnesses resume
3. **No permanent safety bypass** — `enable-stale-production` and `required-participation` remain at safe values
4. **Deterministic committee key** — all nodes agree on who produces blocks during emergency
5. **Minimal consensus changes** — smallest possible diff to existing objects

---

### Change 1: New Constants (config.hpp)

```cpp
/// Emergency consensus mode: activates when no block has been produced for
/// this many seconds since the last irreversible block.
#define CHAIN_EMERGENCY_CONSENSUS_TIMEOUT_SEC    3600  // 1 hour

/// The witness account name that produces blocks during emergency mode
#define CHAIN_EMERGENCY_WITNESS_ACCOUNT          CHAIN_COMMITTEE_ACCOUNT  // "committee"

/// The public key used to sign blocks during emergency mode
#define CHAIN_EMERGENCY_WITNESS_PUBLIC_KEY_STR   "VIZ75CRHVHPwYiUESy1bgN3KhVFbZCQQRA9jT6TnpzKAmpxMPD6Xv"
#define CHAIN_EMERGENCY_WITNESS_PUBLIC_KEY       (graphene::protocol::public_key_type(CHAIN_EMERGENCY_WITNESS_PUBLIC_KEY_STR))

/// Number of consecutive blocks produced by the emergency witness that
/// triggers automatic exit from emergency mode (witnesses have rejoined).
#define CHAIN_EMERGENCY_EXIT_NORMAL_BLOCKS       21  // 1 full round of 21 witnesses
```

### Change 2: Emergency Mode Flag in dynamic_global_property_object (Breaking)

Add a new field to track emergency state:

```cpp
// In global_property_object.hpp, inside dynamic_global_property_object:
bool emergency_consensus_active = false;

/// Block number at which emergency consensus mode was activated.
/// Zero if never activated. Used to compute duration and for logging.
uint32_t emergency_consensus_start_block = 0;
```

**FC_REFLECT update** (breaking change — requires hardfork):

```cpp
FC_REFLECT((graphene::chain::dynamic_global_property_object),
    // ... existing fields ...
    (emergency_consensus_active)
    (emergency_consensus_start_block)
)
```

### Change 3: Emergency Mode Detection — Consensus-Level (database.cpp)

In `update_global_dynamic_data()`, after the existing missed-blocks logic:

```cpp
// After the existing participation/missed-blocks update logic:

const dynamic_global_property_object &dgp = get_dynamic_global_properties();

if (!dgp.emergency_consensus_active) {
    // Check if we should enter emergency mode:
    // No block has been produced by a normal witness since LIB,
    // and more than CHAIN_EMERGENCY_CONSENSUS_TIMEOUT_SEC has elapsed.
    fc::time_point_sec lib_time = get_block_time(dgp.last_irreversible_block_num);
    uint32_t seconds_since_lib = (b.timestamp - lib_time).to_seconds();

    if (seconds_since_lib >= CHAIN_EMERGENCY_CONSENSUS_TIMEOUT_SEC) {
        // Enter emergency consensus mode
        modify(dgp, [&](dynamic_global_property_object &_dgp) {
            _dgp.emergency_consensus_active = true;
            _dgp.emergency_consensus_start_block = b.block_num();
        });

        // Override witness schedule to single emergency witness
        const witness_schedule_object &wso = get_witness_schedule_object();
        modify(wso, [&](witness_schedule_object &_wso) {
            for (int i = 0; i < _wso.num_scheduled_witnesses; i++) {
                _wso.current_shuffled_witnesses[i] = CHAIN_EMERGENCY_WITNESS_ACCOUNT;
            }
            // Keep num_scheduled_witnesses unchanged to preserve schedule rhythm
        });

        ilog("EMERGENCY CONSENSUS MODE activated at block ${b}. "
             "No blocks for ${sec} seconds since LIB ${lib}. "
             "Emergency witness: ${w}",
             ("b", b.block_num())("sec", seconds_since_lib)
             ("lib", dgp.last_irreversible_block_num)
             ("w", CHAIN_EMERGENCY_WITNESS_ACCOUNT));
    }
}
```

### Change 4: Emergency Block Production — Witness Plugin (witness.cpp)

In `maybe_produce_block()`, add emergency mode awareness:

```cpp
// After the _production_enabled check, add:

auto &db = database();
const auto &dgp = db.get_dynamic_global_properties();

if (dgp.emergency_consensus_active) {
    // In emergency mode, any node with the emergency private key
    // can produce blocks. The schedule only contains the emergency witness.
    //
    // The emergency key must be configured in the node's config:
    //   emergency-private-key = 5JPYE8UhnxDgURG7mFDcSPQaNjwT5VmCrq3L4QQ2sZSQhbTUZkZ
    //
    // This is safe because:
    // 1. The key is only used during emergency mode
    // 2. Any number of nodes can have this key (only one produces per slot)
    // 3. Emergency mode auto-exits when normal witnesses return
}
```

Also add a new program option:

```cpp
// In set_program_options:
("emergency-private-key", bpo::value<vector<string>>()->composing()->multitoken(),
 "WIF PRIVATE KEY for emergency consensus block production")
```

And in `plugin_initialize`, load the emergency key alongside existing witness keys:

```cpp
if (options.count("emergency-private-key")) {
    const std::vector<std::string> keys = options["emergency-private-key"].as<std::vector<std::string>>();
    for (const std::string &wif_key : keys) {
        fc::optional<fc::ecc::private_key> private_key = graphene::utilities::wif_to_key(wif_key);
        FC_ASSERT(private_key.valid(), "unable to parse emergency private key");
        pimpl->_private_keys[private_key->get_public_key()] = *private_key;
    }
}
```

### Change 5: Emergency Witness Object — On-Chain (database.cpp)

When entering emergency mode, ensure the emergency witness account exists in the witness index with the correct signing key:

```cpp
// When entering emergency mode:
const auto &witness_by_name = get_index<witness_index>().indices().get<by_name>();
auto wit_itr = witness_by_name.find(CHAIN_EMERGENCY_WITNESS_ACCOUNT);

if (wit_itr == witness_by_name.end()) {
    // Create emergency witness object if it doesn't exist
    create<witness_object>([&](witness_object &w) {
        w.owner = CHAIN_EMERGENCY_WITNESS_ACCOUNT;
        w.signing_key = CHAIN_EMERGENCY_WITNESS_PUBLIC_KEY;
        w.created = head_block_time();
        w.schedule = witness_object::top;
    });
} else {
    // Ensure existing emergency witness has the correct key
    modify(*wit_itr, [&](witness_object &w) {
        w.signing_key = CHAIN_EMERGENCY_WITNESS_PUBLIC_KEY;
        w.schedule = witness_object::top;
    });
}
```

### Change 6: Emergency Mode Exit — Hybrid Schedule Transition (database.cpp)

**Problem with the naive exit approach**: During full emergency, ALL schedule slots = "committee". Normal witnesses can't produce blocks → `last_confirmed_block_num` never updates → the exit condition `head_block_num() - w.last_confirmed_block_num < CHAIN_MAX_WITNESS_MISSED_BLOCKS` is always FALSE → **emergency never exits**.

**Solution: Hybrid schedule** — instead of all-committee or all-real, the emergency schedule is a **mix**. Normal `update_witness_schedule()` runs, and real witnesses get their slots. Committee fills **only** the remaining slots where no real witness is assigned.

This way:
- Witnesses that are online naturally produce at their assigned slots
- Committee covers slots where witnesses are still offline
- No special re-registration or signaling required
- The system **observes** who is actually producing

**LIB advancement** is the natural exit signal:
- `update_last_irreversible_block()` needs 75% (16 of 21) witnesses with recent `last_supported_block_num`
- 11 witnesses producing = 52% → LIB stays frozen
- 16 witnesses producing = 76% → LIB starts moving → emergency exits

In `update_witness_schedule()`, after recalculating the witness schedule:

```cpp
// After normal schedule update, check emergency mode:
const dynamic_global_property_object &dgp = get_dynamic_global_properties();

if (dgp.emergency_consensus_active && has_hardfork(CHAIN_HARDFORK_12)) {
    const witness_schedule_object &wso = get_witness_schedule_object();

    // HYBRID SCHEDULE: Keep the normal computed schedule, but replace
    // slots for witnesses that appear offline with committee.
    // A witness is considered "potentially offline" if:
    //   - signing_key is null (disabled), OR
    //   - no confirmed block since emergency started
    //     (they haven't produced since we entered emergency)
    //
    // Witnesses that ARE online will produce at their assigned slots.
    // Committee fills only the gaps.

    modify(wso, [&](witness_schedule_object &_wso) {
        uint32_t real_witness_slots = 0;

        for (int i = 0; i < _wso.num_scheduled_witnesses;
             i += CHAIN_BLOCK_WITNESS_REPEAT) {
            const auto &wname = _wso.current_shuffled_witnesses[i];
            if (wname == account_name_type()) {
                // Empty slot → assign committee
                for (int j = 0; j < CHAIN_BLOCK_WITNESS_REPEAT &&
                     (i+j) < _wso.num_scheduled_witnesses; ++j) {
                    _wso.current_shuffled_witnesses[i+j] =
                        CHAIN_EMERGENCY_WITNESS_ACCOUNT;
                }
                continue;
            }

            const auto *w = find_witness(wname);
            bool witness_available = w &&
                w->signing_key != public_key_type() &&
                // Has the witness produced since emergency started?
                // If they have, keep their slot.
                // If not, give them the slot anyway — they might be
                // coming back. If they miss, it's a normal miss.
                // The penalty system handles it.
                true;  // Always give real witnesses their slots

            if (!witness_available) {
                for (int j = 0; j < CHAIN_BLOCK_WITNESS_REPEAT &&
                     (i+j) < _wso.num_scheduled_witnesses; ++j) {
                    _wso.current_shuffled_witnesses[i+j] =
                        CHAIN_EMERGENCY_WITNESS_ACCOUNT;
                }
            } else {
                real_witness_slots++;
            }
        }

        ilog("Emergency hybrid schedule: ${r} real witness slots, "
             "${c} committee slots",
             ("r", real_witness_slots)
             ("c", CHAIN_MAX_WITNESSES - real_witness_slots));
    });

    // EXIT CONDITION: LIB is advancing again.
    // When LIB unfreezes (Change 12), it means 75% of scheduled witnesses
    // are producing consistently. That's the strongest possible signal
    // that the network has recovered.
    //
    // We check: has LIB moved past emergency_consensus_start_block?
    // If yes → enough witnesses are back → exit emergency.
    uint32_t current_lib = dgp.last_irreversible_block_num;
    if (current_lib > dgp.emergency_consensus_start_block) {
        modify(dgp, [&](dynamic_global_property_object &_dgp) {
            _dgp.emergency_consensus_active = false;
        });

        ilog("EMERGENCY CONSENSUS MODE deactivated at block ${b}. "
             "LIB has advanced to ${lib}, past emergency start ${start}.",
             ("b", head_block_num())
             ("lib", current_lib)
             ("start", dgp.emergency_consensus_start_block));
    }
}
```

**How the hybrid schedule works step by step**:

```
Step 1: Emergency activates (hour 0)
  - Penalties reset (Change 7)
  - update_witness_schedule() runs normally: top 11 + 10 support
  - BUT all 21 witnesses are offline → all miss their slots
  - Hybrid: committee fills all 21 slots
  - Result: [committee × 21] — same as before

Step 2: 5 witnesses come back online (hour 1.5)
  - update_witness_schedule() computes: top 11 + 10 support
  - 5 of these are online, 16 are still offline
  - Hybrid: 5 get their real slots, 16 = committee
  - Result: [wit1, wit2, wit3, wit4, wit5, committee × 16]
  - Those 5 produce real blocks → their last_confirmed_block_num updates
  - Committee produces the other 16 slots → chain continues
  - LIB: only 5/21 = 24% producing → LIB stays frozen

Step 3: 16 witnesses back (hour 3)
  - Hybrid: 16 real slots + 5 committee
  - 16/21 = 76% > 75% threshold
  - LIB unfreezes → starts advancing
  - LIB passes emergency_consensus_start_block
  - Emergency mode exits!

Step 4: All 21 witnesses back
  - Normal operation, all committee slots replaced
  - Schedule is 100% real witnesses
```

**Key insight**: The witness doesn't need to "re-register" — their `signing_key` is still set from before the emergency. They just need their node to be running and connected to P2P. The hybrid schedule gives them their slot; if they're online, they produce; if not, committee covers.

### Change 7: Penalty Reset on Emergency Activation

When entering emergency mode, reset all witness penalties and re-enable shut-down witnesses:

```cpp
// When entering emergency mode:
const auto &witness_idx = get_index<witness_index>().indices().get<by_id>();
for (auto wit_itr = witness_idx.begin(); wit_itr != witness_idx.end(); ++wit_itr) {
    modify(*wit_itr, [&](witness_object &w) {
        // Re-enable witness if shut down
        if (w.signing_key == public_key_type()) {
            // The witness must be re-registered with their key via
            // witness_update_operation. We cannot restore the key because
            // we don't know it. However, we can reset penalties so that
            // when they do re-register, they aren't heavily penalized.
        }
        // Reset all penalties
        w.penalty_percent = 0;
        w.counted_votes = w.votes;
        // Reset miss counters
        w.current_run = 0;
    });
}

// Remove all pending penalty expiration objects
const auto &penalty_idx = get_index<witness_penalty_expire_index>().indices().get<by_id>();
auto pen_itr = penalty_idx.begin();
while (pen_itr != penalty_idx.end()) {
    const auto &current = *pen_itr;
    ++pen_itr;
    remove(current);
}
```

### Change 8: Emergency Key Configuration in config.ini

```ini
# WIF PRIVATE KEY for emergency consensus block production.
# This key is only used when the network enters emergency consensus mode
# (no blocks for >1 hour since last irreversible block).
# Multiple nodes can safely have this key — only one produces per slot.
# Private key: 5JPYE8UhnxDgURG7mFDcSPQaNjwT5VmCrq3L4QQ2sZSQhbTUZkZ
# Public key: VIZ75CRHVHPwYiUESy1bgN3KhVFbZCQQRA9jT6TnpzKAmpxMPD6Xv
# emergency-private-key = 5JPYE8UhnxDgURG7mFDcSPQaNjwT5VmCrq3L4QQ2sZSQhbTUZkZ
```

### Change 9: Automatic Safety Enforcement — Remove Human Factor (witness.cpp)

**Problem**: Operators set `enable-stale-production=true` and `required-participation=0` to recover the network, then forget to revert. This is the #1 cause of micro-forks.

**Solution**: After hardfork 12, the witness plugin **automatically enforces safe defaults** when the network is healthy, regardless of what the operator configured. The config values `enable-stale-production` and `required-participation` become irrelevant — the consensus layer decides.

In `maybe_produce_block()`, replace the existing stale/participation checks with:

```cpp
// === AUTOMATIC SAFETY ENFORCEMENT (Hardfork 12+) ===
// Remove the human factor: when the network is healthy, enforce safe
// defaults regardless of operator config. Operators no longer need to
// manage enable-stale-production or required-participation manually.

auto &db = database();
const auto &dgp = db.get_dynamic_global_properties();

if (has_hardfork(CHAIN_HARDFORK_12)) {
    if (!dgp.emergency_consensus_active) {
        // NORMAL MODE: Always enforce safe defaults.
        // - Stale check is ALWAYS active (equivalent to enable-stale-production=false)
        // - Participation threshold is ALWAYS 33% (required-participation=3300)
        //
        // This completely removes the human factor. Even if an operator
        // has enable-stale-production=true in their config, it's ignored.

        if (!_production_enabled) {
            if (db.get_slot_time(1) >= now) {
                _production_enabled = true;
            } else {
                return block_production_condition::not_synced;
            }
        }

        uint32_t prate = db.witness_participation_rate();
        if (prate < 33 * CHAIN_1_PERCENT) {  // Hardcoded 33% — not configurable
            capture("pct", uint32_t(prate / CHAIN_1_PERCENT));
            return block_production_condition::low_participation;
        }
    } else {
        // EMERGENCY MODE: Bypass both checks.
        // The consensus layer has determined that emergency mode is needed.
        // The witness plugin trusts the consensus-level decision and
        // produces blocks without the stale/participation checks.
        // No manual enable-stale-production=true needed.
        _production_enabled = true;
    }
} else {
    // Pre-hardfork 12: use legacy behavior with config-based overrides
    // (existing code for enable-stale-production and required-participation)
    if (!_production_enabled) {
        if (db.get_slot_time(1) >= now) {
            _production_enabled = true;
        } else {
            return block_production_condition::not_synced;
        }
    }
    uint32_t prate = db.witness_participation_rate();
    if (prate < _required_witness_participation) {
        capture("pct", uint32_t(prate / CHAIN_1_PERCENT));
        return block_production_condition::low_participation;
    }
}
```

**Effect**: After hardfork 12, the lifecycle is fully automatic:

| Network State | Stale Check | Participation Check | Manual Config Effect |
|---|---|---|---|
| Normal (21 witnesses healthy) | **Always ON** | **Always 33%** | Ignored |
| Emergency mode (LIB timeout) | **Auto-bypassed** | **Auto-bypassed** | Ignored |
| Pre-hardfork 12 | Config-based | Config-based | Honored (legacy) |

Operators **never** need to touch `enable-stale-production` or `required-participation` again.

### Change 10: Emergency Multi-Producer Collision Resolution (fork_database.cpp)

**Problem**: When 5 nodes have the emergency key, ALL of them produce a block for the same slot simultaneously. This creates 5 competing blocks at the same height with different transactions.

**Root cause analysis**:

1. **Fork collision check is blind during emergency** — [witness.cpp:457](../../plugins/witness/witness.cpp#L457) checks `eb->data.witness != scheduled_witness`, but during emergency ALL blocks have `witness = "committee"`, so the condition is always `false`. The check never triggers.

2. **fork_database uses first-seen-wins** — [fork_database.cpp:77](../../libraries/chain/fork_database.cpp#L77): `if (item->num > _head->num)` means same-height blocks don't replace head. Different nodes see different blocks first → persistent parallel chains.

3. **Timing**: All 5 nodes wake up within milliseconds of slot boundary, check fork_db (empty), produce, broadcast. By the time P2P propagates, all 5 blocks exist.

**Solution: Deterministic hash-based tie-breaking**

When two blocks compete at the same height, compare their `block_id` (hash). The block with the **lower hash wins**. Since `block_id` is deterministic (hash of block content), ALL nodes converge to the same winner regardless of P2P arrival order.

In `fork_database::_push_block()`:

```cpp
_index.insert(item);
if (!_head || item->num > _head->num) {
    _head = item;
}
// CHANGE 10: During emergency mode, deterministic hash tie-breaking.
// When two blocks compete at the same height (multiple emergency
// producers), prefer the lower block_id hash. This ensures all
// nodes converge regardless of P2P arrival order.
// Only applies during emergency — normal mode uses slot-based resolution.
else if (item->num == _head->num && item->id < _head->id &&
         _emergency_consensus_active) {
    _head = item;
}
```

> Note: `_emergency_consensus_active` is a flag passed to fork_database (e.g., via `set_emergency_mode(bool)`) from `database` when emergency mode changes state.

**Why this works**:

```
Slot N: 5 nodes produce blocks B1..B5 at height H
  │
  ├─ Each block has a unique block_id (hash of content)
  ├─ Node A receives: B1, B3, B5, B2, B4 (arrival order varies)
  ├─ Node B receives: B3, B1, B4, B5, B2
  ├─ Node C receives: B2, B5, B1, B3, B4
  │
  ├─ All nodes compare hashes: min(B1.id, B2.id, B3.id, B4.id, B5.id)
  ├─ All nodes converge to the SAME winner (e.g., B2 has lowest hash)
  │
  └─ Result: deterministic convergence within one P2P round

Slot N+1: fork collision check (updated below) detects the winning
  block and prevents redundant production on most nodes.
```

**Transactions from losing blocks** (B1, B3, B4, B5) remain in each node's `_pending_tx` pool and are included in subsequent blocks. No transactions are lost.

### Change 11: Fix Fork Collision Check for Emergency Mode (witness.cpp)

The existing fork collision check at [witness.cpp:447-471](../../plugins/witness/witness.cpp#L447) doesn't detect emergency-mode collisions because it requires `witness != scheduled_witness`. During emergency, all blocks are from "committee".

**Fix**: During emergency mode, ANY existing block at the target height is a competing block:

```cpp
// Check if a competing block already exists in fork_db for this height.
{
    auto existing_blocks = db.get_fork_db().fetch_block_by_number(db.head_block_num() + 1);
    if (existing_blocks.size() > 0) {
        bool has_competing_block = false;

        if (dgp.emergency_consensus_active) {
            // During emergency mode: ANY block at this height is competing.
            // Multiple nodes with the emergency key may have produced.
            // Defer to the deterministic hash-based resolution in fork_db.
            has_competing_block = true;
        } else {
            // Normal mode: only count blocks from different witnesses
            // on different parents as competing (existing logic)
            for (const auto &eb : existing_blocks) {
                if (eb->data.witness != scheduled_witness &&
                    eb->data.previous != db.head_block_id()) {
                    has_competing_block = true;
                    break;
                }
            }
        }

        if (has_competing_block) {
            capture("height", db.head_block_num() + 1)("scheduled_witness", scheduled_witness);
            wlog("Skipping block production at height ${h} due to existing competing block "
                 "in fork database (witness ${w} deferring to allow fork resolution)",
                 ("h", db.head_block_num() + 1)("w", scheduled_witness));
            return block_production_condition::fork_collision;
        }
    }
}
```

**Combined effect of Changes 10 + 11**:

| Slot | What happens | Competing blocks |
|---|---|---|
| 1st emergency slot | All 5 nodes produce simultaneously (unavoidable) | 5 blocks |
| Hash resolution | All nodes converge to lowest-hash block | 1 winner |
| 2nd emergency slot | Fork collision check fires on nodes that received a block | 1-2 blocks |
| 3rd+ emergency slots | Only 1 node produces (others see block in fork_db first) | 1 block |

The collision is a **transient startup artifact** that self-resolves within 1-2 slots (~3-6 seconds).

### Change 12: LIB Freeze During Emergency — Partition Safety (database.cpp)

**Problem: Competing irreversible emergency chains**

If the network splits into 3 partitions (e.g., US, EU, Asia), each partition independently activates emergency mode after 1 hour. Each partition produces its own emergency chain with the committee key. The critical flaw:

`update_last_irreversible_block()` ([database.cpp:4494-4631](../../libraries/chain/database.cpp#L4494)) iterates `wso.current_shuffled_witnesses`, gets each witness's `last_supported_block_num`, and uses `nth_element` at the 75% threshold. During emergency, ALL schedule slots = "committee", so the committee witness's `last_supported_block_num` dominates → **LIB advances on each partition independently**.

This makes each partition's emergency blocks **irreversible**. When partitions reconnect via P2P, they have irreconcilable chains — the exact same problem we're trying to solve.

```
Partition A: blocks 1000→2200 (LIB advanced to 2180) — IRREVERSIBLE
Partition B: blocks 1000→2100 (LIB advanced to 2080) — IRREVERSIBLE
Partition C: blocks 1000→1500 (LIB advanced to 1480) — IRREVERSIBLE
                          ↑
              P2P reconnects — fork resolution IMPOSSIBLE
              All three chains are irreversible past fork point
```

**Solution: Freeze LIB during emergency mode**

Emergency blocks should NEVER become irreversible. They are temporary and must be reversible so that when partitions merge, the losing chain can be unwound.

In `update_last_irreversible_block()`:

```cpp
void database::update_last_irreversible_block(uint32_t skip) {
    try {
        const dynamic_global_property_object &dpo = get_dynamic_global_properties();

        // === CHANGE 12: EMERGENCY LIB COMPUTATION ===
        // During emergency mode, compute LIB using ONLY real witnesses
        // (exclude committee). This solves two problems:
        //
        // 1. PARTITION SAFETY: If only committee is producing, LIB stays
        //    frozen → all emergency blocks remain reversible → partitions
        //    can merge.
        //
        // 2. GRADUAL RECOVERY: As real witnesses return via the hybrid
        //    schedule (Change 6), their last_supported_block_num updates.
        //    When 75% of real witnesses are producing, LIB advances
        //    naturally → emergency exits.
        //
        // Without this, committee's last_supported_block_num would
        // dominate, advancing LIB on each partition independently.
        if (has_hardfork(CHAIN_HARDFORK_12) && dpo.emergency_consensus_active) {
            const witness_schedule_object &wso = get_witness_schedule_object();

            // Collect ONLY real (non-committee) witnesses from schedule
            vector<const witness_object *> real_wit_objs;
            for (int i = 0; i < wso.num_scheduled_witnesses;
                 i += CHAIN_BLOCK_WITNESS_REPEAT) {
                const auto &wname = wso.current_shuffled_witnesses[i];
                if (wname != CHAIN_EMERGENCY_WITNESS_ACCOUNT &&
                    wname != account_name_type()) {
                    real_wit_objs.push_back(
                        &get_witness(wname));
                }
            }

            if (real_wit_objs.empty()) {
                // All committee — LIB stays frozen
                // Expand fork_db to accommodate emergency blocks
                uint32_t emergency_fork_db_size = std::min(
                    dpo.head_block_number -
                        dpo.last_irreversible_block_num + 1,
                    uint32_t(CHAIN_MAX_UNDO_HISTORY));
                _fork_db.set_max_size(emergency_fork_db_size);
                return;
            }

            // Compute LIB using real witnesses only.
            // Threshold: 75% of REAL witnesses (not total schedule).
            // E.g., 16 real witnesses in schedule → need 12 (75% of 16)
            // producing consistently for LIB to advance.
            size_t offset =
                ((CHAIN_100_PERCENT - CHAIN_IRREVERSIBLE_THRESHOLD) *
                 real_wit_objs.size() / CHAIN_100_PERCENT);

            std::nth_element(
                real_wit_objs.begin(),
                real_wit_objs.begin() + offset,
                real_wit_objs.end(),
                [](const witness_object *a, const witness_object *b) {
                    return a->last_supported_block_num <
                           b->last_supported_block_num;
                });

            uint32_t new_lib =
                real_wit_objs[offset]->last_supported_block_num;

            if (new_lib > dpo.last_irreversible_block_num) {
                // Real witnesses have advanced LIB!
                // Apply the new LIB and continue with normal commit logic.
                ilog("Emergency LIB advance: ${old} → ${new} "
                     "(${n} real witnesses producing)",
                     ("old", dpo.last_irreversible_block_num)
                     ("new", new_lib)
                     ("n", real_wit_objs.size()));
                // ... fall through to normal LIB commit logic with new_lib ...
            } else {
                // Not enough real witnesses yet — keep LIB frozen
                uint32_t emergency_fork_db_size = std::min(
                    dpo.head_block_number -
                        dpo.last_irreversible_block_num + 1,
                    uint32_t(CHAIN_MAX_UNDO_HISTORY));
                _fork_db.set_max_size(emergency_fork_db_size);
                return;
            }
        }

        // ... existing LIB advancement logic ...
```

**How LIB recovery works with the hybrid schedule**:

```
Hour 0: Emergency activates
  Schedule: [committee × 21]
  Real witnesses in LIB calc: 0
  LIB: FROZEN (no real witnesses)

Hour 1.5: 5 witnesses return
  Schedule: [wit1..wit5, committee × 16]
  Real witnesses: 5, threshold 75% of 5 = 4
  wit1..wit5 producing → last_supported_block_num updates
  4+ witnesses meet threshold → LIB advances for the first time!
  But LIB may not yet pass emergency_consensus_start_block...

Hour 2: 16 witnesses return
  Schedule: [wit1..wit16, committee × 5]
  Real witnesses: 16, threshold 75% of 16 = 12
  LIB advancing steadily
  LIB passes emergency_consensus_start_block → EMERGENCY EXITS

Hour 2+: Normal operation
  Schedule: [wit1..wit21]
  Standard LIB computation
```

Also freeze LIB in `apply_block_post_validation()` ([database.cpp:4260](../../libraries/chain/database.cpp#L4260)):

```cpp
void database::apply_block_post_validation(block_id_type block_id,
                                           const account_name_type &witness_account) {
    try {
        const dynamic_global_property_object &dpo = get_dynamic_global_properties();

        // Don't advance LIB via post-validation during emergency mode
        if (has_hardfork(CHAIN_HARDFORK_12) && dpo.emergency_consensus_active) {
            return;
        }
        // ... existing post-validation logic ...
```

**How partition merge works with frozen LIB**:

**CRITICAL PROBLEM: Different LIBs at partition time**

The original diagram assumed all partitions have the same LIB. In reality, partitions have **different LIBs** because one partition may have had enough witnesses (75%) to advance LIB further before stalling:

```
Partition A:  LIB=1000  emergency blocks [1001..2200] (1200 reversible)
Partition B:  LIB=1010  real blocks [1001..1010] committed + emergency [1011..2100]
Partition C:  LIB=1100  real blocks [1001..1100] committed + emergency [1101..1500]
```

The fork switching logic ([database.cpp:1096](../../libraries/chain/database.cpp#L1096)) uses **longest chain wins**: `new_head->data.block_num() > head_block_num()`. This creates a deadlock:

```
A receives C's chain:
  C's total from fork point (1000): 500 blocks
  A's total from fork point (1000): 1200 blocks
  A is LONGER → WON'T SWITCH
  (even though C has 100 REAL consensus blocks!)

C receives A's chain:
  Fork point: block 1000
  C's LIB = 1100 → can't pop past 1100 (undo data committed)
  A's chain diverges at 1001 (before C's LIB)
  C CANNOT SWITCH → undo data for blocks 1001-1100 is gone

RESULT: DEADLOCK — both stay on their own chain
```

**Root cause**: Emergency blocks (meaningless filler from committee key) have **equal weight** to real-witness blocks (confirmed by 75% of actual witnesses) in the chain comparison.

### Change 14: Vote-Weighted Chain Comparison (database.cpp)

**Problem with simple block counting**:

Counting "real" (non-emergency) blocks is insufficient. Consider:

```
Partition A: 11 top witnesses (big votes, e.g., 100K each)
  → Only 11 witnesses, below 75% threshold
  → LIB stalls, enters emergency eventually
  → 11 real blocks per round + emergency blocks

Partition B: 30 support witnesses (small votes, e.g., 1K each)
  → Enough witnesses to keep producing (>21)
  → Penalty system kicks in: missing top witnesses get penalized
  → Support witnesses promoted to top 21
  → NO emergency mode → produces a normal longer chain
  → 30 real blocks per round

Block count comparison: B(30) > A(11) → B wins ✘
But A has 11×100K = 1.1M total stake, B has 30×1K = 30K total stake
A represents FAR more consensus!
```

Also vulnerable to **Sybil attack**: one operator registers 10 support witnesses with tiny votes. During a partition, these 10 produce "real" blocks cheaply, inflating the block count.

**Rule: Use cumulative `votes` (raw, unpenalized stake weight) of unique witnesses per branch.**

Key design decisions:
- Use `votes` NOT `counted_votes` — because `counted_votes` is affected by penalties accumulated *during* the partition (a partition artifact, not real consensus signal)
- Count each **unique witness once** — prevents a single witness from inflating weight by producing many blocks
- Emergency blocks (`committee` witness) have `votes = 0` — naturally lowest priority

In `push_block()` ([database.cpp:1089-1161](../../libraries/chain/database.cpp#L1089)):

```cpp
if (!(skip & skip_fork_db)) {
    shared_ptr<fork_item> new_head = _fork_db.push_block(new_block);
    _maybe_warn_multiple_production(new_head->num);

    if (new_head->data.previous != head_block_id()) {
        bool should_switch = false;

        if (new_head->data.block_num() > head_block_num()) {
            should_switch = true;
        }

        // === CHANGE 14: VOTE-WEIGHTED CHAIN COMPARISON ===
        // Use the sum of unique witness votes as the primary fork
        // resolution criterion. Applies UNIVERSALLY (not just emergency).
        //
        // This handles:
        // 1. Emergency vs real: committee has 0 votes → always loses
        // 2. Top vs support witnesses: top have more votes → their chain wins
        // 3. Sybil attack: 10 fake witnesses with tiny votes can't outweigh
        //    real top witnesses
        // 4. Penalty manipulation: uses raw `votes` not `counted_votes`,
        //    so partition-induced penalties don't affect comparison
        if (has_hardfork(CHAIN_HARDFORK_12)) {
            auto branches = _fork_db.fetch_branch_from(
                new_head->data.id(), head_block_id());

            // Compute vote weight for each branch:
            // Sum raw `votes` of UNIQUE witnesses (each counted once)
            auto compute_branch_weight = [&](const branch_type &branch)
                -> share_type
            {
                flat_set<account_name_type> seen_witnesses;
                share_type total_weight = 0;

                for (const auto &item : branch) {
                    const auto &witness_name = item->data.witness;

                    // Skip emergency witness (votes = 0 anyway)
                    if (witness_name == CHAIN_EMERGENCY_WITNESS_ACCOUNT)
                        continue;

                    // Count each witness only once
                    if (seen_witnesses.count(witness_name))
                        continue;
                    seen_witnesses.insert(witness_name);

                    // Use raw `votes` — not `counted_votes` —
                    // to ignore partition-induced penalties
                    const auto *w = find_witness(witness_name);
                    if (w) {
                        total_weight += w->votes;
                    }
                }
                return total_weight;
            };

            share_type new_weight = compute_branch_weight(branches.first);
            share_type old_weight = compute_branch_weight(branches.second);

            if (new_weight != old_weight) {
                // Primary: prefer chain with higher vote weight
                should_switch = (new_weight > old_weight);
            } else {
                // Secondary: same vote weight → prefer longer chain
                should_switch =
                    (new_head->data.block_num() > head_block_num());
                // Tertiary: same length → hash tie-breaking (Change 10)
            }

            ilog("Vote-weighted fork comparison: "
                 "new_weight=${nw} (${nc} witnesses) vs "
                 "old_weight=${ow} (${oc} witnesses), "
                 "switch=${s}",
                 ("nw", new_weight)("nc", branches.first.size())
                 ("ow", old_weight)("oc", branches.second.size())
                 ("s", should_switch));
        }

        if (should_switch) {
            // ... existing fork switching logic (pop + push) ...
```

**How this resolves ALL partition scenarios**:

**Scenario 1: Emergency vs real (different LIBs)**
```
Partition A (LIB=1000): committee blocks only
  unique witnesses: {committee} → votes = 0

Partition C (LIB=1100): 11 top witnesses + committee
  unique witnesses: {wit1..wit11} → votes = 11 × 100K = 1.1M

Vote weight: C(1.1M) >> A(0) → C wins ✓
```

**Scenario 2: Top witnesses vs support witnesses (no emergency on B)**
```
Partition A: 11 top witnesses (100K votes each), enters emergency
  unique witnesses: {top1..top11} → votes = 1.1M

Partition B: 30 support witnesses (1K votes each), NO emergency
  unique witnesses: {sup1..sup30} → votes = 30K
  (penalty system promoted them to top, but raw votes still small)

Vote weight: A(1.1M) >> B(30K) → A wins ✓
Even though B has more blocks and no emergency mode!
```

**Scenario 3: Sybil attack (1 operator, 10 witnesses)**
```
Partition A: 11 top witnesses (100K votes each)
  unique witnesses: {top1..top11} → votes = 1.1M

Partition B: 10 sybil witnesses (500 votes each) + 5 support (2K each)
  unique witnesses: {sybil1..sybil10, sup1..sup5} → votes = 15K

Vote weight: A(1.1M) >> B(15K) → A wins ✓
Sybil can't manufacture vote weight!
```

**Scenario 4: Both partitions have similar vote weight**
```
Partition A: 11 top witnesses (100K votes each) → 1.1M
Partition B: 10 top witnesses (100K each) + 5 support (20K each) → 1.1M

Vote weight: A(1.1M) ≈ B(1.1M) → fall through to chain length
Longer chain wins → fall through to hash tie-breaking
```

**Why `votes` not `counted_votes`**:
```
Partition B has 30 support witnesses. After partition:
  - Missing top witnesses get penalized (penalty_percent increases)
  - Their counted_votes drops
  - Support witnesses' counted_votes relatively increases
  - This is a PARTITION ARTIFACT, not real consensus

Using raw `votes` ignores partition-induced penalties:
  - top1.votes = 100K (unchanged regardless of penalties)
  - sup1.votes = 1K (unchanged regardless of promotions)
  - The comparison reflects REAL stake support, not temporary schedule
```

---

**Updated partition merge diagram**:

```
Before merge (3 partitions with DIFFERENT LIBs):

Partition A:  LIB=1000  head=2200  [0 vote weight + emergency blocks]
Partition B:  LIB=1010  head=2100  [30K vote weight (support witnesses)]
Partition C:  LIB=1100  head=1500  [1.1M vote weight (top witnesses)]

P2P reconnects:
  ├─ Nodes exchange blocks via P2P
  ├─ Vote-weighted comparison (Change 14):
  │   Primary criterion: sum of unique witness votes
  │   C(1.1M) >> B(30K) >> A(0)
  ├─ All nodes converge to Partition C's chain
  ├─ A unwinds emergency blocks (all reversible, LIB=1000)
  ├─ B unwinds its chain past LIB=1010
  ├─ Transactions from A and B return to pending pool
  │
  ├─ Normal witnesses reconnect and re-register
  ├─ Emergency mode exits (Change 6)
  ├─ LIB unfreezes and starts advancing normally
  └─ fork_db shrinks back to normal size
```

**Emergency duration limit**: With `CHAIN_MAX_UNDO_HISTORY = 10000` blocks and 3-second intervals, emergency mode can sustain ~8.3 hours of frozen LIB before fork_db overflow. If emergency lasts longer, the oldest reversible blocks get pruned and fork resolution scope shrinks. This is acceptable — if the network hasn't recovered after 8+ hours, manual intervention is expected.

### Change 15: P2P Anti-Spam for Rejected Forks (node.cpp)

**Problem**: After vote-weighted comparison rejects a peer's fork, the peer continues sending blocks from their losing chain. This wastes bandwidth and processing time, especially after partition merges when many nodes may still be on the old fork.

**Current behavior** ([node.cpp:3253-3271](../../libraries/network/node.cpp#L3253)):
- `block_older_than_undo_history` → `peer->inhibit_fetching_sync_blocks = true`
- Invalid block → full `disconnect_from_peer()`

**New behavior**: Add a "soft ban" for peers whose fork loses vote-weighted comparison. Instead of full disconnect, temporarily stop processing blocks from that peer for 1 hour. This allows them to eventually switch to the correct chain and reconnect.

```cpp
// In send_sync_block_to_node_delegate() and process_block_message():
// When push_block() rejects a block due to vote-weighted comparison:
catch (const fork_rejected_by_vote_weight &e) {
    // Peer is on a losing fork. Don't disconnect fully —
    // they might switch to the correct chain soon.
    // Instead, temporarily ignore their blocks.
    fc_wlog(fc::logger::get("sync"),
            "Peer ${peer} sent block from fork rejected by "
            "vote-weighted comparison. Ignoring for 1 hour.",
            ("peer", originating_peer->get_remote_endpoint()));

    originating_peer->fork_rejected_until =
        fc::time_point::now() + fc::seconds(3600);
    originating_peer->inhibit_fetching_sync_blocks = true;
}
```

In `peer_connection`, add:
```cpp
fc::time_point fork_rejected_until;  // soft ban expiry
```

In `process_block_message()` and `send_sync_block_to_node_delegate()`, check:
```cpp
if (originating_peer->fork_rejected_until > fc::time_point::now()) {
    // Still soft-banned — silently discard block
    return;
}
```

**Also increase standard `fork_db` max size**:
```cpp
// In fork_database.hpp:
static const uint32_t MAX_BLOCK_REORDERING = 2400;  // was 1024
// ~2 hours at 3s/block, enough for vote-weighted resolution
```

This gives the universal vote-weighted comparison enough room to operate in normal mode without emergency fork_db expansion.

### Change 13: Keep `enable-stale-production` / `required-participation` as Manual Overrides

Update to Change 9: the config options are NOT deprecated. They remain available as **manual overrides** for operators who want to intervene faster than the 1-hour emergency timeout.

The auto-enforcement logic (Change 9) applies only when:
- Hardfork 12 is active AND
- The network is healthy (participation ≥ 33%, emergency mode not active)

When the network is in distress but hasn't yet hit the 1-hour emergency threshold, operators can still manually set `enable-stale-production=true` and `required-participation=0` to accelerate recovery. The difference from the current system: once the network is healthy again, the witness plugin **automatically reverts** to safe defaults, regardless of config.

```cpp
// Updated Change 9 logic:
if (has_hardfork(CHAIN_HARDFORK_12)) {
    if (dgp.emergency_consensus_active) {
        // EMERGENCY MODE: auto-bypass (no manual config needed)
        _production_enabled = true;
    } else {
        uint32_t prate = db.witness_participation_rate();
        if (prate >= 33 * CHAIN_1_PERCENT) {
            // HEALTHY NETWORK: enforce safe defaults automatically
            // Even if operator has enable-stale-production=true in config,
            // it's overridden because the network doesn't need it.
            if (!_production_enabled) {
                if (db.get_slot_time(1) >= now) {
                    _production_enabled = true;
                } else {
                    return block_production_condition::not_synced;
                }
            }
            if (prate < 33 * CHAIN_1_PERCENT) {
                capture("pct", uint32_t(prate / CHAIN_1_PERCENT));
                return block_production_condition::low_participation;
            }
        } else {
            // DISTRESSED NETWORK (participation < 33%, not yet emergency):
            // Honor manual config overrides — operator may be trying to
            // accelerate recovery before the 1-hour timeout.
            // Fall through to legacy enable-stale-production /
            // required-participation config-based behavior.
            if (!_production_enabled) {
                if (_production_skip_flags & database::skip_undo_history_check) {
                    // enable-stale-production=true → skip sync check
                    _production_enabled = true;
                } else if (db.get_slot_time(1) >= now) {
                    _production_enabled = true;
                } else {
                    return block_production_condition::not_synced;
                }
            }
            if (prate < _required_witness_participation) {
                capture("pct", uint32_t(prate / CHAIN_1_PERCENT));
                return block_production_condition::low_participation;
            }
        }
    }
}
```

**Three-state behavior after HF12**:

| Network State | Participation | `enable-stale-production` | `required-participation` |
|---|---|---|---|
| Healthy | ≥ 33% | **Ignored** (always safe) | **Ignored** (always 33%) |
| Distressed (pre-emergency) | < 33% | **Honored** (manual override) | **Honored** (manual override) |
| Emergency (auto, 1hr+ since LIB) | N/A | **Auto-bypassed** | **Auto-bypassed** |

This preserves operator agency during the gap between "network struggling" and "full emergency activation", while still auto-reverting when the network recovers.

---

## Emergency Mode Lifecycle

### Activation

```
Normal Operation
    │
    ├─ Witnesses start going offline
    ├─ Missed blocks accumulate
    ├─ Participation rate drops below 33%
    ├─ Block production stops
    ├─ Witnesses get shut down (signing_key = null after 200 missed blocks)
    │
    ├─ 1 hour passes since last irreversible block
    │
    └─ EMERGENCY MODE ACTIVATES
        ├─ emergency_consensus_active = true
        ├─ Witness schedule overridden: all slots → "committee"
        ├─ All penalties reset to 0
        ├─ All penalty expiration objects removed
        ├─ Nodes with emergency-private-key start producing blocks
        └─ LIB frozen (Change 12) — all emergency blocks are reversible
```

### Emergency Operation

```
Emergency Mode Active
    │
    ├─ Only "committee" witness produces blocks
    ├─ Blocks signed with CHAIN_EMERGENCY_WITNESS_PUBLIC_KEY
    ├─ Any node with the emergency key can produce (only one per slot)
    ├─ LIB is FROZEN (Change 12) — all emergency blocks are reversible
    ├─ fork_db expanded to CHAIN_MAX_UNDO_HISTORY (10000 blocks ≈ 8.3 hrs)
    ├─ Multi-producer collisions resolved by hash tie-breaking (Change 10)
    │
    ├─ If network is partitioned:
    │   ├─ Each partition produces its own emergency chain
    │   ├─ All chains remain reversible (LIB frozen)
    │   └─ On reconnection: longest chain wins, losing chains unwind
    │
    ├─ Operators re-register their witnesses via CLI:
    │   witness_update "account" "url" {"signing_key": "VIZ..."} true
    │
    ├─ Witnesses rejoin and start confirming blocks
    └─ update_witness_schedule() detects normal witnesses
```

### Deactivation

```
Normal Witnesses Rejoining
    │
    ├─ update_witness_schedule() runs
    ├─ Detects non-emergency witnesses with valid signing keys
    ├─ Emergency mode deactivates:
    │   ├─ emergency_consensus_active = false
    │   └─ Normal witness schedule restored
    │
    └─ Normal Operation Resumes
        ├─ No need to change enable-stale-production
        ├─ No need to change required-participation
        └─ No micro-fork risk from forgotten emergency settings
```

---

## Key Advantages Over Current Approach

| Problem | Current Approach | Emergency Consensus Mode |
|---|---|---|
| Network stalls | Manual `enable-stale-production=true` | Automatic after 1 hour timeout |
| Low participation | Manual `required-participation=0` | Schedule override (all committee) |
| Safety bypass forgotten | Operators forget to revert → micro-forks | Auto-exits when witnesses return |
| Witness shutdown | Manual re-registration via CLI | Penalty reset; still need re-registration |
| Fork_db overflow | Solo production exceeds 1024 blocks | fork_db expanded to 10000 during emergency |
| Multiple producers | Any witness can fork | Single deterministic producer |
| Forgotten settings | enable-stale=true stays forever → micro-forks | Config ignored after HF12 (Change 9) |
| Multi-node emergency | N/A | Hash tie-breaking + collision check + vote-weighted fork comparison (Changes 10+11+14) |

---

## Security Analysis

### Emergency Private Key Security

The emergency private key (`5JPYE8UhnxDgURG7mFDcSPQaNjwT5VmCrq3L4QQ2sZSQhbTUZkZ`) is shared among multiple nodes. This is safe because:

1. **Only used during emergency** — the key is meaningless during normal operation
2. **Deterministic schedule** — only one node produces per slot (based on timing)
3. **Multiple nodes prevent single point of failure** — if one emergency node goes down, another takes the next slot
4. **Short duration** — emergency mode should last minutes to hours, not days
5. **Blocks are validated** — other nodes still validate blocks against consensus rules
6. **Auto-exit** — the more nodes have the key, the faster production resumes, and the faster normal witnesses can rejoin

### Multi-Producer Collision Safety

When N nodes have the emergency key, the first emergency slot produces N competing blocks. This is handled by a **three-layer defense**:

1. **Deterministic hash tie-breaking** (Change 10) — `fork_database::_push_block()` compares `block_id` hashes at the same height: lower hash wins. ALL nodes converge to the same block regardless of P2P arrival order. No transactions are lost — losing blocks' txs stay in `_pending_tx`.

2. **Emergency-aware fork collision check** (Change 11) — after the first slot, nodes that received any block at the target height skip production entirely, deferring to the hash-based winner.

3. **Self-resolving** — collision only affects the 1st emergency slot (5 blocks), drops to 1-2 blocks by slot 2, and reaches 1 block by slot 3+. Total extra overhead: ~5 blocks in the first 3-6 seconds.

### Automatic Safety Enforcement

After hardfork 12, `enable-stale-production` and `required-participation` config options are **context-dependent** (Change 9 + Change 13):
- **Healthy network** (≥ 33% participation): config ignored, safe defaults enforced automatically
- **Distressed network** (< 33%, not yet emergency): config honored as manual overrides
- **Emergency mode**: auto-bypassed, no config needed

This eliminates the most common cause of micro-forks (forgotten emergency settings) while preserving operator agency during the pre-emergency window.

### Attack Vectors

| Attack | Mitigation |
|---|---|
| Malicious actor with emergency key produces invalid blocks | Consensus validation still applies; invalid blocks are rejected |
| Emergency mode activated during normal operation (time attack) | Only activates if LIB timestamp is >1 hour old; during normal ops, LIB advances every few seconds |
| Emergency key holder censors transactions | Emergency mode is temporary; all transactions are public and will be included when normal witnesses return |
| Emergency mode never exits | Exits automatically when any normal witness with valid key produces a block |
| 5 nodes produce competing emergency blocks | Hash-based tie-breaking ensures deterministic convergence within 1 P2P round; fork collision check reduces to 1 producer by slot 3 |
| Operator forgets to revert enable-stale-production | After HF12, config is ignored in healthy mode — consensus layer controls bypass automatically |
| Partitions with different LIBs enter emergency independently | Vote-weighted chain comparison (Change 14) sums unique witness `votes` per branch; chain with more stake-weighted consensus wins regardless of length |

---

## Implementation Plan

### Phase 1: Non-Breaking Changes (No Hardfork Required)

These can be deployed immediately without network-wide upgrade:

| Change | File | Description |
|---|---|---|
| Emergency private key option | `witness.cpp` | Add `emergency-private-key` config option |
| Emergency key loaded into key map | `witness.cpp` | Load emergency key alongside witness keys |
| Emergency mode logging | `witness.cpp` | Log when emergency witness is scheduled |

### Phase 2: Hardfork 12 (Requires Network-Wide Upgrade)

| Change | Breaking | Files Modified |
|---|---|---|
| `emergency_consensus_active` field in DGP | Yes (serialized object) | `global_property_object.hpp`, `database.cpp`, `snapshot/plugin.cpp` |
| `emergency_consensus_start_block` field in DGP | Yes (serialized object) | `global_property_object.hpp`, `database.cpp`, `snapshot/plugin.cpp` |
| Emergency mode activation logic | Yes (consensus behavior) | `database.cpp` `update_global_dynamic_data()` |
| Emergency mode exit logic | Yes (consensus behavior) | `database.cpp` `update_witness_schedule()` |
| Schedule override during emergency | Yes (consensus behavior) | `database.cpp` `update_witness_schedule()` |
| Penalty reset on emergency activation | Yes (consensus behavior) | `database.cpp` |
| Emergency witness object creation | Yes (consensus behavior) | `database.cpp` |
| FC_REFLECT update for DGP | Yes (serialization) | `global_property_object.hpp` |
| Snapshot export/import update | Yes (new fields) | `plugins/snapshot/plugin.cpp` |
| config.ini template update | No | `share/vizd/config/` |
| Auto safety enforcement in witness plugin | Yes (consensus behavior) | `witness.cpp` `maybe_produce_block()` |
| Deterministic hash tie-breaking in fork_db | Yes (fork resolution) | `fork_database.cpp` `_push_block()` |
| LIB freeze + fork_db expansion during emergency | Yes (consensus behavior) | `database.cpp` `update_last_irreversible_block()`, `apply_block_post_validation()` |
| Manual override preservation (enable-stale/required-participation) | No (witness plugin) | `witness.cpp` |
| Emergency-aware chain comparison | Yes (fork resolution) | `database.cpp` `push_block()` — vote-weighted using `witness.votes` (universal) |
| P2P anti-spam for rejected forks | No (P2P layer) | `node.cpp` — soft-ban peers on losing forks for 1 hour |
| fork_db standard size increase to 2400 | Yes (fork resolution) | `fork_database.hpp` `MAX_BLOCK_REORDERING = 2400` |

### Hardfork 12 Definition

**File**: `libraries/chain/hardfork.d/12.hf`

```cpp
// 12 Hardfork — Emergency Consensus Recovery
#ifndef CHAIN_HARDFORK_12
#define CHAIN_HARDFORK_12 12
#define CHAIN_HARDFORK_12_TIME <TBD> // To be determined by witness vote
#define CHAIN_HARDFORK_12_VERSION hardfork_version( version(3, 1, 0) )
#endif
```

All emergency consensus logic should be gated behind:

```cpp
if (has_hardfork(CHAIN_HARDFORK_12)) {
    // Emergency consensus mode logic
}
```

---

## Alternative Approaches Considered

### Alternative A: Time-Based Auto-Revert of Emergency Settings

Instead of on-chain emergency mode, add a timeout to `enable-stale-production` and `required-participation`:

```ini
enable-stale-production = true
enable-stale-production-timeout = 3600  # Auto-revert after 1 hour
required-participation = 0
required-participation-timeout = 3600   # Auto-revert after 1 hour
```

**Rejected because**:
- Still requires manual activation (operators must set emergency values)
- Auto-revert timing is hard to get right — too short and recovery fails, too long and micro-forks happen
- Doesn't address witness shutdown/penalty issues
- fork_db overflow still possible if revert happens while isolated

### Alternative B: Increased fork_db Size

Increase `_max_size` from 1024 to a larger value (e.g., 10000 blocks ≈ 8.3 hours).

**Rejected because**:
- Only delays the problem, doesn't solve it
- Increases memory usage significantly
- Still doesn't prevent micro-forks from happening
- Doesn't address the root cause (solo production on isolated fork)

### Alternative C: P2P-Level Fork Detection

Add P2P-level fork detection where nodes gossip about fork depth and automatically prefer the chain with more witness participation.

**Rejected because**:
- Complex P2P protocol changes
- Vulnerable to Sybil attacks (fake participation claims)
- Doesn't help if witnesses are genuinely offline
- Doesn't address the need for actual block production during a stall

### Alternative D: BFT-Style Fallback to Committee Multisig

Require M-of-N committee signatures for emergency blocks instead of a single key.

**Rejected because**:
- More complex implementation
- Requires committee members to be online during emergency (defeats the purpose)
- Single-key emergency is simpler and the key is only used during emergencies
- Can be upgraded to multisig later if needed

---

## Test Scenarios

### Scenario 1: Normal Emergency Activation and Recovery

1. Start network with 21 witnesses
2. Shut down 15 witnesses (leaving 6 — below 33% participation)
3. Wait for network to stall
4. Wait 1 hour → emergency mode activates
5. Emergency committee produces blocks
6. Restart 15 witnesses, re-register their keys
7. Emergency mode exits → normal production resumes

**Expected**: No manual intervention beyond restarting witness nodes

### Scenario 2: Network Partition (Micro-Fork Prevention)

1. Normal network operation (no emergency settings active)
2. Network partition isolates 5 witnesses from 16
3. Both sides see participation drop
4. Minority side (5): participation < 33% → production stops
5. Majority side (16): participation still > 33% → continues
6. Partition resolves → minority side syncs from majority

**Expected**: No micro-forks because emergency settings were never manually activated

### Scenario 3: Complete Network Stall + Recovery

1. All 21 witnesses go offline (e.g., datacenter power failure)
2. After 1 hour → emergency mode activates on all nodes with emergency key
3. One node produces blocks with committee key
4. Witnesses come back online one by one
5. Each witness re-registers via CLI
6. After enough witnesses are active → emergency mode exits

**Expected**: Chain continues without full stall; LIB frozen during emergency, unfreezes when witnesses return

### Scenario 4: Network Partition During Emergency

1. All 21 witnesses go offline
2. After 1 hour → emergency mode activates on all partitions independently
3. Network is split into 3 partitions (US, EU, Asia)
4. Each partition produces emergency blocks with committee key
5. LIB is frozen on all partitions (Change 12)
6. After 2 hours, P2P connectivity restores between partitions
7. Longest chain wins, losing partitions unwind
8. Normal witnesses re-register, emergency mode exits

**Expected**: Partition merge succeeds because all emergency blocks are reversible. No manual intervention needed beyond witness re-registration.

---

## Open Questions

1. **Should the emergency timeout be configurable on-chain?** **RESOLVED**: No. Keep it hardcoded at 1 hour (`CHAIN_EMERGENCY_CONSENSUS_TIMEOUT_SEC = 3600`). One hour is human-friendly and predictable. Making it on-chain configurable adds complexity without benefit — the timeout is a safety constant, not a tunable parameter. Emergency mode only applies to nodes running the witness plugin.

2. **Should the emergency witness produce blocks with empty transactions?** **RESOLVED**: Emergency blocks **CAN** be empty but **not forced empty**. Transactions must still be processable because witnesses may need to broadcast `witness_update_operation` to re-register their signing key during the hybrid schedule transition (Change 6). If emergency blocks were forced empty, witnesses couldn't re-activate — creating a deadlock where emergency never exits. In practice, most emergency blocks will be empty (low tx volume during a stall), but the mechanism must not prohibit transactions.

3. ~~**What happens if multiple nodes with the emergency key produce blocks simultaneously?**~~ **RESOLVED** (Changes 10+11): Deterministic hash tie-breaking in `fork_database::_push_block()` ensures all nodes converge to the same block (lowest `block_id` wins). Emergency-aware fork collision check prevents redundant production after the first slot. Multi-producer collision is a transient 1-2 slot artifact.

4. **Should the emergency private key be rotated?** **RESOLVED**: No. There's no reason to rotate. The `committee` system account signs emergency blocks only when emergency mode is active. Nodes without the emergency key simply don't produce during emergency — they still validate and accept emergency blocks from peers. The key has no value outside emergency mode, and rotating it would require a hardfork for zero security benefit.

5. **Should emergency blocks have special extensions?** **RESOLVED**: No. Emergency blocks are already identifiable by `witness = "committee"` (the system account). No additional header extension needed — monitoring tools can simply filter by witness name.

6. **What is the minimum number of normal witnesses required to exit emergency mode?** **RESOLVED** (Change 6): Emergency exits when **LIB advances past `emergency_consensus_start_block`**. LIB advancement requires 75% (16 of 21) witnesses producing consistently (`CHAIN_IRREVERSIBLE_THRESHOLD = 7500`). So effectively: **16 witnesses** must be online and producing. With 11 witnesses = 52% → LIB stays frozen, emergency continues. The hybrid schedule (Change 6) mixes real witnesses alongside committee: witnesses get their normal slots, committee fills gaps. No special re-registration needed — witnesses just need to be running with their pre-existing `signing_key`.

7. **Should the deterministic hash tie-breaking apply only during emergency mode?** **RESOLVED**: Yes, gate it behind `emergency_consensus_active` only. In normal mode, witnesses have assigned slots and forks are resolved through the existing scheduling mechanism. Hash tie-breaking is only needed during emergency when multiple nodes produce with the same committee key at the same slot.

8. **Should `enable-stale-production` and `required-participation` config options be deprecated after HF12?** **RESOLVED** (Change 13): Keep them. They serve as manual overrides during the "distressed" phase (participation < 33% but < 1 hour since LIB). Change 9 auto-reverts to safe defaults when the network is healthy. Three-state behavior: Healthy → ignored, Distressed → honored, Emergency → auto-bypassed.

9. **What happens when partitions with independent emergency chains reconnect?** **RESOLVED** (Changes 12+14): LIB is frozen during emergency mode. All emergency blocks are reversible. When partitions merge, the chain with higher **cumulative witness vote weight** wins (Change 14), not the longest chain. Uses raw `votes` (not `counted_votes`) to ignore partition-induced penalties. Handles top-vs-support witness splits and sybil attacks. Maximum emergency duration before fork_db overflow: ~8.3 hours (`CHAIN_MAX_UNDO_HISTORY = 10000` blocks).

11. **What if one operator controls multiple support witnesses (Sybil)?** **RESOLVED** (Change 14): Vote-weighted comparison uses raw `votes` field. A sybil with 10 support witnesses (500 votes each = 5K total) can't outweigh a partition with even 1 top witness (100K votes). The cost of manufacturing vote weight is proportional to real stake — same security assumption as the entire DPoS model.

12. **Should vote-weighted comparison apply outside of emergency mode?** **RESOLVED**: Yes. Apply universally for all fork resolution (not just emergency). This makes fork resolution stake-aware — forks backed by more stake-weighted witnesses are always preferred. Also increase `fork_db` standard max size from 1024 to 2400 blocks (~2 hours at 3s/block) to give more room for fork resolution. Combined with Change 15 (P2P anti-spam), rejected fork blocks don't flood the network.

13. **How to prevent P2P spam from peers on rejected forks?** **RESOLVED** (Change 15): When vote-weighted comparison rejects a peer's fork, temporarily ignore blocks from that peer for 1 hour. The P2P layer already disconnects peers sending invalid blocks ([node.cpp:3264-3271](../../libraries/network/node.cpp#L3264)). Change 15 extends this: instead of full disconnect, add a "soft ban" timeout so the peer can reconnect later on the correct chain.

---

## Reference: Key Constants

| Constant | Value | Meaning | Source |
|---|---|---|---|
| `CHAIN_BLOCK_INTERVAL` | 3 | Seconds per block | [config.hpp:27](../../libraries/protocol/include/graphene/protocol/config.hpp#L27) |
| `CHAIN_MAX_WITNESSES` | 21 | Maximum scheduled witnesses | [config.hpp:41](../../libraries/protocol/include/graphene/protocol/config.hpp#L41) |
| `CHAIN_MAX_TOP_WITNESSES` | 11 | Top voted witnesses | [config.hpp:39](../../libraries/protocol/include/graphene/protocol/config.hpp#L39) |
| `CHAIN_MAX_SUPPORT_WITNESSES` | 10 | Support witnesses | [config.hpp:40](../../libraries/protocol/include/graphene/protocol/config.hpp#L40) |
| `CHAIN_MAX_WITNESS_MISSED_BLOCKS` | 200 | ~10 min before shutdown | [config.hpp:32](../../libraries/protocol/include/graphene/protocol/config.hpp#L32) |
| `CHAIN_IRREVERSIBLE_THRESHOLD` | 7500 (75%) | LIB advancement threshold | [config.hpp:110](../../libraries/protocol/include/graphene/protocol/config.hpp#L110) |
| `CHAIN_IRREVERSIBLE_SUPPORT_MIN_RUN` | 2 | Blocks before supporting LIB | [config.hpp:112](../../libraries/protocol/include/graphene/protocol/config.hpp#L112) |
| `CHAIN_MAX_UNDO_HISTORY` | 10000 | Max head-LIB gap | [config.hpp:108](../../libraries/protocol/include/graphene/protocol/config.hpp#L108) |
| `fork_db._max_size` | 2400 (was 1024) | Fork database depth (~2 hours) | [fork_database.hpp:117](../../libraries/chain/include/graphene/chain/fork_database.hpp#L117) |
| `CONSENSUS_WITNESS_MISS_PENALTY_PERCENT` | 100 (1%) | Miss penalty per block | [config.hpp:127](../../libraries/protocol/include/graphene/protocol/config.hpp#L127) |
| `CONSENSUS_WITNESS_MISS_PENALTY_DURATION` | 1 day | Penalty expiration | [config.hpp:128](../../libraries/protocol/include/graphene/protocol/config.hpp#L128) |
