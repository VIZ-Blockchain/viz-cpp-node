# Webserver Plugin

The webserver plugin provides HTTP and WebSocket endpoints for JSON-RPC API access to the VIZ blockchain node. It includes a response cache to improve performance for frequently-called read-only API methods.

## Overview

The webserver plugin starts an embedded HTTP/WebSocket server that accepts JSON-RPC 2.0 requests and dispatches them to registered API methods. It handles both:

- **HTTP requests** - Standard POST requests with JSON-RPC body
- **WebSocket connections** - Persistent connections for real-time API access

## Basic Usage

Enable the webserver plugin in `config.ini`:

```ini
plugin = webserver

# HTTP endpoint (required for HTTP API access)
webserver-http-endpoint = 127.0.0.1:8090

# WebSocket endpoint (required for WebSocket API access)
webserver-ws-endpoint = 127.0.0.1:8091

# Or use a single endpoint for both (deprecated)
# rpc-endpoint = 127.0.0.1:8090
```

## Response Cache

The webserver plugin includes a response cache that stores JSON-RPC responses to reduce database load for frequently-called methods like `database_api.get_dynamic_global_properties`.

### How It Works

- Cache key is a SHA256 hash of the JSON-RPC request body
- Responses are cached per-block - the cache is cleared when a new block is applied
- Cache hits are served immediately without acquiring database read locks
- Thread-safe implementation with mutex protection

### Cache Configuration

```ini
# Enable/disable the response cache (default: true)
webserver-cache-enabled = true

# Maximum number of cached responses (default: 10000)
webserver-cache-size = 10000
```

### When to Disable Cache

Consider disabling the cache (`webserver-cache-enabled = false`) if:

- Your node serves mostly unique requests
- Memory usage is a concern
- You're running a private node with low API traffic

### Cache Benefits

The cache is most beneficial for:

- Public API nodes serving many concurrent users
- Methods like `get_dynamic_global_properties`, `get_config`, `get_chain_properties`
- Scenarios where users poll the same data frequently (e.g., every 0.5 seconds)

## Thread Pool

The webserver uses a thread pool to handle concurrent requests:

```ini
# Number of threads for handling queries (default: 256)
webserver-thread-pool-size = 256
```

Each incoming request is dispatched to the thread pool, allowing concurrent processing of multiple API calls.

## Security Considerations

### Binding to localhost

For security, bind to localhost (127.0.0.1) unless you need external access:

```ini
# Local access only (recommended)
webserver-http-endpoint = 127.0.0.1:8090

# External access (use with caution)
webserver-http-endpoint = 0.0.0.0:8090
```

### API Access Control

To control which APIs are publicly accessible, use the `public-api` option:

```ini
# Make specific APIs public
public-api = database_api
public-api = network_broadcast_api
```

### Authentication

For authenticated API access, use the `api-user` option:

```ini
api-user = username:password:database_api
```

## Config Reference

### Config file options (`config.ini`)

| Option | Default | Description |
|--------|---------|-------------|
| `webserver-http-endpoint` | (none) | HTTP listen endpoint (IP:port) |
| `webserver-ws-endpoint` | (none) | WebSocket listen endpoint (IP:port) |
| `rpc-endpoint` | (none) | Combined HTTP/WS endpoint (deprecated) |
| `webserver-thread-pool-size` | 256 | Number of handler threads |
| `webserver-cache-enabled` | true | Enable response caching |
| `webserver-cache-size` | 10000 | Maximum cached responses |

### Deprecated Options

The `rpc-endpoint` option is deprecated in favor of separate `webserver-http-endpoint` and `webserver-ws-endpoint` options.

## Architecture

The webserver plugin architecture:

1. **Main thread** - Handles incoming connections
2. **Thread pool** - Processes JSON-RPC requests concurrently
3. **Cache layer** - Intercepts cacheable requests before API dispatch
4. **Signal connection** - Clears cache on each new block via `applied_block` signal

```
Client Request
      |
      v
[HTTP/WebSocket Server]
      |
      v
[Cache Check] ---> Cache Hit? ---> Return cached response
      |                               |
      | No                            |
      v                               |
[Thread Pool]                       |
      |                               |
      v                               |
[JSON-RPC Plugin]                   |
      |                               |
      v                               |
[API Method]                        |
      |                               |
      +-------------------------------+
      |
      v
[Cache Response]
      |
      v
Return to client
```

## See Also

- [Plugin Overview](plugin.md) - General plugin documentation
- [API Notes](api_notes.md) - API usage notes
