# Развёртывание через Docker

VIZ Ledger поставляется с четырьмя Docker-образами для различных профилей развёртывания. Все используют двухэтапную сборку: этап builder компилирует бинарный файл; этап runtime содержит только бинарный файл и конфигурацию.

---

## Доступные образы

| Dockerfile | Тег | Описание |
|-----------|-----|----------|
| `Dockerfile-production` | `latest` | Полный узел мейннета (Release, все плагины) |
| `Dockerfile-testnet` | `testnet` | Узел тестнета (`BUILD_TESTNET=ON`) |

---

## Быстрый старт

```bash
docker run -d \
  --name vizd \
  --restart unless-stopped \
  -p 2001:2001 \
  -p 8090:8090 \
  -p 8091:8091 \
  -v /data/vizd:/var/lib/vizd \
  vizblockchain/vizd:latest
```

Просмотр логов:

```bash
docker logs -f vizd
```

---

## Тома

| Путь в контейнере | Назначение |
|-------------------|-----------|
| `/var/lib/vizd` | Данные блокчейна, разделяемая память, block log |
| `/etc/vizd` | Файлы конфигурации и список сид-узлов |

Всегда монтируйте `/var/lib/vizd` для сохранения состояния между перезапусками контейнера.

Использование пользовательской конфигурации:

```bash
docker run -d \
  -v /data/vizd:/var/lib/vizd \
  -v /my/config.ini:/etc/vizd/config.ini \
  vizblockchain/vizd:latest
```

---

## Переменные окружения

Скрипт входа (`vizd.sh`) считывает следующие переменные окружения:

| Переменная | Описание | Пример |
|------------|----------|--------|
| `VIZD_SEED_NODES` | Список сид-узлов через пробел (переопределяет `/etc/vizd/seednodes`) | `seed1.viz.world:2001 seed2.viz.world:2001` |
| `VIZD_RPC_ENDPOINT` | Переопределить HTTP RPC endpoint | `0.0.0.0:8090` |
| `VIZD_P2P_ENDPOINT` | Переопределить P2P endpoint | `0.0.0.0:2001` |
| `VIZD_WITNESS` | Имя аккаунта валидатора (включает производство блоков) | `alice` |
| `VIZD_PRIVATE_KEY` | Подписывающий ключ валидатора в формате WIF | `5J...` |

---

## Порты

| Порт | Протокол | Назначение |
|------|----------|-----------|
| 2001 | TCP | P2P-соединения с пирами |
| 8090 | TCP | HTTP JSON-RPC |
| 8091 | TCP | WebSocket JSON-RPC |

---

## Узел-валидатор (Docker)

```bash
docker run -d \
  --name vizd-validator \
  --restart unless-stopped \
  -p 2001:2001 \
  -v /data/vizd:/var/lib/vizd \
  -e VIZD_WITNESS=myvalidator \
  -e VIZD_PRIVATE_KEY=5Jxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx \
  vizblockchain/vizd:latest
```

Для валидаторов **не открывайте** порты 8090/8091 публично — привязывайте только к localhost:

```bash
-e VIZD_RPC_ENDPOINT=127.0.0.1:8090
```

---

## Узел тестнета

```bash
docker run -d \
  --name vizd-testnet \
  -p 2001:2001 \
  -p 8090:8090 \
  -v /data/vizd-testnet:/var/lib/vizd \
  vizblockchain/vizd:testnet
```

---

## Локальная сборка образов

```bash
# Production
docker build \
  -f share/vizd/docker/Dockerfile-production \
  -t vizd:local \
  .

# Testnet
docker build \
  -f share/vizd/docker/Dockerfile-testnet \
  -t vizd:testnet \
  .
```

### CMake-флаги для каждого образа

| Образ | `LOW_MEMORY_NODE` | `BUILD_TESTNET` |
|-------|:-----------------:|:---------------:|
| production | OFF | OFF |
| testnet | OFF | ON |

---

## CI/CD (GitHub Actions)

В репозитории поставляется `.github/workflows/docker-main.yml`, который собирает и публикует production-образ с тегом `latest` при каждом push в `master`.

```yaml
- name: Build and push
  uses: docker/build-push-action@v2
  with:
    file: share/vizd/docker/Dockerfile-production
    tags: vizblockchain/vizd:latest
    push: true
```

---

## Требования к ресурсам

| Тип узла | RAM | Диск |
|----------|-----|------|
| Полный узел (мейннет) | 8 ГБ+ | 50 ГБ+ |
| Узел-валидатор | 4 ГБ | 20 ГБ |
| Тестнет | 4 ГБ | 10 ГБ |

Начинайте с размера разделяемой памяти, удобно помещающегося в RAM. В `config.ini`:

```ini
shared-file-size = 4G
```

---

## Устранение неполадок

| Симптом | Причина | Решение |
|---------|---------|---------|
| Контейнер сразу завершается | Плохая конфигурация или отсутствующий том | `docker logs vizd` — проверьте ошибки запуска |
| Порт 8090 недоступен | RPC привязан к localhost | Уберите префикс `127.0.0.1:` или используйте reverse proxy |
| Нет пиров | Файрвол блокирует порт 2001 | Откройте порт 2001 TCP входящий |
| Медленная синхронизация | Снимок не загружен | Предоставьте снимок в томе перед первым запуском |
| `Permission denied` на `/var/lib/vizd` | Несоответствие владельца тома | `chown -R 1000:1000 /data/vizd` |
