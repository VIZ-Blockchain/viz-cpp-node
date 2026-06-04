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
| `/etc/vizd` | Файлы конфигурации |

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

| Образ | `BUILD_TESTNET` |
|-------|:---------------:|
| production | OFF |
| testnet | ON |

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

## Ротация логов

vizd пишет весь вывод в stdout/stderr. Дефолтный драйвер `json-file` в Docker **не имеет ограничений по размеру** — цикл краша или буря ошибок может заполнить диск хоста за считанные минуты (в продакшне наблюдалось 35 ГБ+).

Вместо этого используйте драйвер `local`. Он хранит логи в компактном бинарном формате и автоматически ротирует файлы.

**Глобальная конфигурация (рекомендуется — защищает все контейнеры на хосте):**

```json
// /etc/docker/daemon.json
{
  "log-driver": "local",
  "log-opts": {
    "max-size": "100m",
    "max-file": "5"
  }
}
```

Применить:

```bash
sudo systemctl restart docker
```

**Для конкретного контейнера (`docker run`):**

```bash
docker run -d \
  --log-driver=local \
  --log-opt max-size=100m \
  --log-opt max-file=5 \
  --name vizd \
  vizblockchain/vizd:latest
```

**Для конкретного контейнера (docker-compose):**

```yaml
services:
  vizd:
    image: vizblockchain/vizd:latest
    logging:
      driver: local
      options:
        max-size: "100m"
        max-file: "5"
```

> При `max-file: 5` и `max-size: 100m` Docker хранит не более 500 МБ логов на контейнер и автоматически удаляет старейший файл при ротации.

---

## Устранение неполадок

| Симптом | Причина | Решение |
|---------|---------|---------|
| Контейнер сразу завершается | Плохая конфигурация или отсутствующий том | `docker logs vizd` — проверьте ошибки запуска |
| Порт 8090 недоступен | RPC привязан к localhost | Уберите префикс `127.0.0.1:` или используйте reverse proxy |
| Нет пиров | Файрвол блокирует порт 2001 | Откройте порт 2001 TCP входящий |
| Медленная синхронизация | Снимок не загружен | Предоставьте снимок в томе перед первым запуском |
| `Permission denied` на `/var/lib/vizd` | Несоответствие владельца тома | `chown -R 1000:1000 /data/vizd` |
| Диск заполняется логами Docker | Драйвер `json-file` не имеет ограничения по размеру | Настройте драйвер `local` с `max-size`/`max-file` — см. [Ротация логов](#ротация-логов) |
