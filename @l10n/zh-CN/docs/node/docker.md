# Docker 部署

VIZ Ledger 提供四种 Docker 镜像，适用于不同的部署场景。所有镜像均采用两阶段构建：builder 阶段编译二进制文件；runtime 阶段仅包含二进制文件和配置。

---

## 可用镜像

| Dockerfile | 标签 | 描述 |
|-----------|-----|------|
| `Dockerfile-production` | `latest` | 完整主网节点（Release，全部插件） |
| `Dockerfile-testnet` | `testnet` | 测试网节点（`BUILD_TESTNET=ON`） |

---

## 快速开始

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

查看日志：

```bash
docker logs -f vizd
```

---

## 卷

| 容器路径 | 用途 |
|---------|------|
| `/var/lib/vizd` | 区块链数据、共享内存、block log |
| `/etc/vizd` | 配置文件 |

始终挂载 `/var/lib/vizd` 以在容器重启后保留状态。

使用自定义配置：

```bash
docker run -d \
  -v /data/vizd:/var/lib/vizd \
  -v /my/config.ini:/etc/vizd/config.ini \
  vizblockchain/vizd:latest
```

---

## 环境变量

入口脚本（`vizd.sh`）读取以下环境变量：

| 变量 | 描述 | 示例 |
|------|------|------|
| `VIZD_RPC_ENDPOINT` | 覆盖 HTTP RPC 端点 | `0.0.0.0:8090` |
| `VIZD_P2P_ENDPOINT` | 覆盖 P2P 端点 | `0.0.0.0:2001` |
| `VIZD_WITNESS` | 验证者账户名（启用区块生产） | `alice` |
| `VIZD_PRIVATE_KEY` | WIF 格式的验证者签名密钥 | `5J...` |

---

## 端口

| 端口 | 协议 | 用途 |
|------|------|------|
| 2001 | TCP | P2P 节点连接 |
| 8090 | TCP | HTTP JSON-RPC |
| 8091 | TCP | WebSocket JSON-RPC |

---

## 验证者节点（Docker）

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

对于验证者节点，**不要**公开暴露端口 8090/8091 — 仅绑定到 localhost：

```bash
-e VIZD_RPC_ENDPOINT=127.0.0.1:8090
```

---

## 测试网节点

```bash
docker run -d \
  --name vizd-testnet \
  -p 2001:2001 \
  -p 8090:8090 \
  -v /data/vizd-testnet:/var/lib/vizd \
  vizblockchain/vizd:testnet
```

---

## 本地构建镜像

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

### 各镜像的 CMake 标志

| 镜像 | `BUILD_TESTNET` |
|------|:---------------:|
| production | OFF |
| testnet | ON |

---

## CI/CD（GitHub Actions）

仓库提供 `.github/workflows/docker-main.yml`，每次推送到 `master` 时自动构建并推送标记为 `latest` 的 production 镜像。

```yaml
- name: Build and push
  uses: docker/build-push-action@v2
  with:
    file: share/vizd/docker/Dockerfile-production
    tags: vizblockchain/vizd:latest
    push: true
```

---

## 资源规划

| 节点类型 | 内存 | 磁盘 |
|---------|------|------|
| 完整节点（主网） | 8 GB+ | 50 GB+ |
| 验证者节点 | 4 GB | 20 GB |
| 测试网 | 4 GB | 10 GB |

共享内存大小应能舒适地放入 RAM。在 `config.ini` 中：

```ini
shared-file-size = 4G
```

---

## 日志轮转

vizd 将所有输出写入 stdout/stderr。Docker 默认的 `json-file` 日志驱动**没有大小限制**——崩溃循环或断言风暴可在数分钟内填满宿主机磁盘（生产环境中曾观察到 35 GB+）。

建议改用 `local` 驱动。它以紧凑的二进制格式存储日志并自动轮转。

**全局配置（推荐——保护宿主机上的所有容器）：**

```json
// /etc/docker/daemon.json
{
  "log-driver": "local",
  "log-opts": {
    "max-size": "100m",
    "max-file": "5"
  }
}
```

应用配置：

```bash
sudo systemctl restart docker
```

**单容器配置（`docker run`）：**

```bash
docker run -d \
  --log-driver=local \
  --log-opt max-size=100m \
  --log-opt max-file=5 \
  --name vizd \
  vizblockchain/vizd:latest
```

**单容器配置（docker-compose）：**

```yaml
services:
  vizd:
    image: vizblockchain/vizd:latest
    logging:
      driver: local
      options:
        max-size: "100m"
        max-file: "5"
```

> 设置 `max-file: 5` 和 `max-size: 100m` 后，Docker 每个容器最多保留 500 MB 日志，轮转时自动删除最旧的文件。

---

## 故障排除

| 症状 | 原因 | 解决方案 |
|------|------|---------|
| 容器立即退出 | 配置错误或缺少卷 | `docker logs vizd` — 检查启动错误 |
| 端口 8090 不可达 | RPC 绑定到 localhost | 删除 `127.0.0.1:` 前缀或使用反向代理 |
| 无对等节点 | 防火墙阻止端口 2001 | 开放 2001 TCP 入站 |
| 同步缓慢 | 未加载快照 | 首次启动前在卷中提供快照 |
| `/var/lib/vizd` 权限拒绝 | 卷所有权不匹配 | `chown -R 1000:1000 /data/vizd` |
| Docker 日志填满磁盘 | `json-file` 驱动没有大小限制 | 配置带 `max-size`/`max-file` 的 `local` 驱动——参见[日志轮转](#日志轮转) |
