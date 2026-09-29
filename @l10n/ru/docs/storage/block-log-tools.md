# Инструменты лога блоков

Node.js-скрипты в `programs/util/block-log/` читают файлы блоков офлайн: полный `block_log`, `dlt_block_log` DLT-ноды и файлы-диапазоны [плагина архива блоков](../plugins/block-archive.md). Зависимостей нет — достаточно Node.js 18+. У скриптов расширение `.cjs`, потому что корень репозитория объявлен ES-модулем.

| Файл | Назначение |
|------|------------|
| `block-archive.cjs` | CLI: сводка по архиву, один блок, поиск, экспорт в JSONL |
| `block-log-viewer.cjs` | Интерактивный терминальный просмотрщик одного файла блоков |
| `block-log-reader.cjs` | Библиотека-парсер для обоих (`BlockLogReader`, `DltBlockLogReader`, `readSignedBlock`) |
| `op-layouts.json` | Раскладки полей операций 64+ |
| `gen-op-layouts.cjs` | Перегенерирует `op-layouts.json` из viz-js-lib |

---

## `block-archive.cjs`

```
node block-archive.cjs <команда> <цель> [опции]
```

`<цель>` — папка архива, data dir ноды с `dlt_block_log` или один файл `.log` / `dlt_block_log`.

### `info`

Показывает файлы-диапазоны, их блоки и количество, сообщает о дырах:

```
$ node block-archive.cjs info /var/lib/vizd/block-archive
blocks-0083792402-0083792499.log  83792402-83792499  98 blocks
blocks-0083792500-0083792599.log  83792500-83792599  100 blocks
…
total 83792402-83792999
```

Строка `GAP a-b` значит, что блоков `a..b` нет (см. [Ошибки](../plugins/block-archive.md#ошибки)).

### `get`

Печатает один блок в JSON:

```bash
node block-archive.cjs get /var/lib/vizd/block-archive 83792500
```

### `search`

Печатает по JSON-строке на каждую подходящую операцию:

```bash
node block-archive.cjs search /var/lib/vizd/block-archive --op=pm_place_bet,transfer --account=alice --from=83790000 --to=83799999
```

```json
{"block":83792517,"timestamp":"2026-09-28T21:14:03.000Z","tx":0,"op_in_tx":0,"type":"transfer","virtual":false,"data":{"from":"alice","to":"bob","amount":{…},"memo":"…"}}
```

### `export`

Те же фильтры, вывод в файл или stdout в формате JSONL. По умолчанию — операции; `--blocks` выгружает блоки целиком (с фильтрами — только блоки, где есть подходящая операция):

```bash
node block-archive.cjs export /var/lib/vizd/block-archive --from=83790000 --to=83799999 --out=ops.jsonl
node block-archive.cjs export /var/lib/vizd/block-archive --blocks --out=blocks.jsonl
```

Число записанных записей уходит в stderr, так что stdout остаётся чистым для пайпов.

### Фильтры

| Опция | Значение |
|-------|----------|
| `--from=N`, `--to=N` | Диапазон блоков (по умолчанию — всё, что есть в цели) |
| `--op=a,b` | Имена операций без суффикса `_operation` |
| `--account=name` | Любое строковое поле операции, равное имени (`from`, `to`, `account`, `author`, `creator`, …) |
| `--text=s` | Подстрока JSON операции |
| `--out=file` | Файл вывода для `search` / `export` |
| `--blocks` | `export` блоков целиком вместо операций |

Блоки читаются последовательно, открываются только файлы, пересекающиеся с `--from/--to`, поэтому на больших архивах сужайте диапазон. Ориентир скорости: около 4 500 блоков в секунду (50 000 блоков тестнета за 11 с).

---

## `block-log-viewer.cjs`

Интерактивный просмотрщик одного файла:

```bash
node block-log-viewer.cjs /var/lib/vizd/blockchain/block_log
node block-log-viewer.cjs /var/lib/vizd/blockchain/dlt_block_log --dlt
node block-log-viewer.cjs /var/lib/vizd/block-archive/blocks-0083790000-0083799999.log --dlt
```

Файлы-диапазоны архива — в формате DLT, открывайте их с `--dlt`.

---

## Покрытие операций

Операции 0–63 разбираются вручную написанными читателями. Операции 64+ (прогнозные рынки, агент-доступ, `set_reward_sharing`, …) — по `op-layouts.json`, который генерируется из сериализаторов viz-js-lib, побайтно сверенных с нодой. После добавления новой операции в ноду и viz-js-lib перегенерируйте файл:

```bash
node programs/util/block-log/gen-op-layouts.cjs ../viz-js-lib/src/auth/serializer/src/operations.js
```

Неизвестный id операции делает нечитаемым весь блок — поток байт нельзя безопасно пропустить, — поэтому держите `op-layouts.json` в синхроне с нодой.

Поля заголовка блока — как в JSON ноды: `validator`, `validator_signature`.
