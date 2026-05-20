# Architecture Overview

VIZ Ledger is implemented as a modular C++ daemon (`vizd`) composed of layered libraries and a plugin system. This page describes the structural layers, design patterns, and component interactions.

---

## Layered Structure

```
┌─────────────────────────────────────────────────────────────────┐
│  Programs                                                       │
│  vizd (node daemon)          cli_wallet (CLI wallet)            │
├─────────────────────────────────────────────────────────────────┤
│  Plugins                                                        │
│  chain │ validator │ p2p │ webserver │ json_rpc │ database_api  │
│  account_history │ snapshot │ committee_api │ invite_api │ ...   │
├─────────────────────────────────────────────────────────────────┤
│  Core Libraries                                                 │
│  libraries/chain     — blockchain state machine, fork db        │
│  libraries/protocol  — operation types, transactions            │
│  libraries/network   — peer-to-peer messaging                   │
│  libraries/api       — shared API property types                │
│  libraries/wallet    — transaction building helpers             │
│  libraries/time      — NTP-aware time utilities                 │
└─────────────────────────────────────────────────────────────────┘
```

### Libraries

| Library | Key file | Purpose |
|---------|----------|---------|
| `libraries/chain` | `database.hpp` | Blockchain state: accounts, blocks, objects, fork DB, shared memory |
| `libraries/protocol` | `operations.hpp` | `static_variant` union of all 64+ operation types |
| `libraries/network` | `node.hpp` | P2P engine: peer connections, sync, message propagation |
| `libraries/api` | `chain_api_properties.hpp` | Shared types returned by API plugins |
| `libraries/wallet` | `wallet.hpp` | Remote node API calls, transaction construction |
| `libraries/time` | `time.hpp` | NTP synchronisation for block slot timing |

### Plugins

Plugins are registered with the `AppBase` framework at startup and implement lifecycle hooks (`plugin_initialize`, `plugin_startup`, `plugin_shutdown`).

| Plugin | Role |
|--------|------|
| `chain` | Opens the database, validates and applies blocks/transactions |
| `validator` | Produces blocks on schedule (Fair-DPOS), manages NTP and watchdog |
| `p2p` | Manages peer connections, syncs blocks, propagates transactions |
| `webserver` | HTTP and WebSocket server for API access |
| `json_rpc` | Routes JSON-RPC requests to registered API plugins |
| `database_api` | Read queries: accounts, blocks, transactions, globals |
| `account_by_key` | Lookup accounts by public key |
| `validator_api` | Validator schedule and signing key queries |

---

## Design Patterns

### Event-Driven Observer (Signals)

The chain `Database` emits `fc::signal` events at key points. Plugins subscribe to these signals to implement indexing, history, and notifications without coupling to the core chain.

```
push_block() / push_transaction()
    │
    ├── pre_apply_operation  ──► subscriber plugins (pre-hooks)
    ├── [evaluator applies state change]
    ├── post_apply_operation ──► subscriber plugins (post-hooks)
    └── applied_block        ──► subscriber plugins (block finalized)
```

### Factory + Strategy (Evaluator Registry)

Every operation type has a dedicated **evaluator** class. The `EvaluatorRegistry` maps operation type IDs to evaluator instances. When a transaction is applied:

1. `Database` extracts the operation type tag from the `static_variant`.
2. Registry returns the registered evaluator.
3. Evaluator's `do_apply(op)` mutates the database state.

Adding a new operation requires only registering a new evaluator — no changes to the dispatch loop.

### Plugin-Based Architecture (AppBase)

`vizd/main.cpp` registers all plugins with `AppBase` before calling `app().exec()`. Each plugin declares its options and dependencies; AppBase handles ordering and lifecycle.

```
main() ──► register_plugin<chain>()
       ──► register_plugin<validator>()
       ──► register_plugin<p2p>()
       ──► ...
       ──► app().initialize(argc, argv)
       ──► app().startup()
       ──► app().exec()   ← event loop runs until SIGINT/SIGTERM
```

### MVC Separation

| Layer | Component | Responsibility |
|-------|-----------|---------------|
| Data | `libraries/chain/database` | State persistence, validation, signals |
| Control | Plugins (`chain`, `validator`, `p2p`) | Lifecycle, block/tx acceptance, coordination |
| View | API plugins (`database_api`, `account_history`, …) | Read-only query endpoints |

---

## Data Flow: Incoming Block

```
Peer (P2P) ──► p2p_plugin::handle_block()
              ──► chain_plugin::accept_block()
                  ──► database::push_block()
                      ├── validate block header, signature
                      ├── for each transaction:
                      │     ├── validate authorities
                      │     └── evaluator->do_apply(operation)
                      ├── process virtual operations (rewards, cashouts)
                      ├── emit applied_block signal
                      └── update fork DB / LIB
```

## Data Flow: API Request

```
Client (HTTP/WS) ──► webserver_plugin
                  ──► json_rpc_plugin::call()
                      ──► registry.find_api_method(api, method)
                          ──► database_api / account_history / ...
                              ──► database::get_*(...)
                                  ──► return JSON result
```

---

## Concurrency Model

| Concern | Approach |
|---------|---------|
| Write operations | Single write thread (optional `single-write-thread` config) |
| Read operations | Multiple concurrent readers via `chainbase` shared memory |
| P2P I/O | Dedicated `boost::asio::io_service` thread pool |
| Block production timer | Isolated `io_service` + thread in validator plugin to prevent P2P delays |
| RPC serving | Configurable thread pool (`rpc-endpoint-thread-pool-size`) |

The most important invariant: **only one thread may hold a write lock on the database at a time.** All evaluators and block-processing code runs under this lock.

---

## Shared Memory Database

State is stored in a memory-mapped file (`shared_memory.bin`) managed by `chainbase`. Key properties:

- All object indexes (accounts, blocks, content, validators, …) live in this file.
- The file is resized incrementally when free space drops below a threshold.
- On a clean shutdown the file is consistent; a crash may require replay from the block log.
- Nodes can export a **snapshot** of the shared memory state at a block boundary — see [Snapshots](../storage/snapshots.md).

---

## Source Map

| File | Role in architecture |
|------|---------------------|
| `programs/vizd/main.cpp` | Plugin registration and startup |
| `libraries/chain/include/graphene/chain/database.hpp` | Core database interface and signals |
| `libraries/chain/include/graphene/chain/evaluator_registry.hpp` | Factory for operation evaluators |
| `libraries/network/include/graphene/network/node.hpp` | P2P node delegate interface |
| `libraries/protocol/include/graphene/protocol/operations.hpp` | Operation type union |
| `plugins/chain/plugin.cpp` | Chain plugin: block/tx acceptance |
| `plugins/json_rpc/plugin.cpp` | JSON-RPC dispatch |
