# 插件开发

VIZ Ledger 的插件系统基于 AppBase 构建。每个插件遵循相同的生命周期，向 JSON-RPC 层注册其 API，并订阅链数据库信号。

---

## 插件结构

插件由以下部分组成：

- **头文件**（`include/graphene/plugins/<name>/plugin.hpp`）— 声明插件类及其 API。
- **实现**（`plugin.cpp`）— 生命周期钩子、信号订阅、API 方法主体。
- **CMakeLists.txt** — 声明目标并链接依赖项。

### 创建新插件脚手架

```bash
python3 programs/util/newplugin.py graphene myplugin
```

在 `plugins/myplugin/` 下生成样板代码：
- `CMakeLists.txt`
- `include/graphene/plugins/myplugin/plugin.hpp`
- `plugin.cpp`
- API 头文件和实现文件

---

## 生命周期

```
plugin_initialize(options)
  └── 注册 API 工厂
  └── 解析选项

plugin_startup()
  └── 连接数据库信号
  └── 启动后台线程

plugin_shutdown()
  └── 断开信号
  └── 停止后台线程
```

AppBase 按依赖顺序调用这三个方法。永远不要直接调用 `plugin_startup()`。

---

## JSON-RPC API 注册

插件使用宏驱动的访问者向 `json_rpc` 插件注册方法：

```cpp
// 在 plugin.hpp 中 — 声明 API
DECLARE_API(
    (get_account_history)
    (get_ops_in_block)
)

// 在 plugin.cpp 中 — 启动时
plugin_startup() {
    auto& json_rpc = appbase::app().get_plugin<json_rpc::plugin>();
    json_rpc.add_api(
        MAKE_API(this, get_account_history)
        MAKE_API(this, get_ops_in_block)
    );
}
```

每个 API 方法接受单个参数结构体并返回单个结果结构体。Void 方法使用专用的空结果类型。

**方法命名：** JSON-RPC 方法名为 `<插件命名空间>.<方法名>`。例如，`account_history.get_account_history`。

---

## 数据库信号

链数据库发出插件可订阅的信号：

| 信号 | 触发条件 |
|------|----------|
| `applied_block` | 区块应用后（后状态） |
| `pre_apply_operation` | 每个操作应用前 |
| `on_applied_transaction` | 交易应用后 |
| `post_apply_operation` | 每个操作应用后 |

```cpp
// 在 plugin_startup() 中连接
auto& db = appbase::app().get_plugin<chain::plugin>().db();

db.applied_block.connect([this](const signed_block& b) {
    on_applied_block(b);
});

db.pre_apply_operation.connect([this](const operation_notification& note) {
    on_pre_apply_operation(note);
});
```

**重要：** 信号处理器在区块处理期间同步运行。不要在其中执行繁重的工作——将任务排队到后台线程。

---

## 数据库访问

### 读取（从 API 方法）

使用弱读锁以最小化争用：

```cpp
auto& db = appbase::app().get_plugin<chain::plugin>().db();
// 在 API 处理器中 db 自动加读锁
auto account = db.get_account("alice");
```

### 写入（从信号处理器或评估器）

只在信号处理器或评估器内写入——永远不要从 API 方法写入。

```cpp
// 在 applied_block 处理器内
db.modify(db.get_account("alice"), [](account_object& a) {
    a.some_field = new_value;
});
```

---

## 自定义数据库索引

插件可以向数据库添加自己的索引：

```cpp
// 在 plugin_startup() 中，链初始化后
auto& db = appbase::app().get_plugin<chain::plugin>().db();
db.add_plugin_index<my_custom_index>();
```

按照现有对象的模式在头文件中定义对象和索引：

```cpp
// 对象定义
class my_object : public chainbase::object<my_object_type, my_object> {
    id_type id;
    account_name_type account;
    uint64_t some_field;
};

// MultiIndex 容器
using my_index = chainbase::shared_multi_index_container<
    my_object,
    indexed_by<
        ordered_unique<tag<by_id>, member<my_object, my_object::id_type, &my_object::id>>,
        ordered_unique<tag<by_account>, member<my_object, account_name_type, &my_object::account>>
    >
>;
```

---

## 自定义操作评估器

处理新操作类型：

```cpp
// 在协议层定义操作并注册评估器
class my_operation_evaluator : public evaluator<my_operation> {
public:
    void do_apply(const my_operation& op) {
        // 验证并应用状态变更
        auto& db = this->db();
        // ...
    }
};

// 在数据库初始化时注册
db.register_evaluator<my_operation_evaluator>();
```

使用 `has_hardfork(CHAIN_HARDFORK_N)` 检查来控制向后兼容的行为变更。

---

## WebSocket 实时事件

发送实时通知：

```cpp
// 在 plugin_startup() 期间，向 webserver 注册区块回调
auto& ws = appbase::app().get_plugin<webserver::plugin>();
ws.add_handler("my_stream", [this](const fc::variant& params, fc::variant& result) {
    // 流处理器
});
```

Webserver 插件运行自己的 `io_service` 线程——使用 `ws.post([]{...})` 从任意线程发布回调。

---

## 依赖声明

在插件的 `plugin_requires()` 中声明依赖项：

```cpp
static std::vector<appbase::abstract_plugin*> plugin_requires() {
    return { &appbase::app().get_plugin<json_rpc::plugin>(),
             &appbase::app().get_plugin<chain::plugin>() };
}
```

AppBase 自动解析初始化顺序。

---

## 性能指南

- **API 方法**：使用索引查找，不要全扫描。为热访问模式添加插件索引。
- **信号处理器**：快速返回。将繁重处理排队到专用的 `fc::thread`。
- **缓存**：在内存中缓存热路径结果；在 `applied_block` 时使缓存失效。
- **分页**：始终对大型结果集分页，而不是返回无界集合。

---

## 测试插件

使用 `debug_node` 插件模拟链条件：

```json
{"method":"debug_node.debug_generate_blocks","params":["5K...",10,0,0,{}]}
```

使用 Boost.Test 和现有测试框架编写单元测试。将测试添加到适当的类别套件（`operation_tests`、`block_tests` 等）。

对于集成测试，将插件与链一起加载，并使用 `debug_push_blocks` 重放已知的区块序列。

---

## 部署

在 `config.ini` 中启用插件：

```ini
plugin = myplugin
```

某些插件在现有链上启用时需要完整重新索引（特别是那些跟踪历史操作的插件）。明确记录此要求。

对于外部（第三方）插件，将其放置在 `plugins/external/` 中——CMake 会自动发现它们。

---

参见：[插件概述](../plugins/overview.md)、[Database API](../plugins/database-api.md)、[构建](./building.md)、[调试](./debugging.md)。
