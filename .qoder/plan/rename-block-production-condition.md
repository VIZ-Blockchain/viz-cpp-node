# Rename: `block_production_condition` → `block_validation_condition`

## Overview

Witnesses in VIZ do not *produce* blocks autonomously — they *validate* the chain state and sign the next block when it is their scheduled turn. The term "production" is misleading; "validation" better reflects the semantic role. This plan renames the namespace, the enum type, and all enum values that use the old name across the codebase.

---

## Scope of Changes

### Source files (code changes required)

| File | What changes |
|------|-------------|
| [plugins/witness/include/graphene/plugins/witness/witness.hpp](../../plugins/witness/include/graphene/plugins/witness/witness.hpp) | Rename namespace `block_production_condition` → `block_validation_condition`; rename enum `block_production_condition_enum` → `block_validation_condition_enum`; rename enum value `exception_producing_block` → `exception_validating_block` |
| [plugins/witness/witness.cpp](../../plugins/witness/witness.cpp) | Update all uses of the namespace, the enum type, all enum value references, method names `block_production_loop` → `block_validation_loop`, `maybe_produce_block` → `maybe_validate_block` |

### Documentation files (text search-and-replace)

| File | Action |
|------|--------|
| [.qoder/repowiki/en/content/Witness.md](./../repowiki/en/content/Witness.md) | Update all references to old names |
| [.qoder/research/consensus-emergency-recovery.md](./../research/consensus-emergency-recovery.md) | Update code snippets and prose |
| [.qoder/docs/fork-collision-hardfork-proposal.md](./../docs/fork-collision-hardfork-proposal.md) | Update code snippets |
| [.qoder/docs/consensus-emergency-params.md](./../docs/consensus-emergency-params.md) | Update code snippets |
| [.qoder/plans/Fork_Collision_Resolution_Fix_24537a6e.md](./../plans/Fork_Collision_Resolution_Fix_24537a6e.md) | Update code snippets |

---

## Detailed Changes

### 1. `witness.hpp` — Namespace and enum rename

**File:** `plugins/witness/include/graphene/plugins/witness/witness.hpp`

```cpp
// BEFORE
namespace block_production_condition {
    enum block_production_condition_enum {
        produced                  = 0,
        not_synced                = 1,
        not_my_turn               = 2,
        not_time_yet              = 3,
        no_private_key            = 4,
        low_participation         = 5,
        lag                       = 6,
        consecutive               = 7,
        exception_producing_block = 8,
        fork_collision            = 9,
        minority_fork             = 10
    };
}

// AFTER
namespace block_validation_condition {
    enum block_validation_condition_enum {
        produced                   = 0,
        not_synced                 = 1,
        not_my_turn                = 2,
        not_time_yet               = 3,
        no_private_key             = 4,
        low_participation          = 5,
        lag                        = 6,
        consecutive                = 7,
        exception_validating_block = 8,
        fork_collision             = 9,
        minority_fork              = 10
    };
}
```

### 2. `witness.cpp` — All references

**File:** `plugins/witness/witness.cpp`

Rename map (search → replace):

| Old | New |
|-----|-----|
| `block_production_condition::block_production_condition_enum` | `block_validation_condition::block_validation_condition_enum` |
| `block_production_condition::` | `block_validation_condition::` |
| `block_production_condition_enum` | `block_validation_condition_enum` |
| `exception_producing_block` | `exception_validating_block` |
| `block_production_loop` (method name) | `block_validation_loop` |
| `maybe_produce_block` (method name) | `maybe_validate_block` |

> **Note:** `maybe_produce_block` appears in both the forward declaration (line ~102–104) and the definition (line ~660). Both must be updated together or the build will fail.

### 3. Documentation files — Text substitution

For each doc file listed in the scope table, apply the same rename map as a plain-text search-and-replace. No structural changes to the documents are needed — only identifier names inside code blocks and prose references.

---

## Enum Values That Stay Unchanged

These enum values already describe the *reason for not validating* (or the outcome), not the act of production, so they need no renaming:

- `produced` — outcome, keep as-is
- `not_synced`
- `not_my_turn`
- `not_time_yet`
- `no_private_key`
- `low_participation`
- `lag`
- `consecutive`
- `fork_collision`
- `minority_fork`

Only `exception_producing_block` → `exception_validating_block` changes because the word "producing" appears in it.

---

## Implementation Steps

1. **`witness.hpp`** — rename namespace, enum type, and `exception_producing_block`.
2. **`witness.cpp`** — rename all namespace-qualified references, both method declarations and definitions.
3. **Build** — compile to confirm zero errors before touching docs.
4. **Docs** — apply text substitution to the five documentation files.

---

## Risk

- Low. This is a pure rename with no behavioral change.
- The numeric values of enum members are preserved, so any serialized state that stores these as integers (e.g. log output) is unaffected.
- No public API or wire protocol uses this enum — it is internal to the witness plugin.
