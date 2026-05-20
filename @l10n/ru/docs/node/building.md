# Сборка из исходного кода

VIZ Ledger использует систему сборки на основе CMake с отдельными скриптами для каждой платформы. Двухэтапный процесс для Linux (`install-deps-linux.sh` + `build-linux.sh`) разделяет установку зависимостей (требует root) и саму сборку (выполняется от обычного пользователя).

---

## Требования

| Компонент | Версия |
|-----------|---------|
| CMake | 3.16+ |
| GCC | 4.8+ |
| Clang | 3.3+ |
| Boost | 1.71+ (с компонентом `coroutine`) |
| OpenSSL | Любая актуальная версия |

---

## Linux (Ubuntu 20.04 / 22.04 / 24.04)

### Шаг 1 — Клонирование репозитория

```bash
git clone --recursive https://github.com/VIZ-Blockchain/viz-cpp-node
cd viz-cpp-node
```

### Шаг 2 — Установка зависимостей (от root)

```bash
chmod +x install-deps-linux.sh
sudo ./install-deps-linux.sh
```

Устанавливает: cmake, gcc/g++, git, boost (все компоненты включая coroutine/context), openssl, readline, ccache и библиотеки сжатия.

### Шаг 3 — Сборка (от обычного пользователя)

```bash
chmod +x build-linux.sh
./build-linux.sh
```

Выходной бинарный файл: `build/programs/vizd/vizd`

### Основные флаги сборки

```bash
# Низкопамятный узел (для валидаторов/сид-узлов — без плагинов индексирования истории)
./build-linux.sh -l

# Сборка для тестнета
./build-linux.sh -n

# Debug-сборка
./build-linux.sh -t Debug

# Параллельные задания
./build-linux.sh -j 8

# Пропустить установку зависимостей (уже установлены)
./build-linux.sh --skip-deps

# Пользовательские пути к Boost / OpenSSL
./build-linux.sh --boost-root /opt/boost_1_74_0 --openssl-root /opt/openssl
```

---

## macOS

```bash
chmod +x build-mac.sh
./build-mac.sh
```

Скрипт автоматически устанавливает Xcode Command Line Tools (при необходимости) и зависимости Homebrew, затем конфигурирует и собирает проект.

```bash
# С указанием пути к Boost
./build-mac.sh --boost-root /opt/homebrew/opt/boost

# Пропустить установку зависимостей
./build-mac.sh --skip-deps
```

---

## Windows (MinGW)

```cmd
set BOOST_ROOT=C:\Boost
set OPENSSL_ROOT_DIR=C:\OpenSSL-Win64
build-mingw.bat
```

Опциональные переменные окружения:

| Переменная | По умолчанию | Описание |
|------------|--------------|----------|
| `VIZ_BUILD_TYPE` | `Release` | `Release` или `Debug` |
| `VIZ_LOW_MEMORY` | `OFF` | `ON` для низкопамятного узла |
| `VIZ_BUILD_TESTNET` | `OFF` | `ON` для сборки тестнета |
| `VIZ_FULL_STATIC` | `OFF` | `ON` для полностью статического бинарного файла |

---

## Windows (MSVC)

Требуется Visual Studio 2019+ с нагрузкой «Разработка классических приложений на C++»:

```cmd
set BOOST_ROOT=C:\Boost
set OPENSSL_ROOT_DIR=C:\OpenSSL-Win64
build-msvc.bat
```

---

## Опции CMake

Для прямого использования CMake (продвинутый режим):

| Опция | По умолчанию | Описание |
|-------|--------------|----------|
| `BUILD_TESTNET` | `OFF` | Включить код для тестнета |
| `LOW_MEMORY_NODE` | `OFF` | Исключить плагины истории/индексирования |
| `CHAINBASE_CHECK_LOCKING` | `OFF` | Включить проверки блокировок (debug) |
| `BUILD_SHARED_LIBRARIES` | `OFF` | Собрать разделяемые библиотеки |
| `USE_PCH` | `OFF` | Включить предкомпилированные заголовки (ускоряет пересборку) |

Пример:

```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DLOW_MEMORY_NODE=ON \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      ..
make -j$(nproc)
```

Или с помощью вспомогательного скрипта:

```bash
python3 programs/build_helpers/configure_build.py --release
```

---

## Цели сборки

| Цель | Бинарный файл | Описание |
|------|---------------|----------|
| `vizd` | `programs/vizd/vizd` | Основной демон узла |
| `cli_wallet` | `programs/cli_wallet/cli_wallet` | Кошелёк командной строки |

---

## Docker-сборки

В репозитории поставляются четыре Dockerfile:

| Файл | Назначение |
|------|-----------|
| `Dockerfile-production` | Полный узел мейннета (Release) |
| `Dockerfile-testnet` | Узел тестнета (`BUILD_TESTNET=ON`) |

Пример сборки:

```bash
docker build -f share/vizd/docker/Dockerfile-production -t vizd:local .
```

Полная настройка Docker для продакшена описана в разделе [Docker](./docker.md).

---

## Устранение неполадок

| Проблема | Решение |
|----------|---------|
| `boost/coroutine.hpp` не найден | Установите `libboost-coroutine-dev` (Ubuntu) или Boost 1.71+ |
| CMake < 3.16 | Установите новый CMake с `cmake.org` или Kitware PPA |
| Ошибка `do not run as root` | Запустите `build-linux.sh` от обычного пользователя, не `sudo` |
| Ошибка линковки на macOS (OpenSSL) | `export OPENSSL_ROOT_DIR=$(brew --prefix openssl)` |
| Нехватка памяти при компиляции | Используйте `-j 2` для уменьшения числа параллельных заданий |
