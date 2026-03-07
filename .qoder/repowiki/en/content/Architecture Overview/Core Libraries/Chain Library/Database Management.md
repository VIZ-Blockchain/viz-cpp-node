# Database Management

<cite>
**Referenced Files in This Document**
- [database.hpp](file://libraries/chain/include/graphene/chain/database.hpp)
- [database.cpp](file://libraries/chain/database.cpp)
- [block_log.hpp](file://libraries/chain/include/graphene/chain/block_log.hpp)
- [block_log.cpp](file://libraries/chain/block_log.cpp)
- [fork_database.hpp](file://libraries/chain/include/graphene/chain/fork_database.hpp)
- [fork_database.cpp](file://libraries/chain/fork_database.cpp)
</cite>

## Table of Contents
1. [Introduction](#introduction)
2. [Project Structure](#project-structure)
3. [Core Components](#core-components)
4. [Architecture Overview](#architecture-overview)
5. [Detailed Component Analysis](#detailed-component-analysis)
6. [Dependency Analysis](#dependency-analysis)
7. [Performance Considerations](#performance-considerations)
8. [Troubleshooting Guide](#troubleshooting-guide)
9. [Conclusion](#conclusion)

## Introduction
This document describes the Database Management system that serves as the core state persistence layer for the VIZ blockchain. It covers the database class lifecycle, initialization and cleanup, validation steps, session management, memory allocation strategies, shared memory configuration, checkpoints for fast synchronization, block log integration, observer pattern usage, and practical examples of database operations and performance optimization.

## Project Structure
The database subsystem is implemented primarily in the chain library:
- Core database interface and declarations: libraries/chain/include/graphene/chain/database.hpp
- Implementation of database operations: libraries/chain/database.cpp
- Block log abstraction: libraries/chain/include/graphene/chain/block_log.hpp and libraries/chain/block_log.cpp
- Fork database for reversible blocks: libraries/chain/include/graphene/chain/fork_database.hpp and libraries/chain/fork_database.cpp

```mermaid
graph TB
subgraph "Chain Library"
DBH["database.hpp"]
DBCPP["database.cpp"]
BLH["block_log.hpp"]
BLCPP["block_log.cpp"]
FDH["fork_database.hpp"]
FDCPP["fork_database.cpp"]
end
DBH --> DBCPP
DBCPP --> BLH
DBCPP --> BLCPP
DBCPP --> FDH
DBCPP --> FDCPP
```

**Diagram sources**
- [database.hpp](file://libraries/chain/include/graphene/chain/database.hpp#L36-L561)
- [database.cpp](file://libraries/chain/database.cpp#L1-L5295)
- [block_log.hpp](file://libraries/chain/include/graphene/chain/block_log.hpp#L38-L75)
- [block_log.cpp](file://libraries/chain/block_log.cpp#L1-L302)
- [fork_database.hpp](file://libraries/chain/include/graphene/chain/fork_database.hpp#L53-L125)
- [fork_database.cpp](file://libraries/chain/fork_database.cpp#L1-L245)

**Section sources**
- [database.hpp](file://libraries/chain/include/graphene/chain/database.hpp#L1-L561)
- [database.cpp](file://libraries/chain/database.cpp#L1-L5295)
- [block_log.hpp](file://libraries/chain/include/graphene/chain/block_log.hpp#L1-L75)
- [block_log.cpp](file://libraries/chain/block_log.cpp#L1-L302)
- [fork_database.hpp](file://libraries/chain/include/graphene/chain/fork_database.hpp#L1-L125)
- [fork_database.cpp](file://libraries/chain/fork_database.cpp#L1-L245)

## Core Components
- database class: Public interface for blockchain state management, block and transaction processing, checkpoints, and event notifications.
- block_log: Append-only block storage with random-access indexing.
- fork_database: Maintains reversible blocks and supports fork selection and switching.
- chainbase integration: Provides persistent object storage and undo sessions.

Key responsibilities:
- Lifecycle: open(), reindex(), close(), wipe()
- Validation: validate_block(), validate_transaction(), with configurable skip flags
- Operations: push_block(), push_transaction(), generate_block()
- Observers: signals for pre/post operation, applied block, pending/applied transactions
- Persistence: integrates with block_log and maintains last irreversible block

**Section sources**
- [database.hpp](file://libraries/chain/include/graphene/chain/database.hpp#L36-L561)
- [database.cpp](file://libraries/chain/database.cpp#L206-L456)
- [block_log.hpp](file://libraries/chain/include/graphene/chain/block_log.hpp#L38-L75)
- [fork_database.hpp](file://libraries/chain/include/graphene/chain/fork_database.hpp#L53-L125)

## Architecture Overview
The database composes three primary subsystems:
- Chainbase: Persistent object database with undo/redo capabilities
- Fork database: Holds recent blocks for fork resolution
- Block log: Immutable, append-only block storage with index

```mermaid
classDiagram
class database {
+open(data_dir, shared_mem_dir, ...)
+reindex(data_dir, shared_mem_dir, from_block_num, ...)
+close(rewind=true)
+push_block(block, skip_flags)
+push_transaction(trx, skip_flags)
+validate_block(block, skip_flags)
+validate_transaction(trx, skip_flags)
+add_checkpoints(map)
+get_block_log()
+signals : pre_apply_operation, post_apply_operation, applied_block, on_pending_transaction, on_applied_transaction
}
class block_log {
+open(path)
+close()
+append(block)
+read_block_by_num(num)
+flush()
+head()
}
class fork_database {
+push_block(block)
+start_block(block)
+set_head(item)
+fetch_branch_from(first, second)
+set_max_size(n)
}
database --> block_log : "uses"
database --> fork_database : "uses"
```

**Diagram sources**
- [database.hpp](file://libraries/chain/include/graphene/chain/database.hpp#L36-L561)
- [database.cpp](file://libraries/chain/database.cpp#L206-L456)
- [block_log.hpp](file://libraries/chain/include/graphene/chain/block_log.hpp#L38-L75)
- [fork_database.hpp](file://libraries/chain/include/graphene/chain/fork_database.hpp#L53-L125)

## Detailed Component Analysis

### Database Lifecycle: Constructor, Destructor, and Methods
- Constructor and destructor: Initialize internal implementation and ensure pending transactions are cleared on destruction.
- open(): Initializes schema, opens shared memory, initializes indexes and evaluators, loads genesis if needed, opens block log, rewinds undo state, verifies chain consistency, and initializes hardfork state.
- reindex(): Reads blocks sequentially from the block log, applies them with aggressive skip flags to accelerate replay, periodically sets revision, checks free memory, and updates fork database head.
- close(): Clears pending transactions, flushes and closes chainbase, closes block log, resets fork database.
- wipe(): Closes database, wipes shared memory file, optionally removes block log and index.

```mermaid
sequenceDiagram
participant App as "Application"
participant DB as "database"
participant CB as "chainbase"
participant BL as "block_log"
participant FD as "fork_database"
App->>DB : open(data_dir, shared_mem_dir, ...)
DB->>DB : init_schema()
DB->>CB : open(shared_mem_dir, flags, size)
DB->>DB : initialize_indexes()
DB->>DB : initialize_evaluators()
DB->>DB : with_strong_write_lock(init_genesis)
DB->>BL : open(data_dir/"block_log")
DB->>CB : with_strong_write_lock(undo_all)
DB->>FD : start_block(head_block)
App->>DB : reindex(data_dir, shared_mem_dir, from_block_num, ...)
DB->>DB : with_strong_write_lock()
loop for each block
DB->>BL : read_block_by_num(block_num)
DB->>DB : apply_block(block, skip_flags)
DB->>DB : check_free_memory(...)
end
DB->>FD : start_block(head)
```

**Diagram sources**
- [database.cpp](file://libraries/chain/database.cpp#L206-L350)

**Section sources**
- [database.hpp](file://libraries/chain/include/graphene/chain/database.hpp#L36-L110)
- [database.cpp](file://libraries/chain/database.cpp#L206-L350)

### Validation Steps Enumeration and Use Cases
Validation flags control which checks are performed during block and transaction validation:
- skip_nothing: Perform all validations
- skip_witness_signature: Skip witness signature verification (used during reindex)
- skip_transaction_signatures: Skip transaction signatures (used by non-witness nodes)
- skip_transaction_dupe_check: Skip duplicate transaction checks
- skip_fork_db: Skip fork database checks
- skip_block_size_check: Allow oversized blocks when generating locally
- skip_tapos_check: Skip TaPoS and expiration checks
- skip_authority_check: Skip authority checks
- skip_merkle_check: Skip Merkle root verification
- skip_undo_history_check: Skip undo history bounds
- skip_witness_schedule_check: Skip witness schedule validation
- skip_validate_operations: Skip operation validation
- skip_undo_block: Skip undo db on reindex
- skip_block_log: Skip writing to block log
- skip_apply_transaction: Skip applying transaction
- skip_database_locking: Skip database locking

Typical usage:
- Reindex uses a combination of flags to accelerate replay
- Block generation may skip certain checks for local blocks
- Validation-only nodes may skip expensive checks

**Section sources**
- [database.hpp](file://libraries/chain/include/graphene/chain/database.hpp#L56-L73)
- [database.cpp](file://libraries/chain/database.cpp#L280-L350)
- [database.cpp](file://libraries/chain/database.cpp#L738-L792)
- [database.cpp](file://libraries/chain/database.cpp#L3305-L3404)

### Session Management and Undo Semantics
- Pending transaction session: A temporary undo session is created when pushing the first transaction after applying a block; successful transactions merge into the pending block session.
- Block application session: A strong write lock wraps block application; a temporary undo session is used per transaction; upon success, the session is pushed.
- Undo history: Enforced with bounds; last irreversible block advancement commits revisions and writes to block log.

```mermaid
flowchart TD
Start(["push_transaction(trx, skip)"]) --> CheckSession["Check _pending_tx_session.valid()"]
CheckSession --> |No| NewSession["start_undo_session() -> _pending_tx_session"]
CheckSession --> |Yes| UseExisting["Use existing _pending_tx_session"]
NewSession --> TempSession["start_undo_session() -> temp_session"]
UseExisting --> TempSession
TempSession --> ApplyTx["_apply_transaction(trx, skip)"]
ApplyTx --> Success{"Apply success?"}
Success --> |Yes| Merge["temp_session.squash()"]
Merge --> Notify["notify_on_pending_transaction(trx)"]
Success --> |No| Discard["temp_session destructor discards changes"]
Notify --> End(["Return"])
Discard --> End
```

**Diagram sources**
- [database.cpp](file://libraries/chain/database.cpp#L948-L970)
- [database.cpp](file://libraries/chain/database.cpp#L3652-L3711)

**Section sources**
- [database.cpp](file://libraries/chain/database.cpp#L948-L970)
- [database.cpp](file://libraries/chain/database.cpp#L3652-L3711)

### Memory Allocation Strategies and Shared Memory Configuration
- Auto-resize: When free memory drops below a configured threshold, the system increases shared memory size and logs the change.
- Free memory monitoring: Periodic checks at configured block intervals log free memory and trigger resizing if needed.
- Reserved memory: Prevents fragmentation by reserving a portion of available memory.
- Configuration knobs: Minimum free memory threshold, increment size, and block interval for checks.

```mermaid
flowchart TD
Entry(["check_free_memory(block_num)"]) --> ModCheck["block_num % _block_num_check_free_memory == 0?"]
ModCheck --> |No| Exit(["Return"])
ModCheck --> |Yes| Compute["Compute reserved and free memory"]
Compute --> Compare{"free_mem < min_free_shared_memory_size?"}
Compare --> |No| Exit
Compare --> |Yes| Resize["_resize(block_num)"]
Resize --> Exit
```

**Diagram sources**
- [database.cpp](file://libraries/chain/database.cpp#L396-L430)
- [database.cpp](file://libraries/chain/database.cpp#L368-L394)

**Section sources**
- [database.cpp](file://libraries/chain/database.cpp#L352-L430)
- [database.cpp](file://libraries/chain/database.cpp#L368-L394)

### Checkpoint System for Fast Synchronization
- Checkpoints: A map of block number to expected block ID is maintained; when a checkpoint matches, the system skips expensive validations and authority checks for subsequent blocks until the last checkpoint.
- before_last_checkpoint(): Determines whether the current head is before the last checkpoint to decide whether to enforce stricter checks.

```mermaid
flowchart TD
Start(["apply_block(block, skip)"]) --> HasCheckpoints{"_checkpoints.size() > 0?"}
HasCheckpoints --> |No| Apply["_apply_block(..., skip)"]
HasCheckpoints --> |Yes| Match{"Checkpoint present for block_num?"}
Match --> |Yes| Tighten["Set skip flags for strict checks"]
Tighten --> Apply
Match --> |No| Apply
```

**Diagram sources**
- [database.cpp](file://libraries/chain/database.cpp#L3444-L3499)

**Section sources**
- [database.hpp](file://libraries/chain/include/graphene/chain/database.hpp#L186-L192)
- [database.cpp](file://libraries/chain/database.cpp#L3444-L3499)

### Block Log Integration and Last Irreversible Block Advancement
- Block log: Append-only storage with a secondary index enabling O(1) random access by block number.
- IRV advancement: When sufficient witness validations are collected, the system advances last irreversible block, commits the revision, writes blocks to the block log, and updates dynamic global properties with reference fields.

```mermaid
sequenceDiagram
participant DB as "database"
participant FD as "fork_database"
participant BL as "block_log"
participant DGP as "dynamic_global_property_object"
DB->>DB : check_block_post_validation_chain()
alt Enough validations
DB->>DGP : last_irreversible_block_num++
DB->>DB : commit(last_irreversible_block_num)
loop while log_head_num < LRI
DB->>FD : fetch_block_on_main_branch_by_number(log_head_num+1)
DB->>BL : append(block)
DB->>BL : flush()
end
DB->>DGP : update last_irreversible_block_id/ref fields
DB->>FD : set_max_size(head - LRI + 1)
end
```

**Diagram sources**
- [database.cpp](file://libraries/chain/database.cpp#L3877-L3951)
- [database.cpp](file://libraries/chain/database.cpp#L4140-L4221)

**Section sources**
- [block_log.hpp](file://libraries/chain/include/graphene/chain/block_log.hpp#L38-L75)
- [block_log.cpp](file://libraries/chain/block_log.cpp#L230-L301)
- [database.cpp](file://libraries/chain/database.cpp#L3877-L3951)
- [database.cpp](file://libraries/chain/database.cpp#L4140-L4221)

### Observer Pattern Implementation
The database exposes signals for event-driven state changes:
- pre_apply_operation: Emitted before applying an operation
- post_apply_operation: Emitted after applying an operation
- applied_block: Emitted after a block is applied and committed
- on_pending_transaction: Emitted when a transaction is added to the pending state
- on_applied_transaction: Emitted when a transaction is applied to the chain

These signals are used by plugins to react to blockchain events without tight coupling.

**Section sources**
- [database.hpp](file://libraries/chain/include/graphene/chain/database.hpp#L252-L276)
- [database.cpp](file://libraries/chain/database.cpp#L1158-L1198)
- [database.cpp](file://libraries/chain/database.cpp#L3652-L3655)

### Examples of Database Operations and Queries
- Open database and initialize: open(data_dir, shared_mem_dir, initial_supply, shared_file_size, chainbase_flags)
- Rebuild state from history: reindex(data_dir, shared_mem_dir, from_block_num, shared_file_size)
- Push a block: push_block(signed_block, skip_flags)
- Push a transaction: push_transaction(signed_transaction, skip_flags)
- Validate a block: validate_block(signed_block, skip_flags)
- Validate a transaction: validate_transaction(signed_signed_transaction, skip_flags)
- Query helpers:
  - get_block_id_for_num(uint32_t)
  - fetch_block_by_id(block_id_type)
  - fetch_block_by_number(uint32_t)
  - get_account(name), get_witness(name)
  - get_dynamic_global_properties(), get_witness_schedule_object()

Note: The above APIs are declared in the header and implemented in the cpp file.

**Section sources**
- [database.hpp](file://libraries/chain/include/graphene/chain/database.hpp#L75-L288)
- [database.cpp](file://libraries/chain/database.cpp#L458-L584)

## Dependency Analysis
The database depends on:
- chainbase for persistent storage and undo sessions
- block_log for immutable block storage and random access
- fork_database for reversible blocks and fork resolution
- Protocol types and evaluators for operation processing

```mermaid
graph LR
DB["database.cpp"] --> CB["chainbase (external)"]
DB --> BL["block_log.hpp/.cpp"]
DB --> FD["fork_database.hpp/.cpp"]
DB --> PT["protocol types"]
DB --> EV["evaluators"]
```

**Diagram sources**
- [database.hpp](file://libraries/chain/include/graphene/chain/database.hpp#L1-L26)
- [database.cpp](file://libraries/chain/database.cpp#L1-L30)

**Section sources**
- [database.hpp](file://libraries/chain/include/graphene/chain/database.hpp#L1-L26)
- [database.cpp](file://libraries/chain/database.cpp#L1-L30)

## Performance Considerations
- Use skip flags during reindex to bypass expensive validations and improve replay speed.
- Configure shared memory sizing and thresholds to avoid frequent resizing and fragmentation.
- Monitor free memory and adjust increments to keep latency predictable.
- Use checkpoints to reduce validation overhead for recent blocks.
- Tune flush intervals to balance durability and throughput.

## Troubleshooting Guide
Common issues and remedies:
- Memory exhaustion during block production or reindex: Increase shared file size and tune minimum free memory threshold.
- Chain mismatch between block log and database: Run reindex to rebuild state from block log.
- Excessive undo history: Ensure last irreversible block advances to prune history.
- Signal-related errors: Verify signal handlers and ensure proper exception propagation.

**Section sources**
- [database.cpp](file://libraries/chain/database.cpp#L800-L830)
- [database.cpp](file://libraries/chain/database.cpp#L270-L350)
- [database.cpp](file://libraries/chain/database.cpp#L4140-L4221)

## Conclusion
The Database Management system provides a robust, event-driven, and efficient state persistence layer for the VIZ blockchain. It integrates chainbase for persistent storage, fork_database for reversible blocks, and block_log for immutable history. Through configurable validation flags, checkpointing, and memory management, it supports fast synchronization, reliable block processing, and extensibility via observer signals.