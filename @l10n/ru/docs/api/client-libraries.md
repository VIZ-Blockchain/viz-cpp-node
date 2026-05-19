# Клиентские библиотеки

Официальные клиентские библиотеки доступны для Python, PHP и JavaScript. Все библиотеки взаимодействуют с узлом VIZ через JSON-RPC API и выполняют подписание транзакций локально.

---

## Python — viz-python-lib

**Репозиторий:** https://github.com/VIZ-Blockchain/viz-python-lib

### Установка

```bash
pip install viz-python-lib
```

### Быстрый старт

```python
from viz import Client

viz = Client(
    node="wss://node.viz.cx/ws",
    keys=["5...private_key..."]
)

# Наградить аккаунт энергией
viz.award("receiver_account", 10.5, "with love", None, "your_account")
```

### Возможности

- Транспорт WebSocket (`wss://`) и HTTP (`https://`)
- Полная трансляция транзакций: все операции протокола
- Локальное управление ключами — приватные ключи не покидают клиент
- Автоматические повторные попытки и переключение между несколькими узлами
- Совместимость с Python 3

---

## PHP — viz-php-lib

**Репозиторий:** https://github.com/VIZ-Blockchain/viz-php-lib

### Установка

Библиотека использует автозагрузку PSR-4 и не требует Composer. Склонируйте или скачайте репозиторий и подключите автозагрузчик:

```php
require_once '/path/to/viz-php-lib/autoload.php';
```

**Требования к PHP-расширениям:** `gmp` (предпочтительно) или `bcmath` для работы с большими числами.

### Быстрый старт

```php
$private_key = '5...your_private_key...';
$tx = new VIZ\Transaction('https://node.viz.plus/', $private_key);

// Сформировать и транслировать транзакцию награды
$tx_data   = $tx->award($account, 'committee', 1000, 0, 'memo');
$tx_status = $tx->execute($tx_data['json']);
```

### Возможности

- Поддержка всех 40 операций протокола
- Шифрование/расшифровка мемо по алгоритму AES-256-CBC
- Без внешних зависимостей — только стандартные PHP-расширения
- Совместимость с PHP 7.x и 8.x

---

## JavaScript — viz-js-lib

**Репозиторий:** https://github.com/VIZ-Blockchain/viz-js-lib  
**npm:** https://www.npmjs.com/package/viz-js-lib

### Установка

```bash
npm install viz-js-lib --save
```

### CDN (браузер)

```html
<!-- jsDelivr -->
<script src="https://cdn.jsdelivr.net/npm/viz-js-lib/dist/viz.min.js"></script>

<!-- Unpkg -->
<script src="https://unpkg.com/viz-js-lib/dist/viz.min.js"></script>
```

### Быстрый старт

```js
const viz = require('viz-js-lib');

// Получить WIF-ключ из учётных данных
var wif = viz.auth.toWif(username, password, 'regular');

// Транслировать голос
viz.broadcast.vote(wif, voter, author, permlink, weight, function(err, result) {
    console.log(err, result);
});
```

### Настройка узла

```js
viz.config.set('websocket', 'wss://node.viz.cx/ws');
// или HTTP:
viz.config.set('websocket', 'https://node.viz.cx/');
```

### Возможности

- Транспорт WebSocket (`ws://` / `wss://`) и HTTP (`http://` / `https://`)
- Работает в Node.js и современных браузерах
- Утилиты для ключей: `toWif`, `toPublic`, `isWif`, `signTransaction`
- Полный broadcast API для всех операций протокола
- Лицензия MIT

---

## Выбор библиотеки

| Критерий | Python | PHP | JavaScript |
|----------|--------|-----|-----------|
| Установка | `pip` | Ручная PSR-4 | `npm` |
| Транспорт | WS / HTTP | HTTP | WS / HTTP |
| Среда | Python 3 | PHP 7+ | Node.js / браузер |
| Зависимости | минимальные | GMP или BCMath | нет (bundled) |
| Лицензия | MIT | MIT | MIT |

---

См. также: [JSON-RPC API](./json-rpc.md), [CLI Wallet](./cli-wallet.md).
