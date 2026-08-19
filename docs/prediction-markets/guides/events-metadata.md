---
title: "Events and metadata — how the client assembles markets into matches"
description: "How Forecaster aggregates markets into \"Events\": the event / event_title / child metadata keys, the parent and the child markets, what the node indexes and how to create a market the client will recognise as an event."
---

# Events and metadata: how markets are assembled into matches

A single real-world match is usually several markets: "who wins", "total kills", "first Roshan".
On chain these are independent `pm_market_object`s, but the client (Forecaster) shows them as one event
card with bets on outcomes and a "N more lines" link. The grouping happens **through metadata only** —
there are no special "parent" objects in consensus, and that is deliberate: the protocol stays
minimal, while the grouping is set by the market creator at creation time.

## The short model

- Every market carries a free-form text field `metadata` (a JSON string in `pm_create_market`).
- The node parses a **whitelist of keys** out of it and builds indexes; everything else is ignored.
- Markets with the same `event` are "one event". A market without `child` is the front (parent) one,
  a market with `child: 1` is a child line (a prop).
- Forecaster: the "Events" tab groups active markets by `event`, shows the winner-line market as the
  front one, hides the children from the general feeds and reveals them on the event page.

## The metadata keys the node indexes

The node extracts only these fields from `metadata` (other keys are not indexed, but remain in the
raw JSON — clients can read them themselves):

| Key | Type | What for |
|------|-----|-------|
| `title` | string | The human-readable question of the market (the card headline). |
| `category` | string | The listing section (`esports`, `sports`, `crypto`…) — the `by_category` index. |
| `subcategory` | string | A refinement of the section (optional). |
| `tags` | array or CSV | Tags for filters; the client reads the **array** `market.metadata.tags`, which the node rebuilds itself. |
| `image` | string (URL) | The card cover (a link, not hosted on chain). |
| `description` | string | Short resolution rules — "how the oracle will decide the outcome". |
| `event` | string (slug) | **The event grouping key.** All markets of one match set the same `event`. |
| `event_title` | string | The human-readable name of the event ("Dota 2: MOUZ vs Vici — TI 2026"). |
| `child` | 1 / true | **A child line (a prop).** Hidden from the category/tag feeds; visible on the event page. |
| `banned_jurisdictions` | array or CSV | The jurisdiction filter for clients. |
| `condition_id` | string | The source's dedup identifier (for mirroring parsers). |

Parsing rules: `metadata` must be a valid JSON object (non-JSON is simply not indexed);
`tags`/`banned_jurisdictions` are accepted both as an array and as a CSV string; `child` is accepted as
`true`, `1` or `"1"`. Tags are matched case-insensitively.

## How to designate the "parent" market

The parent is not designated explicitly — it is **derived from the absence of `child`**:

1. Give all the markets of the match the same `event` (a stable slug: latin letters, hyphens —
   for example `dota2-mouz-vg-2026-07-12`) and the same `event_title`.
2. For the main market of the match ("who wins" / moneyline) — **do not set** `child`. That is the
   parent: it stays visible in all feeds and becomes the face of the event card.
3. For all the other lines (totals, handicaps, special markets) — `child: 1`. They disappear from the
   general feeds (no noise in the categories), but remain fully available on the event page and by direct link.

A minimal `metadata` example for three markets of one match:

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

Important: `event` is **immutable in practice** — clients group by an exact string match, so pick the
key before creating the markets and use it identically across all the lines of the match
(case and hyphens matter).

## What the node does

- Builds the market's meta object (`pm_market_meta`) with the parsed fields and indexes: by category,
  by tags and **by event** (`by_meta_event`); the meta goes into the snapshot.
- `list_markets_by_category(...)` **hides children** by default (`hide_children = true`,
  the 8th argument) — the feeds show only parents; pass `false` to see all the lines.
- `list_markets_by_event(event, from, limit)` returns **all** the markets of the event — the parent and
  the children, with no filter. This is the API of the event page.
- In listing rows `event_title` is returned at the top level, in the full market card — inside
  `metadata`; `tags` are rebuilt into an array by the node.

## What Forecaster does with it

- **The "Events" tab** (the sports-book view): active markets are grouped by `event`; the front market
  is the one whose headline looks like a winner line (`winner` / `moneyline` / `to win`), otherwise the
  first binary one; the card shows the outcomes with the current odds (a tap adds a leg to the coupon) and
  a "N more lines" link.
- **The event page** `#/event/<key>` — all the lines of the match in one list (`list_markets_by_event`).
- **Cards** show `event_title` above the question; the "Category › tags" breadcrumbs lead to the listings.
- **Category/tag feeds** do not show child markets — the props live behind the event card.

The practical takeaway for a creator: correctly set `event`/`event_title`/`child` is the difference
between "ten scattered markets making noise in the feed" and "one tidy match card with all the lines
inside". Get the event key wrong and the lines will not group; forget `child` and the props will
clutter the general feeds.

## See also

- [Market creator](./market-creator) — starting liquidity, the oracle, the fee.
- [Multi-outcome markets](./multi-outcome) — when one LMSR market beats several binary ones.
- [Specification](../specification) — the formal model of objects and indexes.
