# 过关（Parlay / 串关）与系统投注 —— 已考虑并否决

状态：**作为共识原语被否决**（owner 决定 2026-08-19，q#603=A）。下面完整的
设计作为一份归档记录保留下来，说明*为什么*这个想法不适合协议，好让下一个「我们加个过关吧」的提案
从这份论证充分的否决出发，而不是从零开始。

## 为什么被否决

平台的核心信任不变量是**流动性提供者和 Lazy Pool 在构造上受本金保障**：下注在下注者之间零和
（赢家分输家的池子），池子/LP 只收费用和下限。存款人不必把自己的本金托付给市场创建者或预言机。
正是这个不变量让一个 permissionless 创建市场、预言机互相竞争的*去中心化*预测市场从根本上可行。

真正的过关需要一个在投注时刻锁定赔率、持有方向性风险的对手方。把 Lazy Pool 变成这个对手方就
破坏了这个不变量——存款人变成每个市场创建者质量的抵押品——而且这不是靠参数能修好的：

1. **腿的相关性是一个结构性的逆向选择洞。** `W = S·(1−m)/Π p_i` 只对*相互独立*的腿才公平。在
   permissionless 的世界里，市场创建者可以随意构造相关的腿（「X 赢得比赛」+「X 赢得第二局」——
   同一个现实事实被两个不同的预言机包装）。`Π p_i` 系统性地低估这类组合，给攻击者一个对池子
   持续的正 EV。市场之间的相关性是现实世界的语义——原则上在链上无法检测。中心化博彩公司用人类
   交易员和 per-combo 限额解决它；协议没有这一层。
2. **腿的价格来自可操纵的曲线。** 执行价报价（q#600=A）能防住开仓前一刻的一次性曲线操纵，但一条
   单薄的帕里姆图曲线仍然不是一个诚实的概率。对着攻击者能影响的价格源做固定赔率，意味着池子为
   别人对价格源的控制买单。
3. **让现有的一个池子前置产品（杠杆，F1/#300）变得安全花了多大代价。** 杠杆是池子前置资金的
   唯一地方，而它恰恰产生了这一失败类别：仓位利润超过输家池，缺口落到 LP 头上。它已经**被解决**
   ——提前退出奖励上限加结果相关的递延权益给出 `winners_pool ≥ (1−cap)·losers − fees ≥ 0`，即
   `uncovered == 0` **在构造上成立**，LP 收取路径只作为 loud 不变量违规日志之后的防御性回退保留。
   关键是那份保证的代价：杠杆是一笔*有界的、有抵押的*贷款，带清算扫描，而且仍然需要一个专门的
   cap、一个递延权益设计和一个常开的不变量来约束。过关簿有乘法赔付、没有可清算的抵押品、也没有
   per-leg 边界可以去封顶——同样的保证无处挂靠。
4. **共识复杂度 vs 一个 UX 特性。** 十个新的中位数参数、新对象、新的结算路径（void 重定价、争议
   交互、escrow FIFO）——全部都是 mainnet 之前要审计的金钱路径攻击面。

过关簿只有在做市商是一个中心化、完全受信的一方时才成立。那显然不是这个协议的信任模型。

## 取代它的是什么

- **优惠券**（一笔交易、N 个独立的 `pm_place_bet`）已经随 Forecaster 客户端发布——一笔无对手方的
  多腿投注。
- 客户端侧的 **auto-roll**（「顺序过关」）可以零共识改动地给出串关的感觉：客户端在一条腿结算后，
  把它的盈利再押到下一腿上。对手方 = 普通的帕里姆图池子；赔率不预先固定，这在帕里姆图定价下是
  诚实的。将来如果想要链上执行保证，可以用一个小的「市场 X 结算后下注」条件操作来加固。
- 如果将来出现一个完全受信的市场做市商，过关簿可以作为一个**独立的 opt-in 风险基金**运行
  （明确地*不是* Lazy Pool），存款人知情地接受庄家风险。在那之前不在范围内。

---

# 归档设计（否决之前，2026-08-18）

这条线以下的内容记录了被否决之前的设计，包括 q#600/q#601 这些在它还是候选者时锁定的范围决策。
仅供参考保留——其中没有任何一项是计划中的工作。

## 问题

随 Forecaster 客户端发布的优惠券（一笔交易携带 N 个独立的 `pm_place_bet` 操作）是一个*多腿投注*，
不是过关：每条腿各自结算，输赢相互独立。**真正的过关（accumulator/экспресс）** 是对 N 个结果的
*合取*下的单笔押注：只有**每**条腿都赢才赔付，而潜在赔付是各腿赔率的乘积。**系统投注「M 选 N」**
是标准泛化：押注被拆分到所有 C(N,M) 个 M 腿子过关上，所以票可以存活多达 N−M 条输腿。

帕里姆图市场没有固定赔率——一条腿的最终系数只有在其池子关闭时才知道。所以一个天真的「把最终
帕里姆图系数相乘」的过关无法由各腿自己的池子提供资金：跨市场的合取赔付不由任何单个市场的输家
支撑。过关需要一个显式的对手方和一个在投注时刻固定的价格。

## 设计概要

- **对手方：Lazy Pool** —— 同一个已经在前置杠杆贷款的持货基金。过关是对着池子按曲线价格的一笔
  边注；它**不**触及各腿的曲线或池子。
- **投注时刻固定价格**，取自每条腿的实时曲线（二元用 CPMM，多元用 LMSR-softmax）：组合价格
  `P = Π p_i`，潜在赔付 `W = S · (1 − pm_parlay_margin) / P`，带封顶。
- **全有或全无结算**，由各腿的常规预言机裁定驱动：任何一条腿输 → 票立即死掉；一条作废（no-contest）
  的腿被*排除*（它的 `p_i` 被乘回去——庄家标准做法）；所有剩余腿都赢 → 在最后一条腿结算后，池子
  自动支付 `W`。无 claim 操作，与 `pm_payout` 自动赔付的理念一致。
- **最坏情况 escrow**：池子在开仓时锁定 `W − S`，所以每张开着的票在构造上都被完全资金支持；押注
  `S` 立即进入 `pool.free_balance`。

## 机制

### 开仓：`pm_parlay_open`

```
pm_parlay_open {
  account,
  legs: [ { market_id, side (binary) | outcome_index (multi) }, ... ],
  amount,            // 押注 S，流动 VIZ
  min_payout,        // 对 W 的滑点保护（曲线可能在报价与纳入之间移动）
  extensions
}
```

验证 / 求值门槛（全部是 loud `FC_ASSERT`）：

1. `2 ≤ legs.size() ≤ pm_parlay_max_legs`；所有 `market_id` 互不相同。
2. 每条腿的市场：status 1（active），下注仍开放，且距该腿的 `betting_expiration` 至少有
   `pm_parlay_min_time_left` 秒（抗狙击：过关按实时曲线定价，所以对一条几乎关闭的腿的晚盘是
   最便宜的攻击）。
3. 每条腿的市场允许即时下注（`allow_instant_bet`），**未**被隐藏在预言机 risk-floor 之下，且其
   曲线深度通过操纵门槛（见下）。
4. 中位数 kill-switch `pm_parlay_enabled` 开启；池子有容量（见下）。
5. `S ≥ pm_min_bet`；账户有流动的 `S`（与 `pm_place_bet` 相同的资金规则）。

**腿价 `p_i`** 是**腿的比例虚拟大小的执行价**，而不是 mid：对着该边/结果上的一笔假设的 `S` 即时
下注给曲线报价，使用得到的平均价格。Mid 报价把价差免费送给攻击者；执行价让在开仓*之前*移动一条
单薄曲线先付出移动者自己的滑点。虚拟报价**不**改变曲线。

**组合赔付**：

```
P      = Π p_i                    (0 < p_i < 1，所以 P ∈ (0,1))
W_raw  = S · (1 − pm_parlay_margin) / P
W      = min(W_raw, pm_parlay_max_payout, S · pm_parlay_max_multiplier)
FC_ASSERT(W ≥ min_payout)         // 用户滑点保护
FC_ASSERT(W > S)                  // 一笔不可能盈利的过关是误点，拒绝
```

**开仓时的资金（单次平衡移动，守恒精确）**：

```
account.balance      -= S
pool.free_balance    += S
pool.parlay_fund_used += (W − S)        // 最坏情况 escrow，由上面的断言 W − S > 0
pool.free_balance    -= (W − S)
```

容量门槛：`parlay_fund_used + (W − S) ≤ free-only base × pm_parlay_fund_percent` —— owner 为杠杆
固定的同一 free-only 基数规则（q#566=A）：义务只对着 `free_balance` 衡量，从不针对 NAV。

### 对象

```
pm_parlay_object {
  id, account,
  legs: [ { market_id, side, outcome_index, price_ppm,   // p_i 开仓时固定，百万分之一
            state } ],                                    // 0 pending | 1 won | 2 lost | 3 void
  stake, payout,                 // S, W (asset)
  margin_ppm_at_open,
  opened_at,
  status,                        // 0 open | 1 won(paid) | 2 lost | 3 refunded(all-void)
  last_settled_leg_count
}
```

索引：`by_id`、`by_account`，以及 **`by_market_leg`（market_id → parlay id）**，这样 per-market
裁定无需扫描就能找到受影响的票。per-market 扇出由 `pm_parlay_max_open_per_market` 限制（开仓时通过
有界索引探测强制，无计数器，见 commit-cap 先例 M4 和 computed-vs-counter 规则）。

### 结算

挂到同一个已经完成赔付的 per-block `process_pm_markets()` 遍历里——过关的腿对腿市场达到
**settled** 状态（争议宽限期之后）做出反应，而不是对原始 resolve 反应，所以争议反转被自动尊重：

- **腿输** → 票 `status = 2` 立即：释放 escrow
  （`parlay_fund_used -= (W − S)`，`free_balance += (W − S)`）。押注已经躺在池子里——它就是池子
  在输票上的收入。发射虚拟操作 `pm_parlay_lost`。
- **腿作废**（no-contest / missed-resolution void）→ `state = 3`；赔付收缩：
  `W' = W · p_i`（把被排除腿的价格乘回去），钳制 `W' = max(W', S)`；释放 escrow 差额。如果**所有**
  腿都作废 → 退还 `S`（`status = 3`，池子退还押注，完整 escrow 释放）。发射 `pm_parlay_leg_void`。
- **腿赢** → `state = 1`；当**最后**一条 pending 腿以赢结算时：支付
  `pool.free_balance -= W; account.balance += W;` 释放 escrow 记账
  （`parlay_fund_used -= (W − S)`；多余的 `W − S` 在开仓时已经从 free 里划出，所以支付 `W` 让
  free_balance 相对 pre-open 净 `−S`——恰是池子在一张赢票上的损失）。`status = 1`，发射
  `pm_parlay_won`（用于 account_history 的 per-account 虚拟操作）。

每个已结算市场的工作是有界的：至多触及 `pm_parlay_max_open_per_market` 张票，每张 O(legs) ≤
`pm_parlay_max_legs`。无无界 per-block 循环（审计类别 H3/M3）。

**不变量**（debug 断言，像 TOKEN 锚那样做快照导入校验）：

1. `parlay_fund_used == Σ_open (W_i − S_i)` —— 可通过遍历开着的票重新计算。
2. `pool.free_balance ≥ 0` 恒成立（FIFO 队列规则未动；过关赔付走同一「永不为负」纪律——escrow
   保证资金存在）。
3. 票的终态是吸收态；`last_settled_leg_count` 单调。

### 系统投注「M 选 N」

一个操作 `pm_system_open`，同样的腿规则，另加 `2 ≤ M < N ≤ pm_parlay_max_legs` 且
`C(N,M) ≤ pm_system_max_combos`（例如 256——把最坏情况结算工作和 escrow 数学保持得简单有界）。
语义：押注 `S` 拆成 `C(N,M)` 等份子押注，每个子过关从同一固定 `price_ppm` 集合按上述方式定价/封顶；
escrow = 对所有组合求和。存储为一个对象（腿 + M + per-combo 派生数据在结算时计算，不存储）。
「7 选 8」= M=7, N=8, 8 个组合。结算：最后一条腿结算时，数 won/void 腿，算术枚举组合（无递归），
支付赢的组合的赔付之和。refund/void/shrink 规则按组合应用。推迟到**实现第二阶段**，但现在就规定好，
以免对象布局和参数来回折腾（snapshot-layout 教训：批次 B → 只能靠快照重新部署）。

## 对抗性评审（实现前）

| 攻击 / 失败类别 | 这里的向量 | 本设计中的缓解 |
|---|---|---|
| 曲线操纵（主要） | 拉升一条单薄腿的曲线，按扭曲的 `p_i` 买过关，平仓 | 执行价报价（移动者自付滑点），每条腿的 `pm_parlay_min_depth` 门槛（最小曲线流动性），`pm_parlay_margin` 庄家优势，硬上限 `max_payout`/`max_multiplier`，`min_time_left` 窗口 |
| 无界累积（#141 类） | `parlay_fund_used` 增长，稍后被减 | 每次终态转换释放 escrow，可重算的不变量 1，失配时钳制到 0 并 loud ilog |
| 符号翻转 / 下溢 | `W − S`、`W' = W·p_i` 收缩、退款 | 开仓时断言 `W > S`；void 收缩钳制在 `S`；所有减法钳制 `max(x,0)` + debug 断言 |
| 缺失下限/断言 | 「escrow 在构造上覆盖赔付」 | 每个维护区块对不变量 1 显式 debug 断言 + 快照导入重查（锚模式） |
| DoS / 每区块工作 | 一个市场上许多票；许多腿 | `max_open_per_market`（有界索引探测）、`max_legs`、`max_combos`，结算 O(tickets×legs) 有界 |
| 治理极端值（F3 类） | 中位数设 margin=0 / multiplier=10^9 | **每个**新参数的 `validate()` 边界（margin ≤ 20%，multiplier ≤ 10000×，legs ≤ 16，combos ≤ 1024，百分比参数 bp 检查 ≤10000）——并且每个参数**接入中位数循环**（retention 参数教训） |
| 预言机/争议交互 | 争议结算前先支付，然后反转 | 腿只对 *settled*（宽限后）状态反应，与 `pm_payout` 扫描同一截止 |
| 自我交易 LP | 下注者同时也是池存款人 | 无需特殊路径：池 P&L 与杠杆完全一样被社会化；margin + 上限约束抽取 |
| 快照往返 | 新对象/字段在导入时丢失 | 全反射导出；带 `contains()` 保护导入；forward-only 计数器要么播种、要么可重算（不变量 1 可重算——优先） |

## 新治理参数（chain_properties，下一个版本号）

`pm_parlay_enabled`（kill-switch，默认 **off** —— 杠杆先例），
`pm_parlay_margin`（bp，默认 500 = 5%，边界 ≤ 2000），
`pm_parlay_max_legs`（默认 8，边界 2..16），
`pm_parlay_max_multiplier`（默认 1000×，边界 ≤ 10000），
`pm_parlay_max_payout`（VIZ，默认 100k），
`pm_parlay_fund_percent`（池 free 的 bp，默认 2000，边界 ≤ 5000），
`pm_parlay_min_depth`（VIZ，默认 1000），
`pm_parlay_min_time_left`（秒，默认 3600），
`pm_parlay_max_open_per_market`（默认 1000，边界 ≤ 10000），
`pm_system_max_combos`（默认 256，边界 ≤ 1024）。

这十个都必须出现在：带边界的 `validate()`、中位数投票循环、`get_pm_chain_properties`、
序列化器（C++ ⇄ js ⇄ php ⇄ python lock-step——P1 的 vop/param drift 教训），以及
`chain_properties_pm` 的快照导出/导入。

## 客户端表面（节点落地后）

优惠券界面增加一个模式切换：**Multi**（今天的 N 个独立下注）/ **Экспресс**（一个
`pm_parlay_open`）/ **系统 M 选 N**（第二阶段）。优惠券已经以完全正确的形态收集腿；过关报价
（`Π p_i`、潜在赔付、上限）可以从下注表单使用的同一曲线读数在客户端计算，以 `min_payout` 作为
滑点保护。读 API：`get_account_parlays`、`get_market_parlays`（按 q#383=A 默认 newest-first），
活动里的过关卡片（历史/进行中标签）。

## 决策日志

- **q#600=A（2026-08-18）：** 腿价 = 押注虚拟大小在实时曲线上的执行价（非 mid）——曲线操纵者先
  付出自己的滑点。
- **q#601=A（2026-08-18）：** 第一轮 = 二元腿 + 简单过关；M-of-N 系统和多元（LMSR）腿是第二阶段。
  系统的对象布局仍在上文规定好，以免状态形态在轮次之间折腾。
- 新 `pm_parlay_*` 参数的发布默认值（margin 500 bp、max_payout 100k VIZ、max_multiplier 1000×、
  max_legs 8、kill-switch 默认 **off**）按提案保留，除非 owner 在实现前覆盖具体值；无论如何它们在
  发布后都可中位数投票，默认值只是给最初的中位数播种。
