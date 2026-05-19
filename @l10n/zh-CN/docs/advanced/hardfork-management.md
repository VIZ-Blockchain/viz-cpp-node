# 硬分叉管理

VIZ Ledger 通过确定性硬分叉系统协调协议升级。硬分叉在编译时定义，由验证者共识激活，并在区块处理期间自动应用——无需节点重启。

---

## 工作原理

### 1. 定义文件

每个硬分叉在 `libraries/chain/hardfork.d/` 中有一个专用的 `*.hf` 文件，定义编译时常量：

```cpp
// 示例：9.hf
#define CHAIN_HARDFORK_9          9
#define CHAIN_HARDFORK_9_VERSION  version(1, 0, 9)
#define CHAIN_HARDFORK_9_TIME     (fc::time_point_sec(1650000000))
```

前言文件（`0-preamble.hf`）声明总数和 hardfork_property 对象架构。当前：`CHAIN_NUM_HARDFORKS = 12`。

### 2. 验证者共识

验证者通过 `versioned_chain_properties_update_operation` 发布其首选的下一个硬分叉版本。在每次验证者调度更新期间，节点：

1. 收集每个活跃验证者支持的硬分叉版本。
2. 如果多数同意的版本 ≥ 下一个计划版本，设置 `next_hardfork` 和 `next_hardfork_time`。

### 3. 区块处理期间的激活

当头区块时间超过 `next_hardfork_time` 且足够多的验证者支持该版本时，节点调用 `apply_hardfork(N)`。所有后续行为变化都通过评估器、通胀逻辑和链属性中的 `has_hardfork(N)` 检查来控制。

---

## 硬分叉历史

| HF | 主要变更 |
|----|---------|
| 1 | 中值计算修复 |
| 2 | 委员会批准阈值修复 |
| 3 | 小型协议更正 |
| 4 | 奖励操作、自定义操作序列 |
| 5 | 带宽和权限修复 |
| 6 | 验证者缺席惩罚、投票计数 |
| 7 | 社交/内容调整 |
| 8 | 协议清理 |
| 9 | 邀请系统、付费订阅、验证者费用、withdraw_intervals |
| 10 | 通胀模型 |
| 11 | 发行模型变更 |
| 12 | 紧急共识恢复（见下文） |

---

## HF12：紧急共识恢复

HF12 引入了区块生产停滞时的自动网络恢复。

### 激活

如果最后一个不可逆区块（LIB）的时间戳比挂钟时间滞后超过 `CHAIN_EMERGENCY_CONSENSUS_TIMEOUT_SEC`（1 小时），紧急模式自动激活。创建一个具有已知公钥（`CHAIN_EMERGENCY_WITNESS_PUBLIC_KEY`）的紧急验证者账户（`CHAIN_EMERGENCY_WITNESS_ACCOUNT = "committee"`），并插入区块生产调度中。

### 三态安全执行

| 网络状态 | 条件 | 行为 |
|---------|------|------|
| 健康 | 参与率 ≥ 33% | 强制执行安全默认值；覆盖手动配置 |
| 困难 | 参与率 < 33% | 遵守手动配置覆盖，以便运营者恢复 |
| 紧急 | 紧急模式激活 | 自动绕过过期和参与率检查 |

### 增强的验证者调度

- **混合调度**：紧急验证者填充不可用的槽位，同时保持真实验证者的位置。
- **投票加权分叉切换**：使用来自唯一非 committee 验证者的原始票数之和作为主要分叉比较标准。
- **中值排除**：紧急验证者的属性投票从链参数中值计算中排除。

---

## Hardfork Property 对象

持久化的 `hardfork_property_object`（chainbase 中的单例）跟踪：

| 字段 | 描述 |
|------|------|
| `processed_hardforks` | 已应用硬分叉时间的向量 |
| `last_hardfork` | 最后一个已应用硬分叉的 ID |
| `current_hardfork_version` | 当前强制执行的协议版本 |
| `next_hardfork` | 下一个计划的硬分叉版本 |
| `next_hardfork_time` | `next_hardfork` 何时激活 |

对于 HF12+，额外字段跟踪紧急共识状态。

---

## 数据库生命周期与硬分叉

### 打开时

```
database::open()
  → init_schema(), initialize_indexes(), initialize_evaluators()
  → 从 chainbase 加载 hardfork_property
  → init_hardforks()  ← 填充 _hardfork_times[] 和 _hardfork_versions[] 数组
  → assert: chainbase 修订版本 == head_block_num
```

### 重新索引时

从 block_log 重放所有区块，带跳过标志（无签名检查、无 merkle 检查）以提高速度。`apply_hardfork()` 在重放期间的每个硬分叉边界触发，确保确定性的状态重建。

### 应用区块时

```
process_hardforks()
  → 检查 next_hardfork_time 是否已过
  → 检查验证者共识是否支持该版本
  → 若是: apply_hardfork(N)
          → 运行版本特定的状态迁移
          → 更新 current_hardfork_version
```

---

## 回滚和分叉切换

数据库使用撤销会话进行原子性区块应用——部分失败会干净地回滚。

对于分叉切换，`fetch_branch_from()` 将两个分支返回到它们的共同祖先，弹出当前分支，然后重新应用新分支。HF12 在此过程中添加了投票加权链比较。

如果区块应用失败，fork 数据库条目被删除并抛出异常。P2P 层通过适当标记发送对等节点来处理该异常。

---

## 添加新硬分叉

1. **在 `libraries/chain/hardfork.d/` 中创建 `N.hf`：**
   ```cpp
   #define CHAIN_HARDFORK_N          N
   #define CHAIN_HARDFORK_N_VERSION  version(1, 0, N)
   #define CHAIN_HARDFORK_N_TIME     (fc::time_point_sec(UNIX_TIMESTAMP))
   ```

2. **在 `0-preamble.hf` 中将 `CHAIN_NUM_HARDFORKS` 递增到 N。**

3. **在评估器和运行时逻辑中控制新行为：**
   ```cpp
   if (db.has_hardfork(CHAIN_HARDFORK_N)) {
       // 新行为
   } else {
       // 旧行为
   }
   ```

4. **在 `database.cpp` 的 `apply_hardfork(N)` 中添加状态迁移：**
   ```cpp
   case CHAIN_HARDFORK_N:
       // 一次性迁移代码
       break;
   ```

5. **考虑紧急模式**：如果硬分叉修改了验证者调度或链参数，确保紧急验证者从受影响的计算中排除。

6. **使用重新索引测试**：对主网区块数据运行完整重新索引，以确认确定性重放产生相同状态。

---

## 故障排查

| 症状 | 可能原因 | 解决方案 |
|------|----------|---------|
| 硬分叉未触发 | 未达到验证者共识 | 验证验证者是否发布目标版本；检查 `get_next_scheduled_hardfork()` API |
| 打开时修订版本不匹配 | chainbase 修订版本 ≠ head block num | 从区块日志重新索引或从快照恢复 |
| 重新索引期间内存耗尽 | 共享内存太小 | 增加 `shared-file-size`；启用自动调整大小 |
| 紧急模式未激活 | HF12 尚未应用 | 验证 `current_hardfork_version` ≥ 1.0.12 |
| 重新索引后状态不匹配 | 非确定性的 has_hardfork() 分支 | 审计 `apply_hardfork()` 的副作用 |

**诊断：**

```json
{ "method": "database_api.get_hardfork_version", "params": [] }
{ "method": "database_api.get_next_scheduled_hardfork", "params": [] }
```

---

## 升级检查表

- [ ] 定义 `N.hf`，时间戳合理（与验证者协调）
- [ ] 在 `0-preamble.hf` 中递增 `CHAIN_NUM_HARDFORKS`
- [ ] 实现 `apply_hardfork(N)` 迁移
- [ ] 用 `has_hardfork()` 检查控制行为变化
- [ ] 部署前备份数据库和区块日志
- [ ] 以只读模式启动节点验证兼容性
- [ ] 监控日志中的硬分叉激活事件
- [ ] 与验证者协调发布新版本
- [ ] 确认 `get_next_scheduled_hardfork()` 显示预期的版本/时间

---

参见：[链属性](../governance/chain-properties.md)、[验证者](../protocol/operations/validators.md)、[数据库架构](./database-schema.md)、[Database API](../plugins/database-api.md)。
