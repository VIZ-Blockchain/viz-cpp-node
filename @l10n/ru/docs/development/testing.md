# Тестирование

VIZ Ledger использует Boost.Test для модульных тестов и предоставляет вспомогательные программы для интеграционного тестирования.

---

## Модульные тесты

Бинарник модульных тестов собирается через CMake в рамках цели `libraries/chain`.

### Категории тестов

| Набор | Описание |
|-------|---------|
| `basic_tests` | Проверка основной функциональности |
| `block_tests` | Логика, специфичная для блокчейна |
| `live_tests` | Проверка сценариев прошлых хардфорков |
| `operation_tests` | Проверка операций |
| `operation_time_tests` | Временно-зависимые операции (вывод вестинга и т.п.) |
| `serialization_tests` | Проверки цикличной сериализации |

### Запуск тестов

```bash
# Запустить все тесты
./tests/chain_test

# Фильтр по набору
./tests/chain_test --run_test=basic_tests

# Фильтр по конкретному тест-кейсу
./tests/chain_test --run_test=basic_tests/my_test_case

# Настройка детализации
./tests/chain_test --log_level=all --report_level=detailed
```

### Параметры времени выполнения Boost.Test

| Параметр | Значения | Описание |
|---------|---------|---------|
| `--log_level` | all, success, test_suite, message, warning, error, cpp_exception, system_error, fatal_error, nothing | Детализация лога |
| `--report_level` | no, confirm, short, detailed | Детализация отчёта |
| `--run_test` | `<suite>` или `<suite>/<test>` | Фильтр запускаемых тестов |

---

## Покрытие кода

Включите покрытие в Debug-сборке:

```bash
cmake -DCMAKE_BUILD_TYPE=Debug -DCOVERAGE=ON ..
make chain_test

# Захватить базовую линию
lcov --capture --initial --directory . --output-file base.info

# Запустить тесты
./tests/chain_test

# Захватить трассировку тестов
lcov --capture --directory . --output-file test.info

# Объединить и очистить
lcov --add-tracefile base.info --add-tracefile test.info --output-file merged.info
lcov --remove merged.info '/usr/*' '*/tests/*' --output-file coverage.info

# Сгенерировать HTML-отчёт
genhtml coverage.info --output-directory coverage_report
```

---

## Интеграционные утилиты

### Утилита тестирования block log

Утилита `test_block_log` проверяет хранение и извлечение блоков:

```bash
# Собирается в programs/util/test_block_log
./test_block_log /tmp/test_block_log_dir
```

Открывает block log, добавляет подписанные блоки, сбрасывает на диск и читает обратно. Полезна для проверки логики хранения блоков.

### Утилиты подписания транзакций

```bash
# Подписать транзакцию (JSON-вход построчно)
echo '{"ref_block_num":...}' | ./sign_transaction

# Подписать сырой дайджест
echo '{"digest":"...","wif":"5K..."}' | ./sign_digest
```

Обе утилиты выводят вычисленные `digest`, `sig_digest`, ключ подписи и подпись. Полезны для диагностики сбоев подписания путём сравнения sig_digest с подписями из кошелька.

---

## Тестовый API-плагин

Плагин `test_api` предоставляет два JSON-RPC API (`test_api_a` и `test_api_b`) для интеграционных тестов. Регистрируется в `programs/vizd/main.cpp` и загружается процессом узла.

---

## Тестовая сеть

Для изолированного тестирования используйте конфигурацию testnet:

```bash
# Запустить узел testnet
vizd --config share/vizd/config/config_testnet.ini

# Или собрать Docker-образ testnet
docker build -f share/vizd/docker/Dockerfile-testnet -t viz-testnet .
```

Снапшот `share/vizd/snapshot-testnet.json` доступен для быстрой инициализации testnet.

---

## Непрерывная интеграция

CI-матрица собирает Docker-образы для нескольких вариантов:

| Вариант | Dockerfile |
|---------|-----------|
| Стандартный | `Dockerfile-production` |
| Low-memory | `Dockerfile-lowmem` |
| MongoDB | `Dockerfile-mongo` |
| Testnet | `Dockerfile-testnet` |

Сборки запускаются для каждой ветки и тега, с публикацией артефактов при наличии учётных данных.

---

## Написание новых тестов

Добавляйте новые наборы тестов к существующей цели с помощью макросов Boost.Test:

```cpp
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(my_feature_tests)

BOOST_AUTO_TEST_CASE(basic_case) {
    BOOST_CHECK_EQUAL(1 + 1, 2);
}

BOOST_AUTO_TEST_SUITE_END()
```

Группируйте тесты в соответствующий категорийный набор. Отдавайте предпочтение интеграционным тестам, работающим с реальным состоянием цепочки, а не моковым — для выявления расхождений между моком и продакшен-поведением.

---

См. также: [Сборка](./building.md), [Отладка](./debugging.md), [Разработка плагинов](./plugin-development.md).
