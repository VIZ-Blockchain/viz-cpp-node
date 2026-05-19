# Конфигурация узла

Узлы VIZ Ledger настраиваются через INI-файл. В репозитории поставляется несколько шаблонов в `share/vizd/config/`:

| Шаблон | Назначение |
|--------|-----------|
| `config.ini` | Полный узел основной сети с публичным RPC |
| `config_witness.ini` | Узел-валидатор (RPC на localhost, производство блоков) |
| `config_testnet.ini` | Тестовая сеть / разработка |
| `config_mongo.ini` | Узел с бэкендом истории MongoDB |
| `config_lowmem.ini` | Маломощный консенсусный/сид-узел |
| `config_stock_exchange.ini` | Потребитель рыночных данных (минимум плагинов) |
| `config_debug.ini` | Режим отладки |

---

## Сеть и P2P

```ini
# Адрес прослушивания P2P-соединений (стандартный порт 2001)
p2p-endpoint = 0.0.0.0:2001

# Максимальное количество подключений к пирам (без ограничений, если не задано)
p2p-max-connections = 200

# Начальные узлы для установки соединений (повторяемый параметр)
p2p-seed-node = seed1.viz.world:2001
p2p-seed-node = seed2.viz.world:2001

# Контрольные точки: доверенные пары (block_num, block_id) (повторяемый параметр)
# checkpoint = [12345,"0003039..." ]
```

---

## Веб-сервер и RPC

```ini
# HTTP JSON-RPC эндпоинт
webserver-http-endpoint = 0.0.0.0:8090

# WebSocket JSON-RPC эндпоинт
webserver-ws-endpoint = 0.0.0.0:8091

# Размер пула потоков RPC
webserver-thread-pool-size = 2
```

> **Примечание по безопасности:** Для узлов-валидаторов привяжите к `127.0.0.1`, чтобы заблокировать внешний доступ:
> ```ini
> webserver-http-endpoint = 127.0.0.1:8090
> webserver-ws-endpoint   = 127.0.0.1:8091
> ```

---

## Блокировки и параллелизм

```ini
# Время ожидания блокировки чтения в микросекундах перед повторной попыткой
read-wait-micro = 500000

# Максимальное количество повторных попыток блокировки чтения
max-read-wait-retries = 2

# Время ожидания блокировки записи в микросекундах перед повторной попыткой
write-wait-micro = 500000

# Максимальное количество повторных попыток блокировки записи
max-write-wait-retries = 3

# Сериализовать все операции записи в одном потоке (рекомендуется)
single-write-thread = true

# Запускать уведомления плагинов при push_transaction (увеличивает задержку; по умолчанию false)
enable-plugins-on-push-transaction = false
```

---

## Разделяемая память (база данных)

Состояние блокчейна хранится в файле с отображением в память (`shared_memory.bin`).

```ini
# Начальный размер файла разделяемой памяти
shared-file-size = 4G

# Минимальный свободный объём перед изменением размера
min-free-shared-file-size = 500M

# Объём увеличения файла при изменении размера
inc-shared-file-size = 2G

# Проверять свободное место каждые N блоков
block-num-check-free-size = 1000
```

Подбирайте `shared-file-size` в зависимости от размера цепочки. Для основной сети начните с `4G` и следите за ростом.

---

## Активация плагинов

```ini
# Каждая строка 'plugin' добавляет плагин (повторяемый параметр)
# Минимальный набор для полного API-узла:
plugin = chain p2p webserver json_rpc database_api network_broadcast_api

# Дополнительные плагины индексирования (отключить на маломощных узлах):
plugin = social_network tags follow account_history account_by_key
plugin = committee_api invite_api paid_subscription_api custom_protocol_api

# Только для узлов-валидаторов:
plugin = validator witness_api
```

### Наборы плагинов по типу узла

| Тип узла | Плагины |
|---------|--------|
| Полный узел | Все перечисленные выше |
| Валидатор | `chain p2p webserver json_rpc database_api network_broadcast_api validator witness_api` |
| Маломощный сид | `chain p2p` |
| Биржа | `chain p2p webserver json_rpc database_api network_broadcast_api account_history` |

---

## История и отслеживание

```ini
# Удалять объекты голосов до данного блока (экономит память; 0 = хранить всё)
clear-votes-before-block = 0

# Пропускать индексирование виртуальных операций (экономит память у валидаторов)
skip-virtual-ops = false

# Индексировать историю аккаунтов только для диапазона (опционально)
# track-account-range = ["alice","alice.zzz"]

# Белый/чёрный список типов операций для истории
# history-whitelist-ops = transfer_operation
# history-blacklist-ops = custom_operation

# Начать индексирование истории с данного номера блока
# history-start-block = 1000000

# Максимальное количество записей в ленте аккаунта (плагин follow)
follow-max-feed-size = 500

# Диапазон отслеживания личных сообщений (опционально)
# pm-account-range = ["alice","alice.zzz"]
```

---

## Валидатор (производство блоков)

На не-валидаторных узлах эти параметры не задаются.

```ini
# Разрешить производство при устаревшей цепочке (только для разработки/тестовой сети)
enable-stale-production = false

# Минимальный процент участия для производства блоков (0–99)
required-participation = 33

# Имя аккаунта валидатора (повторяемый параметр для нескольких валидаторов на одном узле)
validator = alice

# WIF-ключ подписи валидатора
private-key = 5JxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxWIF

# Приватный ключ экстренного консенсуса (опционально)
# emergency-private-key = 5Jxxx...
```

Полная конфигурация валидатора — в разделе [Узел-валидатор](./validator-node.md).

---

## Логирование

```ini
# Консольный логгер (вывод в stderr)
log.console_appender.stderr.stream = std_error

# Файловый логгер для P2P-сообщений
log.file_appender.p2p.filename = logs/p2p/p2p.log

# Уровень логирования: all, debug, info, warn, error, off
logger.default.level = warn
logger.default.appenders = stderr

logger.p2p.level = warn
logger.p2p.appenders = p2p
```

---

## MongoDB (опционально)

Применимо только при сборке с `ENABLE_MONGO_PLUGIN=ON`:

```ini
plugin = mongo_db
mongodb-uri = mongodb://localhost:27017/vizd
```

---

## Полный справочник

Все параметры по исходным файлам:

| Источник | Параметры |
|---------|----------|
| `plugins/chain/plugin.hpp` | `shared-file-size`, `min-free-shared-file-size`, `inc-shared-file-size`, `block-num-check-free-size`, `single-write-thread`, `enable-plugins-on-push-transaction`, `read-wait-micro`, `max-read-wait-retries`, `write-wait-micro`, `max-write-wait-retries`, `skip-virtual-ops`, `clear-votes-before-block`, `track-account-range`, `history-whitelist-ops`, `history-blacklist-ops`, `history-start-block` |
| `plugins/p2p/p2p_plugin.hpp` | `p2p-endpoint`, `p2p-max-connections`, `p2p-seed-node`, `checkpoint` |
| `plugins/webserver/webserver_plugin.hpp` | `webserver-http-endpoint`, `webserver-ws-endpoint`, `webserver-thread-pool-size` |
| `plugins/validator/validator.hpp` | `enable-stale-production`, `required-participation`, `validator`, `private-key`, `emergency-private-key`, `fork-collision-timeout-blocks`, `ntp-server`, `ntp-request-interval`, `debug-block-production` |
| `plugins/follow/` | `follow-max-feed-size` |
| `plugins/private_message/` | `pm-account-range` |
