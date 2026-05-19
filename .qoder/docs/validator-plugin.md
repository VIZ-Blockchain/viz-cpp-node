# Validator Plugin Documentation

## Overview

The validator plugin is responsible for block production in the VIZ blockchain. It manages validator scheduling, block signing, broadcast, and implements sophisticated safety mechanisms including minority fork detection, emergency consensus support, and production watchdog recovery.

**Location:** `plugins/validator/validator.cpp`, `plugins/validator/include/graphene/plugins/validator/validator.hpp`

---

## Configuration Options

### Block Production Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enable-stale-production` | bool | false | Enable block production even if the chain is stale. Overrides sync and participation checks. |
| `required-participation` | uint32_t | 33% (33 * CHAIN_1_PERCENT) | Minimum validator participation percentage required to produce blocks. |
| `validator` / `-w` | string (multi) | - | Name of validator controlled by this node. Can be specified multiple times. |
| `private-key` | string (WIF, multi) | - | WIF private key(s) for signing blocks. |
| `emergency-private-key` | string (WIF, multi) | - | WIF private key for emergency consensus production. Auto-adds `CHAIN_EMERGENCY_WITNESS_ACCOUNT` to validator set. |

### Fork Collision Resolution

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `fork-collision-timeout-blocks` | uint32_t | 21 | Number of consecutive fork-collision deferrals before forcing production. One full validator round = 21 blocks (63 seconds). |

### NTP Synchronization

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `ntp-server` | string (multi) | pool.ntp.org, time.google.com, time.cloudflare.com | NTP server(s) for time synchronization. |
| `ntp-request-interval` | uint32_t | 900 | Time update request interval in seconds (15 min). |
| `ntp-retry-interval` | uint32_t | 300 | Retry interval when NTP hasn't replied (5 min). |
| `ntp-round-trip-threshold` | uint32_t | 150 | Round-trip delay threshold in ms; slower replies discarded. |
| `ntp-history-size` | uint32_t | 5 | Moving-average history window for NTP delta smoothing. |
| `ntp-rejection-threshold-pct` | uint32_t | 50 | Rejection threshold as percentage of absolute moving average. |
| `ntp-rejection-min-threshold` | uint32_t | 5 | Minimum rejection threshold in ms (applied regardless of percentage). |

### Debug Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `debug-block-production` | bool | false | Enable verbose debug logging for block production and chain internals. Sets `database::_debug_block_production`. |

---

## Plugin Dependencies

```cpp
APPBASE_PLUGIN_REQUIRES((chain::plugin) (p2p::p2p_plugin) (snapshot::snapshot_plugin))
```

The validator plugin requires:
- **chain::plugin**: Access to database, fork_db, validator schedule, block generation
- **p2p::p2p_plugin**: Block broadcast, sync status, peer connections, snapshot pause detection
- **snapshot::snapshot_plugin**: Query snapshot creation status via `is_snapshot_in_progress()`

---

## Internal State Variables

### Timer / Thread
- `production_io_service_` (boost::asio::io_service): **Dedicated** io_service for the production timer — completely separate from the appbase/P2P shared io_service. Declared before `production_timer_` to ensure correct init order.
- `production_io_work_` (unique_ptr\<io_service::work\>): Keeps the io_service alive while the thread runs.
- `production_io_thread_` (std::thread): Calls `production_io_service_.run()`. Joined in destructor after `production_io_service_.stop()`.
- `production_timer_` (boost::asio::deadline_timer): Timer bound to `production_io_service_`.

### Production Control
- `_production_skip_flags` (uint32_t): Flags passed to `generate_block()` (e.g., `skip_undo_history_check`)
- `_required_validator_participation` (uint32_t): Participation threshold from config
- `_private_keys` (map<public_key, private_key>): Loaded private keys for signing
- `_validators` (set<string>): Configured validator names (includes `CHAIN_EMERGENCY_VALIDATOR_ACCOUNT` if emergency key configured)
- `_last_lag_slot_time` (fc::time_point_sec): Scheduled time of the most recent `lag` condition. Zero when no lag is active. Used by `schedule_production_loop()` to skip ahead past the missed slot and avoid rechecking it every 250ms.

### Fork Detection & Recovery
- `fork_collision_defer_count_` (uint32_t): Consecutive fork-collision deferrals
- `_fork_collision_timeout_blocks` (uint32_t): Timeout threshold (default: 21)
- `_minority_fork_recovering` (bool): True when recovering from minority fork rollback
- `_minority_fork_recovery_start` (fc::time_point): When minority fork recovery started

### Stall Detection
- `_slot_zero_streak` (uint32_t): Consecutive `not_time_yet` returns (NTP/clock issues)
- `_slot_zero_streak_start` (fc::time_point): When slot=0 streak started
- `_not_my_turn_streak` (uint32_t): Consecutive slots assigned to other validators
- `_not_my_turn_streak_start` (fc::time_point): When not_my_turn streak started
- `_last_scheduled_validator` (string): Last validator that got the slot

### Watchdog & Diagnostics
- `_ever_produced` (bool): Whether we've ever produced a block
- `_last_production_time` (fc::time_point): Timestamp of last successful production
- `_last_slot_result` (int): Last result from slot > 0 iteration (excludes `not_time_yet`)
- `_watchdog_debug_enabled` (bool): Latching flag — set to `true` on first watchdog fire; never reset. Enables `database()._debug_block_production` automatically for post-hoc diagnosis.
- `_slot_hijack_count` (uint32_t): Consecutive blocks where committee filled our scheduled slot
- `_slot_hijack_height` (uint32_t): Last block height where hijack detected
- `_last_applied_block_num` (uint64_t): Last applied block number (for missed slot detection)

---

## Execution Flow

### 1. Plugin Lifecycle

#### `plugin_initialize()`
**Called during:** Application startup, before any plugin starts

**Actions:**
1. Create `impl` instance
2. Load validator names from `--validator` option into `_witnesses` set
3. Load `--enable-stale-production` flag → sets `skip_undo_history_check` in `_production_skip_flags` if true
4. Load `--required-participation` → `_required_witness_participation`
5. Parse `--private-key` WIF strings → `_private_keys` map
6. Parse `--emergency-private-key` WIF strings → `_private_keys` map
   - **IMPORTANT**: Auto-adds `CHAIN_EMERGENCY_WITNESS_ACCOUNT` to `_witnesses`
7. Configure NTP service from options and call `graphene::time::configure_ntp()`
8. Load `--fork-collision-timeout-blocks` → `_fork_collision_timeout_blocks`
9. Load `--debug-block-production` → `database::_debug_block_production`

**Does NOT:** Access database, check sync status, or start production timer

---

#### `plugin_startup()`
**Called during:** Application startup, after all plugins initialized

**Actions:**
1. Start NTP time client: `graphene::time::now()`
2. **Force NTP sync**: `graphene::time::update_ntp_time()` to minimize startup drift
3. Log validator configuration (count, names)
4. If `_witnesses` is not empty:
   - Call `p2p().set_block_production(true)` to enable P2P block production mode
   - Connect to `database::applied_block` signal → `on_block_applied()` handler
   - Set `_last_applied_block_num = database.head_block_num()`
   - If `skip_undo_history_check` set in `_production_skip_flags` (from `--enable-stale-production`):
     - If `head_block_num() == 0`: Print new chain banner
   - **Start production loop**: `schedule_production_loop()`
5. If no validators configured: Log error message

---

#### `plugin_shutdown()`
**Called during:** Application shutdown

**Actions:**
1. Shutdown NTP: `graphene::time::shutdown_ntp_time()`
2. Cancel production timer if validators configured

---

### 2. Production Loop

The production loop runs on a **dedicated `production_io_service_`** (not the shared appbase/P2P io_service) with its own OS thread. This isolation means P2P network activity — peer disconnects, TLS handshakes, send-queue drains — cannot delay the 250ms timer callback and cause missed-slot lag.

```
production_io_thread_ → production_io_service_.run()
                              ↓
                    production_timer_.async_wait()
                              ↓ (250ms boundary)
                    block_production_loop()
                              ↓
                    maybe_produce_block()
                              ↓
                    [result handling + lag skip]
                              ↓
                    schedule_production_loop()  // reschedule
```

#### `schedule_production_loop()`

**Lag skip guard (runs first):**
```cpp
if (_last_lag_slot_time != fc::time_point_sec()) {
    int64_t time_since_lag_ms = (fc::time_point::now()
        - fc::time_point(_last_lag_slot_time)).count() / 1000;
    if (time_since_lag_ms < CHAIN_BLOCK_INTERVAL * 1000) {
        int64_t skip_ms = (CHAIN_BLOCK_INTERVAL * 1000) - time_since_lag_ms;
        production_timer_.expires_from_now(posix_time::milliseconds(skip_ms));
        production_timer_.async_wait(...);
        _last_lag_slot_time = fc::time_point_sec();
        return result;
    }
}
```

After a `lag` condition the current slot is already missed. Without this guard, the loop would recheck the same slot every 250ms and return `lag` again, spinning at high CPU rate until the full 3s slot interval elapses. The guard skips ahead to the start of the next slot.

**Sleep calculation (normal path):**
```cpp
int64_t next_microseconds = 250000 - (ntp_microseconds % 250000);
if (next_microseconds < 50000) {
    next_microseconds += 250000; // minimum 50ms sleep
}
```

This aligns production ticks to 250ms boundaries with a +250ms lookahead in `maybe_produce_block()`.

**Sanity check:** If `next_microseconds > 500000` (500ms), logs warning about NTP backward jump.

---

#### `block_production_loop()`

**Exception handling:**
- `fc::canceled_exception`: Re-throw (node shutting down)
- `unknown_hardfork_exception`: Log error, re-throw (node out of date)
- `fc::exception`: Log error, return `exception_producing_block`

**Result handling:**
- `produced`: Reset fork_collision_defer_count, slot_zero_streak, not_my_turn_streak, slot_hijack_count. Set `_ever_produced`, `_last_production_time`. Clear `_minority_fork_recovering` if set.
- `not_synced`: Reset fork_collision_defer_count, slot_zero_streak, not_my_turn_streak
- `not_my_turn`: Reset fork_collision_defer_count, slot_zero_streak. Track `_not_my_turn_streak` (warning at 500 consecutive ≈ 125s)
- `not_time_yet`: **Track `_slot_zero_streak`** only if `now <= head_block_time()` (NTP behind chain). Warnings at 3 (750ms), 10 (2.5s, force NTP resync), 60 (15s), 120 (30s, critical)
- `no_private_key`, `low_participation`, `lag`, `consecutive`, `exception_producing_block`, `fork_collision`, `minority_fork`: Log appropriate messages

**Watchdog:** If `_ever_produced` and `should_be_producing` (derived from live state) and silence exceeds threshold:

```cpp
bool should_be_producing = false;
const auto& dgp_watch = database().get_dynamic_global_properties();
if (!_minority_fork_recovering && !_witnesses.empty()) {
    if (dgp_watch.emergency_consensus_active) {
        should_be_producing = (_witnesses.count(CHAIN_EMERGENCY_WITNESS_ACCOUNT) > 0);
    } else {
        uint32_t prate_watch = database().witness_participation_rate();
        should_be_producing = (prate_watch >= 33 * CHAIN_1_PERCENT);
    }
}
```

- Emergency master: 60s threshold
- Regular validator: 180s threshold
- Logs every 30s once triggered
- **WATCHDOG-RECOVERY**: If conditions met (head advancing < 30s, not syncing, has peers, has active keys):
  - Clear `_minority_fork_recovering`
  - Clear P2P catchup flag: `p2p().clear_catchup_flag()`
  - Clear chain syncing flag: `chain().clear_syncing()`
  - Reset streak counters
  - Production resumes automatically on next tick (no cached flag to set)

---

#### `maybe_produce_block()` - Main Production Logic

**This is the core function with all safety checks. Executes in order:**

---

##### Step 1: Capture Time and DGP

```cpp
fc::time_point now_fine = graphene::time::now();
fc::time_point_sec now = now_fine + fc::microseconds(250000);
const auto &dgp = db.get_dynamic_global_properties();
```

**Why +250ms lookahead:** Aligns with timer scheduling to ensure we're at slot boundary when production decision is made.

---

##### Step 2: DLT Mode Sync Check (Line ~1098)

```cpp
if (db._dlt_mode && chain().is_syncing()) {
    bool we_are_emergency_master =
        dgp.emergency_consensus_active &&
        _witnesses.find(CHAIN_EMERGENCY_WITNESS_ACCOUNT) != _witnesses.end();
    if (!we_are_emergency_master) {
        return block_production_condition::not_synced;
    }
    // Emergency master: bypass sync check to avoid deadlock
}
```

**What it checks:**
- `chain().is_syncing()`: **YES, calls chain plugin** to check `currently_syncing` atomic flag
- **Why:** In DLT mode, producing during sync creates blocks on stale head conflicting with incoming blocks → oscillation bug

**Emergency master exception:**
- If emergency consensus active AND we have emergency key → production proceeds
- **Why:** Master is sole producer; waiting for sync would deadlock (no blocks arrive to clear syncing flag)

**Outside DLT mode:** This check is NOT applied. Normal validators must produce on canonical head even while network catching up.

---

##### Step 3: Snapshot Pause / Post-Pause Catchup Gate (Line ~1118)

```cpp
// Check snapshot plugin directly for snapshot_in_progress flag
if (snapshot().is_snapshot_in_progress()) {
    wlog("Deferring block production: snapshot creation in progress");
    return not_synced;
}

if (p2p().is_catching_up_after_pause()) {
    wlog("Deferring block production: P2P is catching up after snapshot pause");
    return not_synced;
}
```

**What it checks:**
- `snapshot().is_snapshot_in_progress()`: **YES, calls snapshot plugin** — `snapshot_in_progress` atomic flag (relaxed load)
- `p2p().is_catching_up_after_pause()`: **YES, calls P2P plugin** — returns true if `_block_processing_paused || _catchup_after_pause`

**Why two checks:**
1. **Snapshot in progress**: snapshot plugin holds DB read lock; producing would cause write-lock starvation
2. **P2P catchup after pause**: snapshot finished, but queued blocks haven't drained yet; producing on stale head → fork

**Applies to:** ALL validator types (emergency and normal)

**Flag cleared when:**
- `snapshot_in_progress`: Cleared by snapshot plugin on completion
- `_catchup_after_pause`: Cleared when drain completes + no peer ahead

---

##### Step 4: Hardfork 12 Three-State Safety Enforcement (Line ~1132)

```cpp
if (db.has_hardfork(CHAIN_HARDFORK_12)) {
    if (dgp.emergency_consensus_active) {
        // Emergency mode logic
    } else {
        // Normal mode with participation check
    }
} else {
    // Pre-HF12 legacy behavior
}
```

**Sub-case 4a: Emergency Consensus Active**

```cpp
bool we_are_emergency_master =
    _witnesses.find(CHAIN_EMERGENCY_WITNESS_ACCOUNT) != _witnesses.end();
if (!we_are_emergency_master) {
    // Slave node: must sync first before producing
    if (db.get_slot_time(1) < now) {
        return block_production_condition::not_synced;
    }
}
// Emergency master proceeds unconditionally — no cached flag, state queried fresh every tick
```

**Why:** Master MUST produce to avoid deadlock. Slave nodes must still sync first.

---

**Sub-case 4b: Normal Mode (Participation Check)**

```cpp
uint32_t prate = db.witness_participation_rate();
if (prate >= 33 * CHAIN_1_PERCENT) {
    // HEALTHY NETWORK
    _production_skip_flags &= ~skip_undo_history_check;  // Re-enable minority fork detection
    // Check sync status directly (no cached flag — queried fresh every tick)
    if (db.get_slot_time(1) < now) {
        return not_synced;
    }
    // No participation check needed (already >= 33%)
} else {
    // DISTRESSED NETWORK (< 33%)
    if (!(_production_skip_flags & skip_undo_history_check)) {
        // No stale-production override: require sync
        if (db.get_slot_time(1) < now) {
            return not_synced;
        }
    }
    // enable-stale-production=true: operator override, proceed regardless of sync status
    if (prate < _required_witness_participation) {
        if (_production_skip_flags & skip_undo_history_check) {
            // Operator override: produce anyway to bootstrap stalled network
        } else {
            return low_participation;  // Network partition guard
        }
    }
}
```

**Why 33% threshold:** Below this, node likely in minority network segment. Producing risks two partitions building chains simultaneously.

**enable-stale-production override:** If set, bypasses participation and sync checks to allow operator to recover fully stalled network.

---

**Sub-case 4c: Pre-HF12 Legacy**

```cpp
// Check sync status directly (no cached flag)
if (db.get_slot_time(1) < now) {
    return not_synced;
}
// No participation check here (done later before block generation)
```

---

##### Step 5: Block Post Validation Broadcast (Line ~1228)

```cpp
if(last_block_post_validation_time < now_fine) {
    last_block_post_validation_time = now;
    // Build scheduled_witnesses_set from witness_schedule_object
    // For each validator in _witnesses:
    //   - Skip if not in current schedule
    //   - Get block_post_validations from database
    //   - Sign with validator private key
    //   - Broadcast via p2p().broadcast_block_post_validation()
}
```

**What it does:** Signs and broadcasts post-validation messages for scheduled validators to contribute to LIB advancement.

**Optimization:** Skips validators not in current schedule (can't contribute to LIB).

---

##### Step 6: Minority Fork Detection (Non-Emergency) (Line ~1318)

```cpp
if (!dgp.emergency_consensus_active) {
    auto fork_head = db.get_fork_db().head();
    // Walk back CHAIN_MAX_WITNESSES (21) blocks in fork_db
    // If ALL from our validators → minority fork
    if (all_ours && blocks_checked >= CHAIN_MAX_WITNESSES) {
        if (_production_skip_flags & skip_undo_history_check) {
            // enable-stale-production: continue
        } else {
            p2p().resync_from_lib();
            _minority_fork_recovering = true;
            return minority_fork;
        }
    }
}
```

**Why skipped in emergency mode:** All blocks produced by committee (which may be in `_witnesses`), would always falsely trigger.

**enable-stale-production override:** Operator can continue producing on minority fork (testnet/bootstrap scenario).

---

##### Step 7: DLT-Specific Minority Fork Detection in Emergency Mode (Line ~1382)

```cpp
if (dgp.emergency_consensus_active && db._dlt_mode) {
    // Check if we are emergency master
    bool we_are_master = false;
    if (_witnesses.find(CHAIN_EMERGENCY_WITNESS_ACCOUNT) != _witnesses.end()) {
        // Check if committee is in current validator schedule
        const witness_schedule_object &wso = db.get_witness_schedule_object();
        for (int i = 0; i < wso.num_scheduled_witnesses; i += CHAIN_BLOCK_WITNESS_REPEAT) {
            if (wso.current_shuffled_witnesses[i] == CHAIN_EMERGENCY_WITNESS_ACCOUNT) {
                we_are_master = true;
                break;
            }
        }
    }

    if (!we_are_master) {
        // Slave DLT node: run fork_db isolation scan
        // If last 21 blocks all from our validators → isolated from master
        p2p().resync_from_lib(true /*force_emergency*/);
        _minority_fork_recovering = true;
        return minority_fork;
    }
    // Emergency master: skip to avoid false positives
}
```

**Why separate check:** In DLT emergency mode, standard check skipped but slave node can still get isolated from master. Uses same 21-block threshold (one full round).

**Master skip logic:** If committee in schedule AND we have its key → we ARE the master. All blocks being "ours" is expected.

---

##### Step 8: Acquire Operation Guard (Line ~1443)

```cpp
auto op_guard = db.make_operation_guard();
```

**Why:** Guards lockless reads into shared memory against concurrent resize. Prevents pointer invalidation while reading validator schedule, slot time, etc.

**Stall detection:** If guard blocks > 100ms, logs warning. If crosses slot boundary and we lost our slot → critical error.

**Time refresh:** Re-captures `now` after acquiring guard (if guard blocked on DB resize, original `now` is stale).

---

##### Step 9: Slot Assignment Check (Line ~1477)

```cpp
uint32_t slot = db.get_slot_at_time(now);
if (slot == 0) {
    // NTP drift check: warn if local clock > 250ms behind NTP
    return not_time_yet;
}

string scheduled_witness = db.get_scheduled_witness(slot);
if (_witnesses.find(scheduled_witness) == _witnesses.end()) {
    return not_my_turn;
}
```

**NTP drift warning:** If `ntp_error() > 250ms`, warns about potential silent slot misses.

---

##### Step 10: validator Validation (Line ~1563)

```cpp
const auto &witness_by_name = db.get_index<witness_index>().indices().get<by_name>();
auto itr = witness_by_name.find(scheduled_witness);

fc::time_point_sec scheduled_time = db.get_slot_time(slot);
graphene::protocol::public_key_type scheduled_key = itr->signing_key;

// Check if slot already filled
if (scheduled_time <= db.head_block_time()) {
    return not_time_yet;  // Slot filled during/after snapshot pause
}

// Check if validator disabled (zero key)
if (scheduled_key == public_key_type()) {
    // Log warning (every 60s for regular, 3s for emergency)
    return not_my_turn;
}

// Check if we have private key
auto private_key_itr = _private_keys.find(scheduled_key);
if (private_key_itr == _private_keys.end()) {
    return no_private_key;
}
```

**Slot already filled check:** Critical for snapshot pause scenario. Another validator may have filled the slot during pause.

---

##### Step 11: Pre-HF12 Participation Check (Line ~1629)

```cpp
if (!db.has_hardfork(CHAIN_HARDFORK_12)) {
    uint32_t prate = db.witness_participation_rate();
    if (prate < _required_witness_participation) {
        if (_production_skip_flags & skip_undo_history_check) {
            // Operator override
        } else {
            return low_participation;
        }
    }
}
```

**Why here for pre-HF12:** HF12 moved participation check earlier (Step 4) for better emergency mode handling.

---

##### Step 12: Lag Check (Line ~1644)

```cpp
if (llabs((scheduled_time - now).count()) > fc::milliseconds(500).count()) {
    return lag;  // Woke up too late for this slot
}
```

**Threshold:** 500ms tolerance. If we're more than 500ms past slot time, skip.

---

##### Step 13: Fork Collision Resolution (Line ~1661)

```cpp
auto existing_blocks = db.get_fork_db().fetch_block_by_number(db.head_block_num() + 1);
if (existing_blocks.size() > 0) {
    // Determine if competing block exists
    // Emergency mode: ANY block at this height is competing
    // Normal mode: only different validator + different parent

    if (has_competing_block) {
        fork_collision_defer_count_++;

        // LEVEL 2: Timeout after 21 deferrals (stuck-head)
        if (fork_collision_defer_count_ > _fork_collision_timeout_blocks) {
            // Remove dead-fork competing block, produce on our chain
            db.get_fork_db().remove_blocks_by_number(db.head_block_num() + 1);
            fork_collision_defer_count_ = 0;
            // Fall through to produce
        }
        // LEVEL 1: Vote-weighted comparison (HF12+)
        else if (db.has_hardfork(CHAIN_HARDFORK_12)) {
            int weight_cmp = db.compare_fork_branches(competing_block->id, db.head_block_id());
            if (weight_cmp < 0) {
                // Our fork has MORE weight → produce, remove competing block
            } else if (weight_cmp > 0) {
                // Competing fork has MORE weight → defer
                return fork_collision;
            } else {
                // Tied → defer, timeout will kick in
                return fork_collision;
            }
        }
        // Pre-HF12: defer, timeout still applies
        else {
            return fork_collision;
        }
    }
}
```

**Two-level resolution:**
1. **Vote-weighted comparison** (HF12+): Compare fork branches by accumulated vote weight
2. **Stuck-head timeout** (all versions): After 21 deferrals (63s), assume competing block is on dead fork

**Why 21 blocks:** One full validator round. If head hasn't advanced after 21 slots, longer chain had all scheduled validators produce → canonical chain confirmed.

---

##### Step 14: Second Snapshot Pause Check (Line ~1769)

```cpp
try {
    if (p2p().is_catching_up_after_pause()) {
        return not_time_yet;  // Snapshot started between checks
    }
} catch (...) {}
```

**Why second check:** Race window ~1 block interval. If snapshot started after first check (~line 1118), producing would cause 2-11s write-lock starvation.

**Cost:** One missed slot (3s) — far cheaper than full snapshot read hold time.

---

##### Step 15: Generate and Broadcast Block (Line ~1777)

```cpp
auto block = db.generate_block(
    scheduled_time,
    scheduled_witness,
    private_key_itr->second,
    _production_skip_flags
);

p2p().broadcast_block(block);

// Seed reconnect if few peers
auto peer_count = p2p().get_connections_count();
if (peer_count < 2 && !p2p().is_isolated_peers()) {
    p2p().reconnect_seeds();
}

return produced;
```

**Retry logic:** Up to 2 retries on `fc::exception` (clears pending transactions between retries).

**Exception handling:**
- `shared_memory_corruption_exception`: Call `chain().attempt_auto_recovery()`
- `unlinkable_block_exception`: Fork DB broken → rollback to LIB, resync from P2P
- `fc::exception`: Clear pending transactions, retry

---

### 3. Signal Handler: `on_block_applied()`

**Connected to:** `database::applied_block` signal during `plugin_startup()`

**Purpose:** Detect missed slots and slot hijacks for diagnostics.

---

#### Slot Hijack Detection (Runs for every block)

```cpp
if (database()._dlt_mode && !_witnesses.empty()
    && prev_num > 0 && block_num == prev_num + 1) {
    const auto& dgp = database().get_dynamic_global_properties();
    if (dgp.emergency_consensus_active) {
        // Slot index for the block just applied
        uint64_t slot_idx = dgp.current_aslot % nsw;
        const std::string& expected_witness = wso.current_shuffled_witnesses[slot_idx];

        // True hijack: expected slot is ours AND actual producer is not one of our validators
        if (_witnesses.count(expected_witness) > 0
            && _witnesses.count(block.validator) == 0) {
            _slot_hijack_count++;  // Committee / emergency master filled our slot
            // Log first 3 hijacks, then once per minute
        } else if (_witnesses.count(block.validator) > 0) {
            // ANY of our validators produced → reset (false-positive guard)
            if (_slot_hijack_count > 0) {
                ilog("Hijack counter reset: our validator '${w}' produced...", ...);
            }
            _slot_hijack_count = 0;
        }
    }
}
```

**What it detects:** In DLT emergency mode, the emergency master may blank our validator's signing_key and fill our scheduled slots with committee blocks.

**Important:** The reset condition checks `_witnesses.count(block.validator) > 0` — i.e., ANY of our configured validators produced the block, not just the slot-assigned one. Without this, a legitimate block from a different one of our validators would be mis-counted as a hijack.

---

#### Missed Slot Detection

```cpp
if (block_num > prev_num + 1) {
    uint32_t missed_count = block_num - prev_num - 1;

    // Calculate which validators were scheduled for missed slots
    for (uint32_t i = 0; i < missed_count && i < 100; ++i) {
        uint64_t abs_slot = cur_aslot - missed_count + i;
        const std::string &wname = wso.current_shuffled_witnesses[abs_slot % num_witnesses];
        if (_witnesses.count(wname) > 0) {
            our_witness_missed = true;
        }
    }

    if (our_witness_missed) {
        // Dump full diagnostic state:
        // - Production flags, NTP offset, sync status
        // - On-chain signing key status (blanked?)
        // - Next slot time, scheduled validator
        // - Streak counters
        elog("MISSED-SLOT-OUR-validator: ...");
    }
}
```

**Why check missed slots:** When incoming blocks reveal gaps, determines if our validator was scheduled for any missed slots and logs full diagnostic state for troubleshooting.

---

## Public API Methods

### `is_witness_scheduled_soon()`

**Returns:** `true` if locally-controlled validator is scheduled to produce in next 4 slots (~12 seconds)

**Implementation:**
1. Check 4 upcoming slots (covers snapshot creation time ~10s + safety margin)
2. For each slot:
   - Get scheduled validator name
   - Check if in `_witnesses` set
   - Look up validator object in database
   - Check if signing key is non-zero
   - Check if we have corresponding private key
3. Return `true` if any match found

**Used by:** Snapshot plugin to defer snapshot creation when validator about to produce (avoids fork on stale head)

---

### `is_emergency_master()`

**Returns:** `true` if this node is the emergency master

**Conditions:**
1. Holds `emergency-private-key` (`CHAIN_EMERGENCY_WITNESS_ACCOUNT` in `_witnesses`)
2. Committee account is in current validator schedule

**Why both conditions:** Multiple nodes can have emergency key, but only the one where committee is scheduled should produce solo. Others are followers that must sync.

**Used by:**
- P2P plugin: Determines if node should wait for network or produce
- Snapshot plugin: Skips stalled sync recovery for master
- Watchdog: Different silence thresholds (60s vs 180s)

---

### `is_emergency_key_configured()`

**Returns:** `true` if `emergency-private-key` is configured, regardless of schedule

**Used by:** External diagnostics, P2P hello messages

---

### `get_production_diagnostics()`

**Returns:** Compact diagnostic string with key production-state flags

**Format:** `validator[skip_flags=0x0 catching_up=0 head=#123 last_prod=45s_ago minority_rcv=0 slot_hijacks=2]`

**Used by:** P2P layer when FORWARD stagnation fires with no peer ahead, so stagnation log shows why master isn't filling gap.

---

## Hardfork Checks

### CHAIN_HARDFORK_12

**Location:** Multiple places in `maybe_produce_block()`

**Changes introduced:**
1. **Three-state safety enforcement** (Step 4):
   - Pre-HF12: Simple sync check
   - HF12+: Emergency mode detection, participation-based auto-enable, stale-production override logic

2. **Minority fork detection** (Step 6):
   - Pre-HF12: Standard 21-block check
   - HF12+: Skipped during emergency consensus, DLT-specific check added

3. **Participation check position** (Step 11):
   - Pre-HF12: Checked just before block generation
   - HF12+: Checked early in Step 4 with emergency mode awareness

4. **Fork collision resolution** (Step 13):
   - Pre-HF12: Defer only, timeout applies
   - HF12+: Vote-weighted comparison (Level 1) + timeout (Level 2)

5. **Block post validation**:
   - Pre-HF12: LIB advancement via 2/3 validator signatures
   - HF12+: LIB advancement via validator participation rate, emergency mode disables post-validation chain

---

## Database Access Patterns

### Direct Database Reads (via `database()` reference)

**Frequent (every production tick ~250ms):**
- `db.get_dynamic_global_properties()` — emergency consensus state, head block number/time, current_aslot
- `db.get_slot_at_time(now)` — determine if slot available
- `db.get_scheduled_witness(slot)` — who should produce
- `db.get_slot_time(slot)` — scheduled slot time
- `db.get_witness_schedule_object()` — shuffled validator list, num_scheduled_witnesses
- `db.head_block_num()`, `db.head_block_time()` — current chain state
- `db.get_fork_db().head()`, `db.get_fork_db().fetch_block_by_number()` — fork detection
- `db.get_index<witness_index>().indices().get<by_name>().find()` — validator signing key status
- `db.witness_participation_rate()` — network health check
- `db.has_hardfork(CHAIN_HARDFORK_12)` — feature gate

**Infrequent (on events or diagnostics):**
- `db.get_block_post_validations()` — sign and broadcast validations
- `db.compare_fork_branches()` — vote-weight comparison (HF12+)
- `db.get_fork_db().remove()`, `remove_blocks_by_number()` — fork resolution

**Write operations:**
- `db.generate_block()` — create and sign new block
- `db.clear_pending()` — clear pending transactions on failure

### Does NOT cache results

**All database reads are fresh on every call:**
- No caching of validator schedule
- No caching of DGP state
- No caching of fork_db state
- No caching of signing key status

**Why:** State changes every block (emergency mode can activate/deactivate, validator schedule changes, signing keys can be blanked). Caching would create race conditions and stale decisions.

**Exception:** `_witnesses` set and `_private_keys` map are loaded once during `plugin_initialize()` and never refreshed (operator must restart to change configuration).

---

## Sync/Forward Status Checks

### Chain Plugin: `chain().is_syncing()`

**Called in:**
1. Step 2 (DLT mode sync check)
2. Watchdog recovery
3. Diagnostic logging (missed slot, stall detection)

**What it checks:** `chain_plugin::currently_syncing` atomic flag

**Set by:** Chain plugin when processing P2P sync blocks (`accept_block()` with `currently_syncing_flag=true`)

**Cleared by:**
- Chain plugin when sync completes
- Watchdog recovery (force-clear)

---

### P2P Plugin: `p2p().is_catching_up_after_pause()`

**Called in:**
1. Step 3 (snapshot pause gate)
2. Step 14 (second snapshot pause check)
3. Watchdog recovery
4. Diagnostic logging
5. `get_production_diagnostics()`

**What it checks:** `_block_processing_paused || _catchup_after_pause` flags in `dlt_p2p_node`

**Set by:**
- `pause_block_processing()`: Sets `_block_processing_paused = true` (snapshot creation starting)
- `resume_block_processing()`: Sets `_block_processing_paused = false`, may set `_catchup_after_pause = true` (drain queued blocks)

**Cleared by:**
- `resume_block_processing()`: After drain completes
- Watchdog recovery: `p2p().clear_catchup_flag()`

---

### Snapshot Plugin: Direct API `snapshot().is_snapshot_in_progress()`

**Called in:**
1. Step 3 (snapshot pause gate) — first check before P2P catchup check

**What it checks:** `snapshot_plugin::snapshot_in_progress` atomic flag (relaxed load)

**Implementation:**
```cpp
bool snapshot_plugin::is_snapshot_in_progress() const {
    if (!my) return false;
    return my->snapshot_in_progress.load(std::memory_order_relaxed);
}
```

**Set by:** Snapshot plugin when serialization starts

**Cleared by:** Snapshot plugin on completion

---

### Snapshot Plugin: Indirect check via `is_witness_scheduled_soon()`

**Called by:** Snapshot plugin in `on_applied_block()` before scheduling snapshot

**Why:** Defer snapshot if validator about to produce (~12s window). Producing during snapshot → read-lock contention, producing after on stale head → fork.

---

## State Machine Summary

```
[Startup]
  ↓ plugin_initialize()
[Config loaded]
  ↓ plugin_startup()
[Production loop running]
  ↓ every 250ms
[maybe_produce_block()]
  ├─→ not_synced (DLT sync, snapshot pause)
  ├─→ not_time_yet (slot=0, NTP drift, slot already filled)
  ├─→ not_my_turn (validator disabled, wrong validator scheduled)
  ├─→ no_private_key (missing key)
  ├─→ low_participation (< 33%, no override)
  ├─→ lag (> 500ms past slot time)
  ├─→ fork_collision (competing block, defer)
  ├─→ minority_fork (21 blocks from our validators, rollback to LIB)
  └─→ produced (success, broadcast block)
```

---

## Critical Safety Mechanisms

### 1. Minority Fork Detection
- **Trigger:** Last 21 blocks in fork_db all from our validators
- **Action:** Rollback to LIB, resync from P2P network
- **Override:** `enable-stale-production=true`
- **Emergency mode:** Skipped (committee blocks would always trigger), DLT-specific check for slaves

### 2. Fork Collision Resolution
- **Level 1 (HF12+):** Vote-weight comparison, produce on heavier fork
- **Level 2:** Timeout after 21 deferrals (63s), assume dead fork
- **Emergency mode:** ANY competing block triggers defer

### 3. Network Partition Guard
- **Trigger:** validator participation < 33%
- **Action:** Stop production (return `low_participation`)
- **Override:** `enable-stale-production=true` for bootstrap/recovery

### 4. Slot Already Filled Guard
- **Trigger:** `scheduled_time <= head_block_time()`
- **Why:** Another validator filled slot during/after snapshot pause
- **Action:** Skip production (return `not_time_yet`)

### 5. Production Watchdog
- **Trigger:** No production for 60s (emergency) or 180s (regular); `should_be_producing` true (derived from live DB state)
- **Conditions:** Head advancing, not syncing, has peers, has active keys
- **Action:** Clear blocking conditions (`_minority_fork_recovering`, P2P catchup, chain syncing); production resumes automatically on next tick

### 6. NTP Stall Detection
- **Trigger:** `slot=0` streak (NTP behind chain time)
- **Thresholds:**
  - 3 (750ms): Warning
  - 10 (2.5s): Force NTP resync
  - 60 (15s): Prolonged stall warning
  - 120 (30s): Critical, action required

### 7. Slot Hijack Detection (DLT Emergency)
- **Trigger:** Committee fills our scheduled slot
- **Action:** Log diagnostics, increment counter
- **Why:** Emergency master blanked our key, producing in our slots

---

## Key Invariants

1. **Never produce during sync (DLT mode):** Creates blocks on stale head → oscillation bug
2. **Never produce during snapshot pause:** Write-lock deadlock
3. **Never produce if slot already filled:** Creates micro-fork
4. **Emergency master must always produce:** Sole producer, waiting = deadlock
5. **Slave nodes must sync first:** Producing on stale head = minority fork
6. **Participation < 33% = stop production:** Network partition guard (unless override)
7. **21 consecutive blocks from our validators = minority fork:** Rollback to LIB
8. **All database reads are fresh:** No caching, state changes every block

---

## Troubleshooting Guide

### Problem: Not producing blocks

**Check logs for:**
- `not_synced`: DLT sync active or snapshot pause → wait for sync/pause to complete
- `not_time_yet`: NTP drift or slot=0 → check NTP offset in logs
- `not_my_turn`: Wrong validator scheduled or key blanked → check `keys=[...]` in watchdog log
- `no_private_key`: Missing private key → check config
- `low_participation`: Network participation < 33% → set `enable-stale-production=true`
- `fork_collision`: Competing block → wait for resolution (21 blocks max)
- `minority_fork`: On wrong fork → resyncing from LIB

**Diagnostic command:** Check `get_production_diagnostics()` output in P2P stagnation logs.

---

### Problem: Producing on wrong fork

**Symptoms:** `MINORITY FORK DETECTED` log, blocks not accepted by network

**Cause:** Isolated from network, only seeing own blocks

**Action:**
1. Check peer connections
2. Verify network connectivity
3. Plugin will auto-rollback to LIB and resync

---

### Problem: Emergency master not producing

**Symptoms:** Network stalled, `EMRG-DIAG slot=0` logs

**Check:**
1. Is emergency key configured? → `--emergency-private-key`
2. Is committee in validator schedule? → Check `witness_schedule_object`
3. Is NTP synchronized? → Check NTP offset
4. Is sync flag stuck? → Watchdog should auto-clear

**Watchdog recovery:** If conditions met, watchdog will force-reset flags after 60s silence.

---

### Problem: Slot hijacks detected

**Symptoms:** `SLOT-HIJACK` logs, `slot_hijacks=N` in watchdog diagnostics

**Cause:** Emergency master blanked our validator key and producing committee blocks in our slots

**Normal behavior:** In DLT emergency mode, master may blank offline validators to maintain chain progress

**Action:**
1. Check validator signing key status: `keys=[validator:key=BLANK]`
2. Send `update_witness` transaction to restore key
3. Wait for emergency mode to end (LIB advances)

---

## Related Files

- **Chain plugin:** `plugins/chain/plugin.cpp`, `plugins/chain/include/graphene/plugins/chain/plugin.hpp`
- **P2P plugin:** `plugins/p2p/p2p_plugin.cpp`, `plugins/p2p/include/graphene/plugins/p2p/p2p_plugin.hpp`
- **Snapshot plugin:** `plugins/snapshot/plugin.cpp`, `plugins/snapshot/include/graphene/plugins/snapshot/plugin.hpp`
- **validator guard plugin:** `plugins/witness_guard/witness_guard.cpp`
- **DLT P2P node:** `libraries/network/dlt_p2p_node.cpp`, `libraries/network/include/graphene/network/dlt_p2p_node.hpp`
- **Database:** `libraries/chain/database.cpp` (emergency consensus, hardfork logic, block generation)
- **NTP time:** `libraries/time/time.cpp` (time synchronization)
