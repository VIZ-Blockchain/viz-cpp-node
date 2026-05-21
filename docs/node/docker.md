# Docker Deployment

VIZ Ledger ships Docker images for different deployment profiles. All use a two-stage build: the builder stage compiles the binary; the runtime stage contains only the binary and configuration.

---

## Available Images

| Dockerfile | Tag | Description |
|-----------|-----|-------------|
| `Dockerfile-production` | `latest` | Full mainnet node (Release, all plugins) |
| `Dockerfile-testnet` | `testnet` | Testnet node (`BUILD_TESTNET=ON`) |

---

## Quick Start

```bash
docker run -d \
  --name vizd \
  --restart unless-stopped \
  -p 2001:2001 \
  -p 8090:8090 \
  -p 8091:8091 \
  -v /data/vizd:/var/lib/vizd \
  vizblockchain/vizd:latest
```

Follow logs:

```bash
docker logs -f vizd
```

---

## Volumes

| Container path | Purpose |
|----------------|---------|
| `/var/lib/vizd` | Blockchain data, shared memory, block log |
| `/etc/vizd` | Configuration files |

Always mount `/var/lib/vizd` to persist state across container restarts.

To use a custom config:

```bash
docker run -d \
  -v /data/vizd:/var/lib/vizd \
  -v /my/config.ini:/etc/vizd/config.ini \
  vizblockchain/vizd:latest
```

---

## Environment Variables

The entry script (`vizd.sh`) reads these environment variables:

| Variable | Description | Example |
|----------|-------------|---------|
| `VIZD_RPC_ENDPOINT` | Override HTTP RPC endpoint | `0.0.0.0:8090` |
| `VIZD_P2P_ENDPOINT` | Override P2P endpoint | `0.0.0.0:2001` |
| `VIZD_WITNESS` | Validator account name (enables block production) | `alice` |
| `VIZD_PRIVATE_KEY` | Validator signing key in WIF format | `5J...` |

---

## Ports

| Port | Protocol | Purpose |
|------|----------|---------|
| 2001 | TCP | P2P peer connections |
| 8090 | TCP | HTTP JSON-RPC |
| 8091 | TCP | WebSocket JSON-RPC |

---

## Validator Node (Docker)

```bash
docker run -d \
  --name vizd-validator \
  --restart unless-stopped \
  -p 2001:2001 \
  -v /data/vizd:/var/lib/vizd \
  -e VIZD_WITNESS=myvalidator \
  -e VIZD_PRIVATE_KEY=5Jxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx \
  vizblockchain/vizd:latest
```

For validators, do **not** expose ports 8090/8091 publicly — bind to localhost only:

```bash
-e VIZD_RPC_ENDPOINT=127.0.0.1:8090
```

---

## Testnet Node

```bash
docker run -d \
  --name vizd-testnet \
  -p 2001:2001 \
  -p 8090:8090 \
  -v /data/vizd-testnet:/var/lib/vizd \
  vizblockchain/vizd:testnet
```

---

## Building Images Locally

```bash
# Production
docker build \
  -f share/vizd/docker/Dockerfile-production \
  -t vizd:local \
  .

# Testnet
docker build \
  -f share/vizd/docker/Dockerfile-testnet \
  -t vizd:testnet \
  .
```

### CMake flags per image

| Image | `BUILD_TESTNET` |
|-------|:---------------:|
| production | OFF |
| testnet | ON |

---

## CI/CD (GitHub Actions)

The repository ships `.github/workflows/docker-main.yml` which builds and pushes the production image tagged `latest` on every push to `master`.

```yaml
- name: Build and push
  uses: docker/build-push-action@v2
  with:
    file: share/vizd/docker/Dockerfile-production
    tags: vizblockchain/vizd:latest
    push: true
```

---

## Resource Sizing

| Node type | RAM | Disk |
|-----------|-----|------|
| Full node (mainnet) | 8 GB+ | 50 GB+ |
| Validator node | 4 GB | 20 GB |
| Testnet | 4 GB | 10 GB |

Start with a shared memory size that fits comfortably in RAM. In `config.ini`:

```ini
shared-file-size = 4G
```

---

## Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| Container exits immediately | Bad config or missing volume | `docker logs vizd` — check for startup errors |
| Port 8090 unreachable | RPC bound to localhost | Remove `127.0.0.1:` prefix or use reverse proxy |
| No peers | Firewall blocking port 2001 | Open port 2001 TCP inbound |
| Slow sync | No snapshot loaded | Provide snapshot in volume before first start |
| `Permission denied` on `/var/lib/vizd` | Volume ownership mismatch | `chown -R 1000:1000 /data/vizd` |
