# 客户端库

Python、PHP、JavaScript 和 Swift 均有官方客户端库。所有库通过 JSON-RPC API 与 VIZ 节点通信，并在本地完成交易签名。

---

## Python — viz-python-lib

**仓库：** https://github.com/VIZ-Blockchain/viz-python-lib

### 安装

```bash
pip install viz-python-lib
```

### 快速开始

```python
from viz import Client

viz = Client(
    node="wss://node.viz.cx/ws",
    keys=["5...private_key..."]
)

# 向账户奖励能量
viz.award("receiver_account", 10.5, "with love", None, "your_account")
```

### 功能特性

- 支持 WebSocket（`wss://`）和 HTTP（`https://`）传输
- 完整的交易广播：所有协议操作
- 本地密钥管理——私钥不离开客户端
- 多节点自动重试和故障转移
- 兼容 Python 3

---

## PHP — viz-php-lib

**仓库：** https://github.com/VIZ-Blockchain/viz-php-lib

### 安装

该库使用 PSR-4 自动加载，无需 Composer。克隆或下载仓库后引入自动加载文件：

```php
require_once '/path/to/viz-php-lib/autoload.php';
```

**PHP 扩展要求：** `gmp`（首选）或 `bcmath`，用于大整数运算。

### 快速开始

```php
$private_key = '5...your_private_key...';
$tx = new VIZ\Transaction('https://node.viz.plus/', $private_key);

// 构建并广播奖励交易
$tx_data   = $tx->award($account, 'committee', 1000, 0, 'memo');
$tx_status = $tx->execute($tx_data['json']);
```

### 功能特性

- 支持全部 40 个协议操作
- AES-256-CBC 备注加密/解密
- 无外部依赖——仅使用标准 PHP 扩展
- 兼容 PHP 7.x 和 8.x

---

## JavaScript — viz-js-lib

**仓库：** https://github.com/VIZ-Blockchain/viz-js-lib  
**npm：** https://www.npmjs.com/package/viz-js-lib

### 安装

```bash
npm install viz-js-lib --save
```

### CDN（浏览器）

```html
<!-- jsDelivr -->
<script src="https://cdn.jsdelivr.net/npm/viz-js-lib/dist/viz.min.js"></script>

<!-- Unpkg -->
<script src="https://unpkg.com/viz-js-lib/dist/viz.min.js"></script>
```

### 快速开始

```js
const viz = require('viz-js-lib');

// 从凭据派生 WIF 密钥
var wif = viz.auth.toWif(username, password, 'regular');

// 广播投票
viz.broadcast.vote(wif, voter, author, permlink, weight, function(err, result) {
    console.log(err, result);
});
```

### 节点配置

```js
viz.config.set('websocket', 'wss://node.viz.cx/ws');
// 或 HTTP：
viz.config.set('websocket', 'https://node.viz.cx/');
```

### 功能特性

- 支持 WebSocket（`ws://` / `wss://`）和 HTTP（`http://` / `https://`）传输
- 可在 Node.js 和现代浏览器中使用
- 密钥工具：`toWif`、`toPublic`、`isWif`、`signTransaction`
- 覆盖所有协议操作的完整广播 API
- MIT 许可证

---

## Swift — viz-swift-lib

**仓库：** https://github.com/VIZ-Blockchain/viz-swift-lib

底层、无强制约定的 Swift 库，提供操作、交易、签名、JSON-RPC 等原语，不隐藏任何细节。适用于需要完全控制的移动钱包、后端服务和机器人应用。

### 安装

Swift Package Manager — 添加到 `Package.swift`：

```swift
dependencies: [
    .package(
        url: "https://github.com/viz-blockchain/viz-swift-lib.git",
        .upToNextMinor(from: "0.1.0")
    )
]
```

或在 Xcode 中：**File → Add Packages…** 并输入仓库 URL。

### 快速开始

```swift
import VIZ

let client = VIZ.Client(address: URL(string: "https://node.viz.cx")!)

// 获取链头参数用于引用区块
let props = try await client.send(VIZ.API.GetDynamicGlobalProperties())

// 派生签名密钥
let key = VIZ.PrivateKey(seed: "alice" + "active" + "password")!

// 构建操作
let transfer = VIZ.Operation.Transfer(
    from: "alice",
    to: "bob",
    amount: VIZ.Asset(10.0, .viz),
    memo: "谢谢！"
)

// 封装为交易、签名并广播
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

### 密钥工具

```swift
// 从种子派生密钥
let privateKey = VIZ.PrivateKey(seed: "username" + "active" + "password")!
let publicKey  = privateKey.createPublic()
print(publicKey.address)  // VIZ7…
print(privateKey.wif)     // 5K…

// 从 WIF 导入
let imported = VIZ.PrivateKey("5KQwrPbwdL6PhXujxW37FSSQZ1JiwsST4cqQzDeyXtP79zkvFD3")!

// 资产
let amount = VIZ.Asset(100.5, .viz)    // "100.500 VIZ"
let shares = VIZ.Asset(1000.0, .vests) // "1000.000000 VESTS"
```

### 功能特性

- 完整操作覆盖：转账、奖励、账户创建/更新、vesting、escrow、账户恢复等
- 可组合交易——一笔已签名交易中包含多个操作
- 纯 Swift `secp256k1` ECDSA 签名；从种子派生密钥；WIF 导入/导出
- 基于 `async/await` 的 JSON-RPC 客户端，`actor` 并发架构，全 `Sendable` 类型
- 多签 authority 组合
- 通过 `viz://` URL 进行委托签名
- 跨平台：iOS 13+、macOS 10.15+、tvOS 13+、watchOS 6+、Linux
- 无隐藏状态——密钥管理、重试和广播均由调用方负责
- MIT 许可证

---

## 如何选择库

| 标准 | Python | PHP | JavaScript | Swift |
|------|--------|-----|-----------|-------|
| 安装方式 | `pip` | 手动 PSR-4 | `npm` / CDN | Swift PM / Xcode |
| 传输协议 | WS / HTTP | HTTP | WS / HTTP | HTTP（async/await） |
| 运行环境 | Python 3 | PHP 7+ | Node.js / 浏览器 | iOS / macOS / Linux |
| 依赖项 | 极少 | GMP 或 BCMath | 无（已打包） | secp256k1、OrderedDictionary |
| 抽象层次 | 高层 | 高层 | 高层 | 底层原语 |
| 许可证 | MIT | MIT | MIT | MIT |

---

参见：[JSON-RPC API](./json-rpc.md)、[CLI Wallet](./cli-wallet.md)。
