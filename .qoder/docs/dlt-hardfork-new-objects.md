# DLT Hardforks with New Consensus Objects

This document describes how to implement hardforks that introduce new consensus objects in DLT (Distributed Ledger Technology) mode, and how the snapshot system handles compatibility between nodes running different versions.

## Overview

In DLT mode, nodes can start from snapshots instead of replaying the entire blockchain. When a hardfork introduces new consensus objects, the system must ensure:

1. **Forward compatibility**: Old nodes can load snapshots containing unknown objects
2. **Backward compatibility**: New nodes can load older snapshots missing new objects
3. **Consensus safety**: Nodes only participate in consensus for hardforks they understand

## Adding New Consensus Objects in a Hardfork

### Step 1: Define the Object Type

Add the new object type to `libraries/chain/include/graphene/chain/chain_object_types.hpp`:

```cpp
enum object_type {
    // ... existing types ...
    block_post_validation_object_type,  // last existing type
    prediction_market_object_type,      // NEW: add new type at the end
};

// Forward declaration
class prediction_market_object;

// Type alias
typedef object_id<prediction_market_object> prediction_market_object_id_type;

// Add to FC_REFLECT_ENUM
FC_REFLECT_ENUM(graphene::chain::object_type,
    // ... existing types ...
    (block_post_validation_object_type)
    (prediction_market_object_type)     // NEW
)
```

### Step 2: Create the Object Definition

Create a new header file `libraries/chain/include/graphene/chain/prediction_market_object.hpp`:

```cpp
#pragma once

#include <graphene/chain/chain_object_types.hpp>
#include <chainbase/chainbase.hpp>

namespace graphene { namespace chain {

class prediction_market_object
    : public object<prediction_market_object_type, prediction_market_object> {
public:
    prediction_market_object() = delete;

    template<typename Constructor, typename Allocator>
    prediction_market_object(Constructor&& c, allocator<Allocator> a)
        : title(a), description(a) {
        c(*this);
    }

    id_type id;
    account_name_type creator;
    shared_string title;
    shared_string description;
    asset total_stake;
    time_point_sec resolution_time;
    uint16_t status = 0;  // 0=open, 1=resolved, 2=cancelled
};

struct by_creator;
struct by_resolution_time;

typedef multi_index_container<
    prediction_market_object,
    indexed_by<
        ordered_unique<tag<by_id>,
            member<prediction_market_object, prediction_markarket_object_id_type, &prediction_market_object::id>>,
        ordered_non_unique<tag<by_creator>,
            member<prediction_market_object, account_name_type, &prediction_market_object::creator>>,
        ordered_non_unique<tag<by_resolution_time>,
            member<prediction_market_object, time_point_sec, &prediction_market_object::resolution_time>>
    >,
    allocator<prediction_market_object>
> prediction_market_index;

}} // graphene::chain

FC_REFLECT((graphene::chain::prediction_market_object),
    (id)(creator)(title)(description)(total_stake)(resolution_time)(status))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::prediction_market_object, graphene::chain::prediction_market_index)
```

### Step 3: Register the Index

Add the index registration in `libraries/chain/database.cpp` in `initialize_indexes()`:

```cpp
void database::initialize_indexes() {
    // ... existing indexes ...
    add_core_index<block_post_validation_index>(*this);

    // NEW: Register prediction market index
    add_core_index<prediction_market_index>(*this);

    _plugin_index_signal();
}
```

### Step 4: Update Snapshot Plugin

Add serialization support in `plugins/snapshot/plugin.cpp`:

**In `serialize_state()`:**
```cpp
fc::mutable_variant_object snapshot_plugin::plugin_impl::serialize_state() {
    fc::mutable_variant_object state;

    // ... existing EXPORT_INDEX calls ...

    // NEW: Export prediction market objects
    EXPORT_INDEX(prediction_market_index, prediction_market_object, "prediction_market")

    #undef EXPORT_INDEX
    return state;
}
```

**In `load_snapshot()` - add import logic:**
```cpp
// NEW: Import prediction market objects (in appropriate section)
if (state.contains("prediction_market")) {
    auto n = detail::import_prediction_markets(db, state["prediction_market"].get_array());
    ilog("Imported ${n} prediction markets", ("n", n));
}
```

**Create specialized import function (if using shared_string):**
```cpp
inline uint32_t import_prediction_markets(
    graphene::chain::database& db,
    const fc::variants& arr
) {
    uint32_t count = 0;
    for (const auto& v : arr) {
        auto id_val = v["id"].as_int64();
        auto& mutable_idx = db.get_mutable_index<prediction_market_index>();
        mutable_idx.set_next_id(prediction_market_object_id_type(id_val));

        db.create<prediction_market_object>([&](prediction_market_object& obj) {
            obj.creator = v["creator"].as<account_name_type>();
            from_string(obj.title, v["title"].as_string());
            from_string(obj.description, v["description"].as_string());
            obj.total_stake = v["total_stake"].as<asset>();
            obj.resolution_time = v["resolution_time"].as<fc::time_point_sec>();
            obj.status = v["status"].as_uint64();
        });
        ++count;
    }
    return count;
}
```

### Step 5: Implement Hardfork Logic

Add the hardfork case in `libraries/chain/database.cpp` in `apply_hardfork()`:

```cpp
void database::apply_hardfork(uint32_t hardfork) {
    switch (hardfork) {
        // ... existing cases ...

        case CHAIN_HARDFORK_10:  // NEW hardfork
        {
            // Initialize any required state for prediction markets
            // This runs when the hardfork activates

            // Example: Set initial parameters, migrate data, etc.
            const auto& props = get_dynamic_global_properties();
            modify(props, [&](dynamic_global_property_object& p) {
                // Set any new parameters
            });

            break;
        }

        default:
            break;
    }

    // Update hardfork property object
    modify(get_hardfork_property_object(), [&](hardfork_property_object& hfp) {
        hfp.processed_hardforks.push_back(_hardfork_times[hardfork]);
        hfp.last_hardfork = hardfork;
        hfp.current_hardfork_version = _hardfork_versions[hardfork];
    });
}
```

### Step 6: Define Hardfork Constants

Add hardfork constants to the appropriate header (e.g., `libraries/chain/include/graphene/chain/hardfork.hpp` or hardfork.d files):

```cpp
#ifndef CHAIN_HARDFORK_10
#define CHAIN_HARDFORK_10 10
#define CHAIN_HARDFORK_10_TIME 1893456000  // Unix timestamp
#define CHAIN_HARDFORK_10_VERSION hardfork_version( 1, 10, 0 )
#endif
```

Update `CHAIN_NUM_HARDFORKS` if necessary.

## Snapshot Compatibility

### Forward Compatibility (Old Node → New Snapshot)

When an older node loads a snapshot created by a newer node:

1. **Unknown objects are silently ignored** - The snapshot loader uses conditional checks:
   ```cpp
   if (state.contains("prediction_market")) {
       // import prediction markets
   }
   ```
   If the old node doesn't check for `prediction_market`, it simply won't be imported.

2. **Hardfork state is preserved** - The `hardfork_property_object` contains:
   - `last_hardfork` - highest applied hardfork number
   - `current_hardfork_version` - current network version
   - `processed_hardforks` - timestamps of applied hardforks

3. **Node behavior** - The old node will:
   - Successfully load the snapshot
   - Continue operating at its known hardfork level
   - Reject blocks containing unknown operations
   - Eventually fall out of consensus when the new hardfork activates

### Backward Compatibility (New Node → Old Snapshot)

When a newer node loads an older snapshot:

1. **Missing objects are handled gracefully** - All object imports are conditional:
   ```cpp
   if (state.contains("prediction_market")) {
       // import prediction markets
   }
   // If not present, the index remains empty
   ```

2. **Hardfork initialization** - After loading, `initialize_hardforks()` sets up the hardfork schedule. When the hardfork time arrives, `apply_hardfork()` will:
   - Initialize any required state
   - Create initial objects if needed
   - Update the hardfork property

3. **Node behavior** - The new node will:
   - Successfully load the older snapshot
   - Have empty indexes for new object types
   - Apply the hardfork at the scheduled time
   - Participate normally in consensus after the hardfork

## Compatibility Matrix

| Scenario | Snapshot Contains | Node Version | Result |
|----------|-------------------|--------------|--------|
| Old node loads new snapshot | Unknown objects | Older | ✅ Ignores unknown objects, continues at its hardfork |
| New node loads old snapshot | Missing new objects | Newer | ✅ Initializes empty indexes, applies hardfork when triggered |
| Old node processes new HF blocks | N/A | Older | ❌ Rejects unknown operations (expected consensus split) |
| New node processes old blocks | N/A | Newer | ✅ Validates normally, hardfork not yet active |

## Best Practices

### 1. Object Type Ordering

Always add new object types at the **end** of the `object_type` enum to avoid reordering existing types, which would break binary compatibility.

### 2. Optional vs Required Objects

Consider whether new objects should be:
- **Critical**: Required for consensus (must be in snapshot)
- **Important**: Needed for full functionality (should be in snapshot)
- **Optional**: Can be reconstructed (nice to have in snapshot)

### 3. Hardfork State Validation

Nodes should validate the hardfork state after loading a snapshot:

```cpp
const auto& hfp = db.get_hardfork_property_object();
if (hfp.last_hardfork > CHAIN_NUM_HARDFORKS) {
    wlog("Snapshot requires hardfork ${hf} which this node doesn't support",
         ("hf", hfp.last_hardfork));
}
```

### 4. Snapshot Versioning

Consider incrementing `SNAPSHOT_FORMAT_VERSION` for breaking changes:

```cpp
// In snapshot_types.hpp
static const uint32_t SNAPSHOT_FORMAT_VERSION = 2;  // Increment for breaking changes
```

### 5. Testing Compatibility

Test these scenarios:
1. Create snapshot with new objects, load on old node
2. Create snapshot without new objects, load on new node
3. Verify hardfork applies correctly after loading old snapshot
4. Verify old node rejects new hardfork blocks appropriately

## Example: Complete Hardfork Implementation

Here's a minimal example of adding a simple counter object in hardfork 10:

**1. Object Definition (`counter_object.hpp`):**
```cpp
#pragma once
#include <graphene/chain/chain_object_types.hpp>

namespace graphene { namespace chain {

class counter_object : public object<counter_object_type, counter_object> {
public:
    template<typename Constructor, typename Allocator>
    counter_object(Constructor&& c, allocator<Allocator>) { c(*this); }

    id_type id;
    account_name_type owner;
    uint64_t count = 0;
};

typedef multi_index_container<
    counter_object,
    indexed_by<
        ordered_unique<tag<by_id>, member<counter_object, counter_object_id_type, &counter_object::id>>,
        ordered_unique<tag<by_owner>, member<counter_object, account_name_type, &counter_object::owner>>
    >,
    allocator<counter_object>
> counter_index;

}}

FC_REFLECT((graphene::chain::counter_object), (id)(owner)(count))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::counter_object, graphene::chain::counter_index)
```

**2. Hardfork Initialization:**
```cpp
case CHAIN_HARDFORK_10:
{
    // Initialize counters for existing accounts
    const auto& acc_idx = get_index<account_index>().indices();
    for (const auto& acc : acc_idx) {
        create<counter_object>([&](counter_object& c) {
            c.owner = acc.name;
            c.count = 0;
        });
    }
    break;
}
```

**3. Snapshot Export/Import:**
```cpp
// Export
EXPORT_INDEX(counter_index, counter_object, "counter")

// Import
if (state.contains("counter")) {
    auto n = detail::import_simple_objects<counter_object, counter_index>(
        db, state["counter"].get_array());
    ilog("Imported ${n} counters", ("n", n));
}
```

## Related Documentation

- [Snapshot Plugin](snapshot-plugin.md) - General snapshot functionality
- [Plugins](plugins.md) - Plugin architecture and development
- [Data Types](data-types.md) - Protocol data types and serialization
