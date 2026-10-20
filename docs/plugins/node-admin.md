# Node Admin API (`node_admin`)

Administrative JSON-RPC endpoint for the node process itself: health, configuration,
plugin state, logs, the audit trail and P2P peer control. It is meant for operator tooling
and AI agents holding a bearer token — not for end users, and not as a replacement for the
public API. The same listener also serves an **MCP** (Model Context Protocol) endpoint over
HTTP and a small built-in web UI at `/ui/`.

> **Mutating surface: P2P peers, snapshots, validator keys, webserver policy, `config.ini`
> and the process.** `admin.p2p.add_node`, `admin.p2p.add_trusted` and
> `admin.p2p.clear_banned` change whom the node dials; `admin.snapshot.create` and
> `admin.snapshot.restore` write a snapshot file or replace the node's in-memory state;
> `admin.validator.*` enable/disable validators and change their signing keys by signing
> on-chain transactions; `admin.webserver.*` mutating methods change rate limits, the stats
> switch and the IP allow/deny lists; `admin.config.*` and `admin.plugins.*` edit `config.ini`
> through a two-phase `plan`/`apply`; `admin.restart` asks the process to exit. They require a
> token with the `p2p`, `snapshot`, `chain`, `webserver`, `config`, `plugins` or `process`
> scope respectively. Peer actions are serialized onto the P2P thread; validator actions are
> signed by the `validator_guard` plugin and broadcast to the network; webserver policy
> changes apply to the very next request; config/plugin edits are read at startup and
> therefore need a restart.

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
| `admin-plan-ttl` | `120` | Seconds a `plan_id` stays valid before `apply` refuses it. |

The plugin refuses to start if `admin-endpoint` is set but no token is configured: an
unauthenticated admin port is never a valid state.

The Logs tab reads a **file**, not the container's stdout. Nodes that log only to stderr
inside Docker need an appender that writes the same lines to disk, and that file is what
`admin-log-file` names:

```ini
[log.file_appender.node]
filename = /var/lib/vizd/logs/node.log

[logger.default]
level = info
appenders = stderr,node

admin-log-file = node=/var/lib/vizd/logs/node.log
```

Use the section form: flat `logger.default.*` keys are merged first and a later
`[logger.default]` section wins, so a flat line added on top of an existing section is
silently ignored. The file appender is configured to rotate hourly with one-day retention,
but that rotation does not actually happen in vizd: the chain runs on the fc fiber queue,
and the node's main thread lives in `boost/asio` after startup and never services it
(measured 2026-10-20 — one 37 MB file in nine hours, no hourly files). So the file is
bounded by neither time nor size; on `debug` it can reach gigabytes. For a hard limit use
the optional `capped_file` appender: one file holding only the last `max_lines` lines
(default 10000, `0` = unlimited).

```ini
[log.capped_file_appender.node]
filename = /var/lib/vizd/logs/node.log
max_lines = 10000
```

Point `admin-log-file` at the same path and the Logs tab reads it unchanged. The appender is
optional: nodes without the section, and older binaries that do not know the type, keep the
stock behaviour.

### Tokens

A token is 32 random bytes, for example `openssl rand -hex 32`. Only its SHA-256 is stored
in the configuration; comparison is constant-time. Scopes:

* `read` — every method, including `admin.p2p.status` and the snapshot download route. Every token needs it.
* `p2p` — the mutating peer methods `admin.p2p.add_node`, `admin.p2p.add_trusted`,
  `admin.p2p.clear_banned`. A token with only `p2p` (without `read`) cannot call anything.
* `snapshot` — `admin.snapshot.create` and `admin.snapshot.restore`. Listing and downloading
  snapshots need only `read`.
* `webserver` — the mutating `admin.webserver.*` methods (limits, stats switch, IP lists,
  reset, reload). `admin.webserver.status` and `admin.webserver.ips` need only `read`.
* `config` — `admin.config.plan` and `admin.config.apply` (editing allow-listed `config.ini`
  keys). `admin.config.backups` needs only `read`.
* `plugins` — `admin.plugins.plan` and `admin.plugins.apply` (editing the `plugin =` list).
* `process` — `admin.restart` and `admin.objects.repair`. Both shut the process down; a
  supervisor must start it again. Repair additionally leaves a marker that rewrites state at
  the next startup, so the scope is state-write, not just availability.
* `chain` — `admin.validator.plan` and `admin.validator.apply` (enabling/disabling validators
  and changing their signing keys). These calls are **only accepted while `validator_guard`
  is loaded, started and configured with an active key** — that plugin holds the key that
  signs the chain transaction, and it never exposes the key through this API. Without a live
  guard, a `chain` token can call nothing: `admin.validator.list` stays readable with `read`
  and reports why the mutating calls are unavailable.

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

The non-`POST` routes are the built-in UI and snapshot downloads:

* `GET /ui/` (and `GET /ui`) serves the built-in page described below. It carries no data and
  no secret; every actual call the page makes is a normal authenticated `POST` back to `/`.
* `GET /snapshot/latest` returns the newest snapshot as a file attachment. `GET /snapshot/<name>`
  returns a named file from the snapshot directory. Both require `Authorization: Bearer …`
  and the `read` scope — unlike `/ui/`, a snapshot is node state. `HEAD` works on both and
  carries the same headers, including `Content-Length`. Any other method or path answers `405`.

Download limits: the file must be a regular file directly in the snapshot directory (names
come from `admin.snapshot.status`; traversal is rejected), and files larger than **512 MB**
answer `413` — copy those with `scp` instead.

---

## Methods

### `admin.ping`

```json
{"ok": true, "server_time": "2026-10-19T14:40:00Z"}
```

Liveness check; does not touch the chain database.

### `admin.auth.me`

No params. Returns the identity the presented token carries — its `label`, its `scopes` and
its `expires` (unixtime, `null` when the token does not expire). The token and its hash are
**never** returned. The built-in UI uses this to hide actions the token cannot perform; an
agent can use it to discover its own permissions before planning a call.

```json
{"label":"ui-read","scopes":["read"],"expires":null}
```

### `admin.node_info`

| Field | Meaning |
|---|---|
| `version`, `hardfork_count` | Build version and number of hardforks known to the binary |
| `chain_id` | Chain identifier |
| `head_block_num`, `head_block_id`, `head_block_time` | Current head |
| `head_age_sec` | Wall-clock age of the head block; `null` on an empty chain (head time there is 1970, so an "age" would be meaningless) |
| `last_irreversible_block_num` | Last irreversible block |
| `emergency_consensus_active`, `emergency_consensus_start_block` | Chain-wide emergency consensus state and the block that started it (`0` when inactive) |
| `emergency_key_configured` | The validator plugin has an emergency key loaded |
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

Lists the deduplicated plugins named by `plugin = …` lines in `config.ini`. A line may carry
several names (`plugin = chain p2p`), exactly as AppBase reads it, so each name gets its own
row. AppBase keeps its plugin
registry private (name-based lookup is protected and there is no enumeration API), so the
config file is the population this method can see; plugins enabled only on the command line
are not listed.

Each entry carries `loaded`, `state` and `state_source`:

* `state_source: "observed"` — the lifecycle state was read from the running plugin
  (`chain`, `p2p`, `snapshot`, `webserver`, `validator`, `validator_guard`, `node_admin`).
  `state` is one of `registered`, `initialized`, `started`, `stopped`; `loaded` is false only
  while the plugin is still merely `registered`.
* `state_source: "inferred"` — AppBase initializes every `plugin =` line and aborts startup
  when a name is unknown, so a configured plugin on a running node is loaded. `state` is
  `null` rather than a guess.

Two further fields exist for the operator panel:

* `removable` — `false` for `chain` and `node_admin`, which `admin.plugins.plan` refuses to
  remove anyway. A client must render those as non-toggleable, not as merely "off".
* `params` — the allow-listed keys that belong to this plugin (`p2p`, `webserver`,
  `validator_guard`); an empty array means the plugin has no editable parameters and the
  panel should say so instead of showing an empty form. Logging keys are grouped under
  `logging`, which is not a plugin: they live in `admin.config.get`.
* `status_method` — the method that returns this plugin's **own** status (`admin.p2p.status`,
  `admin.webserver.status`, `admin.snapshot.status`, `admin.validator.list` for
  `validator_guard`), or `null` when the plugin exposes none. `null` is not an error: the
  panel should say "no status" instead of drawing an empty drawer.

The result also carries `known`: every plugin name the binary can load, which is the set
`admin.plugins.plan` validates `add` against. Configured names appear there too; a client
subtracts them to build the "add a plugin" selector.

### `admin.plugins.status`

`{"name":"p2p"}` returns the lifecycle state plus what the plugin is actually doing:

| Field | Meaning |
|---|---|
| `configured` | A `plugin =` line for this name exists in `config.ini` |
| `state`, `state_source` | As in `admin.plugins.list`; `state` is `null` when the plugin type is not linked into this binary |
| `detail_available`, `detail_method` | Whether a dedicated status method exists and which one was called |
| `detail` | The verbatim result of that method (`admin.p2p.status`, `admin.webserver.status`, `admin.snapshot.status`, `admin.validator.list` for `validator_guard`) |
| `reason` | Why there is no detail: unobserved type, plugin not running, or no status API of its own |

The detail method is called only for a **started** plugin: those handlers read
thread-owned state (peer map, connection counters) and would throw on a stopped one.

A plugin whose type is not linked into this binary (`snapshot` on a build without it, say)
answers `state: null` with `reason` — not an empty object and not a fabricated "started".

### `admin.objects.list`

The chainbase object graph as the running process sees it: one row per index (object type),
its row count and the index revision, plus how much of the mapped file is used.

```json
{"state_dir":"/var/lib/vizd/state","shared_memory_path":"/var/lib/vizd/state/shared_memory.bin",
 "shared_memory_bytes":34183577600,"mapped_bytes":34183577600,"free_bytes":12648009654,
 "revision":1234567,"head_block_num":1234567,"recovering":false,
 "schema":{"file_present":true,"stored":15,"compiled":15,"match":true},
 "block_log":{"path":"/var/lib/vizd/block_log","present":true,"bytes":2147483648},
 "markers":[],"index_count":36,"total_objects":9876543,
 "indexes":[{"type":"graphene::chain::account_object","type_id":7,"objects":812345,
             "revision":1234567}],
 "generated_at":"2026-10-20T13:40:00Z"}
```

Counts are read under the same weak read lock the chain API uses; if the block thread does
not yield in time the call returns the file facts plus `lock_error` instead of stalling.

The block log is reported as a file or a directory — the chain plugin wipes it with
`remove_all`, so a directory is a legal shape there.

### `admin.objects.check`

The same view, judged. `verdict` is `ok`, `warn`, `corrupted` or `unreadable`; the result
carries named `findings` (`severity`, `code`, `text`), counts, `replay_available` and
`recommended_repair` (`replay`, `resync` or `null`).

| Code | Severity | What it means |
|---|---|---|
| `read_lock_timeout` | critical | Shared memory did not answer a read lock; the process is wedged |
| `state_dir_missing` / `shared_memory_missing` | critical | The node has no state directory or no image |
| `shared_memory_truncated` | critical | The file is smaller than the mapping: truncated or replaced under a running process |
| `shared_memory_larger_than_mapping` | warn | Something grew the file under this process — usually a second node on the same state dir |
| `stale_marker` | critical | `undo_all_in_progress`, `resize_in_progress`, `force_resync` or `force_replay` survived an interrupted operation |
| `schema_mismatch` | critical | `state/schema_version` is absent or differs from the binary's `CHAIN_SCHEMA_VERSION`; the next startup wipes and rebuilds |
| `revision_behind_head` | warn | Index revision differs from head; the next startup replays the gap. Normal after a crash |
| `shared_memory_exhausted` | critical | Less than 8 MiB free: the node cannot write another block |
| `shared_memory_low` | warn | Less than 5% free |
| `global_properties_count` / `validator_schedule_count` | critical | Exactly one row expected in each |
| `account_index_empty` | critical | Head is above zero but the account index is empty |
| `block_log_missing` | warn | A replay cannot rebuild state; only a full resync can |
| `recovery_in_progress` | warn | The chain plugin is running an automatic recovery |

`corrupted` means hard evidence only (the criticals above); a revision mismatch after a
crash is `warn`, not corruption. `recommended_repair` is filled only when rewriting state can
fix the finding — exhausted shared memory is excluded, because the same size would fill again.

**What the check cannot see.** Page-level corruption, checksums and chainbase's own
environment stamp can only be verified with the file closed, so this is a structural check of
a live process, not `fsck`. A clean verdict does not promise that a replay will succeed.

### `admin.objects.repair`

`{"mode":"replay"|"resync","confirm":"repair"}` (scope `process`) writes a marker next to
`shared_memory.bin`, replies, and quits after 300 ms — the same lifecycle as `admin.restart`.
The marker is honoured by the chain plugin at the next startup, before it maps shared memory:

* `force_replay` — wipe state, keep the block log, reindex it from block 1. Cheaper than a
  network resync and enough whenever the block log itself is intact.
* `force_resync` — wipe state **and** block log, come up empty and sync from peers.

`confirm` must be exactly `repair` and the mode is re-validated against the files: `replay`
is refused with `-32000 no block log` when the log is absent or empty. If a `force_resync`
marker from the wedge watchdog is already present, the startup would perform the resync and
drop the replay request instead of replaying an empty log — so the handler clears the opposite
marker before writing its own, and names it in `removed_marker`. Without that step a confirmed
"replay (keeps blocks)" would silently drop the block log, because startup precedence is
`resync > replay`. A marker that cannot be cleared is a `-32000` refusal, not a promise the
node cannot keep.

**Snapshot-mode nodes.** The two marker effects above describe a node that starts on the
normal path. A node started with `--replay-from-snapshot` never reaches it: the chain plugin
rebuilds state from the snapshot and replays the block log on top, so a `replay` request ends
up as a snapshot rebuild, and a `resync` request comes back from the snapshot instead of
genesis. The repair is still a full state rebuild — which is what an operator wants — but
the response says so instead of repeating the generic wording. `effect` is computed from the
options the node was actually started with, and `snapshot_replay` is `true` on such a node.

The response carries `marker`, `removed_marker`, `effect` and `snapshot_replay`; the supervisor
must bring the node back (`--restart unless-stopped` / `Restart=always`), exactly as for a plain
restart.

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

It also returns `allow`: the editable subset, ready to render. Each item is
`{key, group, section, type, value, defaulted, min, max, values, needs_restart}`.

* `key` is the full address `admin.config.plan` accepts — `logger.default.level` for a key
  inside `[logger.default]` — so a client never has to know which keys are sectioned.
* `value` is `null` and `defaulted` is `true` when the file does not set the key: the node
  uses its code default, and the panel should show that instead of an empty field.
  A value read from the file is returned **normalized** (`yes`/`on`/`1` → `true`), so it
  matches the option list; `valid:false` means the line could not be normalized and should
  be flagged.
* `type` is `boolean`, `unsigned_int` (with `min`/`max`) or `log_level` (with `values`).
* `group` is the plugin the key belongs to (`p2p`, `webserver`, `validator_guard`) or
  `logging` for logger and appender settings.

### `admin.logs.list`

No params. Lists the files declared by `admin-log-file`, each with `name`, `path`, `exists`
and `size` (`null` when the file is missing). A configured appender that has not written yet
is still listed: "empty" and "not configured" are different facts.

```json
{"logs":[{"name":"node","path":"/var/lib/vizd/logs/node.log","exists":true,"size":1048576}]}
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

### `admin.snapshot.status`

Params: `{"offset":0,"limit":20,"after":"snapshot-block-84360000.vizjson"}` — all optional.
Requires `read` and the `snapshot` plugin to be loaded. Returns one page of the snapshot files
in the configured `snapshot-dir`, newest block first, and reports what the plugin is doing.
`limit` is clamped to 200.

Use `after` (the name of the last row you already have) instead of `offset` when paging a
directory that changes under the reader: a snapshot written between two pages shifts every
offset below it, so offsets repeat a row, and the retention sweep (24 h) shifts the other way
and makes offsets skip one. With `after` the page continues after that exact row. If the anchor
has already been pruned the reply carries `after_found: false` and falls back to `offset`.

| Field | Meaning |
|---|---|
| `as_of` | UTC timestamp of the reading |
| `dir` | Snapshot directory in use |
| `head_block_num` | Current head; `null` while a restore is rebuilding state |
| `snapshot_count` | Number of files in the whole directory |
| `snapshot_total` | Same total, named explicitly for paging clients |
| `offset`, `limit` | The page that was returned |
| `after`, `after_found` | The cursor that was sent and whether it still exists |
| `has_more` | More files exist after this page |
| `in_progress`, `restoring`, `recovering` | A snapshot is being written, a restore is running, the chain plugin is recovering |
| `latest_path`, `latest_name` | Newest snapshot (highest block number); `latest_path` is an empty string and `latest_name` is `null` when there are none |
| `snapshots[]` | `name`, `path`, `block_num` (0 when the name does not carry a block), `size_bytes`, `modified` (unixtime), `modified_iso`, `latest` |

```json
{"jsonrpc":"2.0","id":50,"method":"admin.snapshot.status","params":{"offset":0,"limit":20}}
```

```json
{"jsonrpc":"2.0","id":50,"result":{
  "as_of":"2026-10-19T15:30:00Z","dir":"/var/lib/vizd/snapshots",
  "head_block_num":22164,"snapshot_count":1,"snapshot_total":1,
  "offset":0,"limit":20,"has_more":false,"in_progress":false,
  "restoring":false,"recovering":false,
  "latest_path":"/var/lib/vizd/snapshots/snapshot-block-22164.vizjson",
  "latest_name":"snapshot-block-22164.vizjson",
  "snapshots":[{"name":"snapshot-block-22164.vizjson",
                "path":"/var/lib/vizd/snapshots/snapshot-block-22164.vizjson",
                "block_num":22164,"size_bytes":4404019,
                "modified":1792423800,"modified_iso":"2026-10-19T15:30:00Z","latest":true}]}}
```

### `admin.snapshot.create`

No params. Requires `read` and `snapshot`. Writes `snapshot-block-<head>.vizjson` into
`snapshot-dir` **asynchronously** on the plugin's snapshot thread; P2P block processing is
paused for the duration of the database read and resumed before compression. Poll
`admin.snapshot.status` until `in_progress` is false.

```json
{"jsonrpc":"2.0","id":51,"method":"admin.snapshot.create"}
```

```json
{"jsonrpc":"2.0","id":51,"result":{
  "status":"started","path":"/var/lib/vizd/snapshots/snapshot-block-22164.vizjson",
  "note":"snapshot creation runs on the node's snapshot thread; poll admin.snapshot.status until in_progress is false"}}
```

`status` is `started`, `exists` (a file for this head is already on disk), or `busy`
(another creation/restore is running). On an empty chain (head `0`) the call fails with
`-32000` and writes nothing.

### `admin.snapshot.restore`

Params: `{}` (restore the latest), `{"latest":true}`, `{"name":"snapshot-block-22164.vizjson"}`
or `{"path":"/var/lib/vizd/snapshots/<file>"}`. Requires `read` and `snapshot`.

The restore is **in-process**, not a process restart: P2P block processing is paused, shared
memory is closed and wiped, the snapshot is imported, `dlt_block_log` is replayed, peers are
reset and P2P resumes. The OS process and its uptime are unchanged, so the listener keeps
answering during the operation (with `restoring: true` and a `null` head). `path`, if given,
must resolve to a regular file directly inside the snapshot directory.

```json
{"jsonrpc":"2.0","id":52,"method":"admin.snapshot.restore","params":{"latest":true}}
```

```json
{"jsonrpc":"2.0","id":52,"result":{
  "status":"started","snapshot":"/var/lib/vizd/snapshots/snapshot-block-22164.vizjson",
  "snapshot_name":"snapshot-block-22164.vizjson","block_num":22164,
  "process_restarted":false,
  "note":"in-process restore: P2P is paused, shared memory is wiped, the snapshot is imported, dlt_block_log is replayed and P2P resumes. The OS process and its uptime are unchanged. Progress: admin.snapshot.status (restoring)."}}
```

A call that cannot start answers `-32000` (no snapshots, a creation is in progress, the file
is not readable); invalid names, traversal and files outside the snapshot directory answer
`-32602` before any state is touched. If the import itself fails, `do_snapshot_load` has
already wiped the database and requested shutdown — inspect the node log; the admin API does
not pretend the restore succeeded.

---

## Validator key methods (`chain` scope)

Decision q#3051=D: the operator manages validators through `node_admin`, but the **active
key never enters the admin API**. Transactions are built and signed inside the
`validator_guard` plugin, which is configured with the signing and active WIFs of each
validator:

```ini
plugin = validator_guard
validator-guard-validator = ["val1", "5J…signing…", "5J…active…"]
validator-guard-validator = ["val2", "5J…signing…", "5J…active…"]
```

The guard must be loaded, started and hold at least one configured validator; otherwise the
mutating methods answer `-32002 forbidden / validator-guard is not configured`, and
`admin.validator.list` reports `guard.reason`. A configured validator must exist **on chain**
before it can be planned; creating a new validator is not offered here (it charges the
validator declaration fee and changes the set of block producers).

### `admin.validators.active`

No params, `read` only. Returns the names in the current validator schedule (duplicates from
the repeat factor are collapsed), the number of scheduled slots, the emergency consensus
state and whether the validator plugin is running in this process. **No keys are returned** —
this is public chain data. Use `admin.validator.list` for the validators this node actually
operates.

```json
{"as_of":"2026-10-19T15:40:00Z","head_block_num":22164,"count":21,
 "num_scheduled":21,"validators":["val1","val2"],
 "emergency_consensus_active":false,"emergency_consensus_start_block":0,
 "validator_plugin_loaded":true}
```

### `admin.validator.list`

Scope `read`. No params. Returns:

```json
{"guard":{"loaded":true,"started":true,"available":true},
 "validators":[{"name":"val1","on_chain":true,"enabled":true,"hold":false,
                "active_authority":true,"scheduled_in":2,
                "signing_key":"VIZ6…","configured_key":"VIZ6…"}]}
```

* `enabled` — the on-chain signing key is not null.
* `hold` — the operator disabled this validator through this API; the guard's auto-restore
  is suppressed for it until an `enable`/`set_key` succeeds (see below).
* `active_authority` — the guard's active key is in the account's active authority. `false`
  means a transaction signed by the guard will be rejected by the chain; the same warning is
  logged at guard startup.
* `scheduled_in` — position (1-based) of the next 3 scheduled slots this validator produces,
  `null` when it is not in them.

### `admin.validator.plan`

Scope `chain`. Params: `{"actions":[{"name":"val1","action":"disable"},…]}`. `action` is
`enable` (restore the guard's configured signing key), `disable` (null the signing key and
set the operator hold) or `set_key` (requires `signing_key`, a base58 public key). Every name
must be configured in the guard and exist on chain; duplicates are refused.

The reply carries `plan_id`, `kind:"validators"`, `transactions` (how many chain
transactions the plan would send) and a `diff` per action: `from`/`to` signing keys,
`hold_from`/`hold_to`, `tx_needed`. An action that is already satisfied is omitted; a plan
with no effective change is refused with `-32602 no changes`. **Nothing is signed or
broadcast yet.** Plans are single-use, bound to the token label and expire after
`admin-plan-ttl`, exactly like config plans. Capturing the current key in the plan is what
makes a stale plan safe: if the chain moved in between, `apply` refuses that action instead
of overwriting somebody else's update.

### `admin.validator.apply`

Scope `chain`. Params: `{"plan_id":"…"}`. Signs one `validator_update` per changed action with
the guard's active key, puts it into the local pending pool and broadcasts it to peers. The
reply lists per-action `status`: `applied` (with `tx_id`), `already` (the chain reached the
desired state on its own) or `failed` (with `error`). `applied`, `unchanged` and `failed`
counts are given too.

**The operator hold follows the outcome, never the intent.** A successful `disable` writes
the validator into `<data-dir>/validator-guard-holds.txt`; the guard's periodic auto-restore
skips held validators, and the file is re-read at startup, so a restart does not silently
re-enable it either (disable-on-shutdown does not create a hold — it is a normal shutdown
action). A successful `enable` or `set_key` removes the hold. A failed disable leaves no
latch: the validator stays as it was.

Transactions are built under the chain's read lock and broadcast after it is released, the
same rule as every other chain write from this plugin. No restart is needed.

---

## Webserver policy methods

These methods manage the public API plugin: per-IP request counters, the stats log, rate
limits and the IP lists. Their semantics, defaults and log format are documented in
[Webserver — Request Accounting and Rate Limiting](./webserver.md#request-accounting-and-rate-limiting).

**Changes are live.** The plugin keeps this state in memory; a change applies to the next
request the webserver receives, with no restart and no re-reading of `config.ini`. Every
change is also persisted atomically to `webserver-admin-state`
(`<data-dir>/webserver-admin.json` by default), which is loaded at startup and therefore
overrides the `config.ini` seed. The `reload` method re-reads that file for manual edits.

| Method | Scope | Params | Purpose |
|--------|-------|--------|---------|
| `admin.webserver.status` | `read` | none | Limits, totals, IP lists and tracked-IP count. |
| `admin.webserver.ips` | `read` | `{"limit":200}` (max 5000) | Per-IP rows: `total`, `minute`, `hour`, `day`, `blocked`, `limited`, `last_seen`, `whitelisted`, `blacklisted`, sorted by total. |
| `admin.webserver.limits` | `webserver` | `{"enabled":bool,"per_minute":N,"per_hour":N,"per_day":N}` | Omitted fields keep their value. |
| `admin.webserver.stats` | `webserver` | `{"enabled":bool}` | Turn request accounting on/off. Limits are controlled separately. |
| `admin.webserver.whitelist_add` / `whitelist_remove` | `webserver` | `{"ip":"1.2.3.4"}` | Whitelisted addresses bypass limits but are still counted. |
| `admin.webserver.blacklist_add` / `blacklist_remove` | `webserver` | `{"ip":"1.2.3.4"}` | Blacklisted addresses get `403`; enforced even with stats off. |
| `admin.webserver.reset` | `webserver` | none | Zero the counters; keep lists and limits. |
| `admin.webserver.reload` | `webserver` | none | Re-read the state file from disk; returns the applied status. |

Adding an IP to one list removes it from the other. An address must be a single IPv4 or IPv6
literal; anything else answers `-32602`.

```json
{"jsonrpc":"2.0","id":53,"method":"admin.webserver.limits","params":{"per_minute":600}}
```

```json
{"jsonrpc":"2.0","id":53,"result":{
  "status":"applied",
  "limits":{"enabled":true,"per_minute":600,"per_hour":10000,"per_day":100000},
  "stats_enabled":true,
  "note":"applied to the live webserver on the next request and persisted to its admin state file; config.ini is unchanged."}}
```

A node without the `webserver` plugin loaded answers `-32004 webserver unavailable`. The
public webserver rejects blacklisted clients with `403` and rate-limited clients with
`429` + `Retry-After: 60`; those responses are produced by the webserver, not by this API.

---

## Config and plugin editing (two-phase)

`config.ini` is edited through a **plan** that returns the exact diff and writes nothing, and
an **apply** that re-validates the file and replaces it atomically. This makes an agent's
intent reviewable before it touches a production node and keeps a crash mid-apply from
leaving a truncated file.

### Plan semantics

* `admin.config.plan {"set":{"key":value,…}}` validates every key against the **allow-list**
  below, normalizes values (`yes/on/1` → `true`; integers are range-checked) and returns
  `plan_id`, `kind:"config"`, `diff` (`[{key, from, to}]`), `config_path`, `needs_restart:true`
  and `expires_at`. **Nothing is written.** A plan with no effective change is refused with
  `-32602`.
* `admin.plugins.plan {"add":[…],"remove":[…]}` validates plugin names against the set the
  binary can load, refuses duplicates, absent names and removals of `chain` and `node_admin`,
  and returns `diff` of `[{op:"add"|"remove", name}]`.
* A plan is **single-use** (consumed on the first `apply`, successful or not),
  **bound to the token label** that created it (`-32002` for any other token) and **expires**
  after `admin-plan-ttl` seconds (`-32004 plan expired`). Up to 64 plans are kept; the
  oldest is dropped when a new one arrives. `admin.plan.cancel` discards a plan explicitly.
* `apply` recomputes the SHA-256 of `config.ini` and refuses with `-32000 config changed …
  nothing was written` if the file moved after the plan was made. It then normalizes and
  re-validates every value again — an apply never trusts the plan blindly.

### Atomic write and backups

Both appliers edit the file as **raw lines**, so comments, ordering, spacing and CRLF are
preserved. A top-level key that is absent is inserted before the first `[section]`; an
existing top-level key is changed in place. An allow-listed key whose spec names a section
(see the logging row in the table below) is edited inside that section, and inserted
directly under its header when the section exists but the key does not.

Each successful apply writes one backup next to the config — `<config.ini>.bak-<unixtime>`
(with a `-n` suffix on collision) — then a temp file in the same directory, `fsync`s it,
applies the original file's permissions, `rename`s it over `config.ini` and `fsync`s the
directory. A failed write leaves the original file untouched. Old backups are never pruned
by the node; they contain secrets (tokens, keys), so manage them like the config itself.

`admin.config.backups` (scope `read`) lists `<config.ini>.bak-*`, newest first, with size and
mtime. Rolling back by hand is copying a backup back and restarting.

### Allow-list

The admin API edits only these keys. Everything else — identity, keys, data dir, endpoints,
`plugin`, `shared-file-size` — is deliberately out of reach of a bearer token.

| Keys | Type | Range |
|---|---|---|
| `p2p-max-connections` | uint | 1–10000 |
| `dlt-peer-max-disconnect-hours` | uint | 1–8760 |
| `dlt-mempool-max-tx` | uint | 1–1000000 |
| `dlt-mempool-max-bytes` | uint | 1024–1073741824 |
| `dlt-mempool-max-tx-size` | uint | 256–67108864 |
| `dlt-mempool-max-expiration-hours` | uint | 1–720 |
| `dlt-peer-exchange-max-per-reply` | uint | 0–1000 |
| `dlt-peer-exchange-max-per-subnet` | uint | 0–1000 |
| `dlt-peer-exchange-min-uptime-sec` | uint | 0–86400 |
| `dlt-stats-interval-sec` | uint | 10–86400 |
| `p2p-isolated-peers`, `auto-resync-on-wedge` | bool | — |
| `webserver-thread-pool-size` | uint | 1–4096 |
| `webserver-cache-enabled` | bool | — |
| `webserver-cache-size` | uint | 0–10000000 |
| `webserver-stats-enabled`, `webserver-limits-enabled` | bool | — |
| `webserver-limit-m`, `webserver-limit-h`, `webserver-limit-d` | uint | 1–100000000 |
| `webserver-stats-log-max-size` | uint | 1024–1073741824 |
| `webserver-stats-log-keep` | uint | 1–1000 |
| `webserver-stats-interval` | uint | 1–86400 |
| `validator-guard-enabled`, `validator-guard-disable`, `validator-guard-disable-on-shutdown` | bool | — |
| `validator-guard-interval`, `validator-guard-shutdown-grace` | uint | 1–3600 |
| `validator-guard-disable-max` | uint | 0–1000 |
| `logger.default.level`, `logger.p2p.level` | log level | `all`, `debug`, `info`, `warn`, `error`, `off` |
| `log.capped_file_appender.node.max_lines` | uint | 0–10000000 |
| `log.capped_file_appender.node.flush` | bool | — |

The logging keys are addressed with their section: `logger.default.level` edits `level`
inside `[logger.default]`, not a top-level option. A section that is absent from
`config.ini` is an error rather than an insert — the node never declares it, so the value
would be silently ignored at the next start. Appender filenames and rotation windows stay
operator-only: a typo'd path is a lost log, not a tunable.

### Example

```json
{"jsonrpc":"2.0","id":80,"method":"admin.config.plan",
 "params":{"set":{"p2p-max-connections":200,"webserver-limit-m":600}}}

{"jsonrpc":"2.0","id":80,"result":{
  "plan_id":"9f2c41a0d3b54e8770c5f1a2b3c4d5e6","kind":"config",
  "config_path":"/var/lib/vizd/config.ini","needs_restart":true,
  "diff":[{"key":"p2p-max-connections","from":"64","to":"200"},
          {"key":"webserver-limit-m","from":null,"to":"600"}],
  "expires_at":"2026-10-19T14:42:00Z",
  "hint":"nothing was written yet; apply with admin.config.apply {plan_id}…"}}
```

```json
{"jsonrpc":"2.0","id":81,"method":"admin.config.apply",
 "params":{"plan_id":"9f2c41a0d3b54e8770c5f1a2b3c4d5e6"}}

{"jsonrpc":"2.0","id":81,"result":{
  "applied":true,"kind":"config",
  "config_path":"/var/lib/vizd/config.ini",
  "backup":"/var/lib/vizd/config.ini.bak-1760884860",
  "needs_restart":true,
  "restart_hint":"config.ini is read at startup: call admin.restart {confirm:\"restart\"}…"}}
```

Plugin example:

```json
{"jsonrpc":"2.0","method":"admin.plugins.plan",
 "params":{"add":["block_info"],"remove":[]}}
{"jsonrpc":"2.0","method":"admin.plugins.apply","params":{"plan_id":"…"}}
```

Both appliers answer `-32004 config unavailable` when `config.ini` cannot be read, and
`-32000 write failed` when the atomic replace fails (the original file is left untouched).

---

## Restart and supervision

`admin.restart {"confirm":"restart"}` (scope `process`) asks `appbase` to quit: the handler
replies first, then a detached thread calls `quit()` after 300 ms so the HTTP response
reaches the client. The process exits with code 0.

**The plugin cannot start the node again.** That is the supervisor's job — the container's
`--restart unless-stopped` or systemd `Restart=always`. With neither, the node simply stays
down; the reply says so in `hint`, and that is deliberate: an admin endpoint that forks a new
node process would be a privilege-escalation path out of the process's own lifecycle.

Because `config.ini` and the plugin list are read at startup, `admin.config.apply` and
`admin.plugins.apply` take effect only after this restart. `confirm` must be exactly the word
`restart`; anything else is `-32602`. A second call while shutdown is pending answers
`-32000 restart already requested`.

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

Tools are 1:1 with the admin methods: `admin.ping`, `admin.auth.me`, `admin.node_info`,
`admin.diagnostics`, `admin.plugins.list`, `admin.plugins.status`, `admin.objects.list`,
`admin.objects.check`, `admin.objects.repair`, `admin.config.get`, `admin.logs.list`,
`admin.logs.tail`, `admin.audit.tail`, `admin.validators.active`,
`admin.p2p.status`, `admin.p2p.add_node`, `admin.p2p.add_trusted`, `admin.p2p.clear_banned`,
`admin.snapshot.status`, `admin.snapshot.create`, `admin.snapshot.restore`,
`admin.webserver.status`, `admin.webserver.ips`, `admin.webserver.limits`,
`admin.webserver.stats`, `admin.webserver.whitelist_add`, `admin.webserver.whitelist_remove`,
`admin.webserver.blacklist_add`, `admin.webserver.blacklist_remove`, `admin.webserver.reset`,
`admin.webserver.reload`, `admin.validator.list`, `admin.validator.plan`,
`admin.validator.apply`, `admin.config.plan`, `admin.config.apply`, `admin.config.backups`,
`admin.plugins.plan`, `admin.plugins.apply`, `admin.plan.cancel`, `admin.restart`
(forty-one).
The peer actions additionally require the `p2p` scope, the snapshot actions the `snapshot`
scope, validator operations the `chain` scope (and a live `validator_guard` with an active
key), the webserver actions the `webserver` scope, config/plugin edits the `config` or
`plugins` scope, and restart and state repair the `process` scope, exactly as over plain
JSON-RPC. Tool execution errors are returned **inside** the
result (`"isError": true` with the message in `content[0].text`), as MCP expects; protocol
errors (unknown tool, bad params, missing scope) remain JSON-RPC errors.

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
`/ui/` and the `/snapshot/…` routes (the optional SSE stream included) answers `405`. Clients
that require SSE are not supported; JSON responses are.

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

The page derives its JSON-RPC base from its own URL, so it also works when an operator
publishes the listener behind a reverse-proxy prefix instead of the root: with nginx
`location /admin/ { proxy_pass http://127.0.0.1:9099/; }` the page is at `/admin/ui/` and
calls `/admin/`. At the root (`/ui/`) the base is `/`, as before.

**Authentication.** The page holds no secret of its own. On first use the operator pastes a
bearer token; it is kept in `sessionStorage` (per tab, cleared when the tab closes) and sent
as `Authorization: Bearer …` on every JSON-RPC call back to the same listener. The page
itself is served unauthenticated on purpose: it is identical for everybody, contains no data,
and a browser cannot attach a bearer header to a navigation anyway. Tokens are never put in
the URL (that would land in history and access logs).

**What it shows**

The page is organised as seven tabs; the last one is remembered per tab session.

* **Status** — node state, emergency consensus (`ACTIVE` with the starting block, or `normal`),
  whether an emergency key is configured, head/LIB, head age, version, uptime, peer counts,
  best peer head and our lag behind it, bans and flags. The snapshot tab's own state card
  spells out `idle` ("no snapshot job is running") versus `creating` and `restoring`.
* **Peers** — a table of every tracked peer: direction, lifecycle, head, lag, node/fork
  status, flags (`trusted`, `exchange`, `aligned`), connection age, ban/retry note. With the
  `p2p` scope an action panel adds a peer by `IP:port`, adds it as trusted, or clears bans.
* **Snapshots** — state, directory, total count and the newest file; the newest **20** files
  with **Load more** paging through `offset`/`limit`, block, size, age and per-row
  **Download** / **Restore**, plus **Create snapshot** and **Restore latest**. A download
  request disables its button and shows a radial progress spinner until the response body
  arrives, so repeated clicks cannot start parallel downloads. Downloads use the
  authenticated `/snapshot/<name>` route and are saved by the browser under the file's own
  name.
* **Logs** — a selector over `admin-log-file` entries, a **Play/Pause** control and a
  scrolling pane. It starts **paused**: pressing Play polls `admin.logs.tail` every two
  seconds and appends only the new lines at the bottom, colouring them by severity. When the
  operator scrolls up, the pane stops auto-scrolling; scrolling back to the bottom resumes
  it.
* **Plugins** — the plugins listed in `config.ini` with their observed state and how many
  editable parameters each one has, a **Disable** button per row (`chain` and `node_admin`
  show a disabled **protected** button instead) and a selector that adds a plugin the binary
  can load. Every change goes through `plugins.plan` → a confirmation that spells out the
  consequence → `plugins.apply`. Below the table, **Parameters** renders the allow-listed
  keys grouped by plugin and **Logging** renders the logger and appender keys; each group
  saves through `config.plan`/`config.apply`. Values that come from code (the key is absent
  from `config.ini`) are marked `code default` rather than shown as blank. After any apply a
  banner says the file changed and offers **Restart node**, which calls `admin.restart` after
  two confirmations and works only with the `process` scope. A **Status** button on every row
  whose plugin has a status API calls `admin.plugins.status` and shows the result verbatim
  above the table; rows without one have no button, so the absence is visible rather than an
  empty panel.
* **Objects** — the chainbase view: shared-memory path and size, mapped/free/reserved bytes,
  index revision versus head, schema versions, block-log size, repair markers, and a table of
  object types with row counts and per-index revision, largest first. **Check** runs
  `admin.objects.check` and paints the verdict (`ok` / `warn` / `corrupted` / `unreadable`)
  with each finding as `<code>SEVERITY [code] text</code>`; a recommended repair mode
  preselects the selector. **Restart with repair** sends `admin.objects.repair` after two
  confirmations that spell out the difference between replay (keeps blocks) and resync
  (drops them), and needs the `process` scope. Like Plugins, this tab is outside the 5-second
  timer so a pending verdict is not repainted under the operator.
* **Validators** — the names in the current validator schedule (public chain data, no keys),
  the schedule size and the emergency state; below it, the validators this node operates via
  `validator_guard`, when the plugin is live.

**Scopes drive the page.** On load the page calls `admin.auth.me` and hides every action the
token cannot perform: without `snapshot` the Create/Restore buttons and the per-row Restore
disappear, without `p2p` the peer action panel disappears, without `plugins` the Disable/Add
controls disappear, without `config` the parameter inputs are locked, and without `process`
the Restart and Repair buttons are disabled. A read-only token therefore sees a
dashboard with listing and downloads only, and the page says so instead of offering buttons
that would be refused with `-32002`.

The status view refreshes every five seconds while the tab is visible.

**What it does not do.** No token management and no deletion of snapshot files. Writes are
the three peer actions, snapshot creation, the in-process restore, `config.ini` edits through
the plan/apply pair, a restart request and a state-repair request (which is a marker plus a
restart — the actual rebuild happens at the next startup, inside the chain plugin, not in the
admin plugin). It is a thin view over the same JSON-RPC contract documented above — anything it can
do, `curl` and an MCP client can do as well, and every call lands in the audit log under the
token's label (the page's own `GET /ui/` is audited with outcome `ui`, downloads as
`snapshot_download`).

**Token for the page.** Use a dedicated token with the scopes you want the page to have; a
`read`-only token gives a live, read-only dashboard with snapshot download and listing,
adding `p2p` enables the peer action buttons, and adding `snapshot` enables **Create** and
**Restore**:

```ini
admin-token = ui:read,snapshot,p2p:<sha256 of a 32-byte random token>
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
* The admin API never returns WIF keys. Besides the audit log, the methods that write to disk
  are `admin.snapshot.create` (into the configured snapshot directory) and
  `admin.config.*`/`admin.plugins.*` apply (the config plus one `.bak-*` per apply).
* `admin.snapshot.restore` **replaces all chain state** with the chosen file. It pauses block
  processing and wipes shared memory; a wrong or stale snapshot is destructive. Treat the
  `snapshot` scope as equivalent to write access to the node's state, and prefer a snapshot
  taken at or below the peers' LIB when the node is expected to re-sync.
* Snapshot downloads are served by the admin listener only, never by the public webserver.
  Do not copy them to a world-readable path; a snapshot contains full chain state and,
  depending on the build, account metadata.
* `GET /ui/` is unauthenticated by design and is the same bytes for every caller. It grants
  no access: without a token every call it makes gets `401`. It does not answer `OPTIONS`
  and sends no CORS headers, so another origin cannot drive it through a browser.
* Peer actions are in-memory. `add_trusted` and `add_node` survive until the process
  restarts; permanent pins belong in the P2P seed configuration, not in the admin API.
* The `webserver` scope can change the availability of the public API (a too-strict limit or
  a wrong blacklist entry rejects legitimate clients). A token with it is a policy
  credential, not just a read credential; changes also persist across restarts through
  `webserver-admin-state`.
* The `config` and `plugins` scopes edit `config.ini` on disk. The allow-list keeps identity,
  keys, endpoints and `plugin` out of reach, but the keys that are editable still change node
  behaviour at the next restart. `apply` re-checks the file hash, so an edit made by someone
  else after the plan is never silently overwritten; the backup is the rollback path.
* `admin.config.backups` lists files that contain secrets (tokens, `private-key` lines). They
  are created with the original file's permissions; do not copy them to a world-readable path.
* The `process` scope stops the node. Without a supervisor the node does not come back; the
  admin API deliberately cannot restart itself outside the process lifecycle.
* `admin.objects.repair` is a write credential disguised as a maintenance action.
  `mode=replay` discards all state and rebuilds it from the block log;
  `mode=resync` **also deletes the block log**, and the node can then only resync from peers.
  Run `admin.objects.check` first, and treat the scope as equivalent to deleting the state
  directory by hand.

---

## Related

* [Plugin overview](./overview.md)
* [Webserver](./webserver.md) — the public API this plugin deliberately does not use

---

## Tests

`plugins/node_admin/tests/acceptance.sh` starts a throwaway node, drives the listener with
curl and checks this contract: authentication and scopes, config/log masking, the log
allow-list, status codes, audit, rate limit, P2P peer actions (including the `p2p`-scope
denial), snapshot status/listing/download (including `HEAD`, traversal and missing-file
cases, and the `snapshot`-scope denial on create/restore), per-plugin status (a started
plugin with its own view, an unlinked type answering `state: null` with a reason, a started
plugin without a status API, and the missing-name refusal), chainbase objects (index count
matches the array, largest first, schema matches the binary, a clean verdict on a fresh node,
and the repair refusals — read scope, missing `confirm`, unknown mode, and `replay` with an
empty block log, which must leave no marker behind), MCP
`initialize`/`tools/list`/`tools/call`, webserver stats/limits (burst → `429`, whitelist
bypass, blacklist `403`, hot limits change, persistence and reload), config/plugin
plan-and-apply (allow-list, scope and token binding, single use, expiry, stale-hash refusal,
backups, plugin protection), validator key operations (the list stays readable and names the
missing plugin, `plan` is refused without `chain` and again without a live guard; after the
restart the guard is loaded and started but has no active-key validator, so `plan` stays
refused), restart (`confirm` guard, scope guard, clean exit 0 and a
supervisor-style start that reads the edited config back), and the `/ui/` page. It
starts the webserver plugin on a third port and needs a built `vizd` and three free ports,
so it is not part of CTest.

The signing path itself cannot run in that script: a fresh chain has no second validator, and
the genesis `committee` account has an empty active authority. It is exercised by
`plugins/node_admin/tests/validator-e2e.sh`, which builds its own scratch chain (a temp dir and
three ports; `--keep` leaves it behind for inspection). It stages a testnet one-slot chain
through HF9/HF12 — `viz` is registered, vested and voted in the first blocks, because votes weigh
vesting shares, HF9 deletes validators that never signed a block and HF12 excludes `committee`
from selection — then creates two more validators: `backup` carries the operator actions and
`cold` lets a batch plan name two different validators. Checks: list (`on_chain`, `enabled`,
`active_authority`), disable/enable/set-key with a transaction id, the hold file surviving a node
restart, a stale plan refused with `chain state changed` and nothing signed, a two-action plan
applying both actions, the producing validator rotating its own signing key, and the contract
corners (duplicate, unknown, bad key, no-op → `-32602`).

Two staging decisions are load-bearing. Operator actions never target the only *scheduled*
validator: the schedule is rebuilt only at the end of an applied block, so nulling its key leaves
a stale schedule pointing at a keyless validator and bricks a one-slot chain. The self-rotation
section is a regression check for a fixed node bug: a block header is validated against the key
committed at head, so the block that carries the update must be signed with the old key and the
new key takes effect for the next block. The producer used to read the signing key from the
session-visible validator object, so a pending `validator_update` that rotated the producing
validator's key made it sign with the new key, `validate_block_header` rejected the block, the
pending pool was cleared and the update was silently lost. The plugin now keeps a committed-key
snapshot (seeded at startup, refreshed from applied block contents) and production signs with
that key.

`tests/node_admin/` holds two CTest targets (Boost.Test). `node_admin_util` covers the pure
helpers: secret masking, token parsing, the config parser, the allow-list and normalization, and
the raw-line editors (spacing, CRLF, sections, plugin add/remove guards).

`node_admin_objects` covers the verdict branches of `admin.objects.check` in
`objects_check.hpp`, which is deliberately free of fc and chainbase so every arm can be driven
by a hand-built state: healthy, crash-lag (warn, not corrupted), truncation (replay), no block
log (resync), exhaustion alone (no repair offered — the same size fills again), exhaustion next
to another critical (replay), stale markers, schema rules, core index cardinality above and at
genesis, the 5% free-space boundary, a wedged read lock (unreadable, and no repair on the
strength of a lock that was never answered), and recovery/block-log warnings. The judge is what
invites an operator to wipe state, so a false `corrupted` and a false `ok` are both defects and
both have a test.
