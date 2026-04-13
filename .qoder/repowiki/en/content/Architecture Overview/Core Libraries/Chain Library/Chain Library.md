# Chain Library

<cite>
**Referenced Files in This Document**
- [database.hpp](file://libraries/chain/include/graphene/chain/database.hpp)
- [database.cpp](file://libraries/chain/database.cpp)
- [chain_object_types.hpp](file://libraries/chain/include/graphene/chain/chain_object_types.hpp)
- [chain_objects.hpp](file://libraries/chain/include/graphene/chain/chain_objects.hpp)
- [fork_database.hpp](file://libraries/chain/include/graphene/chain/fork_database.hpp)
- [fork_database.cpp](file://libraries/chain/fork_database.cpp)
- [block_log.hpp](file://libraries/chain/include/graphene/chain/block_log.hpp)
- [block_log.cpp](file://libraries/chain/block_log.cpp)
- [dlt_block_log.hpp](file://libraries/chain/include/graphene/chain/dlt_block_log.hpp)
- [dlt_block_log.cpp](file://libraries/chain/dlt_block_log.cpp)
- [account_object.hpp](file://libraries/chain/include/graphene/chain/account_object.hpp)
- [witness_objects.hpp](file://libraries/chain/include/graphene/chain/witness_objects.hpp)
- [committee_objects.hpp](file://libraries/chain/include/graphene/chain/committee_objects.hpp)
- [transaction_object.hpp](file://libraries/chain/include/graphene/chain/transaction_object.hpp)
- [evaluator.hpp](file://libraries/chain/include/graphene/chain/evaluator.hpp)
- [chain_evaluator.hpp](file://libraries/chain/include/graphene/chain/chain_evaluator.hpp)
- [operation_notification.hpp](file://libraries/chain/include/graphene/chain/operation_notification.hpp)
- [plugin.cpp](file://plugins/chain/plugin.cpp)
- [snapshot_plugin.cpp](file://plugins/snapshot/plugin.cpp)
</cite>

## Update Summary
**Changes Made**
- Added comprehensive documentation for snapshot loading support and DLT mode functionality
- Updated chain plugin startup sequence to cover snapshot import and creation phases
- Enhanced error handling documentation for snapshot operations
- Added DLT rolling block log implementation details
- Updated database initialization process to include snapshot mode

## Table of Contents
1. [Introduction](#introduction)
2. [Project Structure](#project-structure)
3. [Core Components](#core-components)
4. [Architecture Overview](#architecture-overview)
5. [Detailed Component Analysis](#detailed-component-analysis)
6. [Snapshot Loading and DLT Mode](#snapshot-loading-and-dlt-mode)
7. [Dependency Analysis](#dependency-analysis)
8. [Performance Considerations](#performance-considerations)
9. [Troubleshooting Guide](#troubleshooting-guide)
10. [Conclusion](#conclusion)
11. [Appendices](#appendices)

## Introduction
This document describes the Chain Library, the core blockchain state management system. It explains how the database persists blockchain state, validates blocks and transactions, resolves forks, and stores blocks efficiently. The library now includes enhanced snapshot loading capabilities, DLT (Data Ledger Technology) mode support, and improved error handling during snapshot operations. It also documents the data model (objects and indices), the evaluator system for operation processing, and the observer pattern used for event-driven notifications. Practical examples and performance optimization techniques are included to help developers integrate and operate the Chain Library effectively.

## Project Structure
The Chain Library is organized around a central database class that orchestrates:
- State persistence and indexing via ChainBase
- Fork handling with a fork database
- Block log for durable storage
- DLT rolling block log for selective block retention
- Object model definitions for accounts, witnesses, committee requests, and more
- Evaluator registry for operation processing
- Observer signals for event-driven integrations
- Snapshot loading and state restoration capabilities

```mermaid
graph TB
subgraph "Chain Core"
DB["database.hpp/.cpp"]
FDB["fork_database.hpp/.cpp"]
BLK["block_log.hpp/.cpp"]
DLT["dlt_block_log.hpp/.cpp"]
end
subgraph "Object Model"
OT["chain_object_types.hpp"]
CO["chain_objects.hpp"]
AO["account_object.hpp"]
WO["witness_objects.hpp"]
CT["committee_objects.hpp"]
TO["transaction_object.hpp"]
end
subgraph "Processing"
EV["evaluator.hpp"]
CEV["chain_evaluator.hpp"]
ON["operation_notification.hpp"]
SNAP["snapshot_plugin.cpp"]
CHAINPLUG["chain plugin.cpp"]
end
DB --> FDB
DB --> BLK
DB --> DLT
DB --> OT
DB --> CO
DB --> AO
DB --> WO
DB --> CT
DB --> TO
DB --> EV
DB --> CEV
DB --> ON
CHAINPLUG --> SNAP
CHAINPLUG --> DB
```

**Diagram sources**
- [database.hpp:36-561](file://libraries/chain/include/graphene/chain/database.hpp#L36-L561)
- [database.cpp:206-456](file://libraries/chain/database.cpp#L206-L456)
- [fork_database.hpp:53-125](file://libraries/chain/include/graphene/chain/fork_database.hpp#L53-L125)
- [fork_database.cpp:1-245](file://libraries/chain/fork_database.cpp#L1-L245)
- [block_log.hpp:1-200](file://libraries/chain/include/graphene/chain/block_log.hpp#L1-L200)
- [block_log.cpp:230-302](file://libraries/chain/block_log.cpp#L230-L302)
- [dlt_block_log.hpp:35-75](file://libraries/chain/include/graphene/chain/dlt_block_log.hpp#L35-L75)
- [dlt_block_log.cpp:161-382](file://libraries/chain/dlt_block_log.cpp#L161-L382)
- [chain_object_types.hpp:44-146](file://libraries/chain/include/graphene/chain/chain_object_types.hpp#L44-L146)
- [chain_objects.hpp:20-226](file://libraries/chain/include/graphene/chain/chain_objects.hpp#L20-L226)
- [account_object.hpp:20-143](file://libraries/chain/include/graphene/chain/account_object.hpp#L20-L143)
- [witness_objects.hpp:27-132](file://libraries/chain/include/graphene/chain/witness_objects.hpp#L27-L132)
- [committee_objects.hpp:15-47](file://libraries/chain/include/graphene/chain/committee_objects.hpp#L15-L47)
- [transaction_object.hpp:19-56](file://libraries/chain/include/graphene/chain/transaction_object.hpp#L19-L56)
- [evaluator.hpp:11-45](file://libraries/chain/include/graphene/chain/evaluator.hpp#L11-L45)
- [chain_evaluator.hpp:14-79](file://libraries/chain/include/graphene/chain/chain_evaluator.hpp#L14-L79)
- [operation_notification.hpp:11-26](file://libraries/chain/include/graphene/chain/operation_notification.hpp#L11-L26)
- [plugin.cpp:360-432](file://plugins/chain/plugin.cpp#L360-L432)
- [snapshot_plugin.cpp:980-1200](file://plugins/snapshot/plugin.cpp#L980-L1200)

**Section sources**
- [database.hpp:36-561](file://libraries/chain/include/graphene/chain/database.hpp#L36-L561)
- [database.cpp:206-456](file://libraries/chain/database.cpp#L206-L456)

## Core Components
- Database: Central state manager that opens/closes the chain, replays history, pushes blocks and transactions, manages undo sessions, and emits observer signals. Now includes DLT mode support and snapshot loading capabilities.
- Fork Database: Maintains a tree of candidate blocks, supports branch resolution, and selects the longest chain.
- Block Log: Provides durable, memory-mapped storage for blocks and an index for fast random access.
- DLT Rolling Block Log: Enhanced block storage system that maintains only a rolling window of recent blocks for selective retention.
- Object Model: Defines all persistent objects (accounts, witnesses, committee requests, transactions, etc.) and their multi-index containers.
- Evaluator System: Registry and base classes for operation processing with a standardized interface.
- Observer Pattern: Signals for pre/post operation application, applied block, and transaction events.
- Snapshot Plugin: Handles state restoration from snapshots and manages snapshot lifecycle.

Key responsibilities:
- Persistence: ChainBase-backed storage with configurable shared memory sizing and periodic revision alignment.
- Validation: Block and transaction validation with configurable skip flags for reindexing and specialized scenarios.
- Consensus: Fork selection and irreversible block updates.
- Eventing: Notifications for plugins and observers.
- State Restoration: Snapshot loading for rapid node startup and state recovery.
- DLT Operations: Selective block retention for compliance and archival purposes.

**Section sources**
- [database.hpp:56-110](file://libraries/chain/include/graphene/chain/database.hpp#L56-L110)
- [database.cpp:206-350](file://libraries/chain/database.cpp#L206-L350)
- [fork_database.hpp:53-125](file://libraries/chain/include/graphene/chain/fork_database.hpp#L53-L125)
- [block_log.hpp:1-200](file://libraries/chain/include/graphene/chain/block_log.hpp#L1-L200)
- [dlt_block_log.hpp:35-75](file://libraries/chain/include/graphene/chain/dlt_block_log.hpp#L35-L75)
- [evaluator.hpp:11-45](file://libraries/chain/include/graphene/chain/evaluator.hpp#L11-L45)

## Architecture Overview
The Chain Library composes several subsystems with enhanced snapshot and DLT capabilities:
- database orchestrates state transitions and emits signals, now supporting DLT mode and snapshot operations
- fork_database maintains candidate chains
- block_log provides durable storage
- dlt_block_log provides selective block retention in DLT mode
- object model defines schema and indices
- evaluators apply operations atomically within undo sessions
- snapshot plugin handles state restoration and validation

```mermaid
sequenceDiagram
participant App as "Caller"
participant DB as "database"
participant FDB as "fork_database"
participant BLK as "block_log"
participant DLT as "dlt_block_log"
participant OBS as "Observers"
App->>DB : push_block(signed_block)
DB->>FDB : push_block()
alt fork head advanced
DB->>DB : fetch_branch_from()
loop pop blocks until forked
DB->>DB : pop_block()
end
loop push new fork blocks
DB->>DB : apply_block()
DB->>OBS : notify_applied_block()
end
else same fork
DB->>DB : apply_block()
end
DB->>BLK : append()
DB->>DLT : append() (if DLT mode)
DB->>OBS : notify_applied_block()
```

**Diagram sources**
- [database.cpp:800-925](file://libraries/chain/database.cpp#L800-L925)
- [fork_database.cpp:33-90](file://libraries/chain/fork_database.cpp#L33-L90)
- [block_log.cpp:253-257](file://libraries/chain/block_log.cpp#L253-L257)
- [dlt_block_log.cpp:211-230](file://libraries/chain/dlt_block_log.cpp#L211-L230)

**Section sources**
- [database.cpp:800-925](file://libraries/chain/database.cpp#L800-L925)
- [fork_database.cpp:168-210](file://libraries/chain/fork_database.cpp#L168-L210)
- [block_log.cpp:253-257](file://libraries/chain/block_log.cpp#L253-L257)
- [dlt_block_log.cpp:211-230](file://libraries/chain/dlt_block_log.cpp#L211-L230)

## Detailed Component Analysis

### Database: State Management, Validation, and Events
The database class extends ChainBase and encapsulates:
- Opening/closing the database and block log
- Reindexing from block log
- Pushing blocks and transactions with configurable validation
- Undo sessions for atomic state transitions
- Observer signals for operations and blocks
- DLT mode support for selective block retention
- Snapshot loading capabilities for rapid state restoration

Notable APIs:
- Block lifecycle: validate_block, push_block, pop_block, generate_block
- Transaction lifecycle: validate_transaction, push_transaction
- Queries: get_account, get_witness, get_content, get_escrow, get_dynamic_global_properties, get_witness_schedule_object, get_hardfork_property_object
- Fork and block log helpers: is_known_block, is_known_transaction, fetch_block_by_id, fetch_block_by_number, get_block_ids_on_fork
- DLT mode: _dlt_mode flag, _dlt_block_log_max_blocks configuration
- Observers: pre_apply_operation, post_apply_operation, applied_block, on_pending_transaction, on_applied_transaction

Validation flags allow skipping expensive checks during reindexing or trusted operations.

```mermaid
classDiagram
class database {
+open(data_dir, shared_mem_dir, ...)
+reindex(data_dir, shared_mem_dir, from_block_num, ...)
+open_from_snapshot(data_dir, shared_mem_dir, initial_supply, shared_file_size, flags)
+push_block(signed_block, skip)
+validate_block(signed_block, skip)
+push_transaction(signed_transaction, skip)
+validate_transaction(signed_transaction, skip)
+generate_block(...)
+pop_block()
+get_account(name)
+get_witness(name)
+get_content(author, permlink)
+get_escrow(name, id)
+get_dynamic_global_properties()
+get_witness_schedule_object()
+get_hardfork_property_object()
+is_known_block(id)
+is_known_transaction(id)
+fetch_block_by_id(id)
+fetch_block_by_number(n)
+get_block_ids_on_fork(head)
+pre_apply_operation(signal)
+post_apply_operation(signal)
+applied_block(signal)
+on_pending_transaction(signal)
+on_applied_transaction(signal)
+initialize_hardforks()
}
class fork_database
class block_log
class dlt_block_log
database --> fork_database : "uses"
database --> block_log : "uses"
database --> dlt_block_log : "uses"
```

**Diagram sources**
- [database.hpp:111-558](file://libraries/chain/include/graphene/chain/database.hpp#L111-L558)
- [database.cpp:206-456](file://libraries/chain/database.cpp#L206-L456)
- [fork_database.hpp:53-125](file://libraries/chain/include/graphene/chain/fork_database.hpp#L53-L125)
- [block_log.hpp:1-200](file://libraries/chain/include/graphene/chain/block_log.hpp#L1-L200)
- [dlt_block_log.hpp:35-75](file://libraries/chain/include/graphene/chain/dlt_block_log.hpp#L35-L75)

**Section sources**
- [database.hpp:111-558](file://libraries/chain/include/graphene/chain/database.hpp#L111-L558)
- [database.cpp:206-456](file://libraries/chain/database.cpp#L206-L456)

### Fork Database: Fork Resolution and Branch Selection
The fork database maintains a tree of candidate blocks:
- push_block inserts a block and links it to previous if possible
- fetch_branch_from computes divergent branches to resolve forks
- walk_main_branch_to_num and fetch_block_on_main_branch_by_number resolve canonical chain membership
- set_max_size prunes old blocks to bound memory growth

```mermaid
flowchart TD
Start(["push_block"]) --> LinkCheck["Link to previous known block?"]
LinkCheck --> |No| Cache["Add to unlinked cache"]
LinkCheck --> |Yes| Insert["_push_block: insert into index"]
Insert --> UpdateHead{"New head? (higher num)"}
UpdateHead --> |Yes| SetHead["Set head"]
UpdateHead --> |No| Done(["Return head"])
Cache --> Done
SetHead --> Done
```

**Diagram sources**
- [fork_database.cpp:33-90](file://libraries/chain/fork_database.cpp#L33-L90)

**Section sources**
- [fork_database.hpp:53-125](file://libraries/chain/include/graphene/chain/fork_database.hpp#L53-L125)
- [fork_database.cpp:47-90](file://libraries/chain/fork_database.cpp#L47-L90)

### Block Log: Efficient Storage and Retrieval
The block log provides:
- Memory-mapped files for blocks and an index
- Random access by block number via index
- Append-only writes with position tracking
- Robust startup logic to reconcile head positions and reconstruct index if needed

```mermaid
flowchart TD
Open(["open(file)"]) --> InitFiles["Ensure block/index files exist"]
InitFiles --> MapFiles["Map files into memory"]
MapFiles --> Reconcile["Compare heads and reconstruct index if needed"]
Reconcile --> Ready(["Ready"])
Append(["append(block)"]) --> Pack["Pack block"]
Pack --> WriteBlock["Write to block log"]
WriteBlock --> WriteIndex["Write position to index"]
WriteIndex --> UpdateHead["Update head"]
ReadNum(["read_block_by_num(n)"]) --> Pos["Get position from index"]
Pos --> ReadBlock["Read block at position"]
ReadBlock --> Verify["Verify block number"]
```

**Diagram sources**
- [block_log.cpp:134-194](file://libraries/chain/block_log.cpp#L134-L194)
- [block_log.cpp:195-227](file://libraries/chain/block_log.cpp#L195-L227)
- [block_log.cpp:270-285](file://libraries/chain/block_log.cpp#L270-L285)

**Section sources**
- [block_log.hpp:1-200](file://libraries/chain/include/graphene/chain/block_log.hpp#L1-L200)
- [block_log.cpp:134-194](file://libraries/chain/block_log.cpp#L134-L194)
- [block_log.cpp:270-285](file://libraries/chain/block_log.cpp#L270-L285)

### DLT Block Log: Selective Block Retention
The DLT block log provides:
- Memory-mapped files for blocks and an index with rolling window capability
- Configurable maximum blocks retention via _dlt_block_log_max_blocks
- Automatic truncation when exceeding configured limits
- Separate from standard block log for compliance and archival purposes
- Support for DLT mode where only irreversible blocks are retained

```mermaid
flowchart TD
Open(["open(file)"]) --> InitFiles["Ensure dlt block/index files exist"]
InitFiles --> MapFiles["Map files into memory"]
MapFiles --> Reconcile["Reconstruct index if mismatched"]
Reconcile --> Ready(["Ready"])
Append(["append(block)"]) --> CheckLimit{"Exceeds max blocks?"}
CheckLimit --> |Yes| Truncate["Truncate old blocks"]
CheckLimit --> |No| Continue["Continue"]
Truncate --> TruncateBefore["truncate_before(new_start)"]
TruncateBefore --> Continue
Continue --> Pack["Pack block"]
Pack --> WriteBlock["Write to dlt block log"]
WriteBlock --> WriteIndex["Write position to index"]
WriteIndex --> UpdateHead["Update head"]
```

**Diagram sources**
- [dlt_block_log.cpp:161-230](file://libraries/chain/dlt_block_log.cpp#L161-L230)
- [dlt_block_log.cpp:356-411](file://libraries/chain/dlt_block_log.cpp#L356-L411)

**Section sources**
- [dlt_block_log.hpp:35-75](file://libraries/chain/include/graphene/chain/dlt_block_log.hpp#L35-L75)
- [dlt_block_log.cpp:161-230](file://libraries/chain/dlt_block_log.cpp#L161-L230)
- [dlt_block_log.cpp:356-411](file://libraries/chain/dlt_block_log.cpp#L356-L411)

### Data Model: Objects and Indices
The object model defines persistent entities and their indices:
- Object types enumerate all managed object kinds
- Account, witness, committee request/vote, transaction, escrow, vesting delegation, and more
- Multi-index containers provide unique and composite keys for efficient lookups

Representative object categories:
- Accounts: balances, vesting shares, delegation, auction metadata, bandwidth tracking
- Witnesses: votes, virtual scheduling, signing keys, version/hardfork votes
- Committee: requests with statuses, funding, payouts
- Transactions: deduplication and expiration tracking
- Escrow and routes: multi-signature transfers and routing

```mermaid
erDiagram
ACCOUNT {
name : string
balance : asset
vesting_shares : asset
delegated_vesting_shares : asset
received_vesting_shares : asset
energy : int16
last_vote_time : time_point_sec
}
WITNESS {
owner : account_name_type
votes : share_type
signing_key : public_key_type
running_version : version
hardfork_version_vote : hardfork_version
hardfork_time_vote : time_point_sec
}
COMMITTEE_REQUEST {
request_id : uint32
creator : account_name_type
worker : account_name_type
required_amount_min : asset
required_amount_max : asset
status : uint16
start_time : time_point_sec
end_time : time_point_sec
payout_amount : asset
remain_payout_amount : asset
}
TRANSACTION_OBJECT {
trx_id : transaction_id_type
expiration : time_point_sec
}
ESCROW {
escrow_id : uint32
from : account_name_type
to : account_name_type
agent : account_name_type
ratification_deadline : time_point_sec
escrow_expiration : time_point_sec
token_balance : asset
pending_fee : asset
to_approved : bool
agent_approved : bool
disputed : bool
}
ACCOUNT ||--o{ WITNESS_VOTE : "votes_for"
ACCOUNT ||--o{ ESCROW : "escrows"
ACCOUNT ||--o{ COMMITTEE_VOTE : "votes"
COMMITTEE_REQUEST ||--o{ COMMITTEE_VOTE : "votes"
TRANSACTION_OBJECT ||--|| BLOCK : "referenced_in"
```

**Diagram sources**
- [chain_object_types.hpp:44-146](file://libraries/chain/include/graphene/chain/chain_object_types.hpp#L44-L146)
- [account_object.hpp:20-143](file://libraries/chain/include/graphene/chain/account_object.hpp#L20-L143)
- [witness_objects.hpp:27-132](file://libraries/chain/include/graphene/chain/witness_objects.hpp#L27-L132)
- [committee_objects.hpp:15-47](file://libraries/chain/include/graphene/chain/committee_objects.hpp#L15-L47)
- [transaction_object.hpp:19-56](file://libraries/chain/include/graphene/chain/transaction_object.hpp#L19-L56)
- [chain_objects.hpp:20-141](file://libraries/chain/include/graphene/chain/chain_objects.hpp#L20-L141)

**Section sources**
- [chain_object_types.hpp:44-146](file://libraries/chain/include/graphene/chain/chain_object_types.hpp#L44-L146)
- [account_object.hpp:20-143](file://libraries/chain/include/graphene/chain/account_object.hpp#L20-L143)
- [witness_objects.hpp:27-132](file://libraries/chain/include/graphene/chain/witness_objects.hpp#L27-L132)
- [committee_objects.hpp:15-47](file://libraries/chain/include/graphene/chain/committee_objects.hpp#L15-L47)
- [transaction_object.hpp:19-56](file://libraries/chain/include/graphene/chain/transaction_object.hpp#L19-L56)
- [chain_objects.hpp:20-141](file://libraries/chain/include/graphene/chain/chain_objects.hpp#L20-L141)

### Evaluator System: Operation Processing
The evaluator system provides a uniform mechanism to apply operations:
- Base evaluator interface with apply and type identification
- Evaluator implementations for each operation type
- Registry-driven dispatch to the appropriate evaluator

```mermaid
classDiagram
class evaluator~OperationType~ {
<<interface>>
+apply(op : OperationType) void
+get_type() int
}
class evaluator_impl~EvaluatorType, OperationType~ {
-_db : database
+apply(op : OperationType) void
+get_type() int
+db() database&
}
class account_create_evaluator
class transfer_evaluator
class witness_update_evaluator
evaluator_impl <|-- account_create_evaluator
evaluator_impl <|-- transfer_evaluator
evaluator_impl <|-- witness_update_evaluator
evaluator <|-- evaluator_impl
```

**Diagram sources**
- [evaluator.hpp:11-45](file://libraries/chain/include/graphene/chain/evaluator.hpp#L11-L45)
- [chain_evaluator.hpp:14-79](file://libraries/chain/include/graphene/chain/chain_evaluator.hpp#L14-L79)

**Section sources**
- [evaluator.hpp:11-45](file://libraries/chain/include/graphene/chain/evaluator.hpp#L11-L45)
- [chain_evaluator.hpp:14-79](file://libraries/chain/include/graphene/chain/chain_evaluator.hpp#L14-L79)

### Observer Pattern: Event-Driven Architecture
The database emits signals for:
- Pre/post operation application
- Applied block
- Pending/applied transactions

Plugins and observers can subscribe to these signals to react to state changes without tight coupling.

```mermaid
sequenceDiagram
participant DB as "database"
participant OBS as "Observers"
participant EVAL as "evaluator"
DB->>EVAL : apply_operation(op)
DB->>OBS : pre_apply_operation(note)
EVAL-->>DB : operation applied
DB->>OBS : post_apply_operation(note)
DB->>OBS : applied_block(block)
DB->>OBS : on_applied_transaction(trx)
```

**Diagram sources**
- [database.hpp:252-286](file://libraries/chain/include/graphene/chain/database.hpp#L252-L286)
- [operation_notification.hpp:11-26](file://libraries/chain/include/graphene/chain/operation_notification.hpp#L11-L26)
- [database.cpp:1158-1198](file://libraries/chain/database.cpp#L1158-L1198)

**Section sources**
- [database.hpp:252-286](file://libraries/chain/include/graphene/chain/database.hpp#L252-L286)
- [operation_notification.hpp:11-26](file://libraries/chain/include/graphene/chain/operation_notification.hpp#L11-L26)
- [database.cpp:1158-1198](file://libraries/chain/database.cpp#L1158-L1198)

## Snapshot Loading and DLT Mode

### Enhanced Chain Plugin Startup Sequence
The chain plugin now implements a sophisticated startup sequence that handles both normal operation and snapshot-based initialization:

```mermaid
flowchart TD
Start(["plugin_startup()"]) --> CheckSnapshot{"--snapshot option present?"}
CheckSnapshot --> |Yes| CheckSHM["Check shared_memory.bin exists"]
CheckSHM --> |Exists & >0| NormalStartup["Skip snapshot import, use normal startup"]
CheckSHM --> |Missing/Empty| CheckFile["Check snapshot file exists"]
CheckFile --> |Exists| OpenSnapshot["Call db.open_from_snapshot()"]
CheckFile --> |Missing| NormalStartup
OpenSnapshot --> LoadState["Execute snapshot_load_callback()"]
LoadState --> ValidateState["Validate snapshot integrity"]
ValidateState --> Success{"Load success?"}
Success --> |Yes| RenameFile["Rename snapshot to .used"]
Success --> |No| Error["Log fatal error and exit"]
NormalStartup --> NormalOpen["Call db.open()"]
NormalOpen --> ReplayCheck{"Need replay?"}
ReplayCheck --> |Yes| ExecuteReplay["Execute replay_db()"]
ReplayCheck --> |No| CompleteStartup["Complete startup"]
ExecuteReplay --> CompleteStartup
CompleteStartup --> Sync["on_sync()"]
```

**Diagram sources**
- [plugin.cpp:364-432](file://plugins/chain/plugin.cpp#L364-L432)
- [plugin.cpp:434-491](file://plugins/chain/plugin.cpp#L434-L491)

**Section sources**
- [plugin.cpp:364-432](file://plugins/chain/plugin.cpp#L364-L432)
- [plugin.cpp:434-491](file://plugins/chain/plugin.cpp#L434-L491)

### Database Initialization for Snapshot Mode
The database provides a dedicated `open_from_snapshot` method that initializes the chain in DLT mode:

**Updated** Enhanced with DLT mode flag management and improved error handling

Key features:
- Sets `_dlt_mode = true` for DLT operations
- Wipes existing shared memory to ensure clean state
- Initializes schema and opens shared memory with proper flags
- Opens both standard and DLT block logs
- Handles genesis initialization in read-write mode

**Section sources**
- [database.cpp:281-324](file://libraries/chain/database.cpp#L281-L324)

### Snapshot Loading Process
The snapshot plugin implements comprehensive state restoration:

**Updated** Enhanced with improved error handling and validation

Process overview:
1. Parse and validate snapshot header
2. Verify chain ID compatibility and version support
3. Validate payload checksum for integrity
4. Clear genesis-created objects to prevent conflicts
5. Import objects in dependency order (singletons first, then multi-instance)
6. Set chainbase revision to match snapshot head block
7. Seed fork database with snapshot head block

**Section sources**
- [snapshot_plugin.cpp:1000-1200](file://plugins/snapshot/plugin.cpp#L1000-L1200)

### DLT Mode Flag Management
The database maintains two key DLT-related flags:

**Updated** Added comprehensive DLT mode support

- `_dlt_mode`: Boolean flag indicating snapshot-loaded state
- `_dlt_block_log_max_blocks`: Configurable limit for rolling block retention

Behavior in DLT mode:
- Block log append operations are conditionally executed
- Irreversible blocks are selectively written to DLT block log
- Automatic truncation when exceeding configured limits
- Separate from standard block log for compliance purposes

**Section sources**
- [database.hpp:57-64](file://libraries/chain/include/graphene/chain/database.hpp#L57-L64)
- [database.cpp:3985-4051](file://libraries/chain/database.cpp#L3985-L4051)

### Error Handling Improvements
Enhanced error handling during snapshot operations:

**Updated** Added comprehensive error handling and recovery mechanisms

- Graceful fallback to normal startup when snapshot import fails
- Detailed error logging with specific failure reasons
- Automatic snapshot file renaming to prevent re-import
- Validation of snapshot integrity before import
- Proper cleanup and resource management on errors

**Section sources**
- [plugin.cpp:391-417](file://plugins/chain/plugin.cpp#L391-L417)
- [snapshot_plugin.cpp:1014-1032](file://plugins/snapshot/plugin.cpp#L1014-L1032)

## Dependency Analysis
The database depends on:
- fork_database for chain selection
- block_log for durable storage
- dlt_block_log for selective block retention in DLT mode
- object model headers for schema definitions
- evaluator registry for operation application
- observer signals for event emission
- snapshot plugin for state restoration

```mermaid
graph LR
DB["database.hpp/.cpp"] --> FDB["fork_database.hpp/.cpp"]
DB --> BLK["block_log.hpp/.cpp"]
DB --> DLT["dlt_block_log.hpp/.cpp"]
DB --> OT["chain_object_types.hpp"]
DB --> CO["chain_objects.hpp"]
DB --> AO["account_object.hpp"]
DB --> WO["witness_objects.hpp"]
DB --> CT["committee_objects.hpp"]
DB --> TO["transaction_object.hpp"]
DB --> EV["evaluator.hpp"]
DB --> CEV["chain_evaluator.hpp"]
DB --> ON["operation_notification.hpp"]
CHAINPLUG["chain plugin.cpp"] --> SNAP["snapshot_plugin.cpp"]
CHAINPLUG --> DB
```

**Diagram sources**
- [database.hpp:1-561](file://libraries/chain/include/graphene/chain/database.hpp#L1-L561)
- [database.cpp:1-200](file://libraries/chain/database.cpp#L1-L200)
- [fork_database.hpp:1-125](file://libraries/chain/include/graphene/chain/fork_database.hpp#L1-L125)
- [block_log.hpp:1-200](file://libraries/chain/include/graphene/chain/block_log.hpp#L1-L200)
- [dlt_block_log.hpp:1-75](file://libraries/chain/include/graphene/chain/dlt_block_log.hpp#L1-L75)
- [chain_object_types.hpp:1-246](file://libraries/chain/include/graphene/chain/chain_object_types.hpp#L1-L246)
- [chain_objects.hpp:1-226](file://libraries/chain/include/graphene/chain/chain_objects.hpp#L1-L226)
- [account_object.hpp:1-565](file://libraries/chain/include/graphene/chain/account_object.hpp#L1-L565)
- [witness_objects.hpp:1-313](file://libraries/chain/include/graphene/chain/witness_objects.hpp#L1-L313)
- [committee_objects.hpp:1-137](file://libraries/chain/include/graphene/chain/committee_objects.hpp#L1-L137)
- [transaction_object.hpp:1-56](file://libraries/chain/include/graphene/chain/transaction_object.hpp#L1-L56)
- [evaluator.hpp:1-62](file://libraries/chain/include/graphene/chain/evaluator.hpp#L1-L62)
- [chain_evaluator.hpp:1-80](file://libraries/chain/include/graphene/chain/chain_evaluator.hpp#L1-L80)
- [operation_notification.hpp:1-27](file://libraries/chain/include/graphene/chain/operation_notification.hpp#L1-L27)
- [plugin.cpp:1-526](file://plugins/chain/plugin.cpp#L1-L526)
- [snapshot_plugin.cpp:1-1976](file://plugins/snapshot/plugin.cpp#L1-L1976)

**Section sources**
- [database.hpp:1-561](file://libraries/chain/include/graphene/chain/database.hpp#L1-L561)
- [database.cpp:1-200](file://libraries/chain/database.cpp#L1-L200)

## Performance Considerations
- Shared memory sizing and auto-resize: Configure minimum free memory thresholds and incremental growth to avoid frequent resizes during heavy load.
- Block log I/O: Memory-mapped files reduce syscall overhead; ensure adequate OS page cache and avoid fragmentation.
- Fork pruning: Limit fork cache size to bound memory usage; prune old blocks when head advances significantly.
- Validation flags: During reindexing, skip expensive checks (signatures, merkle, authority) to accelerate replay.
- Bandwidth accounting: Efficient per-account bandwidth calculations prevent excessive CPU usage on producers.
- Undo sessions: Use short-lived sessions to minimize rollback overhead; squash temporary sessions after successful application.
- DLT mode optimization: Configure `_dlt_block_log_max_blocks` appropriately to balance storage requirements and performance.
- Snapshot loading: Use snapshot mode for rapid node startup, especially for large blockchains.

## Troubleshooting Guide
Common issues and remedies:
- Chain mismatch after restart: The database verifies revision against head block number; if inconsistent, a specific exception is thrown indicating the mismatch.
- Block log/head mismatch: On open, the database validates that the block log head matches the chain head; if not, a specific exception instructs reindexing.
- Memory pressure: Monitor free memory and trigger auto-resize when thresholds are met; periodically print free memory status.
- Fork collisions: When multiple blocks are produced at the same slot, warnings are logged; ensure correct fork resolution and head updates.
- Bad allocation during block push: On memory exhaustion, the system attempts to resize shared memory and retry.
- Snapshot import failures: Check shared_memory.bin existence and snapshot file accessibility; the system gracefully falls back to normal startup.
- DLT mode errors: Verify DLT block log configuration and ensure sufficient disk space for rolling window retention.
- Chain ID mismatches: Snapshot loading validates chain ID compatibility; ensure using correct snapshot for target network.

**Section sources**
- [database.cpp:232-248](file://libraries/chain/database.cpp#L232-L248)
- [database.cpp:270-350](file://libraries/chain/database.cpp#L270-L350)
- [database.cpp:368-430](file://libraries/chain/database.cpp#L368-L430)
- [database.cpp:832-844](file://libraries/chain/database.cpp#L832-L844)
- [plugin.cpp:371-380](file://plugins/chain/plugin.cpp#L371-L380)
- [snapshot_plugin.cpp:1018-1020](file://plugins/snapshot/plugin.cpp#L1018-L1020)

## Conclusion
The Chain Library provides a robust, modular framework for blockchain state management with enhanced snapshot loading capabilities and DLT mode support. Its design separates concerns across database orchestration, fork handling, durable storage, typed object models, operation processing, and event-driven observation. The addition of snapshot loading enables rapid node startup and state restoration, while DLT mode provides selective block retention for compliance and archival purposes. By leveraging ChainBase for persistence, fork_database for consensus, block_log for storage, and the new DLT block log for selective retention, it achieves high throughput, reliability, and regulatory compliance. Developers can extend functionality via evaluators and observe state changes through signals, enabling flexible plugin architectures with enhanced operational capabilities.

## Appendices

### Common Database Operations and Examples
- Open and replay:
  - Open database and block log, initialize indexes and evaluators, then start block log and rewind undo state.
  - Reindex from a specified block number with skip flags to bypass validations.
  - **New**: Open from snapshot using `open_from_snapshot()` for rapid initialization.
- Push block:
  - Validate block (merkle and size), push to fork database, resolve forks if needed, apply block, persist to block log, emit applied block signal.
  - **Enhanced**: In DLT mode, selectively write irreversible blocks to DLT block log.
- Push transaction:
  - Validate transaction size, apply within a pending session, record changes, and emit pending/applied transaction signals.
- Query state:
  - Retrieve account, witness, content, escrow, dynamic global properties, witness schedule, and hardfork property objects by name or identifier.
- Fork resolution:
  - Compute branches from current head to a candidate fork head, pop blocks until common ancestor, then push new fork blocks.
- **New**: Snapshot operations:
  - Load snapshot state via callback during startup
  - Validate snapshot integrity and chain compatibility
  - Handle snapshot file lifecycle (renaming, cleanup)

**Section sources**
- [database.cpp:206-350](file://libraries/chain/database.cpp#L206-L350)
- [database.cpp:800-925](file://libraries/chain/database.cpp#L800-L925)
- [database.cpp:936-970](file://libraries/chain/database.cpp#L936-L970)
- [database.hpp:136-169](file://libraries/chain/include/graphene/chain/database.hpp#L136-L169)
- [plugin.cpp:364-432](file://plugins/chain/plugin.cpp#L364-L432)
- [snapshot_plugin.cpp:1000-1200](file://plugins/snapshot/plugin.cpp#L1000-L1200)

### Performance Optimization Techniques
- Tune shared memory growth: Set minimum free memory and increment sizes to avoid frequent resizing.
- Use skip flags during reindexing: Disable signature and authority checks to accelerate replay.
- Monitor and log memory: Periodic logs help detect approaching limits before failures.
- Keep fork cache bounded: Adjust maximum fork size to control memory footprint.
- Batch operations: Group related operations to minimize undo session overhead.
- **New**: Configure DLT mode: Set `_dlt_block_log_max_blocks` to balance storage and performance.
- **New**: Optimize snapshot loading: Use appropriate snapshot files and monitor import performance.
- **New**: Manage DLT storage: Regularly monitor DLT block log size and adjust retention policies.

**Section sources**
- [database.cpp:368-430](file://libraries/chain/database.cpp#L368-L430)
- [fork_database.cpp:92-124](file://libraries/chain/fork_database.cpp#L92-L124)
- [database.hpp:62-64](file://libraries/chain/include/graphene/chain/database.hpp#L62-L64)
- [dlt_block_log.cpp:356-411](file://libraries/chain/dlt_block_log.cpp#L356-L411)

### DLT Mode Configuration
**New Section** Configuration and usage guidelines for DLT mode

Configuration options:
- `--dlt-block-log-max-blocks`: Number of recent blocks to keep in rolling DLT block log (default: 100000)
- `_dlt_mode`: Internal flag indicating DLT operation mode
- `_dlt_block_log`: Separate block log instance for DLT operations

Usage scenarios:
- Compliance reporting with selective block retention
- Archival purposes with configurable retention periods
- Reduced storage requirements for non-validating nodes
- Regulatory compliance with immutable block records

**Section sources**
- [plugin.cpp:234-236](file://plugins/chain/plugin.cpp#L234-L236)
- [database.hpp:57-64](file://libraries/chain/include/graphene/chain/database.hpp#L57-L64)
- [dlt_block_log.cpp:161-230](file://libraries/chain/dlt_block_log.cpp#L161-L230)