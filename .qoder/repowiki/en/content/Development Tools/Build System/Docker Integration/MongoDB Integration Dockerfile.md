# MongoDB Integration Dockerfile

<cite>
**Referenced Files in This Document**
- [Dockerfile-mongo](file://share/vizd/docker/Dockerfile-mongo)
- [Dockerfile-lowmem](file://share/vizd/docker/Dockerfile-lowmem)
- [Dockerfile-production](file://share/vizd/docker/Dockerfile-production)
- [CMakeLists.txt](file://CMakeLists.txt)
- [plugins/mongo_db/CMakeLists.txt](file://plugins/mongo_db/CMakeLists.txt)
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_plugin.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_plugin.hpp)
- [plugins/mongo_db/mongo_db_plugin.cpp](file://plugins/mongo_db/mongo_db_plugin.cpp)
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_writer.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_writer.hpp)
- [plugins/mongo_db/mongo_db_writer.cpp](file://plugins/mongo_db/mongo_db_writer.cpp)
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_state.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_state.hpp)
- [plugins/mongo_db/mongo_db_state.cpp](file://plugins/mongo_db/mongo_db_state.cpp)
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_types.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_types.hpp)
- [plugins/mongo_db/mongo_db_types.cpp](file://plugins/mongo_db/mongo_db_types.cpp)
- [share/vizd/config/config_mongo.ini](file://share/vizd/config/config_mongo.ini)
- [share/vizd/config/config_debug_mongo.ini](file://share/vizd/config/config_debug_mongo.ini)
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
10. [Appendices](#appendices)

## Introduction
This document explains the MongoDB-enabled Dockerfile variant that integrates VIZ CPP Node with MongoDB for enhanced indexing and query capabilities. It covers the ENABLE_MONGO_PLUGIN CMake option, the MongoDB plugin architecture, data synchronization mechanisms, and performance characteristics. It also documents container setup, connection configuration, persistence strategies, and practical examples for enabling MongoDB integration, configuring replica sets, and optimizing queries. Finally, it compares the trade-offs between traditional SQLite-based storage and MongoDB integration, including storage requirements, backups, and migration procedures.

## Project Structure
The MongoDB integration spans Docker build configuration, CMake build options, and the MongoDB plugin implementation. The Dockerfile variant installs MongoDB C drivers and enables the MongoDB plugin during build. The plugin connects to MongoDB, listens to blockchain events, transforms state and operations into documents, and writes them to collections with secondary indexes.

```mermaid
graph TB
subgraph "Docker Build"
DF["Dockerfile-mongo<br/>Installs MongoDB C/C++ drivers<br/>Builds with ENABLE_MONGO_PLUGIN=TRUE"]
CFG["config_mongo.ini<br/>Enables mongo_db plugin<br/>Sets mongodb-uri"]
end
subgraph "CMake Build"
CM["CMakeLists.txt<br/>ENABLE_MONGO_PLUGIN option"]
PM["plugins/mongo_db/CMakeLists.txt<br/>Finds libmongocxx/libbsoncxx<br/>Links targets"]
end
subgraph "Plugin Runtime"
PLUG["mongo_db_plugin<br/>App base plugin"]
WR["mongo_db_writer<br/>Connects to MongoDB<br/>Bulk writes"]
STATE["state_writer<br/>Formats docs per operation"]
TYPES["mongo_db_types<br/>named_document, helpers"]
end
DF --> CM
CM --> PM
CFG --> PLUG
PLUG --> WR
WR --> STATE
WR --> TYPES
```

**Diagram sources**
- [Dockerfile-mongo](file://share/vizd/docker/Dockerfile-mongo#L1-L111)
- [CMakeLists.txt](file://CMakeLists.txt#L82-L89)
- [plugins/mongo_db/CMakeLists.txt](file://plugins/mongo_db/CMakeLists.txt#L1-L81)
- [plugins/mongo_db/mongo_db_plugin.cpp](file://plugins/mongo_db/mongo_db_plugin.cpp#L54-L125)
- [plugins/mongo_db/mongo_db_writer.cpp](file://plugins/mongo_db/mongo_db_writer.cpp#L39-L133)
- [plugins/mongo_db/mongo_db_state.cpp](file://plugins/mongo_db/mongo_db_state.cpp#L25-L29)
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_types.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_types.hpp#L40-L85)
- [share/vizd/config/config_mongo.ini](file://share/vizd/config/config_mongo.ini#L69-L72)

**Section sources**
- [Dockerfile-mongo](file://share/vizd/docker/Dockerfile-mongo#L1-L111)
- [CMakeLists.txt](file://CMakeLists.txt#L82-L89)
- [plugins/mongo_db/CMakeLists.txt](file://plugins/mongo_db/CMakeLists.txt#L1-L81)
- [share/vizd/config/config_mongo.ini](file://share/vizd/config/config_mongo.ini#L69-L72)

## Core Components
- ENABLE_MONGO_PLUGIN CMake option: Controls whether the MongoDB plugin is built and linked into the node. When enabled, the plugin target is added and linked against libmongocxx and libbsoncxx.
- Dockerfile-mongo: Installs MongoDB C drivers (mongo-c-driver and mongo-cxx-driver), clones and builds the C++ driver, and passes -DENABLE_MONGO_PLUGIN=TRUE to cmake.
- mongo_db_plugin: An appbase plugin that registers CLI/config options, initializes the MongoDB writer, subscribes to applied_block and post_apply_operation signals, and coordinates writes.
- mongo_db_writer: Manages MongoDB connection, bulk writes, document formatting, and index creation on first use per collection.
- state_writer: Visitor that formats blockchain state and operations into named_document entries with upsert semantics and optional removal updates.
- mongo_db_types: Provides document metadata (collection name, key, indexes), hashing helpers, and a multi-index container for efficient deduplication and updates.

**Section sources**
- [CMakeLists.txt](file://CMakeLists.txt#L82-L89)
- [plugins/mongo_db/CMakeLists.txt](file://plugins/mongo_db/CMakeLists.txt#L1-L81)
- [Dockerfile-mongo](file://share/vizd/docker/Dockerfile-mongo#L31-L58)
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_plugin.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_plugin.hpp#L14-L47)
- [plugins/mongo_db/mongo_db_plugin.cpp](file://plugins/mongo_db/mongo_db_plugin.cpp#L54-L125)
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_writer.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_writer.hpp#L42-L90)
- [plugins/mongo_db/mongo_db_writer.cpp](file://plugins/mongo_db/mongo_db_writer.cpp#L39-L133)
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_state.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_state.hpp#L19-L77)
- [plugins/mongo_db/mongo_db_state.cpp](file://plugins/mongo_db/mongo_db_state.cpp#L25-L29)
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_types.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_types.hpp#L40-L85)

## Architecture Overview
The MongoDB integration is layered:
- Build-time: ENABLE_MONGO_PLUGIN toggles inclusion of the mongo_db target and links MongoDB C++ drivers.
- Runtime: The plugin initializes a MongoDB client, subscribes to chain events, formats documents via state_writer, and performs bulk writes with upserts and selective index creation.

```mermaid
sequenceDiagram
participant Node as "vizd node"
participant Plugin as "mongo_db_plugin"
participant Writer as "mongo_db_writer"
participant DB as "MongoDB"
Node->>Plugin : Initialize plugin with options
Plugin->>Writer : initialize(uri, write_raw, ops)
Writer->>DB : Connect via URI
Note over Writer,DB : Connection established
Node->>Plugin : applied_block(block)
Plugin->>Writer : on_block(block)
Writer->>Writer : Buffer blocks until LRB
Writer->>Writer : Format docs via state_writer
Writer->>DB : Bulk insert/update per collection
DB-->>Writer : Acknowledged write
Node->>Plugin : post_apply_operation(op)
Plugin->>Writer : on_operation(op)
Writer->>Writer : Track virtual ops for rollback
```

**Diagram sources**
- [plugins/mongo_db/mongo_db_plugin.cpp](file://plugins/mongo_db/mongo_db_plugin.cpp#L71-L113)
- [plugins/mongo_db/mongo_db_writer.cpp](file://plugins/mongo_db/mongo_db_writer.cpp#L68-L133)
- [plugins/mongo_db/mongo_db_writer.cpp](file://plugins/mongo_db/mongo_db_writer.cpp#L272-L294)

## Detailed Component Analysis

### ENABLE_MONGO_PLUGIN CMake Option
- Purpose: Enables MongoDB plugin compilation and links against libmongocxx and libbsoncxx.
- Behavior:
  - When TRUE: Adds the mongo_db target, finds libmongocxx and libbsoncxx, links them, and exports the target.
  - When FALSE: Skips plugin build and linking.
- Impact on dependencies: Requires MongoDB C drivers to be installed in the build environment for successful linking.

```mermaid
flowchart TD
Start(["Configure CMake"]) --> CheckOpt["ENABLE_MONGO_PLUGIN = TRUE?"]
CheckOpt --> |Yes| FindDrivers["Find libmongocxx and libbsoncxx"]
FindDrivers --> AddTarget["Add mongo_db target<br/>Link drivers and deps"]
AddTarget --> Export["Export graphene::mongo_db"]
CheckOpt --> |No| Skip["Skip mongo_db build"]
Export --> End(["Build Complete"])
Skip --> End
```

**Diagram sources**
- [CMakeLists.txt](file://CMakeLists.txt#L82-L89)
- [plugins/mongo_db/CMakeLists.txt](file://plugins/mongo_db/CMakeLists.txt#L15-L67)

**Section sources**
- [CMakeLists.txt](file://CMakeLists.txt#L82-L89)
- [plugins/mongo_db/CMakeLists.txt](file://plugins/mongo_db/CMakeLists.txt#L1-L81)

### Dockerfile-mongo: MongoDB Driver Installation and Build
- Installs mongo-c-driver and mongo-cxx-driver from source.
- Configures and builds the C++ driver with a static prefix.
- Builds the project with -DENABLE_MONGO_PLUGIN=TRUE and installs artifacts.

```mermaid
flowchart TD
DStart(["Dockerfile-mongo build"]) --> InstallC["Install mongo-c-driver"]
InstallC --> InstallCpp["Clone mongo-cxx-driver<br/>Build and install"]
InstallCpp --> CopySrc["Copy repo sources"]
CopySrc --> Configure["cmake -DENABLE_MONGO_PLUGIN=TRUE .."]
Configure --> Make["make -j$(nproc)"]
Make --> Install["make install"]
Install --> DEnd(["Image ready"])
```

**Diagram sources**
- [Dockerfile-mongo](file://share/vizd/docker/Dockerfile-mongo#L31-L87)

**Section sources**
- [Dockerfile-mongo](file://share/vizd/docker/Dockerfile-mongo#L31-L87)

### mongo_db_plugin: Lifecycle and Event Subscription
- Registers CLI options for mongodb-uri, mongodb-write-raw-blocks, and mongodb-write-operations.
- Initializes the writer with parsed options and subscribes to:
  - applied_block: triggers block processing.
  - post_apply_operation: tracks virtual operations for rollback safety.

```mermaid
classDiagram
class mongo_db_plugin {
+set_program_options(cli, cfg)
+plugin_initialize(options)
+plugin_startup()
+plugin_shutdown()
-pimpl_ : mongo_db_plugin_impl
}
class mongo_db_plugin_impl {
+initialize(uri, write_raw, ops) bool
+on_block(block)
+on_operation(notification)
-writer : mongo_db_writer
-database() : database&
}
mongo_db_plugin --> mongo_db_plugin_impl : "owns"
```

**Diagram sources**
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_plugin.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_plugin.hpp#L14-L47)
- [plugins/mongo_db/mongo_db_plugin.cpp](file://plugins/mongo_db/mongo_db_plugin.cpp#L54-L125)

**Section sources**
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_plugin.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_plugin.hpp#L14-L47)
- [plugins/mongo_db/mongo_db_plugin.cpp](file://plugins/mongo_db/mongo_db_plugin.cpp#L54-L125)

### mongo_db_writer: Connection, Buffering, and Bulk Writes
- Establishes connection from URI, selects database, configures unordered bulk operations.
- Buffers blocks until last irreversible block advances, formats documents, and writes via bulk operations.
- Creates secondary indexes on first use per collection and supports removal updates.

```mermaid
flowchart TD
Init(["initialize(uri, write_raw, ops)"]) --> Conn["Connect to MongoDB"]
Conn --> Loop["on_block(block)"]
Loop --> Buffer["Buffer by block_num"]
Buffer --> LRB{"Block <= LRB?"}
LRB --> |Yes| Format["Format docs via state_writer"]
Format --> Upsert["Upsert or Insert per doc"]
Upsert --> MaybeIndex{"Index created?"}
MaybeIndex --> |No| CreateIdx["Create indexes"]
MaybeIndex --> |Yes| Next["Next block"]
LRB --> |No| Wait["Wait for LRB advance"]
CreateIdx --> Flush["write_data() bulk write"]
Next --> Flush
Flush --> Done(["Processed"])
```

**Diagram sources**
- [plugins/mongo_db/mongo_db_writer.cpp](file://plugins/mongo_db/mongo_db_writer.cpp#L39-L133)
- [plugins/mongo_db/mongo_db_writer.cpp](file://plugins/mongo_db/mongo_db_writer.cpp#L206-L233)
- [plugins/mongo_db/mongo_db_writer.cpp](file://plugins/mongo_db/mongo_db_writer.cpp#L272-L294)

**Section sources**
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_writer.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_writer.hpp#L42-L90)
- [plugins/mongo_db/mongo_db_writer.cpp](file://plugins/mongo_db/mongo_db_writer.cpp#L39-L133)
- [plugins/mongo_db/mongo_db_writer.cpp](file://plugins/mongo_db/mongo_db_writer.cpp#L206-L233)
- [plugins/mongo_db/mongo_db_writer.cpp](file://plugins/mongo_db/mongo_db_writer.cpp#L272-L294)

### state_writer: Operation-to-Document Formatting
- Implements visitor operators for various operations (e.g., vote, content, author_reward, curation_reward, content_reward, content_benefactor_reward).
- Produces named_document entries with:
  - collection_name: target MongoDB collection.
  - key/keyval: composite key for deduplication/upsert.
  - indexes_to_create: secondary index specs (e.g., content root content, content votes).
  - $set body: fields to update.
- Supports removal documents for soft-deletion semantics.

```mermaid
classDiagram
class state_writer {
+operator()(vote_operation)
+operator()(content_operation)
+operator()(author_reward_operation)
+operator()(curation_reward_operation)
+operator()(content_reward_operation)
+operator()(content_benefactor_reward_operation)
-create_document(name, key, keyval) named_document
-create_removal_document(name, key, keyval) named_document
-db_ : database&
-state_block : signed_block
-all_docs : db_map
}
class named_document {
+string collection_name
+document doc
+bool is_removal
+vector<document> indexes_to_create
+string key
+string keyval
}
state_writer --> named_document : "produces"
```

**Diagram sources**
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_state.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_state.hpp#L19-L77)
- [plugins/mongo_db/mongo_db_state.cpp](file://plugins/mongo_db/mongo_db_state.cpp#L31-L49)
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_types.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_types.hpp#L40-L85)

**Section sources**
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_state.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_state.hpp#L19-L77)
- [plugins/mongo_db/mongo_db_state.cpp](file://plugins/mongo_db/mongo_db_state.cpp#L203-L248)
- [plugins/mongo_db/mongo_db_state.cpp](file://plugins/mongo_db/mongo_db_state.cpp#L411-L473)
- [plugins/mongo_db/mongo_db_state.cpp](file://plugins/mongo_db/mongo_db_state.cpp#L501-L530)
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_types.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_types.hpp#L40-L85)

### mongo_db_types: Data Structures and Helpers
- named_document: carries collection metadata, BSON document payload, keying, and index specs.
- db_map: multi_index container with random access and hashed unique index for efficient deduplication and replacement.
- Hashing and formatting helpers: SHA1-based OID generation and typed field formatters for BSON.

```mermaid
classDiagram
class named_document {
+string collection_name
+document doc
+bool is_removal
+vector<document> indexes_to_create
+string key
+string keyval
}
class db_map {
<<multi_index_container>>
+random_access
+hashed_unique(composite_key)
}
class helpers {
+hash_oid(value) string
+format_oid(doc, name, value)
+format_value(doc, name, value)
}
db_map --> named_document : "stores"
```

**Diagram sources**
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_types.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_types.hpp#L40-L85)
- [plugins/mongo_db/mongo_db_types.cpp](file://plugins/mongo_db/mongo_db_types.cpp#L7-L14)

**Section sources**
- [plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_types.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_types.hpp#L40-L85)
- [plugins/mongo_db/mongo_db_types.cpp](file://plugins/mongo_db/mongo_db_types.cpp#L7-L14)

## Dependency Analysis
- Build dependencies:
  - Dockerfile-mongo installs mongo-c-driver and mongo-cxx-driver and passes -DENABLE_MONGO_PLUGIN=TRUE.
  - plugins/mongo_db/CMakeLists.txt requires libmongocxx and libbsoncxx to be found and links them to the mongo_db target.
  - Root CMakeLists.txt defines ENABLE_MONGO_PLUGIN and conditionally sets compiler flags and library linkage.
- Runtime dependencies:
  - mongo_db_plugin depends on chain plugin and JSON RPC plugin.
  - mongo_db_writer depends on mongocxx client and BSON builders.

```mermaid
graph LR
RootCMake["Root CMakeLists.txt<br/>ENABLE_MONGO_PLUGIN"] --> PluginCMake["plugins/mongo_db/CMakeLists.txt"]
PluginCMake --> Drivers["libmongocxx / libbsoncxx"]
Docker["Dockerfile-mongo"] --> Drivers
PluginCMake --> Target["graphene::mongo_db target"]
Target --> AppBase["appbase / fc / graphene libs"]
Plugin["mongo_db_plugin"] --> Chain["chain plugin"]
Plugin --> Writer["mongo_db_writer"]
Writer --> Mongo["MongoDB server"]
```

**Diagram sources**
- [CMakeLists.txt](file://CMakeLists.txt#L82-L89)
- [plugins/mongo_db/CMakeLists.txt](file://plugins/mongo_db/CMakeLists.txt#L15-L67)
- [Dockerfile-mongo](file://share/vizd/docker/Dockerfile-mongo#L31-L58)
- [plugins/mongo_db/mongo_db_plugin.cpp](file://plugins/mongo_db/mongo_db_plugin.cpp#L17-L19)
- [plugins/mongo_db/mongo_db_writer.cpp](file://plugins/mongo_db/mongo_db_writer.cpp#L39-L66)

**Section sources**
- [CMakeLists.txt](file://CMakeLists.txt#L82-L89)
- [plugins/mongo_db/CMakeLists.txt](file://plugins/mongo_db/CMakeLists.txt#L15-L67)
- [Dockerfile-mongo](file://share/vizd/docker/Dockerfile-mongo#L31-L58)
- [plugins/mongo_db/mongo_db_plugin.cpp](file://plugins/mongo_db/mongo_db_plugin.cpp#L17-L19)
- [plugins/mongo_db/mongo_db_writer.cpp](file://plugins/mongo_db/mongo_db_writer.cpp#L39-L66)

## Performance Considerations
- Bulk writes: Unordered bulk operations reduce latency by batching inserts/updates per collection.
- Index creation: Indexes are created on first use per collection to avoid repeated overhead.
- Last irreversible block (LRB) buffering: Ensures writes occur only after blocks become irreversible, preventing rollback costs.
- Optional raw block writes: Can be disabled to reduce storage and improve performance when only state/operation indexing is needed.
- Virtual operations: Only virtual operations are written to plugins, reducing unnecessary processing for non-virtual ops.
- Shared memory sizing and thread pool tuning: Configuration options in the MongoDB config help balance concurrency and lock contention.

[No sources needed since this section provides general guidance]

## Troubleshooting Guide
- MongoDB plugin disabled:
  - Cause: mongodb-uri not provided or initialization fails.
  - Action: Verify config_mongo.ini has a valid mongodb-uri and plugin list includes mongo_db.
- Driver installation failures:
  - Cause: Missing system packages or incompatible driver versions.
  - Action: Ensure Dockerfile-mongo installs mongo-c-driver and mongo-cxx-driver; confirm cmake can find libmongocxx and libbsoncxx.
- Connection errors:
  - Cause: Incorrect URI, unreachable host, or authentication issues.
  - Action: Validate URI in config_mongo.ini; check container networking and firewall rules.
- Bulk write failures:
  - Cause: Temporary server issues or malformed documents.
  - Action: Review logs around write_data(); ensure indexes are compatible with document shapes.
- Index creation errors:
  - Cause: Conflicting index definitions or server-side constraints.
  - Action: Inspect index specs generated by state_writer and adjust as needed.

**Section sources**
- [plugins/mongo_db/mongo_db_plugin.cpp](file://plugins/mongo_db/mongo_db_plugin.cpp#L84-L109)
- [plugins/mongo_db/mongo_db_writer.cpp](file://plugins/mongo_db/mongo_db_writer.cpp#L39-L66)
- [plugins/mongo_db/mongo_db_writer.cpp](file://plugins/mongo_db/mongo_db_writer.cpp#L272-L294)
- [share/vizd/config/config_mongo.ini](file://share/vizd/config/config_mongo.ini#L69-L72)

## Conclusion
The MongoDB-enabled Dockerfile variant integrates VIZ CPP Node with MongoDB through a dedicated plugin that listens to blockchain events, formats documents, and performs efficient bulk writes with secondary indexing. The ENABLE_MONGO_PLUGIN CMake option controls build inclusion and links required drivers. Container setup ensures drivers are present and the node is configured to enable the plugin and connect to MongoDB. While MongoDB adds query flexibility and indexing capabilities, it introduces operational overhead and storage considerations compared to native storage. Proper configuration, index planning, and monitoring are essential for reliable performance.

[No sources needed since this section summarizes without analyzing specific files]

## Appendices

### Practical Examples

- Enabling MongoDB integration in Docker:
  - Use the MongoDB Dockerfile to build an image with drivers and ENABLE_MONGO_PLUGIN=TRUE.
  - Mount persistent volumes for /var/lib/vizd and /etc/vizd.
  - Expose ports 8090 (HTTP), 8091 (WebSocket), and 2001 (P2P) as defined in the Dockerfile.

- Connecting to MongoDB:
  - Set mongodb-uri in config_mongo.ini to point to your MongoDB instance or replica set.
  - Example: mongodb://host:port/dbname or mongodb://user:pass@host:port/dbname with appropriate credentials.

- Configuring replica sets:
  - Deploy MongoDB with replica set configuration.
  - Update mongodb-uri to include replica set hosts and set appropriate read/write concerns in the MongoDB client options if needed.

- Optimizing query performance:
  - Leverage secondary indexes created per collection by state_writer (e.g., content root content, content votes).
  - Limit tracked operations via mongodb-write-operations to reduce write volume.
  - Disable raw block writes if only state/operation indexing is required.

- Trade-offs and migration:
  - Storage requirements: MongoDB documents can be larger than native storage depending on normalization; monitor disk usage.
  - Backup strategies: Use MongoDB-native backup tools (e.g., logical dumps or snapshot-based backups) alongside chain snapshots.
  - Migration: To switch from native storage to MongoDB, deploy the MongoDB-enabled node, allow indexing to complete, and validate queries. Back up MongoDB collections before major upgrades.

**Section sources**
- [Dockerfile-mongo](file://share/vizd/docker/Dockerfile-mongo#L97-L111)
- [share/vizd/config/config_mongo.ini](file://share/vizd/config/config_mongo.ini#L69-L72)
- [plugins/mongo_db/mongo_db_writer.cpp](file://plugins/mongo_db/mongo_db_writer.cpp#L227-L232)
- [plugins/mongo_db/mongo_db_plugin.cpp](file://plugins/mongo_db/mongo_db_plugin.cpp#L58-L67)