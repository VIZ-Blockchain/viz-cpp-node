# P2P Stats Reference

The DLT P2P layer emits two periodic log lines for monitoring:

| Log prefix | Default interval | Purpose |
|------------|-----------------|---------|
| `DLT Status \|` | ~30 s | Compact one-liner for tail/grep health monitoring |
| `=== DLT P2P Stats \|` | ~120 s (configurable via `dlt-stats-interval-sec`) | Full per-peer detail |

---

## Compact Status Line

```
DLT Status | FORWARD | head=#79881136 lib=#79881130 | dlt_range=79000000-79881136 | peers=6active/8conn | uptime=2h15m43s | flags=...
```

| Field | Example | Meaning |
|-------|---------|---------|
| Mode | `FORWARD` | Node operating mode (`SYNC` or `FORWARD`) |
| `head=#N` | `head=#79881136` | Current head block number |
| `lib=#N` | `lib=#79881130` | Last irreversible block number |
| `dlt_range=A-B` | `dlt_range=79000000-79881136` | Block range stored in the rolling DLT block log |
| `peers=Xactive/Yconn` | `peers=6active/8conn` | Exchange-enabled peers / total TCP connections |
| `uptime` | `2h15m43s` | Time since node startup |
| `flags` | various | Active flags (snapshot, paused, catchup, etc.) |

---

## Full Stats — Header Line

```
=== DLT P2P Stats | status=FWD fork=NORMAL head=79881136 lib=79881130 peers=6 conn=4 paused=no uptime=0h20m30s ===
```

### `status` — Node Operating Mode

| Value | Meaning |
|-------|---------|
| `SYNC` | Catching up — pulling blocks from peers; does not broadcast transactions |
| `FWD` | Caught up — producing and relaying blocks and transactions in real time |

**Why `SYNC`:** Node just started; fell behind during downtime; detected minority fork and is re-syncing; head stagnated for >30 seconds with a peer ahead.

**Why `FWD`:** Node has caught up to the network head; all blocks arrive via real-time broadcast.

### `fork` — Fork Status

| Value | Meaning |
|-------|---------|
| `NORMAL` | On the majority fork — no conflict |
| `LOOKING` | Competing tips detected; comparing branches (threshold: 42 blocks = 2 full rounds) |
| `MINORITY` | Confirmed on a minority fork; waiting to switch |

**Why not `NORMAL`:** Two validators produced at the same slot; network partition split validators across tips; an alternative-fork block arrived.

### `head` and `lib`

- **`head`** — block number of the current chain tip
- **`lib`** — Last Irreversible Block; blocks at or below this are finalized

The head-to-lib gap is typically 1–10 blocks in normal DLT operation.

### `peers` and `conn`

- **`peers`** — total peer entries in the peer table (active + connecting + disconnected, tracked for reconnection)
- **`conn`** — current live TCP connections

When `peers` significantly exceeds `conn`, the node has disconnected peers waiting in backoff queues.

### `paused`

| Value | Meaning |
|-------|---------|
| `no` | Block processing active |
| `YES` | Block intake temporarily halted (snapshot export in progress) |

While paused, P2P connections continue normally. Received blocks are queued in the fork DB and applied on resume. Stale-sync and forward-stagnation timers reset so no spurious mode transitions occur during the pause.

---

## Full Stats — Per-Peer Lines

### Active Peer

```
62.109.17.82:2001 | ACTIVE | exch=YES | head=79881136 lib=79880729 | range=79869724-79880729 | peer_fork=NORM peer_node=FWD | spam=0 | +align+emrg+sync
```

#### Lifecycle State

| Label | Meaning |
|-------|---------|
| `CONNECT` | TCP connect in progress (5 s timeout) |
| `HANDSHAKE` | Hello/hello-reply exchange in progress (10 s timeout) |
| `SYNCING` | Downloading a block range from this peer |
| `ACTIVE` | Fully operational; exchange established |
| `DISC` | Disconnected; will reconnect after backoff |
| `BANNED` | Soft-banned; no reconnect until ban expires |

#### `exch` — Exchange Status

| Value | Meaning |
|-------|---------|
| `YES` | Block and transaction exchange enabled; both sides share the same fork |
| `no` | Exchange disabled; fork alignment not confirmed |

**Why `no`:** Handshake just completed and fork alignment not yet verified; peer's head/LIB not in our fork DB; peer reported fork mismatch; SYNC → FORWARD transition not yet propagated.

**How exchange becomes `YES`:**
1. Hello handshake: acceptor calls `is_block_known(peer.head_block_id)` — on match, sets `exchange_enabled=true` in the hello reply.
2. Block acceptance: when a block from this peer applies to our chain, exchange is enabled.
3. FORWARD transition: peer broadcasts `dlt_fork_status_message` with `node_status=FORWARD`, triggering a fork-alignment re-check.

#### `head` / `lib` (peer values)

The peer's last reported head and LIB block numbers — a **snapshot** from the last hello, fork_status message, or block relay. The peer's actual chain may be ahead of these values, especially in FORWARD mode during rapid block production.

#### `range` — DLT Block Log Range

`earliest-latest`: block numbers available in the peer's rolling DLT block log.

If blocks needed for gap fill or initial sync are below `earliest`, this peer cannot serve them. The node searches for another peer whose range covers the missing blocks.

#### `peer_fork` — Peer's Self-Reported Fork Status

| Label | Meaning |
|-------|---------|
| `NORM` | Peer reports being on the majority fork |
| `LOOK` | Peer is resolving a fork conflict |
| `MINO` | Peer reports being on a minority fork |

Peers reporting `MINO` are likely in the process of switching forks and may soon change their head. A block from them should not be treated as canonical.

#### `peer_node` — Peer's Operating Mode

| Label | Meaning |
|-------|---------|
| `SYNC` | Peer is catching up; will not broadcast transactions |
| `FWD` | Peer is caught up and relaying real-time blocks |

#### `spam` — Anti-Spam Strike Counter

Strikes accumulated since the last valid packet. Soft-ban triggers at **10 strikes**. Resets on any valid packet, successful reconnection, or ban expiry.

**Strike triggers:** Deserialization failure; protocol violation (unexpected message in current state); dead-fork blocks (after the grace period).

**Note:** Duplicate blocks and out-of-order blocks within a range reply do **not** increment the counter.

#### Flags

| Flag | Meaning |
|------|---------|
| `+align` | Fork alignment verified — blocks from this peer apply cleanly to our chain |
| `+emrg` | Peer reports emergency consensus is active |
| `+ekey` | Peer holds the emergency committee private key (emergency master candidate) |
| `+sync` | Block range sync pending or in progress with this peer |

---

### Disconnected Peer

```
138.201.117.201:2001 | DISC | disconnected=74s | backoff=480s | reconnect_in=502s | spam=0
```

| Field | Meaning |
|-------|---------|
| `disconnected` | Seconds since the connection was lost |
| `backoff` | Current reconnect interval; doubles on each failure: 30 → 60 → 120 → … → 3600 s |
| `reconnect_in` | Seconds until the next reconnect attempt |
| `spam` | Residual strike count from the previous session |

Backoff resets to the initial value (30 s) when a connection remains stable for >5 minutes.

---

### Banned Peer

```
1.2.3.4:2001 | BANNED | ban_remaining=1800s | reason=spam strike threshold exceeded
```

| Field | Meaning |
|-------|---------|
| `ban_remaining` | Seconds until the ban expires (default ban: 3600 s) |
| `reason` | Human-readable ban reason sent in `dlt_soft_ban_message` |

After expiry the entry reverts to DISCONNECTED and normal backoff reconnection resumes.

---

## Scenario Interpretations

| Symptom | Likely cause | Action |
|---------|-------------|--------|
| All peers `exch=no` | Handshake just completed; fork mismatch; node in SYNC with unrecognized peers | Wait for FORWARD transition to trigger re-evaluation; check `fork` status |
| `status=SYNC` not advancing | Gap to peer's DLT range; no bridging peer available | Check `range` on peers; may need snapshot import |
| `peer_fork=MINO` on multiple peers | Network-wide fork split | Monitor; protocol converges automatically |
| High `backoff` on disconnected peers | Repeated connection failures; network instability | Check connectivity on port 2001; high backoff is expected and resets on success |
| `paused=YES` unexpectedly | Snapshot stuck or crashed during export | Check snapshot plugin logs |
| `fork=LOOKING` not resolving | Fork persists > 42 blocks without clear majority | Check validator connectivity; inspect chain on both tips |
| `spam` growing on one peer | Protocol mismatch; peer on incompatible fork | Will auto-ban at 10 strikes; check peer software version |
| Rapid SYNC ↔ FWD oscillation | No peer ahead; all peers at same head | `emergency_peer_reset()` fires after 60 s of isolation; also check P53 fix in logs |

---

## Protocol Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `SPAM_STRIKE_THRESHOLD` | 10 | Strikes before soft-ban |
| `BAN_DURATION_SEC` | 3600 | Default soft-ban duration (1 h) |
| `INITIAL_RECONNECT_BACKOFF_SEC` | 30 | First reconnect delay |
| `MAX_RECONNECT_BACKOFF_SEC` | 3600 | Maximum reconnect delay (1 h) |
| `STABLE_CONNECTION_RESET_SEC` | 300 | Connection duration before backoff resets (5 min) |
| `PEER_EXCHANGE_MAX_REQUESTS` | 3 | Max peer-exchange requests per sliding window |
| `PEER_EXCHANGE_WINDOW_SEC` | 300 | Peer-exchange rate-limit window (5 min) |
| `CONNECTING_TIMEOUT` | 5 s | TCP connect timeout |
| `HANDSHAKING_TIMEOUT` | 10 s | Hello exchange timeout |
| `PEER_REMOVAL_HOURS` | 8 h | Remove non-responding peer after this long |
| `ISOLATION_RESET_SEC` | 60 | Seconds with zero active peers before `emergency_peer_reset()` |
| `GAP_FILL_MAX_BLOCKS` | 100 | Max blocks per gap fill request |
| `GAP_FILL_COOLDOWN_SEC` | 5 | Minimum interval between gap fill requests |
| `GAP_FILL_TIMEOUT_SEC` | 15 | Gap fill in-progress flag timeout |
| `FORWARD_STAGNATION_SEC` | 30 | Head-not-advancing threshold in FORWARD mode |
| `FORWARD_BEHIND_GRACE_SEC` | 15 | Grace period after SYNC→FORWARD before `check_forward_behind()` acts |
| `SYNC_STAGNATION_SEC` | 30 | No-block-received threshold in SYNC mode |
| `FORK_RESOLUTION_BLOCK_THRESHOLD` | 42 | Blocks before fork resolution triggers (2 × CHAIN_MAX_VALIDATORS) |
| `FORK_RESOLUTION_CONFIRMATION_BLOCKS` | 6 | Consecutive blocks to confirm fork resolution |

---

See also: [P2P Overview](./overview.md), [Messages](./messages.md), [Sync Scenarios](./sync-scenarios.md).
