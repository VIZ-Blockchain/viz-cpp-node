# Fork Collision Reduction Hardfork Proposal

## Problem Statement

The VIZ blockchain experiences recurring "block num collision" events — situations where two witnesses produce blocks at the same height on different chain tips, creating a fork. With a 3-second block interval (`CHAIN_BLOCK_INTERVAL = 3`), there is minimal margin for block propagation, making collisions frequent during periods of network latency or clock drift.

### Observed Pattern (Block 79162800–79162802)

```
Block 79162800: mad-max @ 11:53:12 (latency 10160ms) | jackvote @ 11:53:15 (latency 7160ms)
Block 79162801: lexai @ 11:53:24 (latency 47ms)      | denis-skripnik @ 11:53:21 (latency 8357ms)
Block 79162802: micu @ 11:53:30 (latency 78ms)        | creativity @ 11:53:27 (latency 4044ms)
```

Three consecutive blocks had collisions, indicating a sustained network partition between two witness subsets. The high latency values (7–10 seconds) on one fork branch confirm severe propagation delay.

### Root Causes

1. **Tight block interval with no propagation margin** — 3-second slots leave no buffer for cross-region propagation (typical P2P propagation is 1–4 seconds for a global network).

2. **Deterministic witness ordering without shuffling** — The witness shuffle was [commented out](../libraries/chain/database.cpp) (`// VIZ remove randomization`), making the schedule predictable. If two consecutive witnesses have poor connectivity, collisions recur every round.

3. **No on-chain fork telemetry** — The current `_maybe_warn_multiple_production()` only logs a console warning. There is no on-chain metric for fork frequency, making it impossible to monitor network health programmatically.

4. **No production-time fork awareness** — Witnesses produce blocks on whatever chain tip they see, even if a competing block already exists in their fork database for the same height.

5. **Clock drift susceptibility** — `get_slot_at_time()` uses wall-clock time. NTP drift between witness nodes can cause slot mismatches. API load can indirectly degrade NTP precision due to thread contention.

---

## Proposal: Hardfork 12 — Fork Resilience Improvements

### Change 1: Fork Collision Counter in `dynamic_global_property_object`

**Type**: Consensus-breaking (new serialized field)

Add a rolling fork collision counter to `dynamic_global_property_object`, making fork frequency observable via the `get_dynamic_global_properties` API.

**File**: `libraries/chain/include/graphene/chain/global_property_object.hpp`

```cpp
class dynamic_global_property_object
    : public object<dynamic_global_property_object_type, dynamic_global_property_object> {
public:
    // ... existing fields ...

    /**
     * Total number of fork collisions (block num collisions) detected
     * since genesis. Incremented each time _maybe_warn_multiple_production()
     * finds multiple blocks at the same height in the fork database.
     * This counter never decreases, providing a cumulative metric
     * for monitoring network fork health.
     */
    uint32_t fork_collision_count = 0;

    /**
     * Block number of the most recent fork collision.
     * Zero if no collision has ever occurred.
     * Useful for detecting recent fork events via API polling.
     */
    uint32_t last_fork_collision_block_num = 0;
};
```

**FC_REFLECT update**:

```cpp
FC_REFLECT((graphene::chain::dynamic_global_property_object),
    // ... existing fields ...
    (fork_collision_count)
    (last_fork_collision_block_num)
)
```

**Increment logic** in `database::_maybe_warn_multiple_production()`:

```cpp
void database::_maybe_warn_multiple_production(uint32_t height) const {
    auto blocks = _fork_db.fetch_block_by_number(height);
    if (blocks.size() > 1) {
        // Increment on-chain counter (non-const via modify)
        const auto& dgp = get_dynamic_global_properties();
        modify(dgp, [&](dynamic_global_property_object& obj) {
            obj.fork_collision_count++;
            obj.last_fork_collision_block_num = height;
        });

        // ... diagnostic logging (already implemented) ...
    }
}
```

**Why this requires a hardfork**: Adding new fields to `dynamic_global_property_object` changes its serialized representation. All nodes must agree on the object layout for consensus. The snapshot plugin's export/import must also be updated.

**Consensus benefit**: Enables monitoring dashboards, alerting, and data-driven decisions about network topology. Witnesses with high collision rates can be identified and their connectivity improved.

---

### Change 2: Fork-Aware Block Production Deferral

**Type**: Non-consensus-breaking (witness plugin behavior only)

Already implemented in `plugins/witness/witness.cpp` — before producing a block, check if a competing block already exists in the fork database for the target height. If so, defer production to allow fork resolution.

This does **not** require a hardfork because:
- It only affects the witness plugin's production timing
- It does not change block validation rules
- A deferred block will be produced in the next available slot

However, the hardfork proposal could make this behavior **mandatory** by adding a consensus rule: if a witness observes a fork collision for the current slot, they MUST wait for the competing block's next witness to produce before building on top. This would formalize the deferral as a consensus rule rather than a best-effort optimization.

**Formalized version** (requires hardfork):

Add to `validate_block_header()`:

```cpp
if (has_hardfork(CHAIN_HARDFORK_12)) {
    // After a fork collision at height H, the next block must be
    // built on top of the longest chain's block at height H.
    // Witnesses must not produce on the shorter fork's tip.
    auto existing = _fork_db.fetch_block_by_number(next_block.block_num());
    if (existing.size() > 1) {
        // There was a collision at this height; verify this block
        // builds on the winner (longest chain)
        auto winner = _fork_db.head();
        FC_ASSERT(next_block.previous == winner->data.previous ||
                  next_block.block_num() > winner->num,
                  "Block produced on losing fork after collision");
    }
}
```

**Consensus benefit**: Prevents witnesses from extending a minority fork after a collision is detected, reducing the duration and depth of forks.

---

### Change 2a: Minority Fork Detection & Auto-Resync

**Type**: Non-consensus-breaking (witness + P2P plugin behavior only)

Implemented in `plugins/witness/witness.cpp` and `plugins/p2p/p2p_plugin.cpp` — before producing a block, the witness plugin walks back the last `CHAIN_MAX_WITNESSES` (21) blocks in fork_db and checks if ALL were produced by the node's own configured witnesses. If so, the node is stuck on a minority fork (no external witnesses are participating on this chain).

**Behavior by configuration:**

| Condition | Action |
|---|---|
| `enable-stale-production=false` (default) | Trigger recovery: pop blocks to LIB, reset fork_db, re-initiate P2P sync, reconnect seed nodes |
| `enable-stale-production=true` | Log and continue producing (operator override for bootstrap/testnet/recovery) |
| Emergency consensus active | Skip detection entirely (emergency mode blocks are all from committee account) |

**Recovery flow (`resync_from_lib()`):**

1. Pop all reversible blocks from head back to LIB via `pop_block()` loop
2. Clear pending transactions
3. Reset fork_db and re-seed with LIB block
4. Call `node->sync_from()` + `node->resync()` to re-initiate P2P sync
5. Reconnect all configured seed nodes
6. Set `_production_enabled = false` (node must receive a recent block to re-enable)

This replicates the effect of a manual docker stop/start without node downtime.

**Files modified:** `witness.hpp` (new enum value `minority_fork`), `witness.cpp` (detection logic + switch case), `p2p_plugin.hpp` (new `resync_from_lib()` method), `p2p_plugin.cpp` (implementation)

---

### Change 3: Production Delay Buffer

**Type**: Consensus-breaking (changes block timing expectations)

Add a configurable production delay of `CHAIN_PRODUCTION_DELAY_MS` milliseconds (e.g., 500ms) that a witness must wait after receiving a new block before producing its own. This gives the network time to propagate the latest block before the next witness builds on it.

**Implementation**:

```cpp
// In config.hpp:
#define CHAIN_PRODUCTION_DELAY_MS 500  // Wait 500ms after receiving block before producing

// In witness.cpp maybe_produce_block():
fc::time_point_sec earliest_production_time = db.head_block_time() +
    fc::milliseconds(CHAIN_PRODUCTION_DELAY_MS);
if (now < earliest_production_time) {
    capture("earliest", earliest_production_time)("now", now);
    return block_production_condition::not_time_yet;
}
```

**Why this requires a hardfork**: Changes the timing semantics of when blocks are expected. The current consensus assumes witnesses produce as close to their scheduled slot time as possible. A mandatory delay effectively shortens the usable production window.

**Consensus benefit**: A 500ms delay on a 3-second interval gives the P2P network 500ms to propagate the previous block to all witnesses before the next one starts building. This dramatically reduces the probability of two witnesses building on different chain tips simultaneously.

**Trade-off**: Block latency increases by up to 500ms per block. Over a day (28,800 blocks), this adds ~4 hours of total latency, but the actual user-perceived latency increase is only 500ms per transaction confirmation.

---

## Implementation Plan

### Phase 1: Non-Breaking Changes (No Hardfork Required)

These changes are already implemented and can be deployed immediately:

| Change | File | Status |
|--------|------|--------|
| Enhanced collision diagnostics with fork topology classification | `database.cpp` `_maybe_warn_multiple_production()` | Done |
| Rate-limited collision warnings (prevent log spam) | `database.cpp` `_maybe_warn_multiple_production()` | Done |
| Parent block ID logging for fork topology analysis | `database.cpp` `_maybe_warn_multiple_production()` | Done |
| Pre-production fork collision check in witness plugin | `witness.cpp` `maybe_produce_block()` | Done |
| `fork_collision` block production condition | `witness.hpp` enum, `witness.cpp` handler | Done |
| NTP re-sync on fork collision detection | `witness.cpp` `block_production_loop()` | Done |
| Minority fork detection & auto-resync | `witness.cpp`, `p2p_plugin.cpp/.hpp`, `witness.hpp` | Done |

### Phase 2: Hardfork 12 (Requires Network-Wide Upgrade)

| Change | Breaking | Files Modified |
|--------|----------|----------------|
| Fork collision counter in DGP | Yes (serialized object) | `global_property_object.hpp`, `database.cpp`, `snapshot/plugin.cpp` |
| Production delay buffer | Yes (timing) | `config.hpp`, `witness.cpp` |
| Mandatory fork deferral rule | Yes (validation) | `database.cpp` `validate_block_header()` |

### Hardfork 12 Definition

**File**: `libraries/chain/hardfork.d/12.hf`

```cpp
// 12 Hardfork — Fork Resilience Improvements
#ifndef CHAIN_HARDFORK_12
#define CHAIN_HARDFORK_12 12
#define CHAIN_HARDFORK_12_TIME <TBD> // To be determined by witness vote
#define CHAIN_HARDFORK_12_VERSION hardfork_version( version(3, 1, 0) )
#endif
```

**Update**: `0-preamble.hf` → `#define CHAIN_NUM_HARDFORKS 12`

---

## Impact Analysis

### Fork Collision Rate Reduction (Estimated)

| Scenario | Current | After HF12 | Reduction |
|----------|---------|------------|-----------|
| Consecutive witness pair with poor connectivity | ~95% collision/round | ~5% collision/round | ~19× |
| Random single-slot collision (network hiccup) | 100% produces fork | ~30% (delay absorbs transient) | ~3× |
| Sustained partition (2+ round) | 100% collision every block | 100% (delay can't help) | 0× |
| NTP drift <500ms | ~50% collision | ~10% (delay + re-sync) | ~5× |

### API Compatibility

The `get_dynamic_global_properties` API will return two additional fields. Clients that ignore unknown fields (most JSON parsers) will be unaffected. Clients with strict schema validation must be updated.

### Snapshot Compatibility

Snapshots created before HF12 will not contain `fork_collision_count` or `last_fork_collision_block_num`. The snapshot import logic must handle missing fields with defaults (0 for both). See [dlt-hardfork-new-objects.md](dlt-hardfork-new-objects.md) for the standard procedure.

---

## Alternative Approaches Considered

### A. Increase Block Interval to 5 Seconds

Would eliminate most collisions by giving 2+ seconds of propagation margin. **Rejected** because it increases transaction confirmation latency by 67% and reduces throughput proportionally.

### B. Batch Block Production (Produce 2+ Blocks per Slot)

Similar to EOS's approach where a witness produces a batch of consecutive blocks. **Rejected** because it increases centralization (longer production windows favor better-connected witnesses) and complicates missed-block accounting.

### C. Fork Choice by Witness Priority

Instead of longest-chain, use witness priority (e.g., higher-voted witness's block wins). **Rejected** because it breaks the fundamental longest-chain consensus rule and could enable voting attacks.

### D. P2P Block Prefetch / Fast Relay Network

A dedicated relay network for block propagation (similar to Bitcoin's FIBRE). **Not a hardfork** — can be implemented as a P2P plugin improvement. Recommended as a complementary non-consensus change.

---

## Related Documentation

- [DLT Hardfork New Objects](dlt-hardfork-new-objects.md) — Procedure for adding consensus objects in hardforks
- [Block Processing](block-processing.md) — Block application flow and fork resolution
- [Witness Operations](op-witness.md) — Witness update, vote, chain properties
- [Plugins](plugins.md) — Plugin architecture including witness and P2P plugins
