# 架构概述

VIZ Ledger 实现为一个模块化的 C++ 守护进程（`vizd`），由分层库和插件系统组成。本页描述结构层次、设计模式及组件交互方式。

---

## 分层结构

```
┌─────────────────────────────────────────────────────────────────┐
│  程序                                                           │
│  vizd（节点守护进程）         cli_wallet（CLI 钱包）            │
├─────────────────────────────────────────────────────────────────┤
│  插件                                                           │
│  chain │ validator │ p2p │ webserver │ json_rpc │ database_api  │
│  social_network │ snapshot │ committee_api │ invite_api │ ...   │
├─────────────────────────────────────────────────────────────────┤
│  核心库                                                         │
│  libraries/chain     — 区块链状态机，fork 数据库                │
│  libraries/protocol  — 操作类型，交易                           │
│  libraries/network   — P2P 消息传递                             │
│  libraries/api       — 共享 API 属性类型                        │
│  libraries/wallet    — 交易构建辅助工具                         │
│  libraries/time      — NTP 感知时间工具                         │
└─────────────────────────────────────────────────────────────────┘
```

### 库

| 库 | 关键文件 | 用途 |
|----|---------|------|
| `libraries/chain` | `database.hpp` | 区块链状态：账户、区块、对象、fork 数据库、共享内存 |
| `libraries/protocol` | `operations.hpp` | 所有 64+ 操作类型的 `static_variant` 联合 |
| `libraries/network` | `node.hpp` | P2P 引擎：对等连接、同步、消息传播 |
| `libraries/api` | `chain_api_properties.hpp` | API 插件返回的共享类型 |
| `libraries/wallet` | `wallet.hpp` | 远程节点 API 调用、交易构建 |
| `libraries/time` | `time.hpp` | 用于区块槽位时序的 NTP 同步 |

### 插件

插件在启动时向 `AppBase` 框架注册，并实现生命周期钩子（`plugin_initialize`、`plugin_startup`、`plugin_shutdown`）。

| 插件 | 作用 |
|------|------|
| `chain` | 打开数据库，验证并应用区块/交易 |
| `validator` | 按计划生产区块（Fair-DPOS），管理 NTP 和看门狗 |
| `p2p` | 管理对等连接，同步区块，传播交易 |
| `webserver` | API 访问的 HTTP 和 WebSocket 服务器 |
| `json_rpc` | 将 JSON-RPC 请求路由到已注册的 API 插件 |
| `database_api` | 只读查询：账户、区块、交易、全局数据 |
| `social_network` | 索引和查询内容、投票、回复 |
| `snapshot` | 创建和恢复状态快照 |
| `committee_api` | 委员会工作请求查询 |
| `invite_api` | 邀请对象查询 |
| `paid_subscription_api` | 付费订阅查询 |
| `account_history` | 账户操作历史索引 |
| `account_by_key` | 按公钥查找账户 |
| `follow` | 关注/屏蔽关系索引 |
| `tags` | 基于标签的内容索引 |
| `witness_api` | 验证者计划和签名密钥查询 |
| `debug_node` | 测试工具：注入区块、设置时间 |

---

## 设计模式

### 事件驱动的观察者（信号）

链的 `Database` 在关键点发出 `fc::signal` 事件。插件订阅这些信号以实现索引、历史记录和通知，而无需与核心链耦合。

```
push_block() / push_transaction()
    │
    ├── pre_apply_operation  ──► 订阅者插件（前置钩子）
    ├── [evaluator 应用状态变更]
    ├── post_apply_operation ──► 订阅者插件（后置钩子）
    └── applied_block        ──► 订阅者插件（区块已最终确认）
```

### 工厂 + 策略（Evaluator 注册表）

每种操作类型都有专用的 **evaluator** 类。`EvaluatorRegistry` 将操作类型 ID 映射到 evaluator 实例。应用交易时：

1. `Database` 从 `static_variant` 中提取操作类型标签。
2. 注册表返回已注册的 evaluator。
3. Evaluator 的 `do_apply(op)` 修改数据库状态。

添加新操作只需注册新的 evaluator——无需修改调度循环。

### 基于插件的架构（AppBase）

`vizd/main.cpp` 在调用 `app().exec()` 之前向 `AppBase` 注册所有插件。每个插件声明其选项和依赖项；AppBase 负责管理顺序和生命周期。

```
main() ──► register_plugin<chain>()
       ──► register_plugin<validator>()
       ──► register_plugin<p2p>()
       ──► ...
       ──► app().initialize(argc, argv)
       ──► app().startup()
       ──► app().exec()   ← 事件循环运行至 SIGINT/SIGTERM
```

### MVC 分离

| 层 | 组件 | 职责 |
|----|------|------|
| 数据 | `libraries/chain/database` | 状态持久化、验证、信号 |
| 控制 | 插件（`chain`、`validator`、`p2p`） | 生命周期、区块/交易接收、协调 |
| 视图 | API 插件（`database_api`、`social_network`、…） | 只读查询端点 |

---

## 数据流：传入区块

```
对等节点（P2P）──► p2p_plugin::handle_block()
                ──► chain_plugin::accept_block()
                    ──► database::push_block()
                        ├── 验证区块头和签名
                        ├── 对每笔交易：
                        │     ├── 验证权限
                        │     └── evaluator->do_apply(operation)
                        ├── 处理虚拟操作（奖励、兑现）
                        ├── 发出 applied_block 信号
                        └── 更新 fork 数据库 / LIB
```

## 数据流：API 请求

```
客户端（HTTP/WS）──► webserver_plugin
                 ──► json_rpc_plugin::call()
                     ──► registry.find_api_method(api, method)
                         ──► database_api / social_network / ...
                             ──► database::get_*(...)
                                 ──► 返回 JSON 结果
```

---

## 并发模型

| 关注点 | 方法 |
|--------|------|
| 写操作 | 单一写线程（可选配置 `single-write-thread`） |
| 读操作 | 通过 `chainbase` 共享内存支持多个并发读取者 |
| P2P I/O | 专用 `boost::asio::io_service` 线程池 |
| 区块生产计时器 | 验证者插件中独立的 `io_service` + 线程，防止 P2P 延迟 |
| RPC 服务 | 可配置线程池（`rpc-endpoint-thread-pool-size`） |

最重要的不变量：**任何时刻只有一个线程可以持有数据库的写锁。** 所有 evaluator 和区块处理代码都在该锁下运行。

---

## 共享内存数据库

状态存储在由 `chainbase` 管理的内存映射文件（`shared_memory.bin`）中。关键属性：

- 所有对象索引（账户、区块、内容、验证者、…）都存储在该文件中。
- 当可用空间低于阈值时，文件会增量扩展。
- 正常关闭后文件保持一致；崩溃可能需要从区块日志重放。
- 节点可以在区块边界导出共享内存状态的**快照**——参见[快照](../storage/snapshots.md)。

---

## 源码映射

| 文件 | 架构中的角色 |
|------|------------|
| `programs/vizd/main.cpp` | 插件注册和启动 |
| `libraries/chain/include/graphene/chain/database.hpp` | 核心数据库接口和信号 |
| `libraries/chain/include/graphene/chain/evaluator_registry.hpp` | 操作 evaluator 工厂 |
| `libraries/network/include/graphene/network/node.hpp` | P2P 节点委托接口 |
| `libraries/protocol/include/graphene/protocol/operations.hpp` | 操作类型联合 |
| `plugins/chain/plugin.cpp` | 链插件：区块/交易接收 |
| `plugins/json_rpc/plugin.cpp` | JSON-RPC 调度 |
