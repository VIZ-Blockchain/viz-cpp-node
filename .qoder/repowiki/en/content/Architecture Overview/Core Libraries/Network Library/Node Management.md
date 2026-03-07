# Node Management

<cite>
**Referenced Files in This Document**
- [node.hpp](file://libraries/network/include/graphene/network/node.hpp)
- [node.cpp](file://libraries/network/node.cpp)
- [peer_connection.hpp](file://libraries/network/include/graphene/network/peer_connection.hpp)
- [peer_database.hpp](file://libraries/network/include/graphene/network/peer_database.hpp)
- [message.hpp](file://libraries/network/include/graphene/network/message.hpp)
- [config.hpp](file://libraries/network/include/graphene/network/config.hpp)
- [core_messages.hpp](file://libraries/network/include/graphene/network/core_messages.hpp)
- [exceptions.hpp](file://libraries/network/include/graphene/network/exceptions.hpp)
- [stcp_socket.hpp](file://libraries/network/include/graphene/network/stcp_socket.hpp)
- [message_oriented_connection.hpp](file://libraries/network/include/graphene/network/message_oriented_connection.hpp)
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
This document describes the Node Management component responsible for orchestrating network peers, maintaining connectivity, and managing blockchain synchronization in the P2P layer. It covers the node.hpp class interface, the node_delegate integration for blockchain callbacks, configuration and lifecycle APIs, peer management, and network broadcasting with inventory tracking. It also includes diagrams, practical examples, and troubleshooting guidance for common startup and connection issues.

## Project Structure
The Node Management functionality spans several headers and the implementation source file:
- Public interface: node.hpp defines the node class, node_delegate interface, and related types.
- Implementation: node.cpp implements the node lifecycle, peer orchestration, message routing, synchronization, and inventory management.
- Peer model: peer_connection.hpp defines the peer connection abstraction and state machine.
- Persistence: peer_database.hpp provides persistent peer discovery records.
- Messaging: message.hpp defines the generic message envelope; core_messages.hpp enumerates core P2P message types.
- Networking primitives: stcp_socket.hpp and message_oriented_connection.hpp underpin transport and framing.

```mermaid
graph TB
subgraph "Network Layer"
N["node.hpp<br/>Public API"]
NI["node.cpp<br/>Implementation"]
PC["peer_connection.hpp<br/>Peer Abstraction"]
PD["peer_database.hpp<br/>Persistent Peers"]
MSG["message.hpp<br/>Message Envelope"]
CM["core_messages.hpp<br/>Core Message Types"]
end
subgraph "Transport"
STCP["stcp_socket.hpp"]
MOC["message_oriented_connection.hpp"]
end
N --> NI
NI --> PC
NI --> PD
NI --> MSG
NI --> CM
PC --> STCP
PC --> MOC
```

**Diagram sources**
- [node.hpp](file://libraries/network/include/graphene/network/node.hpp#L180-L355)
- [node.cpp](file://libraries/network/node.cpp#L869-L905)
- [peer_connection.hpp](file://libraries/network/include/graphene/network/peer_connection.hpp#L79-L354)
- [peer_database.hpp](file://libraries/network/include/graphene/network/peer_database.hpp#L104-L134)
- [message.hpp](file://libraries/network/include/graphene/network/message.hpp#L42-L114)
- [core_messages.hpp](file://libraries/network/include/graphene/network/core_messages.hpp)

**Section sources**
- [node.hpp](file://libraries/network/include/graphene/network/node.hpp#L180-L355)
- [node.cpp](file://libraries/network/node.cpp#L869-L905)

## Core Components
- node class: Provides P2P orchestration, configuration, peer management, and broadcast APIs.
- node_delegate interface: Bridges the P2P layer to the blockchain, handling block ingestion, transaction processing, and sync callbacks.
- peer_connection: Encapsulates a single peer link with state machine, inventory tracking, and rate-limited messaging.
- peer_database: Persistent store of potential peers with connection history and disposition.
- message: Generic envelope for all P2P messages with hashing and typed serialization.

Key responsibilities:
- Lifecycle: Construction, configuration loading, listener setup, and graceful shutdown.
- Peer orchestration: Connecting to configured seeds, accepting inbound connections, pruning inactive peers, and enforcing connection limits.
- Synchronization: Requesting and processing blockchain item IDs, fetching blocks/transactions, and notifying the delegate.
- Broadcasting: Advertising inventory and sending items to peers.
- Inventory management: Tracking what peers have, what we need, and what we've recently processed.

**Section sources**
- [node.hpp](file://libraries/network/include/graphene/network/node.hpp#L180-L355)
- [node.cpp](file://libraries/network/node.cpp#L869-L905)
- [peer_connection.hpp](file://libraries/network/include/graphene/network/peer_connection.hpp#L79-L354)
- [peer_database.hpp](file://libraries/network/include/graphene/network/peer_database.hpp#L104-L134)
- [message.hpp](file://libraries/network/include/graphene/network/message.hpp#L42-L114)

## Architecture Overview
The node delegates blockchain integration to a node_delegate and coordinates peers via peer_connection instances. The node maintains separate queues for sync and normal operation, enforces bandwidth and connection limits, and periodically prunes stale peers.

```mermaid
classDiagram
class node {
+load_configuration(dir)
+listen_to_p2p_network()
+connect_to_p2p_network()
+add_node(endpoint)
+connect_to_endpoint(endpoint)
+listen_on_endpoint(ep, wait)
+accept_incoming_connections(flag)
+listen_on_port(port, wait)
+get_actual_listening_endpoint()
+get_connected_peers()
+get_connection_count()
+broadcast(message)
+broadcast_transaction(trx)
+sync_from(item_id, hard_fork_nums)
+is_connected()
+set_advanced_node_parameters(variant)
+get_advanced_node_parameters()
+get_transaction_propagation_data(tx_id)
+get_block_propagation_data(block_id)
+get_node_id()
+set_allowed_peers(ids)
+clear_peer_database()
+set_total_bandwidth_limit(up, down)
+network_get_info()
+network_get_usage_stats()
+get_potential_peers()
+disable_peer_advertising()
+get_call_statistics()
}
class node_delegate {
+has_item(id) bool
+handle_block(blk_msg, sync_mode, contained_txs) bool
+handle_transaction(trx_msg)
+handle_message(msg)
+get_block_ids(synopsis, out_remaining, limit) vector
+get_item(id) message
+get_blockchain_synopsis(ref_point, num_after) vector
+sync_status(item_type, count)
+connection_count_changed(count)
+get_block_number(id) uint32
+get_block_time(id) time_point_sec
+get_blockchain_now() time_point_sec
+get_head_block_id() item_hash_t
+estimate_last_known_fork_from_git_revision_timestamp(ts) uint32
+error_encountered(msg, err)
}
class peer_connection {
+accept_connection()
+connect_to(endpoint, local_ep)
+send_message(msg)
+send_item(item_id)
+close_connection()
+busy() bool
+idle() bool
+is_transaction_fetching_inhibited() bool
+get_remote_endpoint()
+get_total_bytes_sent()
+get_total_bytes_received()
+get_last_message_sent_time()
+get_last_message_received_time()
}
node --> node_delegate : "calls"
node --> peer_connection : "manages"
```

**Diagram sources**
- [node.hpp](file://libraries/network/include/graphene/network/node.hpp#L180-L355)
- [peer_connection.hpp](file://libraries/network/include/graphene/network/peer_connection.hpp#L79-L354)

## Detailed Component Analysis

### Node Lifecycle Management
- Construction and destruction: The node allocates an internal node_impl and initializes defaults for connection targets, timeouts, and rate limiting. On destruction, it attempts to gracefully close connections and updates the peer database.
- Configuration: load_configuration reads node-specific settings (listening endpoint, accept flags) from a JSON file in the configuration directory.
- Listener setup: listen_to_p2p_network and listen_on_endpoint/listen_on_port configure the TCP server to accept inbound connections, with optional retry behavior when the port is busy.
- Startup and shutdown: connect_to_p2p_network initiates outbound connections; close() and destructor ensure cleanup.

Operational loops:
- p2p_network_connect_loop: Periodically connects to candidate peers, respecting retry/backoff and connection caps.
- fetch_sync_items_loop: Requests missing sync items from peers and schedules processing.
- fetch_items_loop: Normal operation fetching of items not yet in local cache.
- advertise_inventory_loop: Broadcasts new inventory to peers.
- terminate_inactive_connections_loop: Detects and disconnects idle/inactive peers.
- bandwidth_monitor_loop: Updates rolling averages of read/write throughput.
- fetch_updated_peer_lists_loop: Requests updated peer lists periodically.

```mermaid
sequenceDiagram
participant App as "Application"
participant Node as "node"
participant Impl as "node_impl"
participant DB as "peer_database"
participant Peer as "peer_connection"
App->>Node : "load_configuration(dir)"
Node->>Impl : "load_configuration(...)"
Impl->>DB : "open/read peers.json"
App->>Node : "listen_to_p2p_network()"
Node->>Impl : "listen_to_p2p_network()"
Impl->>Impl : "start accept loop"
App->>Node : "connect_to_p2p_network()"
Node->>Impl : "connect_to_p2p_network()"
Impl->>Impl : "p2p_network_connect_loop()"
Impl->>Peer : "connect_to(endpoint)"
Peer-->>Impl : "on_connection_accepted"
Impl->>Impl : "move_peer_to_active_list"
Impl->>Peer : "address_request"
Peer-->>Impl : "address_message"
Impl->>DB : "update entries"
Impl->>Impl : "trigger_p2p_network_connect_loop()"
```

**Diagram sources**
- [node.cpp](file://libraries/network/node.cpp#L952-L1047)
- [node.cpp](file://libraries/network/node.cpp#L1623-L1654)
- [node.cpp](file://libraries/network/node.cpp#L2282-L2350)

**Section sources**
- [node.cpp](file://libraries/network/node.cpp#L869-L931)
- [node.cpp](file://libraries/network/node.cpp#L952-L1047)
- [node.cpp](file://libraries/network/node.cpp#L1623-L1654)
- [node.cpp](file://libraries/network/node.cpp#L2282-L2350)

### Peer Connection Establishment
- Outbound: connect_to_endpoint creates a peer_connection and initiates a connect loop; on success, transitions to negotiation and then active.
- Inbound: accept_loop accepts sockets and starts accept_or_connect_task; after hello exchange, moves to active and starts synchronization.
- Handshake validation: Verifies signatures, chain ID, fork compatibility, and prevents self-connections and duplicates.
- Firewall detection: Uses check-firewall messages to infer NAT/firewall status.

```mermaid
sequenceDiagram
participant Impl as "node_impl"
participant Peer as "peer_connection"
participant Delegate as "node_delegate"
Impl->>Peer : "connect_to_task(endpoint)"
Peer-->>Impl : "on_hello_message"
Impl->>Impl : "validate hello (sig, chain, fork)"
alt valid
Impl->>Peer : "send connection_accepted"
Peer-->>Impl : "address_request"
Impl->>Peer : "address_message"
Impl->>Impl : "move_peer_to_active_list"
Impl->>Delegate : "new_peer_just_added -> start_synchronizing"
else invalid
Impl->>Peer : "send connection_rejected"
Impl->>Impl : "disconnect_from_peer"
end
```

**Diagram sources**
- [node.cpp](file://libraries/network/node.cpp#L2029-L2230)
- [node.cpp](file://libraries/network/node.cpp#L2232-L2250)
- [node.cpp](file://libraries/network/node.cpp#L2282-L2350)

**Section sources**
- [node.cpp](file://libraries/network/node.cpp#L2029-L2230)
- [node.cpp](file://libraries/network/node.cpp#L2232-L2250)
- [node.cpp](file://libraries/network/node.cpp#L2282-L2350)

### Network Topology Maintenance
- Peer selection: Maintains a potential peer database with last-seen timestamps, disposition, and attempt counts; applies exponential backoff and retry windows.
- Connection caps: Tracks handshaking, active, closing, and terminating sets; enforces desired/max connection counts.
- Inactivity pruning: Disconnects peers exceeding inactivity thresholds and reschedules outstanding requests to others.
- Peer advertising: Optionally disables advertising to restrict exposure.

```mermaid
flowchart TD
Start(["Connect Loop"]) --> CheckWants{"Wants more connections?"}
CheckWants --> |No| Sleep["Sleep and wait for updates"]
CheckWants --> |Yes| Iterate["Iterate potential peers"]
Iterate --> Eligible{"Eligible to connect?"}
Eligible --> |No| NextPeer["Next peer"]
Eligible --> |Yes| Connect["connect_to_endpoint"]
Connect --> NextPeer
NextPeer --> DoneIter{"Any new connection?"}
DoneIter --> |Yes| CheckWants
DoneIter --> |No| Sleep
```

**Diagram sources**
- [node.cpp](file://libraries/network/node.cpp#L952-L1047)

**Section sources**
- [node.cpp](file://libraries/network/node.cpp#L952-L1047)
- [node.cpp](file://libraries/network/node.cpp#L1400-L1621)

### Blockchain Integration via node_delegate
- Block handling: handle_block receives new blocks during sync or normal operation; returns whether a fork switch occurred; populates contained transaction IDs for propagation.
- Transaction processing: handle_transaction validates and accepts transactions.
- Sync callbacks: get_block_ids, get_blockchain_synopsis, sync_status, and connection_count_changed inform the delegate about sync progress and peer counts.
- Fork awareness: Estimates last known fork from timestamps and rejects incompatible peers.

```mermaid
sequenceDiagram
participant Impl as "node_impl"
participant Peer as "peer_connection"
participant Delegate as "node_delegate"
Peer-->>Impl : "block_message"
Impl->>Delegate : "handle_block(block_msg, sync_mode, contained_txs)"
alt sync_mode
Impl->>Impl : "process_backlog_of_sync_blocks"
else normal
Impl->>Impl : "process_block_during_normal_operation"
end
Delegate-->>Impl : "bool fork_switched"
Impl->>Impl : "broadcast transactions from contained_txs"
```

**Diagram sources**
- [node.hpp](file://libraries/network/include/graphene/network/node.hpp#L79-L80)
- [node.cpp](file://libraries/network/node.cpp#L3117-L3199)

**Section sources**
- [node.hpp](file://libraries/network/include/graphene/network/node.hpp#L79-L80)
- [node.cpp](file://libraries/network/node.cpp#L3117-L3199)

### Configuration Methods
- load_configuration: Reads node_config.json and sets listening endpoint, accept flags, and persistence directory.
- listen_on_endpoint/accept_incoming_connections/listen_on_port: Configure the TCP listener and availability behavior.
- set_advanced_node_parameters/get_advanced_node_parameters: Tuning knobs for advanced behavior.
- set_total_bandwidth_limit: Configures upload/download rate limiting.
- disable_peer_advertising: Restricts outbound peer advertisement.

**Section sources**
- [node.hpp](file://libraries/network/include/graphene/network/node.hpp#L200-L294)
- [node.cpp](file://libraries/network/node.cpp#L933-L950)
- [node.cpp](file://libraries/network/node.cpp#L1686-L1713)

### Peer Management Functions
- add_node/connect_to_endpoint: Adds a seed or forces immediate connection.
- get_connected_peers: Returns status for UI/monitoring.
- get_connection_count/is_connected: Reports current connectivity.
- set_allowed_peers/clear_peer_database: Controls allowed peers and resets peer DB for diagnostics.
- get_potential_peers/disable_peer_advertising: Inspect and control peer discovery.

**Section sources**
- [node.hpp](file://libraries/network/include/graphene/network/node.hpp#L211-L296)
- [node.cpp](file://libraries/network/node.cpp#L1788-L1841)
- [node.cpp](file://libraries/network/node.cpp#L2282-L2350)

### Network Broadcasting and Inventory
- broadcast/broadcast_transaction: Queues outgoing messages and triggers inventory advertisement.
- Inventory tracking: Per-peer inventories (advertised to us/advertised to peer) and node-wide new_inventory set.
- Rate limiting: fc::rate_limiting_group controls bandwidth.
- Message caching: blockchain_tied_message_cache stores recent messages for retrieval.

```mermaid
flowchart TD
NewItem["New Item Arrives"] --> Cache["Cache in message_cache"]
Cache --> Advertise["Advertise Inventory"]
Advertise --> ForEachPeer{"For each active peer"}
ForEachPeer --> CheckInv["Check inventory overlap"]
CheckInv --> |Not ours| Queue["Queue for send"]
CheckInv --> |Overlap| Skip["Skip"]
Queue --> Send["Send item_ids_inventory_message"]
Send --> Deliver["Deliver item via fetch_items_message"]
```

**Diagram sources**
- [node.cpp](file://libraries/network/node.cpp#L1326-L1398)
- [node.cpp](file://libraries/network/node.cpp#L2830-L2892)
- [node.cpp](file://libraries/network/node.cpp#L111-L217)

**Section sources**
- [node.cpp](file://libraries/network/node.cpp#L1326-L1398)
- [node.cpp](file://libraries/network/node.cpp#L2830-L2892)
- [node.cpp](file://libraries/network/node.cpp#L111-L217)

### Examples

- Node initialization and startup:
  - Load configuration from a directory.
  - Enable listening on a specific endpoint/port.
  - Trigger outbound connection to seed peers.
  - Monitor connection count via delegate callback.

- Peer discovery workflow:
  - Add seed endpoints via add_node.
  - Allow connect loop to establish connections.
  - Receive address_message with peer list; update potential peer database.
  - Transition to active and synchronize.

- Network synchronization process:
  - Delegate provides blockchain synopsis.
  - Node requests item IDs, tracks unfetched counts, and fetches blocks.
  - On acceptance, broadcasts contained transactions.

[No sources needed since this subsection provides conceptual examples]

## Dependency Analysis
The node depends on:
- peer_connection for per-peer state and messaging.
- peer_database for persistent peer records.
- message/core_messages for typed envelopes and core message dispatch.
- stcp_socket and message_oriented_connection for transport and framing.
- fc::rate_limiting_group for bandwidth control.

```mermaid
graph LR
Node["node.hpp"] --> Impl["node.cpp"]
Impl --> PeerConn["peer_connection.hpp"]
Impl --> PeerDB["peer_database.hpp"]
Impl --> Msg["message.hpp"]
Impl --> CoreMsg["core_messages.hpp"]
PeerConn --> STCP["stcp_socket.hpp"]
PeerConn --> MOC["message_oriented_connection.hpp"]
Impl --> Rate["fc::rate_limiting_group"]
```

**Diagram sources**
- [node.hpp](file://libraries/network/include/graphene/network/node.hpp#L180-L355)
- [node.cpp](file://libraries/network/node.cpp#L869-L905)
- [peer_connection.hpp](file://libraries/network/include/graphene/network/peer_connection.hpp#L79-L354)
- [peer_database.hpp](file://libraries/network/include/graphene/network/peer_database.hpp#L104-L134)
- [message.hpp](file://libraries/network/include/graphene/network/message.hpp#L42-L114)
- [core_messages.hpp](file://libraries/network/include/graphene/network/core_messages.hpp)

**Section sources**
- [node.hpp](file://libraries/network/include/graphene/network/node.hpp#L180-L355)
- [node.cpp](file://libraries/network/node.cpp#L869-L905)

## Performance Considerations
- Connection limits: desired/max connections cap concurrent peers; enforced in is_wanting_new_connections and is_accepting_new_connections.
- Bandwidth throttling: rate limiter updates rolling averages and constrains upload/download rates.
- Prefetching: Limits for sync and normal operations prevent resource exhaustion.
- Inactivity pruning: Keeps the mesh healthy by dropping idle peers and rescheduling requests.
- Inventory deduplication: Prevents redundant fetches and unbounded growth of fetch queues.

[No sources needed since this section provides general guidance]

## Troubleshooting Guide
Common issues and resolutions:
- Port binding conflicts: Use listen_on_port with wait_if_not_available=true to retry; otherwise, allow dynamic port selection.
- Rejection reasons: Review connection_rejected_message reason codes (e.g., connected_to_self, already_connected, not_accepting_connections, different_chain, outdated client).
- Firewall/NAT: Use check-firewall messages to detect; adjust inbound/outbound ports and consider advertised inbound addresses.
- Peer database corruption: Clear peer database via clear_peer_database to reset discovery state.
- Bandwidth saturation: Adjust set_total_bandwidth_limit and review advertised inventory sizes.
- Hard fork incompatibility: Upgrade client if rejected due to inability to process future blocks.

**Section sources**
- [node.cpp](file://libraries/network/node.cpp#L2251-L2280)
- [node.cpp](file://libraries/network/node.cpp#L2137-L2168)
- [node.cpp](file://libraries/network/node.cpp#L1686-L1713)
- [node.cpp](file://libraries/network/node.cpp#L1326-L1398)

## Conclusion
The Node Management component provides a robust, configurable, and efficient P2P orchestration layer. It integrates tightly with the blockchain via node_delegate, maintains a resilient peer topology, and ensures reliable synchronization and broadcasting. Proper configuration of limits, bandwidth, and peer discovery, combined with monitoring and troubleshooting practices, yields a stable and performant network node.