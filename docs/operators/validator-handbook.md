# Validator Operator Handbook

Fresh Debian VPS to a producing VIZ validator, and how to keep it running.

This page is the end-to-end procedure. For reference material it links out
rather than restating: [Configuration](../node/configuration.md) for every
option, [Validator Node](../node/validator-node.md) for the production loop and
log messages, [Building](../node/building.md) for source builds.

---

## Part 0 — Before you start

### Two node profiles

| Profile | Signs blocks | Public surface | Use |
|---|---|---|---|
| **Validator** | yes | none — RPC on loopback, p2p outbound only | producing blocks |
| **Relay / seed** | no | `:2001` inbound, optionally public RPC | serving the network, giving your own validators a reliable peer |

Keep them separate. A validator holds a signing key; putting it on the public
p2p surface buys nothing and costs you the key's isolation. Part 6 covers the
relay profile.

### Prerequisites

- A Debian 12 or 13 VPS with root or sudo access.
- A registered VIZ account. Obtaining one, and the stake to be a viable
  validator, is outside this page — see [Staking and DAO](../governance/staking-and-dao.md).
- A domain is *not* required. A validator needs no inbound DNS.

### Sizing

| | Validator | Relay / RPC |
|---|---|---|
| vCPU | 2 | 4 |
| RAM | 4 GB + 8 GB swap | 8 GB + swap |
| Disk | 40 GB SSD | 100 GB SSD |

The swap is not optional on a 4 GB box: chain state loading spikes well above
steady-state usage.

### Ports

| Port | Validator | Relay | Purpose |
|---|---|---|---|
| 2001 | outbound only | **inbound** | p2p |
| 8090 | loopback only | optional public | HTTP JSON-RPC |
| 8092 | outbound only | optional inbound | snapshot transfer |

---

## Part 1 — Host preparation

Everything here runs as root on a fresh box, in order. It ends on a gate you
must pass before deploying the node.

### A sudo user with an SSH key

From your **workstation**, if you do not already have a key:

```bash
ssh-keygen -t ed25519 -C "viz-validator"
```

On the **server**, as root:

```bash
adduser --gecos "" viz
usermod -aG sudo viz
install -d -m 700 -o viz -g viz /home/viz/.ssh
```

Then push your public key from the workstation:

```bash
ssh-copy-id -i ~/.ssh/id_ed25519.pub viz@YOUR_SERVER_IP
```

Confirm it works — `ssh viz@YOUR_SERVER_IP` must succeed **without a password
prompt** — before continuing.

### SSH hardening

```bash
sudo tee /etc/ssh/sshd_config.d/10-hardening.conf >/dev/null <<'EOF'
PermitRootLogin no
PasswordAuthentication no
KbdInteractiveAuthentication no
EOF
sudo sshd -t && sudo systemctl reload ssh
```

::: danger Keep your current session open
Do not close this shell. Open a **second** terminal, `ssh viz@YOUR_SERVER_IP`,
and confirm it logs in. Only then close the first one. If key auth is broken and
you have already logged out, you are locked out and recovering means the
provider's rescue console.
:::

### Firewall

Default-deny inbound. A validator needs no inbound port except SSH — p2p is
outbound-only, and the RPC is published on loopback by Docker.

```bash
sudo apt update && sudo apt install -y ufw
sudo ufw default deny incoming
sudo ufw default allow outgoing
sudo ufw allow OpenSSH
sudo ufw --force enable
```

Relay operators add `sudo ufw allow 2001/tcp` — see Part 6. Do not add it on a
validator.

### Time synchronization

**This is not optional.** Block production is slot-scheduled at 3-second
intervals. A clock that drifts by more than a fraction of a slot signs blocks
the network rejects, or misses its slot entirely, and the symptom looks exactly
like a network problem.

```bash
sudo apt install -y systemd-timesyncd
sudo timedatectl set-ntp true
timedatectl status
```

The node also runs its own NTP check and refuses to produce on excessive drift —
see the NTP section of [Validator Node](../node/validator-node.md). Host sync
and the node's check are complements, not alternatives.

### Swap

```bash
sudo fallocate -l 8G /swapfile
sudo chmod 600 /swapfile
sudo mkswap /swapfile
sudo swapon /swapfile
echo '/swapfile none swap sw 0 0' | sudo tee -a /etc/fstab
```

Persist a low swappiness so the kernel prefers reclaiming page cache over
swapping out live chain state:

```bash
echo 'vm.swappiness=10' | sudo tee /etc/sysctl.d/99-vizd.conf
sudo sysctl --system
```

### Docker

From Docker's official apt repository — the Debian-packaged `docker.io` lags and
ships no compose plugin.

```bash
sudo apt install -y ca-certificates curl
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/debian/gpg \
  -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] \
https://download.docker.com/linux/debian $(. /etc/os-release && echo "$VERSION_CODENAME") stable" \
  | sudo tee /etc/apt/sources.list.d/docker.list >/dev/null
sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io \
  docker-buildx-plugin docker-compose-plugin
sudo usermod -aG docker viz
```

Log out and back in for the group change to take effect.

**Gate — do not continue until all three pass:**

```bash
timedatectl | grep 'System clock synchronized: yes'
docker run --rm hello-world
sudo ufw status | grep 'Status: active'
```

---

## Part 2 — Deploy the node

### Generate a signing key

```bash
docker run --rm -it vizblockchain/vizd:latest cli_wallet --suggest-brain-key
```

Record the WIF private key and the `VIZ...` public key. The private key goes in
`config.ini` below; the public key gets registered on-chain in Part 3.

Your account's **active key never touches this machine.** It stays in your
browser for the Part 3 cutover. The signing key is a separate, disposable
identity — if it leaks you rotate it (Part 5) and lose nothing else.

### Layout

```bash
sudo install -d -o viz -g viz /opt/vizd/logs
cd /opt/vizd
```

Three things live here: `compose.yml`, `config.ini`, and `logs/`. Chain state
does **not** — it lives in a Docker named volume, which is the whole point of
Part 5's decision table.

### `compose.yml`

```yaml
name: viz-validator

services:
  vizd:
    image: vizblockchain/vizd:latest
    container_name: viz-validator
    restart: unless-stopped
    ports:
      - "127.0.0.1:8090:8090"   # loopback only — health checks, never public
    volumes:
      - vizd-state:/var/lib/vizd
      - ./config.ini:/etc/vizd/config.ini:ro
      - ./logs:/var/log/vizd
    logging:
      driver: json-file
      options:
        max-size: "10m"
        max-file: "3"

volumes:
  vizd-state:
```

Three things about this file are deliberate:

- **The `:ro` config mount is correct.** The entry script copies
  `/etc/vizd/config.ini` into the data directory before use, so the container
  never writes the mounted file.
- **Chain state lives in the named `vizd-state` volume.** `docker compose down`
  keeps it; `docker compose down -v` destroys it. That one flag is the
  difference between a restart and a full re-bootstrap.
- **Logs go to the Docker `json-file` driver**, capped so they cannot fill the
  disk — read them with `docker compose logs`. The `./logs` mount is there for a
  file appender if you add one; with the stderr-only logging below it stays
  empty.

### `config.ini`

```ini
# ─── p2p (outbound only; nothing listens publicly) ───────────────────
p2p-seed-node = seed3.viz.world:2001
p2p-seed-node = seed1.viz.world:2001
p2p-seed-node = rpc.viz.cx:2001

# ─── RPC — published on loopback by compose, for health checks ───────
webserver-http-endpoint = 0.0.0.0:8090
webserver-thread-pool-size = 1
single-write-thread = true
enable-plugins-on-push-transaction = false

# ─── shared memory ───────────────────────────────────────────────────
shared-file-size = 2G
min-free-shared-file-size = 500M
inc-shared-file-size = 2G

# ─── plugins: producer + health RPC only ─────────────────────────────
plugin = chain p2p json_rpc webserver database_api validator validator_api
plugin = snapshot

# ─── validator identity ──────────────────────────────────────────────
validator = "YOUR_ACCOUNT"
private-key = YOUR_SIGNING_WIF

# ─── snapshot: bootstrap from a trusted peer, never serve ────────────
snapshot-dir = /var/lib/vizd/snapshots
sync-snapshot-from-trusted-peer = true
trusted-snapshot-peer = seed3.viz.world:8092
trusted-snapshot-peer = seed1.viz.world:8092
trusted-snapshot-peer = seed2.viz.world:8092
trusted-snapshot-peer = rpc.viz.cx:8092
allow-snapshot-serving = false
snapshot-every-n-blocks = 1200
snapshot-max-age-days = 10
dlt-block-log-max-blocks = 100000

# ─── production safety ───────────────────────────────────────────────
required-participation = 33
enable-stale-production = false
skip-virtual-ops = true
clear-votes-before-block = 0

# ─── logging ─────────────────────────────────────────────────────────
# fc is strict here: no inline comments inside [log.*]/[logger.*] sections,
# and level must be one of all/debug/info/warn/error/off. "none" is invalid
# and silently falls back to default stderr logging.
[log.console_appender.stderr]
stream=std_error
[logger.default]
level=info
appenders=stderr
```

The two placeholders:

- `validator = "YOUR_ACCOUNT"` — your account name, **in quotes**.
- `private-key = YOUR_SIGNING_WIF` — the bare WIF from the keypair above, **no
  quotes**.

The option is `validator`. `witness` still works as a deprecated alias but logs
`Config option 'witness' is deprecated, use 'validator' instead.` — if you see
that warning, you copied an old snippet.

`seed2.viz.world` appears in the snapshot list but **not** in the p2p list. That
asymmetry is deliberate: its `:8092` serves snapshots, its `:2001` does not
accept peers. Do not "fix" it.

### Start

The file holds your signing key, so lock it down before the node ever runs:

```bash
chmod 600 config.ini
docker compose up -d
docker compose logs -f
```

On a first boot with no state you will see:

```
Node has no state. Triggering P2P snapshot sync from trusted peers...
```

Bootstrap from a snapshot takes **minutes, not hours**. If you instead see the
node replaying the block log from genesis, it did not reach a trusted snapshot
peer — check the `trusted-snapshot-peer` list and outbound `:8092`.

**Gate:**

```bash
# Head must climb between two calls ~7s apart
curl -s http://127.0.0.1:8090 -H 'Content-Type: application/json' \
  --data '{"id":1,"jsonrpc":"2.0","method":"call","params":["database_api","get_dynamic_global_properties",[]]}' \
  | grep -o '"head_block_number":[0-9]*'

# Nothing may be listening publicly
ss -tlnp | grep 8090   # must show 127.0.0.1:8090 and nothing else
```

---

## Part 3 — The on-chain cutover

::: warning Stop
Do not start this part until Part 2's gate passes **and** the node has caught up
to the network tip. Registering a signing key on a node that is still syncing
schedules you for slots you cannot fill.
:::

### 1. Set the signing key

Go to `https://wallet.viz.world/dao/witness-params/` and log in with your
account's **active key, in the browser**. Set the signing key to the `VIZ...`
public key you generated in Part 2.

::: warning Set the key first, then get votes
An un-keyed validator reports `running_version 0.0.0` and does not appear in the
web wallet's DAO validator list at all — so it cannot be voted for. Setting the
signing key is what makes it visible. Doing these in the other order looks like
the wallet is broken.
:::

### 2. Obtain votes

A validator with zero votes is registered but **never scheduled** — its
`virtual_scheduled_time` is max-uint, so the scheduler never reaches it. It will
sit at the tip indefinitely, healthy and idle, producing nothing.

Votes come from the `account_validator_vote` operation and are weighted by the
voter's vesting stake, so a handful of large holders outweighs a crowd of small
ones. See [Staking and DAO](../governance/staking-and-dao.md).

### 3. Verify production

```bash
curl -s http://127.0.0.1:8090 -H 'Content-Type: application/json' \
  --data '{"id":1,"jsonrpc":"2.0","method":"call","params":["validator_api","get_validator_by_account",["YOUR_ACCOUNT"]]}'
```

Check that `signing_key` matches your public key and that
`last_confirmed_block_num` advances between calls.

::: warning Once voted and producing, the node must stay up
Missing enough consecutive scheduled blocks causes VIZ to auto-deactivate the
validator by null-keying it. Recovering means re-registering the key and
regaining schedule position — planned maintenance on a producing validator is a
short restart, not an afternoon.
:::

::: tip total_missed is a lifetime counter
It never resets. Record your baseline **now** and alert on the delta. Watching
the absolute number tells you nothing except that the node has existed.
:::

To disable or roll back a validator, set its signing key to the null key:

```
VIZ1111111111111111111111111111111114T1Anm
```

That is the same mechanism the chain uses to auto-deactivate you, so it is a
clean stop rather than a hack.

::: tip cli_wallet TLS
As of 2026-07-27, `cli_wallet` in the `vizd` image failed every `wss` handshake,
refused plain `http`, and spoke only plain `ws` — which public nodes do not
expose. If you hit this, broadcast through the web wallet or a client library
instead. Re-test before assuming it still applies.
:::

**Gate:** `signing_key` equals your public key, and `last_confirmed_block_num`
advances across two calls a minute apart.

---

## Part 4 — Health monitoring

Three things can go wrong quietly: the container dies, the head stops advancing,
or the node keeps up but misses its own slots. One check covers all three, and
alerts only on failure — a monitor that pings you on success gets muted, and a
muted monitor is not a monitor.

```bash
#!/usr/bin/env bash
# /opt/vizd/health.sh — cron'd every 15 minutes.
# Checks container is up, head is advancing, and missed blocks are not growing.
# Alerts only on failure; no success spam.
set -euo pipefail

DIR=/opt/vizd
RPC=http://127.0.0.1:8090
STATE="$DIR/health.state"

source "$DIR/.env"   # ALERT_TOKEN, ALERT_CHAT, WITNESS_ACCOUNT

alert() {
    curl -s --max-time 10 "https://api.telegram.org/bot${ALERT_TOKEN}/sendMessage" \
        -d chat_id="${ALERT_CHAT}" \
        --data-urlencode text="viz-validator ($(hostname)): $1" >/dev/null || true
}

if ! docker ps --format '{{.Names}}' | grep -q '^viz-validator$'; then
    alert "container not running"; exit 1
fi

rpc() { curl -s --max-time 10 "$RPC" -H 'Content-Type: application/json' --data "$1"; }
DGP='{"id":1,"jsonrpc":"2.0","method":"call","params":["database_api","get_dynamic_global_properties",[]]}'

head1=$(rpc "$DGP" | grep -o '"head_block_number":[0-9]*' | grep -o '[0-9]*$' || true)
sleep 9   # three block intervals
head2=$(rpc "$DGP" | grep -o '"head_block_number":[0-9]*' | grep -o '[0-9]*$' || true)

if [ -z "$head1" ] || [ -z "$head2" ]; then
    alert "RPC not answering on 127.0.0.1:8090"; exit 1
fi
if [ "$head2" -le "$head1" ]; then
    alert "head frozen at #$head2 (was #$head1 nine seconds earlier)"; exit 1
fi

missed=$(rpc "{\"id\":2,\"jsonrpc\":\"2.0\",\"method\":\"call\",\"params\":[\"validator_api\",\"get_validator_by_account\",[\"$WITNESS_ACCOUNT\"]]}" \
    | grep -o '"total_missed":[0-9]*' | grep -o '[0-9]*$' || true)
if [ -n "$missed" ]; then
    prev=$(cat "$STATE" 2>/dev/null || echo "$missed")
    [ "$missed" -gt "$prev" ] && alert "missed blocks growing: $prev -> $missed"
    echo "$missed" > "$STATE"
fi
```

The parsing is `curl` plus `grep` on purpose. No `jq`, so nothing extra needs
installing on a minimal box and the check has no dependency that can go stale.

Credentials go in a mode-600 `.env` beside it:

```bash
cat > /opt/vizd/.env <<'EOF'
ALERT_TOKEN=123456:your-telegram-bot-token
ALERT_CHAT=your-telegram-chat-id
WITNESS_ACCOUNT=your-account
EOF
chmod 600 /opt/vizd/.env
chmod 700 /opt/vizd/health.sh
```

And the cron entry (`crontab -e` as the `viz` user):

```cron
*/15 * * * * /opt/vizd/health.sh >> /opt/vizd/health.log 2>&1
```

For what the node itself logs on each production attempt — produced, missed,
minority fork, watchdog — see
[Validator Node](../node/validator-node.md), which tabulates every result rather
than repeating it here. For metrics and deeper alerting, see
[Monitoring](../node/monitoring.md).

**Gate — test the alarm, do not assume it:**

```bash
docker compose stop vizd     # then wait for the next cron tick
# an alert must arrive within 15 minutes
docker compose start vizd
```

An untested alarm is not an alarm. Do this once now, while missing a few slots
costs nothing, rather than discovering the token was wrong during an incident.

---

## Part 5 — Day-two operations

Start here. This table is the choice operators get wrong, and the wrong choice
costs slots that a `restart` would not have.

| Situation | Command | Cost |
|---|---|---|
| config-only change | `docker compose restart vizd` | ~1 slot |
| new image | `docker compose pull` **then** `docker compose up -d` | gap replay, near-zero downtime |
| wedged or on a bad fork | `docker compose down -v && docker compose up -d` | full re-bootstrap; misses slots until resynced |

### Image upgrades

`vizblockchain/vizd:latest` is rebuilt on every push to master, so there is no
version tag to bump — the tag itself moves.

The trap: **`docker compose up -d` does not pull a newer `:latest` when the tag
already exists locally.** It sees a matching tag, reuses the local image, and
reports success. You must `docker compose pull` first, or you have restarted the
same binary and concluded the upgrade did nothing.

```bash
cd /opt/vizd
docker compose pull
docker compose up -d
docker compose logs -f
```

Upgrade **one box at a time**. Between boxes, confirm head is advancing and
`total_missed` has not grown. If a pull ships a regression, the previous image is
still on disk — `docker images vizblockchain/vizd` lists it by id, and you can
pin that id in `compose.yml` to roll back.

### A recreate does not clear a wedge

Chain state lives in the `vizd-state` volume, and compose reattaches that volume
across `up -d`, `restart`, and even `--force-recreate`. A fresh container
therefore reloads the *same* bad state and wedges again, which reads as "the
restart didn't help" when in fact nothing was reset.

Only `docker compose down -v` drops the volume. That is the entire difference,
and it is why the flag is worth memorising rather than looking up mid-incident.

### Dead-fork recovery, and its ordering trap

Symptoms: head frozen while peers are connected, or a canonical peer logging
that it soft-banned you for a dead fork.

**Configure a canonical snapshot source before you wipe.** If you `down -v`
first, the node re-syncs from whatever peers it has — which, on a dead fork, may
be the same peers that fed you the dead fork. You pay the full re-bootstrap and
arrive back where you started. Order:

1. Confirm `config.ini`'s `trusted-snapshot-peer` list leads with
   `seed3.viz.world:8092` and `sync-snapshot-from-trusted-peer = true`.
2. Confirm the `p2p-seed-node` list leads with `seed3.viz.world:2001`. Peers are
   tried in order, so the canonical tip must be first.
3. `docker compose down -v && docker compose up -d`.
4. Watch for `Node has no state. Triggering P2P snapshot sync from trusted
   peers...` and confirm the head lands near the network tip, not at the frozen
   height.

### State refused after an upgrade

If the node exits at startup complaining the state was built with a different
compiler, build, or Boost version, that is not corruption — it is a deliberate
refusal. See [Boost 1.9x Upgrade](./boost-1.9x-upgrade.md).

### Key rotation

1. Generate a new keypair exactly as in Part 2.
2. Update `private-key` in `config.ini`.
3. `docker compose restart vizd`.
4. Re-register the new **public** key on-chain, exactly as in Part 3.

The on-chain registration is the cutover fence. Until it lands, the old key
produces valid blocks; the moment it lands, only the new one does. There is no
window where both are valid, so there is no double-production risk — but there
*is* a window between steps 3 and 4 where the node holds a key the chain has not
accepted yet and will report `no_private_key`. Keep it short.

### Box loss

There is nothing to restore. Your identity is the signing key plus its on-chain
registration, and both live off the box — the key in your password manager, the
registration on the chain. Re-run Parts 1–2 on new hardware and the node
re-bootstraps from a snapshot in minutes. Do not back up chain state; it is
worthless compared to what the network will hand you.

### Running more than one validator

One signing key per box. **Never the same WIF on two machines** — that is double
production: two nodes signing different blocks for the same slot, which is
exactly what the network is built to punish. Separate accounts with separate keys
are fully independent and perfectly fine.

---

## Part 6 — The keyless relay / seed profile

VIZ is short on public seed nodes. A relay costs you nothing in signing risk and
gives your own validators a reliable peer on infrastructure you control, while
the validators themselves stay inbound-closed. If you run one validator, running
one relay is the highest-value second box you can add.

The relay is the Part 2 deployment with these differences:

- **No `validator` and no `private-key`.** It signs nothing, so there is no key
  on the box to protect.
- **`p2p-endpoint = 0.0.0.0:2001`**, plus `sudo ufw allow 2001/tcp`. This is the
  one place inbound p2p is correct.
- **`allow-snapshot-serving = true`** if you want to serve snapshots to other
  operators; add `sudo ufw allow 8092/tcp` with it.
- **Add `network_broadcast_api` and the history plugins only if you serve API
  clients.** They cost memory and disk; a pure relay does not need them.
- **Public RPC, if any, belongs behind a reverse proxy with TLS.** Never publish
  `:8090` directly.

::: warning Do not co-locate a relay with your only validator
One box failure then takes out both your producer and the peer it depends on.
A relay feeding a validator on the same host was implicated in a fork incident.
If you run both, run them on separate machines.
:::

---

## Part 7 — Symptom table

Host and deployment symptoms. For symptoms visible in the node's own production
log — `no_private_key`, `low_participation`, `minority_fork` and friends — see
[Validator Node](../node/validator-node.md), which tabulates every result.

| Symptom | First command | Usual cause |
|---|---|---|
| Container restart-looping | `docker compose logs --tail=100 vizd` | malformed `config.ini` — an inline comment inside a `[log.*]` section, or an invalid log level |
| RPC not answering on loopback | `docker compose ps` then `ss -tlnp \| grep 8090` | container down, or `ports:` missing the `127.0.0.1:` prefix |
| Head frozen, peers connected | `docker compose logs --tail=200 vizd \| grep -i fork` | wedged state or a dead fork — Part 5, and note that a plain restart will not clear it |
| Head frozen, zero peers | `docker compose logs vizd \| grep -i 'p2p\|peer'` | every `p2p-seed-node` unreachable, or outbound `:2001` blocked — test with `nc -z seed3.viz.world 2001` |
| `total_missed` climbing, head advancing | `timedatectl` | clock drift, or the box is too slow to sign within its slot — check load and swap pressure |
| Nothing produced after a correct-looking cutover | `get_validator_by_account` — compare `signing_key`, read `virtual_scheduled_time` | zero votes, so never scheduled (Part 3), or the registered key does not match `config.ini` |
| State refused on startup after an upgrade | `docker compose logs vizd \| head -40` | Boost/compiler state-version mismatch — [Boost 1.9x Upgrade](./boost-1.9x-upgrade.md) |
| Disk full | `df -h && docker system df` | old images and dangling volumes — `docker image prune -a`; check snapshot retention (`snapshot-max-age-days`) and that `logging.options.max-size` is set |
