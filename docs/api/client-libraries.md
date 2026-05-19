# Client Libraries

Official client libraries are available for Python, PHP, JavaScript, and Swift. All libraries communicate with a VIZ node over the JSON-RPC API and handle transaction signing locally.

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

## Swift — viz-swift-lib

**Repository:** https://github.com/VIZ-Blockchain/viz-swift-lib

Low-level, unopinionated Swift library providing primitives — operations, transactions, signing, JSON-RPC — without hiding any details. Suitable for mobile wallets, backend services, and bots where full control is required.

### Installation

Swift Package Manager — add to `Package.swift`:

```swift
dependencies: [
    .package(
        url: "https://github.com/viz-blockchain/viz-swift-lib.git",
        .upToNextMinor(from: "0.1.0")
    )
]
```

Or in Xcode: **File → Add Packages…** and enter the repository URL.

### Quick start

```swift
import VIZ

let client = VIZ.Client(address: URL(string: "https://node.viz.cx")!)

// Fetch chain head for the reference block
let props = try await client.send(VIZ.API.GetDynamicGlobalProperties())

// Derive the signing key
let key = VIZ.PrivateKey(seed: "alice" + "active" + "password")!

// Build the operation
let transfer = VIZ.Operation.Transfer(
    from: "alice",
    to: "bob",
    amount: VIZ.Asset(10.0, .viz),
    memo: "Thanks!"
)

// Wrap in a transaction, sign, and broadcast
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

### Key utilities

```swift
// Derive keys from seed
let privateKey = VIZ.PrivateKey(seed: "username" + "active" + "password")!
let publicKey  = privateKey.createPublic()
print(publicKey.address)  // VIZ7…
print(privateKey.wif)     // 5K…

// Import from WIF
let imported = VIZ.PrivateKey("5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3")!

// Assets
let amount = VIZ.Asset(100.5, .viz)    // "100.500 VIZ"
let shares = VIZ.Asset(1000.0, .vests) // "1000.000000 VESTS"
```

### Features

- Full operation coverage: transfers, awards, account create/update, vesting, escrow, recovery, and more
- Composable transactions — multiple operations in one signed transaction
- Pure-Swift `secp256k1` ECDSA signing; key derivation from seed; WIF import/export
- `async/await` JSON-RPC client with `actor`-based concurrency, `Sendable` types throughout
- Multi-signature authority composition
- Delegated signing via `viz://` URLs
- Cross-platform: iOS 13+, macOS 10.15+, tvOS 13+, watchOS 6+, Linux
- No hidden state — key management, retries, and broadcasting are left to the caller
- MIT license

---

## Choosing a Library

| Criterion | Python | PHP | JavaScript | Swift |
|-----------|--------|-----|-----------|-------|
| Install | `pip` | Manual PSR-4 | `npm` / CDN | Swift PM / Xcode |
| Transport | WS / HTTP | HTTP | WS / HTTP | HTTP (async/await) |
| Runtime | Python 3 | PHP 7+ | Node.js / browser | iOS / macOS / Linux |
| Dependencies | minimal | GMP or BCMath | none (bundled) | secp256k1, OrderedDictionary |
| Level | high-level | high-level | high-level | low-level primitives |
| License | MIT | MIT | MIT | MIT |

---

See also: [JSON-RPC API](./json-rpc.md), [CLI Wallet](./cli-wallet.md).
