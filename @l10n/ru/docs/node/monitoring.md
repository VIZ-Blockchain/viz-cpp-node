# Мониторинг

На этой странице описаны проверки работоспособности, паттерны логов, статистика P2P-пиров и интеграция с внешними стеками мониторинга для узлов VIZ Ledger.

---

## Проверка работоспособности: синхронизация узла

Запросите динамические глобальные свойства узла, чтобы убедиться в его работе и синхронизации:

```bash
curl -s -X POST http://localhost:8090 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"call","params":["database_api","get_dynamic_global_properties",[]],"id":1}' \
  | python3 -m json.tool
```

Проверьте `head_block_number` — он должен увеличиваться каждые 3 секунды при синхронизации. Проверьте `time` — оно должно быть в пределах нескольких секунд от системных часов.

Простой скрипт проверки доступности:

```bash
#!/bin/bash
RESPONSE=$(curl -sf -X POST http://localhost:8090 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"call","params":["database_api","get_dynamic_global_properties",[]],"id":1}')
if [ $? -ne 0 ]; then echo "CRIT: RPC unreachable"; exit 2; fi

HEAD=$(echo "$RESPONSE" | python3 -c "import sys,json; print(json.load(sys.stdin)['result']['head_block_number'])")
echo "OK: head_block_number=$HEAD"
```

---

## Паттерны логов

### Производство блоков (узлы-валидаторы)

```
# Хорошо: слот произведён
produced block #123456 @2025-01-01T00:00:03 validator=alice sz=2048

# Пропущенный слот
MISSED-SLOT-OUR-validator: alice missed slot at 2025-01-01T00:00:06

# Обнаружен minority fork
MINORITY FORK DETECTED: rolling back to LIB #123400

# Сработал watchdog
WATCHDOG: no production for 180s, clearing flags
```

### P2P-подключение

```
# Новый пир подключён
New peer is connected (203.0.113.10:2001), now 8 active peers

# Мягкий бан пира
soft-banning peer 203.0.113.10:2001 for 300s: reason=only_fork_db_blocks_no_progress

# Синхронизация завершена
Sync: peer 203.0.113.10 says we're up-to-date
```

### Снимок и восстановление

```
# Снимок создан
Snapshot created at block 5000000 in 14.2s: /data/snapshots/snapshot-block-5000000.json

# Запущено автовосстановление
shared_memory_corruption_exception detected — starting auto-recovery
Recovery complete. Resumed from block 4999500
```

### Sync-логгер (DLT-режим)

Включите логгер `sync` для просмотра деталей переговоров о синхронизации:

```ini
[logger.sync]
level = info
appenders = stderr
```

Ключевые сообщения:
- `Starting sync with peer ...` — синхронизация начата
- `on_blockchain_item_ids_inventory: ...` — получена партия ID блоков
- `Sync: peer X says we're up-to-date` — синхронизация с этим пиром завершена
- `DEFERRED_RESIZE: sync block #N deferred` — синхронизация блока отложена из-за изменения размера разделяемой памяти
- `auto-clearing stuck peer_needs_sync_items_from_us` — 30-секундная защита сняла зависший флаг

---

## Конфигурация логов

Логи настраиваются в `config.ini`:

```ini
# Вывод в консоль
log.console_appender.stderr.stream = std_error

# P2P лог-файл
log.file_appender.p2p.filename = logs/p2p/p2p.log

# Уровни логирования: all, debug, info, warn, error, off
logger.default.level = warn
logger.default.appenders = stderr

logger.p2p.level = warn
logger.p2p.appenders = p2p
```

> **Примечание:** `node.cpp` направляет все свои вызовы `ilog`/`wlog` в логгер `p2p`. Для просмотра P2P-сообщений настройте уровень логгера `p2p` на `info`.

Ротация логов через `logrotate` (пример `/etc/logrotate.d/vizd`):

```
/data/vizd/logs/p2p/p2p.log {
    daily
    rotate 14
    compress
    missingok
    copytruncate
}
```

---

## Статистика P2P-пиров

Плагин P2P записывает метрики работоспособности пиров каждые 5 минут (настраивается). Включите в `config.ini`:

```ini
p2p-stats-enabled = true
p2p-stats-interval = 300   # секунды
```

Пример вывода в лог:

```
P2P peer | ip: 203.0.113.10  | port: 2001 | latency: 45ms  | bytes_in: 12345 | blocked: false | reason:
P2P peer | ip: 198.51.100.5  | port: 2001 | latency: 120ms | bytes_in: 8765  | blocked: true  | reason: soft_ban
Block storage | dlt_log: [79174319..79274318] | dlt_resizes: 412 | fork_db: linked=18 unlinked=0
```

Поля:
- `latency` — задержка туда-обратно в мс
- `bytes_in` — дельта байт, полученных с последнего измерения
- `blocked` / `reason` — статус мягкого бана или ограничения и причина
- `Block storage` — диапазон DLT block log, счётчик изменений размера, состояние fork_db

Высокое значение `dlt_resizes` в сочетании с уменьшающимся диапазоном `dlt_log` может указывать на срабатывание самовосстановления файла отображения. Пир с `reason: soft_ban` может находиться на форке или отправлять только устаревшие данные.

---

## Prometheus и Grafana

Узел не предоставляет нативный endpoint Prometheus. Используйте [Node Exporter](https://github.com/prometheus/node_exporter) для метрик уровня ОС и опрашивайте JSON-RPC endpoint с помощью пользовательского экспортёра:

```python
# минимальный пример: опрос head_block_number
import requests, time
from prometheus_client import Gauge, start_http_server

g = Gauge('viz_head_block_number', 'Current head block')

def collect():
    r = requests.post('http://localhost:8090', json={
        "jsonrpc": "2.0", "method": "call",
        "params": ["database_api", "get_dynamic_global_properties", []],
        "id": 1
    }, timeout=5)
    g.set(r.json()['result']['head_block_number'])

start_http_server(9100)
while True:
    collect()
    time.sleep(3)
```

**Рекомендуемые панели дашборда:**

| Панель | Метрика / Источник |
|--------|-------------------|
| Головной блок | `viz_head_block_number` (увеличивается каждые 3 с при синхронизации) |
| Отставание блока | `time() - viz_head_block_time` (секунды отставания от системных часов) |
| Количество пиров | Из лога P2P-статистики |
| Задержка пиров | Лог P2P-статистики по IP пира |
| Свободная разделяемая память | `viz_shared_memory_free_mb` из пользовательского экспортёра |
| CPU / RAM | Стандартные метрики Node Exporter |
| Дисковый I/O | `node_disk_*` Node Exporter |

---

## ELK / Централизованное логирование

Перенаправляйте логи узла в центральный коллектор. Пример с Filebeat:

```yaml
# filebeat.yml
filebeat.inputs:
  - type: log
    paths:
      - /data/vizd/logs/p2p/p2p.log
    fields:
      service: vizd
      node: validator-1

output.logstash:
  hosts: ["logstash:5044"]
```

Разбор ключевых полей (grok Logstash или ingest pipeline Elasticsearch):

```
MISSED-SLOT-OUR-validator: %{WORD:validator} missed slot at %{TIMESTAMP_ISO8601:slot_time}
produced block #%{NUMBER:block_num} @%{TIMESTAMP_ISO8601:block_time} validator=%{WORD:producer}
```

---

## Мониторинг, специфичный для валидатора

### Ключевые метрики для оповещений

| Условие | Серьёзность | Действие |
|---------|-------------|---------|
| `MISSED-SLOT-OUR-validator` в логах | Предупреждение | Проверить NTP, задержку сети, нагрузку CPU |
| `MINORITY FORK DETECTED` | Критическое | Проверить P2P-подключение к сид-узлам |
| `WATCHDOG: no production for 180s` | Критическое | Проверить ключ валидатора и работоспособность узла |
| Код результата `no_private_key` | Критическое | Несоответствие ключа подписи — проверить конфигурацию |
| Код результата `low_participation` | Предупреждение | Деградация работоспособности сети |
| Головной блок перестал увеличиваться | Критическое | Узел может быть заблокирован |
| Количество пиров упало до 0 | Критическое | Сетевой раздел или проблема с файрволом |

### Проверка NTP

```bash
chronyc tracking | grep "System time"
# или
timedatectl | grep "NTP synchronized"
```

Плагин validator использует собственный NTP-клиент (настраивается через `ntp-server` в config), но синхронизация системных часов также важна. Дрейф >200мс может вызвать пропуск слотов.

---

## Обслуживание базы данных

### Размер разделяемой памяти

Следите за предупреждениями о нехватке места в логах:

```
chainbase: shared memory low — resizing from 4G to 6G
```

Превентивно настройте параметры роста в `config.ini`:

```ini
shared-file-size = 4G
min-free-shared-file-size = 500M
inc-shared-file-size = 2G
block-num-check-free-size = 1000
```

### Проверка резервных копий снимков

После создания снимка проверьте его корректную загрузку на тестовом узле:

```bash
vizd --create-snapshot /tmp/verify-snap.json --plugin snapshot
# Ожидается: завершается корректно с "Snapshot created at block N"
```

Периодически тестируйте восстановление после сбоя:

```bash
vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot
# Ожидается: импортирует снимок, воспроизводит dlt_block_log, выдаёт "Recovery complete"
```

---

## Чеклист реагирования на инциденты

**Узел не синхронизируется:**
1. Проверьте количество пиров (логи `p2p-stats-enabled` или RPC `get_info`).
2. Убедитесь, что файрвол разрешает TCP-порт 2001 входящий.
3. Проверьте настройки `p2p-seed-node` — попробуйте альтернативные сиды.
4. Ищите записи `soft_ban` в P2P-статистике — узел может находиться на форке.

**Валидатор не производит блоки:**
1. Убедитесь, что `validator` и `private-key` в `config.ini` соответствуют on-chain ключу подписи.
2. Проверьте, не является ли причиной `low_participation` (работоспособность сети).
3. Проверьте синхронизацию NTP.
4. Ищите `MINORITY FORK DETECTED` — узлу может потребоваться ресинхронизация.

**Узел завис / разделяемая память повреждена:**
1. Если `--auto-recover-from-snapshot` включён (по умолчанию) и снимки существуют, узел восстанавливается автоматически — проверьте логи.
2. Ручное восстановление: `vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot`.
3. Если снимков нет: `vizd --replay-blockchain` (требует полный block log; недоступно в DLT-режиме).

**RPC недоступен:**
1. Проверьте привязку `webserver-http-endpoint` — валидаторы по умолчанию используют `127.0.0.1:8090`.
2. Проверьте конфигурацию файрвола или reverse proxy.
3. Убедитесь, что список плагинов включает `webserver json_rpc database_api`.

---

См. также: [Узел-валидатор](./validator-node.md), [Защита валидатора](./validator-guard.md), [Снимки](./snapshot.md), [Конфигурация](./configuration.md).
