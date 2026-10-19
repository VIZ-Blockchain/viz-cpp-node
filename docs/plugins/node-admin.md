# Node Admin API (`node_admin`)

Administrative JSON-RPC endpoint for the node process itself: health, configuration,
plugin state, logs, the audit trail and P2P peer control. It is meant for operator tooling
and AI agents holding a bearer token — not for end users, and not as a replacement for the
public API. The same listener also serves an **MCP** (Model Context Protocol) endpoint over
HTTP and a small built-in web UI at `/ui/`.

> **Mutating surface is limited to P2P peers.** `admin.p2p.add_node`, `admin.p2p.add_trusted`
> and `admin.p2p.clear_banned` change whom the node dials; everything else is read-only.
> They require a token with the `p2p` scope and are serialized onto the P2P thread. The
> remaining mutating operations (config, plugins, process, chain) are a later stage and will
> follow the same authentication/audit model with two-phase `plan`/`apply`.

---

## Why a separate listener

The public webserver API is normally published through nginx. Inside the node every such
request arrives **from 127.0.0.1**, so checking the source address on the public endpoint
protects nothing. `node_admin` therefore opens its own HTTP listener, bound to loopback by
default, that is never proxied.

An endpoint on a non-loopback address is refused unless `admin-allow-remote = true` is set
explicitly. A bearer token on an open port is a different threat model, not a convenience.

---

## Configuration

```ini
plugin = node_admin

admin-endpoint = 127.0.0.1:8099
admin-token = agent:read:3f8c…(64 hex)…e1
admin-audit-log = /var/lib/vizd/node-admin-audit.log
admin-log-file = node=/var/log/vizd/vizd.log
```

| Option | Default | Description |
|---|---|---|
| `admin-endpoint` | — | Listen address. Empty/absent: the plugin is inert. |
| `admin-token` | — | `label:scope[,scope]:sha256hex[:expires-unixtime]`. Repeatable. |
| `admin-allow-remote` | `false` | Permit a non-loopback `admin-endpoint`. |
| `admin-audit-log` | next to `config.ini` | Append-only audit log (0600, rotated at 16 MB). |
| `admin-log-file` | — | `name=path` entries that `admin.logs.tail` may read. Repeatable. |

The plugin refuses to start if `admin-endpoint` is set but no token is configured: an
unauthenticated admin port is never a valid state.

### Tokens

A token is 32 random bytes, for example `openssl rand -hex 32`. Only its SHA-256 is stored
in the configuration; comparison is constant-time. Scopes:

* `read` — every method, including `admin.p2p.status`. Every token needs it.
* `p2p` — the mutating peer methods `admin.p2p.add_node`, `admin.p2p.add_trusted`,
  `admin.p2p.clear_banned`. A token with only `p2p` (without `read`) cannot call anything.
* `plugins`, `config`, `process`, `chain` — reserved for the mutating stages; a token that
  carries only these cannot call anything yet.

Revocation is editing the line and restarting. `expires-unixtime` (optional) makes the
token invalid after that moment without a restart.

---

## Protocol

`POST` with `Content-Type: application/json` and `Authorization: Bearer <token>`.
Body ≤ 64 KB. Responses are JSON-RPC 2.0:

```json
{"jsonrpc":"2.0","id":1,"result":{...}}
{"jsonrpc":"2.0","id":1,"error":{"code":-32002,"message":"forbidden",
 "data":{"reason":"token 'agent' has no 'read' scope"}}}
```

| Code | Meaning |
|---|---|
| `-32700` | Parse error |
| `-32600` | Invalid request |
| `-32601` | Method not found |
| `-32602` | Invalid params |
| `-32001` | Unauthorized (HTTP 401) |
| `-32002` | Missing scope (HTTP 200) |
| `-32004` | Unknown/not allowed name (e.g. log file) |
| `-32005` | Rate limit: 20 requests/second per token (HTTP 429) |
| `-32099` | Internal error |

Authentication is checked **before** the body is parsed.

The only non-`POST` route is `GET /ui/` (and `GET /ui`), which serves the built-in page
described below. It carries no data and no secret; every actual call the page makes is a
normal authenticated `POST` back to `/`. `HEAD` works on the same path; any other method or
path still answers `405`.

---

## Methods

### `admin.ping`

```json
{"ok": true, "server_time": "2026-10-19T14:40:00Z"}
```

Liveness check; does not touch the chain database.

### `admin.node_info`

| Field | Meaning |
|---|---|
| `version`, `hardfork_count` | Build version and number of hardforks known to the binary |
| `chain_id` | Chain identifier |
| `head_block_num`, `head_block_id`, `head_block_time` | Current head |
| `head_age_sec` | Wall-clock age of the head block; `null` on an empty chain (head time there is 1970, so an "age" would be meaningless) |
| `last_irreversible_block_num` | Last irreversible block |
| `recovering` | Chain plugin is recovering shared memory |
| `uptime_sec` | Time since the admin listener started |
| `server_time` | UTC now |

### `admin.diagnostics`

Everything from `admin.node_info`, plus:

| Field | Meaning |
|---|---|
| `data_dir`, `config_path` | Paths in use |
| `p2p_loaded`, `peers` | P2P plugin present, active connection count (read on the p2p thread) |
| `plugins_configured`, `plugins_observed` | `plugin =` entries in `config.ini`, and how many of those have their lifecycle state read directly (see `admin.plugins.list`) |
| `shared_memory_bytes` | Size of `shared_memory.bin` |
| `disk_total_bytes`, `disk_free_bytes` | Filesystem of `data_dir` (POSIX) |
| `rss_peak_kb` | Peak RSS (POSIX) |
| `loadavg` | 1/5/15-minute load average (POSIX) |

Numbers are emitted as JSON numbers, not strings: fc's default formatter quotes int64 values
above 2^32-1 and every double, which would turn byte counts and load averages into text.

### `admin.plugins.list`

Lists the deduplicated `plugin = …` entries from `config.ini`. AppBase keeps its plugin
registry private (name-based lookup is protected and there is no enumeration API), so the
config file is the population this method can see; plugins enabled only on the command line
are not listed.

Each entry carries `loaded`, `state` and `state_source`:

* `state_source: "observed"` — the lifecycle state was read from the running plugin
  (`chain`, `p2p`, `node_admin`). `state` is one of `registered`, `initialized`, `started`,
  `stopped`; `loaded` is false only while the plugin is still merely `registered`.
* `state_source: "inferred"` — AppBase initializes every `plugin =` line and aborts startup
  when a name is unknown, so a configured plugin on a running node is loaded. `state` is
  `null` rather than a guess.

### `admin.config.get`

Returns `config.ini` as ordered sections, preserving duplicate keys (needed for the
repeated `plugin` lines). **Values of secret-bearing keys are replaced with `***`**:
anything containing `private-key`, `private_key`, `password`, `secret`, `token`, `wif` or
`emergency`.

```json
{"path":"/etc/vizd/config.ini","exists":true,"sections":[
  {"name":"","entries":[{"key":"plugin","value":"chain"},
                        {"key":"private-key","value":"***"}]}]}
```

### `admin.logs.tail`

Params: `{"name":"node","bytes":16384,"lines":200}`. `name` must match an
`admin-log-file` entry; nothing outside that allow-list can be read. `bytes` is clamped to
64 KB and `lines` to 2000. WIF-looking strings and `key = value` secrets are masked.

### `admin.audit.tail`

Params: `{"lines":100}`. Returns the tail of the audit log.

### `admin.p2p.status`

No params. Requires only `read` (a token that may look can also look at peers).
Returns the same facts the node writes to its own log — `log_node_status()` every minute and
`log_peer_stats()` every five minutes — but on demand and in JSON:

| Field | Meaning |
|---|---|
| `as_of` | UTC timestamp of the reading |
| `uptime_sec` | Node uptime |
| `node_status`, `fork_status` | `SYNC`/`FORWARD`, `NORMAL`/`LOOKING_RESOLUTION`/`MINORITY` |
| `paused`, `isolated_peers`, `catching_up` | Block processing paused, seed-only peer mode, catch-up after pause |
| `head_block_num`, `lib_block_num` | Our head and last irreversible block |
| `dlt_earliest`, `dlt_latest` | The DLT block range held locally (what this node can serve) |
| `connections`, `active`, `max_connections` | Open connections, of which fully active, and the limit |
| `known_peers`, `trusted_peers`, `banned_peers` | Known-peer set, operator-trusted endpoints, currently banned |
| `best_peer_head` | Highest head reported by any tracked peer |
| `best_peer_head_lag` | `our head − best peer head`; **negative means we are behind** |
| `peers_ahead` | Number of peers whose head is above ours |
| `peers[]` | Per-peer detail, sorted by endpoint |

Each peer entry: `endpoint`, `node_id` (hex, empty before the hello), `lifecycle`
(`CONNECTING`/`HANDSHAKING`/`SYNCING`/`ACTIVE`/`DISCONNECTED`/`BANNED`), `node_status`,
`fork_status`, `incoming`, `trusted`, `exchange_enabled`, `fork_alignment`, `connected_sec`,
`peer_head`, `peer_lib`, `peer_dlt_earliest`, `peer_dlt_latest`, `head_lag` (ours − theirs),
`spam_strikes`, `ban_reason`, `ban_duration_sec`, `reconnect_backoff_sec`,
`next_reconnect_in_sec`.

```json
{"jsonrpc":"2.0","id":40,"method":"admin.p2p.status"}
```

```json
{"jsonrpc":"2.0","id":40,"result":{
  "as_of":"2026-10-19T15:10:00Z","uptime_sec":86400,
  "node_status":"FORWARD","fork_status":"NORMAL","paused":false,
  "isolated_peers":false,"catching_up":false,
  "head_block_num":22164,"lib_block_num":22150,
  "dlt_earliest":1,"dlt_latest":22164,
  "connections":3,"active":3,"max_connections":50,
  "known_peers":12,"trusted_peers":1,"banned_peers":0,
  "best_peer_head":22164,"best_peer_head_lag":0,"peers_ahead":0,
  "peers":[{"endpoint":"1.2.3.4:1776","node_id":"02ab…","lifecycle":"ACTIVE",
            "node_status":"FORWARD","fork_status":"NORMAL","incoming":false,
            "trusted":true,"exchange_enabled":true,"fork_alignment":true,
            "connected_sec":3600,"peer_head":22164,"peer_lib":22150,
            "head_lag":0,"ban_reason":"","next_reconnect_in_sec":0}]}}
```

### `admin.p2p.add_node`

Params: `{"endpoint":"1.2.3.4:1776"}` — IPv4 and port. Requires `read` **and** `p2p`.
Adds the endpoint to the known-peer set and dials it now (clearing any old backoff or ban on
that endpoint). The peer is not trusted.

```json
{"endpoint":"1.2.3.4:1776","trusted":false,"status":"added"}
```

### `admin.p2p.add_trusted`

Same params and response shape, `"trusted": true`. A trusted peer:

* is **never soft-banned** — `soft_ban_peer()` disconnects it and retries after 30 s instead
  of parking it for an hour;
* is **kept through the known-peer garbage collector**, which otherwise drops silent peers;
* is dialed immediately and re-dialed as needed.

The trusted set lives in memory: it is **not persisted across restarts**. To pin a peer
permanently, keep it in `p2p-seed-node` (or in the seed list used together with
`p2p-isolated-peers`) in `config.ini`.

### `admin.p2p.clear_banned`

No params. Requires `read` and `p2p`. Clears every soft ban (lifecycle `BANNED` or a
non-empty `ban_reason`), resets the reconnect backoff and retries those peers immediately.

```json
{"cleared":3,"status":"ok"}
```

Typical operator flow:

```bash
# status first: what is wrong and with whom
curl -s http://127.0.0.1:8099 -H "Authorization: Bearer $T" \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"admin.p2p.status"}' | jq '.result | {head_block_num, best_peer_head_lag, peers}'

# add a peer and trust it
curl -s http://127.0.0.1:8099 -H "Authorization: Bearer $T" \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":2,"method":"admin.p2p.add_trusted","params":{"endpoint":"1.2.3.4:1776"}}'

# after a fork/ban storm: retry everyone
curl -s http://127.0.0.1:8099 -H "Authorization: Bearer $T" \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":3,"method":"admin.p2p.clear_banned"}'
```

---

## MCP endpoint (Streamable HTTP)

Decision q#3052=B: the MCP server lives **inside this plugin**, not in a sidecar. The same
loopback listener, the same bearer tokens and the same audit log serve the Model Context
Protocol over HTTP; there is nothing extra to install on the host.

MCP requests are ordinary JSON-RPC 2.0 `POST`s to the admin endpoint:

| Method | Purpose |
|---|---|
| `initialize` | Protocol handshake. Supported versions: `2024-11-05`, `2025-03-26`, `2025-06-18` (echoed when requested, otherwise `2025-06-18`). Advertises only the `tools` capability. |
| `notifications/initialized` | Notification (no `id`); answered with HTTP 202 and an empty body. |
| `ping` | MCP keepalive; returns `{}`. |
| `tools/list` | The tools below with JSON-Schema `inputSchema`. |
| `tools/call` | `{"name":"admin.node_info","arguments":{…}}` — executes the matching `admin.*` method. |

Tools are 1:1 with the admin methods: `admin.ping`, `admin.node_info`, `admin.diagnostics`,
`admin.plugins.list`, `admin.config.get`, `admin.logs.tail`, `admin.audit.tail`,
`admin.p2p.status`, `admin.p2p.add_node`, `admin.p2p.add_trusted`, `admin.p2p.clear_banned`.
The last three additionally require the `p2p` scope, exactly as over plain JSON-RPC. Tool
execution errors are returned **inside** the result (`"isError": true` with the message in
`content[0].text`), as MCP expects; protocol errors (unknown tool, bad params, missing
scope) remain JSON-RPC errors.

```json
{"jsonrpc":"2.0","id":7,"result":{"tools":[
  {"name":"admin.node_info","description":"Chain id, head block, hardfork, uptime and head age.",
   "inputSchema":{"type":"object","properties":{},"additionalProperties":false}}]}}
```

```json
{"jsonrpc":"2.0","id":8,"result":{
  "content":[{"type":"text","text":"{\"head_block_num\":1234,…}"}],"isError":false}}
```

The endpoint is sessionless: no `Mcp-Session-Id` is issued, and `GET` on anything except
`/ui/` (the optional SSE stream included) answers `405`. Clients that require SSE are not
supported; JSON responses are.

Client configuration (Streamable HTTP transport) points at the same loopback address:

```json
{"mcpServers":{"viz-node":{"type":"http","url":"http://127.0.0.1:8099",
  "headers":{"Authorization":"Bearer <token>"}}}}
```

---

## Built-in UI (`/ui/`)

A single static page, embedded in the binary, served by the same listener:

```text
http://127.0.0.1:8099/ui/
```

It is plain HTML + vanilla JS — no build step, no external assets, no frameworks. The CSP
allows only the page's own inline style/script and same-origin `fetch`; everything else is
`'none'`.

**Authentication.** The page holds no secret of its own. On first use the operator pastes a
bearer token; it is kept in `sessionStorage` (per tab, cleared when the tab closes) and sent
as `Authorization: Bearer …` on every JSON-RPC call back to the same listener. The page
itself is served unauthenticated on purpose: it is identical for everybody, contains no data,
and a browser cannot attach a bearer header to a navigation anyway. Tokens are never put in
the URL (that would land in history and access logs).

**What it shows**

* node status, fork status, head/LIB, DLT block range, uptime;
* peer counts, best peer head and our lag behind it (negative = behind), peers ahead;
* a table of every tracked peer: direction, lifecycle, head, lag, node/fork status, flags
  (`trusted`, `exchange`, `aligned`), connection age, and ban/retry note;
* an action panel: add a peer by `IP:port`, add it as trusted, clear the banned list;
* a log pane with the outcome of every action and status error.

The status view refreshes every five seconds while the tab is visible.

**What it does not do.** No writes other than the three peer actions; no token management; no
config editing. It is a thin view over the same JSON-RPC contract documented above — anything
it can do, `curl` and an MCP client can do as well, and every call lands in the audit log
under the token's label (the page's own `GET /ui/` is audited with outcome `ui`).

**Token for the page.** Use a dedicated token with the scopes you want the page to have; a
`read`-only token gives a live, read-only dashboard, adding `p2p` enables the action buttons:

```ini
admin-token = ui:read,p2p:<sha256 of a 32-byte random token>
```


---

## Audit

Every request — including failed authentication, parse errors and rate limiting — appends
one JSON line:

```json
{"ts":"2026-10-19T14:40:00Z","rid":"17","token":"agent","method":"admin.node_info",
 "params":{},"outcome":"ok","code":0,"ms":2,"remote":"127.0.0.1:51234"}
```

`token` is the label, never the token or its hash. `remote` is the real peer address of the
connection (not `X-Forwarded-For`: this listener is never behind a proxy, so that header
would let a client pick its own attribution). `params` are masked with the same redaction
rules as the log tail. The file is created `0600` and rotated to `.1` at 16 MB.

---

## Security notes

* The listener is loopback-only by default; do not put it behind nginx.
* Never send the token in a query string — it would land in proxy/access logs.
* Treat the token as a credential to the node process. Use one token per agent so the
  audit log attributes calls correctly, and give each the smallest scope it needs.
* `admin.config.get` masks secrets by key name, not by value shape. A secret stored under
  an innocuous key would be printed; do not invent such keys.
* The admin API never returns WIF keys, and no method in this version writes to disk
  except the audit log.
* `GET /ui/` is unauthenticated by design and is the same bytes for every caller. It grants
  no access: without a token every call it makes gets `401`. It does not answer `OPTIONS`
  and sends no CORS headers, so another origin cannot drive it through a browser.
* Peer actions are in-memory. `add_trusted` and `add_node` survive until the process
  restarts; permanent pins belong in the P2P seed configuration, not in the admin API.

---

## Related

* [Plugin overview](./overview.md)
* [Webserver](./webserver.md) — the public API this plugin deliberately does not use

---

## Tests

`plugins/node_admin/tests/acceptance.sh` starts a throwaway node, drives the listener with
curl and checks this contract: authentication and scopes, config/log masking, the log
allow-list, status codes, audit, rate limit, P2P peer actions (including the `p2p`-scope
denial), MCP `initialize`/`tools/list`/`tools/call`, and the `/ui/` page. It is not part of
CTest because it needs a built `vizd` and two free ports.
