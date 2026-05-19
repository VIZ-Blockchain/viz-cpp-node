# P2P 同步场景

本页描述 DLT P2P 层如何处理常见同步情况：初始启动、停机后追赶、DLT 范围间隙、fork 恢复和紧急共识。

---

## 节点分类

以下场景使用 4 节点参考配置：

| 角色 | 描述 |
|------|------|
| **Master** | FORWARD 模式；DLT 区块日志 `[A..B]`；持有紧急私钥 |
| **Slave (NEAR)** | Head 在 `A-1`（恰好毗邻 master 的 DLT 范围） |
| **Slave (FAR)** | Head 显著低于 `A`（不在 master 的 DLT 范围内） |
| **Fresh node** | 无区块；仅 genesis 状态 |

---

## 场景 1：NEAR Slave（head 毗邻 master 的 DLT 范围）

**配置：** Master DLT 范围 `[1000-2000]`。Slave head = 999。

### Hello 握手

1. Slave 发送 hello：`head_num=999, head_id=H999`。
2. Master 的 `check_fork_alignment` — 多级检查：
   - `head_num=999` 低于 `dlt_earliest=1000` — 不在范围内。
   - `head_num + 1 == dlt_earliest (1000)` → **边界链接检查**：读取区块 1000，验证 `block_1000.previous == H999`。
   - 匹配 → `fork_alignment = true`，`exchange_enabled = true`。
3. Master 回复：`exchange_enabled=true, fork_alignment=true`。
4. Slave 在 master 上进入 **SYNCING** 生命周期状态。

### 区块同步

Slave 请求 `dlt_get_block_range(start=1000, end=1199, prev=H999)`。Master 从其 DLT 日志响应区块 1000–1199。Slave 应用每个区块。此过程以 200 个区块为批次重复，直到 slave 达到区块 2000 且 `is_last=true` 触发 `transition_to_forward()`。

**结果：** 无需快照下载的干净 P2P 同步。无退避惩罚。

---

## 场景 2：FAR Slave（head 远低于 master 的 DLT 范围）

**配置：** Master DLT 范围 `[1000-2000]`。Slave head = 800。

### Hello 握手

1. Slave 发送 hello：`head_num=800, head_id=H800`。
2. Master 的 fork 对齐检查：`800 < 1000`，边界链接检查失败（`800 + 1 ≠ 1000`），LIB 回退也失败（LIB ID 已裁剪）。
3. `fork_alignment = false`，但 `exchange_enabled = false`。
4. Master **不断开** slave，因为 `hello.node_status == SYNC` — SYNC 对端始终进入 ACTIVE 生命周期状态。

### 同步尝试

Slave 在 master 上进入 ACTIVE 生命周期状态。由于 `exchange_enabled = false`，master 不发送 forward 区块。Slave 尝试区块范围请求：`request_blocks_from_peer` 检测到 `our_head+1 (801) < peer_dlt_earliest (1000)` — **检测到间隙**。

节点在所有连接的对端中搜索 DLT 范围覆盖区块 801 的对端。如果找到，该对端用作桥接同步源。如果没有对端能弥补间隙：

```
[P2P] Gap detected: our_head=800, nearest_peer_dlt_earliest=1000
      No peer can serve blocks 801-999. Snapshot import may be required.
```

约 90 秒无 head 进展后，快照插件的停滞同步检测器触发，从受信对端发起快照下载（通过 `trusted-peer-for-snapshot` 配置）。在区块 1500 导入快照后，slave 重新进入 SYNC 模式并正常追赶。

---

## 场景 3：Fresh Node（无区块）

**配置：** 节点无区块；`head_num=0, head_id=zero_id`。

### Hello 握手

1. 新节点发送 hello：`head_num=0`。
2. Master 的 fork 检查：`head_num == 0` → **空对端** → `fork_alignment = true`（视为"新节点，尚未在任何 fork 上"）。
3. `exchange_enabled = true`（master 将接受来自此节点的区块）。
4. 新节点在 master 上进入 ACTIVE 生命周期状态。

### 同步尝试

在 `request_blocks_from_peer` 中，`our_head=0`，`peer_dlt_latest=2000`。但 `peer_dlt_earliest=1000`，所以最早可用的是区块 1000。请求从 `max(our_head+1, peer_dlt_earliest) = 1000` 开始。节点收到区块 1000+，但无法应用它们，因为链数据库没有区块 1000 之前的状态。

快照插件检测到停滞并下载快照（例如在区块 1500）。导入后，新节点从区块 1500 → 2000 正常追赶。

---

## 场景 4：崩溃后重启

**配置：** 节点在 head 1912，DLT 范围 `[1750-1912]`。重启后，对端在区块 2000。

### 启动恢复

1. `database::open()` 检查 DLT 区块日志一致性：如果日志 head 与数据库 head 匹配 → 一致；否则重置日志。
2. 来自 DLT 区块日志的最后 **100 个区块**被播种到 `fork_db`（区块 1813–1912）。这为新到达的区块提供 100 个区块的父窗口，无需先获取它们。
3. **60 秒宽限期**适用：启动后前 60 秒，head 附近 10 个区块内的区块被视为 `FORK_DB_ONLY` 而非 `DEAD_FORK`。这防止了"拒绝级联"——当对端重放 fork_db 尚不知道的接近 head 的区块时。

### 追赶

节点重新进入 SYNC 模式，从 1913 开始请求区块。DLT 范围 `[1800-2000]` 的对端可以服务所有所需区块。节点追赶到 2000 并过渡到 FORWARD。

---

## 场景 5：Fork 切换

**配置：** 节点在 fork A 的 head `H`。对端有 fork B 的 head `H'`，其中 `H' > H` 且 fork B 有更多投票权重。

### Fork 检测

1. 来自 fork B 的区块通过广播到达。Fork DB 将其链接到其父链。
2. 每个区块后调用 `track_fork_state()`。当 fork B 维持其领先 **42 个区块**（2 个完整的验证者轮次）时，运行 `resolve_fork()`。
3. `resolve_fork()` 计算每个分支上验证者的总投票权重（委托的 SHARES）。Fork B 必须在切换提交前维持 6 个连续区块的确认。

### Fork 切换执行

1. `pop_block()` 将 fork A 的区块回滚到公共祖先。弹出的交易进入 `_popped_tx`。
2. Fork B 的区块从公共祖先应用到新 head。
3. `_popped_tx` 和 `_pending_tx` 被重新应用；已在 fork B 链中的交易静默跳过。

**统计中的 fork 状态：** 转换 `NORMAL → LOOKING → NORMAL`（如果此节点在失败分支上则为 `MINORITY`）。

---

## 场景 6：紧急共识同步

**配置：** 网络停滞超过 3600 秒。紧急共识已激活。

### Master 运行

紧急 master（配置中有 `emergency-private-key` 的节点）使用"committee"签名密钥每轮生产所有 21 个区块。统计中：`+emrg +ekey`。

### 紧急期间的 Slave 同步

1. Slave 连接到 master。Master 的 hello 包含 `emergency_active=true, has_emergency_key=true`。
2. Slave 的 fork 对齐检查正常进行 — 从 P2P 层的角度来看，committee 区块是常规签名区块。
3. Slave 进入 SYNC 模式，从 master 请求 committee 生产的区块。
4. 区块验证：`verify_signing_witness()` 在紧急期间放宽槽位生产者映射检查 — 如果区块生产者与确切的计划槽位不匹配，只要签名根据生产者的 `signing_key` 验证通过，就被接受。

### 验证者密钥恢复

当真实验证者恢复其签名密钥（通过 `validator_update_operation`）时，调度重建将它们纳入混合调度。一旦 21 个验证者槽位中有 **15 个**是真实的（非 committee），紧急模式停用。后续区块由真实验证者生产并正常同步。

---

## 场景 7：停滞同步恢复

**条件：** SYNC 模式，30 秒内未收到区块。

1. `sync_stagnation_check()` 触发：第 1/3 次重试 — 重新从所有启用交换的活跃对端请求区块。
2. 30 秒后：第 2/3 次重试。
3. 30 秒后：第 3/3 次重试。
4. 第三次重试后：带停滞警告的 `transition_to_forward()`。

如果节点在过渡到 FORWARD 时仍然落后，`check_forward_stagnation()` 将在 30 秒后检测到无 head 进展并过渡回 SYNC 模式，开始新循环。

---

## 场景 8：Gap Fill（间隙填补）

**条件：** FORWARD 模式；区块流中缺少 1–100 个区块。

Gap fill 在以下情况自动触发：
- 收到乱序区块（区块 N+2 在 N+1 之前到达）。
- `periodic_task()` 检测到 `highest_seen_block_num > our_head + 1`。
- 快照暂停后调用 `resume_block_processing()`。

**协议：**
1. 选择活跃对端中 `peer_head_num` 最高的对端。
2. 发送 `dlt_gap_fill_request(block_nums=[N+1, N+2, ...])`（最多 100 个区块）。
3. 等待回复最多 **15 秒**。
4. 收到后应用返回的区块。如果区块仍然缺失，在下一个周期任务中触发另一次 gap fill。

**如果没有对端能服务间隙**（没有有更高 head 的 exchange-enabled 或 SYNCING 对端），节点立即过渡到 SYNC 模式。

---

## 场景 9：SYNC ↔ FORWARD 振荡防止

**振荡的根本原因：** 从 FORWARD→SYNC 过渡后，同步停滞计时器继承了一个过时的时间戳，立即触发，`check_sync_catchup` 看到零个前方对端 → 过渡回 FORWARD。循环继续。

**已实施的修复：**
- `transition_to_sync()` 将 `_last_block_received_time` 重置为 `now`，使停滞计时器重新开始。
- `check_forward_stagnation()` 当所有连接对端与我们节点 head 相同时**不**过渡到 SYNC — 当没有人在前面时没有必要同步。
- `check_sync_catchup()` 当零个活跃对端存在时**不**声称"已追上"；而是启动 60 秒隔离计时器。
- 60 秒隔离后，`emergency_peer_reset()` 清除所有软封禁和退避，强制立即重连所有已知对端。

---

## 场景 10：Dead Fork 区块

**条件：** 对端发送来自在节点 fork DB 窗口之前分叉的链的区块。`push_block()` 抛出 `unlinkable_block_exception`，区块编号 ≤ `head_block_num`。

**行为：**
1. `dlt_delegate::accept_block()` 返回 `DEAD_FORK`。
2. 区块**不**存储在 `fork_db._unlinked_index` 中（防止内存增长）。
3. 对端每个 dead-fork 区块累积一次 spam 计数。
4. 10 次计数后对端被软封禁 3600 秒。
5. 同步循环中断 — 当前批次中不再处理来自此对端的区块。

**宽限期（P22 修复）：** 节点启动后前 60 秒，当前 head 附近 10 个区块内以 `unlinkable_block_exception` 失败的区块返回为 `FORK_DB_ONLY`（而非 `DEAD_FORK`）。这防止了在 fork_db 从最后 100 个区块种子完全重建之前，对发送接近 head 区块的合法对端的误封禁。

---

## 与同步相关的配置

| 设置 | 默认值 | 效果 |
|------|--------|------|
| `seed-node` | — | 静态对端；`emergency_peer_reset()` 后重连 |
| `dlt-block-log-max-blocks` | 100000 | DLT 日志容量；影响对端可向后弥合间隙的程度 |
| `trusted-peer-for-snapshot` | — | 接受快照下载的对端 |
| `stalled-sync-timeout-minutes` | 2 | 快照插件触发恢复前的分钟数 |
| `enable-stale-production` | false | 允许验证者在未同步时生产（仅开发用） |

---

参见：[P2P 概述](./overview.md)、[转发模式](./forward-mode.md)、[紧急共识](../consensus/emergency-consensus.md)、[快照](../node/snapshot.md)。
