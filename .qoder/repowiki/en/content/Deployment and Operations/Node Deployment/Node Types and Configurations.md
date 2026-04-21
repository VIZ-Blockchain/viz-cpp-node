# Node Types and Configurations

<cite>
**Referenced Files in This Document**
- [main.cpp](file://programs/vizd/main.cpp)
- [config.ini](file://share/vizd/config/config.ini)
- [config_witness.ini](file://share/vizd/config/config_witness.ini)
- [config_debug.ini](file://share/vizd/config/config_debug.ini)
- [config_mongo.ini](file://share/vizd/config/config_mongo.ini)
- [config_testnet.ini](file://share/vizd/config/config_testnet.ini)
- [config_debug_mongo.ini](file://share/vizd/config/config_debug_mongo.ini)
- [config_stock_exchange.ini](file://share/vizd/config/config_stock_exchange.ini)
- [Dockerfile-production](file://share/vizd/docker/Dockerfile-production)
- [Dockerfile-testnet](file://share/vizd/docker/Dockerfile-testnet)
- [Dockerfile-mongo](file://share/vizd/docker/Dockerfile-mongo)
- [Dockerfile-lowmem](file://share/vizd/docker/Dockerfile-lowmem)
- [testnet.md](file://documentation/testnet.md)
- [debug_node_plugin.md](file://documentation/debug_node_plugin.md)
- [witness.hpp](file://plugins/witness/include/graphene/plugins/witness/witness.hpp)
- [mongo_db_plugin.hpp](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_plugin.hpp)
- [plugin.hpp](file://plugins/debug_node/include/graphene/plugins/debug_node/plugin.hpp)
</cite>

## Update Summary
**Changes Made**
- Updated P2P endpoint configuration section to reflect standardized port 2001 across all configuration files
- Removed references to external seed node file management system
- Updated seed node configuration to show direct integration into config.ini templates
- Added config_stock_exchange.ini to the configuration templates section
- Updated section sources to reflect current configuration files

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
This document explains the different VIZ node types and their specific configurations. It covers full node setup with blockchain synchronization and API exposure, witness node configuration for block production and key management, seed node configuration for network bootstrap, and specialized configurations for testnet, debug, and MongoDB-integrated nodes. It also provides configuration file templates, parameter explanations, operational differences, performance tuning, resource allocation recommendations, monitoring requirements, and a comparison matrix across node configurations.

## Project Structure
The repository organizes node configuration templates under share/vizd/config, Dockerfiles for different deployment modes under share/vizd/docker, and documentation under documentation. The main executable initializes plugins and loads configuration.

```mermaid
graph TB
A["programs/vizd/main.cpp"] --> B["share/vizd/config/config.ini"]
A --> C["share/vizd/config/config_witness.ini"]
A --> D["share/vizd/config/config_debug.ini"]
A --> E["share/vizd/config/config_mongo.ini"]
A --> F["share/vizd/config/config_testnet.ini"]
A --> G["share/vizd/config/config_debug_mongo.ini"]
A --> H["share/vizd/config/config_stock_exchange.ini"]
I["share/vizd/docker/Dockerfile-production"] --> B
J["share/vizd/docker/Dockerfile-testnet"] --> F
K["share/vizd/docker/Dockerfile-mongo"] --> E
L["share/vizd/docker/Dockerfile-lowmem"] --> B
```

**Diagram sources**
- [main.cpp:106-158](file://programs/vizd/main.cpp#L106-L158)
- [config.ini:1-136](file://share/vizd/config/config.ini#L1-L136)
- [config_witness.ini:1-138](file://share/vizd/config/config_witness.ini#L1-L138)
- [config_debug.ini:1-126](file://share/vizd/config/config_debug.ini#L1-L126)
- [config_mongo.ini:1-135](file://share/vizd/config/config_mongo.ini#L1-L135)
- [config_testnet.ini:1-132](file://share/vizd/config/config_testnet.ini#L1-L132)
- [config_debug_mongo.ini:1-135](file://share/vizd/config/config_debug_mongo.ini#L1-L135)
- [config_stock_exchange.ini:1-114](file://share/vizd/config/config_stock_exchange.ini#L1-L114)
- [Dockerfile-production:74-87](file://share/vizd/docker/Dockerfile-production#L74-L87)
- [Dockerfile-testnet:75-87](file://share/vizd/docker/Dockerfile-testnet#L75-L87)
- [Dockerfile-mongo:97-110](file://share/vizd/docker/Dockerfile-mongo#L97-L110)
- [Dockerfile-lowmem:68-81](file://share/vizd/docker/Dockerfile-lowmem#L68-L81)

**Section sources**
- [main.cpp:106-158](file://programs/vizd/main.cpp#L106-L158)
- [config.ini:1-136](file://share/vizd/config/config.ini#L1-L136)
- [Dockerfile-production:74-87](file://share/vizd/docker/Dockerfile-production#L74-L87)
- [Dockerfile-testnet:75-87](file://share/vizd/docker/Dockerfile-testnet#L75-L87)
- [Dockerfile-mongo:97-110](file://share/vizd/docker/Dockerfile-mongo#L97-L110)
- [Dockerfile-lowmem:68-81](file://share/vizd/docker/Dockerfile-lowmem#L68-L81)

## Core Components
- Full node: default configuration with broad plugin set for general-purpose operation and API exposure.
- Witness node: enables block production with witness and witness_api plugins, requires witness name and private key.
- Debug node: specialized configuration for simulation and experimentation with debug_node plugin.
- Testnet node: minimal configuration optimized for testnet operation with enabled stale production.
- MongoDB-integrated node: includes mongo_db plugin for external database indexing and analytics.
- Stock exchange node: specialized configuration for trading infrastructure with optimized performance settings.
- Seed node: bootstrap configuration with predefined seed peers for network discovery.

Key configuration parameters:
- P2P endpoint (standardized to port 2001) and seed nodes for connectivity.
- Webserver endpoints for HTTP and WebSocket APIs.
- Plugin selection for functional capabilities.
- Shared memory sizing and growth thresholds for database performance.
- Lock wait timeouts and retries for RPC concurrency.
- Witness participation and block production controls.
- Logging configuration via appenders and loggers.

**Updated** Standardized P2P endpoint configuration to port 2001 across all configuration files, removing the previous inconsistency where some configurations used different ports.

**Section sources**
- [config.ini:1-136](file://share/vizd/config/config.ini#L1-L136)
- [config_witness.ini:68-138](file://share/vizd/config/config_witness.ini#L68-L138)
- [config_debug.ini:69-126](file://share/vizd/config/config_debug.ini#L69-L126)
- [config_mongo.ini:69-135](file://share/vizd/config/config_mongo.ini#L69-L135)
- [config_testnet.ini:69-132](file://share/vizd/config/config_testnet.ini#L69-L132)
- [config_debug_mongo.ini:69-135](file://share/vizd/config/config_debug_mongo.ini#L69-L135)
- [config_stock_exchange.ini:69-114](file://share/vizd/config/config_stock_exchange.ini#L69-L114)

## Architecture Overview
The VIZ node is a modular application built on appbase with a plugin architecture. The main program registers and initializes plugins, loads configuration, and starts the server loop. Different node types are achieved by selecting different plugins and configuration files.

```mermaid
graph TB
subgraph "Executable"
MAIN["programs/vizd/main.cpp<br/>Registers plugins and starts app"]
end
subgraph "Plugins"
CHAIN["chain"]
P2P["p2p"]
WEBSERVER["webserver"]
NB["network_broadcast_api"]
DBAPI["database_api"]
ACC_HIST["account_history"]
OP_HIST["operation_history"]
WIT["witness"]
WIT_API["witness_api"]
DEBUG["debug_node"]
MONGO["mongo_db"]
TESTAPI["test_api"]
TAGS["tags"]
FOLLOW["follow"]
SOCIAL["social_network"]
PRIVATE_MSG["private_message"]
COMMITTEE["committee_api"]
INVITE["invite_api"]
PAID_SUB["paid_subscription_api"]
CUSTOM["custom_protocol_api"]
STOCK_EX["stock_exchange"]
end
MAIN --> CHAIN
MAIN --> P2P
MAIN --> WEBSERVER
MAIN --> NB
MAIN --> DBAPI
MAIN --> ACC_HIST
MAIN --> OP_HIST
MAIN --> WIT
MAIN --> WIT_API
MAIN --> DEBUG
MAIN --> MONGO
MAIN --> TESTAPI
MAIN --> TAGS
MAIN --> FOLLOW
MAIN --> SOCIAL
MAIN --> PRIVATE_MSG
MAIN --> COMMITTEE
MAIN --> INVITE
MAIN --> PAID_SUB
MAIN --> CUSTOM
MAIN --> STOCK_EX
```

**Diagram sources**
- [main.cpp:62-91](file://programs/vizd/main.cpp#L62-L91)
- [witness.hpp:34-65](file://plugins/witness/include/graphene/plugins/witness/witness.hpp#L34-L65)
- [mongo_db_plugin.hpp:14-47](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_plugin.hpp#L14-L47)
- [plugin.hpp:38-108](file://plugins/debug_node/include/graphene/plugins/debug_node/plugin.hpp#L38-L108)

**Section sources**
- [main.cpp:62-91](file://programs/vizd/main.cpp#L62-L91)
- [witness.hpp:34-65](file://plugins/witness/include/graphene/plugins/witness/witness.hpp#L34-L65)
- [mongo_db_plugin.hpp:14-47](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_plugin.hpp#L14-L47)
- [plugin.hpp:38-108](file://plugins/debug_node/include/graphene/plugins/debug_node/plugin.hpp#L38-L108)

## Detailed Component Analysis

### Full Node Configuration
- Purpose: General-purpose node with comprehensive API coverage and history tracking.
- Key parameters:
  - P2P endpoint standardized to port 2001 with integrated seed nodes for connectivity.
  - HTTP and WebSocket webserver endpoints for API access.
  - Broad plugin set including chain, p2p, json_rpc, webserver, network_broadcast_api, database_api, account_history, operation_history, committee_api, invite_api, paid_subscription_api, custom_protocol_api, account_by_key, block_info, raw_block.
  - Shared memory sizing and growth thresholds to manage database capacity.
  - Lock wait and retry settings for RPC client concurrency.
  - Optional virtual operations skipping and vote clearing to optimize performance.
- Operational differences:
  - Exposes APIs publicly by default.
  - Tracks extensive operation history.
  - Suitable for production and public API services.

**Updated** P2P endpoint now standardized to port 2001 with integrated seed node configuration directly in the template.

**Section sources**
- [config.ini:1-136](file://share/vizd/config/config.ini#L1-L136)

### Witness Node Configuration
- Purpose: Block production node with witness and witness_api plugins.
- Key parameters:
  - P2P endpoint standardized to port 2001 with integrated seed nodes.
  - Local webserver endpoints for internal access.
  - Plugins: chain, p2p, json_rpc, webserver, network_broadcast_api, database_api, witness, witness_api.
  - Witness participation and stale production controls.
  - Required witness name and private key for block signing.
  - Optimized logging configuration.
- Operational differences:
  - Requires valid witness credentials.
  - Can operate with stricter network isolation (local webserver endpoints).
  - Enables witness-specific APIs.

```mermaid
sequenceDiagram
participant Operator as "Operator"
participant Node as "VIZ Node"
participant Witness as "Witness Plugin"
participant Chain as "Chain Plugin"
Operator->>Node : Start with witness config
Node->>Witness : Initialize with witness name and private key
Witness->>Chain : Request scheduled slot
Chain-->>Witness : Block production slot
Witness->>Chain : Produce block with private key
Chain-->>Node : Accept block
Node-->>Operator : Report block production status
```

**Diagram sources**
- [config_witness.ini:82-86](file://share/vizd/config/config_witness.ini#L82-L86)
- [witness.hpp:20-32](file://plugins/witness/include/graphene/plugins/witness/witness.hpp#L20-L32)

**Section sources**
- [config_witness.ini:68-138](file://share/vizd/config/config_witness.ini#L68-L138)
- [witness.hpp:34-65](file://plugins/witness/include/graphene/plugins/witness/witness.hpp#L34-L65)

### Debug Node Configuration
- Purpose: Simulation and experimentation with debug_node plugin for "what-if" scenarios.
- Key parameters:
  - Minimal plugin set including chain, p2p, json_rpc, webserver, network_broadcast_api, database_api, debug_node, test_api, and others for deep inspection.
  - Shared memory tuned for smaller footprint during development.
  - Stale production enabled for accelerated simulation.
  - Local webserver endpoints for secure access.
- Operational differences:
  - Designed for local-only access.
  - Provides APIs to push blocks, generate blocks, and update objects for simulation.
  - Useful for hardfork testing and state experimentation.

```mermaid
flowchart TD
Start(["Start Debug Node"]) --> LoadBlocks["Load blocks from directory"]
LoadBlocks --> Simulate["Simulate block production"]
Simulate --> UpdateState["Update chain state via debug APIs"]
UpdateState --> Verify["Verify operations and history"]
Verify --> End(["Stop and analyze"])
```

**Diagram sources**
- [config_debug.ini:69-126](file://share/vizd/config/config_debug.ini#L69-L126)
- [plugin.hpp:62-90](file://plugins/debug_node/include/graphene/plugins/debug_node/plugin.hpp#L62-L90)

**Section sources**
- [config_debug.ini:69-126](file://share/vizd/config/config_debug.ini#L69-L126)
- [debug_node_plugin.md:50-134](file://documentation/debug_node_plugin.md#L50-L134)
- [plugin.hpp:38-108](file://plugins/debug_node/include/graphene/plugins/debug_node/plugin.hpp#L38-L108)

### Testnet Node Configuration
- Purpose: Lightweight testnet node with stale production enabled for rapid iteration.
- Key parameters:
  - P2P endpoint on port 4243 (different from mainnet) with integrated seed nodes.
  - HTTP and WebSocket endpoints.
  - Minimal plugin set focused on chain, p2p, json_rpc, webserver, network_broadcast_api, database_api, witness, witness_api.
  - Stale production enabled and witness participation set to minimal.
  - Predefined testnet witness and private key.
- Operational differences:
  - Optimized for testnet with snapshot support.
  - Simplified plugin set reduces overhead.
  - Suitable for CI and automated testing.

**Section sources**
- [config_testnet.ini:69-132](file://share/vizd/config/config_testnet.ini#L69-L132)
- [testnet.md:21-37](file://documentation/testnet.md#L21-L37)

### MongoDB-Integrated Node Configuration
- Purpose: Node with mongo_db plugin for external analytics and off-chain indexing.
- Key parameters:
  - P2P endpoint on port 4243 with integrated seed nodes.
  - HTTP and WebSocket endpoints.
  - Plugin set including mongo_db alongside standard plugins.
  - MongoDB connection URI for external database.
  - Market history bucket sizes and retention.
- Operational differences:
  - Requires MongoDB instance and drivers.
  - Adds significant I/O overhead; monitor storage and network.
  - Enables advanced analytics and historical reporting.

**Section sources**
- [config_mongo.ini:69-135](file://share/vizd/config/config_mongo.ini#L69-L135)

### Stock Exchange Node Configuration
- Purpose: Specialized trading infrastructure node with optimized performance settings.
- Key parameters:
  - P2P endpoint on port 4243 with integrated seed nodes.
  - Local webserver endpoints for internal trading systems.
  - Optimized plugin set focusing on chain, p2p, json_rpc, webserver, network_broadcast_api, witness, database_api, block_info, raw_block, operation_history, account_history, witness_api.
  - Skip virtual operations and clear votes before block for improved performance.
  - Reduced read wait retries for faster response times.
- Operational differences:
  - Designed for high-frequency trading scenarios.
  - Optimized for minimal latency and maximum throughput.
  - Suitable for production trading infrastructure.

**Section sources**
- [config_stock_exchange.ini:69-114](file://share/vizd/config/config_stock_exchange.ini#L69-L114)

### Seed Node Configuration
- Purpose: Bootstrap and peer discovery for network formation.
- Key parameters:
  - P2P endpoint bound to port 2001 for standard network compatibility.
  - Integrated seed nodes configured directly in the template for immediate connectivity.
  - Minimal plugin set to reduce resource usage.
- Operational differences:
  - Does not synchronize the chain automatically.
  - Serves as a discovery anchor for other nodes.
  - Useful for private networks or isolated environments.

**Updated** Removed references to external seed node file management system as seed nodes are now integrated directly into the config.ini templates.

**Section sources**
- [config.ini:7-12](file://share/vizd/config/config.ini#L7-L12)

## Dependency Analysis
The main executable registers and initializes plugins. Witness and MongoDB plugins depend on the chain plugin. The debug node plugin depends on the chain plugin for state manipulation. Dockerfiles embed configuration templates and expose ports for RPC and P2P.

```mermaid
graph LR
MAIN["programs/vizd/main.cpp"] --> REG["Register plugins"]
REG --> WIT["witness.hpp"]
REG --> MONGO["mongo_db_plugin.hpp"]
REG --> DEBUG["plugin.hpp"]
MAIN --> CFG["Config files"]
CFG --> DOCKER["Dockerfiles"]
```

**Diagram sources**
- [main.cpp:62-91](file://programs/vizd/main.cpp#L62-L91)
- [witness.hpp:34-65](file://plugins/witness/include/graphene/plugins/witness/witness.hpp#L34-L65)
- [mongo_db_plugin.hpp:14-47](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_plugin.hpp#L14-L47)
- [plugin.hpp:38-108](file://plugins/debug_node/include/graphene/plugins/debug_node/plugin.hpp#L38-L108)

**Section sources**
- [main.cpp:62-91](file://programs/vizd/main.cpp#L62-L91)
- [witness.hpp:34-65](file://plugins/witness/include/graphene/plugins/witness/witness.hpp#L34-L65)
- [mongo_db_plugin.hpp:14-47](file://plugins/mongo_db/include/graphene/plugins/mongo_db/mongo_db_plugin.hpp#L14-L47)
- [plugin.hpp:38-108](file://plugins/debug_node/include/graphene/plugins/debug_node/plugin.hpp#L38-L108)

## Performance Considerations
- Shared memory sizing:
  - Increase shared-file-size and adjust min-free-shared-file-size and inc-shared-file-size to accommodate larger histories and reduce resizing pressure.
  - Monitor free space checks via block-num-check-free-size to balance safety and performance.
- Concurrency and locks:
  - single-write-thread reduces contention on database writes.
  - Tune read-wait-micro and max-read-wait-retries, write-wait-micro and max-write-wait-retries to match workload patterns.
- Plugin selection:
  - Disable unused plugins to reduce memory and CPU overhead.
  - Skip virtual operations and clear old votes to improve performance on full nodes.
- Witness node specifics:
  - Keep participation thresholds low for testnet; raise for production.
  - Ensure private key availability and secure storage.
- Debug node specifics:
  - Use local-only endpoints and smaller shared memory for development.
  - Enable stale production for fast simulation.
- MongoDB node specifics:
  - Provision adequate disk IOPS and network bandwidth for MongoDB.
  - Monitor write amplification and index maintenance costs.
- Stock exchange node specifics:
  - Optimize for high-frequency trading with reduced read wait retries.
  - Enable skip-virtual-ops and clear-votes-before-block for maximum throughput.
- Resource allocation recommendations:
  - Full node: moderate CPU, substantial RAM for shared memory, fast SSD for block log and database.
  - Witness node: dedicated CPU cores, reliable network, secure key management.
  - Debug node: modest resources, local SSD, restricted network access.
  - Testnet node: minimal resources, ephemeral data.
  - MongoDB node: high IOPS storage, separate MongoDB cluster, network isolation.
  - Stock exchange node: high-performance hardware, low-latency networking, optimized storage.

## Troubleshooting Guide
- RPC lock errors:
  - Adjust read-wait-micro/read-wait-retries and write-wait-micro/write-wait-retries.
  - Consider enabling single-write-thread to reduce lock contention.
- Insufficient shared memory:
  - Increase shared-file-size and tune min-free-shared-file-size and inc-shared-file-size.
  - Monitor block-num-check-free-size frequency.
- Witness production issues:
  - Verify witness name and private key.
  - Check participation thresholds and network synchronization.
- Debug node anomalies:
  - Confirm local-only endpoints and secure access.
  - Validate block pushing and generation commands.
- MongoDB integration:
  - Verify mongodb-uri and connectivity.
  - Monitor MongoDB performance and replica set health.
- P2P connectivity issues:
  - Verify p2p-endpoint is set to port 2001 for standard network compatibility.
  - Check p2p-seed-node configuration for proper peer discovery.
- Stock exchange performance:
  - Monitor read-wait-micro settings for optimal latency.
  - Ensure skip-virtual-ops and clear-votes-before-block are enabled.

**Section sources**
- [config.ini:22-67](file://share/vizd/config/config.ini#L22-L67)
- [config_witness.ini:76-86](file://share/vizd/config/config_witness.ini#L76-L86)
- [config_debug.ini:95-105](file://share/vizd/config/config_debug.ini#L95-L105)
- [config_mongo.ini:71-72](file://share/vizd/config/config_mongo.ini#L71-L72)
- [config_stock_exchange.ini:22-34](file://share/vizd/config/config_stock_exchange.ini#L22-L34)

## Conclusion
Different VIZ node types serve distinct operational needs. Full nodes provide broad API coverage, witness nodes enable consensus participation, debug nodes support experimentation, testnet nodes accelerate development, MongoDB-integrated nodes enable advanced analytics, and stock exchange nodes optimize for trading infrastructure. Proper configuration, performance tuning, and monitoring are essential for each type to achieve reliable operation. The standardized P2P endpoint configuration ensures consistent network compatibility across all node types.

## Appendices

### Configuration Templates and Parameters
- Full node template: [config.ini:1-136](file://share/vizd/config/config.ini#L1-L136)
- Witness node template: [config_witness.ini:1-138](file://share/vizd/config/config_witness.ini#L1-L138)
- Debug node template: [config_debug.ini:1-126](file://share/vizd/config/config_debug.ini#L1-L126)
- MongoDB node template: [config_mongo.ini:1-135](file://share/vizd/config/config_mongo.ini#L1-L135)
- Testnet template: [config_testnet.ini:1-132](file://share/vizd/config/config_testnet.ini#L1-L132)
- Debug + MongoDB template: [config_debug_mongo.ini:1-135](file://share/vizd/config/config_debug_mongo.ini#L1-L135)
- Stock exchange template: [config_stock_exchange.ini:1-114](file://share/vizd/config/config_stock_exchange.ini#L1-L114)

### Node Type Comparison Matrix

| Feature | Full Node | Witness Node | Debug Node | Testnet Node | MongoDB Node | Stock Exchange Node |
|---|---|---|---|---|---|---|
| P2P endpoint | 0.0.0.0:2001 | 0.0.0.0:2001 | Not set | 0.0.0.0:4243 | 0.0.0.0:4243 | 0.0.0.0:4243 |
| Seed nodes | Integrated | Integrated | Not set | Integrated | Integrated | Integrated |
| Webserver endpoints | Public | Local | Local | Public | Public | Local |
| Plugins | Broad | Essential + witness | Debug + test | Minimal | Mongo + essentials | Optimized trading |
| Shared memory | Large | Large | Small | Large | Large | Large |
| Stale production | Off | On (configurable) | On | On | Off | Off |
| Witness participation | N/A | Configurable | N/A | Configurable | N/A | N/A |
| Private key | N/A | Required | N/A | N/A | N/A | N/A |
| MongoDB integration | No | No | No | No | Yes | No |
| Typical use | Production API | Consensus | Dev/Test | CI/Testing | Analytics | Trading Infrastructure |
| Port standardization | ✅ 2001 | ✅ 2001 | ❌ Not set | ❌ 4243 | ❌ 4243 | ❌ 4243 |

**Updated** Added stock exchange node to the comparison matrix and updated P2P endpoint port information to reflect the standardized configuration.

**Section sources**
- [config.ini:1-136](file://share/vizd/config/config.ini#L1-L136)
- [config_witness.ini:68-138](file://share/vizd/config/config_witness.ini#L68-L138)
- [config_debug.ini:69-126](file://share/vizd/config/config_debug.ini#L69-L126)
- [config_testnet.ini:69-132](file://share/vizd/config/config_testnet.ini#L69-L132)
- [config_mongo.ini:69-135](file://share/vizd/config/config_mongo.ini#L69-L135)
- [config_stock_exchange.ini:69-114](file://share/vizd/config/config_stock_exchange.ini#L69-L114)