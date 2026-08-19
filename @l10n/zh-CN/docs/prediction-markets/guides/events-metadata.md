---
title: "事件与元数据 —— 客户端如何把市场聚合成比赛"
description: "Forecaster 如何把市场聚合成「事件」：event / event_title / child 元数据键、父市场与子市场、节点索引了什么，以及如何创建一个能被客户端识别为事件的市场。"
---

# 事件与元数据：市场如何被聚合成比赛

现实世界中的一场比赛，通常是若干个市场：「谁获胜」、「击杀总数」、「首个 Roshan」。
在链上它们是彼此独立的 `pm_market_object`，但客户端（Forecaster）把它们显示为一张事件
卡片，带有对结果的下注和一个「还有 N 条盘口」的链接。这种聚合**只通过元数据**发生 ——
共识中不存在特殊的「父」对象，而这是刻意为之：协议保持
最小化，而聚合关系由市场创建者在创建时设定。

## 简短模型

- 每个市场都带有一个自由格式的文本字段 `metadata`（`pm_create_market` 中的一个 JSON 字符串）。
- 节点从其中解析出一份**键的白名单**并建立索引；其余一切都被忽略。
- 具有相同 `event` 的市场就是「同一个事件」。没有 `child` 的市场是前台（父）市场，
  带 `child: 1` 的市场是子盘口（特殊玩法）。
- Forecaster：「事件」标签页按 `event` 聚合活跃市场，把胜负盘市场显示为
  前台市场，在通用列表中隐藏子市场，并在事件页面上把它们展示出来。

## 节点会索引的元数据键

节点只从 `metadata` 中提取以下字段（其他键不会被索引，但仍保留在
原始 JSON 中 —— 客户端可以自行读取）：

| 键 | 类型 | 用途 |
|------|-----|-------|
| `title` | string | 市场的人类可读问题（卡片标题）。 |
| `category` | string | 列表分区（`esports`、`sports`、`crypto`…）—— `by_category` 索引。 |
| `subcategory` | string | 对分区的细化（可选）。 |
| `tags` | array 或 CSV | 用于筛选的标签；客户端读取的是**数组** `market.metadata.tags`，它由节点自行重建。 |
| `image` | string (URL) | 卡片封面（一个链接，不在链上托管）。 |
| `description` | string | 简短的裁定规则 —— 「预言机将如何判定结果」。 |
| `event` | string (slug) | **事件聚合键。** 同一场比赛的所有市场设置相同的 `event`。 |
| `event_title` | string | 事件的人类可读名称（「Dota 2: MOUZ vs Vici — TI 2026」）。 |
| `child` | 1 / true | **一条子盘口（特殊玩法）。** 从分类/标签列表中隐藏；在事件页面上可见。 |
| `banned_jurisdictions` | array 或 CSV | 供客户端使用的司法辖区过滤器。 |
| `condition_id` | string | 来源的去重标识符（供镜像解析器使用）。 |

解析规则：`metadata` 必须是一个有效的 JSON 对象（非 JSON 的内容根本不会被索引）；
`tags`/`banned_jurisdictions` 既接受数组也接受 CSV 字符串；`child` 接受
`true`、`1` 或 `"1"`。标签匹配不区分大小写。

## 如何指定「父」市场

父市场不是显式指定的 —— 它是**由 `child` 的缺失推导出来的**：

1. 给这场比赛的所有市场相同的 `event`（一个稳定的 slug：拉丁字母、连字符 ——
   例如 `dota2-mouz-vg-2026-07-12`）以及相同的 `event_title`。
2. 对这场比赛的主市场（「谁获胜」/ 胜负盘）—— **不要设置** `child`。它就是
   父市场：它在所有列表中保持可见，并成为事件卡片的门面。
3. 对其余所有盘口（大小盘、让分盘、特殊市场）—— `child: 1`。它们会从
   通用列表中消失（分类里没有噪音），但在事件页面上以及通过直接链接仍然完全可用。

同一场比赛三个市场的最小 `metadata` 示例：

```json
// Parent (winner line) — WITHOUT child
{"title":"Will MOUZ beat Vici Gaming?","category":"esports","tags":["dota-2"],
 "event":"dota2-mouz-vg-2026-07-12","event_title":"Dota 2: MOUZ vs Vici Gaming"}

// Child line 1
{"title":"Total kills over 45.5 (map 1)?","category":"esports","tags":["dota-2"],
 "event":"dota2-mouz-vg-2026-07-12","event_title":"Dota 2: MOUZ vs Vici Gaming","child":1}

// Child line 2
{"title":"First Roshan — MOUZ?","category":"esports","tags":["dota-2"],
 "event":"dota2-mouz-vg-2026-07-12","event_title":"Dota 2: MOUZ vs Vici Gaming","child":1}
```

重要：`event` **在实践中是不可变的** —— 客户端按字符串精确匹配来聚合，所以要在
创建市场之前就把这个键定好，并在这场比赛的所有盘口上完全一致地使用它
（大小写与连字符都算）。

## 节点做了什么

- 用解析出的字段构建市场的元对象（`pm_market_meta`）并建立索引：按分类、
  按标签，以及**按事件**（`by_meta_event`）；元数据会进入快照。
- `list_markets_by_category(...)` 默认**隐藏子市场**（`hide_children = true`，
  第 8 个参数）—— 列表只显示父市场；传 `false` 可以看到所有盘口。
- `list_markets_by_event(event, from, limit)` 返回该事件的**全部**市场 —— 父市场与
  子市场，没有过滤。这是事件页面的 API。
- 在列表行中 `event_title` 在顶层返回，在完整市场卡片中则在
  `metadata` 内部；`tags` 由节点重建为数组。

## Forecaster 拿它做什么

- **「事件」标签页**（体育博彩视图）：活跃市场按 `event` 聚合；前台市场
  是标题看起来像胜负盘的那一个（`winner` / `moneyline` / `to win`），否则取
  第一个二元市场；卡片显示带当前赔率的各个结果（点一下就把一注加入投注单），以及
  一个「还有 N 条盘口」的链接。
- **事件页面** `#/event/<key>` —— 这场比赛的所有盘口在一个列表里（`list_markets_by_event`）。
- **卡片**在问题上方显示 `event_title`；「分类 › 标签」面包屑通向各个列表。
- **分类/标签列表**不显示子市场 —— 特殊玩法藏在事件卡片背后。

对创建者而言的实用要点：正确设置 `event`/`event_title`/`child`，就是
「十个散落的市场在列表里制造噪音」与「一张整洁的比赛卡片，所有盘口都在
里面」之间的区别。事件键弄错了，盘口就不会聚合；忘了 `child`，特殊玩法就会
把通用列表塞满。

## 另请参阅

- [市场创建者](./market-creator) —— 启动流动性、预言机、费用。
- [多结果市场](./multi-outcome) —— 什么时候一个 LMSR 市场胜过若干个二元市场。
- [规范](../specification) —— 对象与索引的形式化模型。
