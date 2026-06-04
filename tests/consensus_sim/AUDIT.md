# Consensus Harness API Audit

Audited commit: worktree `test/consensus-harness` (mirrors master at `36b0aa8`)
Audit date: 2026-05-01

---

## TL;DR

**Issue found: wall-clock read inside transaction evaluator writes consensus state;
revise Task 3 (harness design) to either avoid `account_buy_operation` in test
fixtures or patch/skip the evaluator for deterministic testing.**

All other plan-writer claims verified correct. Proceed with the plan subject to
the mitigation note in the "wall-clock reads" section below.

---

## database lifecycle

Source: `libraries/chain/include/graphene/chain/database.hpp`

### Constructor / destructor

```cpp
database();   // line 39 — default constructor, no arguments
~database();  // line 41
```

### `open`

```cpp
// line 106
void open(
    const fc::path &data_dir,
    const fc::path &shared_mem_dir,
    uint64_t initial_supply  = CHAIN_INIT_SUPPLY,   // 50 000 000 000 (3-decimal tokens)
    uint64_t shared_file_size = 0,
    uint32_t chainbase_flags  = 0
);
```

Variant: `open_from_snapshot(data_dir, shared_mem_dir, initial_supply, shared_file_size, chainbase_flags)` — no defaults, used for DLT/snapshot restore.

### `close` / `wipe`

```cpp
void close(bool rewind = true);   // line 176
void wipe(const fc::path &data_dir, const fc::path &shared_mem_dir, bool include_blocks);  // line 174
```

### Block production / application

```cpp
// line 263
bool push_block(const signed_block &b, uint32_t skip = skip_nothing);

// line 271
bool _push_block(const signed_block &b, uint32_t skip);

// lines 281-286
signed_block generate_block(
    const fc::time_point_sec when,
    const account_name_type &witness_owner,
    const fc::ecc::private_key &block_signing_private_key,
    uint32_t skip
);

// lines 288-293
signed_block _generate_block(
    const fc::time_point_sec when,
    const account_name_type &witness_owner,
    const fc::ecc::private_key &block_signing_private_key,
    uint32_t skip
);
```

Note: `generate_block` has **no default for `skip`** in the declaration (plan-writer
implied defaults exist — they do not; caller must supply the value explicitly).

### Accessors

```cpp
time_point_sec  head_block_time() const;           // line 463 — returns dgp.time (state-derived)
uint32_t        head_block_num() const;             // line 465
block_id_type   head_block_id() const;             // line 467
uint32_t        last_non_undoable_block_num() const; // line 469 — wraps dgp.last_irreversible_block_num
fork_database&       get_fork_db();                // line 517
const fork_database& get_fork_db() const;          // line 521
```

**Naming drift vs. plan-writer claim 1:** The plan references
`last_irreversible_block_num()` as a public method. No such method exists. The
public accessor is named `last_non_undoable_block_num()` (line 469) and delegates
to `get_dynamic_global_properties().last_irreversible_block_num`. Use
`last_non_undoable_block_num()` in harness code.

---

## init constants

Source: `libraries/protocol/include/graphene/protocol/config.hpp`

| Macro | Value | Notes |
|---|---|---|
| `CHAIN_INIT_SUPPLY` | `int64_t(50000000000)` | 50 000 000.000 VIZ (3 decimals) |
| `CHAIN_INITIATOR_NAME` | `"viz"` | genesis account name (plan called it `CHAIN_INIT_MINER_NAME`) |
| `CHAIN_NUM_INITIATORS` | `0` | no extra genesis miners besides the initiator |
| `CHAIN_INITIATOR_PUBLIC_KEY_STR` | `"VIZ6MyX5QiXAXRZk7SYCiqpi6Mtm8UbHWDFSV8HPpt7FJyahCnc2T"` | canonical genesis key |
| `CHAIN_BLOCK_INTERVAL` | `3` (seconds) | slot duration |

**Naming drift vs. plan-writer claims 2 & 3:**
- Plan used `CHAIN_INIT_MINER_NAME` — actual macro is `CHAIN_INITIATOR_NAME`.
- Plan used `CHAIN_NUM_INIT_MINERS` — actual macro is `CHAIN_NUM_INITIATORS` (value `0`).
- Plan used `CHAIN_INIT_PRIVATE_KEY` — no such macro exists. The public key is
  exposed as `CHAIN_INITIATOR_PUBLIC_KEY_STR`; the matching private key
  (`5JabcrvaLnBTCkCVFX5r4rmeGGfuJuVp4NAKRNLTey6pxhRQmf4`) is only in a comment
  at line 35. The harness must hard-code or derive the private key directly.

---

## wall-clock reads

Grep output (`grep -rn "fc::time_point::now()"` on `libraries/chain` and
`libraries/protocol`):

### Non-consensus (safe for harness)

| File:line | Context | Classification |
|---|---|---|
| `database.cpp:208,316` | `_node_startup_time = fc::time_point::now()` in `open()` / `open_from_snapshot()` | non-consensus: used only for emergency-consensus startup-delay guard |
| `database.cpp:209,218,222,296,317,347,363,392,432,448,508,541` | Timing of `open()`, `reindex()`, and `reindex_from_dlt()` operations | non-consensus: elapsed-time logging only |
| `database.cpp:1193` | Rate-limit on `_maybe_warn_multiple_production` log warning | non-consensus: uses static vars, affects only log output |
| `database.cpp:4258` | Flush-block RNG seed (`_next_flush_block`) | non-consensus: only controls when `chainbase::flush()` is called; does not affect block content or chain state |
| `database.cpp:4689` | Emergency-consensus startup-delay check | non-consensus: reads `_node_startup_time` (set in `open()`) to guard against premature emergency activation; never alters block content |
| `db_with.hpp:38,43,64` | `CHAIN_PENDING_TRANSACTION_EXECUTION_LIMIT` enforcement in `pending_transactions_restorer` | non-consensus: only limits how many pending txs are re-applied after a pop; affects _which_ transactions end up in the mempool, not in a finalized block |

### **CONSENSUS — P0 FLAG**

| File:line | Context | Risk |
|---|---|---|
| `chain_evaluator.cpp:2156` | Inside `account_buy_operation` evaluator (HF11+ auction path) — `time_point_sec expand_start_time = fc::time_point::now() + CHAIN_ACCOUNT_AUCTION_EXTENSION_TIME;` then `a.account_on_sale_start_time = std::max(a.account_on_sale_start_time, expand_start_time);` | **CONSENSUS**: this wall-clock timestamp is written into `account_object::account_on_sale_start_time`, which is persistent chain state. Two nodes applying the same bid transaction at different wall-clock times produce different `account_on_sale_start_time` values, making their state diverge. |

**Risk 1 has materialized** — but only in a narrow, avoidable path. The harness
can avoid it by never including `account_buy_operation` (auction bids) in test
transactions, or by patching the evaluator to use `head_block_time()` instead of
`fc::time_point::now()` (which would be the correct fix). This does not block
the harness for most consensus scenarios.

> Note: `database.cpp:3958` (`time_point_sec genesis_time = fc::time_point::now()`)
> appears in `init_genesis()` and also writes to `dynamic_global_property_object`
> and `hardfork_property_object`. However it is **immediately overwritten** when
> block 1 is applied (`_apply_block` lines 4296-4304: `genesis_time =
> next_block.timestamp - CHAIN_BLOCK_INTERVAL`). So the wall-clock value is
> ephemeral and does not affect post-genesis chain state.

---

## `_generate_block` witness-key behavior (Risk 3 check)

Source: `libraries/chain/database.cpp` lines 1539–1727.

1. **Looks up witness by `witness_owner` parameter** (line 1564: `get_witness(witness_owner)`) — does not reference any globally-configured key.
2. **Signs with `block_signing_private_key` parameter** (line 1714-1715, inside `if (!(skip & skip_witness_signature))`).
3. **Key-match assertion** at lines 1585-1587:
   ```cpp
   if (!(skip & skip_witness_signature))
       FC_ASSERT(witness_obj.signing_key == block_signing_private_key.get_public_key());
   ```
   This assertion fires only if `skip_witness_signature` is NOT set.
   **Implication for harness**: when calling `_generate_block` (or `generate_block`),
   either (a) register the witness on-chain with the same public key as the
   private key you pass, or (b) pass `skip | skip_witness_signature` to bypass
   the check. Option (a) is strongly preferred for realistic consensus simulation.

**Risk 3 has NOT materialized.** `_generate_block` is fully parameterized on
witness identity and signing key.

---

## `chainbase` multi-instance isolation (Risk 2 check)

Source: `thirdparty/chainbase/include/chainbase/chainbase.hpp` lines 837–1327.

Chainbase `database` is a plain C++ class with no static data members. All state
lives in per-instance members:
- `_segment` — `boost::interprocess::unique_ptr<managed_mapped_file>` (per-path)
- `_mutex`, `_flock`, `_index_list`, `_index_map`, `_index_types` — all instance members
- No singletons, no process-wide registrations found

The file-lock `_flock` is per-path (it locks the mmap file), so two `database`
instances pointing to different directories coexist without contention.

**Risk 2 has NOT materialized.** N independent `database` instances in one
process are safe as long as each has a distinct `shared_mem_dir`.

---

## Drift summary vs. plan-writer pre-grounded facts

| Claim | Status | Detail |
|---|---|---|
| 1. `open()` signature at hpp:106 | **Correct** | Signature matches exactly |
| 2. `push_block()` at line 263 | **Correct** | Matches |
| 3. `generate_block()` at line 281 | **Correct** | Matches (note: no default for `skip`) |
| 4. `head_block_time()` is state-derived | **Correct** | Returns `dgp.time` |
| 5. All `fc::time_point::now()` non-consensus | **INCORRECT** | `chain_evaluator.cpp:2156` is consensus-critical |
| 6. No `genesis_state`; genesis via `init_genesis()` in `open()` | **Correct** | Confirmed |

Additional naming drifts (not in the 6 claims but relevant to downstream tasks):
- `CHAIN_INIT_MINER_NAME` → actual: `CHAIN_INITIATOR_NAME`
- `CHAIN_NUM_INIT_MINERS` → actual: `CHAIN_NUM_INITIATORS` (value `0`)
- `CHAIN_INIT_PRIVATE_KEY` → does not exist as a macro; private key is comment-only
- `last_irreversible_block_num()` public method → actual: `last_non_undoable_block_num()`
