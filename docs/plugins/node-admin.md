# Node Admin API (`node_admin`)

Administrative JSON-RPC endpoint for the node process itself: health, configuration,
plugin state, logs and the audit trail. It is meant for operator tooling and AI agents
holding a bearer token — not for end users, and not as a replacement for the public API.
The same listener also serves an **MCP** (Model Context Protocol) endpoint over HTTP.

> **v1 is read-only.** It cannot change configuration, enable/disable plugins, restart the
> process or touch the validator key. Mutating methods are a later stage and will follow
> the same authentication/audit model with two-phase `plan`/`apply`.

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

* `read` — every v1 method.
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

Tools are 1:1 with the read methods: `admin.ping`, `admin.node_info`, `admin.diagnostics`,
`admin.plugins.list`, `admin.config.get`, `admin.logs.tail`, `admin.audit.tail`. Tool
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

The endpoint is sessionless: no `Mcp-Session-Id` is issued, and `GET` (the optional SSE
stream) answers `405`. Clients that require SSE are not supported; JSON responses are.

Client configuration (Streamable HTTP transport) points at the same loopback address:

```json
{"mcpServers":{"viz-node":{"type":"http","url":"http://127.0.0.1:8099",
  "headers":{"Authorization":"Bearer <token>"}}}}
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

---

## Related

* [Plugin overview](./overview.md)
* [Webserver](./webserver.md) — the public API this plugin deliberately does not use

---

## Tests

`plugins/node_admin/tests/acceptance.sh` starts a throwaway node, drives the listener with
curl and checks this contract (auth, scopes, masking, allow-list, status codes, audit, rate
limit). It is not part of CTest because it needs a built `vizd` and a free port.
