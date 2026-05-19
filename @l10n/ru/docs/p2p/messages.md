# Справочник P2P-сообщений

DLT P2P использует бинарный протокол через raw TCP. Каждое сообщение оформляется 4-байтовым заголовком с порядком байт little-endian, содержащим ID типа сообщения, за которым следует полезная нагрузка с префиксом длины, сериализованная с помощью FC reflection.

**Заголовочный файл:** [libraries/network/include/graphene/network/dlt_p2p_messages.hpp](../../libraries/network/include/graphene/network/dlt_p2p_messages.hpp)

---

## Сводка типов сообщений

| ID типа | Имя | Направление | Назначение |
|---------|-----|------------|-----------|
| 5100 | `dlt_hello_message` | инициатор → принимающий | Начальное рукопожатие — состояние цепочки и возможности |
| 5101 | `dlt_hello_reply_message` | принимающий → инициатор | Ответ на рукопожатие — выравнивание форков, статус обмена |
| 5102 | `dlt_range_request_message` | любое | Запросить у пира наличие конкретного блока |
| 5103 | `dlt_range_reply_message` | любое | Ответ: доступный диапазон блоков |
| 5104 | `dlt_get_block_range_message` | синхр. → пир | Запрос диапазона блоков (массовая синхронизация) |
| 5105 | `dlt_block_range_reply_message` | пир → синхр. | Массовая доставка блоков |
| 5106 | `dlt_get_block_message` | любое | Запрос одного блока |
| 5107 | `dlt_block_reply_message` | любое | Доставка одного блока |
| 5108 | `dlt_not_available_message` | любое | Запрошенный блок недоступен |
| 5109 | `dlt_fork_status_message` | любое | Обновление состояния живой цепочки (голова, LIB, форк, DLT-диапазон) |
| 5110 | `dlt_peer_exchange_request` | любое | Запросить список адресов пиров |
| 5111 | `dlt_peer_exchange_reply` | любое | Ответ со списком адресов пиров |
| 5112 | `dlt_peer_exchange_rate_limited` | любое | Ответ с ограничением скорости на запрос обмена пирами |
| 5113 | `dlt_transaction_message` | любое | Трансляция транзакции |
| 5114 | `dlt_soft_ban_message` | любое → забаненный | Уведомление о мягком бане перед отключением |
| 5115 | `dlt_gap_fill_request` | любое | Запрос конкретных блоков для заполнения разрыва |
| 5116 | `dlt_gap_fill_reply` | любое | Доставка блоков для заполнения разрыва |

---

## Перечисления

### `dlt_node_status`

| Значение | Значение |
|---------|---------|
| `DLT_NODE_STATUS_SYNC` (0) | Узел отстаёт; активно вытягивает блоки от пиров |
| `DLT_NODE_STATUS_FORWARD` (1) | Узел обновлён; обменивается блоками через трансляцию |

### `dlt_fork_status`

| Значение | Значение |
|---------|---------|
| `DLT_FORK_STATUS_NORMAL` (0) | На форке большинства |
| `DLT_FORK_STATUS_LOOKING_RESOLUTION` (1) | Форк обнаружен; выполняется алгоритм разрешения |
| `DLT_FORK_STATUS_MINORITY` (2) | Подтверждён на minority fork |

### `dlt_peer_lifecycle_state`

| Значение | Значение |
|---------|---------|
| `DLT_PEER_LIFECYCLE_CONNECTING` (0) | TCP-соединение в процессе (таймаут 5 с) |
| `DLT_PEER_LIFECYCLE_HANDSHAKING` (1) | Обмен hello в процессе (таймаут 10 с) |
| `DLT_PEER_LIFECYCLE_SYNCING` (2) | Обмен блоками синхронизации (`we_need_sync_items` или пир нуждается в блоках от нас) |
| `DLT_PEER_LIFECYCLE_ACTIVE` (3) | Полностью синхронизирован; нормальный обмен блоками/транзакциями |
| `DLT_PEER_LIFECYCLE_DISCONNECTED` (4) | Не подключён; допустим для переподключения после отката |
| `DLT_PEER_LIFECYCLE_BANNED` (5) | Мягко забанен; нет переподключения до истечения бана |

---

## Подробный справочник сообщений

### 5100 — `dlt_hello_message`

Отправляется немедленно после установки TCP-соединения. Содержит полное состояние цепочки и возможности инициирующего узла.

```cpp
struct dlt_hello_message {
    uint16_t      protocol_version;    // в настоящее время 1
    block_id_type head_block_id;
    uint32_t      head_block_num;
    block_id_type lib_block_id;
    uint32_t      lib_block_num;
    uint32_t      dlt_earliest_block;  // старейший блок в нашем скользящем DLT log (0 если нет)
    uint32_t      dlt_latest_block;    // последний блок в нашем DLT log
    bool          emergency_active;    // экстренный консенсус активен в настоящее время
    bool          has_emergency_key;   // мы держим приватный ключ экстренного committee
    uint8_t       fork_status;         // перечисление dlt_fork_status
    uint8_t       node_status;         // перечисление dlt_node_status (SYNC или FORWARD)
};
```

**Примечания:**
- `dlt_earliest_block` критически важен для выравнивания форков с учётом DLT-диапазона. Принимающий использует его во избежание запроса блоков, которых больше нет в скользящем окне инициатора.
- `has_emergency_key` идентифицирует экстренный мастер-узел. Другие узлы могут приоритизировать синхронизацию от этого пира во время режима экстренного консенсуса.

---

### 5101 — `dlt_hello_reply_message`

Отправляется принимающим в ответ на 5100. Завершает рукопожатие.

```cpp
struct dlt_hello_reply_message {
    bool          exchange_enabled;    // true если мы считаем инициатора обновлённым
    bool          fork_alignment;      // true если инициатор на том же форке
    block_id_type initiator_head_seen; // эхо: head_block_id инициатора как мы его видим
    block_id_type initiator_lib_seen;  // эхо: lib_block_id инициатора как мы его видим
    uint32_t      our_dlt_earliest;    // наш ранний DLT-блок
    uint32_t      our_dlt_latest;      // наш последний DLT-блок
    uint8_t       our_fork_status;     // перечисление dlt_fork_status
    uint8_t       our_node_status;     // перечисление dlt_node_status
};
```

**Проверка выравнивания форков многоуровневая** для обработки обрезанных DLT-диапазонов:

| Случай | Выполняемая проверка |
|--------|---------------------|
| Пир не имеет блоков (`head_num == 0`) | → выровнен |
| Голова пира в нашем DLT-диапазоне | `is_block_known(peer.head_block_id)` |
| head пира + 1 == наш ранний | Читаем `our_earliest_block.previous == peer.head_block_id` |
| Запасной вариант | `is_block_known(peer.lib_block_id)` |

**`exchange_enabled`** равно `true`, когда fork_db принимающего содержит головной блок инициатора (т.е. инициатор находится в окне обмена и на том же форке). Только exchange-enabled пиры получают трансляции блоков и транзакций.

---

### 5102 — `dlt_range_request_message`

Спрашивает пира, есть ли у него конкретный блок по номеру и/или ID.

```cpp
struct dlt_range_request_message {
    uint32_t      block_num;
    block_id_type block_id;   // хэш запрашиваемого блока
};
```

---

### 5103 — `dlt_range_reply_message`

Ответ на 5102. Возвращает доступный диапазон обслуживания от пира.

```cpp
struct dlt_range_reply_message {
    uint32_t  range_start;   // ранний блок, который пир может обслужить
    uint32_t  range_end;     // последний блок, который пир может обслужить
    bool      has_blocks;    // false если у пира нет блоков вообще
};
```

---

### 5104 — `dlt_get_block_range_message`

Запрашивает непрерывный диапазон блоков в режиме SYNC. Максимум 200 блоков на запрос.

```cpp
struct dlt_get_block_range_message {
    uint32_t      start_block_num;
    uint32_t      end_block_num;
    block_id_type prev_block_id;  // хэш блока (start_block_num - 1); используется для проверки непрерывности цепочки
};
```

**Примечания:**
- Обслуживающий пир проверяет, что `blocks[0].previous == prev_block_id` перед отправкой.
- Разрыв между `start_block_num` и `dlt_earliest_block` обслуживающего пира может потребовать промежуточного пира.

---

### 5105 — `dlt_block_range_reply_message`

Ответ на 5104. Содержит до 200 блоков.

```cpp
struct dlt_block_range_reply_message {
    std::vector<signed_block> blocks;
    uint32_t                  last_block_next_available;  // следующий доступный блок после этой партии
    bool                      is_last;  // true если на этом пире больше нет блоков
};
```

**`is_last = true`** запускает `transition_to_forward()` на принимающей стороне, если узел обновлён.

---

### 5106 — `dlt_get_block_message`

Запрашивает один блок по номеру.

```cpp
struct dlt_get_block_message {
    uint32_t      block_num;
    block_id_type prev_block_id;  // хэш (block_num - 1) для проверки связи цепочки
};
```

---

### 5107 — `dlt_block_reply_message`

Ответ на 5106.

```cpp
struct dlt_block_reply_message {
    signed_block  block;
    uint32_t      next_available;  // следующий номер блока, который пир может обслужить (0 если на голове)
    bool          is_last;         // true если это головной блок пира
};
```

---

### 5108 — `dlt_not_available_message`

Отправляется, когда пир не может обслужить запрошенный блок (блок вне диапазона DLT log или блок неизвестен).

```cpp
struct dlt_not_available_message {
    uint32_t  block_num;
};
```

Запрашивающий узел должен найти другой пир с блоком в диапазоне или запустить заполнение разрыва / SYNC-режим.

---

### 5109 — `dlt_fork_status_message`

Обновление состояния живой цепочки. Отправляется при изменении головы, LIB, DLT-окна или статуса форка узла, и при переходе SYNC → FORWARD.

```cpp
struct dlt_fork_status_message {
    uint8_t       fork_status;         // перечисление dlt_fork_status
    block_id_type head_block_id;
    uint32_t      head_block_num;
    block_id_type lib_block_id;
    uint32_t      lib_block_num;
    uint32_t      dlt_earliest_block;
    uint32_t      dlt_latest_block;
    uint8_t       node_status;         // перечисление dlt_node_status
};
```

**Ключевой сценарий использования:** При переходе узла SYNC → FORWARD он транслирует это сообщение всем подключённым пирам, чтобы они могли немедленно переоценить `exchange_enabled` для этого узла, не ожидая следующего цикла hello.

Принимающий узел обновляет своё локальное `dlt_peer_state` для отправителя и повторно проверяет выравнивание форков + право на обмен.

---

### 5110 — `dlt_peer_exchange_request`

Пустое сообщение, запрашивающее список известных пиров.

```cpp
struct dlt_peer_exchange_request {
    // пусто
};
```

Ограничение скорости: **3 запроса за 5-минутное скользящее окно** на пир. Нарушители получают `dlt_peer_exchange_rate_limited` (5112).

---

### 5111 — `dlt_peer_exchange_reply`

Ответ на 5110. Содержит информацию о конечных точках известных пиров.

```cpp
struct dlt_peer_endpoint_info {
    fc::ip::endpoint  endpoint;
    node_id_t         node_id;
};

struct dlt_peer_exchange_reply {
    std::vector<dlt_peer_endpoint_info> peers;
};
```

**Фильтры, применяемые перед включением в ответ:**
- Время работы пира ≥ `dlt-peer-exchange-min-uptime-sec` (по умолчанию 600 с)
- Максимум `dlt-peer-exchange-max-per-subnet` (по умолчанию 2) пиров на /24-подсеть
- Пиры с `is_incoming` исключены (эфемерные исходные порты)
- Максимум `dlt-peer-exchange-max-per-reply` (по умолчанию 10) пиров всего

---

### 5112 — `dlt_peer_exchange_rate_limited`

Отправляется вместо 5111 при превышении лимита скорости запросов.

```cpp
struct dlt_peer_exchange_rate_limited {
    uint32_t  wait_seconds;  // сколько времени запрашивающий должен ждать перед повторной попыткой
};
```

---

### 5113 — `dlt_transaction_message`

Переносит подписанную транзакцию для распространения через mempool.

```cpp
struct dlt_transaction_message {
    signed_transaction trx;
};
```

Полученные транзакции добавляются в P2P mempool (фильтруются по сроку, TaPoS, размеру) перед отправкой в цепочечный `_pending_tx`. Успешно принятые транзакции пересылаются всем exchange-enabled пирам.

---

### 5114 — `dlt_soft_ban_message`

Отправляется пиру непосредственно перед закрытием соединения из-за спама или нарушения протокола. Принимающий пир входит в состояние BANNED на `ban_duration_sec` и не будет пытаться переподключиться до истечения бана.

```cpp
struct dlt_soft_ban_message {
    uint32_t    ban_duration_sec;  // длительность бана в секундах
    std::string reason;            // причина в понятном человеку формате
};
```

Общие причины в логах:
- `"spam_strikes_exceeded"` — 10 страйков за недействительные пакеты
- `"dead_fork_blocks"` — пир неоднократно присылал блоки с мёртвого форка
- `"protocol_violation"` — неожиданный тип сообщения или некорректные данные

---

### 5115 — `dlt_gap_fill_request`

Запрашивает конкретные блоки для заполнения обнаруженного разрыва в потоке блоков узла. Работает в обоих режимах SYNC и FORWARD.

```cpp
struct dlt_gap_fill_request {
    std::vector<uint32_t> block_nums;  // конкретные запрашиваемые номера блоков
};
```

**Ограничения:**
- Максимум **100 блоков** на запрос (`GAP_FILL_MAX_BLOCKS`).
- **5-секундное охлаждение** между запросами от одного узла (`GAP_FILL_COOLDOWN_SEC`).
- Более крупные разрывы запрашиваются кусками по 100 блоков; последующие куски запускаются в следующем цикле периодической задачи.
- Обслуживающий пир читает блоки из своего `dlt_block_log`; номера блоков вне диапазона обслуживания дают 5108.
- Обслуживание принимает запросы от любого активного пира (не только exchange-enabled).

Запускается из трёх мест:
1. `on_dlt_block_reply()` — обнаружен внеочерёдной блок
2. `periodic_task()` — проактивная проверка разрывов каждые 5 с
3. `resume_block_processing()` — после завершения паузы снимка

---

### 5116 — `dlt_gap_fill_reply`

Ответ на 5115. Содержит запрошенные блоки (может быть подмножеством, если некоторые недоступны).

```cpp
struct dlt_gap_fill_reply {
    std::vector<signed_block> blocks;  // запрошенные блоки; может быть частичным
};
```

---

## Поток рукопожатия

```
Инициатор                              Принимающий
    │                                     │
    │──TCP connect──────────────────────►│
    │                                     │
    │  5100 dlt_hello_message             │
    │──────────────────────────────────►│
    │  (head/lib, DLT-диапазон,           │
    │   emergency_active, node_status)    │
    │                                     │ проверка выравнивания форков
    │                                     │ установка exchange_enabled
    │  5101 dlt_hello_reply_message       │
    │◄──────────────────────────────────│
    │  (exchange_enabled, fork_alignment, │
    │   наш DLT-диапазон, наш node_status)│
    │                                     │
    ├── exchange_enabled = true ──────────┼── начинается обмен FORWARD или синхронизация
    └── exchange_enabled = false ─────────┴── инициатор входит в SYNC-режим
```

---

## Поток SYNC-режима

```
Синхр. узел                            Обслуживающий пир
    │                                     │
    │  5104 dlt_get_block_range           │
    │  (start=our_head+1, end=+200,       │
    │   prev_block_id=our_head_id)        │
    │──────────────────────────────────►│
    │                                     │ читает dlt_block_log
    │  5105 dlt_block_range_reply         │
    │◄──────────────────────────────────│
    │  (blocks=[N+1..N+200], is_last)     │
    │                                     │
    │  применяет каждый блок              │
    │  если is_last → переход в forward   │
    │  иначе → запрашивает следующую      │
    │  партию                             │
```

Если разрыв существует между `our_head + 1` и `dlt_earliest_block` обслуживающего пира, узел ищет промежуточный пир перед запросом.

---

## Трансляция в FORWARD-режиме

```
производитель блока      Пир A                 Пир B (exchange-enabled)
    │                       │                        │
    │ произвести блок        │                        │
    │ ─5109 fork_status─────►│                        │
    │ (через уведомление    │                        │
    │  о переходе)           │                        │
    │                        │                        │
    │ ─трансляция блока─────►│ (exchange-enabled)     │
    │                        │ ─трансляция блока─────►│
    │                        │                        │ push_block()
    │                        │                        │ ─5109 fork_status─► пиры
```

Только exchange-enabled пиры в `DLT_FORK_STATUS_NORMAL` или `DLT_FORK_STATUS_LOOKING_RESOLUTION` получают трансляции блоков.

---

## Поток заполнения разрывов

```
Узел (FORWARD, разрыв обнаружен)      Пир (имеет блоки разрыва)
    │                                     │
    │  5115 dlt_gap_fill_request          │
    │  (block_nums=[N+1, N+2, N+3])       │
    │──────────────────────────────────►│
    │                                     │ читает dlt_block_log
    │  5116 dlt_gap_fill_reply            │
    │◄──────────────────────────────────│
    │  (blocks=[N+1, N+2, N+3])          │
    │                                     │
    │  применяет блоки → голова продвиг.  │
```

---

## Формат провода

Каждое сообщение записывается как:

```
[ 4 байта: ID типа (uint32 LE) ][ 4 байта: длина полезной нагрузки (uint32 LE) ][ N байт: FC-сериализованная полезная нагрузка ]
```

Чтение использует `fc::tcp_socket::readsome()` / `writesome()` (неблокирующий, с уступкой волокна). Слоя шифрования нет — все пиры в одной цепочке разделяют общую сетевую идентичность и сообщения не подписываются пер-сообщение.

---

См. также: [Обзор P2P](./overview.md), [Сценарии синхронизации](./sync-scenarios.md), [Справочник статистики](./stats-reference.md).
