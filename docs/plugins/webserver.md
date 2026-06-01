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

## CORS

The webserver plugin handles browser cross-origin requests natively — no reverse proxy is required for local or development setups.

**Preflight requests** (`OPTIONS`) are answered immediately with:

| Response header | Value |
|----------------|-------|
| `Access-Control-Allow-Origin` | `*` |
| `Access-Control-Allow-Methods` | `POST, GET, OPTIONS` |
| `Access-Control-Allow-Headers` | `Content-Type, Authorization` |
| `Access-Control-Max-Age` | `86400` |

**All other HTTP responses** include `Access-Control-Allow-Origin: *`.

This allows browser-based wallets and dApps to call the JSON-RPC endpoint directly. For production deployments behind nginx, CORS is handled at the proxy layer (see [Exposing the API via HTTPS](#exposing-the-api-via-https-nginx--certbot)) — both layers setting the header is harmless.

---

## Security

- **Bind to localhost** (`127.0.0.1`) and use a reverse proxy (nginx/Caddy) for public exposure. Binding to `0.0.0.0` exposes the RPC directly to the network.
- The plugin has no built-in authentication or rate limiting. Apply those at the reverse proxy layer.
- Mutating methods (`network_broadcast_api`) are blocked from cache poisoning by design, but they remain callable from any connected client — restrict access at the network level if needed.

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

## Exposing the API via HTTPS (nginx + certbot)

Bind the node to localhost, then front it with nginx. certbot patches the config automatically when you run `certbot --nginx`.

### 1. Node config.ini

```ini
webserver-http-endpoint = 127.0.0.1:8090
webserver-ws-endpoint   = 127.0.0.1:8091
```

### 2. /etc/nginx/sites-enabled/viz-node

```nginx
server {
    listen 80;
    server_name your.domain.com;  # ← replace with your domain

    # ACME challenge for certbot
    location /.well-known/acme-challenge/ {
        root /var/www/certbot;
    }

    location / {
        proxy_pass http://127.0.0.1:8090;
        proxy_http_version 1.1;

        proxy_set_header Host              $host;
        proxy_set_header X-Real-IP         $remote_addr;
        proxy_set_header X-Forwarded-For   $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;

        proxy_connect_timeout 60s;
        proxy_send_timeout    60s;
        proxy_read_timeout    60s;
    }
}
```

### 3. Obtain the TLS certificate

```bash
sudo nginx -t && sudo systemctl reload nginx
sudo certbot --nginx -d your.domain.com
```

certbot adds the `listen 443 ssl` block, `ssl_certificate` directives, and an HTTP→HTTPS redirect automatically. After this your node is reachable at `https://your.domain.com`.

### WebSocket via HTTPS

WebSocket clients need the `Upgrade` header forwarded. Add a separate location (or a second server block on port 8091):

```nginx
location /ws {
    proxy_pass http://127.0.0.1:8091;
    proxy_http_version 1.1;
    proxy_set_header Upgrade    $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header Host       $host;
    proxy_read_timeout 3600s;
}
```

---

See also: [Plugin Overview](./overview.md), [Database API](./database-api.md), [JSON-RPC API](../api/json-rpc.md).
