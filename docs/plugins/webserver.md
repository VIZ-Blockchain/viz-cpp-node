# Webserver Plugin

The webserver plugin exposes HTTP and WebSocket endpoints that forward JSON-RPC requests to the `json_rpc` plugin. It includes a response cache keyed on `method + params` (not `id`) that is invalidated on each applied block, and a thread pool for concurrent request handling.

**Source:** [plugins/webserver/webserver_plugin.cpp](../../plugins/webserver/webserver_plugin.cpp)

---

## Dependencies

```
json_rpc::plugin
```

---

## Configuration

| Option | Default | Description |
|--------|---------|-------------|
| `webserver-http-endpoint` | — | HTTP listen address, e.g. `0.0.0.0:8090` |
| `webserver-ws-endpoint` | — | WebSocket listen address, e.g. `0.0.0.0:8091` |
| `webserver-thread-pool-size` | `256` | Worker threads for handling HTTP and WebSocket requests. |
| `webserver-cache-enabled` | `true` | Enable the response cache. |
| `webserver-cache-size` | `10000` | Maximum number of cached responses. Cache is evicted entirely when this limit is reached. |

At least one of `webserver-http-endpoint` or `webserver-ws-endpoint` must be set for the plugin to do anything. Both can be enabled simultaneously.

---

## Minimal config.ini

```ini
plugin = webserver

webserver-http-endpoint = 0.0.0.0:8090
webserver-ws-endpoint   = 0.0.0.0:8091
```

---

## Response Cache

### What is cached

Every read-only JSON-RPC response is eligible for caching. The cache key is a SHA-256 hash of `method + params` — deliberately **excluding `id`** so that rotating the request `id` cannot bypass the cache.

### What is never cached

| Namespace | Reason |
|-----------|--------|
| `network_broadcast_api.*` | State-changing (transaction/block broadcast) |
| `debug_node.*` | State-changing debug operations |
| Malformed / unparseable requests | No reliable key can be derived |

Batch requests (JSON array) are treated as a single atomic cache entry keyed on the full array hash.

### Invalidation

The cache is cleared on every applied block. Responses are never served stale beyond one block interval (3 seconds).

### Disabling the cache

```ini
webserver-cache-enabled = false
```

Disable for nodes that serve highly latency-sensitive or real-time clients where a 3-second cache window is unacceptable.

---

## Thread Pool

HTTP and WebSocket servers each run on a dedicated `io_service` instance. Incoming requests are dispatched to a shared thread pool of `webserver-thread-pool-size` workers.

**Sizing guidance:**
- Public API node with mixed read/write traffic: `256` (default) is sufficient for most workloads.
- High-throughput nodes: increase to `512` or more. Monitor CPU saturation — more threads than CPU cores do not help for CPU-bound operations.
- Development / local node: `4`–`8` is adequate.

---

## WebSocket Subscriptions

WebSocket clients can register callbacks:

| Method | Description |
|--------|-------------|
| `database_api.set_block_applied_callback` | Called on every applied block with the block header |
| `database_api.set_pending_transaction_callback` | Called when a transaction enters the pending pool |
| `database_api.cancel_all_subscriptions` | Unsubscribe from all callbacks |

Subscriptions require a persistent WebSocket connection. They are not available over plain HTTP.

---

## Security

- **Bind to localhost** (`127.0.0.1`) and use a reverse proxy (nginx/Caddy) for public exposure. Binding to `0.0.0.0` exposes the RPC directly to the network.
- The plugin has no built-in authentication or rate limiting. Apply those at the reverse proxy layer.
- Mutating methods (`network_broadcast_api`, `debug_node`) are blocked from cache poisoning by design, but they remain callable from any connected client — restrict access at the network level if needed.

---

## Troubleshooting

| Symptom | Check |
|---------|-------|
| Port already in use on startup | Another process is bound to the configured port; change the port or kill the conflicting process |
| High memory usage | Reduce `webserver-cache-size` or disable caching |
| Slow responses under load | Increase `webserver-thread-pool-size`; check CPU saturation |
| WebSocket subscriptions not firing | Subscriptions require a WebSocket connection, not HTTP |
| Stale responses | If `webserver-cache-enabled = true`, responses are fresh within one block interval (~3 s); for real-time use disable the cache |

---

See also: [Plugin Overview](./overview.md), [Database API](./database-api.md), [JSON-RPC API](../api/json-rpc.md).
