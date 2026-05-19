# Client Libraries

Official client libraries are available for Python, PHP, and JavaScript. All libraries communicate with a VIZ node over the JSON-RPC API and handle transaction signing locally.

---

## Python — viz-python-lib

**Repository:** https://github.com/VIZ-Blockchain/viz-python-lib

### Installation

```bash
pip install viz-python-lib
```

### Quick start

```python
from viz import Client

viz = Client(
    node="wss://node.viz.cx/ws",
    keys=["5...private_key..."]
)

# Award energy to an account
viz.award("receiver_account", 10.5, "with love", None, "your_account")
```

### Features

- WebSocket (`wss://`) and HTTP (`https://`) transport
- Full transaction broadcasting: all protocol operations
- Local key management — private keys never leave the client
- Automatic retry and failover across multiple nodes
- Python 3 compatible

---

## PHP — viz-php-lib

**Repository:** https://github.com/VIZ-Blockchain/viz-php-lib

### Installation

The library uses PSR-4 autoloading and does not require Composer. Clone or download the repository and include the autoloader:

```php
require_once '/path/to/viz-php-lib/autoload.php';
```

**PHP extension requirements:** `gmp` (preferred) or `bcmath` for big-integer arithmetic.

### Quick start

```php
$private_key = '5...your_private_key...';
$tx = new VIZ\Transaction('https://api.viz.world/', $private_key);

// Build and broadcast an award transaction
$tx_data   = $tx->award($account, 'committee', 1000, 0, 'memo');
$tx_status = $tx->execute($tx_data['json']);
```

### Features

- All 40 protocol operations supported
- AES-256-CBC memo encryption/decryption
- No external dependencies — standard PHP extensions only
- Compatible with PHP 7.x and 8.x

---

## JavaScript — viz-js-lib

**Repository:** https://github.com/VIZ-Blockchain/viz-js-lib
**npm:** https://www.npmjs.com/package/viz-js-lib

### Installation

```bash
npm install viz-js-lib --save
```

### CDN (browser)

```html
<!-- jsDelivr -->
<script src="https://cdn.jsdelivr.net/npm/viz-js-lib/dist/viz.min.js"></script>

<!-- Unpkg -->
<script src="https://unpkg.com/viz-js-lib/dist/viz.min.js"></script>
```

### Quick start

```js
const viz = require('viz-js-lib');

// Derive WIF key from credentials
var wif = viz.auth.toWif(username, password, 'regular');

// Broadcast a vote
viz.broadcast.vote(wif, voter, author, permlink, weight, function(err, result) {
    console.log(err, result);
});
```

### Node configuration

```js
viz.config.set('websocket', 'wss://node.viz.cx/ws');
// or HTTP:
viz.config.set('websocket', 'https://node.viz.cx/');
```

### Features

- WebSocket (`ws://` / `wss://`) and HTTP (`http://` / `https://`) transports
- Works in Node.js and modern browsers
- Key utilities: `toWif`, `toPublic`, `isWif`, `signTransaction`
- Full broadcast API covering all protocol operations
- MIT license

---

## Choosing a Library

| Criterion | Python | PHP | JavaScript |
|-----------|--------|-----|-----------|
| Install | `pip` | Manual PSR-4 | `npm` |
| Transport | WS / HTTP | HTTP | WS / HTTP |
| Runtime | Python 3 | PHP 7+ | Node.js / browser |
| Dependencies | minimal | GMP or BCMath | none (bundled) |
| License | MIT | MIT | MIT |

---

See also: [JSON-RPC API](./json-rpc.md), [CLI Wallet](./cli-wallet.md).
