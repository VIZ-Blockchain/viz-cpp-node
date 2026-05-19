# Начало работы

В этом руководстве описано всё необходимое для запуска узла VIZ Ledger — от установки зависимостей до начальной синхронизации.

---

## Требования

| Требование | Минимум | Рекомендуется |
|-----------|---------|--------------|
| ОС | Ubuntu 20.04 LTS | Ubuntu 24.04 LTS |
| ОЗУ | 4 ГБ | 8 ГБ+ |
| Диск | 20 ГБ | 50 ГБ+ SSD |
| CPU | 2 ядра | 4+ ядра |
| Сеть | Публичный IP, открытый порт 2001 | Стабильное соединение |

**Используемые порты:**

| Порт | Протокол | Назначение |
|------|---------|-----------|
| 2001 | TCP | P2P соединения с пирами |
| 8090 | TCP | HTTP JSON-RPC |
| 8091 | TCP | WebSocket JSON-RPC |

---

## Вариант А: Docker (рекомендуется для быстрого старта)

### 1. Загрузка образа

```bash
docker pull vizblockchain/vizd:latest
```

### 2. Запуск узла

```bash
docker run -d \
  --name vizd \
  -p 2001:2001 \
  -p 8090:8090 \
  -p 8091:8091 \
  -v /data/vizd:/var/lib/vizd \
  vizblockchain/vizd:latest
```

### 3. Просмотр логов

```bash
docker logs -f vizd
```

Через несколько минут вы должны увидеть подключения к пирам и прогресс синхронизации блоков.

### Переменные окружения (Docker)

| Переменная | Назначение | Пример |
|-----------|-----------|--------|
| `VIZD_SEED_NODES` | Переопределить начальные узлы | `node1.viz.media:2001` |
| `VIZD_WITNESS` | Имя валидатора (для узла-валидатора) | `alice` |
| `VIZD_PRIVATE_KEY` | Ключ подписи валидатора (WIF) | `5J...` |

---

## Вариант Б: Сборка из исходников

### 1. Установка зависимостей (Linux)

```bash
git clone --recursive https://github.com/VIZ-Blockchain/viz-cpp-node
cd viz-cpp-node
chmod +x install-deps-linux.sh
sudo ./install-deps-linux.sh
```

### 2. Сборка

```bash
chmod +x build-linux.sh
./build-linux.sh
```

Для сборки с малым объёмом памяти (валидаторы и сид-узлы — без плагинов индексирования):

```bash
./build-linux.sh -l
```

Исполняемый файл помещается в `build/programs/vizd/vizd`.

### 3. macOS

```bash
chmod +x build-mac.sh
./build-mac.sh
```

### 4. Windows (MinGW)

```cmd
set BOOST_ROOT=C:\Boost
set OPENSSL_ROOT_DIR=C:\OpenSSL-Win64
build-mingw.bat
```

Подробные инструкции по платформам и параметры CMake — в разделе [Сборка](./building.md).

---

## Начальная настройка

Скопируйте шаблон конфигурации для основной сети:

```bash
cp share/vizd/config/config.ini /data/vizd/config.ini
```

Минимальные правки для публичного узла:

```ini
# P2P
p2p-endpoint = 0.0.0.0:2001
p2p-seed-node = seed1.viz.media:2001
p2p-seed-node = seed2.viz.media:2001

# RPC
webserver-http-endpoint = 0.0.0.0:8090
webserver-ws-endpoint   = 0.0.0.0:8091

# Разделяемая память — подберите по доступному диску
shared-file-size = 4G

# Плагины (полный узел)
plugin = chain p2p webserver json_rpc database_api network_broadcast_api
plugin = social_network tags follow account_history
```

Для узла-валидатора см. [Узел-валидатор](./validator-node.md).

---

## Запуск узла

```bash
./vizd --config-file /data/vizd/config.ini --data-dir /data/vizd
```

В Docker передайте каталог данных как том (см. Вариант А выше).

---

## Проверка синхронизации

Запросите узел через HTTP RPC:

```bash
curl -s -X POST http://localhost:8090 \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","method":"call","params":["database_api","get_dynamic_global_properties",[]],"id":1}' \
  | python3 -m json.tool
```

Проверьте `head_block_number` — после синхронизации он должен увеличиваться каждые 3 секунды.

---

## Типы узлов

| Тип | Шаблон конфигурации | Описание |
|-----|-------------------|---------|
| Полный узел | `config.ini` | Все плагины, публичные RPC-эндпоинты |
| Валидатор | `config_witness.ini` | Производство блоков, RPC только на localhost |
| Тестовая сеть | `config_testnet.ini` | Разработка и тестирование |
| Малая память | `config.ini` + флаг сборки `LOW_MEMORY_NODE` | Только консенсус, без индексов истории |
| MongoDB | `config_mongo.ini` | Полная история в MongoDB |

---

## Дальнейшие шаги

- [Справочник конфигурации](./configuration.md) — описание всех параметров
- [Развёртывание Docker](./docker.md) — продакшн-настройка Docker
- [Узел-валидатор](./validator-node.md) — запуск блок-продюсера
- [Снапшоты](./snapshot.md) — быстрая синхронизация через снапшоты состояния
