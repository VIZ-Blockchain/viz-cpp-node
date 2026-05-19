# 安全实现

VIZ Ledger 的安全模型基于三个支柱：基于阈值的权限验证、确定性签名验证和加密的对等节点传输。本页介绍每个子系统，并为运营者和插件开发者提供指导。

---

## 权限模型

每个账户有三个权限级别——master、active 和 regular——每个级别表示为带有权重阈值的密钥和/或账户引用的加权集合。

```
Authority {
    weight_threshold: uint32
    key_auths:     { PublicKey → weight }
    account_auths: { AccountName → weight }
}
```

当提供的签名权重之和（以及递归解析的账户权限）达到或超过 `weight_threshold` 时，操作即被授权。

**递归深度**受限，以防止嵌套账户权限链中的无限循环。

相同的权限结构作为 `SharedAuthority` 存储在共享内存中，使用与 Boost.Interprocess 映射文件兼容的进程间分配器。

---

## 签名验证

交易签名验证使用确定性的 secp256k1 ECDSA：

1. **摘要**：`sha256(chain_id || serialized_transaction)`
2. **恢复**：`secp256k1_recover(signature, digest)` → 公钥
3. **权限检查**：`sign_state.check_authority(account, level)` 遍历权限树，验证恢复的密钥集满足阈值。

`sign_state` 引擎：
- 维护提供的签名集及其恢复的密钥。
- 递归解析账户权限，直到最大深度。
- 验证后过滤未使用的签名。

**对于插件开发者：** 在处理敏感操作前，使用 `auth_util` 插件的 `check_authority_signature` API 验证签名：

```json
{
  "method": "auth_util.check_authority_signature",
  "params": ["alice", "regular", "<hex_digest>", ["<sig1>", "<sig2>"]]
}
```

若权限满足则返回已验证签名密钥的集合，否则返回错误。

---

## 对等节点传输加密

所有点对点连接使用 ECDH 密钥交换 + AES 流加密：

1. 每一方在连接时生成临时密钥对。
2. ECDH 从临时密钥生成共享密钥。
3. 使用共享密钥初始化 AES 编码器/解码器流。
4. 所有后续消息均被加密。

这防止了被动窃听。每个连接使用新鲜的临时密钥，因此一个会话的泄露不影响其他会话。

实现位于 `stcp_socket`（`libraries/network/stcp_socket.cpp`）。

---

## API 暴露

`webserver` 插件通过 HTTP 和 WebSocket 提供 JSON-RPC 服务。安全配置：

```ini
# 绑定到 loopback 仅供内部访问
webserver-http-endpoint = 127.0.0.1:8090
webserver-ws-endpoint = 127.0.0.1:8091

# 使用反向代理（nginx、caddy）通过 TLS 进行公共访问
```

**线程池大小：** Webserver 运行可配置数量的线程。将 `webserver-thread-pool-size` 设置为与预期并发请求负载匹配。大小不足会导致请求排队；过大会浪费资源。

```ini
webserver-thread-pool-size = 4
```

---

## 网络安全措施

- **加密通道**：所有对等节点连接均已加密（ECDH + AES）。被动窃听不可能实现。
- **对等节点数据库**：P2P 节点维护对等节点数据库和传播时间元数据。
- **软封禁**：行为不正确的对等节点（发送无效区块、仅分叉数据无进展）接受临时软封禁，而非永久断开连接。
- **带宽限制**：可通过 `max-send-buffer-size` 和相关 P2P 选项配置。

---

## 漏洞评估

**常见风险：**

| 风险 | 缓解措施 |
|------|----------|
| 通过畸形签名绕过权限 | 限制递归深度；严格的权重阈值检查 |
| 临时密钥的弱随机性 | 使用 secp256k1 的确定性密钥生成 |
| 未加密 RPC 上的中间人攻击 | 将 webserver 绑定到 loopback；对公共端点使用 TLS 反向代理 |
| 通过超大载荷的 DoS | JSON-RPC 载荷大小限制；webserver 线程池控制并发 |
| 嵌套权限耗尽 | 在 `sign_state` 中强制执行最大递归深度 |

**渗透测试检查表：**
- 提交畸形或不完整的签名以验证权限绕过保护。
- 使用深度嵌套的账户权限链测试递归深度限制。
- 通过网络抓包验证传输加密（线路上不应出现明文）。
- 对 webserver 端点进行压力测试，检测内存耗尽和队列饱和。

---

## 插件开发安全最佳实践

**输入验证：**
- 在插件边界拒绝畸形或超大的 JSON-RPC 载荷。
- 在处理前针对预期类型和范围验证所有参数。

**认证：**
- 在应用需要授权的状态变更前，始终使用 `auth_util.check_authority_signature`。
- 未验证签名前，永远不要信任账户名或密钥引用。

**恒定时间比较：**
- 对秘密比较使用 `fc::crypto::secure_compare` 或等效工具，以防止定时侧信道攻击。

**不存储明文凭据：**
- 永远不要在插件状态或日志中存储私钥。
- 每个会话生成临时密钥；永不复用。

**每个权限级别的威胁模型：**
- `regular` 权限：社交/内容操作——最低权限。
- `active` 权限：资金、质押、投票——中等权限。
- `master` 权限：密钥轮换、恢复——最高权限。在任何 UI 中需要明确的用户确认。

---

## 监控和事件响应

**需要监控的指标：**
- 对等节点连接数及流失率。
- Webserver 线程池队列深度和响应延迟。
- 签名验证失败率（在日志 `warn` 级别可见）。
- 每个对等节点连接的带宽。

**事件响应：**
1. 隔离受影响的端点（将 `webserver-http-endpoint` 限制为 loopback）。
2. 若验证者密钥泄露，通过 master 权限轮换签名密钥。
3. 密钥轮换后重新验证账户权限。
4. 检查 `sign_state` 失败和异常权限链周围的日志。

**密钥轮换：**
- 验证者签名密钥：使用新签名密钥执行 `update_validator` 操作。
- 账户密钥：使用新 master/active/regular 密钥执行 `update_account`。
- 所有密钥变更在下一个区块后立即生效。

---

参见：[插件开发](../development/plugin-development.md)、[数据类型](../protocol/data-types.md)、[验证者](../protocol/operations/validators.md)、[Webserver 插件](../plugins/webserver.md)。
