# 核心概念

本页介绍在作为开发者、节点运营者或应用构建者使用 VIZ Ledger 之前需要了解的基本概念。

---

## 账户

VIZ Ledger 上的每个参与者都是一个**账户**。账户持有余额、创建内容、为验证者投票，并与所有协议功能交互。

### 账户命名规则

- 总长度：3–16 个字符
- 点号分隔的标签：每个标签 ≥ 3 个字符
- 每个标签以字母开头，以字母或数字结尾
- 仅允许小写字母（`a-z`）、数字（`0-9`）、连字符（`-`）
- 有效名称示例：`alice`、`alice.bob`、`viz-user1`

### 权限级别

每个账户有三个权限级别，每个级别持有一组密钥或账户代理：

| 级别 | 用于 | 使用的密钥 |
|------|------|----------|
| `master` | 更换密钥、账户恢复、高安全性操作 | 主密钥（离线保存） |
| `active` | 代币转账、质押、验证者投票 | 活跃密钥 |
| `regular` | 内容、奖励、委员会投票、社交操作 | 常规密钥 |

权限是多重签名结构：`{ weight_threshold, account_auths[], key_auths[] }`。当提供的签名权重之和达到或超过 `weight_threshold` 时，交易被授权。

---

## 代币

### VIZ — 流动代币

- 3 位小数：`"10.000 VIZ"`
- 用于转账、手续费和资金操作
- 可通过 `transfer_to_vesting_operation` 转换为 SHARES

### SHARES — 质押代币

- 6 位小数：`"10.000000 SHARES"`
- 代表投票权重和能量容量
- 通过质押 VIZ 创建；经 28 个间隔（≈28 天）提取回 VIZ
- 不可直接转让；可委托给其他账户

---

## 能量系统

能量是控制社交行为（奖励、投票）影响力的资源，与账户的 SHARES 份额相关。

| 属性 | 值 |
|------|----|
| 单位 | 基点：`0` = 0%，`10000` = 100% |
| 恢复速率 | 24 小时（86400 秒）完全恢复 |
| 恢复公式 | `current_energy = min(10000, last_energy + elapsed_sec * 10000 / 86400)` |

当账户以 `energy = 500`（5%）进行奖励时，该比例的账户 SHARES 用于确定奖励池分配。消耗能量不会"销毁"代币——它决定在奖励池中的权重。

---

## 验证者（区块生产者）

**验证者**（曾称为"见证人"）是生产区块并维护网络的账户。

- 任何账户都可以通过 `validator_update_operation` 注册为验证者。
- 代币持有者使用 SHARES 权重为验证者投票。
- 按投票权重排名靠前的验证者以轮询方式获得区块生产计划。
- 每个区块槽位恰好为 3 秒。
- 21 个验证者的一轮 = 21 个区块 = 63 秒。

### Fair-DPOS 参与机制

与标准 DPOS 不同，VIZ Ledger 对不活跃行为进行惩罚：
- 每个验证者都有基于近期区块生产的**参与度评分**。
- 如果全网参与度降至 `required-participation`（默认 33%）以下，区块生产将暂停。
- 错过过多区块的验证者会受到投票惩罚，惩罚持续 `witness_miss_penalty_duration` 秒。

---

## 区块和交易

### 区块

由验证者在其计划槽位生产的已签名交易集合。包含：
- `previous`：前一区块的哈希（链接）
- `timestamp`：确切的槽位时间
- `witness`：生产验证者的名称
- `transactions[]`：已签名交易列表
- `witness_signature`：验证者的签名

### 交易

一个或多个经过分组和签名的操作。属性：
- `ref_block_num = head_block_number & 0xFFFF`
- `ref_block_prefix` = 引用区块 ID 的第 4–7 字节（小端 uint32）
- `expiration`：必须在当前时间 60 秒内（推荐）
- `operations[]`：1 个或多个操作
- `signatures[]`：满足所有所需权限的 ECDSA 签名

### 操作

状态变更的原子单位。在交易内序列化为 `[type_id, operation_object]`。共有 64+ 种操作类型，涵盖转账、社交行为、治理等——参见[操作概述](../protocol/operations/overview.md)。

---

## 奖励池

通货膨胀持续添加到奖励池中。验证者和内容创作者从该池中获取奖励：

| 接收方 | 来源 |
|--------|------|
| 验证者 | 区块奖励的 `inflation_witness_percent` |
| 委员会 | `inflation_ratio_committee_vs_reward_fund` 比例 |
| 奖励基金 | 余额——通过奖励和内容投票分配 |

确切百分比由验证者通过 `versioned_chain_properties_update_operation` 以共识方式设定，由顶级验证者投票决定。

---

## Fork 与 LIB

**Fork 数据库**（`fork_db`）：最近接收的区块的内存树，这些区块可能尚未成为规范链的一部分。节点追踪所有候选 fork，始终延伸最重的（最多批准的）fork。

**LIB（Last Irreversible Block，最后不可逆区块）**：已被超过 2/3 验证者确认的最新区块。LIB 及其以下的区块永远不会被重组。一旦区块低于 LIB，它就会被写入永久区块日志。

---

## 快照

快照是特定区块编号时整个数据库状态的二进制转储。它允许新节点：
1. 下载快照文件
2. 在几秒内加载（而非重放整个区块历史）
3. 从快照区块高度恢复同步

快照由 `snapshot` 插件创建，对规范链没有影响——它们纯粹是运维工具。

---

## 链属性（治理参数）

链上共识参数由验证者通过 `versioned_chain_properties_update_operation` 控制。活跃参数是顶级 21 个验证者提交值的**中位数**。

关键参数包括：
- `account_creation_fee` — 创建新账户的费用
- `maximum_block_size` — 每个区块的最大字节数
- `inflation_witness_percent` — 验证者的区块奖励份额
- `witness_miss_penalty_percent` / `witness_miss_penalty_duration` — 错过惩罚
- `withdraw_intervals` — 质押提取间隔数

参见[链属性治理](../governance/chain-properties.md)查看完整参数列表。

---

## 硬分叉

协议升级以**硬分叉**的形式部署——在特定区块号处的计划激活。一旦 ≥17/21 个验证者表示支持某个硬分叉，它将在下一个计划区块处激活。硬分叉可以添加新的操作类型、更改共识规则或引入新的链属性。

参见[硬分叉](../consensus/hardforks.md)了解历史和升级流程。
