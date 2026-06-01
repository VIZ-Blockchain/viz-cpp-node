# Клиентские библиотеки

Официальные клиентские библиотеки доступны для Python, PHP, JavaScript и Swift. Все библиотеки взаимодействуют с узлом VIZ через JSON-RPC API и выполняют подписание транзакций локально.

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
$tx = new VIZ\Transaction('https://api.viz.world/', $private_key);

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

## Swift — viz-swift-lib

**Репозиторий:** https://github.com/VIZ-Blockchain/viz-swift-lib

Низкоуровневая, минималистичная Swift-библиотека: предоставляет примитивы — операции, транзакции, подписание, JSON-RPC — без лишних абстракций. Подходит для мобильных кошельков, бэкенд-сервисов и ботов, где нужен полный контроль.

### Установка

Swift Package Manager — добавьте в `Package.swift`:

```swift
dependencies: [
    .package(
        url: "https://github.com/viz-blockchain/viz-swift-lib.git",
        .upToNextMinor(from: "0.1.0")
    )
]
```

Или в Xcode: **File → Add Packages…** и введите URL репозитория.

### Быстрый старт

```swift
import VIZ

let client = VIZ.Client(address: URL(string: "https://node.viz.cx")!)

// Получить параметры цепочки для reference block
let props = try await client.send(VIZ.API.GetDynamicGlobalProperties())

// Получить ключ подписания
let key = VIZ.PrivateKey(seed: "alice" + "active" + "password")!

// Создать операцию
let transfer = VIZ.Operation.Transfer(
    from: "alice",
    to: "bob",
    amount: VIZ.Asset(10.0, .viz),
    memo: "Спасибо!"
)

// Обернуть в транзакцию, подписать и транслировать
let tx = VIZ.Transaction(
    refBlockNum: UInt16(props.headBlockNumber & 0xFFFF),
    refBlockPrefix: props.headBlockId.prefix,
    expiration: props.time.addingTimeInterval(60),
    operations: [transfer]
)
let signed = try tx.sign(usingKey: key)
let confirmation = try await client.send(
    VIZ.API.BroadcastTransaction(transaction: signed)
)
```

### Работа с ключами

```swift
// Вывод ключей из seed
let privateKey = VIZ.PrivateKey(seed: "username" + "active" + "password")!
let publicKey  = privateKey.createPublic()
print(publicKey.address)  // VIZ7…
print(privateKey.wif)     // 5K…

// Импорт из WIF
let imported = VIZ.PrivateKey("5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3")!

// Активы
let amount = VIZ.Asset(100.5, .viz)    // "100.500 VIZ"
let shares = VIZ.Asset(1000.0, .vests) // "1000.000000 VESTS"
```

### Возможности

- Полное покрытие операций: переводы, награды, создание/обновление аккаунтов, vesting, escrow, восстановление и другие
- Компонуемые транзакции — несколько операций в одной подписанной транзакции
- Pure-Swift `secp256k1` ECDSA подписание; вывод ключей из seed; импорт/экспорт WIF
- JSON-RPC клиент на `async/await` с `actor`-архитектурой, типы `Sendable`
- Составные multi-sig authority
- Делегированное подписание через `viz://` URL
- Кроссплатформенность: iOS 13+, macOS 10.15+, tvOS 13+, watchOS 6+, Linux
- Нет скрытого состояния — управление ключами, повторы и трансляция остаются на стороне разработчика
- Лицензия MIT

---

## Выбор библиотеки

| Критерий | Python | PHP | JavaScript | Swift |
|----------|--------|-----|-----------|-------|
| Установка | `pip` | Ручная PSR-4 | `npm` / CDN | Swift PM / Xcode |
| Транспорт | WS / HTTP | HTTP | WS / HTTP | HTTP (async/await) |
| Среда | Python 3 | PHP 7+ | Node.js / браузер | iOS / macOS / Linux |
| Зависимости | минимальные | GMP или BCMath | нет (bundled) | secp256k1, OrderedDictionary |
| Уровень | высокоуровневый | высокоуровневый | высокоуровневый | низкоуровневые примитивы |
| Лицензия | MIT | MIT | MIT | MIT |

---

См. также: [JSON-RPC API](./json-rpc.md), [CLI Wallet](./cli-wallet.md).
