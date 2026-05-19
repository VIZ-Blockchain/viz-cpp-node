# Разработка плагинов

Система плагинов VIZ Ledger построена на AppBase. Каждый плагин следует единому жизненному циклу, регистрирует свой API в JSON-RPC слое и подписывается на сигналы базы данных цепочки.

---

## Структура плагина

Плагин состоит из:

- **Заголовка** (`include/graphene/plugins/<name>/plugin.hpp`) — объявляет класс плагина и его API.
- **Реализации** (`plugin.cpp`) — хуки жизненного цикла, подписки на сигналы, тела API-методов.
- **CMakeLists.txt** — объявляет цель и связывает зависимости.

### Создание каркаса нового плагина

```bash
python3 programs/util/newplugin.py graphene myplugin
```

Генерирует шаблонный код в `plugins/myplugin/`:
- `CMakeLists.txt`
- `include/graphene/plugins/myplugin/plugin.hpp`
- `plugin.cpp`
- Заголовок и реализация API

---

## Жизненный цикл

```
plugin_initialize(options)
  └── Зарегистрировать фабрику API
  └── Разобрать опции

plugin_startup()
  └── Подключиться к сигналам БД
  └── Запустить фоновые потоки

plugin_shutdown()
  └── Отключить сигналы
  └── Остановить фоновые потоки
```

Все три метода вызываются AppBase в порядке зависимостей. Никогда не вызывайте `plugin_startup()` напрямую.

---

## Регистрация JSON-RPC API

Плагины регистрируют методы в плагине `json_rpc` с помощью макросного посетителя:

```cpp
// В plugin.hpp — объявить API
DECLARE_API(
    (get_account_history)
    (get_ops_in_block)
)

// В plugin.cpp — при запуске
plugin_startup() {
    auto& json_rpc = appbase::app().get_plugin<json_rpc::plugin>();
    json_rpc.add_api(
        MAKE_API(this, get_account_history)
        MAKE_API(this, get_ops_in_block)
    );
}
```

Каждый API-метод принимает одну структуру аргументов и возвращает одну структуру результата. Void-методы используют специальный пустой тип результата.

**Именование методов:** JSON-RPC имя метода — `<пространство_имён_плагина>.<имя_метода>`. Например, `account_history.get_account_history`.

---

## Сигналы базы данных

База данных цепочки излучает сигналы, на которые подписываются плагины:

| Сигнал | Триггер |
|--------|---------|
| `applied_block` | После применения блока (постсостояние) |
| `pre_apply_operation` | Перед применением каждой операции |
| `on_applied_transaction` | После применения транзакции |
| `post_apply_operation` | После применения каждой операции |

```cpp
// Подключение в plugin_startup()
auto& db = appbase::app().get_plugin<chain::plugin>().db();

db.applied_block.connect([this](const signed_block& b) {
    on_applied_block(b);
});

db.pre_apply_operation.connect([this](const operation_notification& note) {
    on_pre_apply_operation(note);
});
```

**Важно:** Обработчики сигналов выполняются синхронно в процессе обработки блоков. Не выполняйте тяжёлую работу внутри них — ставьте задачи в очередь фонового потока.

---

## Доступ к базе данных

### Чтение (из API-методов)

Используйте слабую блокировку чтения для минимизации конкуренции:

```cpp
auto& db = appbase::app().get_plugin<chain::plugin>().db();
// В API-обработчиках db автоматически блокируется для чтения
auto account = db.get_account("alice");
```

### Запись (из обработчиков сигналов или эвалуаторов)

Пишите только внутри обработчиков сигналов или эвалуаторов — никогда из API-методов.

```cpp
// Внутри обработчика applied_block
db.modify(db.get_account("alice"), [](account_object& a) {
    a.some_field = new_value;
});
```

---

## Пользовательские индексы базы данных

Плагины могут добавлять собственные индексы в БД:

```cpp
// В plugin_startup(), после инициализации цепочки
auto& db = appbase::app().get_plugin<chain::plugin>().db();
db.add_plugin_index<my_custom_index>();
```

Определяйте объект и индекс в заголовках по образцу существующих объектов:

```cpp
// Определение объекта
class my_object : public chainbase::object<my_object_type, my_object> {
    id_type id;
    account_name_type account;
    uint64_t some_field;
};

// Контейнер MultiIndex
using my_index = chainbase::shared_multi_index_container<
    my_object,
    indexed_by<
        ordered_unique<tag<by_id>, member<my_object, my_object::id_type, &my_object::id>>,
        ordered_unique<tag<by_account>, member<my_object, account_name_type, &my_object::account>>
    >
>;
```

---

## Пользовательские эвалуаторы операций

Для обработки новых типов операций:

```cpp
// Определить операцию в протокольном слое и зарегистрировать эвалуатор
class my_operation_evaluator : public evaluator<my_operation> {
public:
    void do_apply(const my_operation& op) {
        // Валидация и применение изменений состояния
        auto& db = this->db();
        // ...
    }
};

// Регистрация при инициализации БД
db.register_evaluator<my_operation_evaluator>();
```

Используйте проверки `has_hardfork(CHAIN_HARDFORK_N)` для управления изменениями поведения с целью обратной совместимости.

---

## WebSocket события реального времени

Для отправки уведомлений в реальном времени:

```cpp
// При plugin_startup(), зарегистрировать callback блока в веб-сервере
auto& ws = appbase::app().get_plugin<webserver::plugin>();
ws.add_handler("my_stream", [this](const fc::variant& params, fc::variant& result) {
    // Обработчик потока
});
```

Плагин веб-сервера работает в собственном потоке `io_service` — постите callback'и из любого потока с помощью `ws.post([]{...})`.

---

## Объявление зависимостей

Объявляйте зависимости в `plugin_requires()` вашего плагина:

```cpp
static std::vector<appbase::abstract_plugin*> plugin_requires() {
    return { &appbase::app().get_plugin<json_rpc::plugin>(),
             &appbase::app().get_plugin<chain::plugin>() };
}
```

AppBase автоматически разрешает порядок инициализации.

---

## Руководящие принципы производительности

- **API-методы**: Используйте индексированные поиски, а не полное сканирование. Добавляйте индексы плагинов для горячих паттернов доступа.
- **Обработчики сигналов**: Возвращайте управление быстро. Ставьте тяжёлую обработку в очередь выделенного `fc::thread`.
- **Кэширование**: Кэшируйте результаты горячих путей в памяти; инвалидируйте при `applied_block`.
- **Пагинация**: Всегда разбивайте на страницы большие наборы результатов вместо возврата неограниченных коллекций.

---

## Тестирование плагинов

Используйте плагин `debug_node` для симуляции условий в цепочке:

```json
{"method":"debug_node.debug_generate_blocks","params":["5K...",10,0,0,{}]}
```

Пишите модульные тесты с помощью Boost.Test и существующей тестовой оснастки. Добавляйте тесты в соответствующий категорийный набор (`operation_tests`, `block_tests` и т.п.).

Для интеграционных тестов загружайте плагин вместе с цепочкой и воспроизводите известную последовательность блоков с помощью `debug_push_blocks`.

---

## Развёртывание

Включайте плагины в `config.ini`:

```ini
plugin = myplugin
```

Некоторые плагины требуют полного реиндексирования при включении на существующей цепочке (особенно те, которые отслеживают историю операций). Документируйте это требование явно.

Для внешних (сторонних) плагинов помещайте их в `plugins/external/` — CMake обнаруживает их автоматически.

---

См. также: [Обзор плагинов](../plugins/overview.md), [Database API](../plugins/database-api.md), [Сборка](./building.md), [Отладка](./debugging.md).
