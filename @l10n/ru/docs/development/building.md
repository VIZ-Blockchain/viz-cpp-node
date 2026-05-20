# Сборка

Узел VIZ Ledger использует CMake 3.16+ и требует Boost 1.71+ с компонентом coroutine. Поддерживаемые платформы: Ubuntu 24.04+, macOS (Homebrew), Windows (MSVC или MinGW).

---

## Linux (Ubuntu/Debian)

### Шаг 1: Установка зависимостей (требуется root)

```bash
chmod +x install-deps-linux.sh
sudo ./install-deps-linux.sh
```

Устанавливает: CMake, GCC/G++, Git, Make, ccache, OpenSSL, Boost 1.71 (все необходимые компоненты, включая coroutine/context), readline и библиотеки сжатия.

### Шаг 2: Сборка (выполнять от обычного пользователя, не root)

```bash
chmod +x build-linux.sh
./build-linux.sh
```

**Распространённые параметры:**

```bash
./build-linux.sh              # Release-сборка (по умолчанию)
./build-linux.sh -l           # LOW_MEMORY_NODE (узлы-валидаторы)
./build-linux.sh -n           # Testnet-сборка
./build-linux.sh -t Debug -j4 # Debug-сборка с 4 параллельными задачами
./build-linux.sh --skip-deps  # Пропустить установку зависимостей
./build-linux.sh --install    # Установить в систему после сборки

# Пользовательские пути к зависимостям
./build-linux.sh --boost-root /opt/boost_1_74_0 --openssl-root /opt/openssl
```

### Fedora/RHEL

Те же скрипты автоматически определяют `dnf`. Устанавливаемые пакеты: `cmake`, `gcc-c++`, `git`, `ccache`, `boost-devel`, `openssl-devel`, `bzip2-devel`, `zstd-devel`.

---

## macOS

```bash
chmod +x build-mac.sh
./build-mac.sh
```

Требует Xcode Command Line Tools и Homebrew. Скрипт устанавливает: `boost`, `cmake`, `git`, `autoconf`, `automake`, `libtool`, `openssl`, `readline`.

**Параметры:**

```bash
./build-mac.sh -l              # Low-memory узел
./build-mac.sh -n              # Testnet
./build-mac.sh --skip-deps     # Пропустить установки Homebrew
./build-mac.sh --boost-root /opt/boost_1_74_0
```

---

## Windows (MinGW)

Установите необходимые переменные окружения, затем запустите batch-скрипт:

```cmd
set BOOST_ROOT=C:\Boost
set OPENSSL_ROOT_DIR=C:\OpenSSL-Win64
build-mingw.bat
```

**Опциональные переменные:**

| Переменная | По умолчанию | Описание |
|-----------|-------------|---------|
| `VIZ_BUILD_TYPE` | Release | Release или Debug |
| `VIZ_LOW_MEMORY` | OFF | Включить low-memory узел |
| `VIZ_BUILD_TESTNET` | OFF | Testnet-сборка |
| `VIZ_FULL_STATIC` | OFF | Полностью статический бинарник |
| `VIZ_CMAKE_EXTRA` | — | Дополнительные флаги CMake |

**Требования:** MinGW-w64 с C++11 и SSE4.2, CMake 3.16+, Boost 1.71+ (статический, `link=static threading=multi runtime-link=shared`), OpenSSL для Windows.

---

## Windows (MSVC)

```cmd
set BOOST_ROOT=C:\Boost
set OPENSSL_ROOT_DIR=C:\OpenSSL-Win64
build-msvc.bat
```

**Опциональные переменные:**

| Переменная | По умолчанию | Описание |
|-----------|-------------|---------|
| `VIZ_VS_VERSION` | "Visual Studio 17 2022" | Генератор Visual Studio |
| `VIZ_BUILD_TYPE` | Release | Тип сборки |
| `VIZ_LOW_MEMORY` | OFF | Low-memory узел |
| `VIZ_BUILD_TESTNET` | OFF | Testnet-сборка |

**Требования:** Visual Studio 2019+ с нагрузкой "Desktop development with C++", CMake 3.16+.

---

## Docker

В репозитории поставляются Dockerfile для нескольких конфигураций:

| Dockerfile | Описание |
|-----------|---------|
| `Dockerfile-production` | Полный узел мейннета (Release) |
| `Dockerfile-testnet` | Testnet (`BUILD_TESTNET=ON`) |

Все Dockerfile используют двухэтапную сборку для минимизации размера образа и пакеты Boost 1.71 (`libboost-coroutine-dev`, `libboost-context-dev`).

---

## Параметры CMake

| Параметр | По умолчанию | Описание |
|---------|-------------|---------|
| `BUILD_TESTNET` | OFF | Сборка для testnet |
| `LOW_MEMORY_NODE` | OFF | Исключить неконсенсусные данные (уменьшает RAM) |
| `CHAINBASE_CHECK_LOCKING` | OFF | Включить проверку блокировок (только для разработки) |
| `BUILD_SHARED_LIBRARIES` | OFF | Собирать разделяемые библиотеки |
| `USE_PCH` | OFF | Включить предкомпилированные заголовки (ускоряет пересборку) |

---

## Расширенный вариант: `configure_build.py`

Обёртка над CMake с разумными значениями по умолчанию и поддержкой кросс-компиляции:

```bash
# Release-сборка
python3 programs/build_helpers/configure_build.py --release --src ../..

# Debug с low-memory
python3 programs/build_helpers/configure_build.py --debug --low-memory

# Кросс-компиляция для Windows с MinGW
python3 programs/build_helpers/configure_build.py --win --release

# Пользовательские пути к зависимостям
python3 programs/build_helpers/configure_build.py \
  --boost-dir /opt/boost_1_74_0 \
  --openssl-dir /opt/openssl \
  --release
```

---

## Создание каркаса нового плагина

```bash
python3 programs/util/newplugin.py graphene myplugin
```

Генерирует: `CMakeLists.txt`, заголовок/реализацию плагина, заголовок/реализацию API в `libraries/plugins/myplugin/`.

---

## Цели сборки

| Бинарник | Описание |
|---------|---------|
| `vizd` | Основной демон узла |
| `cli_wallet` | Кошелёк командной строки |
| `js_operation_serializer` | Сериализатор операций для JavaScript |
| `size_checker` | Утилита анализа размеров |

---

## Устранение неполадок

**Версия Boost ниже 1.71:** Установите Boost 1.71+ из пакетного менеджера (Ubuntu 24.04 поставляет 1.74). На macOS `brew install boost` предоставляет актуальную версию. На Windows соберите из исходников с компонентом coroutine.

**Ошибка `Do not run this script as root`:** Используйте `sudo ./install-deps-linux.sh` для зависимостей, затем запускайте `./build-linux.sh` от обычного пользователя.

**Отсутствует компонент coroutine:** Убедитесь, что на Ubuntu/Debian установлены `libboost-coroutine-dev` и `libboost-context-dev`.

**macOS: OpenSSL не найден:** Задайте `OPENSSL_ROOT_DIR` вручную: `export OPENSSL_ROOT_DIR=$(brew --prefix openssl)`.

**Windows MinGW: отсутствуют переменные:** Перед запуском `build-mingw.bat` должны быть заданы `BOOST_ROOT` и `OPENSSL_ROOT_DIR`.

---

См. также: [Разработка плагинов](./plugin-development.md), [Тестирование](./testing.md), [Обзор плагинов](../plugins/overview.md).
