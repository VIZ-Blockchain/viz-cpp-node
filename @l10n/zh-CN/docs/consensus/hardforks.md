# 硬分叉

硬分叉是改变共识规则的网络升级。所有节点必须在计划的激活时间戳之前升级；运行旧版软件的节点将在激活后与网络分叉。

---

## 激活机制

每个硬分叉包含：
- 唯一**编号**（N）。
- **Unix 时间戳** — 硬分叉可以激活的最早系统时间。
- **验证者投票超多数** — 当前验证者集合中 >80% 必须通过 `validator_update_operation` 发出新硬分叉版本信号。

两个条件必须同时满足。验证者可以通过即使在计划时间戳后仍不提交版本投票来阻止不需要的硬分叉。

---

## 硬分叉历史

| # | 版本 | 关键变更 |
|---|------|---------|
| 1–10 | 1.x – 2.x | 基础、社交图、能量系统、委员会、订阅 |
| 11 | 3.0.0 | — |
| 12 | 3.1.0 | Fork 碰撞指标、投票加权 fork 比较、紧急共识模式、NTP 改进 |
| 13 | 3.2.0 | 验证者奖励共享，按投票比例分配 |

---

## HF12 摘要

HF12（版本 3.1.0）引入了：

1. **Fork 碰撞计数器** — `fork_collision_count` 和 `last_fork_collision_block_num` 添加到 `dynamic_global_property_object`。可通过 `get_dynamic_global_properties` 观察。
2. **投票加权 fork 比较**（`compare_fork_branches()`）— fork 选择使用每个验证者分支的总委托 SHARES + 较长链 10% 奖励。
3. **紧急共识模式** — 1 小时无区块后自动激活；"committee"账户接管所有 21 个槽位。参见[紧急共识](./emergency-consensus.md)。
4. **少数派 fork 自动重新同步** — 验证者插件检测节点隔离（连续 21 个自己的区块）并回滚到 LIB。
5. **NTP 改进** — 专用 NTP 客户端，可配置服务器、间隔和往返时间阈值。

---

## HF13 摘要

HF13（版本 3.2.0）引入了：

**验证者奖励共享**：每个区块的部分验证者奖励按比例重新分配给投票支持该验证者的账户（按其 SHARES 投票权重）。

- `validator_object` 上的新字段：`reward_percent` — 与投票者共享的区块奖励比例（0–10000 基点）。
- 新虚拟操作：`validator_reward_virtual_operation` — 每次奖励分配触发一次。
- 通过 `validator_update_operation` 设置。

---

## 实现新硬分叉

### 步骤 1：创建硬分叉定义文件

`libraries/chain/hardfork.d/N.hf`：

```cpp
#ifndef CHAIN_HARDFORK_N
#define CHAIN_HARDFORK_N N
#define CHAIN_HARDFORK_N_TIME  1234567890  // Unix 时间戳 — 必须是未来的时间
#define CHAIN_HARDFORK_N_VERSION hardfork_version(3, N, 0)
#endif
```

### 步骤 2：更新常量

`libraries/chain/hardfork.d/0-preamble.hf`：
```cpp
#define CHAIN_NUM_HARDFORKS N
```

`libraries/protocol/include/graphene/protocol/config.hpp`（如果对协议可见）：
```cpp
#define CHAIN_VERSION  (version(3, N, 0))
```

### 步骤 3：模式版本

如果任何 chainbase 对象布局发生变化（新字段、删除字段、调整大小的类型），在 `config.hpp` 中**递增 `CHAIN_SCHEMA_VERSION`**：

```cpp
#define CHAIN_SCHEMA_VERSION  uint32_t(N)
```

chain 插件在启动时检查此项。不匹配时在打开前清除 `shared_memory.bin`，防止从旧布局中读取损坏数据。

新字段应始终具有**零值默认值**以避免迁移代码：
```cpp
uint16_t my_new_field = 0;
```

### 步骤 4：连接到 database.cpp

`init_hardforks()`：
```cpp
FC_ASSERT(CHAIN_HARDFORK_N == N);
_hardfork_times[N]    = fc::time_point_sec(CHAIN_HARDFORK_N_TIME);
_hardfork_versions[N] = hardfork_version(CHAIN_HARDFORK_N_VERSION);
```

`apply_hardfork()` 的 case：
```cpp
case CHAIN_HARDFORK_N: {
    // 必要时进行迁移。如果零值默认值足够，留空并添加注释。
    break;
}
```

### 步骤 5：操作和评估器（如果有新操作）

1. 在 `chain_operations.hpp` 中添加带 `validate()` 和权限获取器的结构体。
2. 添加到 `operations.hpp` 中的 `static_variant`。
3. 在 `chain_evaluator.hpp` 中声明 `DEFINE_EVALUATOR(my_new_op)`。
4. 在 `.cpp` 评估器文件中实现 `do_apply()` — 始终先检查 `ASSERT_REQ_HF(CHAIN_HARDFORK_N, ...)`。
5. 在 `database.cpp` 的 `initialize_evaluators()` 中注册。

### 步骤 6：插件更新

| 插件 | 需要更新的内容 |
|------|-------------|
| `account_history` | 为任何新虚拟操作添加影响提取器 |
| `validator_api` | 将 `validator_object` 中的新字段添加到 `validator_api_object` |
| `snapshot` | 将新 chainbase 对象添加到 `serialize_state` / `load_snapshot` |

---

## 模式版本生命周期

```
新节点（无现有数据）：
  stored = 0, compiled = N → 不匹配
  清除 shared_memory（不存在时为无操作）
  写入 schema_version = N
  genesis → 正常启动

升级（旧二进制版本为 M < N）：
  stored = M, compiled = N → 不匹配
  清除 shared_memory.bin
  写入 schema_version = N
  db.open() → 版本不匹配异常
  → 自动恢复：快照导入 + dlt_block_log 重放

正常重启：
  stored = N, compiled = N → 匹配
  db.open() 正常进行
```

**关键文件：**
- `config.hpp` — `CHAIN_SCHEMA_VERSION`
- `plugins/chain/plugin.cpp` — 模式检查和清除逻辑
- `<data_dir>/schema_version` — 包含当前版本的纯文本文件

---

## 部署清单

- [ ] `CHAIN_NUM_HARDFORKS` 已递增
- [ ] `CHAIN_VERSION` 已更新（如果对协议可见）
- [ ] `CHAIN_SCHEMA_VERSION` 已递增（如果任何 chainbase 对象布局发生变化）
- [ ] 硬分叉 `.hf` 文件已创建，带有未来激活时间戳
- [ ] 所有新字段有零值默认值；`apply_hardfork` 注释解释为何不需要迁移
- [ ] 新评估器在 `initialize_evaluators()` 中注册
- [ ] 新虚拟操作在 `account_history` 插件中注册
- [ ] 如果 `validator_object` 发生变化，`validator_api_object` 已更新
- [ ] 如果添加了新 chainbase 对象，快照插件已更新

---

参见：[Fair-DPOS](./fair-dpos.md)、[紧急共识](./emergency-consensus.md)、[快照](../node/snapshot.md)。
