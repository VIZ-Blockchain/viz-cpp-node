# Prediction Markets (Onix, HF14) — Library Integration Specification

> **Audience:** implementers of the JS / PHP / Python client libraries.
> **Goal:** everything a library needs to (a) build & sign prediction-market transactions and
> (b) read prediction-market state over JSON-RPC — operation names, plugin names, API method
> names, parameters, and the full field layout of every operation and returned object.
>
> This document is generated from the node source and is authoritative for field **names,
> order, and types**. It intentionally does **not** re-explain the economic mechanics
> (CPMM/LMSR/parimutuel, leverage math, dispute game theory) — for that see
> `docs/prediction-markets/specification.md`. Here we describe *the wire contract only*.

---

## 1. Conventions

### 1.1 Data types

| Spec type | Wire (JSON) representation | Notes |
|---|---|---|
| `account_name_type` | string | 3–25 chars, VIZ account name |
| `asset` | string `"10.000 VIZ"` | VIZ, **3 decimals**, `TOKEN_SYMBOL = VIZ`. Serialized as `"<amount>.<3 decimals> VIZ"` |
| `share_type` | integer (int64) | raw amount, **already scaled by 1000** (i.e. `10.000 VIZ` = `10000`). Used inside read-only objects |
| `pm_object_id_type` | integer (int64) | plain numeric id of a pm object (market/bet/liquidity/…). NOT the `space.type.instance` string form |
| `<obj>_id_type` (in returned objects) | string `"<space>.<type>.<instance>"` OR integer, depending on serializer | ChainBase object id. In practice compare/pass the numeric instance. See §1.4 |
| `time_point_sec` | string ISO-8601 `"2026-07-02T12:00:00"` (UTC, no `Z`) | seconds precision |
| `fc::sha256` | hex string (64 chars) | |
| `fc::uint128_t` | integer or string | may exceed JS `Number.MAX_SAFE_INTEGER` — use big-int/string |
| `uint16_t` percent (bp) | integer | **basis points**, `10000 = 100.00%` unless noted otherwise |
| `extensions_type` | array `[]` | always present, always empty for HF14; include as `[]` when signing |

**Percent conventions differ by field — read the per-field notes.** Three scales appear:
- **bp (basis points):** `10000 = 100%`. Most fee/percent fields.
- **`time_penalty` on a bet:** `1e6 = 100%` (`pm_max_time_penalty = 1000000`).
- **`pm_leverage_*_percent` governance knobs:** plain percent (`10 = 10%`), see §9.

### 1.2 Operation serialization

Operations are members of a single `static_variant` (tagged union). Two encodings:

- **JSON (what libraries build):** a 2-element array `["<op_name>", { ...fields }]`, where
  `<op_name>` is the struct name **with the `_operation` suffix removed** — e.g.
  `pm_place_bet_operation` → `"pm_place_bet"`.
- **Binary (for signing):** the variant **tag is a varint = the op-id** in §2/§3, followed by
  the fields in the exact declared order. Field order in every table below **is** the binary
  order. `extensions` is the last field of user ops (empty vector → single `0x00` byte).

A transaction's `operations` array holds these 2-element arrays. Signing (ref-block, expiration,
chain-id, canonical secp256k1) is identical to every other VIZ operation — unchanged by HF14.

### 1.3 JSON-RPC calling convention

All reads go through the **`prediction_market_api`** plugin. Call shape (same as every VIZ API
plugin):

```json
{ "jsonrpc":"2.0", "id":1, "method":"call",
  "params":["prediction_market_api", "<method>", [ <positional args...> ]] }
```

Positional args are order-sensitive; trailing optional args may be omitted. Pagination is
uniformly `(…key…, from, limit)` with `limit ≤ 1000`.

### 1.4 Object ids

Returned objects carry an `id`. The numeric **instance** part is what you pass back into ops as
`market_id`, `bet_id`, `liquidity_id`, `position_id`, `commit_id`. When reading you may receive
either the string `"space.type.instance"` or a bare integer depending on the serializer build;
always be able to parse the trailing integer. Ops take the bare integer (`pm_object_id_type`).

---

## 2. Plugins & node configuration

| Plugin (config `plugin =` name) | Role |
|---|---|
| `chain` | consensus DB (always on) |
| `json_rpc` | RPC transport (always on) |
| `prediction_market_api` | **all PM read methods** (§5). Requires `chain` + `json_rpc` |

`prediction_market_api` config option (`config.ini`):

| Option | Default | Meaning |
|---|---|---|
| `pmm-ttl-days` | `7` | Days to keep a market's non-consensus metadata + kline history after its dispute window closes, then pruned |

There is **no separate operations plugin** — PM operations are part of the core protocol
(`operation` variant); any node accepting transactions accepts them once HF14 is active.

---

## 3. User (signed) operations

35 total PM operations exist; **23 are user operations** (submitted in transactions), 12 are
virtual (§4). Op-id = index in the global `operation` variant (fixed, append-only).

Legend for **Auth**: which authority must sign — `active` (spending auth) or `regular` (posting-level auth).

| # | op-id | JSON name | Auth | Purpose |
|---|---|---|---|---|
| 1 | 66 | `pm_oracle_register` | active(`owner`) | Register an oracle with a bonded insurance deposit |
| 2 | 67 | `pm_oracle_update` | active(`owner`) | Change insurance / fees / rules / auto-accept policy |
| 3 | 68 | `pm_create_market` | active(`creator`) | Create a market; creator seeds first liquidity |
| 4 | 69 | `pm_oracle_accept_market` | active(`oracle`) | Oracle accepts/rejects a pending market & quotes terms |
| 5 | 70 | `pm_place_bet` | active(`account`) | Place a bet (instant or batch) |
| 6 | 71 | `pm_commit_bet` | active(`account`) | Commit-reveal phase 1: hidden commitment |
| 7 | 72 | `pm_reveal_bet` | active(`account`) | Commit-reveal phase 2: reveal & enqueue |
| 8 | 73 | `pm_cancel_bet` | active(`account`) | Cancel an open/queued bet (needs `allow_cancellation`) |
| 9 | 74 | `pm_add_liquidity` | active(`provider`) | Add liquidity to a market |
| 10 | 75 | `pm_withdraw_liquidity` | active(`provider`) | Withdraw liquidity |
| 11 | 76 | `pm_resolve_market` | active(`oracle`) | Oracle resolves to a winning outcome |
| 12 | 77 | `pm_no_contest` | active(`oracle`) | Oracle voids the market (refund all) |
| 13 | 78 | `pm_dispute_create` | active(`disputer`) | File a dispute (escrows `pm_dispute_fee`) |
| 14 | 79 | `pm_dispute_vote` | **regular**(`voter`) | Committee-mode dispute vote |
| 15 | 80 | `pm_dispute_resolve` | active(`resolver`) | Account-mode dispute verdict by configured resolver |
| 16 | 81 | `pm_transfer_position` | active(`from`) | Transfer bet weight to another account |
| 17 | 82 | `pm_lazy_deposit` | active(`account`) | Deposit into the lazy liquidity pool |
| 18 | 83 | `pm_lazy_withdraw` | active(`account`) | Withdraw from the lazy pool |
| 19 | 91 | `pm_leverage_open` | active(`account`) | Open a leveraged CPMM position |
| 20 | 92 | `pm_leverage_close` | active(`account`) | Voluntarily close a leveraged position |
| 21 | 93 | `pm_leverage_convert` | active(`account`) | Convert a leveraged position to a normal bet |
| 22 | 98 | `pm_dispute_oracle_respond` | active(`oracle`) | Oracle posts a public rebuttal on an open dispute |
| 23 | 99 | `pm_unban` | active(`resolver`) | Lift an oracle/creator ban set by that resolver |

> Op-ids 84–90 and 94–97 are the **virtual** ops interleaved in the variant (§4). The user-op
> op-ids above are therefore not fully contiguous. Always verify the numeric tag against a live
> node if you hand-roll binary serialization; JSON name-based serialization is the safe path.

Below, each op lists its fields **in declared (binary) order**. All ops end with
`extensions: []` (omitted from the tables). Percent fields are bp unless noted.

### 3.1 `pm_oracle_register`
| Field | Type | Description |
|---|---|---|
| `owner` | account | Oracle account; also the required signer |
| `insurance` | asset | VIZ bond locked from `owner`; must be `≥ pm_min_oracle_insurance` |
| `fee_percent` | uint16 (bp) | Oracle % of losers' pool; `≤ pm_max_oracle_fee_percent` |
| `fixed_fee` | asset | Per-market fixed fee (VIZ, `≥ 0`) |
| `rules_url` | string | Oracle rules/profile URL; `≤ 256` chars |
| `auto_accept_creator` | account | Auto-accept only markets from this creator; empty = any |
| `auto_accept_resolver` | account | Empty = auto-accept committee-mode markets only; set = only account-mode markets whose `dispute_resolver` equals this |
| `auto_accept` | bool | Enable the anti-collusion auto-accept policy above |

### 3.2 `pm_oracle_update`
All change fields are **optional** — omit to leave unchanged.
| Field | Type | Description |
|---|---|---|
| `owner` | account | Oracle account (signer) |
| `insurance_delta` | optional asset | Signed: `>0` top-up, `<0` withdraw. Withdraw blocked while active markets exist or if it would drop below `pm_min_oracle_insurance` |
| `fee_percent` | optional uint16 (bp) | New oracle fee % |
| `fixed_fee` | optional asset | New per-market fixed fee |
| `rules_url` | optional string | New rules URL |
| `auto_accept_creator` | optional account | Update auto-accept policy |
| `auto_accept_resolver` | optional account | Update auto-accept policy |
| `auto_accept` | optional bool | Enable/disable auto-accept |

### 3.3 `pm_create_market`
Oracle terms here are the creator's **offer ceiling** (max the maker will pay). The oracle locks
its actual quote (`≤` these) at accept; a self-oracle freezes them as-is at creation.
| Field | Type | Description |
|---|---|---|
| `creator` | account | Market creator (signer); becomes first LP |
| `oracle` | account | Registered oracle, or `creator` for a self-oracle |
| `market_type` | uint8 | `0` binary (CPMM), `1` multi (LMSR) |
| `outcomes` | string[] | Outcome labels. Size **2** for binary; `3..pm_max_outcomes` for multi. Each label `≤ 64` chars |
| `url` | string | Resolution criteria/title; `≤ 256` chars |
| `oracle_fee_percent` | uint16 (bp) | Offered max oracle % of losers' pool |
| `oracle_fixed_fee` | asset | Offered max oracle fixed fee (VIZ, `≥ 0`) |
| `creator_fee_percent` | uint16 (bp) | Creator's cut of losers' pool |
| `liquidity_fee_percent` | uint16 (bp) | LP cut of losers' pool |
| `liquidity` | asset | VIZ seed liquidity; `≥ pm_min_liquidity` |
| `lmsr_b` | share_type | Multi only: client-computed LMSR `b`. Node checks `floor(liquidity / ln_q(N)) == b`. `0` for binary |
| `betting_expiration` | time_point_sec | Betting closes at this time |
| `result_expiration` | time_point_sec | `> betting_expiration`; `≤ now + pm_max_market_duration` |
| `time_penalty_type` | uint8 | Time-decay penalty model selector |
| `time_penalty_value` | uint32 | Penalty parameter (see market mechanics) |
| `penalty_curve_type` | uint8 | Penalty curve selector |
| `allow_early_resolution` | bool | Oracle may resolve before `betting_expiration` |
| `allow_cancellation` | bool | Bettors may cancel open bets |
| `allow_batch` | bool | Allow batch (commit-reveal / queued) betting |
| `allow_instant_bet` | bool | Allow instant bets. **Multi forces `true`** (no LMSR batch yet) |
| `endogeneity_tier` | uint8 | `1` econ-data / `2` sports / `3` political |
| `dispute_mode` | uint8 | `0` committee / `1` account |
| `dispute_resolver` | account | Required & must exist iff `dispute_mode==1`; must NOT equal `oracle` or `creator` |
| `dispute_penalty_percent` | int16 (bp) | `−10000..+10000` oracle penalty policy on a successful dispute: `>0` slash % of insurance ×consensus; `<0` good-faith bonus from fee; `0` none |
| `metadata` | string | Free-form client JSON (no length cap). Consensus-opaque; parsed off-chain by the meta plugin (see §6) |

### 3.4 `pm_oracle_accept_market`
Quote fields are ignored on reject and for self-oracle markets.
| Field | Type | Description |
|---|---|---|
| `oracle` | account | Oracle (signer) |
| `market_id` | int64 | Target market |
| `accept` | bool | `true` accept, `false` reject |
| `oracle_fee_percent` | uint16 (bp) | Oracle's quoted %; `≤` market offer & the median cap |
| `oracle_fixed_fee` | asset | Oracle's quoted fixed fee; `≤` market offer |

Emits virtual `pm_market_accepted` on accept.

### 3.5 `pm_place_bet`
| Field | Type | Description |
|---|---|---|
| `account` | account | Bettor (signer) |
| `market_id` | int64 | Target market |
| `side` | int8 | Binary: `0`/`1`. Multi: `-1` |
| `outcome_index` | int16 | Multi: `0..N-1`. Binary: `-1` |
| `amount` | asset | Stake (VIZ, `> 0`) |
| `min_tokens` | share_type | Slippage floor on received weight (`0` = none) |
| `mode` | uint8 | `0` instant, `1` batch (queued to next epoch) |

### 3.6 `pm_commit_bet`
| Field | Type | Description |
|---|---|---|
| `account` | account | Bettor (signer) |
| `market_id` | int64 | Target market |
| `commitment` | sha256 | SHA-256 of the exact binary preimage in §3.6.1 (consensus-critical) |
| `escrow_amount` | asset | VIZ locked; `≥ pm_min_batch_bet` |
| `no_reveal_fee_percent` | uint16 (bp) | **MUST equal** median(`pm_commit_no_reveal_penalty_percent`) — consensus-checked |

#### 3.6.1 Commitment preimage (byte-exact — must match consensus)

The node recomputes this hash in the `pm_reveal_bet` evaluator (`verify_commit` in
`libraries/chain/pm_evaluator.cpp`) and rejects the reveal on any mismatch — a wrong preimage
**forfeits the escrow**. The preimage is a **raw binary concatenation with no separators and no
length prefixes** (it is *not* the colon-delimited string some prototypes used). Fields, in order:

| # | Field | Bytes | Encoding |
|---|---|---|---|
| 1 | `market_id` | 8 | int64, **little-endian** (the numeric market instance) |
| 2 | `account` | 32 | ASCII account name left-aligned in a **32-byte** buffer, **zero-padded** (VIZ `fixed_string_32` storage) |
| 3 | `side` | 1 | int8 (binary: `0`/`1`; multi: `-1` = `0xFF`) |
| 4 | `outcome_index` | 2 | int16, **little-endian** (multi: `0..N-1`; binary: `-1` = `0xFFFF`) |
| 5 | `amount` | 8 | int64, little-endian — the **revealed** amount in **milli-VIZ** (`asset.amount`, i.e. VIZ×1000) |
| 6 | `min_tokens` | 8 | int64, little-endian (milli-VIZ / weight units) |
| 7 | `salt` | variable | raw UTF-8/byte content of the salt string (its own length, no terminator) |

Fixed portion = **59 bytes**; total = `59 + len(salt)`. `commitment = SHA-256(preimage)` (32-byte
digest). The same `side`, `outcome_index`, `amount`, `min_tokens`, `salt` must be re-supplied in
`pm_reveal_bet` (§3.7). Integers use **little-endian** because the node hashes raw host bytes on its
(x86-64, little-endian) consensus platform — libraries MUST emit little-endian regardless of host.

**Golden test vector** (verify your encoder against this before shipping):

```
market_id      = 5
account        = "alice"
side           = 0
outcome_index  = -1
amount         = 10000        # 10.000 VIZ
min_tokens     = 0
salt           = "cafe1234"

preimage (hex, 67 bytes):
0500000000000000 616c696365000000000000000000000000000000000000000000000000000000 00 ffff 1027000000000000 0000000000000000 6361666531323334

commitment = SHA-256(preimage)
           = acc2fbc9e024509a529584baba41dc2eabdb82c3c107dc041d37c94b24f4b3c0
```

(Preimage shown space-grouped by field for readability; concatenate without spaces before hashing.)

### 3.7 `pm_reveal_bet`
| Field | Type | Description |
|---|---|---|
| `account` | account | Bettor (signer) |
| `commit_id` | int64 | The `pm_commit` object being revealed |
| `side` | int8 | Same value that was committed |
| `outcome_index` | int16 | Same value that was committed |
| `amount` | asset | `≤ escrow_amount`; surplus refunded |
| `salt` | string | Entropy bound into the commitment hash |
| `min_tokens` | share_type | Slippage floor |

### 3.8 `pm_cancel_bet`
| Field | Type | Description |
|---|---|---|
| `account` | account | Bettor (signer) |
| `bet_id` | int64 | Bet to cancel |
| `min_return` | share_type | Slippage floor on the refund |

### 3.9 `pm_add_liquidity`
| Field | Type | Description |
|---|---|---|
| `provider` | account | LP (signer) |
| `market_id` | int64 | Target market |
| `amount` | asset | VIZ to add (`> 0`) |

### 3.10 `pm_withdraw_liquidity`
| Field | Type | Description |
|---|---|---|
| `provider` | account | LP (signer) |
| `liquidity_id` | int64 | LP position to withdraw from |
| `amount` | asset | VIZ to withdraw; `0` = full position. Principal-safe; locked from `betting_expiration` until resolution |

### 3.11 `pm_resolve_market`
| Field | Type | Description |
|---|---|---|
| `oracle` | account | Oracle (signer) |
| `market_id` | int64 | Target market |
| `winning_outcome` | int16 | Winning outcome index |
| `decision_url` | string | Evidence/decision URL; `≤ 256` chars. Stored on the market (`pm_market_object.decision_url`) |
| `decision_reason` | string | Free-text justification; `≤ 1024` chars. Stored on the market (`pm_market_object.decision_reason`), readable via `get_market` |

### 3.12 `pm_no_contest`
| Field | Type | Description |
|---|---|---|
| `oracle` | account | Oracle (signer) |
| `market_id` | int64 | Target market |
| `reason` | string | `≤ 1024` chars. Stored on the market as `decision_reason` |

### 3.13 `pm_dispute_create`
| Field | Type | Description |
|---|---|---|
| `disputer` | account | Disputer (signer); escrows `pm_dispute_fee` |
| `market_id` | int64 | Target market |
| `proposed_outcome` | int16 | Outcome the disputer claims is correct (`-1` = void/no-contest challenge) |
| `reason` | string | `≤ 1024` chars |

### 3.14 `pm_dispute_vote` (committee mode)
Signed with **regular** authority (like `committee_vote_request`). Ballots are revisable until
voting closes (modify-or-create).
| Field | Type | Description |
|---|---|---|
| `voter` | account | Voter (regular-auth signer) |
| `market_id` | int64 | Disputed market |
| `vote_outcome` | int16 | Correct outcome, or `-1` to uphold the oracle |
| `vote_percent` | int16 | Conviction / penalty intensity `[-10000, 10000]` (bp). Sign encodes direction; see §5 `get_dispute_votes` for tally semantics |

### 3.15 `pm_dispute_resolve` (account mode)
Verdict by the market's configured `dispute_resolver`.
| Field | Type | Description |
|---|---|---|
| `resolver` | account | Configured resolver (signer) |
| `market_id` | int64 | Disputed market |
| `correct_outcome` | int16 | Final correct outcome |
| `penalty_amount` | asset | Oracle insurance to slash |
| `ban_oracle` | bool | Ban the oracle |
| `ban_oracle_until` | time_point_sec | Oracle ban expiry (`time_point_sec::maximum()` = permanent) |
| `ban_creator` | bool | Ban the creator from creating markets |
| `ban_creator_until` | time_point_sec | Creator ban expiry (max = permanent) |

> **Bans are a compliance/regulator feature, exclusive to account mode.** When a market routes
> disputes to an account-mode `dispute_resolver` (e.g. a regulator or a licensed arbitrator), that
> resolver can sanction **both the oracle and the market creator** — temporarily or permanently
> (`*_until = maximum()`) — in the same verdict, on top of the insurance slash. This lets a regulator
> that serves as resolver enforce off-chain rules (bar a bad-faith oracle or a repeat-offender
> creator from the platform). **Committee/DAO mode (`dispute_mode = 0`) has no ban power by design**
> — it is a transparent public hearing that only slashes insurance and dings reputation
> (`pm_dispute_finalize`); it never bans. A ban set here records the issuing `resolver` in the
> target's `banned_by`, so only that resolver may lift it early via `pm_unban` (§3.23); otherwise it
> lapses at `banned_until` (cron emits `pm_ban_expired`, §4.12).

### 3.16 `pm_transfer_position`
| Field | Type | Description |
|---|---|---|
| `from` | account | Current holder (signer) |
| `bet_id` | int64 | Bet whose weight is transferred |
| `to` | account | Recipient |
| `amount` | share_type | Weight to reassign; `0` = full. No market impact |
| `memo` | string | Plaintext, or `#`-prefixed ECIES (same as VIZ memos) |

### 3.17 `pm_lazy_deposit`
| Field | Type | Description |
|---|---|---|
| `account` | account | Depositor (signer) |
| `amount` | asset | VIZ deposited into the lazy pool (`> 0`) |

### 3.18 `pm_lazy_withdraw`
| Field | Type | Description |
|---|---|---|
| `account` | account | Depositor (signer) |
| `shares` | share_type | Pool shares to burn; `0` = all |
| `emergency` | bool | `true` = withdraw before lock ends, with penalty on locked-share profit |

### 3.19 `pm_leverage_open`
Gated by `pm_leverage_enabled`.
| Field | Type | Description |
|---|---|---|
| `account` | account | Bettor (signer) |
| `market_id` | int64 | Target market (binary CPMM only) |
| `outcome_index` | int16 | Binary side `0`/`1` |
| `collateral` | asset | Bettor's own stake (VIZ) |
| `loan` | asset | Pool loan (VIZ) |
| `min_tokens` | share_type | Slippage floor (consensus-checked) |
| `max_slippage_percent` | uint16 (bp) | User-facing front-run guard |

### 3.20 `pm_leverage_close`
| Field | Type | Description |
|---|---|---|
| `account` | account | Position owner (signer) |
| `position_id` | int64 | Leverage position to close (only if `cancel_value ≥ liquidation_threshold`) |
| `min_return` | share_type | Slippage floor on the bettor's return |

### 3.21 `pm_leverage_convert`
| Field | Type | Description |
|---|---|---|
| `account` | account | Position owner (signer) |
| `position_id` | int64 | Leverage position to convert to a normal bet |
| `conversion_profit_cost` | uint16 (bp) | **MUST equal** median(`pm_conversion_profit_cost_percent`) |

### 3.22 `pm_dispute_oracle_respond`
The market's oracle posts a public rebuttal onto the open dispute. Stored on the dispute object
(read via `get_dispute`); allowed only while the dispute is open and `now ≤ oracle_response_deadline`.
Re-posting overwrites the previous response.
| Field | Type | Description |
|---|---|---|
| `oracle` | account | Market oracle (signer) |
| `market_id` | int64 | Disputed market |
| `response` | string | Rebuttal text; non-empty, `≤ 1024` chars |

### 3.23 `pm_unban`
Lifts a ban imposed by an account-mode `pm_dispute_resolve`. Only the resolver recorded in the
target's `banned_by` may lift it. At least one of `unban_oracle` / `unban_creator` must be true.
| Field | Type | Description |
|---|---|---|
| `resolver` | account | The resolver that imposed the ban (signer); must equal the target's `banned_by` |
| `target` | account | The banned oracle / creator account |
| `unban_oracle` | bool | Clear the oracle ban (`pm_oracle_object.banned_until`) |
| `unban_creator` | bool | Clear the creator ban (`pm_creator_ban_object.banned_until`) |

---

## 4. Virtual operations

Virtual ops are **never submitted** by clients — they are emitted deterministically by the
node's bounded per-block cron and appear in block/account history (via the account-history /
operation-history plugins, `get_ops_in_block`, etc.). Libraries need to **parse** them.
They have no `extensions` field.

| op-id | JSON name | Emitted when |
|---|---|---|
| 84 | `pm_batch_settle` | Epoch boundary: queued/revealed bets settle at a uniform price |
| 85 | `pm_commit_forfeit` | An unrevealed commitment forfeits its penalty into `forfeit_pool` |
| 86 | `pm_auto_payout` | Deferred market-level payout marker after the dispute grace elapses |
| 87 | `pm_dispute_finalize` | Committee-mode tally finalized at `voting_end_time` |
| 88 | `pm_dispute_auto_close` | Anti-freeze: dispute unresolved at `auto_close_time` → full refund |
| 89 | `pm_oracle_missed_penalty` | Oracle missed `result_expiration` → insurance slashed, refund all |
| 90 | `pm_lazy_recall` | Lazy-pool graduated recall step (idle allocation returned) |
| 94 | `pm_leverage_liquidate` | A leveraged position force-closed (opposing bet / cancel / expiration) |
| 95 | `pm_leverage_resolve` | A leveraged position settled at market resolution |
| 96 | `pm_market_accepted` | Oracle accepted (or self-oracle auto-accepted); terms frozen |
| 97 | `pm_payout` | Per-bettor parimutuel settlement (one per active bet inside settle) |
| 100 | `pm_ban_expired` | A temporary oracle/creator ban lapsed at `banned_until`; the cron cleared it |

### 4.1 `pm_batch_settle`
`market_id` (int64), `epoch` (uint32), `settled_bets` (uint32).

### 4.2 `pm_commit_forfeit`
`account`, `commit_id` (int64), `market_id` (int64), `penalty` (asset → `forfeit_pool`), `refund` (asset → account).

### 4.3 `pm_auto_payout`
`account`, `market_id` (int64), `bet_id` (int64), `payout` (asset).

### 4.4 `pm_dispute_finalize`
`market_id` (int64), `winning_outcome` (int16), `oracle_penalty` (asset).

### 4.5 `pm_dispute_auto_close`
`market_id` (int64), `oracle_penalty` (asset).

### 4.6 `pm_oracle_missed_penalty`
`oracle`, `market_id` (int64), `slashed` (asset).

### 4.7 `pm_lazy_recall`
`market_id` (int64), `recalled` (asset).

### 4.8 `pm_leverage_liquidate`
`account`, `position_id` (int64), `market_id` (int64), `cancel_value` (asset), `pool_received` (asset),
`bettor_received` (asset), `reason` (uint8: `0` opposing_bet, `1` cancel_bet, `2` expiration).

### 4.9 `pm_leverage_resolve`
`account`, `position_id` (int64), `market_id` (int64), `won` (bool), `pool_received` (asset),
`bettor_received` (asset), `outcome_index` (int16), `leverage` (uint16 — integer multiple `total_bet/collateral`).
`won` = position was solvent; `bettor_received == 0` ⇒ collateral lost.

### 4.10 `pm_market_accepted`
`oracle`, `creator`, `market_id` (int64), `oracle_fee_percent` (uint16 bp, frozen),
`oracle_fixed_fee` (asset, frozen), `self_oracle` (bool — `true` = auto-accepted at creation).

### 4.11 `pm_payout`
`account`, `market_id` (int64), `bet_id` (int64), `side` (int8: binary `0`/`1`, multi `-1`),
`outcome_index` (int16: multi `0..N-1`, binary `-1`), `amount` (asset — the stake),
`payout` (asset — credited at settle; `0` on a loss).

### 4.12 `pm_ban_expired`
`account`, `oracle` (bool), `creator` (bool). Emitted by the per-block cron when a **temporary**
oracle and/or creator ban reaches `banned_until` — the cron clears the ban and fires this so
history/indexers observe the lift. An *early manual* lift is the signed `pm_unban` op instead (§3.23),
so this vop fires only for automatic time-expiry. Permanent bans (`banned_until = maximum()`) never
expire and never emit it.

---

## 5. API methods — `prediction_market_api` plugin

Call as `call("prediction_market_api", "<method>", [args])`. `from`/`limit` are pagination
offsets; `limit ≤ 1000`. Returned object schemas are in §7.

### Markets

**`get_market(market_id)`**
- `market_id` int64.
- Returns `pm_market_object` (§7.2). Throws if not found.

**`list_markets(status, from, limit, [show_risky=false])`**
- `status` int8 — filter by market status (`-1` deleted, `0` waiting, `1` active, `2` closed, `3` resolved).
- `from` uint32, `limit` uint32.
- `show_risky` bool (optional) — when `false` (default), markets whose oracle insurance covers
  `< 2.5×` their betting volume are **hidden**. `true` reveals them.
- Returns `pm_market_object[]`.

**`list_markets_by_oracle(oracle, from, limit)`** → `pm_market_object[]` for `oracle` (account name).

**`list_markets_by_creator(creator, from, limit)`** → `pm_market_object[]` for `creator` (account name).

**`get_market_outcomes(market_id)`** → `pm_outcome_object[]` (§7.3), ordered by `outcome_index`.

**`get_market_weight_sums(market_id)`** → `pm_market_weight_sums_api_object` (§7.16). Per-outcome
aggregated staked amount and curve weight (computed live from active/resolved bets). For binary
markets the outcome labels are synthesized as `"A"`/`"B"`.

**`get_market_bets(market_id, from, limit)`** → `pm_bet_object[]` (§7.4), all bets on the market.

**`get_account_positions(account, from, limit)`** → `pm_position_api_object[]` (§7.15) — each
bet plus its `expected_payout` (parimutuel projection mirroring settlement) and the market status.

**`get_market_liquidity(market_id, from, limit)`** → `pm_liquidity_object[]` (§7.5).

**`get_market_full(market_id, [account])`** → `pm_market_full_api_object` (§7.24). One-call enriched
view: market + outcomes + weight sums + oracle (with reliability) + parsed metadata, plus — when
`account` is given — that account's bets / leverage positions / LP **on this market**. Saves the thin
client several round-trips when opening a market detail screen. Throws only if the market is missing.

### Leverage

**`get_account_leverage_positions(account, from, limit)`** → `pm_leverage_position_object[]` (§7.12).

**`get_market_leverage_positions(market_id, from, limit)`** → `pm_leverage_position_object[]` (§7.12).

**`get_creator_ban(account)`** → `pm_creator_ban_object` (§7.13). Throws if the account is not banned.

**`get_leverage_quote(market_id, outcome_index, collateral)`** → `pm_leverage_quote_api_object` (§7.20).
Read-only projection of `pm_leverage_open` using the **same in-node margin math**: the max solvent
loan, the resulting max leverage, the pool/position caps, and up to 12 slider stops (each with
tokens, threshold, current & worst-case cancel value). `collateral` is a `share_type` integer
(milli-VIZ). When leverage is not possible, `available=false` and `failed_constraints[]` explains
why. Throws only if the market does not exist.

**`get_leverage_close_preview(position_id)`** → `pm_leverage_close_preview_api_object` (§7.21).
Mirrors `pm_leverage_close` at head-block reserves: cancel value, pool obligation, what the bettor
receives, and `closeable` (false ⇒ the protocol would liquidate instead).

**`get_leverage_convert_preview(position_id)`** → `pm_leverage_convert_preview_api_object` (§7.22).
Mirrors `pm_leverage_convert`: cancel value, current profit, the conversion fee at the current
median `pm_conversion_profit_cost_percent`, and the `total_user_payment` the convert op would debit.

> These three are **non-consensus quotes** — reserves move between the read and the broadcast, so
> treat the numbers as an estimate at the head block and always send the on-chain slippage guards
> (`min_tokens` / `min_return` / `conversion_profit_cost`).

### Oracles

**`get_oracle(owner)`** → `pm_oracle_api_object` (§7.14) — the raw `pm_oracle_object` plus a
non-consensus `reliability_score` (bp `[0..10000]`). `owner` = account name. Throws if not found.

**`list_oracles(from, limit)`** → `pm_oracle_object[]` (§7.1), ordered by owner name.

### Disputes

**`get_dispute(market_id)`** → `pm_dispute_object` (§7.7). Throws if no dispute exists.

**`get_dispute_votes(market_id)`** → `pm_dispute_votes_api_object` (§7.17). Returns the live
ballot list **plus** a stake-weighted projection of what the finalize cron would apply right now
(quorum status, expected outcome, consensus strength). Safe to call when no dispute exists
(returns defaults). Key semantics of the vote tally:
- A vote with `vote_outcome == -1` (or `vote_percent ≤ 0`) counts as **defending the oracle**.
- A vote with `vote_percent > 0` and a valid `vote_outcome` backs an **outcome change**, weighted
  by `voter_shares × vote_percent / 10000`.
- Voter weight = `effective_vesting_shares + lazy-pool stake→shares`.

### Lazy pool & chain properties

**`get_lazy_pool()`** (no args) → `pm_lazy_pool_object` (§7.9). Throws if the pool is uninitialized.

**`get_lazy_deposit(account)`** → `pm_lazy_deposit_object` (§7.10). Throws if the account has no deposit.

**`get_lazy_allocations(from, limit)`** → `pm_lazy_allocation_object[]` (§7.11). All lazy-pool
per-market allocation records (for a pool dashboard).

**`get_market_lazy_allocation(market_id)`** → `pm_lazy_allocation_object` (§7.11). The lazy-pool
allocation for one market. Throws if none.

> Oracle penalty stamps are already on `pm_oracle_object` (`penalty_stamps`, `last_penalty_stamp_time`,
> returned by `get_oracle`) — no separate method needed.

**`get_pm_chain_properties()`** (no args) → `chain_properties_pm` (§9) — the **median** (active,
consensus) values of every PM governance parameter.

### Metadata & charts (non-consensus, plugin-indexed)

**`get_market_meta(market_id)`** → `pm_market_meta_object` (§7.18) — parsed category/tags/etc.
Throws if none indexed yet.

**`list_markets_by_category(category, from, limit, [jurisdiction=""], [subcategory=""], [tag=""], [sort="newest"])`**
→ `pm_market_meta_object[]`. Optional filters: `jurisdiction` (ISO code — exclude markets banning
it), `subcategory` (exact match), `tag` (CSV membership). `sort` ∈ `newest` (market id desc,
default) · `oldest` (id asc) · `volume` (`bets_sum` desc) · `expiration` (`betting_expiration` asc).
`volume`/`expiration` load each matching market, so they scan the whole (non-pruned) category
before paging.

**`get_market_categories()`** (no args) → `pm_market_categories_api_object` (§7.23). Category taxonomy
with live per-category / per-subcategory counts, plus the top 20 hot tags (jurisdiction-* tags
excluded), aggregated over the currently indexed (non-pruned) markets. Use it to build the browse
filter chips without hard-coding a taxonomy.

**`get_market_kline(market_id, [from=0], [limit=1000])`** → `pm_kline_api_object[]` (§7.19).
Time-series of per-outcome weight snapshots, ascending by `seq` (oldest→newest). Pagination is
**offset-from-newest**: `from` skips the newest N points, then up to `limit` are returned. So
`(from=0, limit=1000)` is the latest ≤1000 changes; `(from=1000, limit=1000)` steps another 1000
back. Empty array when the market has no recorded points.

---

## 6. `metadata` JSON (client convention)

The `metadata` field of `pm_create_market` is consensus-opaque free-form JSON. The
`prediction_market_api` plugin parses these keys (unknown keys ignored) and exposes them via
`get_market_meta` / `list_markets_by_category`:

| Key | Type | Indexed as |
|---|---|---|
| `category` | string | `category` (queryable) |
| `subcategory` | string | `subcategory` |
| `tags` | string[] or CSV | `tags` (comma-joined) |
| `banned_jurisdictions` | string[] or CSV of ISO codes | `banned_jurisdictions` (filtered in `list_markets_by_category`) |

Any other keys (title translations, images, source links, etc.) are preserved on-chain in
`metadata` verbatim but not indexed. Localization is a pure client concern.

---

## 7. Returned object schemas

Field order below matches the node's reflection (JSON key order is not guaranteed, but these are
the exact keys). All monetary fields are `share_type` **integers** (raw, ×1000) unless the type
says `asset`.

### 7.1 `pm_oracle_object`
| Field | Type | Description |
|---|---|---|
| `id` | object-id | Oracle id |
| `owner` | account | Oracle account |
| `insurance` | share_type | Locked insurance bond |
| `fee_percent` | uint16 (bp) | Oracle % of losers' pool |
| `fixed_fee` | share_type | Per-market fixed fee |
| `rules_url` | string | Rules/profile URL |
| `active_since` | time | First activation time |
| `last_active_time` | time | Last activity time |
| `banned_until` | time | `0` not banned; `time_point_sec::maximum()` = permanent |
| `markets_accepted` | uint32 | Reputation counter |
| `markets_resolved` | uint32 | Reputation counter |
| `no_contest_count` | uint32 | Markets voided |
| `missed_count` | uint32 | Missed resolution deadlines |
| `disputes_received` | uint32 | Disputes filed against this oracle |
| `disputes_lost` | uint32 | Disputes where oracle was overturned |
| `disputes_won` | uint32 | Disputes where oracle was upheld |
| `disputes_auto_closed` | uint32 | Disputes that hit auto-close |
| `dispute_responses_missed` | uint32 | Missed dispute-response deadlines |
| `total_volume_resolved` | share_type | Cumulative resolved volume |
| `total_insurance_slashed` | share_type | Cumulative insurance slashed |
| `avg_resolution_time` | uint32 | Avg seconds to resolve |
| `penalty_stamps` | uint32 | Active penalty stamps (10-day decay) |
| `bans_received` | uint32 | Ban count |
| `last_penalty_stamp_time` | time | Time of most recent penalty stamp |
| `auto_accept_creator` | account | Auto-accept policy (empty = any) |
| `auto_accept_resolver` | account | Auto-accept policy |
| `auto_accept` | bool | Auto-accept enabled |
| `banned_by` | account | Resolver that set `banned_until` (empty if unset); the account allowed to `pm_unban` |

### 7.2 `pm_market_object`
| Field | Type | Description |
|---|---|---|
| `id` | object-id | Market id |
| `creator` | account | Creator |
| `oracle` | account | Oracle |
| `market_type` | uint8 | `0` binary (CPMM), `1` multi (LMSR) |
| `outcome_count` | uint8 | Number of outcomes (binary = 2) |
| `url` | string | Resolution criteria/title |
| `status` | int8 | `-1` deleted, `0` waiting, `1` active, `2` closed, `3` resolved |
| `payout_status` | uint8 | `0` none, `1` pending, `2` paid, `3` disputed |
| `created_time` | time | Creation time |
| `betting_expiration` | time | Betting closes |
| `result_expiration` | time | Oracle deadline |
| `resolved_outcome` | int16 | Winning outcome; `-1` if unresolved |
| `reserve_a` | share_type | Binary CPMM reserve A |
| `reserve_b` | share_type | Binary CPMM reserve B |
| `k` | uint128 | CPMM invariant `reserve_a × reserve_b` |
| `a_bets_sum` | share_type | Binary: total staked on side A |
| `b_bets_sum` | share_type | Binary: total staked on side B |
| `lmsr_b` | share_type | Multi: LMSR liquidity parameter |
| `lmsr_subsidy` | share_type | Multi: LMSR subsidy |
| `bets_sum` | share_type | Total staked across all outcomes |
| `liquidity_sum` | share_type | Total LP liquidity |
| `oracle_fee_percent` | uint16 (bp) | Frozen oracle fee % |
| `creator_fee_percent` | uint16 (bp) | Creator fee % |
| `liquidity_fee_percent` | uint16 (bp) | LP fee % |
| `oracle_fixed_fee` | share_type | Frozen oracle fixed fee |
| `liquidity_fee_earned` | share_type | Accrued LP fees |
| `forfeit_pool` | share_type | Forfeited commit penalties added to winners' pool |
| `time_penalty_type` | uint8 | Time-decay penalty model |
| `time_penalty_value` | uint32 | Penalty parameter |
| `penalty_curve_type` | uint8 | Penalty curve |
| `allow_early_resolution` | bool | |
| `allow_cancellation` | bool | |
| `allow_batch` | bool | |
| `allow_instant_bet` | bool | |
| `endogeneity_tier` | uint8 | `1`/`2`/`3` |
| `current_epoch` | uint32 | Current batch epoch |
| `dispute_mode` | uint8 | `0` committee / `1` account |
| `dispute_resolver` | account | Configured resolver (account mode) |
| `dispute_penalty_percent` | int16 (bp) | Oracle penalty policy on successful dispute |
| `metadata` | string | Raw client JSON |
| `decision_url` | string | Oracle's cited evidence link, set at resolution (empty until resolved) |
| `decision_reason` | string | Oracle's free-text justification, set at `pm_resolve_market` (or the NO-CONTEST reason at `pm_no_contest`). Stored on-chain, readable via `get_market` |

### 7.3 `pm_outcome_object`
| Field | Type | Description |
|---|---|---|
| `id` | object-id | |
| `market` | object-id | Owning market |
| `outcome_index` | uint8 | Index within the market |
| `label` | string | Outcome label |
| `q` | share_type | LMSR quantity |
| `bets_sum` | share_type | Total staked on this outcome |
| `weight_sum` | share_type | Total curve weight on this outcome |
| `bets_count` | uint32 | Number of bets |

### 7.4 `pm_bet_object`
| Field | Type | Description |
|---|---|---|
| `id` | object-id | Bet id |
| `market` | object-id | Owning market |
| `account` | account | Bettor |
| `side` | int8 | Binary: `0`/`1`; multi: `-1` |
| `outcome_index` | int16 | Multi: `0..N-1`; binary: `-1` |
| `amount` | share_type | Stake |
| `weight` | share_type | Curve weight received |
| `price` | uint64 | Execution price (fixed-point) |
| `time_penalty` | uint32 | Time penalty (`1e6 = 100%`) |
| `mode` | uint8 | `0` instant, `1` batch |
| `epoch` | uint32 | Batch epoch (for queued bets) |
| `status` | uint8 | `0` active, `1` cancelled, `2` refunded, `3` resolved, `5` queued, `6` revealed-pending |
| `min_tokens` | share_type | Slippage floor for queued batch bets |
| `resolved_amount` | share_type | Realized payout after settlement |
| `created_time` | time | |

### 7.5 `pm_liquidity_object`
| Field | Type | Description |
|---|---|---|
| `id` | object-id | LP position id |
| `market` | object-id | Owning market |
| `provider` | account | LP account; **empty = Lazy Pool** |
| `amount` | share_type | Provided liquidity |
| `weight_a` | share_type | Binary reserve share A |
| `weight_b` | share_type | Binary reserve share B |
| `b_share` | share_type | LMSR share |
| `sec_to_expiration` | uint32 | Position term |
| `deposit_time` | time | |
| `earned_fee` | share_type | Accrued fees |
| `status` | uint8 | `0` active, `3` resolved/closed |

### 7.6 `pm_commit_object`
| Field | Type | Description |
|---|---|---|
| `id` | object-id | |
| `market` | object-id | Owning market |
| `account` | account | Committer |
| `commitment` | sha256 | Commitment hash |
| `escrow_amount` | share_type | Locked escrow |
| `no_reveal_fee_percent` | uint16 (bp) | Snapshotted penalty % at commit |
| `commit_time` | time | |
| `reveal_deadline` | time | |
| `status` | uint8 | `0` committed, `1` revealed, `2` forfeited |

> Note: there is no direct "get_commit" API method in HF14; commits surface via history/virtual ops.

### 7.7 `pm_dispute_object`
| Field | Type | Description |
|---|---|---|
| `id` | object-id | |
| `market` | object-id | Disputed market |
| `disputer` | account | Who filed |
| `dispute_fee` | share_type | Escrowed fee |
| `reason` | string | Dispute reason |
| `filed_time` | time | |
| `oracle_response_deadline` | time | Oracle must respond by |
| `dispute_mode` | uint8 | `0` committee / `1` account |
| `voting_end_time` | time | Committee voting closes |
| `auto_close_time` | time | Anti-freeze fallback |
| `proposed_outcome` | int16 | Outcome the disputer proposes |
| `status` | uint8 | `0` open, `1` oracle-wrong, `2` oracle-right, `3` auto-closed |
| `oracle_response` | string | Oracle's public rebuttal (empty until it responds via `pm_dispute_oracle_respond`) |
| `oracle_response_time` | time | When the rebuttal was posted (`0` = none) |

### 7.8 `pm_dispute_vote_object`
| Field | Type | Description |
|---|---|---|
| `id` | object-id | |
| `market` | object-id | Disputed market |
| `voter` | account | Voter |
| `vote_outcome` | int16 | Correct outcome, or `-1` = uphold oracle |
| `vote_percent` | int16 | Conviction/penalty `[-10000, 10000]` |
| `time` | time | Vote time |

### 7.9 `pm_lazy_pool_object` (singleton, id 0)
| Field | Type | Description |
|---|---|---|
| `id` | object-id | Always instance 0 |
| `total_shares` | share_type | Total pool shares outstanding |
| `free_balance` | share_type | Unallocated VIZ |
| `allocated_balance` | share_type | VIZ allocated into markets |
| `earned_balance` | share_type | Cumulative earnings (monotonic) |
| `reward_per_share` | uint128 | Reward accumulator (`LAZY_POOL_PRECISION = 1e9`) |
| `leverage_fund_used` | share_type | Total active leverage loans |

### 7.10 `pm_lazy_deposit_object`
| Field | Type | Description |
|---|---|---|
| `id` | object-id | |
| `account` | account | Depositor |
| `shares` | share_type | Pool shares held |
| `principal` | share_type | Original deposited principal |
| `reward_snapshot` | uint128 | Reward accumulator snapshot |
| `pending_rewards` | share_type | Unclaimed rewards |
| `unlock_time` | time | Lock expiry (`pm_lazy_lock_sec` after deposit) |

### 7.11 `pm_lazy_allocation_object`
Per-market lazy-pool allocation record. Readable via `get_lazy_allocations` (list) and
`get_market_lazy_allocation(market_id)` (§5).
`id`, `market`, `amount`, `original_amount`, `recalled_amount`, `returned_amount`,
`bets_sum_at_check`, `check_step` (uint32 `0..10`), `last_check_time` (time), `status` (uint8: `0` active, `1` returned).

### 7.12 `pm_leverage_position_object`
| Field | Type | Description |
|---|---|---|
| `id` | object-id | Position id |
| `market` | object-id | Owning market |
| `account` | account | Position owner |
| `outcome_index` | int16 | Binary side `0`/`1` |
| `collateral` | share_type | Bettor's own stake |
| `loan` | share_type | Pool loan |
| `total_bet` | share_type | `collateral + loan` deployed |
| `tokens` | share_type | Weight received from the AMM |
| `bet` | object-id | Underlying `pm_bet` id |
| `pool_profit` | share_type | `loan × R%` |
| `liquidation_threshold` | share_type | `loan × (1 + R%)` |
| `status` | uint8 | `0` active, `1` liquidated, `2` resolved_won, `3` resolved_lost, `4` closed_voluntary, `5` converted |
| `liquidated_at` | time | |
| `liquidated_by_bet` | object-id | The opposing bet that triggered liquidation |
| `cancel_value_at_liquidation` | share_type | |
| `pool_received` | share_type | VIZ returned to pool at close |
| `bettor_received` | share_type | VIZ returned to bettor at close |
| `created_time` | time | |
| `last_update` | time | |

### 7.13 `pm_creator_ban_object`
| Field | Type | Description |
|---|---|---|
| `id` | object-id | |
| `creator` | account | Banned creator |
| `banned_until` | time | Ban expiry (`maximum()` = permanent; a past time = not banned) |
| `ban_count` | uint32 | Number of bans received |
| `banned_by` | account | Resolver that set the current ban (the account allowed to `pm_unban`) |

### 7.14 `pm_oracle_api_object` (from `get_oracle`)
| Field | Type | Description |
|---|---|---|
| `oracle` | `pm_oracle_object` | The raw oracle object (§7.1) |
| `reliability_score` | uint32 (bp) | Non-consensus heuristic `[0..10000]`: blends resolution success and dispute-win ratios, minus `1000` per ban |

### 7.15 `pm_position_api_object` (from `get_account_positions`)
| Field | Type | Description |
|---|---|---|
| `bet` | `pm_bet_object` | The bet (§7.4) |
| `expected_payout` | share_type | Parimutuel payout if this side wins (or realized payout once settled). Mirrors settlement exactly |
| `market_status` | int8 | The market's `status` |
| `resolved_outcome` | int16 | The market's `resolved_outcome` (`-1` if unresolved) |

### 7.16 `pm_market_weight_sums_api_object` (from `get_market_weight_sums`)
| Field | Type | Description |
|---|---|---|
| `market_type` | uint8 | `0` binary / `1` multi |
| `bets_sum` | share_type | Total staked |
| `outcomes` | `pm_weight_entry[]` | Per-outcome breakdown |

`pm_weight_entry`: `outcome_index` (int16), `label` (string), `bets_sum` (share_type), `weight_sum` (share_type).

### 7.17 `pm_dispute_votes_api_object` (from `get_dispute_votes`)
| Field | Type | Description |
|---|---|---|
| `votes` | `pm_dispute_vote_object[]` | All ballots (§7.8) |
| `uphold_weight` | int64 | **Legacy** rough tally (Σ`|vote_percent|` upholding oracle) |
| `challenge_weight` | int64 | Legacy: Σ`|vote_percent|` backing a change |
| `total_weight` | int64 | `uphold + challenge` (legacy) |
| `challenger_leads` | bool | Legacy: challenge share ≥ approve-min |
| `proposed_outcome` | int16 | Disputer's proposed outcome |
| `participation_shares` | int64 | Σ voter weight (vesting-shares) that has voted |
| `electorate_shares` | int64 | `total_vesting_shares + pool_NAV→shares` (quorum base) |
| `quorum_required_shares` | int64 | `electorate × pm_dispute_approve_min_percent` |
| `quorum_percent_bp` | int32 | `participation / electorate` (bp) |
| `quorum_reached` | bool | `participation ≥ quorum_required` |
| `oracle_defense_shares` | int64 | Σ rshares defending the oracle |
| `change_shares` | int64 | Σ rshares backing an outcome change |
| `outcome_change_shares` | int64[] | Per-outcome backing rshares (size = `outcome_count`) |
| `expected_uphold` | bool | `true` ⇒ oracle resolution stands if finalized now |
| `expected_outcome` | int16 | Outcome that would be set at finalize now |
| `expected_consensus_strength_bp` | int32 | `winning / participation` (bp); `0` when uphold |

### 7.18 `pm_market_meta_object` (from `get_market_meta` / `list_markets_by_category`)
| Field | Type | Description |
|---|---|---|
| `id` | object-id | |
| `market` | object-id | Owning market |
| `category` | string | Parsed from metadata JSON |
| `subcategory` | string | |
| `tags` | string | Comma-joined |
| `banned_jurisdictions` | string | Comma-joined ISO codes; empty = allowed everywhere |
| `expiry` | time | Prune time (dispute window close + TTL) |

### 7.19 `pm_kline_api_object` (from `get_market_kline`)
| Field | Type | Description |
|---|---|---|
| `seq` | uint32 | 0-based contiguous index of the change within the market |
| `timestamp` | uint32 | **Unix seconds** (x coordinate) |
| `reason` | uint8 | `0` bet, `1` cancel, `2` liquidation, `3` batch settle, `4` leverage open, `5` leverage resolve |
| `bets_sum` | share_type | Total staked across all outcomes at this point |
| `weights` | share_type[] | Per-outcome staked weight (y values), index = `outcome_index`. For binary: `[a_bets_sum, b_bets_sum]` |

For charting: `x = timestamp`, `y[i] = weights[i]`; implied probability of outcome `i` =
`weights[i] / Σ weights`.

### 7.20 `pm_leverage_quote_api_object` (from `get_leverage_quote`)
| Field | Type | Description |
|---|---|---|
| `available` | bool | `true` ⇒ `max_loan > 0` (some leverage possible) |
| `outcome_index` | int16 | Echoed side (0/1) |
| `collateral` | share_type | Echoed collateral |
| `max_loan` | share_type | Largest solvent loan (`0` if none qualifies) |
| `max_leverage_x100` | uint32 | `(collateral+max_loan)/collateral × 100` (`100` = 1.00×) |
| `pool_free_amount` | share_type | `free_balance − leverage_fund_used` |
| `fund_available` | share_type | `free_balance × pm_leverage_fund_percent − leverage_fund_used` |
| `per_position_cap` | share_type | `fund_available × pm_leverage_max_per_position_bp` |
| `market_position_cap` | share_type | `liquidity_sum × pm_leverage_max_position_ratio_percent` |
| `pool_profit_percent` | uint16 | `R` (plain %) |
| `safety_margin_percent` | uint16 | `S` (plain %) |
| `max_slippage_percent` | uint16 | `SL` (plain %) |
| `m_factor_percent` | uint16 | worst-opposing m-factor (plain %) |
| `expiration_buffer_sec` | uint32 | Leverage disabled this long before `betting_expiration` |
| `auto_close_time` | time | `betting_expiration − buffer` (protocol force-close point) |
| `stops` | `pm_leverage_stop[]` | Up to 12 solvent slider stops (`0 < loan ≤ max_loan`, ≥1.01×) |
| `failed_constraints` | `pm_leverage_constraint[]` | Populated when `!available` |

`pm_leverage_stop`: `leverage_x100` (uint32), `loan`, `total_bet`, `expected_tokens`, `pool_profit`,
`liquidation_threshold`, `current_cancel_value`, `worst_case_cancel_value` (all share_type).
`pm_leverage_constraint`: `constraint` (string key: `leverage_disabled` / `cpmm_binary_only` /
`market_inactive` / `expiration_buffer` / `min_market_liquidity` / `fund_availability` /
`position_size` / `solvency`), `reason` (string).

### 7.21 `pm_leverage_close_preview_api_object` (from `get_leverage_close_preview`)
| Field | Type | Description |
|---|---|---|
| `position_id` | int64 | Echoed position |
| `outcome_index` | int16 | Position side |
| `cancel_value` | share_type | VIZ the tokens fetch from the curve now |
| `pool_obligation` | share_type | `liquidation_threshold` → returned to the pool |
| `bettor_receives` | share_type | `cancel_value − pool_obligation` (floored 0) |
| `collateral` | share_type | Original bettor stake |
| `loan` | share_type | Pool loan |
| `pool_profit_charge` | share_type | Pool's fixed profit on the loan |
| `closeable` | bool | `cancel_value ≥ pool_obligation` (else the protocol liquidates) |
| `loss_vs_collateral` | int64 | `collateral − bettor_receives` (negative = profit) |
| `loss_percent_bp` | int32 | `loss_vs_collateral / collateral` (bp) |

### 7.22 `pm_leverage_convert_preview_api_object` (from `get_leverage_convert_preview`)
| Field | Type | Description |
|---|---|---|
| `position_id` | int64 | Echoed position |
| `outcome_index` | int16 | Position side |
| `cancel_value` | share_type | VIZ the tokens fetch now |
| `pool_obligation` | share_type | Loan + pool profit (repaid on convert) |
| `current_profit` | share_type | `cancel_value − pool_obligation` |
| `conversion_profit_cost_percent` | uint16 | Median value the convert op **must** echo |
| `conversion_fee` | share_type | `current_profit × cost% / 100` |
| `total_user_payment` | share_type | `pool_obligation + conversion_fee` (debited on convert) |
| `convertible` | bool | `current_profit > 0` |

### 7.23 `pm_market_categories_api_object` (from `get_market_categories`)
| Field | Type | Description |
|---|---|---|
| `categories` | `pm_category_count[]` | Sorted by count desc |
| `hot_tags` | `pm_tag_count[]` | Top 20 tags by count (jurisdiction-* excluded) |

`pm_category_count`: `category` (string), `count` (uint32), `subcategories` (`pm_subcategory_count[]`).
`pm_subcategory_count`: `subcategory` (string), `count` (uint32).
`pm_tag_count`: `tag` (string), `count` (uint32).

### 7.24 `pm_market_full_api_object` (from `get_market_full`)
| Field | Type | Description |
|---|---|---|
| `market` | `pm_market_object` | The market (§7.2) |
| `outcomes` | `pm_outcome_object[]` | Outcomes (§7.3); empty for binary markets |
| `weight_sums` | `pm_market_weight_sums_api_object` | Per-outcome amount + curve weight (§7.16) |
| `oracle` | `pm_oracle_api_object` \| null | The market's oracle + reliability (§7.14); null if not found |
| `meta` | `pm_market_meta_object` \| null | Parsed metadata (§7.18); null if not indexed |
| `my_positions` | `pm_position_api_object[]` | The `account` arg's bets on this market (empty if no account) |
| `my_leverage_positions` | `pm_leverage_position_object[]` | The account's leverage positions on this market |
| `my_liquidity` | `pm_liquidity_object[]` | The account's LP positions on this market |

`oracle` and `meta` are optional (JSON `null` when absent). The `my_*` arrays are empty unless the
optional `account` argument was supplied.

---

## 8. Enum / status quick reference

| Enum | Values |
|---|---|
| `market.market_type` | `0` binary (CPMM), `1` multi (LMSR) |
| `market.status` | `-1` deleted, `0` waiting, `1` active, `2` closed, `3` resolved |
| `market.payout_status` | `0` none, `1` pending, `2` paid, `3` disputed |
| `market.dispute_mode` | `0` committee, `1` account |
| `market.endogeneity_tier` | `1` econ-data, `2` sports, `3` political |
| `bet.status` | `0` active, `1` cancelled, `2` refunded, `3` resolved, `5` queued, `6` revealed-pending |
| `bet.mode` / `place_bet.mode` | `0` instant, `1` batch |
| `liquidity.status` | `0` active, `3` resolved/closed |
| `commit.status` | `0` committed, `1` revealed, `2` forfeited |
| `dispute.status` | `0` open, `1` oracle-wrong, `2` oracle-right, `3` auto-closed |
| `leverage_position.status` | `0` active, `1` liquidated, `2` resolved_won, `3` resolved_lost, `4` closed_voluntary, `5` converted |
| `leverage_liquidate.reason` | `0` opposing_bet, `1` cancel_bet, `2` expiration |
| `lazy_allocation.status` | `0` active, `1` returned |
| `kline.reason` | `0` bet, `1` cancel, `2` liquidation, `3` batch settle, `4` leverage open, `5` leverage resolve |
| `dispute_vote.vote_outcome` | `-1` uphold oracle; `≥0` proposed correct outcome |

---

## 9. Chain (governance) properties — `get_pm_chain_properties`

These are median-voted validator params (part of `versioned_chain_properties`). Values shown are
**mainnet defaults**; a client must read live values via `get_pm_chain_properties`. Assets are
VIZ; `*_percent` are **bp (10000 = 100%)** unless the row says otherwise.

| Field | Default | Unit | Meaning |
|---|---|---|---|
| `pm_oracle_registration_fee` | 10.000 VIZ | asset | Oracle registration fee → committee fund |
| `pm_min_oracle_insurance` | 5000.000 VIZ | asset | Minimum insurance bond |
| `pm_market_creation_fee` | 5.000 VIZ | asset | Market creation fee → committee fund |
| `pm_min_liquidity` | 100.000 VIZ | asset | Minimum seed liquidity |
| `pm_max_outcomes` | 10 | count | Max outcomes per multi market (`≤ 16`) |
| `pm_max_market_duration` | 31536000 | sec | Max market lifetime (≤ 1 year) |
| `pm_max_oracle_fee_percent` | 500 | bp | Cap on oracle fee % (5%) |
| `pm_listing_min_coverage_percent` | 250 | **coverage %** | Hide markets whose oracle insurance covers < this % of bets (250 = 2.5×); enforced by `list_markets`/`list_markets_by_category` (revealed via `show_risky`) |
| `pm_betting_min_coverage_percent` | 150 | **coverage %** | Advisory: below this coverage a client should require an explicit risk confirmation. Not enforced on-chain; must be `≤ pm_listing_min_coverage_percent` |
| `pm_default_time_penalty_percent` | 50 | bp | Default time penalty |
| `pm_max_time_penalty` | 1000000 | 1e6=100% | Max time penalty on profit |
| `pm_dispute_fee` | 1000.000 VIZ | asset | Dispute filing fee |
| `pm_dispute_grace_sec` | 43200 | sec | Payout grace after resolution (12h) |
| `pm_oracle_dispute_response_sec` | 43200 | sec | Oracle response window (12h) |
| `pm_dispute_auto_close_sec` | 1209600 | sec | Anti-freeze auto-close (14d) |
| `pm_dispute_vote_period_sec` | 259200 | sec | Committee voting period (3d) |
| `pm_dispute_approve_min_percent` | 1000 | bp | Quorum / participation threshold |
| `pm_oracle_penalty_percent` | 500 | bp | Insurance slashed on missed deadline |
| `pm_no_contest_penalty_percent` | 5000 | bp | % of dispute fee on no-contest (50%) |
| `pm_dispute_reward_multiplier` | 30000 | bp | Dispute reward multiplier (10000=1×, default 3×) |
| `pm_batch_epoch_blocks` | 20 | blocks | Batch epoch length (~60s) |
| `pm_reveal_window_blocks` | 200 | blocks | Reveal window (~10min) |
| `pm_commit_no_reveal_penalty_percent` | 2000 | bp | No-reveal penalty → winners' pool (20%) |
| `pm_min_batch_bet` | 1.000 VIZ | asset | Anti-dust minimum batch bet |
| `pm_commit_reveal_enabled` | true | bool | Commit-reveal kill-switch |
| `pm_processing_cap_per_block` | 200 | count | Bounded per-block virtual-op work |
| `pm_lazy_pool_enabled` | true | bool | Lazy-pool kill-switch |
| `pm_lazy_alloc_percent` | 2000 | bp | Free balance allocated per market |
| `pm_lazy_max_total_alloc_percent` | 7000 | bp | Cap on total allocation |
| `pm_lazy_lock_sec` | 604800 | sec | Deposit lock (7d) |
| `pm_lazy_recall_step_percent` | 1000 | bp | Recalled per idle step |
| `pm_lazy_emergency_penalty_percent` | 5000 | bp | Emergency-withdraw penalty on profit |
| `pm_leverage_enabled` | false | bool | **Leverage kill-switch (off by default)** |
| `pm_leverage_fund_percent` | 10 | **percent** | % of free balance usable for loans (F) |
| `pm_leverage_max_per_position_bp` | 20 | bp | Fund-available per position (P=0.2%) |
| `pm_leverage_pool_profit_percent` | 10 | **percent** | Pool profit per loan (R) |
| `pm_leverage_safety_margin_percent` | 1 | **percent** | Open-time safety buffer (S) |
| `pm_leverage_max_slippage_percent` | 10 | **percent** | Max price impact per bet (SL) |
| `pm_leverage_min_market_liquidity` | 5000.000 VIZ | asset | Min liquidity for leverage |
| `pm_leverage_max_position_ratio_percent` | 5 | **percent** | Max position as % of `liquidity_sum` |
| `pm_leverage_expiration_buffer_sec` | 86400 | sec | Leverage disabled N sec before expiration |
| `pm_leverage_m_factor_percent` | 50 | **percent** | `M_effective = M_max × this%` |
| `pm_conversion_profit_cost_percent` | 50 | **percent** | Fee % of unrealized profit on convert |

> **Percent-scale trap for library authors:** the `pm_leverage_*` and `pm_conversion_*` knobs use
> plain percent (`10 = 10%`, validated `≤ 100`), NOT basis points, unlike every other `*_percent`
> in this table. `pm_leverage_max_per_position_bp` is the one leverage knob that is genuinely bp.

---

## 10. Fixed limits (compile-time, from `config.hpp`)

| Constant | Value | Applies to |
|---|---|---|
| `MAX_PM_DECISION_URL_LEN` | 256 | `pm_resolve_market.decision_url` |
| `MAX_PM_PROFILE_URL_LEN` | 256 | `pm_oracle_register.rules_url` |
| `MAX_PM_DISPUTE_REASON_LEN` | 1024 | `pm_no_contest.reason`, `pm_dispute_create.reason` |
| `MAX_PM_MARKET_TITLE_LEN` | 256 | `pm_create_market.url` |
| `MAX_PM_OUTCOME_LABEL_LEN` | 64 | each `pm_create_market.outcomes[i]` |
| `MAX_PM_OUTCOMES_PER_MARKET` | 16 | hard ceiling for `pm_max_outcomes` |
| `LAZY_POOL_PRECISION` | 1e9 | `reward_per_share` accumulator scale |

`metadata` on `pm_create_market` has **no length cap** at the protocol level (like `custom_operation`).

---

## 11. Implementation checklist per library

1. **Operation builders** — 23 user ops (§3), each serialized as `["<name>", {fields}]` in JSON
   order, with `extensions: []`. Percent-scale per field (§1.1 / §9 trap).
2. **Asset handling** — VIZ, 3 decimals; format/parse `"x.yyy VIZ"`. Internal `share_type` = ×1000 integer.
3. **API client** — 29 read methods (§5) via `call("prediction_market_api", …, [args])`, `limit ≤ 1000`.
4. **Object models** — decode §7 objects; keep `uint128`/large `int64` as big-int/string in JS.
5. **Virtual-op parsing** — recognize the 12 vops (§4) in account/block history.
6. **Enums** — map §8 status codes to human labels.
7. **Metadata** — write the `category`/`subcategory`/`tags`/`banned_jurisdictions` convention (§6).
8. **Governance** — surface `get_pm_chain_properties` (§9) so UIs read live limits, not hard-coded defaults.

---

*Source of truth (regenerate this doc if these change):*
`libraries/protocol/include/graphene/protocol/pm_operations.hpp`,
`pm_virtual_operations.hpp`, `operations.hpp`,
`libraries/chain/include/graphene/chain/pm_objects.hpp`,
`libraries/protocol/include/graphene/protocol/chain_operations.hpp` (`chain_properties_pm`),
`libraries/protocol/include/graphene/protocol/config.hpp` (`MAX_PM_*`),
`plugins/prediction_market_api/` (`prediction_market_api.{hpp,cpp}`, `meta_object.hpp`, `kline_object.hpp`).
