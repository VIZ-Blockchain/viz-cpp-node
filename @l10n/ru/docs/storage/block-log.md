# Block Log

VIZ хранит блоки в бинарных лог-файлах. Существуют два варианта:

| Вариант | Файлы | Назначение |
|---------|-------|-----------|
| `block_log` | `block_log` + `block_log.index` | Полная история (архивные узлы) |
| `dlt_block_log` | `dlt_block_log` + `dlt_block_log.index` | Скользящее окно (DLT/snapshot-узлы) |

Оба используют одинаковый формат файла данных; формат индекса незначительно отличается.

---

## Бинарная сериализация (`fc::raw`)

Все данные используют кодировку little-endian.

| Тип | Формат |
|-----|--------|
| `uint8_t` – `uint64_t` | Фиксированная ширина, little-endian |
| `fc::unsigned_int` | Переменная длина (varint): 7 бит данных + 1 бит продолжения на байт |
| `string` | `[varint: длина][байты UTF-8]` |
| `vector<T>` | `[varint: количество][элементы...]` |
| `optional<T>` | `[uint8: 0 или 1][значение если 1]` |
| `static_variant` | `[varint: индекс типа][сериализованное значение]` |

---

## Формат файла данных

`block_log` и `dlt_block_log` используют одинаковый формат:

```
[бинарный блок 1][uint64 LE: позиция блока 1]
[бинарный блок 2][uint64 LE: позиция блока 2]
...
```

Каждая запись = сериализованный `signed_block`, за которым следует его начальное смещение в виде `uint64_t`.

**Чтение head-блока:** перейти к последним 8 байтам, прочитать смещение, перейти туда, десериализовать.

---

## Индексные файлы

### `block_log.index`

Каждая запись — 8-байтовое смещение `uint64_t` в `block_log`.

```
offset = 8 × (block_num − 1)
```

### `dlt_block_log.index`

Начинается с 8-байтового заголовка:

```
[uint64 LE: start_block_num][uint64 LE: смещение start_block_num][...]
```

```
offset = 8 + 8 × (block_num − start_block_num)
```

---

## Структура `signed_block`

```
block_header:
  [20 байт: ID предыдущего блока (ripemd160)]
  [4 байта: метка времени (uint32 Unix seconds)]
  [varint + string: имя аккаунта валидатора]
  [20 байт: transaction_merkle_root (ripemd160)]
  [varint + vector: extensions]

signed_block_header (добавляется):
  [65 байт: validator_signature (1 байт восстановления + 32 r + 32 s)]

signed_block (добавляется):
  [varint + vector<signed_transaction>: транзакции]
```

Номер блока **не хранится напрямую**. Вычисляется как:
```
block_num = num_from_id(previous) + 1
num_from_id = первые 4 байта block_id как uint32_t LE
```
(Genesis: `previous` — все нули → `block_num = 1`.)

---

## Структура `signed_transaction`

```
transaction:
  [2 байта:  ref_block_num  (uint16 LE)]
  [4 байта:  ref_block_prefix (uint32 LE)]
  [4 байта:  expiration (uint32 Unix seconds)]
  [varint + vector<operation>: операции]
  [varint + extensions_type: extensions]

signed_transaction (добавляется):
  [varint + vector<signature_type>: подписи]
```

---

## Сериализация операций

Каждая операция в векторе `operations` — статический вариант:

```
[varint: type_id][поля, специфичные для операции...]
```

Type ID: см. [Обзор операций](../protocol/operations/overview.md).

**Формат ассета на уровне протокола:**
```
[int64: amount][uint64: symbol]
```
Symbol — упакованный `uint64`: байт 0 = знаков после запятой, байты 1–6 = ASCII-имя, байт 7 = 0x00.

| Символ | Hex (LE) |
|--------|----------|
| VIZ (3 знака) | `03 56 49 5A 00 00 00 00` |
| SHARES (6 знаков) | `06 53 48 41 52 45 53 00` |

**Формат публичного ключа на уровне протокола:** 33 сырых байта (сжатый secp256k1): `[0x02 или 0x03][32 байта x]`.

---

## Расширения заголовка блока

| Индекс | Тип | Данные |
|--------|-----|--------|
| 0 | `void_t` | (нет) |
| 1 | `version` | `uint32_t` номер версии (major 8 \| hf 8 \| release 16 бит) |
| 2 | `hardfork_version_vote` | `uint32_t` hf_version + `uint32_t` hf_time |

---

## Скользящее окно `dlt_block_log`

DLT-лог хранит только недавнее окно блоков; более старые блоки обрезаются. Начинается с `start_block_num > 1`. Узлы, использующие снапшоты, используют этот файл для восстановления после сбоя (реплей из снапшота + dlt_block_log).

---

## Просмотрщик block log

Терминальный просмотрщик block log лежит в `programs/util/block-log/` (Node.js, без зависимостей):

```
node programs/util/block-log/block-log-viewer.cjs <path> [--dlt]
```

Основные команды: `f` — первый, `l` — последний, `n`/`p` — следующий/предыдущий, `g <N>` — перейти к блоку N, `o` — показать операции, `s <type>` — поиск по типу операции, `S <str>` — поиск по содержимому, `scan` — построить битовую маску для быстрой навигации.

Команда `scan` создаёт файл битовой маски (`block_log.bitmask`), отмечающий блоки с непустыми операциями, что позволяет мгновенно перемещаться по `N`/`P`.

---

## Архив блоков

`dlt_block_log` хранит только скользящее окно. Чтобы сохранять все необратимые блоки, включите неконсенсусный плагин `block_archive`:

```
plugin = block_archive
block-archive-dir = block-archive     # относительно data dir
block-archive-range = 10000           # блоков в файле
```

Плагин пишет `<dir>/blocks-<first>-<last>.log` (+ `.log.index`, формат `dlt_block_log`), по файлу на диапазон. Недописанный диапазон лежит в `<dir>/partial/` и переносится наверх, когда его последний блок стал необратимым, поэтому файлы в корне папки полные и больше не меняются. У ноды, запущенной со снапшота, первый файл короче: `<first>` — первый реально сохранённый блок. Ошибка останавливает архив с записью в лог и никогда не влияет на обработку блоков.

`block-archive.cjs` читает всю папку (или один `dlt_block_log`):

```
node programs/util/block-log/block-archive.cjs info   <dir>
node programs/util/block-log/block-archive.cjs get    <dir> <block>
node programs/util/block-log/block-archive.cjs search <dir> --op=pm_place_bet --account=alice --from=N --to=N
node programs/util/block-log/block-archive.cjs export <dir> --from=N --to=N [--blocks] --out=ops.jsonl
```

`search` и `export` выводят по JSON-строке на операцию (`block`, `timestamp`, `tx`, `op_in_tx`, `type`, `data`); `--blocks` экспортирует блоки целиком. `--account` совпадает с любым строковым полем, равным имени, `--text` — с подстрокой JSON операции. Операции 64+ разбираются по `op-layouts.json`; после добавления операции в ноду и viz-js-lib перегенерируйте его через `gen-op-layouts.cjs`.

---

См. также: [Разделяемая память](./shared-memory.md), [Снапшоты](./snapshots.md), [Плагин chain](../plugins/chain.md).
