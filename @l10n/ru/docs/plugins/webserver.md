# Плагин Webserver

Плагин webserver предоставляет HTTP и WebSocket конечные точки, перенаправляющие JSON-RPC запросы в плагин `json_rpc`. Включает кеш ответов, ключом которого служит `method + params` (не `id`), инвалидируемый при каждом применённом блоке, и пул потоков для параллельной обработки запросов.

**Исходник:** [plugins/webserver/webserver_plugin.cpp](../../plugins/webserver/webserver_plugin.cpp)

---

## Зависимости

```
json_rpc::plugin
```

---

## Конфигурация

| Опция | По умолчанию | Описание |
|-------|-------------|---------|
| `webserver-http-endpoint` | — | Адрес прослушивания HTTP, например `0.0.0.0:8090` |
| `webserver-ws-endpoint` | — | Адрес прослушивания WebSocket, например `0.0.0.0:8091` |
| `webserver-thread-pool-size` | `256` | Рабочие потоки для обработки HTTP и WebSocket запросов. |
| `webserver-cache-enabled` | `true` | Включить кеш ответов. |
| `webserver-cache-size` | `10000` | Максимальное количество кешированных ответов. Кеш полностью вытесняется при достижении этого лимита. |

Для работы плагина должно быть задано хотя бы одно из `webserver-http-endpoint` или `webserver-ws-endpoint`. Оба могут быть включены одновременно.

---

## Минимальный config.ini

```ini
plugin = webserver

webserver-http-endpoint = 0.0.0.0:8090
webserver-ws-endpoint   = 0.0.0.0:8091
```

---

## Кеш ответов

### Что кешируется

Каждый ответ JSON-RPC только для чтения подходит для кеширования. Ключ кеша — это SHA-256 хеш `method + params` — намеренно **исключая `id`**, чтобы ротация `id` запроса не могла обойти кеш.

### Что никогда не кешируется

| Пространство имён | Причина |
|------------------|---------|
| `network_broadcast_api.*` | Изменяющий состояние (трансляция транзакций/блоков) |
| `debug_node.*` | Изменяющие состояние операции отладки |
| Некорректные/нераспознаваемые запросы | Невозможно надёжно вычислить ключ |

Пакетные запросы (JSON-массив) обрабатываются как единственная атомарная запись кеша с ключом на основе хеша полного массива.

### Инвалидация

Кеш очищается при каждом применённом блоке. Ответы никогда не обслуживаются устаревшими дольше одного интервала блока (3 секунды).

### Отключение кеша

```ini
webserver-cache-enabled = false
```

Отключите для узлов, обслуживающих клиентов с высокими требованиями к задержке или клиентов реального времени, для которых окно кеша в 3 секунды неприемлемо.

---

## Пул потоков

Серверы HTTP и WebSocket каждый работают на выделенном экземпляре `io_service`. Входящие запросы направляются в общий пул потоков из `webserver-thread-pool-size` рабочих.

**Рекомендации по размеру:**
- Публичный API-узел со смешанным трафиком чтения/записи: `256` (по умолч.) достаточно для большинства нагрузок.
- Высокопроизводительные узлы: увеличьте до `512` или более. Следите за насыщением CPU — больше потоков, чем ядер CPU, не помогает для операций, ограниченных CPU.
- Разработка/локальный узел: достаточно `4`–`8`.

---

## WebSocket-подписки

WebSocket-клиенты могут регистрировать колбеки:

| Метод | Описание |
|-------|---------|
| `database_api.set_block_applied_callback` | Вызывается при каждом применённом блоке с заголовком блока |
| `database_api.set_pending_transaction_callback` | Вызывается, когда транзакция поступает в пул ожидания |
| `database_api.cancel_all_subscriptions` | Отписаться от всех колбеков |

Подписки требуют постоянного WebSocket-соединения. Они недоступны по обычному HTTP.

---

## Безопасность

- **Привяжите к localhost** (`127.0.0.1`) и используйте обратный прокси (nginx/Caddy) для публичного доступа. Привязка к `0.0.0.0` открывает RPC напрямую в сеть.
- Плагин не имеет встроенной аутентификации или ограничения скорости. Применяйте их на уровне обратного прокси.
- Мутирующие методы (`network_broadcast_api`, `debug_node`) защищены от отравления кеша по замыслу, но они остаются доступными для вызова с любого подключённого клиента — при необходимости ограничьте доступ на сетевом уровне.

---

## Устранение неполадок

| Симптом | Проверка |
|---------|---------|
| Порт уже занят при запуске | Другой процесс привязан к настроенному порту; измените порт или завершите конфликтующий процесс |
| Высокое использование памяти | Уменьшите `webserver-cache-size` или отключите кеширование |
| Медленные ответы под нагрузкой | Увеличьте `webserver-thread-pool-size`; проверьте насыщение CPU |
| WebSocket-подписки не срабатывают | Подписки требуют WebSocket-соединения, а не HTTP |
| Устаревшие ответы | Если `webserver-cache-enabled = true`, ответы актуальны в пределах одного интервала блока (~3 с); для использования в реальном времени отключите кеш |

---

## Публикация API через HTTPS (nginx + certbot)

Привяжите узел к localhost, затем поставьте перед ним nginx. certbot автоматически дополнит конфиг при запуске `certbot --nginx`.

### 1. config.ini узла

```ini
webserver-http-endpoint = 127.0.0.1:8090
webserver-ws-endpoint   = 127.0.0.1:8091
```

### 2. /etc/nginx/sites-enabled/viz-node

```nginx
server {
    listen 80;
    server_name your.domain.com;  # ← замените на ваш домен

    # ACME-challenge для certbot
    location /.well-known/acme-challenge/ {
        root /var/www/certbot;
    }

    location / {
        # CORS — разрешить любой источник (публичный API)
        add_header 'Access-Control-Allow-Origin'   '*'                                                                                         always;
        add_header 'Access-Control-Allow-Methods'  'GET, POST, PUT, DELETE, PATCH, OPTIONS'                                                    always;
        add_header 'Access-Control-Allow-Headers'  'DNT,User-Agent,X-Requested-With,If-Modified-Since,Cache-Control,Content-Type,Range,Authorization' always;
        add_header 'Access-Control-Expose-Headers' 'Content-Length,Content-Range'                                                              always;

        if ($request_method = 'OPTIONS') {
            add_header 'Access-Control-Allow-Origin'  '*'                                                                                         always;
            add_header 'Access-Control-Allow-Methods' 'GET, POST, PUT, DELETE, PATCH, OPTIONS'                                                    always;
            add_header 'Access-Control-Allow-Headers' 'DNT,User-Agent,X-Requested-With,If-Modified-Since,Cache-Control,Content-Type,Range,Authorization' always;
            add_header 'Access-Control-Max-Age'       1728000;
            add_header 'Content-Type'                 'text/plain charset=UTF-8';
            add_header 'Content-Length'               0;
            return 204;
        }

        proxy_pass http://127.0.0.1:8090;
        proxy_http_version 1.1;

        proxy_set_header Host              $host;
        proxy_set_header X-Real-IP         $remote_addr;
        proxy_set_header X-Forwarded-For   $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;

        proxy_connect_timeout 60s;
        proxy_send_timeout    60s;
        proxy_read_timeout    60s;
    }
}
```

### 3. Получить TLS-сертификат

```bash
sudo nginx -t && sudo systemctl reload nginx
sudo certbot --nginx -d your.domain.com
```

certbot автоматически добавит блок `listen 443 ssl`, директивы `ssl_certificate` и редирект HTTP→HTTPS. После этого узел доступен по адресу `https://your.domain.com`.

### WebSocket через HTTPS

WebSocket-клиентам нужна проксировка заголовка `Upgrade`. Добавьте отдельный location (или второй server block на порту 8091):

```nginx
location /ws {
    proxy_pass http://127.0.0.1:8091;
    proxy_http_version 1.1;
    proxy_set_header Upgrade    $http_upgrade;
    proxy_set_header Connection "upgrade";
    proxy_set_header Host       $host;
    proxy_read_timeout 3600s;
}
```

---

См. также: [Обзор плагинов](./overview.md), [Database API](./database-api.md), [JSON-RPC API](../api/json-rpc.md).
