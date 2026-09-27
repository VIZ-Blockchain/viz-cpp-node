# HF15 — PM 审计修复：激活清单与陈旧状态迁移

状态：下述两项修复**已实现，但未在 mainnet 排期**。分叉在两种配置中都会注册（`CHAIN_NUM_HARDFORKS`
均为 15），并由激活时间加验证者法定人数共同门控，因此 production 二进制在该时间到达后即会应用它；
在此之前不会执行任何内容，而无论哪种情况都不需要迁移。两个编译期时间戳见 §2。本文是面向运维的
清单：被门控的是什么、分叉如何注册与激活、需要验证什么，以及如何处置遗留路径已经留下的状态。

## 1. 分叉改变了什么

两个门控都以 `has_hardfork(CHAIN_PM_AUDIT_FIX_HARDFORK)` 形式在 `pm_evaluator.cpp` 中读取；分叉前
历史行为逐字节保留，因此常规重放不受影响。

**A. LMSR 流动性部分提取（`pm_withdraw_liquidity`，`market_type == 1`）。** 曲线按市场持有
`lmsr_b`，每条 LP 记录持有自己的 `b_share`，即该记录在其中的份额。*部分*提取过去会从一个已取整
（floored）的 `b_remove` 中扣除 `market.lmsr_b`，而记录的 `b_share` 保持不变 → 该记录继续声称拥有
比市场实际持有更多的曲线。该记录的下一次提取（通常是全额退出）会取走全部陈旧份额，并可能把
`lmsr_b` 压到 `<= 0`。修复后两条记录按同一个 `b_remove` 同步缩减，而 `b_remove` 会超过
`market.lmsr_b` 的提取将被**拒绝**（`FC_ASSERT`），而不是被截断（clamp）。

为什么拒绝而非截断：`lmsr_b <= 0` 并不是「平坦曲线」。`lmsr_q96` 会软失败——当 `b <= 0` 时
`lmsr_price`、`lmsr_buy_cost` 与 `lmsr_tokens_for_amount` 都直接返回 `0`，根本不会走到
`validate_domain`——于是所有结果定价为零，**下注不花任何成本**，而市场上仍留有 LP 资本和投注者的
本金。截断为零会把这种状态交给下一个调用者；拒绝则让定价曲线保持存活，而被拒绝的 LP 不会损失本金
（见 §4——结算时会全额返还）。

**B. 直接 `mode = 1` 下注（`pm_place_bet`）。** 带有 `allow_instant_bet = false` 与
`allow_batch = true` 的市场，其存在意义就是强制走抗抢跑流程，受支持的入口是
`pm_commit_bet` → `pm_reveal_bet`（托管、状态 5 的记录、在批次边界执行）。而直接的
`pm_place_bet` 搭配 `mode = 1` 会走到 *instant* 成交路径，即绕过该市场自己选择的保护，并按
instant 门控的价格成交。修复后它会被拒绝。历史不受影响：已产出区块中的 `mode = 1` 交易仍按分叉前
规则重放。这关闭了绕过路径；它本身并不证明即时成交对使用者是有利可图的。

两项修复都不改变对象布局，也不需要提升 schema：`apply_hardfork` 中没有 HF15 的 `case`
（此处 `default: break` 路径是正确的），快照导入不需要新分区，激活时也没有需要重算的内容。

## 2. 分叉如何注册与激活

* **注册是无条件的，两种配置都有。** `0-preamble.hf` 为所有构建把 `CHAIN_NUM_HARDFORKS` 设为 15，
  `hardfork.d/15.hf` 始终定义 `CHAIN_HARDFORK_15` / `CHAIN_PM_AUDIT_FIX_HARDFORK` 与
  `CHAIN_HARDFORK_15_VERSION`，`database_hardfork.cpp` 无条件注册 `_hardfork_times[15]` /
  `_hardfork_versions[15]`。数组大小为 `[CHAIN_NUM_HARDFORKS + 1]`，两种构建中索引都不会越界。
  门控该分叉的是激活时间戳与验证者法定人数——而不是构建类型。
* **为什么 production 也携带它。** 公共 testnet（`testnet.viz.world`，我们的解析器、预言机与客户端
  所连接的那个节点）是 **production 配置** 部署：它报告 `CHAIN_NAME "VIZ"`、`CHAIN_ID = sha256("VIZ")`
  以及 `CHAIN_HARDFORK_REQUIRED_VALIDATORS = 17`。`config_testnet.hpp` 会把 `CHAIN_NAME` 改成
  `VIZTEST`（因此改变 chain id）并把法定人数降到 1——把那样的镜像部署到现有 testnet 不是选项，
  这条链将不再是其快照与客户端所属的那条链。因此只在 `BUILD_TESTNET` 下注册的分叉**永远**无法在
  我们实际使用的 testnet 上激活：那里的 `has_hardfork(15)` 恒为 false，部署也证明不了任何东西。
  两种配置都注册、只以时间作区分，才能用 production 将要发布的那份产物完成「先在 testnet 部署」的
  验证。
* **激活时间是唯一的旋钮，且它是编译期常量。** `15.hf` 携带两个时间戳：`BUILD_TESTNET` 下
  `CHAIN_HARDFORK_15_TIME` = 2026-09-27 08:33:20 UTC（全新的 testnet 配置链可以立即激活），
  production 下 = **2026-10-05 00:00:00 UTC**。production 取值是**暂定的**——它不是已排期的 mainnet
  日期，它存在的目的是让 production 配置的 testnet 能够走到激活并被观察到；见下面的清单。修改它
  只是改一行再构建发布，因此在真正规划 HF14+HF15 发布时必须重新确认该日期。请把它保持在**未来**：
  时间戳落在过去时，分叉会在第 17 个验证者恰好升级的那个区块生效，没有可宣告的时刻（与 `14.hf`
  中的警告相同）。
* **版本，而非修订号。** `version(m, h, r)` 把分叉版本打进中间分量，`CHAIN_HARDFORK_VERSION` 就是
  该分量。因此新分叉必须推进 `CHAIN_VERSION` 本身——现在 `config.hpp` 与 `config_testnet.hpp` 都是
  `4.1.0`——因为 `hardfork_version` 会丢弃修订号，所以 `4.0.1` 在投票意义上与 `4.0.0` 无法区分。
  `database_hardfork.cpp` 断言 `CHAIN_HARDFORK_VERSION == _hardfork_versions[CHAIN_NUM_HARDFORKS]`，
  这正是版本与 `CHAIN_NUM_HARDFORKS` 必须在两种构建中同步推进的原因。
* **投票是自动的。** 当验证者已记录的投票与二进制的下一个分叉不一致时，
  `database.cpp::_generate_block` 会注入 `hardfork_version_vote`；当
  `CHAIN_HARDFORK_REQUIRED_VALIDATORS` 达成一致（testnet 所运行的 production 配置为 17，testnet
  构建为 **1**）**且**达到激活时间戳时，`process_hardforks()` 应用该分叉。testnet 的 21 个验证者槽位
  全部由同一个账户驱动，因此那里的法定人数是瞬时的，只由时间戳决定区块。
* **mainnet 激活清单。** (1) 确认或替换暂定日期，并提前足够时间公告，留出到时间戳的余量——这是唯一
  剩余的排期决定。(2) 运行 §3 的陈旧状态检测器，并就 §4 的迁移问题作出决定。(3) 发布镜像并让验证者
  更新——投票是自动的。(4) 从节点日志确认激活，并针对实时链重跑 §3 的检查。注意 HF14 自身的 mainnet
  日期（2026-08-28）已经过去，因此首次携带两个分叉的 mainnet 部署会在同一个区块激活它们
  （`process_hardforks` 在 `_hardfork_versions[last] < next_hardfork` 时循环）；快照中已处理 HF14 的
  testnet 是唯一能单独验证 HF15 的地方。
* **回滚。** 在激活时间戳之前，回退到旧镜像是安全的：分叉只是保持待定（状态中保留一个已投票但未
  应用的分叉；重新部署新镜像会清除它）。激活之后，标记是链上状态，因此不要回滚——HF15 之前的
  二进制不知道这个分叉，会在链已声明新规则的情况下按旧规则评估被门控的操作。此时应向前推进。

## 3. 验证

激活前（testnet 部署新镜像之后、时间戳未到之前）：`get_hardfork_property_object`
（或 `database_api.get_hardfork_property`）显示当前分叉仍为 14、下一个分叉的版本/时间处于待定、
验证者正在为它投票；下方检测器报告即将被门控的状态。

激活后：

* 分叉出现在 `processed_hardforks` 中，节点日志显示激活；
* 在 `allow_instant_bet = false` 的市场上直接 `pm_place_bet(mode = 1)` 被拒绝
  （`mode=1` 不再走到即时成交），而 `pm_commit_bet` → `pm_reveal_bet` 仍然可用；
* 部分提取 LMSR 后，该市场活跃记录的 `Σ b_share` 等于 `market.lmsr_b`；
* 对遗留路径留下的陈旧记录做全额退出会被「would drain the LMSR pricing curve」断言拒绝，
  且市场继续正常定价；
* 常规的重新部署后不变量：SHARES delta 为 0，TOKEN delta 等于该链的 legacy 锚点，
  `restarts 0`，列表非空。

## 4. 陈旧状态：检测什么、决定什么

修复后的不变量（按 LMSR 市场）：**其活跃 LP 记录的 `Σ b_share` 等于 `market.lmsr_b`。**
遗留偏差是单向的——曲线失去了 `b_remove`，而记录保留了完整的 `b_share`，因此记录合计声称的份额
*多于*市场持有。所以检测就是一次遍历：对 `market_type == 1` 的市场，汇总活跃记录的 `b_share` 并与
`lmsr_b` 比较；任何合计更大的市场至少有一条陈旧记录，而它下一次全额退出正是新门控拒绝的操作。

只有其 LP 在激活前做过部分提取的市场才会处于这种状态；分叉之后创建的不会，而所有 LP 退出都是
「全有或全无」的市场按构造就是一致的。

可选方案及链对各自的做法：

* **不做任何事（当前行为）。** 陈旧记录在退出时被拒绝，LP 无法提前取回其剩余的 `b_share`。不会有
  任何损失：本金在结算时全额返还，此时活跃市场的流动性下限已不再适用，而市场在此期间继续定价。
  代价：该记录的提前退出选项失效，且不一致只能通过拒绝被观察到。这是保守的、无需变更共识的方案，
  也是默认方案，因为偏差可以*测量*但无法*重建*——链只持有当前状态，没有逐笔提取历史可用来还原真实
  的归属。
* **一次性修复归属。** 对每个出现偏差的市场，把活跃记录的 `b_share` 按比例缩减到 `market.lmsr_b`
  （向下取整，余数给最后一条记录），使记录与曲线重新一致、全额退出恢复可用。在相同状态下它是确定性
  且幂等的，但这是共识可见的状态变更，需要自己的分叉门控迁移、自己的测试和评审——也就是说，这是一项
  独立的改动，而不是本次改动的附属品。
* **按市场的人工处置**（推动受影响的 LP，或解析/结算该市场）不是修复：结算无论如何都会返还本金，
  所以真正处在风险中的只是提前退出窗口。

因此，在为 HF15 排期之前网络需要作出的决定是：「拒绝陈旧退出、在结算时返还本金」是否可接受，还是
网络还需要一次性归属修复。这应由检测器的输出驱动——如果没有任何 LMSR 市场出现偏差，该问题即不成立，
分叉可以按原样排期。
