# VIZ Blockchain — Consensus Emergency Parameters

Parameters used to restart stuck consensus, their operational mechanics, and the micro-fork risks when operators forget to revert them after the emergency is resolved.

---

## Overview

When the VIZ network stalls — no blocks are produced because too few witnesses are online — operators can activate emergency parameters to unblock production:

| Parameter | Normal Value | Emergency Value | Location |
|---|---|---|---|
| `enable-emergency-mode` | `false` | `true` | chain plugin |
| `enable-stale-production` | `false` | `true` | witness plugin |
| `required-participation` | `3300` (33%) | `0` | witness plugin |
| `fork_db` `_max_size` | `1024` (dynamic) | `1024` (initial) | chain library |

Starting with Hardfork 12, the primary emergency mechanism is `enable-emergency-mode`, which gates the on-chain automatic emergency consensus activation. When enabled and the network stalls for >1 hour, the node activates emergency consensus mode (committee witness fills empty schedule slots, penalties reset, hybrid schedule). The older `enable-stale-production` and `required-participation` overrides are still available for pre-HF12 scenarios or manual recovery.

These parameters are essential for chain recovery, but if left in emergency mode after the network stabilizes, they become the **root cause of micro-forks**: isolated delegates continue producing blocks on their own divergent chain during any subsequent network partition.

---

## Parameter 1: `enable-stale-production`

### Definition

Controls whether the witness node produces blocks when the chain is "stale" — i.e., the node has not received recent blocks and its head block is behind the network.

| Property | Value |
|---|---|
| Type | `bool` |
| Default | `false` |
| Command line | `--enable-stale-production` |
| Config file | `enable-stale-production = true` |
| Source | [witness.cpp:126](../../plugins/witness/witness.cpp#L126) |

### How It Works

When `enable-stale-production = false` (default), the witness plugin starts with `_production_enabled = false`. Before each production attempt, the check at [witness.cpp:333-339](../../plugins/witness/witness.cpp#L333) runs:

```cpp
if (!_production_enabled) {
    if (db.get_slot_time(1) >= now) {
        _production_enabled = true;    // auto-enable once caught up
    } else {
        return block_production_condition::not_synced;
    }
}
```

The node will **not** produce blocks until it receives a block whose timestamp places the next slot in the present or future — confirming it is synchronized with the network. Once caught up, production auto-enables permanently.

When `enable-stale-production = true`, `_production_enabled` is set to `true` immediately at initialization ([witness.cpp:149-151](../../plugins/witness/witness.cpp#L149)), bypassing the sync check entirely.

### Side Effect: `skip_undo_history_check`

Setting `enable-stale-production = true` also activates the `skip_undo_history_check` production flag ([witness.cpp:184](../../plugins/witness/witness.cpp#L184)):

```cpp
if (pimpl->_production_enabled) {
    pimpl->_production_skip_flags |= graphene::chain::database::skip_undo_history_check;
}
```

This bypasses a critical safety assertion in `_apply_block` ([database.cpp:4114-4123](../../libraries/chain/database.cpp#L4114)):

```cpp
if (!(skip & skip_undo_history_check)) {
    CHAIN_ASSERT(
        _dgp.head_block_number - _dgp.last_irreversible_block_num < CHAIN_MAX_UNDO_HISTORY,
        undo_database_exception,
        "The database does not have enough undo history...");
}
```

`CHAIN_MAX_UNDO_HISTORY` = 10000 blocks ([config.hpp:108](../../libraries/protocol/include/graphene/protocol/config.hpp#L108)). Without this check, a node producing blocks alone can accumulate an unlimited gap between head and last irreversible block (LIB), since LIB only advances when enough witnesses sign off via block post-validation.

### Emergency Use Case

When the network has completely stalled (no witnesses producing), setting `enable-stale-production = true` on at least one witness node allows it to start producing blocks from its current head, even if it considers the chain stale. This breaks the deadlock and restarts block production.

### Micro-Fork Risk

**If the operator forgets to revert this to `false` after the network recovers**, any subsequent network partition causes the node to continue producing blocks in isolation:

1. The node loses P2P connectivity to other witnesses
2. No new blocks are received, so `get_slot_time(1) < now` — but since `enable-stale-production = true`, this check is skipped
3. The node keeps producing blocks on its own fork
4. `skip_undo_history_check` means there is **no limit** on how far the head-LIB gap grows
5. When connectivity is restored, the node has a long divergent fork that must be reconciled

---

## Parameter 2: `required-participation`

### Definition

The minimum witness participation rate (in basis points) required for block production. The participation rate measures what fraction of the last 128 block slots were actually filled by witnesses.

| Property | Value |
|---|---|
| Type | `uint32_t` |
| Default | `33 * CHAIN_1_PERCENT` = `3300` (33%) |
| Range | 0–9900 (0%–99%) |
| Command line | `--required-participation <value>` |
| Config file | `required-participation = <value>` |
| Source | [witness.cpp:127](../../plugins/witness/witness.cpp#L127) |

### Internal Representation

The value is stored in **basis points** where `CHAIN_100_PERCENT = 10000` and `CHAIN_1_PERCENT = 100` ([config.hpp:57-58](../../libraries/protocol/include/graphene/protocol/config.hpp#L57)):

- Config value `3300` = 33% participation threshold
- Config value `0` = no participation required (emergency)
- Config value `9900` = 99% participation required (very strict)

### Participation Rate Calculation

The rate is computed in [database.cpp:870-873](../../libraries/chain/database.cpp#L870):

```cpp
uint32_t database::witness_participation_rate() const {
    const dynamic_global_property_object &dpo = get_dynamic_global_properties();
    return uint64_t(CHAIN_100_PERCENT) *
           dpo.recent_slots_filled.popcount() / 128;
}
```

`recent_slots_filled` is a 128-bit bitmask ([global_property_object.hpp:94](../../libraries/chain/include/graphene/chain/global_property_object.hpp#L94)) where each bit represents one of the last 128 block slots. A `1` means the slot was filled by a witness, `0` means it was missed. The rate is the percentage of filled slots.

The bitmask is updated on each block application at [database.cpp:4060-4063](../../libraries/chain/database.cpp#L4060):

```cpp
for (uint32_t i = 0; i < missed_blocks + 1; i++) {
    dgp.participation_count -= dgp.recent_slots_filled.hi & 0x8000000000000000ULL ? 1 : 0;
    dgp.recent_slots_filled = (dgp.recent_slots_filled << 1) + (i == 0 ? 1 : 0);
    dgp.participation_count += (i == 0 ? 1 : 0);
}
```

For each missed block, a `0` is shifted in. For the current block, a `1` is shifted in. This gives a rolling 128-slot window of participation history.

### Production Check

Before producing a block, the witness plugin checks ([witness.cpp:436-439](../../plugins/witness/witness.cpp#L436)):

```cpp
uint32_t prate = db.witness_participation_rate();
if (prate < _required_witness_participation) {
    capture("pct", uint32_t(prate / CHAIN_1_PERCENT));
    return block_production_condition::low_participation;
}
```

If the participation rate is below the threshold, block production is suppressed and the error message logs:
```
Not producing block because node appears to be on a minority fork with only X% witness participation
```

### Emergency Use Case

When the network has stalled, `recent_slots_filled` will be mostly zeros (many missed slots), so the participation rate will be very low — potentially below the 33% default threshold. Even if `enable-stale-production = true`, blocks won't be produced because the participation check fails. Setting `required-participation = 0` bypasses this safety check entirely.

### Micro-Fork Risk

**If the operator forgets to revert this to `3300` (33%) after the network recovers**, the participation check becomes ineffective:

1. A network partition occurs
2. On the minority fork, `recent_slots_filled` decays (more missed slots)
3. With the default 33% threshold, production would stop within ~85 missed slots (when participation drops below 33%)
4. With `required-participation = 0`, the node **never stops producing** regardless of how isolated it is
5. A single witness on a completely isolated node continues producing blocks alone

This is the most dangerous of the three parameters because it removes the **last automatic safeguard** against solo block production on a minority fork.

---

## Parameter 3: `fork_db` `_max_size = 1024`

### Definition

The maximum number of blocks the fork database retains for fork detection and resolution. This is not a user-configurable parameter — it is hardcoded in the chain library.

| Property | Value |
|---|---|
| Type | `uint32_t` |
| Default | `1024` blocks |
| Source | [fork_database.hpp:117](../../libraries/chain/include/graphene/chain/fork_database.hpp#L117) |
| Configurable | No (hardcoded) |

### How It Works

The fork database (`fork_database`) maintains a tree of recently received blocks, allowing the node to detect and switch between competing chain tips. When a new block arrives that extends a different parent than the current head, the node has a fork.

The `_max_size` determines how many blocks are retained in the fork database. Blocks with `block_num < head_block_num - _max_size` are pruned ([fork_database.cpp:105-137](../../libraries/chain/fork_database.cpp#L105)).

At 3-second block intervals, 1024 blocks equals approximately **51 minutes** of chain history.

### Dynamic Resizing

During normal operation, `_max_size` is dynamically resized to match the gap between head and LIB:

```cpp
_fork_db.set_max_size(dpo.head_block_number - dpo.last_irreversible_block_num + 1);
```

This happens in three places:
- [database.cpp:4244-4245](../../libraries/chain/database.cpp#L4244) — after block post-validation chain check
- [database.cpp:4389-4390](../../libraries/chain/database.cpp#L4389) — after block post-validation application
- [database.cpp:4629-4630](../../libraries/chain/database.cpp#L4629) — after `update_last_irreversible_block`

In normal operation with healthy participation, LIB advances within a few blocks of head, so `_max_size` is small (e.g., 10-30 blocks). The initial 1024 is only used until the first LIB advancement after startup.

### Emergency Relevance

When consensus is stuck, the fork database is mostly irrelevant (it contains few blocks). The 1024-block default is sufficient for initial chain restart scenarios.

However, the fork_db becomes critical **after** the emergency, when forgotten parameters cause a delegate to produce blocks alone:

1. **Isolation < 51 minutes**: The fork database can hold both the main chain and the divergent chain blocks. When connectivity is restored, fork resolution works correctly — the longer chain wins.

2. **Isolation > 51 minutes**: The fork database has already pruned older blocks from the divergent chain. When the node reconnects, it cannot fully compare branches. The node must re-sync from the main chain, which involves popping blocks and replaying — a costly operation.

3. **Isolation > CHAIN_MAX_UNDO_HISTORY blocks (~8.3 hours)**: Even with `skip_undo_history_check` bypassed during solo production, the state divergence is enormous. After reconnection, the node may need a full replay from a snapshot or block log.

### Why 1024 Specifically

The value 1024 matches the `MAX_BLOCK_REORDERING` constant ([fork_database.hpp:57](../../libraries/chain/include/graphene/chain/fork_database.hpp#L57)):

```cpp
const static int MAX_BLOCK_REORDERING = 1024;
```

This limits how far back a block can be inserted out-of-order. Blocks more than 1024 positions behind the head are rejected with `unlinkable_block_exception`. The fork_db size and reordering limit are aligned to ensure consistent behavior.

---

## The Combined Micro-Fork Scenario

The three parameters interact to create a specific failure pattern:

```
Timeline:
──────────────────────────────────────────────────────────────────►

1. Network stalls          2. Emergency activated    3. Network recovers
   (no blocks)               (delegate sets:           (delegate forgets
                              enable-stale=true,        to revert settings)
                              required-participation=0)

4. Normal operation        5. Network partition       6. Micro-fork detected
   (with emergency            (delegate loses           (delegate built blocks
    settings still active)     P2P connectivity)         on its own fork)
```

### Step-by-step breakdown

1. **Network stalls**: Insufficient witnesses are online to meet the 33% participation threshold. No blocks are produced.

2. **Emergency activated**: A delegate sets `enable-stale-production = true` and `required-participation = 0` in their config. The node starts producing blocks, restarting the chain.

3. **Network recovers**: Other witnesses come online, see the new blocks, and start participating. The network is healthy again — but the emergency settings are still active in the delegate's config.

4. **Normal operation with emergency settings**: Everything appears fine. The delegate is producing blocks normally. The participation rate is high, so `required-participation = 0` makes no difference. The node is synced, so `enable-stale-production = true` makes no difference.

5. **Network partition occurs**: The delegate's server loses P2P connectivity (ISP issue, DDoS, routing problem, etc.).

6. **Micro-fork**: With the emergency settings still active:
   - `enable-stale-production = true` → The node does not stop producing when it stops receiving blocks
   - `required-participation = 0` → The participation check doesn't stop production even as `recent_slots_filled` decays
   - `skip_undo_history_check` → No limit on head-LIB gap growth
   - The delegate produces blocks on their own fork

7. **Reconnection**: When connectivity is restored:
   - If the isolation lasted < 51 minutes (< 1024 blocks): Fork resolution occurs via fork_db. The longer main chain wins, and the delegate's fork blocks are discarded. Short disruption.
   - If the isolation lasted > 51 minutes but < ~8.3 hours: The fork_db has pruned old blocks. Re-sync required. Moderate disruption.
   - If the isolation lasted > ~8.3 hours: Massive state divergence. Full replay may be needed. Severe disruption.

### Why Normal Settings Prevent This

With normal settings (`enable-stale-production = false`, `required-participation = 3300`):

1. **Network partition occurs**: The node stops receiving blocks from other witnesses.

2. **Participation decays**: `recent_slots_filled` shifts in zeros for each missed slot. After ~85 missed slots (about 4 minutes), participation drops below 33%.

3. **Production stops**: The `low_participation` check suppresses block production. The node logs:
   ```
   Not producing block because node appears to be on a minority fork with only X% witness participation
   ```

4. **Even if participation hasn't decayed yet**: When the node's chain becomes stale (no recent blocks), the `_production_enabled` check would block production if `enable-stale-production = false` had been set and the node had somehow lost its synced state.

The dual safeguard (stale check + participation check) ensures that an isolated delegate stops producing within minutes, keeping any potential fork to at most ~85 blocks (~4 minutes) — well within the fork_db's 1024-block resolution window.

---

## Operational Guidelines

### Activating Emergency Mode

When the network has stalled and block production must be restarted:

1. **Edit config.ini** (or pass command-line flags):
   ```ini
   # Hardfork 12+: enable on-chain emergency consensus (recommended)
   enable-emergency-mode = true

   # Also needed for block production:
   enable-stale-production = true
   required-participation = 0
   ```

2. **Restart the node**:
   ```bash
   vizd --enable-stale-production --required-participation=0
   ```

3. **Monitor production** — confirm blocks are being generated:
   ```
   Generated block #N with timestamp T at time C by W
   ```

4. **Wait for network recovery** — once other witnesses are back online and the participation rate stabilizes above 33%, **immediately revert the settings**.

### Reverting Emergency Mode (Critical Step)

```ini
enable-emergency-mode = false
enable-stale-production = false
required-participation = 3300
```

Then restart the node. **Do not skip this step.** Leaving emergency settings active is the primary cause of micro-forks in practice.

### Checklist for Emergency Activation

| Step | Action | Verification |
|------|--------|-------------|
| 1 | Set `enable-emergency-mode = true` | Node will activate emergency consensus when network stalls |
| 2 | Set `enable-stale-production = true` | Node produces blocks on stale chain |
| 3 | Set `required-participation = 0` | Production continues despite low participation |
| 4 | Monitor participation rate via API | `witness_participation_rate` rises as others rejoin |
| 5 | **When participation > 50%** | Revert all three settings to normal values |
| 6 | Restart node with normal config | Confirm production continues with normal checks active |
| 7 | Verify `low_participation` safeguard works | Node would stop if isolated (test by briefly disconnecting) |

### Red Flags: Emergency Settings Still Active

If you observe any of these log patterns, a witness is likely running with emergency settings still active:

- **Blocks produced during a network partition**: A witness that should have stopped continues generating blocks
- **Participation rate logs showing < 33% but blocks still produced**: `required-participation = 0` is active
- **Head-LIB gap growing beyond 10000 blocks**: `skip_undo_history_check` is active (via `enable-stale-production = true`)
- **Fork collision warnings with rapid reoccurrence**: Isolated witness creates competing blocks at the same heights

---

## Configuration File Examples

### Production Witness (Normal Operation)

```ini
# Normal witness configuration — SAFE
enable-stale-production = false
required-participation = 3300
```

Source: [config_witness.ini:76-80](../../share/vizd/config/config_witness.ini#L76)

### Debug / Testnet (Emergency Mode Acceptable)

```ini
# Testnet/debug configuration — emergency settings acceptable
enable-stale-production = true
required-participation = 0
```

Source: [config_debug.ini:95-99](../../share/vizd/config/config_debug.ini#L95), [config_testnet.ini:99-101](../../share/vizd/config/config_testnet.ini#L99)

### Emergency Recovery (Temporary)

```ini
# EMERGENCY ONLY — revert immediately after network recovers!
enable-emergency-mode = true
enable-stale-production = true
required-participation = 0
```

---

## Technical Reference

### Key Constants

| Constant | Value | Meaning | Source |
|---|---|---|---|
| `CHAIN_100_PERCENT` | 10000 | 100% in basis points | [config.hpp:57](../../libraries/protocol/include/graphene/protocol/config.hpp#L57) |
| `CHAIN_1_PERCENT` | 100 | 1% in basis points | [config.hpp:58](../../libraries/protocol/include/graphene/protocol/config.hpp#L58) |
| `CHAIN_MAX_UNDO_HISTORY` | 10000 | Max head-LIB gap before undo history exception | [config.hpp:108](../../libraries/protocol/include/graphene/protocol/config.hpp#L108) |
| `CHAIN_EMERGENCY_CONSENSUS_TIMEOUT_SEC` | 3600 | Seconds since LIB before emergency activates | [config.hpp:112](../../libraries/protocol/include/graphene/protocol/config.hpp#L112) |
| `CHAIN_IRREVERSIBLE_THRESHOLD` | 7500 (75%) | Witness validation threshold for LIB advancement | [config.hpp:110](../../libraries/protocol/include/graphene/protocol/config.hpp#L110) |
| `fork_db._max_size` | 1024 | Default fork database depth | [fork_database.hpp:117](../../libraries/chain/include/graphene/chain/fork_database.hpp#L117) |
| `MAX_BLOCK_REORDERING` | 1024 | Max out-of-order block insertion distance | [fork_database.hpp:57](../../libraries/chain/include/graphene/chain/fork_database.hpp#L57) |

### Participation Rate Timeline

With 3-second block intervals and 128-slot window:

| Missed Slots | Participation Rate | Status |
|---|---|---|
| 0 | 100% | All witnesses active |
| 32 (1.6 min) | 75% | Healthy |
| 64 (3.2 min) | 50% | Degraded |
| 85 (4.3 min) | 33.6% | Just above default threshold |
| 86 (4.3 min) | 32.8% | **Below 33% — production stops** (with normal settings) |
| 96 (4.8 min) | 25% | Significant participation loss |
| 128 (6.4 min) | 0% | Complete stall |

### Fork Database Depth Timeline

| Solo Production Duration | Approx. Blocks | Fork DB Coverage |
|---|---|---|
| < 51 minutes | < 1024 | Full coverage — clean fork resolution |
| 51 min – 1 hour | 1020–1200 | Partial pruning begins |
| 1–8 hours | 1200–9600 | Significant pruning, re-sync likely needed |
| > 8.3 hours | > 10000 | Exceeds CHAIN_MAX_UNDO_HISTORY, replay may be needed |

### Code Flow: Block Production Decision

```
maybe_produce_block()
  │
  ├─ [HF12+] Is emergency_consensus_active?
  │   └─ Yes: Three-state safety handles production/sync checks automatically
  │
  ├─ Is _production_enabled?
  │   ├─ No: Is chain synced (get_slot_time(1) >= now)?
  │   │   ├─ Yes: _production_enabled = true (auto-enable)
  │   │   └─ No: return not_synced          ← BYPASSED when enable-stale-production=true
  │   └─ Yes: continue
  │
  ├─ Is it my turn? (scheduled witness check)
  │   └─ No: return not_my_turn
  │
  ├─ Do I have the private key?
  │   └─ No: return no_private_key
  │
  ├─ Is participation rate >= required-participation?
  │   └─ No: return low_participation         ← BYPASSED when required-participation=0
  │
  ├─ Am I within 500ms of scheduled time?
  │   └─ No: return lag
  │
  ├─ Fork collision in fork_db?
  │   └─ Yes: return fork_collision
  │
  ├─ Minority fork detection? (last 21 blocks all ours)
  │   └─ Yes: return minority_fork            ← BYPASSED when enable-stale-production=true
  │
  └─ Generate and broadcast block
      └─ return produced

update_global_dynamic_data() — Emergency activation:
  │
  ├─ enable-emergency-mode=false? → skip
  ├─ Is node syncing (replay/reindex or blocks_since_lib > 210)? → skip
  ├─ LIB block not available (snapshot restore)? → skip
  ├─ seconds_since_lib < 3600? → skip
  └─ Activate emergency consensus mode
```

The two emergency parameters bypass the two most important safeguards (steps 1 and 4), removing all automatic protection against solo production during network partitions.
