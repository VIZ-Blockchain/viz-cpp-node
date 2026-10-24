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
| `webserver-stats-enabled` | `false` | Enable per-IP request accounting, the stats log, and (unless overridden) the rate limits. |
| `webserver-limits-enabled` | value of `webserver-stats-enabled` | Enforce the per-IP rate limits. Limits need the counters from `webserver-stats-enabled`. |
| `webserver-limit-m` | `1000` | Maximum requests per IP per minute. |
| `webserver-limit-h` | `10000` | Maximum requests per IP per hour. |
| `webserver-limit-d` | `100000` | Maximum requests per IP per day. |
| `webserver-stats-log` | `<data-dir>/webserver-stats.log` | Stats log path; rotated by size. |
| `webserver-stats-log-max-size` | `10485760` (10 MiB) | Rotate the stats log when it exceeds this size. |
| `webserver-stats-log-keep` | `5` | Number of rotated files (`.1` … `.N`) to keep. |
| `webserver-stats-interval` | `60` | Seconds between snapshot lines written to the stats log. |
| `webserver-admin-state` | `<data-dir>/webserver-admin.json` | Live state (limits, IP lists, stats switch) persisted by `node_admin`. |

At least one of `webserver-http-endpoint` or `webserver-ws-endpoint` must be set for the plugin to do anything. Both can be enabled simultaneously.

The stats and limit options are the **startup seed** only. Once the node is running, the live values are owned by the plugin and can be changed through `node_admin` (see [Managing stats and limits at runtime](#managing-stats-and-limits-at-runtime)); every change is applied to the next request and persisted to `webserver-admin-state`.

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

## Request Accounting and Rate Limiting

Off by default. Set `webserver-stats-enabled = true` to count requests per client IP and enable the limits (or set `webserver-limits-enabled` explicitly to decouple the two).

### Limits

| Window | Default |
|--------|---------|
| minute | 1000 requests |
| hour | 10000 requests |
| day | 100000 requests |

The windows are fixed (the minute bucket resets at the minute boundary, the hour at the hour, the day at midnight UTC), and every request is checked against all three. A request over any limit is rejected:

- HTTP: `429 Too Many Requests`, `Retry-After: 60`, body `rate limit exceeded`.
- WebSocket: JSON-RPC error `-32006 rate limit exceeded`.

This is deliberate protection against scraping and parser traffic: consumers that need more throughput should run their own node.

### Lists

- **Whitelist** — addresses that bypass the limits. They are still counted and still appear in the stats.
- **Blacklist** — addresses that are rejected with `403 Forbidden` (HTTP) or `-32005 forbidden` (WebSocket). The blacklist wins over the whitelist. It is enforced even when stats are disabled.
- Adding an address to one list removes it from the other.

### What the stats log contains

Every `webserver-stats-interval` seconds the plugin appends one line per tracked IP plus a summary:

```text
2026-10-19T12:00:00Z ip=203.0.113.7 total=4210 minute=320 hour=4210 day=4210 blocked=0 limited=3 whitelisted=0 blacklisted=0 last_seen=1760875200
2026-10-19T12:00:00Z summary total_requests=4210 blocked_requests=0 limited_requests=3 tracked_ips=1
```

The file is rotated by size: `<name>` becomes `<name>.1`, older files shift to `.2` … `.webserver-stats-log-keep`, and the oldest is deleted.

### Client address

The plugin uses the TCP peer address of the connection. Put behind nginx, every request appears to come from the proxy (`127.0.0.1`), so per-IP limits become global limits and the per-IP report shows a single row. Either expose the API directly, whitelist the proxy and rate-limit at the proxy layer (`limit_req`), or enable PROXY protocol support at the proxy/network layer.

### Managing stats and limits at runtime

The `webserver` scope on a `node_admin` token controls the mutating methods; reads only need `read`:

| Method | Purpose |
|--------|---------|
| `admin.webserver.status` | Limits, totals, IP lists, tracked IP count |
| `admin.webserver.ips` | Per-IP counters (`limit` up to 5000), sorted by request count |
| `admin.webserver.limits` | Change `enabled`, `per_minute`, `per_hour`, `per_day` (omitted fields keep their value) |
| `admin.webserver.stats` | Turn stats logging on/off; enabled limits retain counters (`{"enabled": true|false}`) |
| `admin.webserver.whitelist_add` / `whitelist_remove` | Edit the whitelist (`{"ip": "..."}`) |
| `admin.webserver.blacklist_add` / `blacklist_remove` | Edit the blacklist (`{"ip": "..."}`) |
| `admin.webserver.reset` | Zero the counters, keep the lists and limits |
| `admin.webserver.reload` | Re-read `webserver-admin-state` from disk |

Each of these is also exposed as an MCP tool of the same name. **Changes are applied to the live plugin immediately — no restart and no re-reading of `config.ini`.** They are written to `webserver-admin-state` (atomic temp-file + rename), which is loaded at the next startup only when its `config_policy` fingerprint matches the startup stats/limits settings. Changed config.ini policy wins; IP lists are restored independently. Legacy state files restore IP lists only. `config.ini` is never rewritten. `reload` exists for operators who edited the JSON file by hand; normal changes do not need it.

```json
{"jsonrpc":"2.0","id":1,"method":"admin.webserver.limits",
 "params":{"per_minute":600,"per_hour":8000}}
```

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

This allows browser-based wallets and dApps to call the JSON-RPC endpoint directly. **The node grants CORS itself** — it emits these headers on every response, even when running behind a reverse proxy. The nginx config in [Exposing the API via HTTPS](#exposing-the-api-via-https-nginx--certbot) therefore only proxies requests and sets **no** CORS header of its own.

> **Do not** add `add_header Access-Control-Allow-Origin` at the nginx layer while the node also sets it: two identical headers make the browser reject the response. If you additionally serve static files (an API explorer or SPA) from the same domain, add the CORS header only on those static locations — and only for `GET`/`HEAD` — so proxied responses keep the single header set by the node.

---

## Security

- **Bind to localhost** (`127.0.0.1`) and use a reverse proxy (nginx/Caddy) for public exposure. Binding to `0.0.0.0` exposes the RPC directly to the network.
- The plugin has no built-in authentication. It has optional per-IP rate limiting and IP lists (see [Request Accounting and Rate Limiting](#request-accounting-and-rate-limiting)), but those are blunt controls — keep authentication and coarse filtering at the reverse proxy layer.
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

### Trusted reverse proxies and persistence

By default limits use the TCP peer address and ignore forwarding headers. Behind nginx, explicitly configure `webserver-trusted-proxy = 127.0.0.1` (repeat the option for each actual proxy IP) and forward `X-Forwarded-For`. The node walks the chain from right to left and stops at the first untrusted hop. Never trust arbitrary client addresses as proxies.

The 200,000-IP capacity guard expires entries older than one day and rejects new untracked sources while full; it never clears live clients' rate windows. Enabled limits continue counting when stats logging is disabled. Periodic stats write changed rows only, with formatting and disk I/O outside the admission mutex. Persisted policy is bound to a `config_policy` startup fingerprint; changed config.ini stats/limits settings supersede old runtime policy while preserving IP lists.
