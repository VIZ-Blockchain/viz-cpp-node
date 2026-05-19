# 快速开始

本指南涵盖运行 VIZ Ledger 节点所需的一切——从安装依赖项到初始同步。

---

## 前置要求

| 要求 | 最低配置 | 推荐配置 |
|------|---------|---------|
| 操作系统 | Ubuntu 20.04 LTS | Ubuntu 24.04 LTS |
| 内存 | 4 GB | 8 GB+ |
| 磁盘 | 20 GB | 50 GB+ SSD |
| CPU | 2 核 | 4+ 核 |
| 网络 | 公网 IP，开放端口 2001 | 稳定连接 |

**使用的端口：**

| 端口 | 协议 | 用途 |
|------|------|------|
| 2001 | TCP | P2P 节点连接 |
| 8090 | TCP | HTTP JSON-RPC |
| 8091 | TCP | WebSocket JSON-RPC |

---

## 方案 A：Docker（推荐快速启动）

### 1. 拉取生产镜像

```bash
docker pull vizblockchain/vizd:latest
```

### 2. 运行节点

```bash
docker run -d \
  --name vizd \
  -p 2001:2001 \
  -p 8090:8090 \
  -p 8091:8091 \
  -v /data/vizd:/var/lib/vizd \
  vizblockchain/vizd:latest
```

### 3. 查看日志

```bash
docker logs -f vizd
```

几分钟内应该看到节点连接和区块同步进度。

### 环境变量（Docker）

| 变量 | 用途 | 示例 |
|------|------|------|
| `VIZD_SEED_NODES` | 覆盖默认种子节点 | `node1.viz.media:2001` |
| `VIZD_WITNESS` | 验证者名称（验证者节点时使用） | `alice` |
| `VIZD_PRIVATE_KEY` | 验证者签名密钥（WIF 格式） | `5J...` |

---

## 方案 B：从源码构建

### 1. 安装依赖（Linux）

```bash
git clone --recursive https://github.com/VIZ-Blockchain/viz-cpp-node
cd viz-cpp-node
chmod +x install-deps-linux.sh
sudo ./install-deps-linux.sh
```

### 2. 构建

```bash
chmod +x build-linux.sh
./build-linux.sh
```

低内存构建（验证者和种子节点——不含索引插件）：

```bash
./build-linux.sh -l
```

二进制文件位于 `build/programs/vizd/vizd`。

### 3. macOS

```bash
chmod +x build-mac.sh
./build-mac.sh
```

### 4. Windows（MinGW）

```cmd
set BOOST_ROOT=C:\Boost
set OPENSSL_ROOT_DIR=C:\OpenSSL-Win64
build-mingw.bat
```

详细的平台说明和 CMake 选项参见[构建](./building.md)。

---

## 初始配置

复制主网配置模板：

```bash
cp share/vizd/config/config.ini /data/vizd/config.ini
```

公共节点的最小修改：

```ini
# P2P
p2p-endpoint = 0.0.0.0:2001
p2p-seed-node = seed1.viz.media:2001
p2p-seed-node = seed2.viz.media:2001

# RPC
webserver-http-endpoint = 0.0.0.0:8090
webserver-ws-endpoint   = 0.0.0.0:8091

# 共享内存——根据可用磁盘调整
shared-file-size = 4G

# 插件（全节点）
plugin = chain p2p webserver json_rpc database_api network_broadcast_api
plugin = social_network tags follow account_history
```

验证者节点配置参见[验证者节点](./validator-node.md)。

---

## 启动节点

```bash
./vizd --config-file /data/vizd/config.ini --data-dir /data/vizd
```

使用 Docker 时，将数据目录作为卷挂载（参见方案 A）。

---

## 验证同步

通过 HTTP RPC 查询节点：

```bash
curl -s -X POST http://localhost:8090 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"call","params":["database_api","get_dynamic_global_properties",[]],"id":1}' \
  | python3 -m json.tool
```

检查 `head_block_number`——同步后应每 3 秒增加一次。

---

## 节点类型

| 类型 | 配置模板 | 描述 |
|------|---------|------|
| 全节点 | `config.ini` | 所有插件，公共 RPC 端点 |
| 验证者 | `config_witness.ini` | 区块生产，RPC 仅限本地 |
| 测试网 | `config_testnet.ini` | 开发和测试 |
| 低内存 | `config.ini` + `LOW_MEMORY_NODE` 构建标志 | 仅共识，无历史索引 |
| MongoDB | `config_mongo.ini` | MongoDB 中的完整历史 |

---

## 后续步骤

- [配置参考](./configuration.md) — 所有配置选项说明
- [Docker 部署](./docker.md) — 生产环境 Docker 设置
- [验证者节点](./validator-node.md) — 运行区块生产验证者
- [快照](./snapshot.md) — 使用状态快照快速同步
