# 节点管理 API、UI 与 MCP

`node_admin` 是节点进程内部的管理插件，面向操作员和持有管理令牌的 AI 工具。它提供健康检查、配置、插件状态、日志、审计、P2P、快照和验证者管理；不是面向普通用户的公共链 API。

## 独立监听器与配置

公共 API 通常经 nginx 转发，节点看到的来源可能都是回环地址，因此管理权限不能依赖公共端口的来源 IP。管理插件使用独立监听器，默认只允许回环地址。非回环地址必须显式设置 `admin-allow-remote = true`。远程使用优先选择 SSH 隧道。

```ini
plugin = node_admin
admin-endpoint = 127.0.0.1:8099
admin-token = operator:read:REPLACE_WITH_SHA256_OF_TOKEN
admin-audit-log = /var/lib/vizd/node-admin-audit.log
admin-log-file = node=/var/lib/vizd/logs/node.log
admin-plan-ttl = 120
```

`admin-endpoint` 缺省或为空时插件不监听。设置了端口却没有令牌时拒绝启动。`admin-token` 可重复，格式为 `label:scope[,scope]:sha256hex[:expires-unixtime]`。用 `openssl rand -hex 32` 生成随机令牌，只把它的 SHA-256 放入配置；请求携带原令牌 `Authorization: Bearer ...`。比较使用恒定时间。令牌到期后无需重启即可失效；修改配置撤销令牌需要重启。

## 权限

所有令牌都必须包含 `read`。额外权限不替代 `read`。

| Scope | 功能 |
|---|---|
| `read` | 查询、诊断、日志、快照列表和下载 |
| `p2p` | 添加节点、可信节点、清除封禁 |
| `snapshot` | 创建、恢复快照 |
| `webserver` | 限流、统计、IP 名单及重置/重载 |
| `config` | 配置 plan/apply |
| `plugins` | 插件 plan/apply |
| `chain` | 验证者启停、签名密钥变更，需要已启动且配置 active key 的 `validator_guard` |
| `process` | 重启和状态修复；修复会在下次启动改写状态，不只是停止进程 |

## JSON-RPC 与方法

向管理端点发送 JSON-RPC 2.0 POST，`Content-Type: application/json`。所有调用均检查令牌和 scope。省略必需参数、未知方法及越权请求会返回错误，不应把 HTTP 成功等同于操作成功。

```json
{"jsonrpc":"2.0","id":1,"method":"admin.node_info","params":{}}
```

| 方法组 | 方法与用途 |
|---|---|
| 健康 | `admin.ping`、`admin.auth.me`（当前令牌权限/期限）、`admin.node_info`（头块/LIB/版本）、`admin.diagnostics` |
| 插件 | `admin.plugins.list`、`admin.plugins.status`；配置的插件与实际观测到的生命周期状态有区别 |
| 状态对象 | `admin.objects.list`、`admin.objects.check`、`admin.objects.repair`；修复需要 `process` 并重启，不把未能读取当作健康 |
| 配置/日志 | `admin.config.get`、`admin.logs.list`、`admin.logs.tail`、`admin.audit.tail`；只可读取显式允许的日志路径，敏感配置不返回明文 |
| P2P | `admin.p2p.status`、`admin.p2p.add_node`、`admin.p2p.add_trusted`、`admin.p2p.clear_banned`；变更在 P2P 线程执行 |
| 快照 | `admin.snapshot.status`、`admin.snapshot.create`、`admin.snapshot.restore`；恢复会替换节点状态，不能当作只读操作 |
| 验证者 | `admin.validators.active`、`admin.validator.list`、`admin.validator.plan`、`admin.validator.apply` |
| Webserver | `admin.webserver.status`、`admin.webserver.ips`、`admin.webserver.limits`、`admin.webserver.stats`、`admin.webserver.whitelist_add`、`admin.webserver.whitelist_remove`、`admin.webserver.blacklist_add`、`admin.webserver.blacklist_remove`、`admin.webserver.reset`、`admin.webserver.reload` |
| 配置计划 | `admin.config.plan`、`admin.config.apply`、`admin.config.backups` |
| 插件计划 | `admin.plugins.plan`、`admin.plugins.apply` |
| 计划/进程 | `admin.plan.cancel`、`admin.restart` |

Webserver 统计和限流是独立开关。白名单绕过限制但仍计数；黑名单即使关闭统计也返回 403。重置计数不清除名单或限制。配置的插件不一定已经加载，UI 不把配置列表等同于实际运行状态。

## 两阶段变更和备份

`admin.config.plan {"set":{"key":value}}` 只验证允许的键并返回 `plan_id`、精确 `diff`、`config_path`、`needs_restart` 和 `expires_at`，不写文件。`admin.plugins.plan` 使用 `add`/`remove` 列表，拒绝重复、未知插件及移除 `chain`/`node_admin`。无实际变化的计划被拒绝。

计划与创建它的令牌 label 绑定、一次性使用，并在 `admin-plan-ttl`（默认 120 秒）后过期；最多保留 64 个。apply 会重新验证参数以及配置 SHA-256；文件在 plan 后被别人改动则不写入。`admin.plan.cancel` 可丢弃计划。

成功写入会保留原注释、顺序、CRLF 和权限，创建同目录 `config.ini.bak-<unixtime>`，通过临时文件、fsync、rename 原子替换。备份不会自动清理，可能含密钥，须像原配置一样保护。配置/插件变更需要重启后生效，不是热加载。`admin.restart` 只请求退出，由 systemd/Docker 等监督器重新启动；不会自行启动第二个进程。

验证者变更通过 `validator_guard` 的 active key 签署链上交易，API 不返回密钥。没有可用 guard 时查询仍可说明原因，变更不可用。

## 内置 UI

打开 `http://127.0.0.1:8099/ui/`。页面编译进二进制，无外部资源；首次使用输入 bearer 令牌。令牌保存在本标签页的 `sessionStorage`，不是服务器内置秘密。UI 根据 `admin.auth.me` 隐藏无权限操作，实际服务端仍独立检查权限。

UI 提供总览、诊断、插件、对象检查、日志、P2P、快照、Webserver 策略、验证者和配置计划。危险变更先展示计划/差异，再执行 apply。页面自身 GET 不需要认证，但不携带节点数据，也不授予任何权限。反向代理前缀下页面根据自己的 URL 计算 RPC 基址。

快照下载 `GET /snapshot/latest` 或 `GET /snapshot/<name>` 及对应 HEAD 需要 bearer/read；只接受列表中实际文件名，拒绝目录穿越。超过 8 MiB 返回 413，此时使用 scp 等方式复制。

## MCP（Streamable HTTP）

MCP 在同一插件和监听器内运行，无需 sidecar；使用相同令牌和审计。

| 方法 | 行为 |
|---|---|
| `initialize` | 支持 `2024-11-05`、`2025-03-26`、`2025-06-18`，默认后者；只声明 tools 能力 |
| `notifications/initialized` | 无 id 通知，HTTP 202 空正文 |
| `ping` | 返回空对象 |
| `tools/list` | 返回管理工具及 JSON Schema |
| `tools/call` | `{"name":"admin.node_info","arguments":{}}` 调用对应管理方法 |

MCP 工具与上表管理方法一一对应，共 41 个。无会话，不签发 `Mcp-Session-Id`，不提供持续 SSE 订阅。工具执行错误通过结果中的 `isError:true` 和 `content` 返回；协议错误（未知工具、参数错误、缺少权限）仍是 JSON-RPC 错误。AI 工具不会因为通过 MCP 调用就得到额外权限。

## 日志、审计与安全

审计记录令牌 label、方法、脱敏参数、结果、错误码、耗时和来源，不记录原令牌或私钥。审计文件权限 0600，16 MB 轮转。仅允许列出的日志文件，路径和配置中的秘密字段被过滤。

普通 fc file_appender 的定时轮转在当前节点主线程模型下不能可靠执行。需要硬上限时使用可选 `capped_file` appender（默认保留 10000 行，0 为不限），并把 `admin-log-file` 指到同一路径。不要把仅配置了小时轮转当作已限制磁盘增长。

管理端口包含状态恢复、链上密钥变更及进程控制能力，不应接入公共 RPC 反向代理。通过隧道访问时保留令牌权限分离和审计。详尽参数、配置允许列表和各方法返回字段可对照[英文完整参考](/plugins/node-admin)。

测试入口：`plugins/node_admin/tests/acceptance.sh`、`plugins/node_admin/tests/validator-e2e.sh`；包含 token/scope 拒绝、plan/apply、快照、对象修复、MCP/UI 与审计检查。

## 4.2.0 安全补充

入口总预算为每秒 100 个请求，超出部分在写审计前拒绝；审计轮换保留 `.1`–`.8` 八代，MCP 审计增加实际工具名称 `tool`。HTTP 握手超时为 5 秒，并发连接上限为 64。快照 HTTP 下载上限为 8 MiB，大文件使用 scp。

持久化的 webserver 运行策略仅在 `config_policy` 与启动时 config.ini 的统计/限流配置一致时恢复。修改这些配置后采用新配置，IP 名单仍保留；无指纹的旧状态文件仅恢复 IP 名单。关闭统计日志不会关闭已启用的限流。
