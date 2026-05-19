# 网络服务器插件

网络服务器插件提供 HTTP 和 WebSocket 端点，将 JSON-RPC 请求转发到 `json_rpc` 插件。它包含以 `method + params`（不含 `id`）为键的响应缓存（每个应用的区块使其失效），以及用于并发请求处理的线程池。

**源码：** [plugins/webserver/webserver_plugin.cpp](../../plugins/webserver/webserver_plugin.cpp)

---

## 依赖

```
json_rpc::plugin
```

---

## 配置

| 选项 | 默认值 | 描述 |
|------|--------|------|
| `webserver-http-endpoint` | — | HTTP 监听地址，例如 `0.0.0.0:8090` |
| `webserver-ws-endpoint` | — | WebSocket 监听地址，例如 `0.0.0.0:8091` |
| `webserver-thread-pool-size` | `256` | 处理 HTTP 和 WebSocket 请求的工作线程数。 |
| `webserver-cache-enabled` | `true` | 启用响应缓存。 |
| `webserver-cache-size` | `10000` | 缓存响应的最大数量。达到此限制时整个缓存被驱逐。 |

插件至少需要设置 `webserver-http-endpoint` 或 `webserver-ws-endpoint` 之一才能工作。两者可同时启用。

---

## 最小 config.ini

```ini
plugin = webserver

webserver-http-endpoint = 0.0.0.0:8090
webserver-ws-endpoint   = 0.0.0.0:8091
```

---

## 响应缓存

### 缓存什么

每个只读 JSON-RPC 响应都有资格被缓存。缓存键是 `method + params` 的 SHA-256 哈希 — 故意**排除 `id`**，这样轮换请求 `id` 就无法绕过缓存。

### 永不缓存

| 命名空间 | 原因 |
|---------|------|
| `network_broadcast_api.*` | 改变状态（交易/区块广播） |
| `debug_node.*` | 改变状态的调试操作 |
| 格式错误/无法解析的请求 | 无法可靠地派生键 |

批量请求（JSON 数组）被视为单个原子缓存条目，以完整数组哈希为键。

### 失效

缓存在每个应用的区块上被清除。响应永远不会超过一个区块间隔（3 秒）地服务过时内容。

### 禁用缓存

```ini
webserver-cache-enabled = false
```

对于服务高延迟敏感或实时客户端（3 秒缓存窗口不可接受）的节点，请禁用。

---

## 线程池

HTTP 和 WebSocket 服务器各自运行在专用的 `io_service` 实例上。传入请求被分派到 `webserver-thread-pool-size` 个工作线程的共享线程池。

**大小指导：**
- 混合读写流量的公共 API 节点：`256`（默认）对大多数工作负载足够。
- 高吞吐量节点：增加到 `512` 或更多。监控 CPU 饱和 — 线程数超过 CPU 核心数对 CPU 密集操作没有帮助。
- 开发/本地节点：`4`–`8` 足够。

---

## WebSocket 订阅

WebSocket 客户端可以注册回调：

| 方法 | 描述 |
|------|------|
| `database_api.set_block_applied_callback` | 每个应用的区块触发，带区块头 |
| `database_api.set_pending_transaction_callback` | 交易进入待处理池时触发 |
| `database_api.cancel_all_subscriptions` | 取消所有回调的订阅 |

订阅需要持久的 WebSocket 连接。普通 HTTP 不支持。

---

## 安全性

- **绑定到 localhost**（`127.0.0.1`）并使用反向代理（nginx/Caddy）进行公共暴露。绑定到 `0.0.0.0` 会将 RPC 直接暴露给网络。
- 插件没有内置身份验证或速率限制。在反向代理层应用这些。
- 变更方法（`network_broadcast_api`、`debug_node`）在设计上受到保护，不受缓存中毒影响，但它们仍可从任何连接的客户端调用 — 如需要，在网络级别限制访问。

---

## 故障排除

| 症状 | 检查 |
|------|------|
| 启动时端口已被使用 | 另一个进程绑定到配置的端口；更改端口或终止冲突进程 |
| 高内存使用 | 减少 `webserver-cache-size` 或禁用缓存 |
| 负载下响应慢 | 增加 `webserver-thread-pool-size`；检查 CPU 饱和 |
| WebSocket 订阅未触发 | 订阅需要 WebSocket 连接，而非 HTTP |
| 过时响应 | 如果 `webserver-cache-enabled = true`，响应在一个区块间隔内（~3 秒）是新鲜的；对于实时使用请禁用缓存 |

---

参见：[插件概述](./overview.md)、[Database API](./database-api.md)、[JSON-RPC API](../api/json-rpc.md)。
