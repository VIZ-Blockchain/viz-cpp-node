# Административный API ноды (`node_admin`)

Административный JSON-RPC-эндпоинт самого процесса ноды: здоровье, конфигурация,
состояние плагинов, логи, аудит, управление P2P-пирами и снапшотами. Предназначен для
инструментов оператора и AI-агентов, у которых есть bearer-токен; не для конечных
пользователей и не вместо публичного API. На том же слушателе работают эндпоинт **MCP**
(Model Context Protocol) поверх HTTP и небольшой встроенный веб-интерфейс по адресу `/ui/`.

> **Изменяющая поверхность: P2P-пиры, снапшоты и политика webserver.** `admin.p2p.add_node`,
> `admin.p2p.add_trusted` и `admin.p2p.clear_banned` меняют, с кем нода устанавливает
> соединения; `admin.snapshot.create` и `admin.snapshot.restore` пишут файл снапшота или
> заменяют состояние ноды в памяти; изменяющие методы `admin.webserver.*` меняют лимиты,
> переключатель статистики и списки разрешённых/запрещённых IP. Им нужен токен со скоупом
> `p2p`, `snapshot` или `webserver` соответственно. Действия над пирами сериализуются на
> P2P-потоке; изменения политики webserver применяются к следующему же запросу. Остальные
> изменяющие операции (конфигурация, плагины, процесс, цепь) — следующий этап, на той же
> модели аутентификации и аудита с двухфазными `plan`/`apply`.

---

## Почему отдельный слушатель

Публичный API обычно опубликован через nginx. Внутри ноды такой запрос приходит
**с 127.0.0.1**, поэтому проверка адреса источника на публичном эндпоинте ничего не
защищает. Поэтому `node_admin` поднимает собственный HTTP-слушатель, по умолчанию
привязанный к loopback, и он никогда не проксируется.

Адрес не на loopback отклоняется при старте, если явно не задан
`admin-allow-remote = true`. Bearer-токен на открытом порту — это другая модель угроз,
а не удобство.

---

## Конфигурация

```ini
plugin = node_admin

admin-endpoint = 127.0.0.1:8099
admin-token = agent:read:3f8c…(64 hex)…e1
admin-audit-log = /var/lib/vizd/node-admin-audit.log
admin-log-file = node=/var/log/vizd/vizd.log
```

| Опция | По умолчанию | Описание |
|---|---|---|
| `admin-endpoint` | — | Адрес прослушивания. Пусто/нет — плагин неактивен. |
| `admin-token` | — | `label:scope[,scope]:sha256hex[:expires-unixtime]`. Можно повторять. |
| `admin-allow-remote` | `false` | Разрешить `admin-endpoint` не на loopback. |
| `admin-audit-log` | рядом с `config.ini` | Append-only журнал аудита (0600, ротация на 16 МБ). |
| `admin-log-file` | — | Записи `name=path`, которые разрешено читать `admin.logs.tail`. Можно повторять. |

Плагин отказывается стартовать, если `admin-endpoint` задан, а токенов нет: админский
порт без аутентификации не бывает допустимым состоянием.

### Токены

Токен — 32 случайных байта, например `openssl rand -hex 32`. В конфигурации хранится
только его SHA-256; сравнение — constant-time. Скоупы:

* `read` — все методы, включая `admin.p2p.status` и скачивание снапшотов. Нужен любому токену.
* `p2p` — изменяющие методы над пирами `admin.p2p.add_node`, `admin.p2p.add_trusted`,
  `admin.p2p.clear_banned`. Токен только с `p2p` (без `read`) не может вызвать ничего.
* `snapshot` — `admin.snapshot.create` и `admin.snapshot.restore`. Список и скачивание
  снапшотов требуют только `read`.
* `webserver` — изменяющие методы `admin.webserver.*` (лимиты, переключатель статистики,
  списки IP, reset, reload). `admin.webserver.status` и `admin.webserver.ips` требуют
  только `read`.
* `plugins`, `config`, `process`, `chain` — зарезервированы под изменяющие этапы; токен
  только с ними пока не может вызвать ничего.

Отзыв — правка строки и перезапуск. Необязательный `expires-unixtime` делает токен
недействительным после этого момента без перезапуска.

---

## Протокол

`POST` с `Content-Type: application/json` и `Authorization: Bearer <token>`.
Тело ≤ 64 КБ. Ответы — JSON-RPC 2.0:

```json
{"jsonrpc":"2.0","id":1,"result":{...}}
{"jsonrpc":"2.0","id":1,"error":{"code":-32002,"message":"forbidden",
 "data":{"reason":"token 'agent' has no 'read' scope"}}}
```

| Код | Значение |
|---|---|
| `-32700` | Ошибка разбора |
| `-32600` | Некорректный запрос |
| `-32601` | Метод не найден |
| `-32602` | Некорректные параметры |
| `-32001` | Нет авторизации (HTTP 401) |
| `-32002` | Не хватает скоупа (HTTP 200) |
| `-32004` | Неизвестное/запрещённое имя (например, лог-файл) |
| `-32005` | Лимит: 20 запросов/с на токен (HTTP 429) |
| `-32099` | Внутренняя ошибка |

Аутентификация проверяется **до** разбора тела.

Маршруты не-`POST` — встроенный интерфейс и скачивание снапшотов:

* `GET /ui/` (и `GET /ui`) отдаёт встроенную страницу, описанную ниже. В ней нет ни данных,
  ни секретов; все настоящие вызовы страница делает обычными авторизованными `POST` на `/`.
* `GET /snapshot/latest` отдаёт самый свежий снапшот как файл-вложение. `GET /snapshot/<name>`
  отдаёт именованный файл из каталога снапшотов. Оба требуют `Authorization: Bearer …` и
  скоуп `read` — в отличие от `/ui/`, снапшот это состояние ноды. `HEAD` работает на обоих и
  несёт те же заголовки, включая `Content-Length`. Любой другой метод или путь отвечает `405`.

Ограничения скачивания: файл обязан быть обычным файлом непосредственно в каталоге
снапшотов (имена берутся из `admin.snapshot.status`; traversal отклоняется), а файлы больше
**512 МБ** получают `413` — их копируйте через `scp`.

---

## Методы

### `admin.ping`

```json
{"ok": true, "server_time": "2026-10-19T14:40:00Z"}
```

Проверка живости; базу цепи не трогает.

### `admin.node_info`

| Поле | Смысл |
|---|---|
| `version`, `hardfork_count` | Версия сборки и число известных бинарнику хардфорков |
| `chain_id` | Идентификатор цепи |
| `head_block_num`, `head_block_id`, `head_block_time` | Текущая голова |
| `head_age_sec` | Возраст головы по стенным часам; `null` на пустой цепи (там время головы 1970 — «возраст» ничего не значит) |
| `last_irreversible_block_num` | Последний необратимый блок |
| `recovering` | Chain-плагин восстанавливает shared memory |
| `uptime_sec` | Время с запуска админского слушателя |
| `server_time` | Текущее UTC |

### `admin.diagnostics`

Всё из `admin.node_info`, плюс:

| Поле | Смысл |
|---|---|
| `data_dir`, `config_path` | Используемые пути |
| `p2p_loaded`, `peers` | P2P-плагин присутствует, число активных соединений (читается на p2p-потоке) |
| `plugins_configured`, `plugins_observed` | Записи `plugin =` в `config.ini` и сколько из них читаются напрямую (см. `admin.plugins.list`) |
| `shared_memory_bytes` | Размер `shared_memory.bin` |
| `disk_total_bytes`, `disk_free_bytes` | Файловая система `data_dir` (POSIX) |
| `rss_peak_kb` | Пиковый RSS (POSIX) |
| `loadavg` | Средняя нагрузка за 1/5/15 минут (POSIX) |

Числа отдаются числами JSON, а не строками: дефолтный форматер fc берёт int64 выше 2^32-1
и все double в кавычки, из-за чего размеры в байтах и load average пришлось бы парсить дважды.

### `admin.plugins.list`

Список записей `plugin = …` из `config.ini` без дублей. Реестр плагинов AppBase закрыт
(поиск по имени — protected, перечисления нет), поэтому видимое население — сам конфиг;
плагины, включённые только командной строкой, здесь не видны.

У каждой записи есть `loaded`, `state` и `state_source`:

* `state_source: "observed"` — состояние прочитано у живого плагина (`chain`, `p2p`, `snapshot`, `webserver`,
  `node_admin`). `state` — `registered`, `initialized`, `started` или `stopped`; `loaded`
  равен false только пока плагин в состоянии `registered`.
* `state_source: "inferred"` — AppBase инициализирует каждую строку `plugin =` и падает на
  старте при неизвестном имени, значит на работающей ноде сконфигурированный плагин загружен.
  `state` при этом `null`, а не догадка.

### `admin.config.get`

Отдаёт `config.ini` упорядоченными секциями с сохранением дублей ключей (нужно для
повторяющихся строк `plugin`). **Значения ключей, несущих секреты, заменяются на `***`**:
всё, что содержит `private-key`, `private_key`, `password`, `secret`, `token`, `wif` или
`emergency`.

```json
{"path":"/etc/vizd/config.ini","exists":true,"sections":[
  {"name":"","entries":[{"key":"plugin","value":"chain"},
                        {"key":"private-key","value":"***"}]}]}
```

### `admin.logs.tail`

Параметры: `{"name":"node","bytes":16384,"lines":200}`. `name` обязан совпасть с
записью `admin-log-file`; ничего вне этого allowlist прочитать нельзя. `bytes`
ограничивается 64 КБ, `lines` — 2000. Строки, похожие на WIF, и секреты вида
`ключ = значение` маскируются.

### `admin.audit.tail`

Параметры: `{"lines":100}`. Хвост журнала аудита.

### `admin.p2p.status`

Без параметров. Нужен только `read` (кто может смотреть — может смотреть и на пиров).
Отдаёт те же факты, что нода пишет в свой лог — `log_node_status()` раз в минуту и
`log_peer_stats()` раз в пять минут — но по запросу и в JSON:

| Поле | Смысл |
|---|---|
| `as_of` | UTC-метка замера |
| `uptime_sec` | Аптайм ноды |
| `node_status`, `fork_status` | `SYNC`/`FORWARD`, `NORMAL`/`LOOKING_RESOLUTION`/`MINORITY` |
| `paused`, `isolated_peers`, `catching_up` | Обработка блоков на паузе, режим только-сиды, догон после паузы |
| `head_block_num`, `lib_block_num` | Наша голова и последний необратимый блок |
| `dlt_earliest`, `dlt_latest` | Диапазон блоков DLT, который держим локально (что эта нода может отдать) |
| `connections`, `active`, `max_connections` | Открытые соединения, из них полностью активных, и лимит |
| `known_peers`, `trusted_peers`, `banned_peers` | Множество известных пиров, доверенные оператором, сейчас забаненные |
| `best_peer_head` | Наибольшая голова среди отслеживаемых пиров |
| `best_peer_head_lag` | `наша голова − лучшая голова пира`; **отрицательное значит, что мы отстаём** |
| `peers_ahead` | Сколько пиров впереди нас |
| `peers[]` | Детали по каждому пиру, отсортированы по адресу |

Поля пира: `endpoint`, `node_id` (hex, пусто до hello), `lifecycle`
(`CONNECTING`/`HANDSHAKING`/`SYNCING`/`ACTIVE`/`DISCONNECTED`/`BANNED`), `node_status`,
`fork_status`, `incoming`, `trusted`, `exchange_enabled`, `fork_alignment`, `connected_sec`,
`peer_head`, `peer_lib`, `peer_dlt_earliest`, `peer_dlt_latest`, `head_lag` (наш минус его),
`spam_strikes`, `ban_reason`, `ban_duration_sec`, `reconnect_backoff_sec`,
`next_reconnect_in_sec`.

```json
{"jsonrpc":"2.0","id":40,"method":"admin.p2p.status"}
```

```json
{"jsonrpc":"2.0","id":40,"result":{
  "as_of":"2026-10-19T15:10:00Z","uptime_sec":86400,
  "node_status":"FORWARD","fork_status":"NORMAL","paused":false,
  "isolated_peers":false,"catching_up":false,
  "head_block_num":22164,"lib_block_num":22150,
  "dlt_earliest":1,"dlt_latest":22164,
  "connections":3,"active":3,"max_connections":50,
  "known_peers":12,"trusted_peers":1,"banned_peers":0,
  "best_peer_head":22164,"best_peer_head_lag":0,"peers_ahead":0,
  "peers":[{"endpoint":"1.2.3.4:1776","node_id":"02ab…","lifecycle":"ACTIVE",
            "node_status":"FORWARD","fork_status":"NORMAL","incoming":false,
            "trusted":true,"exchange_enabled":true,"fork_alignment":true,
            "connected_sec":3600,"peer_head":22164,"peer_lib":22150,
            "head_lag":0,"ban_reason":"","next_reconnect_in_sec":0}]}}
```

### `admin.p2p.add_node`

Параметры: `{"endpoint":"1.2.3.4:1776"}` — IPv4 и порт. Нужны `read` **и** `p2p`.
Добавляет адрес в множество известных пиров и сразу звонит по нему (снимая старый бэкофф
или бан на этом адресе). Пир при этом не доверенный.

```json
{"endpoint":"1.2.3.4:1776","trusted":false,"status":"added"}
```

### `admin.p2p.add_trusted`

Те же параметры и форма ответа, `"trusted": true`. Доверенный пир:

* **никогда не получает soft-ban** — `soft_ban_peer()` отключает его и повторяет попытку
  через 30 с вместо часового бана;
* **не выбрасывается сборщиком известных пиров**, который иначе удаляет молчащих;
* звонится сразу и переподключается по мере надобности.

Множество доверенных живёт в памяти: оно **не сохраняется между перезапусками**. Чтобы
закрепить пир навсегда, держите его в `p2p-seed-node` (или в списке сидов, который
используется вместе с `p2p-isolated-peers`) в `config.ini`.

### `admin.p2p.clear_banned`

Без параметров. Нужны `read` и `p2p`. Снимает все soft-баны (lifecycle `BANNED` или
непустой `ban_reason`), сбрасывает бэкофф переподключения и сразу повторяет попытки с
этими пирами.

```json
{"cleared":3,"status":"ok"}
```

Типичный операторский сценарий:

```bash
# сначала статус: что не так и с кем
curl -s http://127.0.0.1:8099 -H "Authorization: Bearer $T" \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"admin.p2p.status"}' | jq '.result | {head_block_num, best_peer_head_lag, peers}'

# добавить пира и сделать его доверенным
curl -s http://127.0.0.1:8099 -H "Authorization: Bearer $T" \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":2,"method":"admin.p2p.add_trusted","params":{"endpoint":"1.2.3.4:1776"}}'

# после форк-шторма или волны банов: повторить всех
curl -s http://127.0.0.1:8099 -H "Authorization: Bearer $T" \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":3,"method":"admin.p2p.clear_banned"}'
```

### `admin.snapshot.status`

Без параметров. Нужен `read` и загруженный плагин `snapshot`. Перечисляет файлы снапшотов
в настроенном `snapshot-dir` и сообщает, что делает плагин.

| Поле | Смысл |
|---|---|
| `as_of` | UTC-метка замера |
| `dir` | Используемый каталог снапшотов |
| `head_block_num` | Текущая голова; `null`, пока восстановление перестраивает состояние |
| `snapshot_count` | Число файлов в `snapshots[]` |
| `in_progress`, `restoring`, `recovering` | Пишется снапшот, идёт восстановление, chain-плагин восстанавливается |
| `latest_path`, `latest_name` | Самый свежий снапшот (наибольший номер блока); когда файлов нет — `latest_path` пустая строка, `latest_name` равен `null` |
| `snapshots[]` | `name`, `path`, `block_num` (0, если имя не несёт блок), `size_bytes`, `modified` (unixtime), `modified_iso`, `latest` |

```json
{"jsonrpc":"2.0","id":50,"method":"admin.snapshot.status"}
```

```json
{"jsonrpc":"2.0","id":50,"result":{
  "as_of":"2026-10-19T15:30:00Z","dir":"/var/lib/vizd/snapshots",
  "head_block_num":22164,"snapshot_count":1,"in_progress":false,
  "restoring":false,"recovering":false,
  "latest_path":"/var/lib/vizd/snapshots/snapshot-block-22164.vizjson",
  "latest_name":"snapshot-block-22164.vizjson",
  "snapshots":[{"name":"snapshot-block-22164.vizjson",
                "path":"/var/lib/vizd/snapshots/snapshot-block-22164.vizjson",
                "block_num":22164,"size_bytes":4404019,
                "modified":1792423800,"modified_iso":"2026-10-19T15:30:00Z","latest":true}]}}
```

### `admin.snapshot.create`

Без параметров. Нужны `read` и `snapshot`. Пишет `snapshot-block-<head>.vizjson` в
`snapshot-dir` **асинхронно** на потоке снапшотов плагина; обработка блоков P2P приостановлена
на время чтения базы и возобновляется до сжатия. Опрашивайте `admin.snapshot.status`, пока
`in_progress` не станет false.

```json
{"jsonrpc":"2.0","id":51,"method":"admin.snapshot.create"}
```

```json
{"jsonrpc":"2.0","id":51,"result":{
  "status":"started","path":"/var/lib/vizd/snapshots/snapshot-block-22164.vizjson",
  "note":"snapshot creation runs on the node's snapshot thread; poll admin.snapshot.status until in_progress is false"}}
```

`status` — `started`, `exists` (файл для этой головы уже на диске) или `busy` (идёт другое
создание/восстановление). На пустой цепи (голова `0`) вызов падает с `-32000` и ничего не пишет.

### `admin.snapshot.restore`

Параметры: `{}` (восстановить последний), `{"latest":true}`,
`{"name":"snapshot-block-22164.vizjson"}` или
`{"path":"/var/lib/vizd/snapshots/<файл>"}`. Нужны `read` и `snapshot`.

Восстановление идёт **в процессе**, это не перезапуск процесса: обработка блоков P2P
приостанавливается, shared memory закрывается и очищается, снапшот импортируется,
`dlt_block_log` проигрывается заново, пиры сбрасываются и P2P возобновляется. ОС-процесс и
его аптайм не меняются, поэтому слушатель продолжает отвечать во время операции
(`restoring: true`, голова `null`). Если задан `path`, он обязан указывать на обычный файл
непосредственно в каталоге снапшотов.

```json
{"jsonrpc":"2.0","id":52,"method":"admin.snapshot.restore","params":{"latest":true}}
```

```json
{"jsonrpc":"2.0","id":52,"result":{
  "status":"started","snapshot":"/var/lib/vizd/snapshots/snapshot-block-22164.vizjson",
  "snapshot_name":"snapshot-block-22164.vizjson","block_num":22164,
  "process_restarted":false,
  "note":"in-process restore: P2P is paused, shared memory is wiped, the snapshot is imported, dlt_block_log is replayed and P2P resumes. The OS process and its uptime are unchanged. Progress: admin.snapshot.status (restoring)."}}
```

Вызов, который не может начаться, отвечает `-32000` (нет снапшотов, идёт создание, файл
нечитаем); некорректные имена, traversal и файлы вне каталога снапшотов отвечают `-32602`
до любого изменения состояния. Если сам импорт падает, `do_snapshot_load` уже очистил базу
и запросил завершение — смотрите лог ноды; админский API не делает вид, что восстановление
удалось.

---

## Методы политики webserver

Эти методы управляют плагином публичного API: счётчиками запросов по IP, логом статистики,
лимитами и списками IP. Семантика, значения по умолчанию и формат лога описаны в
[Webserver — «Учёт запросов и лимиты»](./webserver.md#учёт-запросов-и-лимиты).

**Изменения живые.** Плагин держит это состояние в памяти; изменение применяется к
следующему запросу webserver — без перезапуска и без перечитывания `config.ini`. Каждое
изменение также атомарно сохраняется в `webserver-admin-state`
(по умолчанию `<data-dir>/webserver-admin.json`), который загружается при старте и потому
перекрывает стартовое значение из `config.ini`. Метод `reload` перечитывает этот файл для
правок руками.

| Метод | Скоуп | Параметры | Назначение |
|-------|-------|-----------|------------|
| `admin.webserver.status` | `read` | нет | Лимиты, итоги, списки IP и число отслеживаемых адресов. |
| `admin.webserver.ips` | `read` | `{"limit":200}` (макс. 5000) | Строки по IP: `total`, `minute`, `hour`, `day`, `blocked`, `limited`, `last_seen`, `whitelisted`, `blacklisted`; сортировка по total. |
| `admin.webserver.limits` | `webserver` | `{"enabled":bool,"per_minute":N,"per_hour":N,"per_day":N}` | Опущенные поля сохраняют значение. |
| `admin.webserver.stats` | `webserver` | `{"enabled":bool}` | Включить/выключить учёт запросов. Лимиты управляются отдельно. |
| `admin.webserver.whitelist_add` / `whitelist_remove` | `webserver` | `{"ip":"1.2.3.4"}` | Адреса из белого списка обходят лимиты, но всё равно считаются. |
| `admin.webserver.blacklist_add` / `blacklist_remove` | `webserver` | `{"ip":"1.2.3.4"}` | Адреса из чёрного списка получают `403`; действует даже при выключенной статистике. |
| `admin.webserver.reset` | `webserver` | нет | Обнулить счётчики; списки и лимиты сохранить. |
| `admin.webserver.reload` | `webserver` | нет | Перечитать файл состояния с диска; вернуть применённый статус. |

Добавление IP в один список удаляет его из другого. Адрес должен быть одним литералом
IPv4 или IPv6; иначе `-32602`.

```json
{"jsonrpc":"2.0","id":53,"method":"admin.webserver.limits","params":{"per_minute":600}}
```

```json
{"jsonrpc":"2.0","id":53,"result":{
  "status":"applied",
  "limits":{"enabled":true,"per_minute":600,"per_hour":10000,"per_day":100000},
  "stats_enabled":true,
  "note":"applied to the live webserver on the next request and persisted to its admin state file; config.ini is unchanged."}}
```

Если плагин `webserver` не загружен, ответ — `-32004 webserver unavailable`. Публичный
webserver отвечает клиентам из чёрного списка `403`, а превысившим лимит — `429` с
`Retry-After: 60`; эти ответы производит webserver, а не этот API.

---

## Эндпоинт MCP (Streamable HTTP)

Решение q#3052=B: MCP-сервер живёт **внутри этого плагина**, а не в sidecar. Тот же
loopback-слушатель, те же bearer-токены и тот же журнал аудита обслуживают Model Context
Protocol поверх HTTP — на хосте ничего дополнительно ставить не нужно.

Запросы MCP — обычные JSON-RPC 2.0 `POST` на админский эндпоинт:

| Метод | Назначение |
|---|---|
| `initialize` | Рукопожатие. Поддерживаемые версии: `2024-11-05`, `2025-03-26`, `2025-06-18` (запрошенная возвращается как есть, иначе `2025-06-18`). Объявляется только возможность `tools`. |
| `notifications/initialized` | Уведомление (без `id`); ответ — HTTP 202 с пустым телом. |
| `ping` | Keepalive MCP; возвращает `{}`. |
| `tools/list` | Инструменты ниже с JSON-схемой `inputSchema`. |
| `tools/call` | `{"name":"admin.node_info","arguments":{…}}` — выполняет соответствующий метод `admin.*`. |

Инструменты 1:1 с методами admin: `admin.ping`, `admin.node_info`, `admin.diagnostics`,
`admin.plugins.list`, `admin.config.get`, `admin.logs.tail`, `admin.audit.tail`,
`admin.p2p.status`, `admin.p2p.add_node`, `admin.p2p.add_trusted`, `admin.p2p.clear_banned`,
`admin.snapshot.status`, `admin.snapshot.create`, `admin.snapshot.restore`,
`admin.webserver.status`, `admin.webserver.ips`, `admin.webserver.limits`,
`admin.webserver.stats`, `admin.webserver.whitelist_add`, `admin.webserver.whitelist_remove`,
`admin.webserver.blacklist_add`, `admin.webserver.blacklist_remove`, `admin.webserver.reset`,
`admin.webserver.reload` (двадцать четыре).
Действия над пирами дополнительно требуют скоупа `p2p`, действия над снапшотами — `snapshot`,
действия над webserver — `webserver`, ровно как и по обычному JSON-RPC. Ошибки исполнения инструмента возвращаются **внутри**
результата (`"isError": true`, текст в `content[0].text`), как требует MCP; протокольные
ошибки (неизвестный инструмент, плохие параметры, нет скоупа) остаются ошибками JSON-RPC.

```json
{"jsonrpc":"2.0","id":7,"result":{"tools":[
  {"name":"admin.node_info","description":"Chain id, head block, hardfork, uptime and head age.",
   "inputSchema":{"type":"object","properties":{},"additionalProperties":false}}]}}
```

```json
{"jsonrpc":"2.0","id":8,"result":{
  "content":[{"type":"text","text":"{\"head_block_num\":1234,…}"}],"isError":false}}
```

Эндпоинт без сессий: `Mcp-Session-Id` не выдаётся, `GET` на всё, кроме `/ui/` и маршрутов
`/snapshot/…` (включая необязательный SSE-поток), отвечает `405`. Клиенты, которым обязателен
SSE, не поддерживаются; ответы — JSON.

Конфигурация клиента (транспорт Streamable HTTP) указывает на тот же loopback-адрес:

```json
{"mcpServers":{"viz-node":{"type":"http","url":"http://127.0.0.1:8099",
  "headers":{"Authorization":"Bearer <token>"}}}}
```

---

## Встроенный веб-интерфейс (`/ui/`)

Одна статическая страница, вкомпилированная в бинарник и отдаваемая тем же слушателем:

```text
http://127.0.0.1:8099/ui/
```

Чистые HTML + vanilla JS — без сборки, без внешних ресурсов, без фреймворков. CSP разрешает
только собственные inline-стиль/скрипт страницы и same-origin `fetch`, всё остальное —
`'none'`.

**Аутентификация.** У страницы нет собственного секрета. При первом использовании оператор
вставляет bearer-токен; он хранится в `sessionStorage` (на вкладку, очищается при её
закрытии) и отправляется как `Authorization: Bearer …` в каждом JSON-RPC-вызове обратно на
тот же слушатель. Сама страница отдаётся без аутентификации намеренно: она одинакова для
всех, данных в ней нет, а браузер всё равно не умеет приложить bearer-заголовок к переходу
по ссылке. Токены никогда не кладутся в URL (иначе они попадут в историю и access-логи).

**Что показывает**

* статус ноды, статус форка, голова/LIB, диапазон блоков DLT, аптайм;
* счётчики пиров, лучшая голова пира и наше отставание (отрицательное = мы позади),
  сколько пиров впереди;
* таблицу всех отслеживаемых пиров: направление, lifecycle, голова, отставание, статусы
  ноды/форка, флаги (`trusted`, `exchange`, `aligned`), возраст соединения и примечание о
  бане/повторе;
* панель действий: добавить пира по `IP:port`, добавить его как доверенного, очистить
  список забаненных;
* панель снапшотов: состояние (idle / creating / restoring), каталог, число и самый свежий
  файл; таблица файлов с блоком, размером, возрастом и кнопками **Download** / **Restore**
  в каждой строке, плюс кнопки **Create snapshot** и **Restore latest**. Скачивание идёт
  через авторизованный маршрут `/snapshot/<name>` и сохраняется браузером под именем файла;
* панель лога с результатом каждого действия и ошибками статуса.

Статус обновляется каждые пять секунд, пока вкладка видима.

**Чего оно не делает.** Нет правки конфигурации, управления токенами и удаления файлов
снапшотов. Единственные записи — три действия над пирами, создание снапшота и
восстановление в процессе. Это тонкий вид поверх того же JSON-RPC-контракта, описанного
выше — всё, что умеет страница, умеют и `curl`, и MCP-клиент, а каждый вызов попадает в
журнал аудита под меткой токена (собственный `GET /ui/` пишется в аудит с исходом `ui`,
скачивание — с `snapshot_download`).

**Токен для страницы.** Заведите отдельный токен с нужными странице скоупами: токен только
с `read` даёт живой read-only дашборд со списком и скачиванием снапшотов, добавление `p2p`
включает кнопки действий над пирами, а `snapshot` — кнопки **Create** и **Restore**:

```ini
admin-token = ui:read,snapshot,p2p:<sha256 от 32 случайных байт>
```

---

## Аудит

Каждый запрос — включая неудачную аутентификацию, ошибки разбора и срабатывание лимита —
добавляет одну JSON-строку:

```json
{"ts":"2026-10-19T14:40:00Z","rid":"17","token":"agent","method":"admin.node_info",
 "params":{},"outcome":"ok","code":0,"ms":2,"remote":"127.0.0.1:51234"}
```

`token` — метка, никогда не сам токен и не его хеш. `remote` — настоящий адрес пира
соединения (не `X-Forwarded-For`: слушатель по построению не стоит за прокси, иначе клиент
выбирал бы себе атрибуцию в аудите). `params` маскируются теми же правилами, что и хвост
лога. Файл создаётся с правами `0600` и ротируется в `.1` на 16 МБ.

---

## Замечания по безопасности

* Слушатель по умолчанию только на loopback; не ставьте его за nginx.
* Никогда не передавайте токен в query string — он попадёт в логи прокси и access-логи.
* Токен — это доступ к процессу ноды. Один токен на агента, чтобы аудит атрибутировал
  вызовы, и минимальный набор скоупов.
* `admin.config.get` маскирует секреты по имени ключа, а не по форме значения. Секрет под
  безобидным именем будет напечатан — не заводите таких ключей.
* Админский API никогда не отдаёт WIF-ключи. Кроме журнала аудита, единственный метод,
  который пишет на диск, — `admin.snapshot.create` (в настроенный каталог снапшотов).
* `admin.snapshot.restore` **заменяет всё состояние цепи** выбранным файлом. Он
  приостанавливает обработку блоков и очищает shared memory; неверный или устаревший снапшот
  разрушителен. Считайте скоуп `snapshot` эквивалентом права записи в состояние ноды, а при
  ожидаемом догоне с пирами предпочитайте снапшот на уровне LIB пиров или ниже.
* Снапшоты отдаёт только админский слушатель, никогда публичный webserver. Не копируйте их
  в общедоступный путь; снапшот содержит полное состояние цепи и, в зависимости от сборки,
  метаданные аккаунтов.
* `GET /ui/` без аутентификации — так задумано, и это одни и те же байты для всех. Доступа
  это не даёт: без токена каждый её вызов получает `401`. `OPTIONS` он не отвечает и
  CORS-заголовков не шлёт, поэтому чужая страница не сможет дёргать его из браузера.
* Действия над пирами живут в памяти. `add_trusted` и `add_node` действуют до перезапуска
  процесса; постоянные закрепления — в конфигурации сидов P2P, а не в админском API.
* Скоуп `webserver` может влиять на доступность публичного API (слишком строгий лимит или
  ошибочная запись в чёрном списке отклоняют добросовестных клиентов). Токен с ним — это
  политический credential, а не только читающий; изменения к тому же переживают перезапуск
  через `webserver-admin-state`.

---

## См. также

* [Обзор плагинов](./overview.md)
* [Webserver](./webserver.md) — публичный API, который этот плагин намеренно не использует

---

## Тесты

`plugins/node_admin/tests/acceptance.sh` поднимает одноразовую ноду, дёргает слушатель
curl'ом и проверяет этот контракт: аутентификацию и скоупы, маскирование конфига/логов,
allowlist логов, коды ответов, аудит, лимит, действия над P2P-пирами (включая отказ без
скоупа `p2p`), статус/список/скачивание снапшотов (включая `HEAD`, traversal и отсутствующий
файл, а также отказ без скоупа `snapshot` на create/restore), статистику/лимиты webserver
(всплеск → `429`, обход белым списком, `403` чёрным, смена лимитов на ходу, сохранение и
reload), MCP `initialize`/`tools/list`/`tools/call` и страницу `/ui/`. Поднимает плагин
webserver на третьем порту, поэтому нужны собранный `vizd` и три свободных порта; в CTest
не входит.
