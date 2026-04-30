
How plugins work
----------------

Plugins live under `plugins/<name>/` at the repo root. Each plugin is built as a static library (or shared, when `BUILD_SHARED_LIBRARIES=TRUE`) and registered with the appbase plugin framework.

`plugins/CMakeLists.txt` walks every subdirectory and includes the ones that contain a `CMakeLists.txt`. Plugins are then wired into the `vizd` binary by explicit registration in `programs/vizd/main.cpp` — there is no auto-discovery at runtime.

To add a new plugin:

1. Create a directory under `plugins/<your_plugin>/`.
2. Add a `CMakeLists.txt` modeled on an existing plugin (e.g. `plugins/raw_block/CMakeLists.txt`).
3. Register the plugin in `programs/vizd/main.cpp` (see the `register_plugins()` function).

Registering plugins
-------------------

- Plugins are enabled with the `enable-plugin` config file option.
- When specifying plugins, you should specify `witness` and `account_history` in addition to the new plugins.
- Some plugins may keep records in the database (currently only `account_history` does).  If you change whether such a plugin is disabled/enabled, you should also replay the chain.  Detecting this situation and automatically replaying when needed will be implemented in a future release.
- If you want to make API's available publicly, you must use the `public-api` option.
- When specifying public API's, you should specify `database_api` and `login_api` in addition to the new plugins.
- The `api-user` option allows for password protected access to an API.

Authoring a plugin
------------------

When implementing a new plugin:

- Register signal handlers in `plugin_startup()` (if needed)
- Add methods to `myplugin_api` class and reflect them in `FC_API` declaration
