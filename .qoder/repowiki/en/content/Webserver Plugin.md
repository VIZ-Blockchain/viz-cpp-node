# Webserver Plugin

<cite>
**Referenced Files in This Document**
- [webserver_plugin.hpp](file://plugins/webserver/include/graphene/plugins/webserver/webserver_plugin.hpp)
- [webserver_plugin.cpp](file://plugins/webserver/webserver_plugin.cpp)
- [plugin.hpp](file://plugins/json_rpc/include/graphene/plugins/json_rpc/plugin.hpp)
- [plugin.cpp](file://plugins/json_rpc/plugin.cpp)
- [utility.hpp](file://plugins/json_rpc/include/graphene/plugins/json_rpc/utility.hpp)
- [webserver-plugin.md](file://documentation/webserver-plugin.md)
- [config.ini](file://share/vizd/config/config.ini)
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
The Webserver Plugin provides HTTP and WebSocket endpoints for JSON-RPC API access to the VIZ blockchain node. It serves as a bridge between external clients and the internal JSON-RPC system, offering both persistent WebSocket connections for real-time updates and standard HTTP endpoints for traditional API calls. The plugin includes intelligent caching mechanisms to optimize performance for frequently accessed read-only API methods.

## Project Structure
The webserver plugin is organized within the plugins/webserver directory structure, following the standard VIZ plugin architecture pattern:

```mermaid
graph TB
subgraph "Webserver Plugin Structure"
A[webserver_plugin.hpp] --> B[Public Header]
C[webserver_plugin.cpp] --> D[Implementation]
subgraph "Dependencies"
E[json_rpc/plugin.hpp] --> F[JSON-RPC Plugin]
G[appbase/application.hpp] --> H[Application Framework]
I[websocketpp/server.hpp] --> J[WebSocket Library]
end
D --> E
D --> G
D --> I
end
```

**Diagram sources**
- [webserver_plugin.hpp:1-62](file://plugins/webserver/include/graphene/plugins/webserver/webserver_plugin.hpp#L1-L62)
- [webserver_plugin.cpp:1-449](file://plugins/webserver/webserver_plugin.cpp#L1-L449)

**Section sources**
- [webserver_plugin.hpp:1-62](file://plugins/webserver/include/graphene/plugins/webserver/webserver_plugin.hpp#L1-L62)
- [webserver_plugin.cpp:1-449](file://plugins/webserver/webserver_plugin.cpp#L1-L449)

## Core Components
The webserver plugin consists of several key components working together to provide HTTP and WebSocket API services:

### Main Plugin Class
The primary interface is the `webserver_plugin` class that inherits from appbase's plugin system, providing lifecycle management and configuration options.

### Implementation Container
The `webserver_plugin_impl` struct contains all the internal state and functionality, including:
- HTTP and WebSocket server instances
- Thread pool management for concurrent request processing
- Response caching mechanism
- Connection handling for both protocols

### JSON-RPC Integration
The plugin integrates with the JSON-RPC plugin to handle API method dispatching and response generation.

**Section sources**
- [webserver_plugin.hpp:32-57](file://plugins/webserver/include/graphene/plugins/webserver/webserver_plugin.hpp#L32-L57)
- [webserver_plugin.cpp:90-134](file://plugins/webserver/webserver_plugin.cpp#L90-L134)

## Architecture Overview
The webserver plugin follows a multi-threaded architecture designed for high concurrency and reliability:

```mermaid
graph TB
subgraph "Client Layer"
A[HTTP Clients]
B[WebSocket Clients]
end
subgraph "Webserver Plugin"
C[HTTP Server Thread]
D[WebSocket Server Thread]
E[Thread Pool]
F[Response Cache]
end
subgraph "JSON-RPC Layer"
G[JSON-RPC Plugin]
H[API Registry]
end
subgraph "Blockchain Layer"
I[Chain Plugin]
J[Database]
end
A --> C
B --> D
C --> E
D --> E
E --> G
G --> H
H --> I
I --> J
F -.-> C
F -.-> D
F -.-> E
subgraph "Cache Management"
K[Block Number Tracking]
L[Cache Eviction]
end
I --> K
K --> L
```

**Diagram sources**
- [webserver_plugin.cpp:136-189](file://plugins/webserver/webserver_plugin.cpp#L136-L189)
- [webserver_plugin.cpp:432-439](file://plugins/webserver/webserver_plugin.cpp#L432-L439)

The architecture implements several key design patterns:
- **Separation of Concerns**: HTTP and WebSocket servers run in separate threads
- **Thread Pool Pattern**: Concurrent request processing with configurable thread count
- **Caching Pattern**: Response caching with block-based invalidation
- **Observer Pattern**: Chain event subscription for cache management

## Detailed Component Analysis

### HTTP/WebSocket Server Implementation
The plugin implements both HTTP and WebSocket servers using the websocketpp library with custom Asio configuration:

```mermaid
sequenceDiagram
participant Client as "Client"
participant Server as "Webserver Plugin"
participant ThreadPool as "Thread Pool"
participant JSONRPC as "JSON-RPC Plugin"
participant Cache as "Response Cache"
Client->>Server : HTTP/WebSocket Request
Server->>ThreadPool : Post request handler
ThreadPool->>Cache : Check cache availability
Cache-->>ThreadPool : Cache hit/miss
alt Cache miss
ThreadPool->>JSONRPC : Call API method
JSONRPC-->>ThreadPool : API response
ThreadPool->>Cache : Store response
end
ThreadPool-->>Client : Send response
```

**Diagram sources**
- [webserver_plugin.cpp:252-339](file://plugins/webserver/webserver_plugin.cpp#L252-L339)
- [webserver_plugin.cpp:107-110](file://plugins/webserver/webserver_plugin.cpp#L107-L110)

### Response Caching Mechanism
The caching system provides significant performance improvements for frequently accessed API methods:

```mermaid
flowchart TD
Start([Request Received]) --> Hash["Generate SHA256 Hash"]
Hash --> CacheCheck{"Cache Enabled?"}
CacheCheck --> |No| ProcessRequest["Process Request"]
CacheCheck --> |Yes| LookupCache["Lookup Cache Entry"]
LookupCache --> CacheHit{"Cache Hit?"}
CacheHit --> |Yes| BlockCheck{"Block Valid?"}
CacheHit --> |No| ProcessRequest
BlockCheck --> |Yes| ReturnCached["Return Cached Response"]
BlockCheck --> |No| ProcessRequest
ProcessRequest --> StoreCache["Store in Cache"]
StoreCache --> ReturnResponse["Return Response"]
ReturnCached --> End([Complete])
ReturnResponse --> End
```

**Diagram sources**
- [webserver_plugin.cpp:216-250](file://plugins/webserver/webserver_plugin.cpp#L216-L250)

### Thread Pool Management
The plugin uses a dedicated thread pool for request processing, separate from the main application thread:

```mermaid
classDiagram
class webserver_plugin_impl {
+thread_pool_size_t thread_pool_size
+asio : : io_service thread_pool_ios
+asio : : io_service : : work thread_pool_work
+vector<boost : : thread> worker_threads
+start_webserver()
+stop_webserver()
+handle_ws_message()
+handle_http_message()
}
class ThreadGroup {
+create_thread(function)
+join_all()
+ioservice scheduler
}
webserver_plugin_impl --> ThreadGroup : "uses"
```

**Diagram sources**
- [webserver_plugin.cpp:92-97](file://plugins/webserver/webserver_plugin.cpp#L92-L97)
- [webserver_plugin.cpp:121-122](file://plugins/webserver/webserver_plugin.cpp#L121-L122)

**Section sources**
- [webserver_plugin.cpp:136-189](file://plugins/webserver/webserver_plugin.cpp#L136-L189)
- [webserver_plugin.cpp:216-250](file://plugins/webserver/webserver_plugin.cpp#L216-L250)
- [webserver_plugin.cpp:92-97](file://plugins/webserver/webserver_plugin.cpp#L92-L97)

### Configuration and Options
The plugin supports extensive configuration through command-line options and configuration files:

| Option | Default | Description |
|--------|---------|-------------|
| `webserver-http-endpoint` | (none) | HTTP listen endpoint (IP:port) |
| `webserver-ws-endpoint` | (none) | WebSocket listen endpoint (IP:port) |
| `rpc-endpoint` | (none) | Combined HTTP/WS endpoint (deprecated) |
| `webserver-thread-pool-size` | 256 | Number of handler threads |
| `webserver-cache-enabled` | true | Enable response caching |
| `webserver-cache-size` | 10000 | Maximum cached responses |

**Section sources**
- [webserver_plugin.cpp:347-361](file://plugins/webserver/webserver_plugin.cpp#L347-L361)
- [webserver-plugin.md:111-124](file://documentation/webserver-plugin.md#L111-L124)

## Dependency Analysis
The webserver plugin has well-defined dependencies that enable its functionality:

```mermaid
graph LR
subgraph "External Dependencies"
A[Boost.Asio]
B[websocketpp]
C[FC Library]
D[AppBase Framework]
end
subgraph "Internal Dependencies"
E[JSON-RPC Plugin]
F[Chain Plugin]
G[Application Core]
end
subgraph "Webserver Plugin"
H[webserver_plugin]
end
H --> E
H --> F
H --> G
H --> A
H --> B
H --> C
H --> D
```

**Diagram sources**
- [webserver_plugin.hpp:3-8](file://plugins/webserver/include/graphene/plugins/webserver/webserver_plugin.hpp#L3-L8)
- [webserver_plugin.cpp:12-31](file://plugins/webserver/webserver_plugin.cpp#L12-L31)

### JSON-RPC Integration Details
The plugin integrates with the JSON-RPC system through method registration and call delegation:

```mermaid
sequenceDiagram
participant WS as "Webserver Plugin"
participant JR as "JSON-RPC Plugin"
participant AR as "API Registry"
participant AP as "API Method"
WS->>JR : call(json_body, callback)
JR->>AR : Find API method
AR->>AP : Execute method(args)
AP-->>AR : Return result
AR-->>JR : Return variant result
JR-->>WS : Stringified response
WS-->>Client : Send response
```

**Diagram sources**
- [plugin.cpp:180-200](file://plugins/json_rpc/plugin.cpp#L180-L200)
- [webserver_plugin.cpp:276-284](file://plugins/webserver/webserver_plugin.cpp#L276-L284)

**Section sources**
- [webserver_plugin.hpp:38](file://plugins/webserver/include/graphene/plugins/webserver/webserver_plugin.hpp#L38)
- [webserver_plugin.cpp:124](file://plugins/webserver/webserver_plugin.cpp#L124)
- [plugin.cpp:159-178](file://plugins/json_rpc/plugin.cpp#L159-L178)

## Performance Considerations
The webserver plugin implements several performance optimization strategies:

### Caching Strategy
- **SHA256 Hash Keys**: Unique request identification for cache entries
- **Block-Based Invalidation**: Cache cleared on each new block to prevent stale data
- **Thread-Safe Operations**: Mutex protection for concurrent access
- **Eviction Policy**: Automatic cache clearing when maximum size is reached

### Concurrency Model
- **Separate IO Services**: HTTP and WebSocket servers use dedicated io_service instances
- **Configurable Thread Pool**: Adjustable worker thread count based on workload
- **Non-blocking Operations**: Async processing prevents thread starvation

### Memory Management
- **Smart Pointers**: Proper resource management for server instances
- **RAII Patterns**: Automatic cleanup on plugin shutdown
- **Connection Pooling**: Efficient WebSocket connection handling

**Section sources**
- [webserver-plugin.md:29-64](file://documentation/webserver-plugin.md#L29-L64)
- [webserver_plugin.cpp:232-245](file://plugins/webserver/webserver_plugin.cpp#L232-L245)

## Troubleshooting Guide

### Common Issues and Solutions

#### Server Binding Failures
**Problem**: Unable to bind to specified endpoints
**Solution**: Verify port availability and network permissions
- Check if ports are already in use
- Ensure proper network interface binding
- Verify firewall configuration

#### High Memory Usage
**Problem**: Excessive memory consumption from caching
**Solution**: Adjust cache configuration
- Reduce `webserver-cache-size` value
- Disable caching for low-traffic scenarios
- Monitor cache hit ratios

#### Performance Degradation
**Problem**: Slow response times under load
**Solution**: Optimize thread pool configuration
- Increase `webserver-thread-pool-size`
- Monitor thread utilization
- Consider hardware resources

### Error Handling Patterns
The plugin implements comprehensive error handling:

```mermaid
flowchart TD
Request[Incoming Request] --> Parse[Parse JSON-RPC]
Parse --> Valid{Valid JSON-RPC?}
Valid --> |No| ParseError[Return Parse Error]
Valid --> |Yes| CacheCheck[Check Cache]
CacheCheck --> CacheHit{Cache Hit?}
CacheHit --> |Yes| SendCached[Send Cached Response]
CacheHit --> |No| Process[Process Request]
Process --> Success{Success?}
Success --> |Yes| CacheStore[Store in Cache]
Success --> |No| SendError[Return Error Response]
CacheStore --> SendResponse[Send Response]
SendCached --> Complete[Complete]
SendResponse --> Complete
SendError --> Complete
ParseError --> Complete
```

**Diagram sources**
- [webserver_plugin.cpp:258-291](file://plugins/webserver/webserver_plugin.cpp#L258-L291)
- [webserver_plugin.cpp:294-339](file://plugins/webserver/webserver_plugin.cpp#L294-L339)

**Section sources**
- [webserver_plugin.cpp:258-291](file://plugins/webserver/webserver_plugin.cpp#L258-L291)
- [webserver_plugin.cpp:294-339](file://plugins/webserver/webserver_plugin.cpp#L294-L339)

## Conclusion
The Webserver Plugin provides a robust, high-performance solution for exposing VIZ blockchain functionality through HTTP and WebSocket interfaces. Its architecture emphasizes scalability through concurrent processing, reliability through comprehensive error handling, and efficiency through intelligent caching mechanisms. The plugin's modular design and extensive configuration options make it suitable for various deployment scenarios, from development environments to production public API services.

Key strengths of the implementation include:
- **High Concurrency**: Thread pool architecture supporting thousands of concurrent requests
- **Intelligent Caching**: Block-aware cache invalidation preventing stale data
- **Flexible Deployment**: Separate HTTP and WebSocket endpoints with independent configuration
- **Production Ready**: Comprehensive error handling and graceful degradation
- **Extensible Design**: Clean separation of concerns enabling easy maintenance and enhancement

The plugin serves as an excellent foundation for building applications that require programmatic access to VIZ blockchain data and operations, with performance characteristics suitable for both private deployments and public API services.