# DLT P2P Statistics Reference

This document explains the DLT P2P statistics output — what each field means, why it has its current value, and what actions (if any) an operator should take.

---

## Node-Level Summary (Header Line)

```
=== DLT P2P Stats | status=FWD fork=NORMAL head=79881136 lib=79881130 peers=6 conn=4 paused=no uptime=0h20m30s ===
```

### `status` — Node Operating Mode

| Value | Meaning |
|-------|---------|
| `SYNC` | Node is catching up — downloading and applying blocks from peers. Transactions are not broadcast during this mode. |
| `FWD` | Node is caught up and in forward mode — producing/relaying blocks and transactions in real-time. |

**Why it might be `SYNC`:**
- Node just started and is downloading the blockchain
- Node fell behind the network (missed blocks during downtime or connection loss)
- Node detected it is on a minority fork and is re-syncing from the majority chain
- Block processing stagnation — head hasn't advanced for a configured period

**Why it might be `FWD`:**
- Node has caught up to the network and is operating normally
- All blocks are being received via real-time broadcast from peers

### `fork` — Fork Status

| Value | Meaning |
|-------|---------|
| `NORMAL` | Node is on the majority fork — no fork conflict detected. |
| `LOOKING` | Node detected multiple competing fork tips and is actively comparing branches to determine which is the majority. |
| `MINORITY` | Node determined it is on a minority fork (fewer witnesses producing on this branch). A fork switch is likely pending. |

**Why it might not be `NORMAL`:**
- Two or more witnesses produced blocks at the same slot, creating a temporary fork
- Network partition caused different subsets of witnesses to build on different tips
- Node just received a block from an alternative fork that doesn't link to its current head

### `head` / `lib`

- **head** — Block number of the node's current head (latest block in the chain).
- **lib** — Last Irreversible Block number. Blocks at or below LIB are finalized and cannot be reverted.

The gap between head and lib indicates how many blocks are on the reversible part of the chain. In normal operation with DLT, this gap is small (typically 1-10 blocks).

### `peers` / `conn`

- **peers** — Total number of peer entries known to the node (includes active, connecting, and disconnected peers tracked for reconnection).
- **conn** — Number of active TCP connections currently open.

If `peers` is significantly higher than `conn`, the node has disconnected peers it is waiting to reconnect to (with exponential backoff).

### `paused`

| Value | Meaning |
|-------|---------|
| `no` | Block processing is active — incoming blocks are being applied immediately. |
| `YES` | Block processing is temporarily paused. Incoming blocks are queued and will be processed when resumed. |

**Why it might be `YES`:**
- A snapshot is being created — the database is locked for consistent state export
- An internal operation requires exclusive database access

When paused, the node continues to accept P2P connections and receive messages, but blocks are not applied to the chain until processing resumes.

### `uptime`

Time since the P2P node was started. Format: `XhYmZs`.

---

## Per-Peer Statistics

Each peer is shown on a separate line with detailed state information.

### Disconnected Peers

```
138.201.117.201:2001 | DISC | disconnected=74s | backoff=480s | reconnect_in=502s | spam=0
```

| Field | Meaning |
|-------|---------|
| `DISC` | Peer is currently disconnected. The node will attempt to reconnect. |
| `disconnected` | Seconds since the connection was lost. |
| `backoff` | Current reconnection backoff interval in seconds. Starts at 30s and doubles on each failed attempt, up to 3600s (1 hour). |
| `reconnect_in` | Seconds until the next reconnection attempt. |
| `spam` | Spam strike counter. Reset on successful reconnection. |

**Backoff behavior:**
- First disconnect: 30s backoff
- Each subsequent failed reconnect doubles the backoff (30 → 60 → 120 → 240 → 480 → ... → 3600)
- If a connection stays stable for more than 5 minutes, the backoff resets to 30s
- Maximum backoff is 3600s (1 hour)

### Active Peers

```
62.109.17.82:2001 | ACTIVE | exch=YES | head=79881136 lib=79880729 | range=79869724-79880729 | peer_fork=NORM peer_node=FWD | spam=0 | +align+emrg+sync
```

#### Lifecycle State

| State | Meaning |
|-------|---------|
| `CONNECT` | TCP connection is being established (timeout: 5s). |
| `HANDSHAKE` | Hello/hello-reply exchange is in progress (timeout: 10s). |
| `SYNCING` | Actively downloading a block range from this peer. |
| `ACTIVE` | Handshake complete, exchange is established, peer is operational. |
| `DISC` | Disconnected — see disconnected peer format above. |
| `BANNED` | Temporarily banned — peer will not be contacted until ban expires (default: 1 hour). |

#### `exch` — Exchange Status

| Value | Meaning |
|-------|---------|
| `YES` | Block and transaction exchange is enabled with this peer. Both sides recognize each other as being on the same fork. |
| `no` | Exchange is disabled. The node does not know if this peer is on the same fork, so it does not send or request blocks. |

**Why exchange might be `no`:**
- Handshake just completed and fork alignment has not been verified yet
- Peer's head/LIB is not recognized by our node (different forks)
- Peer explicitly reported that our chain is not on its fork during handshake
- Peer sent a block from an unrecognized fork

**How exchange becomes `YES`:**
- During handshake: if our node recognizes the peer's head block, or the peer recognizes our head block
- After accepting a block from the peer: if the block applies to our chain, exchange is automatically enabled
- When a peer transitions from SYNC to FWD and its head becomes recognized

#### `head` / `lib` (peer)

The peer's reported head and LIB block numbers. **These are snapshots** from the last communication (hello, fork_status message, or block relay), not real-time values. The peer's actual chain head may be significantly higher, especially if the peer is in FWD mode and producing blocks rapidly.

#### `range` — DLT Block Range

The block range this peer has available in its DLT block log: `earliest-latest`.

- **earliest** — Oldest block number still retained in the peer's DLT log
- **latest** — Newest block number in the peer's DLT log

If our node needs blocks below the peer's `earliest`, this peer cannot serve them. This is relevant during gap fill operations when historical blocks are needed.

#### `peer_fork` — Peer's Fork Status

What the peer reports about its own fork situation:

| Value | Meaning |
|-------|---------|
| `NORM` | Peer believes it is on the majority fork. |
| `LOOK` | Peer is actively resolving a fork conflict. |
| `MINO` | Peer believes it is on a minority fork. |

This is self-reported by the peer via fork_status messages. If a peer reports `MINO`, it is likely in the process of switching to the majority fork and may soon have a different head.

#### `peer_node` — Peer's Operating Mode

| Value | Meaning |
|-------|---------|
| `SYNC` | Peer is catching up — downloading blocks. It will not broadcast transactions. |
| `FWD` | Peer is caught up — operating in forward mode, producing and relaying blocks. |

**Why this matters:**
- If our node is in FWD mode and a peer is in SYNC, we do not expect real-time block broadcasts from that peer
- If our node is in SYNC mode, we request blocks from all exchange-enabled peers regardless of their mode
- If a peer transitions from SYNC to FWD, it may re-evaluate exchange and start sending us blocks

#### `spam` — Anti-Spam Strike Counter

Number of spam strikes accumulated by this peer. Each invalid or malformed message increments this counter. When it reaches 10, the peer is soft-banned for 1 hour.

**What triggers a spam strike:**
- Malformed messages that fail deserialization
- Messages that violate protocol rules (e.g., out-of-order blocks outside of expected sync)
- Repeated invalid data from the peer

**How it resets:**
- On receiving a valid packet, the counter resets to 0
- On successful reconnection after a disconnect
- When a ban is lifted

#### Flags

| Flag | Meaning |
|------|---------|
| `+align` | Fork alignment verified — this peer's chain links to ours. Blocks from this peer apply cleanly to our chain. |
| `+emrg` | Peer has emergency consensus active — it is producing blocks using the emergency key mechanism. |
| `+ekey` | Peer possesses an emergency private key — it can participate in emergency consensus if needed. |
| `+sync` | A block range sync is currently pending or in progress with this peer. The node has requested blocks and is waiting for the response. |

**Why flags matter:**
- `+align` is the most important — it confirms the peer is a valid source for blocks
- `+emrg` + `+ekey` together indicate the peer is an emergency witness participant
- `+sync` indicates active synchronization — the peer is being used as a block source

### Banned Peers

```
1.2.3.4:2001 | BANNED | ban_remaining=1800s | reason=spam strike threshold exceeded
```

| Field | Meaning |
|-------|---------|
| `BANNED` | Peer is temporarily banned. No connection attempts will be made. |
| `ban_remaining` | Seconds remaining until the ban expires. Default ban duration is 3600s (1 hour). |
| `reason` | Why the peer was banned. Common reasons: spam threshold exceeded, protocol violation. |

After the ban expires, the peer state resets to DISCONNECTED and normal reconnection backoff begins.

---

## Common Scenarios and Interpretations

### Scenario 1: All Peers Show `exch=no`

**What it means:** The node does not recognize any peer's chain as being on the same fork.

**Likely causes:**
- Node just started and hasn't completed handshake with any peer
- Node is on a different fork than all connected peers
- Peers connected while the node was in SYNC mode and their heads were not yet recognized

**What to do:**
- Wait for handshake to complete — exchange may enable automatically
- Check the `fork` status in the header — if it says `MINORITY` or `LOOKING`, the node is resolving a fork conflict
- If the node stays in this state for a long time, it may need to re-sync from a different peer set

### Scenario 2: `status=SYNC` with Many Active Peers

**What it means:** The node is actively downloading blocks from multiple peers.

**Likely causes:**
- Node startup after being offline
- Node fell behind the network
- Node is re-syncing after a fork switch

**What to do:**
- Normal behavior — wait for sync to complete
- Check `head` progression in successive stats outputs to confirm blocks are being applied
- If `head` does not advance for an extended period, check for block validation errors in logs

### Scenario 3: `peer_fork=MINO` on Multiple Peers

**What it means:** Multiple peers believe they are on a minority fork.

**Likely causes:**
- Network-wide fork event — witnesses are split between two competing chains
- The majority fork is being built by a different set of witnesses

**What to do:**
- Monitor the `fork` status in the header — if it transitions to `MINORITY`, our node will also switch forks
- Wait for fork resolution — the protocol will eventually converge on a single chain
- If this persists, check witness activity and network connectivity

### Scenario 4: High `backoff` Values on Disconnected Peers

**What it means:** Peers have disconnected multiple times and the reconnection interval has grown.

**Likely causes:**
- Network instability between our node and the peers
- Peers are restarting or experiencing downtime
- Firewall or NAT issues blocking persistent connections

**What to do:**
- Check network connectivity to the peer addresses
- Verify firewall rules allow outbound connections on port 2001
- If peers are known to be online, high backoff is normal and will reset on successful connection

### Scenario 5: `paused=YES`

**What it means:** Block processing is temporarily suspended.

**Likely causes:**
- Snapshot creation in progress — database is locked for consistent state export
- Internal maintenance operation requiring exclusive database access

**What to do:**
- Normal during snapshot operations — wait for completion
- Blocks received during pause are queued and will be processed when resumed
- If pause persists unexpectedly, check for stuck snapshot operations

### Scenario 6: Peer Exchange Rate-Limiting in Logs

**Log message:** `Peer <ep> rate-limited our exchange request, wait <w>s`

**What it means:** Our node sent a `dlt_peer_exchange_request` to a peer, but that peer responded with a `dlt_peer_exchange_rate_limited` message because the remote peer has already served 3 exchange requests from us within the last 5-minute window.

**Mechanism:**
- Each peer tracks `peer_exchange_request_count` and `peer_exchange_window_start` per remote peer.
- When a `dlt_peer_exchange_request` arrives, the receiving peer checks `is_peer_exchange_rate_limited()` — if 3 or more requests were received within the 5-minute window (`PEER_EXCHANGE_WINDOW_SEC`), it responds with `dlt_peer_exchange_rate_limited{wait_seconds}` instead of a peer list.
- On our side, receiving this response marks us as rate-limited locally (`peer_exchange_request_count = MAX`), which prevents `periodic_peer_exchange()` from selecting this peer for future requests until the window expires.
- This is a two-sided mechanism: both nodes independently enforce the same sliding window (3 requests per 5 minutes).

**Why this exists:**
- Prevents excessive peer discovery traffic on stable networks
- Reduces unnecessary message overhead when peer topology is not changing
- Complements the `dlt-peer-exchange-min-uptime-sec` (600s) filter which ensures only stable peers are shared

**What to do:**
- This is normal, expected behavior — no action required
- The message appears at most after the 4th request within a 5-minute window per peer
- If this message appears very frequently for many peers, it may indicate that the periodic exchange interval is configured too aggressively or the network has very few unique peers

---

## Quick Reference

### Enumerations

**Node Status:**
- `DLT_NODE_STATUS_SYNC` (0) = Catching up
- `DLT_NODE_STATUS_FORWARD` (1) = Caught up, real-time operation

**Fork Status:**
- `DLT_FORK_STATUS_NORMAL` (0) = On majority fork
- `DLT_FORK_STATUS_LOOKING_RESOLUTION` (1) = Resolving fork conflict
- `DLT_FORK_STATUS_MINORITY` (2) = On minority fork

**Peer Lifecycle States:**
- `DLT_PEER_LIFECYCLE_CONNECTING` (0) = Establishing TCP connection
- `DLT_PEER_LIFECYCLE_HANDSHAKING` (1) = Exchanging hello messages
- `DLT_PEER_LIFECYCLE_SYNCING` (2) = Downloading block range
- `DLT_PEER_LIFECYCLE_ACTIVE` (3) = Operational, exchange established
- `DLT_PEER_LIFECYCLE_DISCONNECTED` (4) = Disconnected, will reconnect
- `DLT_PEER_LIFECYCLE_BANNED` (5) = Temporarily banned

### Thresholds and Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `SPAM_STRIKE_THRESHOLD` | 10 | Spam strikes before soft-ban |
| `BAN_DURATION_SEC` | 3600 | Default ban duration (1 hour) |
| `INITIAL_RECONNECT_BACKOFF_SEC` | 30 | Initial reconnection delay |
| `MAX_RECONNECT_BACKOFF_SEC` | 3600 | Maximum reconnection delay (1 hour) |
| `PEER_EXCHANGE_MAX_REQUESTS` | 3 | Maximum peer exchange requests allowed per peer within the sliding window |
| `PEER_EXCHANGE_WINDOW_SEC` | 300 | Peer exchange sliding window duration (5 min) — both requesting and serving sides enforce this; a rate-limited response carries `wait_seconds` indicating remaining window time |
| `SEND_QUEUE_MAX_DEPTH` | 100 | Maximum queued messages per peer |
| `KNOWN_BLOCKS_WINDOW` | 20 | Block ID ring buffer size for echo suppression |
| `CONNECTING_TIMEOUT` | 5s | Timeout for TCP connection establishment |
| `HANDSHAKING_TIMEOUT` | 10s | Timeout for hello/hello-reply exchange |
