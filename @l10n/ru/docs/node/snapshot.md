# Снимки (Snapshots)

Плагин snapshot обеспечивает почти мгновенный старт узла путём сериализации полного состояния блокчейна в JSON-файл. Вместо воспроизведения миллионов блоков из block log узел загружает снимок и синхронизирует только блоки, произведённые после его создания.

Узлы, загруженные из снимка, работают в **DLT-режиме**: основной block log остаётся пустым, а компактный **скользящий DLT block log** (`dlt_block_log`) хранит последние необратимые блоки.

---

## Краткий справочник

| Задача | Команда / Конфигурация |
|--------|----------------------|
| Создать снимок один раз (остановить узел) | `vizd --create-snapshot /path/snap.json --plugin snapshot` |
| Создать снимок на блоке N | `snapshot-at-block = N` + `snapshot-dir = /path` |
| Создавать снимок каждые N блоков | `snapshot-every-n-blocks = N` + `snapshot-dir = /path` |
| Развернуть новый узел из файла | `vizd --snapshot /path/snap.json --plugin snapshot` |
| Восстановление после сбоя | `vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot` |
| Автоматическое развёртывание через P2P | `sync-snapshot-from-trusted-peer = true` + `trusted-snapshot-peer = host:8092` |

---

## Подключение плагина

```ini
plugin = snapshot
```

---

## Справочник по конфигурации

### Флаги только для CLI

| Флаг | Описание |
|------|----------|
| `--snapshot <path>` | Развернуть из файла снимка (DLT-режим). Пропускается, если `shared_memory.bin` уже существует или файл уже был импортирован (переименован в `.used`). |
| `--snapshot-auto-latest` | Автоматически определить последний снимок в `snapshot-dir` по номерам блоков в именах файлов. Используется с `--replay-from-snapshot`. Игнорируется при наличии `--snapshot`. |
| `--replay-from-snapshot` | Восстановление после сбоя: всегда очищает разделяемую память, импортирует снимок, воспроизводит `dlt_block_log`. Не переименовывает файл снимка. Требует `--snapshot` или `--snapshot-auto-latest`. |
| `--auto-recover-from-snapshot` | (по умолчанию: `true`) Автоматическое восстановление при обнаружении повреждения разделяемой памяти — без перезапуска. Отключается через `--no-auto-recover-from-snapshot`. |
| `--create-snapshot <path>` | Создать снимок по указанному пути из текущей базы данных, затем выйти. |
| `--sync-snapshot-from-trusted-peer` | (по умолчанию: `false`) Загрузить снимок от доверенных пиров при пустом состоянии. Требует явного включения. |

### Опции конфигурационного файла

| Опция | По умолчанию | Описание |
|-------|--------------|----------|
| `snapshot-at-block` | `0` | Создать снимок при достижении указанного номера блока (0 = отключено). |
| `snapshot-every-n-blocks` | `0` | Создавать снимок каждые N блоков (0 = отключено). Срабатывает только на живых блоках — пропускается при P2P-синхронизации. |
| `snapshot-dir` | — | Каталог для автоматически генерируемых снимков. Создаётся автоматически при отсутствии. |
| `snapshot-max-age-days` | `90` | Удалять снимки старше N дней после создания нового (0 = отключено). |
| `allow-snapshot-serving` | `false` | Раздавать снимки другим узлам по TCP. |
| `allow-snapshot-serving-only-trusted` | `false` | Ограничить раздачу только доверенными пирами. |
| `snapshot-serve-endpoint` | `0.0.0.0:8092` | TCP-endpoint для сервера снимков. |
| `trusted-snapshot-peer` | — | Адрес доверенного пира для P2P-синхронизации снимков (повторяемый). |
| `dlt-block-log-max-blocks` | `100000` | Размер скользящего block log в DLT-режиме (опция плагина chain). 0 = отключено. |

---

## Создание снимков

### Метод 1: Одноразовый (остановить узел, создать файл, выйти)

Сначала остановите работающий узел, затем:

```bash
vizd --create-snapshot /data/snapshots/viz-snapshot.json --plugin snapshot
```

Узел открывает существующую базу данных, при необходимости выполняет воспроизведение, записывает снимок и завершается до активации P2P или валидаторных плагинов.

### Метод 2: Снимок на конкретном блоке (без простоя)

```ini
plugin = snapshot
snapshot-at-block = 5000000
snapshot-dir = /data/snapshots
```

При применении блока 5 000 000 снимок записывается в `/data/snapshots/snapshot-block-5000000.json` без остановки узла.

### Метод 3: Периодические снимки (рекомендуется для продакшена)

```ini
plugin = snapshot
snapshot-every-n-blocks = 28800   # ~24 часа при 3 с/блок
snapshot-dir = /data/snapshots
snapshot-max-age-days = 90
```

Файлы автоматически именуются `snapshot-block-<N>.json`. Создание снимка асинхронно:

- **Фаза 1** (read lock, ~1 с): сериализация всех объектов базы данных в память.
- **Фаза 2** (без блокировки, ~2 с): сжатие, контрольная сумма, запись на диск.

Обработка блоков приостанавливается только во время Фазы 1; API и P2P-чтения не затрагиваются на всём протяжении.

**Рекомендуемые интервалы:**

| Частота | Блоки | Примерное время |
|---------|-------|----------------|
| Частые | 10 000 | ~8 часов |
| Ежедневно | 28 800 | ~24 часа |
| Еженедельно | 100 000 | ~3,5 дня |

### Метод 4: Совмещение at-block и периодических

Оба параметра могут быть активны одновременно:

```ini
snapshot-at-block = 5000000
snapshot-every-n-blocks = 100000
snapshot-dir = /data/snapshots
```

---

## Развёртывание: загрузка из снимка (DLT-режим)

Перенесите файл снимка на новый узел, затем:

```bash
vizd \
  --snapshot /data/snapshots/viz-snapshot.json \
  --plugin snapshot \
  --plugin p2p \
  --p2p-seed-node seed1.viz.world:2001
```

Узел загружает состояние за секунды и начинает P2P-синхронизацию с высоты блока снимка.

### Что происходит при загрузке

1. `chain::plugin_startup()` обнаруживает `--snapshot`.
2. Три проверки безопасности (по порядку): разделяемая память уже существует → пропустить; файл не найден (уже `.used`) → пропустить; иначе продолжить.
3. База данных открывается через `open_from_snapshot()` (очищает и переинициализирует chainbase).
4. Снимок JSON проверяется (версия формата, chain ID, SHA-256 контрольная сумма), все 32 типа объектов импортируются.
5. Файл снимка переименовывается в `.used` для предотвращения повторного импорта при перезапуске.
6. LIB продвигается до `head_block_num`, чтобы P2P-synopsis начинался с головы снимка.
7. Fork database заполняется головным блоком снимка.
8. Плагин P2P запускается и синхронизирует блоки начиная с `LIB + 1`.

### Безопасность перезапуска

| Сценарий | Результат |
|----------|-----------|
| Первый запуск (нет `shared_memory.bin`, файл присутствует) | Импортирует снимок, переименовывает в `.used` |
| Перезапуск (shared_memory существует) | Пропускает импорт — использует существующее состояние |
| Перезапуск (shared_memory очищена, файл уже `.used`) | Пропускает импорт — файл не найден |
| Принудительный повторный импорт | `--resync-blockchain` + новый файл снимка |

Нет необходимости убирать `--snapshot` из командной строки или Docker `VIZD_EXTRA_OPTS` после первоначального импорта.

---

## Формат файла снимка

Снимок — это единый JSON-файл:

```json
{
  "header": {
    "version": 1,
    "chain_id": "...",
    "snapshot_block_num": 12345678,
    "snapshot_block_id": "...",
    "snapshot_block_time": "2025-01-01T00:00:00",
    "last_irreversible_block_num": 12345660,
    "payload_checksum": "sha256...",
    "object_counts": { "account": 50000, ... }
  },
  "state": {
    "dynamic_global_property": [ ... ],
    "account": [ ... ],
    ...
  }
}
```

### 32 включённых типа объектов

**Критические (11)** — обязательные для консенсуса:
`dynamic_global_property`, `witness_schedule`, `hardfork_property`, `account`, `account_authority`, `validator`, `witness_vote`, `block_summary`, `content`, `content_vote`, `block_post_validation`

**Важные (15)** — необходимые для полноценной работы:
`transaction`, `vesting_delegation`, `vesting_delegation_expiration`, `fix_vesting_delegation`, `withdraw_vesting_route`, `escrow`, `proposal`, `required_approval`, `committee_request`, `committee_vote`, `invite`, `award_shares_expire`, `paid_subscription`, `paid_subscribe`, `witness_penalty_expire`

**Дополнительные (5)** — метаданные и восстановление:
`content_type`, `account_metadata`, `master_authority_history`, `account_recovery_request`, `change_recovery_account_request`

---

## DLT-скользящий block log

В DLT-режиме основной `block_log` остаётся пустым. `dlt_block_log` (файлы `dlt_block_log.log` + `dlt_block_log.index`) хранит последние необратимые блоки для:

- **P2P-раздачи блоков** — пиры могут запрашивать последние блоки для разрешения fork.
- **API-доступа** — `get_block` работает для блоков в скользящем окне.

```ini
dlt-block-log-max-blocks = 100000   # хранить последние ~3,5 дня блоков
```

Когда лог превышает этот размер, старые блоки удаляются с начала. Реализация отслеживает логические размеры файлов независимо от файла, отображаемого в память, для предотвращения ошибок с устаревшим размером после тысяч циклов изменения размера.

---

## Восстановление после сбоя: `--replay-from-snapshot`

Используйте этот режим, когда `shared_memory.bin` повреждён (некорректное завершение, диск заполнен, аппаратный сбой). Обычный `--replay-blockchain` недоступен в DLT-режиме, поскольку `block_log` пуст.

```bash
# Указать путь к снимку явно
vizd --replay-from-snapshot --snapshot /data/snapshots/snapshot-block-79273800.json --plugin snapshot

# Или автоматически определить последний снимок
vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot
```

Шаги восстановления:
1. Всегда очищает `shared_memory.bin` (предполагает повреждение).
2. Импортирует состояние снимка.
3. Воспроизводит `dlt_block_log` начиная с `snapshot_head + 1`.
4. Генерирует `on_sync` — P2P заполняет оставшийся разрыв до головы живой цепочки.

Файл снимка **не** переименовывается в `.used` (может потребоваться снова).

### Пример сценария восстановления

DLT-узел падает на блоке 79 274 318 при `snapshot-every-n-blocks = 100000` и `dlt-block-log-max-blocks = 100000`:

```
/data/viz-snapshots/snapshot-block-79273800.json   ← последний снимок
/blockchain/dlt_block_log.*                         ← содержит блоки 79174319..79274318
/blockchain/shared_memory.bin                       ← ПОВРЕЖДЁН
```

```bash
vizd --replay-from-snapshot --snapshot-auto-latest --plugin snapshot
```

```
Loading state from snapshot: .../snapshot-block-79273800.json  (12.3 с)
Replaying dlt_block_log from block 79273801 to 79274318...
  100%  518 of 518  (block 79274318, elapsed 7.2 с)
Recovery complete. Started on blockchain with 79274318 blocks.
```

Узел теперь на блоке 79 274 318; P2P-синхронизация доставляет остальное.

---

## Автоматическое восстановление в реальном времени: `--auto-recover-from-snapshot`

Включено по умолчанию (`true`). При обнаружении повреждения во время обработки или генерации блоков в реальном времени узел:

1. Находит последний снимок в `snapshot-dir`.
2. Закрывает базу данных.
3. Очищает и повторно импортирует по тому же пути, что и `--replay-from-snapshot`.
4. Возобновляет P2P-синхронизацию — перезапуск не требуется.

**Предварительные условия:** плагин `snapshot` включён и снимки присутствуют в `snapshot-dir`.

Для отключения (при отладке):

```bash
vizd --no-auto-recover-from-snapshot
```

---

## P2P-синхронизация снимков

Узлы могут загружать снимки от доверенных пиров по пользовательскому TCP-протоколу, исключая ручную передачу файлов.

### Сервер снимков

```ini
plugin = snapshot
allow-snapshot-serving = true
snapshot-serve-endpoint = 0.0.0.0:8092
snapshot-every-n-blocks = 28800
snapshot-dir = /data/viz-snapshots
```

### Развёртывание нового узла

```ini
plugin = snapshot
trusted-snapshot-peer = seed1.viz.world:8092
trusted-snapshot-peer = seed2.viz.world:8092
trusted-snapshot-peer = seed3.viz.world:8092
sync-snapshot-from-trusted-peer = true
```

Когда узел стартует с 0 блоков и `sync-snapshot-from-trusted-peer = true`, он запрашивает все доверенные пиры, выбирает пир с наибольшим снимком, загружает его блоками по 1 МБ, проверяет SHA-256 контрольную сумму и импортирует — всё до активации P2P или валидаторных плагинов.

### Безопасность

- Загрузки свыше 2 ГБ отклоняются.
- Контрольная сумма проверяется через потоковый SHA-256 (никогда не загружается полностью в память).
- Ограничение скорости, максимум 5 одновременных соединений, 60-секундный дедлайн на соединение.
- `allow-snapshot-serving-only-trusted = true` ограничивает доступ списком `trusted-snapshot-peer`.

---

## Docker

Используйте `VIZD_EXTRA_OPTS` для передачи флагов снимков:

```bash
# Развернуть из снимка
docker run -e VIZD_EXTRA_OPTS="--snapshot /var/lib/vizd/snapshots/snap.json --plugin snapshot" ...

# Восстановление после сбоя
docker run -e VIZD_EXTRA_OPTS="--replay-from-snapshot --snapshot-auto-latest --plugin snapshot" ...
```

Периодические снимки через `config.ini` (без `VIZD_EXTRA_OPTS`):

```ini
plugin = snapshot
snapshot-every-n-blocks = 28800
snapshot-dir = /var/lib/vizd/snapshots
```

Файлы снимков доступны на хосте по пути смонтированного тома.

| Задача | Метод |
|--------|-------|
| Периодические снимки | `snapshot-every-n-blocks` в config |
| Одноразовый снимок | `--create-snapshot` через `VIZD_EXTRA_OPTS` |
| Развёртывание нового узла | `--snapshot` через `VIZD_EXTRA_OPTS` |
| Восстановление после сбоя | `--replay-from-snapshot --snapshot-auto-latest` через `VIZD_EXTRA_OPTS` |
| Авто-восстановление | По умолчанию — убедитесь, что `plugin = snapshot` и `snapshot-dir` заданы |
| P2P-авторазвёртывание | `sync-snapshot-from-trusted-peer = true` + `trusted-snapshot-peer` в config |

---

## Рекомендуемая конфигурация для продакшена

```ini
plugin = snapshot

# Снимок каждые ~24 часа
snapshot-every-n-blocks = 28800
snapshot-dir = /data/viz-snapshots
snapshot-max-age-days = 90

# DLT-скользящий block log: хранить последние ~3,5 дня
dlt-block-log-max-blocks = 100000

shared-file-size = 4G
plugin = p2p
p2p-seed-node = seed1.viz.world:2001
```

---

## Обнаружение устаревшего снимка

Если последний снимок раздающего узла старше начального блока `dlt_block_log`, новые узлы, загружающие снимок, не смогут синхронизировать недостающие блоки. При запуске плагин обнаруживает это и автоматически создаёт свежий снимок на следующем живом блоке — ручного вмешательства не требуется.

---

## Устранение неполадок

| Проблема | Проверьте |
|----------|-----------|
| Узел повторно импортирует снимок при каждом перезапуске | Файл снимка не переименовывается в `.used` — проверьте права записи в каталог снимков |
| `item_not_available` от пиров | DLT block log может не охватывать рекламируемые блоки — убедитесь, что `dlt-block-log-max-blocks` достаточно велик |
| P2P-синхронизация зависает после загрузки снимка | Проверьте `[logger.sync]` в config; убедитесь, что LIB был продвинут до головы после импорта |
| Создание снимка не удаётся | Проверьте место на диске в `snapshot-dir`; при сбое узел продолжает работу |
| Авто-восстановление срабатывает неожиданно | Проверьте ошибки диска; изучите логи на наличие `shared_memory_corruption_exception` |
| P2P-загрузка отклонена (>2 ГБ) | Снимок слишком большой — увеличьте `dlt-block-log-max-blocks` на раздающем узле для уменьшения размера снимка |

---

См. также: [Плагин Snapshot](../plugins/snapshot.md) — полная справка по реализации; [P2P-обзор](../p2p/overview.md) — детали DLT-протокола синхронизации.
