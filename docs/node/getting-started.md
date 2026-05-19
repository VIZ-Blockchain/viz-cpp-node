# Getting Started

This guide covers everything needed to run a VIZ Ledger node — from installing dependencies to initial synchronization.

---

## Prerequisites

| Requirement | Minimum | Recommended |
|-------------|---------|-------------|
| OS | Ubuntu 20.04 LTS | Ubuntu 24.04 LTS |
| RAM | 4 GB | 8 GB+ |
| Disk | 20 GB | 50 GB+ SSD |
| CPU | 2 cores | 4+ cores |
| Network | Public IP, open port 2001 | Stable connection |

**Ports used:**

| Port | Protocol | Purpose |
|------|----------|---------|
| 2001 | TCP | P2P peer connections |
| 8090 | TCP | HTTP JSON-RPC |
| 8091 | TCP | WebSocket JSON-RPC |

---

## Option A: Docker (Recommended for Quick Start)

### 1. Pull the production image

```bash
docker pull vizblockchain/vizd:latest
```

### 2. Run the node

```bash
docker run -d \
  --name vizd \
  -p 2001:2001 \
  -p 8090:8090 \
  -p 8091:8091 \
  -v /data/vizd:/var/lib/vizd \
  vizblockchain/vizd:latest
```

### 3. Follow logs

```bash
docker logs -f vizd
```

You should see peer connections and block sync progress within a few minutes.

### Environment variables (Docker)

| Variable | Purpose | Example |
|----------|---------|---------|
| `VIZD_SEED_NODES` | Override default seed nodes | `node1.viz.media:2001` |
| `VIZD_WITNESS` | Validator name (if validator node) | `alice` |
| `VIZD_PRIVATE_KEY` | Validator signing key (WIF) | `5J...` |

---

## Option B: Build from Source

### 1. Install dependencies (Linux)

```bash
git clone --recursive https://github.com/VIZ-Blockchain/viz-cpp-node
cd viz-cpp-node
chmod +x install-deps-linux.sh
sudo ./install-deps-linux.sh
```

### 2. Build

```bash
chmod +x build-linux.sh
./build-linux.sh
```

For a low-memory build (validators and seed nodes — excludes indexing plugins):

```bash
./build-linux.sh -l
```

The binary is placed at `build/programs/vizd/vizd`.

### 3. macOS

```bash
chmod +x build-mac.sh
./build-mac.sh
```

### 4. Windows (MinGW)

```cmd
set BOOST_ROOT=C:\Boost
set OPENSSL_ROOT_DIR=C:\OpenSSL-Win64
build-mingw.bat
```

See [Building](./building.md) for detailed platform instructions and CMake options.

---

## Initial Configuration

Copy the mainnet config template:

```bash
cp share/vizd/config/config.ini /data/vizd/config.ini
```

Minimum edits for a public node:

```ini
# P2P
p2p-endpoint = 0.0.0.0:2001
p2p-seed-node = seed1.viz.media:2001
p2p-seed-node = seed2.viz.media:2001

# RPC
webserver-http-endpoint = 0.0.0.0:8090
webserver-ws-endpoint   = 0.0.0.0:8091

# Shared memory — adjust to available disk
shared-file-size = 4G

# Plugins (full node)
plugin = chain p2p webserver json_rpc database_api network_broadcast_api
plugin = social_network tags follow account_history
```

For a validator node, see [Validator Node](./validator-node.md).

---

## Starting the Node

```bash
./vizd --config-file /data/vizd/config.ini --data-dir /data/vizd
```

Or with Docker, pass the data directory as a volume (see Option A above).

---

## Verifying Sync

Query the node via HTTP RPC:

```bash
curl -s -X POST http://localhost:8090 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"call","params":["database_api","get_dynamic_global_properties",[]],"id":1}' \
  | python3 -m json.tool
```

Check `head_block_number` — it should increase every 3 seconds once synced.

---

## Node Types

| Type | Config template | Description |
|------|----------------|-------------|
| Full node | `config.ini` | All plugins, public RPC endpoints |
| Validator | `config_witness.ini` | Block production, RPC on localhost only |
| Testnet | `config_testnet.ini` | Development and testing |
| Low-memory | `config.ini` + `LOW_MEMORY_NODE` build flag | Consensus only, no history indexes |
| MongoDB | `config_mongo.ini` | Full history in MongoDB |

---

## Next Steps

- [Configuration reference](./configuration.md) — all config options explained
- [Docker deployment](./docker.md) — production Docker setup
- [Validator node](./validator-node.md) — run a block-producing validator
- [Snapshots](./snapshot.md) — fast-sync using state snapshots
