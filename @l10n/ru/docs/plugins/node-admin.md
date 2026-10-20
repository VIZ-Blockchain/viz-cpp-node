# Административный API ноды (`node_admin`)

Административный JSON-RPC-эндпоинт самого процесса ноды: здоровье, конфигурация,
состояние плагинов, логи, аудит, управление P2P-пирами и снапшотами. Предназначен для
инструментов оператора и AI-агентов, у которых есть bearer-токен; не для конечных
пользователей и не вместо публичного API. На том же слушателе работают эндпоинт **MCP**
(Model Context Protocol) поверх HTTP и небольшой встроенный веб-интерфейс по адресу `/ui/`.

> **Изменяющая поверхность: P2P-пиры, снапшоты, ключи валидаторов, политика webserver,
> `config.ini` и процесс.**
> `admin.p2p.add_node`, `admin.p2p.add_trusted` и `admin.p2p.clear_banned` меняют, с кем нода
> устанавливает соединения; `admin.snapshot.create` и `admin.snapshot.restore` пишут файл
> снапшота или заменяют состояние ноды в памяти; `admin.validator.*` включает и выключает
> валидаторов и меняет их signing-ключи, подписывая транзакции в цепи; изменяющие методы
> `admin.webserver.*` меняют лимиты, переключатель статистики и списки
> разрешённых/запрещённых IP; `admin.config.*` и `admin.plugins.*` правят `config.ini` через
> двухфазные `plan`/`apply`; `admin.restart` просит процесс завершиться. Им нужен токен со
> скоупом `p2p`, `snapshot`, `chain`, `webserver`, `config`, `plugins` или `process`
> соответственно. Действия над пирами сериализуются на P2P-потоке; действия над
> валидаторами подписывает плагин `validator_guard` и рассылает их в сеть; изменения
> политики webserver применяются к следующему же запросу; правки конфигурации и плагинов
> читаются при старте, поэтому нужен перезапуск.

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
| `admin-plan-ttl` | `120` | Сколько секунд `plan_id` остаётся действительным до отказа `apply`. |

Плагин отказывается стартовать, если `admin-endpoint` задан, а токенов нет: админский
порт без аутентификации не бывает допустимым состоянием.

Вкладка Logs читает **файл**, а не stdout контейнера. Ноде, которая внутри Docker пишет
только в stderr, нужен аппендер, кладущий те же строки на диск; именно этот файл и
называется в `admin-log-file`:

```ini
[log.file_appender.node]
filename = /var/lib/vizd/logs/node.log

[logger.default]
level = info
appenders = stderr,node

admin-log-file = node=/var/lib/vizd/logs/node.log
```

Пиши секциями: плоские ключи `logger.default.*` сливаются первыми, а секция
`[logger.default]` ниже их перетирает — плоская строка поверх существующей секции молча
игнорируется. Файловый аппендер НАСТРОЕН на часовую ротацию с хранением суток, но в
`vizd` она фактически не происходит: цепочка живёт в очереди файберов fc, а главный поток
ноды после старта сидит в `boost/asio` и её не обслуживает (замер 20.10.2026 — один файл
на 37 МБ за девять часов, часовых файлов нет). То есть файл не ограничен ни по времени, ни
по размеру; на `debug` он доходит до гигабайт. Для жёсткого предела есть опциональный
аппендер `capped_file`: один файл, только последние `max_lines` строк (по умолчанию
10000, `0` — без ограничения).

```ini
[log.capped_file_appender.node]
filename = /var/lib/vizd/logs/node.log
max_lines = 10000
```

Укажи тот же путь в `admin-log-file` — таб Logs читает его без изменений. Аппендер
опционален: ноды без секции и старые бинари, не знающие типа, работают как раньше.

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
* `config` — `admin.config.plan` и `admin.config.apply` (правка разрешённых ключей
  `config.ini`). `admin.config.backups` требует только `read`.
* `plugins` — `admin.plugins.plan` и `admin.plugins.apply` (правка списка `plugin =`).
* `process` — `admin.restart`. Он гасит процесс; поднять его обратно обязан супервизор.
* `chain` — `admin.validator.plan` и `admin.validator.apply` (включение/выключение
  валидаторов и смена их signing-ключей). Эти вызовы принимаются **только пока
  `validator_guard` загружен, запущен и сконфигурирован с active-ключом** — именно этот
  плагин хранит ключ, которым подписывается транзакция цепи, и через API он ключ не
  отдаёт. Без живого guard токен с `chain` не может вызвать ничего: `admin.validator.list`
  остаётся доступен со скоупом `read` и объясняет, почему изменяющие вызовы недоступны.

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

### `admin.auth.me`

Без параметров. Возвращает личность, которую несёт предъявленный токен: `label`, `scopes`
и `expires` (unixtime, `null` — токен бессрочный). Сам токен и его хэш **не** возвращаются
никогда. Встроенный UI по этому ответу скрывает действия, которые токену недоступны; агент
может так же узнать свои права перед планированием вызова.

```json
{"label":"ui-read","scopes":["read"],"expires":null}
```

### `admin.node_info`

| Поле | Смысл |
|---|---|
| `version`, `hardfork_count` | Версия сборки и число известных бинарнику хардфорков |
| `chain_id` | Идентификатор цепи |
| `head_block_num`, `head_block_id`, `head_block_time` | Текущая голова |
| `head_age_sec` | Возраст головы по стенным часам; `null` на пустой цепи (там время головы 1970 — «возраст» ничего не значит) |
| `last_irreversible_block_num` | Последний необратимый блок |
| `emergency_consensus_active`, `emergency_consensus_start_block` | Состояние аварийного консенсуса цепи и блок, с которого он начался (`0`, когда не активен) |
| `emergency_key_configured` | В плагине validator загружен аварийный ключ |
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

Для панели оператора есть ещё два поля:

* `removable` — `false` у `chain` и `node_admin`: `admin.plugins.plan` всё равно откажет в
  их удалении. Клиент обязан рисовать их как «непереключаемые», а не как «выключенные».
* `params` — allow-list-ключи этого плагина (`p2p`, `webserver`, `validator_guard`); пустой
  массив значит «редактируемых параметров нет», и панель должна это сказать, а не показать
  пустую форму. Ключи логов сгруппированы под `logging` — это не плагин, они в
  `admin.config.get`.

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

Ещё он отдаёт `allow` — редактируемое подмножество, готовое к отрисовке. Элемент:
`{key, group, section, type, value, defaulted, min, max, values, needs_restart}`.

* `key` — полный адрес, который принимает `admin.config.plan` (`logger.default.level` для
  ключа внутри `[logger.default]`), поэтому клиенту не нужно знать, какие ключи секционные.
* `value` равен `null`, а `defaulted` — `true`, когда в файле ключа нет: нода берёт значение
  из кода, и панель должна это показать, а не пустое поле.
* `type` — `boolean`, `unsigned_int` (с `min`/`max`) или `log_level` (с `values`).
* `group` — плагин, которому принадлежит ключ (`p2p`, `webserver`, `validator_guard`), либо
  `logging` для настроек логгеров и аппендеров.

### `admin.logs.list`

Без параметров. Перечисляет файлы, объявленные в `admin-log-file`: `name`, `path`, `exists`
и `size` (`null`, если файла ещё нет). Настроенный, но пока пустой аппендер тоже виден:
«пусто» и «не настроено» — разные факты.

```json
{"logs":[{"name":"node","path":"/var/lib/vizd/logs/node.log","exists":true,"size":1048576}]}
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

Параметры: `{"offset":0,"limit":20,"after":"snapshot-block-84360000.vizjson"}` — все
необязательны. Нужен `read` и загруженный плагин `snapshot`. Возвращает одну страницу файлов
снапшотов из настроенного `snapshot-dir` (сначала наибольший номер блока) и сообщает, что
делает плагин. `limit` ограничивается 200.

Листая каталог, который меняется под читателем, передавай `after` (имя последней уже
полученной строки), а не `offset`: снапшот, записанный между двумя страницами, сдвигает все
offset'ы ниже себя, и страница повторяет строку; чистка по сроку (24 ч) сдвигает в другую
сторону и заставляет offset пропускать строку. С `after` страница продолжается ровно после
этой строки. Если якорь уже вычищен, ответ несёт `after_found: false` и работает по `offset`.

| Поле | Смысл |
|---|---|
| `as_of` | UTC-метка замера |
| `dir` | Используемый каталог снапшотов |
| `head_block_num` | Текущая голова; `null`, пока восстановление перестраивает состояние |
| `snapshot_count` | Число файлов во всём каталоге |
| `snapshot_total` | Тот же итог, названный явно для клиентов с пагинацией |
| `offset`, `limit` | Выданная страница |
| `after`, `after_found` | Присланный курсор и остался ли он в каталоге |
| `has_more` | После этой страницы есть ещё файлы |
| `in_progress`, `restoring`, `recovering` | Пишется снапшот, идёт восстановление, chain-плагин восстанавливается |
| `latest_path`, `latest_name` | Самый свежий снапшот (наибольший номер блока); когда файлов нет — `latest_path` пустая строка, `latest_name` равен `null` |
| `snapshots[]` | `name`, `path`, `block_num` (0, если имя не несёт блок), `size_bytes`, `modified` (unixtime), `modified_iso`, `latest` |

```json
{"jsonrpc":"2.0","id":50,"method":"admin.snapshot.status","params":{"offset":0,"limit":20}}
```

```json
{"jsonrpc":"2.0","id":50,"result":{
  "as_of":"2026-10-19T15:30:00Z","dir":"/var/lib/vizd/snapshots",
  "head_block_num":22164,"snapshot_count":1,"snapshot_total":1,
  "offset":0,"limit":20,"has_more":false,"in_progress":false,
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

## Методы ключей валидаторов (скоуп `chain`)

Решение q#3051=D: оператор управляет валидаторами через `node_admin`, но **active-ключ
никогда не попадает в админский API**. Транзакции собираются и подписываются внутри
плагина `validator_guard`, которому заданы signing- и active-WIF каждого валидатора:

```ini
plugin = validator_guard
validator-guard-validator = ["val1", "5J…signing…", "5J…active…"]
validator-guard-validator = ["val2", "5J…signing…", "5J…active…"]
```

Guard должен быть загружен, запущен и содержать хотя бы одного настроенного валидатора;
иначе изменяющие методы отвечают `-32002 forbidden / validator-guard is not configured`, а
`admin.validator.list` показывает `guard.reason`. Настроенный валидатор обязан **существовать
в цепи** до планирования; создание нового валидатора здесь не предлагается (оно списывает
сбор за объявление валидатора и меняет набор производителей блоков).

### `admin.validators.active`

Без параметров, только `read`. Возвращает имена из текущего расписания валидаторов
(дубликаты от коэффициента повтора схлопнуты), число слотов расписания, состояние аварийного
консенсуса и запущен ли плагин validator в этом процессе. **Ключи не возвращаются** — это
публичные данные цепи. Для валидаторов, которыми управляет эта нода, есть
`admin.validator.list`.

```json
{"as_of":"2026-10-19T15:40:00Z","head_block_num":22164,"count":21,
 "num_scheduled":21,"validators":["val1","val2"],
 "emergency_consensus_active":false,"emergency_consensus_start_block":0,
 "validator_plugin_loaded":true}
```

### `admin.validator.list`

Скоуп `read`. Параметров нет. Возвращает:

```json
{"guard":{"loaded":true,"started":true,"available":true},
 "validators":[{"name":"val1","on_chain":true,"enabled":true,"hold":false,
                "active_authority":true,"scheduled_in":2,
                "signing_key":"VIZ6…","configured_key":"VIZ6…"}]}
```

* `enabled` — on-chain signing-ключ не null.
* `hold` — оператор выключил валидатор через этот API; авто-восстановление guard'а для него
  подавлено до успешного `enable`/`set_key` (см. ниже).
* `active_authority` — active-ключ guard'а входит в active-власть аккаунта. `false` значит,
  что транзакция, подписанная guard'ом, будет отвергнута цепью; то же предупреждение
  пишется в лог при старте guard'а.
* `scheduled_in` — позиция (с 1) в ближайших 3 слотах расписания, которые производит этот
  валидатор; `null`, если его там нет.

### `admin.validator.plan`

Скоуп `chain`. Параметры: `{"actions":[{"name":"val1","action":"disable"},…]}`. `action` —
`enable` (вернуть signing-ключ, настроенный в guard), `disable` (обнулить signing-ключ и
поставить operator hold) или `set_key` (требует `signing_key` — публичный ключ в base58).
Каждое имя должно быть настроено в guard и существовать в цепи; дубликаты отвергаются.

Ответ содержит `plan_id`, `kind:"validators"`, `transactions` (сколько транзакций цепи
отправит план) и `diff` по каждому действию: ключи `from`/`to`, `hold_from`/`hold_to`,
`tx_needed`. Уже выполненное действие в план не попадает; план без фактических изменений
отвергается с `-32602 no changes`. **Ничего пока не подписано и не отправлено.** Планы
одноразовые, привязаны к метке токена и истекают через `admin-plan-ttl`, как и планы
конфигурации. Захват текущего ключа в план и делает устаревший план безопасным: если цепь
изменилась между plan и apply, apply отвергает это действие, а не затирает чужое обновление.

### `admin.validator.apply`

Скоуп `chain`. Параметры: `{"plan_id":"…"}`. Подписывает по одной `validator_update` на
каждое изменившееся действие active-ключом guard'а, кладёт её в локальный pending pool и
рассылает пирам. Ответ перечисляет по каждому действию `status`: `applied` (с `tx_id`),
`already` (цепь сама пришла в нужное состояние) или `failed` (с `error`). Также даны
счётчики `applied`, `unchanged`, `failed`.

**Operator hold следует за результатом, а не за намерением.** Успешный `disable` записывает
валидатора в `<data-dir>/validator-guard-holds.txt`; периодическое авто-восстановление
guard'а пропускает удерживаемых, а файл перечитывается при старте — поэтому перезапуск тоже
не включит валидатор молча (disable-on-shutdown hold не создаёт — это штатное действие
выключения). Успешный `enable` или `set_key` снимает hold. Неудачный disable не оставляет
защёлки: валидатор остаётся как был.

Транзакции собираются под read-lock цепи и рассылаются после его освобождения — то же
правило, что у любой другой записи в цепь из этого плагина. Перезапуск не нужен.

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

## Правка конфигурации и плагинов (две фазы)

`config.ini` правится через **plan**, который возвращает точный дифф и ничего не пишет, и
**apply**, который перепроверяет файл и заменяет его атомарно. Так намерение агента можно
проверить до того, как оно коснётся боевой ноды, а падение посреди apply не оставит
обрезанный файл.

### Семантика plan

* `admin.config.plan {"set":{"key":value,…}}` проверяет каждый ключ по **allow-list** ниже,
  нормализует значение (`yes/on/1` → `true`; целые проверяются по диапазону) и возвращает
  `plan_id`, `kind:"config"`, `diff` (`[{key, from, to}]`), `config_path`, `needs_restart:true`
  и `expires_at`. **Ничего не пишется.** План без фактических изменений отклоняется с
  `-32602`.
* `admin.plugins.plan {"add":[…],"remove":[…]}` проверяет имена по набору плагинов, которые
  умеет грузить бинарник, отклоняет дубли, отсутствующие имена и удаление `chain` и
  `node_admin`, и возвращает `diff` из `[{op:"add"|"remove", name}]`.
* План **одноразовый** (сгорает на первом `apply`, успешном или нет), **привязан к label
  токена**, который его создал (`-32002` для любого другого), и **истекает** через
  `admin-plan-ttl` секунд (`-32004 plan expired`). Хранится до 64 планов; при переполнении
  вытесняется самый старый. `admin.plan.cancel` отбрасывает план явно.
* `apply` заново считает SHA-256 файла `config.ini` и отказывает с `-32000 config changed …
  nothing was written`, если файл изменился после создания плана. Затем он заново
  нормализует и проверяет каждое значение — apply никогда не доверяет плану слепо.

### Атомарная запись и бэкапы

Оба apply правят файл как **сырые строки**, поэтому комментарии, порядок, отступы и CRLF
сохраняются. Отсутствующий ключ верхнего уровня вставляется перед первым `[section]`;
существующий ключ верхнего уровня меняется на месте. Ключ, у которого в спецификации
указана секция (см. строку логов в таблице ниже), правится внутри этой секции и
вставляется сразу под её заголовок, если секция есть, а ключа в ней нет.

Каждый успешный apply пишет один бэкап рядом с конфигом — `<config.ini>.bak-<unixtime>`
(при коллизии с суффиксом `-n`), — затем временный файл в том же каталоге, `fsync`, копию
прав исходного файла, `rename` поверх `config.ini` и `fsync` каталога. Неудачная запись
оставляет исходный файл нетронутым. Старые бэкапы нода не чистит; в них есть секреты
(токены, ключи), поэтому обращаться с ними нужно как с самим конфигом.

`admin.config.backups` (скоуп `read`) перечисляет `<config.ini>.bak-*`, новые сверху, с
размером и mtime. Откат вручную — скопировать бэкап обратно и перезапустить ноду.

### Allow-list

Админский API правит только эти ключи. Всё остальное — идентичность, ключи, data-dir,
эндпоинты, `plugin`, `shared-file-size` — намеренно вне досягаемости bearer-токена.

| Ключи | Тип | Диапазон |
|---|---|---|
| `p2p-max-connections` | uint | 1–10000 |
| `dlt-peer-max-disconnect-hours` | uint | 1–8760 |
| `dlt-mempool-max-tx` | uint | 1–1000000 |
| `dlt-mempool-max-bytes` | uint | 1024–1073741824 |
| `dlt-mempool-max-tx-size` | uint | 256–67108864 |
| `dlt-mempool-max-expiration-hours` | uint | 1–720 |
| `dlt-peer-exchange-max-per-reply` | uint | 0–1000 |
| `dlt-peer-exchange-max-per-subnet` | uint | 0–1000 |
| `dlt-peer-exchange-min-uptime-sec` | uint | 0–86400 |
| `dlt-stats-interval-sec` | uint | 10–86400 |
| `p2p-isolated-peers`, `auto-resync-on-wedge` | bool | — |
| `webserver-thread-pool-size` | uint | 1–4096 |
| `webserver-cache-enabled` | bool | — |
| `webserver-cache-size` | uint | 0–10000000 |
| `webserver-stats-enabled`, `webserver-limits-enabled` | bool | — |
| `webserver-limit-m`, `webserver-limit-h`, `webserver-limit-d` | uint | 1–100000000 |
| `webserver-stats-log-max-size` | uint | 1024–1073741824 |
| `webserver-stats-log-keep` | uint | 1–1000 |
| `webserver-stats-interval` | uint | 1–86400 |
| `validator-guard-enabled`, `validator-guard-disable`, `validator-guard-disable-on-shutdown` | bool | — |
| `validator-guard-interval`, `validator-guard-shutdown-grace` | uint | 1–3600 |
| `validator-guard-disable-max` | uint | 0–1000 |
| `logger.default.level`, `logger.p2p.level` | уровень лога | `all`, `debug`, `info`, `warn`, `error`, `off` |
| `log.capped_file_appender.node.max_lines` | uint | 0–10000000 |
| `log.capped_file_appender.node.flush` | bool | — |

Ключи логов адресуются вместе с секцией: `logger.default.level` правит `level` внутри
`[logger.default]`, а не одноимённую опцию верхнего уровня. Отсутствующая в `config.ini`
секция — ошибка, а не повод её создать: нода такую секцию не объявляет, и значение молча
игнорировалось бы на следующем старте. Имена файлов аппендеров и окна ротации остаются за
оператором: опечатка в пути — это потерянный лог, а не настройка.

### Пример

```json
{"jsonrpc":"2.0","id":80,"method":"admin.config.plan",
 "params":{"set":{"p2p-max-connections":200,"webserver-limit-m":600}}}

{"jsonrpc":"2.0","id":80,"result":{
  "plan_id":"9f2c41a0d3b54e8770c5f1a2b3c4d5e6","kind":"config",
  "config_path":"/var/lib/vizd/config.ini","needs_restart":true,
  "diff":[{"key":"p2p-max-connections","from":"64","to":"200"},
          {"key":"webserver-limit-m","from":null,"to":"600"}],
  "expires_at":"2026-10-19T14:42:00Z",
  "hint":"nothing was written yet; apply with admin.config.apply {plan_id}…"}}
```

```json
{"jsonrpc":"2.0","id":81,"method":"admin.config.apply",
 "params":{"plan_id":"9f2c41a0d3b54e8770c5f1a2b3c4d5e6"}}

{"jsonrpc":"2.0","id":81,"result":{
  "applied":true,"kind":"config",
  "config_path":"/var/lib/vizd/config.ini",
  "backup":"/var/lib/vizd/config.ini.bak-1760884860",
  "needs_restart":true,
  "restart_hint":"config.ini is read at startup: call admin.restart {confirm:\"restart\"}…"}}
```

Пример с плагинами:

```json
{"jsonrpc":"2.0","method":"admin.plugins.plan",
 "params":{"add":["block_info"],"remove":[]}}
{"jsonrpc":"2.0","method":"admin.plugins.apply","params":{"plan_id":"…"}}
```

Оба apply отвечают `-32004 config unavailable`, если `config.ini` не читается, и
`-32000 write failed`, если атомарная замена не удалась (исходный файл остаётся нетронутым).

---

## Перезапуск и супервизия

`admin.restart {"confirm":"restart"}` (скоуп `process`) просит `appbase` завершиться:
сначала уходит ответ, затем отдельный поток через 300 мс вызывает `quit()`, чтобы HTTP-ответ
успел дойти до клиента. Процесс завершается с кодом 0.

**Сам плагин ноду обратно не поднимает.** Это работа супервизора — `--restart
unless-stopped` у контейнера или `Restart=always` у systemd. Без них нода просто останется
лежать; ответ говорит об этом в `hint`, и это намеренно: админский эндпоинт, который
форкает новый процесс ноды, был бы путём эскалации привилегий за пределы жизненного цикла
процесса.

Поскольку `config.ini` и список плагинов читаются при старте, `admin.config.apply` и
`admin.plugins.apply` вступают в силу только после этого перезапуска. `confirm` обязан быть
ровно словом `restart`; иначе `-32602`. Повторный вызов, пока завершение ещё не началось,
отвечает `-32000 restart already requested`.

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

Инструменты 1:1 с методами admin: `admin.ping`, `admin.auth.me`, `admin.node_info`,
`admin.diagnostics`, `admin.plugins.list`, `admin.config.get`, `admin.logs.list`,
`admin.logs.tail`, `admin.audit.tail`, `admin.validators.active`,
`admin.p2p.status`, `admin.p2p.add_node`, `admin.p2p.add_trusted`, `admin.p2p.clear_banned`,
`admin.snapshot.status`, `admin.snapshot.create`, `admin.snapshot.restore`,
`admin.webserver.status`, `admin.webserver.ips`, `admin.webserver.limits`,
`admin.webserver.stats`, `admin.webserver.whitelist_add`, `admin.webserver.whitelist_remove`,
`admin.webserver.blacklist_add`, `admin.webserver.blacklist_remove`, `admin.webserver.reset`,
`admin.webserver.reload`, `admin.validator.list`, `admin.validator.plan`,
`admin.validator.apply`, `admin.config.plan`, `admin.config.apply`, `admin.config.backups`,
`admin.plugins.plan`, `admin.plugins.apply`, `admin.plan.cancel`, `admin.restart`
(тридцать семь).
Действия над пирами дополнительно требуют скоупа `p2p`, действия над снапшотами — `snapshot`,
действия над валидаторами — `chain` (и живого `validator_guard` с active-ключом), действия
над webserver — `webserver`, правки конфига и плагинов — `config` или `plugins`,
перезапуск — `process`, ровно как и по обычному JSON-RPC. Ошибки исполнения инструмента возвращаются **внутри**
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

Базу JSON-RPC страница выводит из собственного URL, поэтому работает и когда оператор
публикует слушатель за префиксом обратного прокси, а не в корне: при nginx
`location /admin/ { proxy_pass http://127.0.0.1:9099/; }` страница живёт на `/admin/ui/`
и вызывает `/admin/`. В корне (`/ui/`) база — `/`, как и раньше.

**Аутентификация.** У страницы нет собственного секрета. При первом использовании оператор
вставляет bearer-токен; он хранится в `sessionStorage` (на вкладку, очищается при её
закрытии) и отправляется как `Authorization: Bearer …` в каждом JSON-RPC-вызове обратно на
тот же слушатель. Сама страница отдаётся без аутентификации намеренно: она одинакова для
всех, данных в ней нет, а браузер всё равно не умеет приложить bearer-заголовок к переходу
по ссылке. Токены никогда не кладутся в URL (иначе они попадут в историю и access-логи).

**Что показывает**

Страница разбита на пять вкладок; последняя выбранная запоминается на сессию вкладки.

* **Status** — состояние ноды, аварийный консенсус (`ACTIVE` с блоком начала или `normal`),
  настроен ли аварийный ключ, голова/LIB, возраст головы, версия, аптайм, счётчики пиров,
  лучшая голова пира и наше отставание, баны и флаги. Карточка состояния снапшотов прямо
  расшифровывает `idle` («никакой задачи со снапшотом не выполняется»), `creating` и
  `restoring`.
* **Peers** — таблица всех отслеживаемых пиров: направление, lifecycle, голова, отставание,
  статусы ноды/форка, флаги (`trusted`, `exchange`, `aligned`), возраст соединения,
  примечание о бане/повторе. При скоупе `p2p` появляется панель действий: добавить пира по
  `IP:port`, добавить как доверенного, очистить баны.
* **Snapshots** — состояние, каталог, общее число и самый свежий файл; **20** новейших
  файлов с кнопкой **Load more**, которая листает через `offset`/`limit`; блок, размер,
  возраст и кнопки **Download** / **Restore** в строке, плюс **Create snapshot** и
  **Restore latest**. На время скачивания кнопка блокируется и показывает радиальный
  индикатор до прихода тела ответа, поэтому повторные клики не запускают параллельные
  скачивания. Скачивание идёт через авторизованный маршрут `/snapshot/<name>` и
  сохраняется браузером под именем файла.
* **Logs** — выбор файла из записей `admin-log-file`, кнопка **Play/Pause** и прокручиваемая
  панель. По умолчанию **пауза**: Play опрашивает `admin.logs.tail` каждые две секунды и
  дописывает только новые строки внизу, раскрашивая их по важности. Если оператор прокрутил
  вверх, автопрокрутка прекращается; возврат вниз её возобновляет.
* **Validators** — имена из текущего расписания валидаторов (публичные данные цепи, без
  ключей), размер расписания и аварийное состояние; ниже — валидаторы, которыми управляет
  эта нода через `validator_guard`, если плагин жив.

**Страницей управляют скоупы.** При загрузке страница вызывает `admin.auth.me` и скрывает
каждое действие, которое токену недоступно: без `snapshot` пропадают кнопки Create/Restore
и Restore в строках, без `p2p` — панель действий над пирами. Поэтому read-only токен видит
дашборд только со списком и скачиванием, и страница говорит об этом прямо, вместо кнопок,
на которые API ответил бы `-32002`.

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
* Админский API никогда не отдаёт WIF-ключи. Кроме журнала аудита, на диск пишут
  `admin.snapshot.create` (в настроенный каталог снапшотов) и apply у
  `admin.config.*`/`admin.plugins.*` (конфиг плюс один `.bak-*` на каждый apply).
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
* Скоупы `config` и `plugins` правят `config.ini` на диске. Allow-list держит вне
  досягаемости идентичность, ключи, эндпоинты и `plugin`, но редактируемые ключи всё равно
  меняют поведение ноды при следующем перезапуске. `apply` сверяет хеш файла, поэтому правка,
  сделанная кем-то другим после плана, никогда не будет молча перезаписана; бэкап — это путь
  отката.
* `admin.config.backups` перечисляет файлы, в которых есть секреты (токены, строки
  `private-key`). Они создаются с правами исходного файла; не копируйте их в общедоступный
  путь.
* Скоуп `process` останавливает ноду. Без супервизора нода не вернётся; админский API
  намеренно не умеет перезапускать себя в обход жизненного цикла процесса.

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
reload), двухфазную правку конфига и плагинов (allow-list, скоупы и привязка к токену,
одноразовость, истечение, отказ по устаревшему хешу, бэкапы, защита плагинов), операции с
ключами валидаторов (список остаётся читаемым и называет недостающий плагин, `plan` отвергается
без `chain`, а затем без живого guard'а; после перезапуска guard загружен и стартовал, но
валидатора с active-ключом нет — `plan` по-прежнему отвергается), перезапуск
(гард `confirm`, гард скоупа, чистый выход с кодом 0 и старт «как у супервизора», который
читает отредактированный конфиг обратно), MCP `initialize`/`tools/list`/`tools/call` и
страницу `/ui/`. Поднимает плагин
webserver на третьем порту, поэтому нужны собранный `vizd` и три свободных порта; в CTest
не входит.

Путь подписи этим скриптом не проверить: у свежей цепи нет второго валидатора, а у
genesis-аккаунта `committee` active-авторитет пуст. Его проверяет
`plugins/node_admin/tests/validator-e2e.sh` — он поднимает собственную черновую цепь (временный
каталог и три порта; `--keep` оставляет её для разбора). Скрипт проводит тестовую однослотовую
цепь через HF9/HF12: `viz` регистрируется, вестится и получает голос в первых блоках, потому что
голоса весят доли вестинга, HF9 удаляет валидаторов, не подписавших ни одного блока, а HF12
исключает `committee` из выбора; затем создаются два валидатора: `backup` несёт операторские
действия, а `cold` нужен, чтобы план-батч мог назвать двух разных валидаторов. Проверки: список
(`on_chain`, `enabled`, `active_authority`), disable/enable/set-key с id транзакции, файл удержаний
переживает перезапуск ноды, устаревший план отвергается с `chain state changed` и ничего не
подписывает, план из двух действий применяет оба, производящий валидатор ротирует собственный
ключ, и углы контракта (дубль, неизвестное имя, плохой ключ, no-op → `-32602`).

Два решения в подготовке цепи принципиальны. Операторские действия никогда не целятся в
единственного *запланированного* валидатора: расписание пересобирается только в конце
применённого блока, поэтому обнуление его ключа оставляет устаревшее расписание, указывающее на
валидатора без ключа, и однослотовая цепь встаёт намертво. Секция саморотации — регрессия на
исправленный баг ноды: заголовок блока проверяется по ключу, зафиксированному в head, поэтому
блок, несущий обновление, обязан быть подписан СТАРЫМ ключом, а новый вступает в силу со
следующего блока. Раньше продюсер брал ключ подписи из session-visible объекта валидатора
(`validator.cpp`), поэтому ожидающий `validator_update`, меняющий ключ производящего валидатора,
заставлял подписать блок НОВЫМ ключом; `validate_block_header` отвергал блок, pending-пул
очищался, обновление молча терялось. Теперь плагин держит снапшот committed-ключей (засевается на
старте, обновляется по содержимому применённых блоков) и подписывает им.

`tests/node_admin/` — цель CTest (`node_admin_util`, Boost.Test) над чистыми хелперами:
маскирование секретов, разбор токенов, парсер конфига, allow-list и нормализация, а также
редакторы сырых строк (отступы, CRLF, секции, гарды добавления/удаления плагинов).
