# Plugin Development

VIZ Ledger's plugin system is built on AppBase. Every plugin follows the same lifecycle, registers its API with the JSON-RPC layer, and subscribes to chain database signals.

---

## Plugin Structure

A plugin consists of:

- **Header** (`include/graphene/plugins/<name>/plugin.hpp`) — declares the plugin class and its API.
- **Implementation** (`plugin.cpp`) — lifecycle hooks, signal subscriptions, API method bodies.
- **CMakeLists.txt** — declares the target and links dependencies.

### Scaffolding a new plugin

```bash
python3 programs/util/newplugin.py graphene myplugin
```

This generates boilerplate under `plugins/myplugin/`:
- `CMakeLists.txt`
- `include/graphene/plugins/myplugin/plugin.hpp`
- `plugin.cpp`
- API header and implementation files

---

## Lifecycle

```
plugin_initialize(options)
  └── Register API factory
  └── Parse options

plugin_startup()
  └── Connect to database signals
  └── Start any background threads

plugin_shutdown()
  └── Disconnect signals
  └── Stop background threads
```

All three methods are called by AppBase in dependency order. Never call `plugin_startup()` directly.

---

## JSON-RPC API Registration

Plugins register methods with the `json_rpc` plugin using a macro-driven visitor:

```cpp
// In plugin.hpp — declare API
DECLARE_API(
    (get_account_history)
    (get_ops_in_block)
)

// In plugin.cpp — startup
plugin_startup() {
    auto& json_rpc = appbase::app().get_plugin<json_rpc::plugin>();
    json_rpc.add_api(
        MAKE_API(this, get_account_history)
        MAKE_API(this, get_ops_in_block)
    );
}
```

Each API method accepts a single argument struct and returns a single result struct. Void methods use a dedicated empty result type.

**Method naming:** The JSON-RPC method name is `<plugin_namespace>.<method_name>`. For example, `account_history.get_account_history`.

---

## Database Signals

The chain database emits signals that plugins subscribe to:

| Signal | Trigger |
|--------|---------|
| `applied_block` | After a block is applied (post-state) |
| `pre_apply_operation` | Before each operation is applied |
| `on_applied_transaction` | After a transaction is applied |
| `post_apply_operation` | After each operation is applied |

```cpp
// Connect in plugin_startup()
auto& db = appbase::app().get_plugin<chain::plugin>().db();

db.applied_block.connect([this](const signed_block& b) {
    on_applied_block(b);
});

db.pre_apply_operation.connect([this](const operation_notification& note) {
    on_pre_apply_operation(note);
});
```

**Important:** Signal handlers run synchronously during block processing. Do not perform heavy work inside them — queue tasks to a background thread instead.

---

## Database Access

### Reading (from API methods)

Use a weak read lock to minimize contention:

```cpp
auto& db = appbase::app().get_plugin<chain::plugin>().db();
// db is automatically locked for reading in API handlers
auto account = db.get_account("alice");
```

### Writing (from signal handlers or evaluators)

Only write inside signal handlers or evaluators — never from API methods.

```cpp
// Inside an applied_block handler
db.modify(db.get_account("alice"), [](account_object& a) {
    a.some_field = new_value;
});
```

---

## Custom Database Indices

Plugins can add their own indices to the database:

```cpp
// In plugin_startup(), after chain is initialized
auto& db = appbase::app().get_plugin<chain::plugin>().db();
db.add_plugin_index<my_custom_index>();
```

Define the object and index in headers following the pattern of existing objects:

```cpp
// Object definition
class my_object : public chainbase::object<my_object_type, my_object> {
    id_type id;
    account_name_type account;
    uint64_t some_field;
};

// MultiIndex container
using my_index = chainbase::shared_multi_index_container<
    my_object,
    indexed_by<
        ordered_unique<tag<by_id>, member<my_object, my_object::id_type, &my_object::id>>,
        ordered_unique<tag<by_account>, member<my_object, account_name_type, &my_object::account>>
    >
>;
```

---

## Custom Operation Evaluators

To handle new operation types:

```cpp
// Define the operation in the protocol layer and register an evaluator
class my_operation_evaluator : public evaluator<my_operation> {
public:
    void do_apply(const my_operation& op) {
        // Validate and apply state changes
        auto& db = this->db();
        // ...
    }
};

// Register in database initialization
db.register_evaluator<my_operation_evaluator>();
```

Use `has_hardfork(CHAIN_HARDFORK_N)` checks to gate behavior changes for backward compatibility.

---

## WebSocket Real-time Events

To emit real-time notifications:

```cpp
// During plugin_startup(), register a block callback with the webserver
auto& ws = appbase::app().get_plugin<webserver::plugin>();
ws.add_handler("my_stream", [this](const fc::variant& params, fc::variant& result) {
    // Stream handler
});
```

The webserver plugin runs its own `io_service` thread — post callbacks from any thread using `ws.post([]{...})`.

---

## Dependency Declaration

Declare dependencies in your plugin's `plugin_requires()`:

```cpp
static std::vector<appbase::abstract_plugin*> plugin_requires() {
    return { &appbase::app().get_plugin<json_rpc::plugin>(),
             &appbase::app().get_plugin<chain::plugin>() };
}
```

AppBase resolves the initialization order automatically.

---

## Performance Guidelines

- **API methods**: Use indexed lookups, not full scans. Add plugin indices for hot access patterns.
- **Signal handlers**: Return quickly. Queue heavy processing to a dedicated `fc::thread`.
- **Caching**: Cache hot-path results in memory; invalidate on `applied_block`.
- **Pagination**: Always paginate large result sets rather than returning unbounded collections.

---

## Testing Plugins

Write unit tests using Boost.Test and the existing test harness. Add tests to the appropriate category suite (`operation_tests`, `block_tests`, etc.).

For integration tests, load your plugin alongside the chain and replay a known block sequence with `--replay-from-snapshot --snapshot-auto-latest`.

---

## Deployment

Enable plugins in `config.ini`:

```ini
plugin = myplugin
```

Some plugins require a full reindex when enabled on an existing chain (especially those that track historical operations). Document this requirement clearly.

For external (third-party) plugins, place them in `plugins/external/` — CMake discovers them automatically.

---

See also: [Plugins Overview](../plugins/overview.md), [Database API](../plugins/database-api.md), [Building](./building.md), [Debugging](./debugging.md).
