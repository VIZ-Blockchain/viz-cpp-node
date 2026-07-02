# Forecaster / Onix — Frontend Design Document

**Purpose of this document.** This is a complete, user-facing specification of the *current working prototype* of the Forecaster prediction market (protocol name **Onix**). It is written for the developers who will build a **new thin client on the VIZ blockchain** (using the `viz-js` library — see the [VIZ node docs](https://github.com/VIZ-Blockchain/viz-cpp-node/tree/master/docs)). Use it as a coverage checklist: every screen, capability, API call and transaction the current app performs is enumerated here so you can confirm the thin client reproduces all of them and maps each server-side API call to the correct on-chain operation / query.

**How to read it.**
- The prototype is a single-page app (`app.js`, cash/jQuery-style `$`) served by a thin PHP router. It talks to the backend exclusively through `POST`/`GET` calls to `/api/<endpoint>/` returning JSON. Every one of those 58 endpoints is documented, with the exact request fields the client sends and the exact response fields the UI consumes.
- Each functional area has its own section (2–9). Section 10 is the **master coverage matrix** — one row per endpoint — that node developers should tick off against their transaction map.
- "VIZ thin-client note" call-outs are *suggestions* for how an action might map to a blockchain operation/query. They are hints for the node team, not a description of the current server internals (which are already ported).
- Internal server mechanics (LMSR/parimutuel pricing, fixed-point math, DB schema, chain settlement) are **out of scope** — this document only covers the client contract and UX.

> Terminology note: the codebase uses **Onix Binary** for two-outcome (Yes/No) markets and **Onix Multi** for categorical markets with 2–10 outcomes. Binary now settles parimutuel, same as Multi.

---

## Contents

1. [Scope, roles, navigation & glossary](#1-scope-roles-navigation--glossary)
2. [App shell, authentication, session, profile & role registration](#2-app-shell-authentication-session-profile--role-registration)
3. [Browsing markets: list, catalog & filtering](#3-browsing-markets-list-catalog--filtering)
4. [Market detail & binary betting (bet, commit-reveal, cancel, transfer)](#4-market-detail--binary-betting-bet-commit-reveal-cancel-transfer)
5. [Multi-outcome markets (create, bet, cancel, liquidity, resolve)](#5-multi-outcome-markets-create-bet-cancel-liquidity-resolve)
6. [Creating a market (creator role)](#6-creating-a-market-creator-role)
7. [Oracle role: acceptance, insurance, pending queue & resolution](#7-oracle-role-acceptance-insurance-pending-queue--resolution)
8. [Disputes & DAO / committee governance](#8-disputes--dao--committee-governance)
9. [Wallet, balance, history, liquidity pools & leverage](#9-wallet-balance-history-liquidity-pools--leverage)
10. [Master API / transaction coverage matrix](#10-master-api--transaction-coverage-matrix)

> **⚠ Contract gaps found during this audit** (see §5 for detail) — the node team should treat these as known bugs to fix rather than reproduce: (1) the "Add liquidity" button always calls the binary `add-liquidity` even on multi markets — `add-liquidity-multi` / `withdraw-liquidity-multi` have **no client caller**; (2) multi resolution sends field `outcome` but `resolve-market-multi` reads `winning_outcome`; (3) the multi-cancel success toast reads `data.returned` while the endpoint returns `return_amount`.

---

## 1. Scope, roles, navigation & glossary

### 1.1 Actors / roles

The app recognises the following roles (About screen, `about.role_*`). A single account may hold several roles at once; role capability gates specific screens and API calls.

| Role | In-app name | What they do | Gated by |
|------|-------------|--------------|----------|
| **Player / Bettor** | Player (`about.role_player`) | Buys outcome tokens (bets), cancels, transfers positions, claims payouts | Any authenticated user |
| **Market Creator** | Market creator (`about.role_creator`) | Creates markets, sets rules/oracle/committee/fees | `user.create_market == 1` (must **register as creator**) |
| **Oracle** | Oracle (`about.role_oracle`) | Accepts/rejects assigned markets, resolves outcome, no-contest, funds insurance | Registered oracle (`register-oracle`) |
| **Liquidity Provider (LP)** | Liquidity provider (`about.role_lp`) | Adds/withdraws liquidity, lazy-pool deposits, leverage/boost | Any authenticated user |
| **Disputer** | (a Player who disputes) | Opens a dispute against an oracle's resolution | Any bettor on the market, within grace window |
| **Oracle-as-respondent** | Oracle | Responds to a dispute raised against them | The market's oracle |
| **Arbitrator / Committee / DAO resolver** | Arbitrator "Judge" (`about.role_judge`) | Votes/decides disputes (oracle right/wrong + justification), can slash, unban | Committee member / DAO voter / private resolver |

### 1.2 Authentication

Two entry paths (see §2):
1. **Telegram Mini App** — `window.Telegram.WebApp.initData` is POSTed to `check-session` → returns a session token + user.
2. **Website / browser** — `auth-code` returns a one-time code the user pastes into the Telegram bot to bind a session.

Session token is persisted in a cookie and `localStorage`; on every load `load-session` re-hydrates the user object.

### 1.3 Navigation map

The shell has a **4-item bottom tab bar** (`template/index.tpl`). Visibility is role-gated by CSS classes `v1`/`v3`/`v4` set in `update_user()`:

- **v1** — not logged in: only *Markets* + *Profile* (login).
- **v3** — logged in, not a creator: *Markets*, *Balance*, *Profile*.
- **v4** — logged in creator: *Markets*, *Create*, *Balance*, *Profile*.

Routing is hash-based: `#tab/<tab>/<subpath...>` (`select_tab()` / `check_hash()` / `hashchange`). Full screen map:

| Hash | Tab | Screen | Section |
|------|-----|--------|---------|
| `#tab/market` | Markets | Market **list** — status filter (all/active/resolved/closed) + "show risky" + category/subcategory/tags/sort filters | §3 |
| `#tab/market/about` | Markets | **About / landing** page (default when no hash) | §1.5 |
| `#tab/market/<id>` | Markets | **Market detail** (odds, bet form, my bets, resolution, dispute blocks) | §4, §5, §7, §8 |
| `#tab/market/oracle/<id>` | Markets | **Oracle public profile** (reliability, stats) | §7 |
| `#tab/create` | Create | **Create market** form (creator only) | §6 |
| `#tab/history` | Balance | **Balance / wallet**: top-up, withdraw, transaction history | §9 |
| `#tab/profile` | Profile | **Profile**: identity, balances, role registration, preferences, oracle settings | §2 |
| `#s/<session>` | — | Session-injection deep link → redirects to Profile | §2 |

### 1.4 System settings the UI reads

`load-settings` returns a config object; the client reads these keys via `get_setting()`:

| Key | Used for |
|-----|----------|
| `dispute_grace_hours` | Grace window (countdown) before a resolution is final / disputable |
| `min_risk_score_listing` | Threshold below which a market is "risky" (hidden unless *show risky*) |
| `min_risk_score_betting` | Threshold below which betting is risk-blocked (mandatory risk confirm) |
| `max_time_penalty` | Cap on time-penalty display in market detail |
| `commit_no_reveal_penalty_permille` | Penalty (‰) applied to committed-but-unrevealed hidden bets |
| `lmsr_min_outcomes` / `lmsr_max_outcomes` | Min/max outcomes for Onix Multi creation |

### 1.5 About / landing screen

Default landing (`render_about_page`, shown when no hash). Static, no API calls. Content (`about.*`): title + lead, the 5 **platform roles** (Player, Creator, Oracle, LP, Arbitrator/Judge), a "how it works in 5 steps" block, **market types** (Onix Binary = Yes/No; Onix Multi = 2–10 outcomes), FAQ, copyright and Onix protocol credit. Purely informational — the thin client can render it statically.

### 1.6 Glossary of on-screen money/label conventions

- Balances are integers in base units; the UI divides by `price_precision_ratio` (VIZ precision = 3) and renders with `render_number()`. Currency glyph is **Ƶ** (VIZ).
- Profile shows up to four sub-balances: **available** (`balance`), **bets** (`bets_balance`), **liquidity** (`liquidity_balance`), **oracle** (`oracle_balance`).
- **Reliability / Trust index** — an oracle score 0–100 rendered as a coloured badge with class buckets: excellent / good / average / new / poor / unreliable (`reliability_score_class` / `_label`, `oracle.score_*`).
## 2. App shell, authentication, session, profile & role registration

This section documents the application shell (HTML container, bottom tab bar, hash routing, message area), the two identity flows (Telegram WebApp vs. website one-time code), session persistence, the Profile screen, and the Oracle / Creator / preferences registration forms. All field names below are taken verbatim from `template/index.tpl`, `app.js`, `module/api.php`, and the English labels from `i18n/en.json`.

> **Note on transport.** The client always POSTs/GETs JSON to `/api/<endpoint>/` (trailing slash). The generic helper `api_post(endpoint, body)` (app.js ~2044) does `fetch('/api/'+endpoint+'/', {method:'POST', body: JSON.stringify(body)})`, parses JSON, and **throws `new Error(data.error || 'Error')` on any non-2xx** — so every `api_post` caller shows the server's `error` string. Some shell calls use raw `fetch` with GET; those are noted per endpoint. Session is NOT sent in the JSON body for `api_post` calls — the server resolves the user from the `forecaster_session` cookie (except `load-session`, which sends `{session}` explicitly).

---

### 2.1 Application shell (`template/index.tpl`)

The whole app is a single HTML page. Server-side placeholders (`{title}`, `{head_addon}`, `{meta_image}`, `{content}`, `*_change_time`) are filled by PHP at render time; the SPA logic is entirely in `app.js` + `market_math.js`, using `cash.min.js` (the `$` shim) and Telegram's `telegram-web-app.js`.

Structure the thin client must reproduce:

- **`.container`** — white card holding all screens.
- **`.messages`** (hidden by default) — the toast/alert area. `show_message(type, text, timeout)` appends `<div class="message alert-<type>">…</div>` where `type` ∈ `success | danger | warning`. Default timeout 5000 ms; pass `false` to make a message sticky (used for the browser-login code and the risk confirmation). `check_messages()` toggles the container's visibility based on child count.
- **Four tab-content panels**, each `id` mapped to a bottom-nav tab:
  | Panel `id` | Bottom-nav `data-tab` | Label (en.json) |
  |---|---|---|
  | `market` | `market` | `nav.markets` = "Markets" |
  | `create` | `create` | `nav.create` = "Create" |
  | `history` | `history` | `nav.balance` = "Balance" (shows live balance `<span class="balance">` + `Ƶ`) |
  | `profile` | `profile` | `nav.profile` = "Profile" |

  Note the **`history` panel/tab is the "Balance" screen** — internal id is `history`, user-facing label is "Balance".
- **`.tabs`** — fixed bottom bar. `update_nav_labels()` (app.js 74) rewrites the four tab captions from i18n after locale load; the Balance tab is special-cased to preserve the inner `.balance` span and re-append `" Ƶ"`.

**Tab width classes `v1`/`v2`/`v3`/`v4`** (index.tpl CSS 364–379) set each tab to 100 % / 50 % / 33 % / 25 % width. `update_user()` (app.js 236) sets exactly one class based on auth/role so only visible tabs occupy the bar:
- Not authenticated → `.tabs.v1`; only **Markets** is shown (Create/Balance/Profile hidden via `display:none`).
- Authenticated **without** `create_market` → `.tabs.v3`; Markets + Balance + Profile visible; Create hidden.
- Authenticated **with** `create_market == 1` → `.tabs.v4`; all four tabs visible.

(`v2` exists in CSS but is not assigned by `update_user()`.)

> **VIZ thin-client note (suggestion):** the tab-gating is purely a UI concern driven by two account flags (`create_market`, and implicitly auth). On VIZ these map to on-chain/account state the thin client can read once at boot (e.g., "is this account a registered creator?") and cache; there is no per-render server round-trip needed just to decide tab visibility.

---

### 2.2 Hash routing scheme `#tab/<tab>/<subpath>`

Routing is driven by `location.hash`. Three functions cooperate:
- `check_hash()` (app.js 216) — runs on boot and after `update_user()`.
- `hashchange` handler (app.js 442) — runs on every hash change.
- `select_tab(name)` (app.js 814) — the router body that renders a tab's content based on the remaining hash segments.

Hash grammar and enumerated subpaths (from `select_tab`, splitting `hash` on `/`):

| Hash | Screen rendered | Notes |
|---|---|---|
| *(empty)* | redirects to `#tab/market/about` | default landing = About page |
| `#s/<session>` | sets `session=<…>`, replaces to `#tab/profile`, selects Profile | **login-by-link**: session token passed in the URL fragment |
| `#tab/market` | Markets list | status filter tabs, category filter, `load_markets_list(-1,0)` |
| `#tab/market/about` | About page (`render_about_page()`) | also reached by clicking the logo |
| `#tab/market/oracle/<id>` | Oracle public profile | `load_oracle_profile(id)` → `load-oracle-profile` |
| `#tab/market/<market_id>` | Single market detail | `load_market_detail(id)` (any numeric 3rd segment that isn't `about`/`oracle`) |
| `#tab/create` | Create-market form | role-gated on `create_market`; loads oracles + committees |
| `#tab/history` | Balance screen (deposit/withdraw + history table) | `load-history` |
| `#tab/profile` | Profile screen | rendered by `update_user()` + `render_profile_extra()` |

`navigate(hash)` pushes a new history entry (no-op if unchanged); `navigate_replace(hash)` replaces the current entry (used to normalize `#s/…` and to strip subpaths after rendering Create/Balance). Tabs are also clickable directly: `app_mouse` (app.js 3018) intercepts clicks on `.tab` and calls `navigate('#tab/'+data-tab)`; clicking `.logo-clickable` navigates to `#tab/market/about`.

---

### 2.3 Boot sequence & the two login flows

On `$(function(){…})` (app.js 456) the client:
1. `load_system_settings()` (see 2.7) and `init_i18n()` → then `update_nav_labels()` + `load_categories()`.
2. Reads the session token from the **`forecaster_session` cookie**, falling back to **`localStorage['forecaster_session']`**, else from a `#s/…` hash.
3. If a token exists → `try_load_session()`. On error, clears the token and falls through to `try_check_session()`. On success, sets `auth=true`, `user=result`, adopts `user.lang` if present, and calls `update_user()`.
4. If no token → `try_check_session()`.

**Flow A — Telegram WebApp (automatic).** `try_check_session()` (app.js 373) checks `window.Telegram.WebApp.initData`. If non-empty it POSTs it to `check-session`. On success it calls `save_session(result, update_user)` which stores `session`, sets the cookie to expire at `result.expiration`, mirrors to localStorage, sets `auth=true`, `user=result.user`.

**Flow B — Website one-time auth code.** If there is no Telegram initData, `try_check_session()` GETs `auth-code`, then shows a **sticky warning** `auth.browser_login` = *"Sign in via browser through Telegram bot"* with a deep link `https://t.me/viz_forecaster_bot?start=<CODE>`. The user opens the bot, which (out of band) issues a session; the bot hands the session back to the site via a `#s/<session>` link, closing the loop into Flow A's persistence.

**Session persistence** (`save_session` app.js 352, `save_session_simple` app.js 339): the token lives in the `forecaster_session` cookie (path `/`, 30-day expiry for the simple path, or server `expiration` for the full path) **and** mirrored to `localStorage`. Logout (`logout-action` in `app_mouse` 3124) sets `session=false; auth=false; user={}` then `save_session_simple()` which deletes both cookie and localStorage entry and re-renders as logged-out.

> **VIZ thin-client note (suggestion):** On VIZ the identity is a keypair, not a server session cookie. The thin client would replace both flows with local key management: derive/import the VIZ account key, and instead of a server-issued `session` token, sign each state-changing request (a broadcast) with the account's active key. The Telegram-initData verification and the one-time-code bridge are server-auth artifacts that disappear; what must be preserved is the *account identity* and the *balance/role fields* the UI reads from `user`.

#### `load-session`
- **Method:** POST · **When:** boot, whenever a stored session token exists; also after any role/pref change via `update_user_data()`. · **Auth:** the token itself.
- **Request**
  | Field | Type | Meaning |
  |---|---|---|
  | `session` | string | session hash from cookie/localStorage/hash |
- **Response:** the full `users` row (with `data` JSON-decoded). UI consumes (see Profile 2.5): `data.username`, `data.first_name`, `data.last_name`, `data.photo_url`, `create_market`, `balance`, `bets_balance`, `bets_count`, `liquidity_balance`, `oracle_balance`, `oracle`, `oracle_insurance`, `oracle_rules`, `oracle_fee`, `oracle_fixed_fee`, `account`, `jurisdiction`, `lang`, `memo`.
- **Errors:** throws `Unknown session` / `Unknown user from session` (non-2xx) → boot shows `auth.session_error` and falls back to `try_check_session`.

#### `check-session`
- **Method:** POST · **When:** running inside Telegram WebApp with non-empty `initData`. · **Auth:** Telegram HMAC signature (validated server-side against `bot_key`).
- **Request**
  | Field | Type | Meaning |
  |---|---|---|
  | `data` | string | raw `window.Telegram.WebApp.initData` querystring |
- **Response**
  | Field | Type | UI use |
  |---|---|---|
  | `session` | string | stored as the session token; cookie/localStorage |
  | `expiration` | int (unix s) | cookie expiry |
  | `user` | object | full users row → becomes `user`, drives Profile & tabs |
- **Errors:** `Unknown tg init data`, `Time range from tg init data exceed 10 min range`, `Hash from tg init data is different` → boot renders logged-out.

#### `auth-code`
- **Method:** GET · **When:** website context (no Telegram initData). · **Auth:** none.
- **Request:** none.
- **Response**
  | Field | Type | UI use |
  |---|---|---|
  | `code` | string (sha256) | inserted into the `auth.browser_login` deep-link `?start=<code>` shown as a sticky warning |
- **Errors:** on failure the client throws and renders logged-out.

---

### 2.4 The Markets / Create / Balance tabs (routing entry points)

Only the routing side is in scope here (screen bodies are documented in later sections). From `select_tab`:
- **`create`** — if `!auth`, shows `market.not_logged_in`; if `auth` but `create_market != 1`, shows `market.cannot_create` = *"You cannot create markets."*; if `create_market == 1`, renders the full create form and eagerly GETs `load-oracles` and `load-committees` to populate the Oracle/Committee `<select>`s.
- **`history`** (Balance) — if `!auth`, `market.not_logged_in`; else shows available balance, Top-up / Withdraw toggles, and a 100-row history table via GET `load-history`. History type labels come from `balance.history_type_0..16`.
- **`market`** — default view: status filter chips (`filter.all` / `filter.active` / `filter.resolved` / `filter.closed`, `data-status` = -1/1/3/2), a **"Show risky markets"** checkbox (`filter.show_risky`), the category filter, then the list.

---

### 2.5 Profile screen (`update_user()` + `render_profile_extra()`)

Rendered into `#profile .section`. When **not authenticated**, shows `profile.not_logged_in` = *"You are not logged in… sign in via the Telegram bot."* and applies `.tabs.v1`.

When **authenticated**, `update_user()` (app.js 236) builds, in order:
1. **Avatar** — `.avatar` div background = `user.data.photo_url` (if present).
2. **Username** — `@<user.data.username>` as `.title`; also written into the Profile tab caption.
3. **Full name** — `user.data.first_name` + optional `last_name` as `.subtitle`.
4. **Create-tab gating** — if `user.create_market == 1` → `.tabs.v4` + show Create tab; else `.tabs.v3`.
5. **Live balance in the Balance tab** — `user.balance` (integer minor units; `price_precision_ratio` scales it) is abbreviated (k / kk) into `.tab[data-tab="history"] .balance`.
6. **Balance lines** (each shown only if the value is `> 0`), using `render_number()` (integer → `/1000`, 3 dp):
   | Line | i18n key | Label |
   |---|---|---|
   | available | `profile.available_balance` | "Available balance: %%AMOUNT%% Ƶ" (always shown) |
   | in-market bets | `profile.bets_balance` | "In market bets: %%AMOUNT%% Ƶ" |
   | bets count | `profile.bets_count` | "Bets placed: %%COUNT%%" |
   | in liquidity | `profile.liquidity_balance` | "In liquidity: %%AMOUNT%% Ƶ" |
   | oracle collateral | `profile.oracle_balance` | "Oracle collateral: %%AMOUNT%% Ƶ" |
7. **Log out** button — `.logout-action`, label `profile.logout` = "Log out".
8. **`.profile-extra`** container → filled by `render_profile_extra()`.

`render_profile_extra()` (app.js 2787) appends (all only when authenticated):
- **User Preferences** form (2.6): language `<select>` (`en`/`ru`, prefilled from `current_lang`) + jurisdiction `<input maxlength=5>` prefilled from `user.jurisdiction`, and **Save preferences** button (`profile.save_preferences`).
- **Oracle section** — branches on `user.oracle`:
  - If `oracle == 1`: shows insurance fund (`profile.oracle_insurance_display` with `user.oracle_insurance`), a deposit/withdraw insurance form (Deposit/Withdraw → `oracle-deposit-insurance` / `oracle-withdraw-insurance`, out of scope here), and an **Oracle settings** edit form (name / rules / fee ‰ / fixed fee) that re-submits `register-oracle`. Also renders pending markets awaiting the oracle's accept/reject (`load-pending-markets`).
  - Else: the **"Become an oracle"** registration form (2.6).
- **Creator section** — if `create_market != 1`: the **"Become a market creator"** form (2.6).
- **My positions** (`load-positions`) and **My payouts** (`load-payouts`) tables, and a boost-positions container (documented in later sections).

> **VIZ thin-client note (suggestion):** Everything on this screen is read-model state of a single account (balances split into available/bets/liquidity/oracle, counts, role flags, oracle metadata). On VIZ these become derived balances/asset holdings plus custom account metadata; the thin client can render this profile from account state + indexed positions rather than one monolithic `user` object.

---

### 2.6 Role registration & preferences endpoints

#### `register-oracle`
- **Method:** POST (`api_post`) · **When:** clicking **Register** in "Become an oracle" (`register_oracle_action`, app.js 2917) OR **Save settings** in the Oracle-settings edit form (`oracle_update_action`, app.js 2932). · **Auth:** required (server: "Auth required").
- **Required role:** any authenticated user may become an oracle (charged a registration fee); existing oracles use the same endpoint to update settings free of charge.
- **Request** (registration path sends the first three; settings-edit path adds `oracle_fixed_fee`):
  | Field | Type | Meaning |
  |---|---|---|
  | `account` | string | oracle display name (required; server throws "Oracle name is required" if empty) |
  | `oracle_rules` | string | operating rules / statement of intent |
  | `oracle_fee` | int | fee in ‰ (per-mille) taken from the market |
  | `oracle_fixed_fee` | float (VIZ) | fixed reward; server multiplies ×1000; ≤0 defaults to 5000 (=5 VIZ). *(sent only from the settings-edit form)* |
- **Response**
  | Field | Type | UI use |
  |---|---|---|
  | `status` | bool | success flag |
  | `updated` | bool | present when editing an existing oracle (no fee charged) |
- **UI states:** loading text `bet.sending` = "Sending…" / `profile.saving` = "Saving…"; success → `profile.registered_success` / `profile.settings_saved` (green); on throw shows `common.error_prefix` + message (red). On success calls `update_user_data()` (re-runs `load-session`, re-renders Profile).
- **Errors surfaced:** "Insufficient balance for registration fee" (new registration when `balance < oracle_registration_fee`), "Registration failed", "Profile update failed".

#### `register-creator`
- **Method:** POST (`api_post`) · **When:** clicking **Register** in "Become a market creator" (`register_creator_action`, app.js 2948). · **Auth:** required.
- **Request**
  | Field | Type | Meaning |
  |---|---|---|
  | `creator_rules` | string | creator's operating rules |
- **Response**
  | Field | Type | UI use |
  |---|---|---|
  | `status` | bool | success flag |
- **Server side-effects:** sets `create_market=1` **and** `add_liquidity=1`, stores `creator_rules`, deducts `creator_registration_fee` from balance. On success the UI calls `update_user_data()`, which flips the account to `.tabs.v4` and reveals the **Create** tab.
- **UI states:** loading `bet.sending`; success `profile.registered_success` (green); error `common.error_prefix` + message (red).
- **Errors surfaced:** "Already registered as market creator", "Insufficient balance for registration fee", "Registration failed", "Auth required".

#### `update-user-preferences`
- **Method:** POST (`api_post`) · **When:** clicking **Save preferences** (`save_preferences_action`, app.js 2974). · **Auth:** required.
- **Request**
  | Field | Type | Meaning |
  |---|---|---|
  | `lang` | string | UI language; server whitelists to `en`/`ru` (anything else → `en`) |
  | `jurisdiction` | string | country code; server uppercases, strips non-A–Z, truncates to 5 chars |
- **Response**
  | Field | Type | UI use |
  |---|---|---|
  | `status` | bool | success flag |
  | `lang` | string | normalized value echoed back |
  | `jurisdiction` | string | normalized value echoed back |
- **Client after success:** if `lang` changed, sets `current_lang`, persists `localStorage['user_lang']`, `await load_i18n(new_lang)`, `update_nav_labels()`; updates `user.jurisdiction` / `user.lang` in memory. The jurisdiction value later gates market visibility (`is_market_jurisdiction_banned`, app.js 2997) and triggers the `jurisdiction.warning` modal.
- **UI states:** loading `bet.sending`; success `common.success` = "Success!" (green); error `common.error_prefix` + message (red).
- **Errors surfaced:** `{status:false, error:'Not authenticated'}` when unauthenticated.

#### `load-oracle-profile`
- **Method:** POST (`api_post` via `load_oracle_profile`) · **When:** navigating to `#tab/market/oracle/<id>`, or clicking an `.oracle-profile-link`. · **Auth:** none required (public read).
- **Request**
  | Field | Type | Meaning |
  |---|---|---|
  | `oracle_id` | int | target oracle's user id |
- **Response** (consumed by the Oracle public-profile screen; labels under the `oracle.*` i18n namespace):
  | Field | Type | UI use |
  |---|---|---|
  | `id` | int | oracle id |
  | `name` | string | display name (`oracle.profile_title`) |
  | `rules` | string | shown under `oracle.rules_label` |
  | `fee` | int | ‰ fee (`oracle.fee_label`) |
  | `fixed_fee` | int | fixed reward (`oracle.fee_fixed`) |
  | `insurance` | int | insurance fund (`oracle.insurance_fund_display`) |
  | `banned` | int/bool | shows `oracle.banned_label` |
  | `ban_until` | int | `oracle.banned_until` / `oracle.banned_forever` |
  | `reliability_score` | int (0-100) | behavior score → reliability badge classes |
  | `is_new` | 0/1 | shows `oracle.hint_new` / "New oracle" |
  | `risk_score` | float | coverage = insurance ÷ active bets (`oracle.coverage`) |
  | `trust_score` | int (0-100) | composite Trust Index (`oracle.trust_index`) |
  | `metrics{}` | object | detailed stat table: `markets_accepted`, `markets_resolved`, `markets_no_contest`, `markets_missed`, `disputes_received`, `disputes_lost`, `disputes_won`, `disputes_auto_closed`, `dispute_responses_missed`, `total_volume_resolved`, `total_insurance_slashed`, `avg_resolution_time`, `bans_received`, `active_since`, `last_active_time` → the `oracle.stat_*` rows |
  | `derived{}` | object | `resolution_rate`, `dispute_loss_rate`, `no_contest_rate`, `deadline_miss_rate`, `dispute_response_rate` → the `oracle.rate_*` indicator rows |
- **Errors:** server throws `Oracle not found` → the detail area shows `oracle.load_error`.

> **VIZ thin-client note (suggestion):** Oracle registration, creator registration and preference updates are all account-metadata + fee mutations. On VIZ these become signed operations (e.g., an account custom-metadata/JSON op plus a token transfer for the registration fee) broadcast by the account key; the reliability/trust metrics in `load-oracle-profile` are indexer-computed aggregates the thin client would read from an off-chain indexer rather than the chain itself.

---

### 2.7 System settings the UI reads (`load-settings`)

#### `load-settings`
- **Method:** GET (raw `fetch` in `load_system_settings`, app.js 142) · **When:** first thing at boot. · **Auth:** none.
- **Request:** none.
- **Response:** a flat JSON object `{ key: value, … }` (every row of the server `settings` table). Stored in `system_settings`; read via `get_setting(key, default)` (app.js 149).

Keys the client actually reads (all `get_setting(...)` call sites in app.js):
| Setting key | Read at (app.js) | What the UI does with it |
|---|---|---|
| `lmsr_max_outcomes` | 726, 744 | max number of outcomes allowed when creating an Onix Multi market (`market.max_outcomes`) |
| `lmsr_min_outcomes` | 733, 745 | min number of outcomes for Onix Multi (`market.min_outcomes`) |
| `dispute_grace_hours` | 1225 | grace-period countdown before payout / dispute window (`status.grace_countdown`) |
| `min_risk_score_listing` | 1236 | threshold below which a market is treated as "risky" and hidden unless "Show risky" is on |
| `min_risk_score_betting` | 1237, 1713 | threshold below which betting shows the risk warning / requires explicit confirmation (`bet.risk_detail`) |
| `max_time_penalty` | 1686 | caps the displayed late-bet penalty max % |
| `commit_no_reveal_penalty_permille` | 2131 | penalty (‰) shown for commit-reveal bets that are never revealed |

> **VIZ thin-client note (suggestion):** these are global protocol parameters, not per-user data. On VIZ they would either be chain-consensus constants or values published by the protocol's governing account; the thin client should fetch them once at boot (as here) and cache. None of them require auth.

---

### 2.8 Session/identity summary for the porting team

- **Session token name:** `forecaster_session` (cookie **and** `localStorage`).
- **The `user` object** returned by `load-session` / `check-session.user` is the single source of truth for: identity (`data.username/first_name/last_name/photo_url`), balances (`balance`, `bets_balance`, `liquidity_balance`, `oracle_balance`), counts (`bets_count`), role flags (`create_market`, `oracle`, `add_liquidity`), oracle metadata (`account`, `oracle_rules`, `oracle_fee`, `oracle_fixed_fee`, `oracle_insurance`), preferences (`lang`, `jurisdiction`), and the deposit `memo`.
- **Role gates in the UI:** Create tab ⇔ `create_market == 1`; oracle settings/insurance UI ⇔ `oracle == 1`; the "Become …" forms appear only when the corresponding flag is unset.
## 3. Browsing markets: list, catalog & filtering

This section documents the **Markets** browsing screen (the default landing tab, `nav.markets` = "Markets"), reached at hash `#tab/market` with no trailing id. It covers the full filter/sort/status/risk/category UI, the two list-fetch endpoints, jurisdiction hiding, pagination ("load more"), and every field of the market **card** rendered by `render_market_card`.

All list state lives in module-level JS variables (app.js 1166–1171):

| Variable | Default | Meaning | Sent as request field |
|---|---|---|---|
| `current_market_filter` | `-1` | Status tab (-1 all / 1 active / 3 resolved / 2 closed) | `status` |
| `current_show_risky` | `0` | "Show risky markets" checkbox state (0/1) | `show_risky` |
| `current_category` | `''` | Selected category chip id | `category` (only if non-empty) |
| `current_subcategory` | `''` | Subcategory dropdown value | `subcategory` (only if non-empty) |
| `current_tag` | `''` | Active hot-tag chip | `tag` (only if non-empty) |
| `current_sort` | `'newest'` | Sort selector value | `sort` (only if not `newest`) |

Note: the user's **country** is `user.jurisdiction` (set via profile preferences, uppercased ISO code). It is applied **client-side** in `load_markets_list` — the list endpoints do NOT receive jurisdiction as a param; the client hides banned markets after fetch (see below).

---

### Screen layout (`select_tab('market')`, app.js 1017–1051)

When `#tab/market` has no `path[2]` (no market id / not `about` / not `oracle`), the list view is built:

1. **Logo header** (clickable, returns to markets), then `<hr>`.
2. **Status filter tabs** (`.market-filters` row): four buttons and one checkbox:
   - `filter.all` "All" — `data-status="-1"` (starts `active`)
   - `filter.active` "Active" — `data-status="1"`
   - `filter.resolved` "Resolved" — `data-status="3"`
   - `filter.closed` "Closed" — `data-status="2"`
   - Checkbox `.show-risky-toggle` with label `filter.show_risky` "Show risky markets".
3. **Category filter container** (`.category-filter-container`) = output of `render_category_filter()` (see below).
4. **Markets list** (`.markets-list`) — initially shows `filter.loading` "Loading…", then filled by `load_markets_list(-1, 0)` on first render.

#### Category filter block (`render_category_filter`, app.js 94–139)

Rendered from `categories_cache` (populated by `load_categories()` on startup → `load-categories` endpoint). Contents:

- **Category chips** (`.category-chips`): an "All categories" chip (`filter.all_categories`, `data-cat=""`) plus one chip per category showing `icon + localized label + (count)`. Active chip gets class `active` when `current_category` matches.
- **Subcategory dropdown** (`.subcategory-select`): only shown when a category is selected AND it has subcategories. First option `filter.all_subcategories` "All subcategories"; each option shows localized subcategory label + `(count)`.
- **Hot tags** (`.tag-chips`): label `filter.hot_tags` "Tags:" followed by one `.tag-chip` per hot tag showing `tag (count)`. Active tag toggled.
- **Sort selector** (`.sort-select`): label `filter.sort_label` "Sort:" with three options — `filter.sort_newest` "Newest" (value `newest`), `filter.sort_volume` "Volume" (value `volume`), `filter.sort_expiration` "Ending soon" (value `expiration`).

**Filter interactions** (delegated handlers):

| User action | Handler location | Effect |
|---|---|---|
| Click category chip | app.js 3204–3213 (`app_mouse`) | sets `current_category`, clears `current_subcategory` + `current_tag`, re-renders filter block, reloads list from offset 0 |
| Change subcategory dropdown | app.js 566–570 | sets `current_subcategory`, reloads list |
| Click hot-tag chip | app.js 3215–3222 | toggles `current_tag` (click again clears), re-renders filter, reloads |
| Change sort dropdown | app.js 571–575 | sets `current_sort`, reloads |
| Click status tab | app.js 3224–3230 | moves `active` class, reloads with new `status` |
| Toggle "show risky" | app.js 3232–3236 | reloads with current filter (checkbox re-read inside `load_markets_list`) |
| Click "Load more" | app.js 3238–3242 | reloads passing the last card's id as `last_market_id` (append mode) |
| Click a card (`.clickable-market`) | app.js 3192–3196 | navigates to `#tab/market/<id>` (market detail) |

---

### The market list fetch (`load_markets_list`, app.js 1173–1217)

Builds the request body, POSTs to **`load-markets-catalog`** (NOT `load-markets` — see note under that endpoint), then for each returned item:
- Skips it if `is_market_jurisdiction_banned(market.tags)` is true (client-side jurisdiction ban).
- Otherwise appends `render_market_card(market)`.

If `last_market_id==0` (fresh load): replaces `.markets-list` html; if empty result, shows `filter.no_markets` "No markets found". If `last_market_id>0` (load more): removes the old load-more button and **appends** the new cards.

**Pagination:** `limit` is hard-coded to **10**. If the returned array length ≥ limit, a `.load-more-btn` (`filter.load_more` "Load more") is appended carrying `data-last` = id of the last returned market. On error: `.markets-list` shows `filter.load_error` "Error loading markets" (non-ok) or `common.error_prefix + err.message` (exception).

#### Jurisdiction handling (`is_market_jurisdiction_banned`, app.js 2997–3004)

Returns false if the user has no `jurisdiction` or the market has no `tags`. Otherwise it builds the ban tag `jurisdiction-ban:<COUNTRY>` (user's jurisdiction uppercased) and returns true if that exact tag is present in `market.tags`. Banned markets are silently omitted from the list. (On the market **detail** screen, app.js 1563, a banned market instead triggers `show_jurisdiction_warning` — a modal with `jurisdiction.warning` text, `jurisdiction.proceed` "I understand, proceed" and `jurisdiction.go_back` "Go back".) The market-creation form collects `jurisdiction_relevant` and `jurisdiction_banned` free-text inputs (app.js 873–874) which become these tags server-side.

---

#### `load-markets-catalog`

- **Method:** POST — `/api/load-markets-catalog/`
- **Fires when:** every markets-list load/refresh — initial render, any filter/sort/status/risky change, and "Load more". This is the endpoint the browsing screen actually calls.
- **Role/auth:** none (public).

**Request fields** (api.php 213–243):

| Field | Type | Meaning |
|---|---|---|
| `last_market_id` | int | Pagination cursor; adds `AND id < last_market_id`. `0`/absent = first page. |
| `limit` | int | Page size, clamped 1–100 (client sends 10). |
| `status` | int | `-1` all (default → status ≥ 1, excludes pending); `0` pending, `1` active, `2` closed, `3` resolved. |
| `category` | string | Exact category slug filter (omitted when empty). |
| `subcategory` | string | Exact subcategory slug filter. |
| `tag` | string | Tag filter via INNER JOIN on `market_tags`. |
| `sort` | string | `newest` (default, `id DESC`), `volume` (`bets_sum DESC`), `expiration` (`betting_expiration ASC`). |

Note: `show_risky` IS put in the body by the client but this lightweight endpoint does NOT read/apply it (risk filtering only exists in `load-markets`). Multi-outcome markets are fully priced here.

**Response:** a JSON **array** of market rows. Fields consumed by `render_market_card`:

| Field | Type | Card usage |
|---|---|---|
| `id` | int | Card `rel` (click → detail); last id used as `data-last` for load-more. |
| `status` | int | Status badge: 1 active / 2 closed / 3 resolved / else failed; label from `status.market_<n>` ("Active"/"Closed"/"Resolved"/"Awaiting oracle"). |
| `q` | string | Market question, shown as `.title` (html-escaped). |
| `a`, `b` | string | Binary outcome A/B labels (answer lines). |
| `category`, `subcategory` | string | (returned; not directly drawn on card). |
| `metadata` | string/json | Multi-language title/labels source (not parsed by card). |
| `reserve_a`, `reserve_b`, `k` | int | Binary AMM reserves → `calculate_bet` + `implied_probability` for price/odds/probability lines. |
| `liquidity_sum` | int | Binary "Liquidity: X Ƶ" line (`market.liquidity_label`, divided by `price_precision_ratio`). |
| `a_bets_sum`, `b_bets_sum` | int | Binary bet-distribution bar percentages (`.bets-fill-a`/`-b`). |
| `bets_sum` | int | (volume sort key server-side). |
| `betting_expiration` | unixtime | "Betting until:" datetime (`market_detail.betting_until`). |
| `result_expiration` | unixtime | "Result deadline:" datetime (`market_detail.result_deadline`). |
| `market_type` | int | 0 = binary, 1 = multi (Onix Multi badge + multi bar). |
| `outcome_count` | int | Multi badge text `market.type_multi_badge` "Onix Multi (N outcomes)". |
| `lmsr_b`, `lmsr_subsidy` | int | Multi depth line `market.depth_line` "Depth (b): … | Subsidy: … Ƶ". |
| `outcomes[]` | array | Multi only: each `{outcome_index,label,q,bets_sum,bets_count,price}`. `price` (0–10000) → `%` shown per outcome, colored left border. |
| `payout_status` | int | With status 3, `1` = awaiting payout → grace-period countdown badge. |
| `update` | unixtime | Grace-period countdown base (`update + dispute_grace_hours`). |
| `time` | unixtime | Creation time (returned; not drawn). |
| `tags[]` | array | Used by `is_market_jurisdiction_banned` to hide market. |

**UI states/errors:** empty array → "No markets found"; array length ≥ limit → "Load more" button; grace countdown badge (`market.payout_in` "Payout in …") for resolved-awaiting-payout markets.

> **VIZ thin-client note (suggestion):** The catalog is a paginated, filterable query over on-chain market objects. A thin client would query the node's market index by `status`, `category`, `subcategory`, `tag`, ordered by newest/volume/expiration, page by `id` cursor. Risk/oracle enrichment is not needed for the list; the client should carry `tags` so it can locally hide `jurisdiction-ban:<country>` markets for the logged-in user.

---

#### `load-markets`

- **Method:** POST — `/api/load-markets/`
- **Fires when:** *(legacy/enriched variant — the current browsing screen calls `load-markets-catalog` instead; documented here because it is the risk-and-oracle-enriched list contract and shares the card fields.)*
- **Role/auth:** none (public).

**Request fields** (api.php 124–157):

| Field | Type | Meaning |
|---|---|---|
| `last_market_id` | int | Pagination cursor (`AND id < last_market_id`). |
| `limit` | int | Page size, clamped to max 100. |
| `status` | int | Same semantics as catalog (-1 all→≥1; 0/1/2/3 exact). |
| `category` | string | Sanitized `[a-z0-9_-]`, lowercased. |
| `subcategory` | string | Sanitized `[a-z0-9_-]`, lowercased. |
| `tag` | string | Sanitized `[a-z0-9_:-]`; INNER JOIN on `market_tags`. |
| `show_risky` | int | 0/1. When 0, active markets whose `risk_score < min_risk_score_listing` (default 2.5) are dropped from the list. |

**Response:** array of enriched market rows. In addition to all catalog fields above, this endpoint adds the fields the card uses for creator/oracle meta and risk badges:

| Field | Type | Card usage |
|---|---|---|
| `creator_data` | object | `{username,first_name,…}` → creator name in `market.meta_creator_oracle` "Creator: … · Oracle: …". |
| `account` | string | Oracle account name (fallback `#<oracle>`). |
| `oracle` | int | Oracle id (fallback name). |
| `oracle_reliability_score` | int (0–100) | Feeds `render_reliability_badge`. |
| `oracle_is_new` | int (0/1) | "New" badge variant. |
| `risk_score` | float | If present: `< min_risk_score_betting` (1.5) → red `market.risk_high` "⚠ High risk (S)"; `< min_risk_score_listing` (2.5) → amber `market.risk_warning` "⚠ Risk (S)". |
| `risk_score_low` | int (0/1) | Server flag driving the `show_risky` filter. |

**UI states/errors:** same list-level states as catalog; risky active markets suppressed unless `show_risky=1`.

> **VIZ thin-client note (suggestion):** This is the catalog plus per-oracle reliability + coverage (risk) enrichment. A thin client would additionally read each market's oracle account, its reliability metrics and insurance-vs-open-bets coverage from the node, compute the badge locally, and gate "risky" markets behind the user's `show_risky` preference.

---

#### `load-categories`

- **Method:** POST — `/api/load-categories/`
- **Fires when:** app startup (`load_categories()`, app.js 88–92); result cached in `categories_cache` and used by `render_category_filter()` and the create-market category dropdown.
- **Role/auth:** none.
- **Request:** empty body `{}`.

**Response** (object, api.php 387–413):

| Field | Type | UI usage |
|---|---|---|
| `categories[]` | array | Each `{id, icon, i18n, count, subcategories[], tags[]}`. `id`→chip `data-cat`; `icon`+localized `i18n`→chip label; `count`→`(n)` on chip. |
| `categories[].subcategories[]` | array | Each `{id, i18n, count}` → subcategory dropdown options. |
| `hot_tags[]` | array | Each `{tag, count}` (top 20 active tags, excluding `jurisdiction%`) → hot-tag chips. |

> **VIZ thin-client note (suggestion):** Categories are a fixed taxonomy (`get_market_categories`) plus live counts. A thin client can hold the taxonomy statically and derive per-category/subcategory/tag counts by aggregating active markets from the node.

---

#### `load-oracles`

- **Method:** GET — `/api/load-oracles/`
- **Fires when:** opening the **create-market** form (`select_tab('create')`, app.js 897–918) to populate the oracle `<select>`. (Not part of the browsing list, but drives oracle labels/reliability used in the create flow.)
- **Role/auth:** none to fetch (creating requires `user.create_market==1`).
- **Request:** none.

**Response** (array, api.php 437–443): each `{id, fee, name, rules, insurance, reliability_score, is_new}`. The form option shows `name [Fee X%, resolution: <reliability_score>]` with `value=id`, `rel=fee`. On non-ok → `market.oracle_load_error` warning toast.

> **VIZ thin-client note (suggestion):** List accounts flagged as oracles from the node with their fee, insurance and computed reliability score for selection when creating a market.

---

#### `load-committees`

- **Method:** GET — `/api/load-committees/`
- **Fires when:** opening the **create-market** form (app.js 921–935) to populate the dispute-committee `<select>`.
- **Role/auth:** none to fetch.
- **Request:** none.

**Response** (array, api.php 500–515): each `{id, name}` (name resolved from account, else first/last name, else `@username`, else `User #id`). Rendered as options `name [#id]`, first option `market.committee_not_selected`.

> **VIZ thin-client note (suggestion):** List accounts flagged as committees for optional dispute-resolution assignment at market creation.

---

### Reliability badge (`render_reliability_badge` / `reliability_score_class` / `reliability_score_label`, app.js 595–614)

Used inline on the card's creator/oracle meta line and on the market-detail oracle line. Given a numeric `score` (0–100) and `is_new` flag it renders `<span class="reliability-badge <class>">score — <label></span>`:

| Condition | CSS class | Label (i18n) |
|---|---|---|
| `is_new` | `reliability-new` | `oracle.score_new` "New" |
| `score ≥ 80` | `reliability-excellent` | `oracle.score_excellent` "Excellent" |
| `score ≥ 60` | `reliability-good` | `oracle.score_good` "Good" |
| `score ≥ 40` | `reliability-average` | `oracle.score_average` "Average" |
| `score ≥ 20` | `reliability-poor` | `oracle.score_poor` "Poor" |
| else | `reliability-unreliable` | `oracle.score_unreliable` "Unreliable" |

---

### Boost auto-close banner (card, app.js 1246–1248)

If `boost_active_loaded` and the market id is in `boost_active_market_ids` (populated from a cached leverage-info call), the card shows a `.boost-banner` with `boost.banner_text` "⚠️ Your boost auto-closes in %%TIME%%" and a live countdown. This is per-user leveraged-position state, not part of the list endpoints.

> **VIZ thin-client note (suggestion):** Auto-close timers derive from the user's open leveraged positions vs. each market's betting expiration; the client can compute the banner locally from position + market data queried per user.
## 4. Market detail & binary betting (bet, commit-reveal, cancel, transfer)

This section documents the single-market detail screen and the full binary-betting lifecycle (place, batch-queue, hidden commit-reveal, cancel, transfer). Everything is rendered client-side by `load_market_detail(market_id, jurisdiction_confirmed)` (app.js 1555–2041) from one enriched fetch (`load-market-enriched`). Amounts are shown/entered in VIZ (`Ƶ`) but the wire format is **milli-VIZ integers** (VIZ × 1000). Token/weight values, fee per-mille, and reserves are also integers. Fee/penalty "precision-6" values are divided by `price_precision_ratio` (10000) or `/10000` for display.

> Note: this screen also renders Boost/leverage, add-liquidity, dispute, oracle-resolve and public liquidity/payout tables. Those are covered in their own sections; here we describe only the market header, the bet form, the "My bets" table, and the bet/cancel/transfer endpoints.

---

### Screen: Market detail

Route: hash `#/market/<market_id>` (the code re-derives `market_id` from `document.location.hash.split('/')[2]` after actions). On entry, if the user is authenticated the client first calls `pm_process_pending_reveals()` to re-send any commit-reveal reveals left pending in `localStorage` (resilience — see commit-reveal below), then fetches `load-market-enriched`.

**Jurisdiction gate.** After the fetch, if `!jurisdiction_confirmed && is_market_jurisdiction_banned(m.tags)` the client aborts the render and calls `show_jurisdiction_warning(market_id)` instead, showing `jurisdiction.warning` ("This market is restricted in your jurisdiction …") with buttons `jurisdiction.proceed` ("I understand, proceed" → re-enters `load_market_detail` with `jurisdiction_confirmed=true`) and `jurisdiction.go_back` ("Go back"). The banned check is driven by `m.tags` (array of tag strings from the enriched response).

The detail body (built into `.market-detail`) is composed top-to-bottom of:

1. **Question & status** — `m.q` as title; a status badge `status.market_<status>` (0 Awaiting oracle / 1 Active / 2 Closed / 3 Resolved; a 4th "failed" style is used for anything else). If `m.payout_status>0` a second badge `status.payouts_label` + `payout_status_labels[m.payout_status]`.
2. **Grace countdown** — only when `status==3 && payout_status==1 && m.grace_period_end` and the end is in the future: badge `status.grace_countdown` ("Payout in <time> (if no dispute)") with a live `#grace-countdown` element driven by `start_grace_countdown()` / `format_countdown()` (h:mm:ss; flips to `market.payout_available` "Payout available" at 0).
3. **Boost auto-close banner** — if `data.my_leveraged_positions` has an open position (`status===0 && countdown_seconds>0`): `boost.banner_text`.
4. **Odds / probabilities bar** — binary only (multi handled separately): a two-tone `.bets-bar` split by `a_bets_sum` vs `b_bets_sum` percentages, then two `.answer-line` rows showing side name (`m.a`, `m.b`), implied probability via `implied_probability(reserve_a, reserve_b)` = `{a: reserve_b/(ra+rb)*100, b: reserve_a/(ra+rb)*100}`, and `market_detail.price_odds` ("(Price: <price> Ƶ, Odds: <odds>)") where price comes from `calculate_bet(side, 1000, reserve_a, reserve_b, k)[0]` (CPMM probe of a 1-VIZ trade) and odds = `1/price`.
5. **Liquidity line** — `market.liquidity_label` ("Liquidity: <amount> Ƶ") from `m.liquidity_sum`.
6. **Resolution result** — when `status==3 && resolved_outcome>=0`: `resolution.result_label` + winning side name; optional `m.decision` as sub-note.
7. **Settings/time block** — `market_detail.created` (`m.time`), `market_detail.betting_until` (`m.betting_expiration`), `market_detail.result_deadline` (`m.result_expiration`), all rendered as localized datetimes; then `resolution.resolution_label` + `m.resolution` (the resolution rules text).
8. **Creator** — `market_detail.creator` + `@username`/`first_name` from `data.creator.data`.
9. **Oracle + reliability** — `market.oracle_name_label` with a clickable `.oracle-profile-link` (`data.oracle.account`), a reliability badge from `render_reliability_badge(data.oracle.reliability_score, data.oracle.is_new)`, and `market_detail.oracle_fee_display` ("(fee: X% from losing …)") using `data.oracle.oracle_fee/price_precision_ratio` plus optional fixed-fee suffix from `m.oracle_fixed_fee`. Hints: `market_detail.oracle_new_hint` if new, else `market_detail.oracle_low_hint` if score<40. `data.oracle.oracle_rules` shown via `market_detail.oracle_rules` if present.
10. **Key parameters block** (`market_detail.params_title`) — LP fee (`m.liquidity_fee`), oracle fee (`m.oracle_fee`), optional fixed fee, creator fee (`m.creator_fee`), late-bet penalty (`m.time_penalty_value` / `m.time_penalty_type` / `m.penalty_curve_type`, capped by `max_time_penalty` setting), `market_detail.early_resolution` (`m.allow_early_resolution`), `market_detail.cancel_possible`/`cancel_impossible` (`m.allow_cancellation`), an `market_detail.instant_disabled` warning when `m.allow_instant_bet==0`, committee id (`m.committee`), and total bets (`m.bets_sum`).

#### `load-market-enriched` — the single data source for this screen

- **Method:** POST `/api/load-market-enriched/`
- **Fires:** on every open/refresh of the market detail screen (and again ~1–1.2s after any bet/cancel/transfer via `setTimeout(load_market_detail,…)`).
- **Auth/role:** none required to view; user-specific blocks (`my_bets`, `my_liquidity`, `my_leveraged_positions`) only populate when authenticated.

**Request**

| field | type | meaning |
|---|---|---|
| `market_id` | int | market to load |

**Response** (top-level keys the UI consumes)

| field | type | UI use |
|---|---|---|
| `market` (`m`) | object | all header/param fields (see below) |
| `oracle` | object | `id`, `account`, `oracle_fee`, `oracle_fixed_fee`, `oracle_rules`, `oracle_insurance`, `reliability_score`, `is_new` → oracle block + reliability badge |
| `creator` | object | `{id, data:{username?,first_name?}}` → creator line |
| `my_bets` | array | drives "My bets" table (auth only) |
| `my_liquidity` | array | "My liquidity" cards (auth only) |
| `my_leveraged_positions` | array | boost auto-close banner (`status`, `countdown_seconds`) |
| `all_bets` | array | public bets table (`user`,`user_data`,`side`,`outcome_index`,`amount`,`weight`,`time_penalty`,`status`,`resolved_amount`) |
| `all_liquidity` | array | public LP table |
| `all_payouts` | array | public payouts table (`user`,`type`,`amount`,`status`) |
| `dispute` | object/null | dispute card |
| `log` | array | market log (last 50): `time`,`action`,`details` |

**`market` (`m`) fields the detail/bet UI reads** (grounded in the render):

| field | type | UI use |
|---|---|---|
| `id`,`q` | int/str | market id, question title |
| `status` | int | 0/1/2/3 badge; gates bet form (`==1`) |
| `payout_status`,`grace_period_end` | int | payout badge + grace countdown |
| `market_type` | int | 0 = binary (this section), 1 = multi |
| `a`,`b` | str | binary side labels |
| `a_bets_sum`,`b_bets_sum` | int | probability bar + parimutuel preview |
| `a_weight_sum`,`b_weight_sum` | int | winning-weight totals for parimutuel payout estimate |
| `reserve_a`,`reserve_b`,`k` | int | CPMM price/odds, live preview, cancel preview |
| `liquidity_sum` | int | liquidity line |
| `bets_sum` | int | total bets |
| `resolved_outcome`,`decision` | int/str | resolution result card |
| `time`,`betting_expiration`,`result_expiration` | int (unix) | dates; `betting_expiration` gates the whole bet form & action buttons |
| `resolution` | str | resolution rules text |
| `oracle_fee`,`liquidity_fee`,`creator_fee`,`oracle_fixed_fee` | int (per-mille / precision-6) | params + `data-fee-permille` (sum of oracle+creator+liquidity) |
| `time_penalty_value`,`time_penalty_type`,`penalty_curve_type` | int | late-bet penalty description |
| `allow_early_resolution`,`allow_cancellation` | int | param flags |
| `allow_instant_bet` | int/null | `null`→treat as 1 (legacy); `0`→force batch mode, warning shown |
| `allow_batch` | int/null | `null`→treat as 1; enables batch/hidden mode checkboxes |
| `committee` | int | committee id line |
| `risk_score`,`risk_score_betting_blocked` | float/int | drives risk warning + mandatory confirm checkbox |
| `tags` | array | jurisdiction ban check |
| `outcomes` | array | multi only (`label`,`q`,`bets_sum`,`weight_sum`,`price`); ignored in binary |

> The legacy `load-market` endpoint (POST `/api/load-market/`, request `{market_id}`) returns the same shape and is a drop-in fallback; enriched adds oracle reliability metrics, risk score, tags, and `my_leveraged_positions`. For binary it also computes `a_weight_sum`/`b_weight_sum` (SUM of `weight` over active `status=0` bets per side) that the client needs for parimutuel estimates.

**VIZ thin-client note (suggestion):** Replace this aggregate with node queries — read the market object + AMM reserves + per-side active-bet weight sums from chain state, and the oracle account's reliability/insurance. `my_bets`/`my_leveraged_positions` become "positions owned by the signed-in account on this market." The public tables (`all_bets`, `all_liquidity`, `all_payouts`, `log`) map to iterating on-chain operation history for the market id.

---

### Screen section: Bet form (binary)

Rendered only when `status==1 && auth && time_now() < betting_expiration`. The `.bet-form` element carries data-attributes the live preview reads without another request: `data-reserve-a/-b`, `data-k`, `data-risk-blocked`, `data-market-type`, `data-a-bets-sum`, `data-b-bets-sum`, `data-a-weight-sum`, `data-b-weight-sum`, `data-fee-permille`, `data-bets-sum`.

**Controls**
- **Risk warning + mandatory checkbox** — only if `risk_score_betting_blocked==1`: shows `bet.risk_warning` ("⚠ Warning: oracle insurance fund is insufficient!") + `bet.risk_detail` (score vs `min_risk_score_betting`), and a required checkbox `bet.risk_confirm` ("I understand all risks and want to continue"). Submitting without it shows `bet.risk_must_confirm` and aborts client-side.
- **Side selector** — `<select name=bet_side>` with options `m.a` (value 0) and `m.b` (value 1). Title = `bet.place_title` ("Place bet").
- **Amount** — `<input name=bet_amount type=number step=0.001 min=0.001>` placeholder `bet.amount_placeholder` ("Amount VIZ").
- **Mode checkboxes** (only if binary and `allow_batch==1`):
  - `bet.mode_batch` ("Protect from front-running (batch)"), hint `bet.mode_batch_hint`. If `allow_instant_bet==0` this checkbox is force-checked+disabled and an `bet.instant_disabled_notice` note appears (instant is disabled → every order must be batched/committed).
  - `bet.mode_hidden` ("Hide my bet (commit-reveal)"), hint `bet.mode_hidden_hint`.
  - Default (neither checked) = instant.
- **Live preview** (`.bet-preview`) — recomputed on every amount/side change, no server call. For binary it shows:
  - Probability shift `prob_now% → prob_new%` (from `implied_probability` before/after the CPMM trade).
  - `bet.preview_est_payout` ("≈ **<payout> Ƶ** if <side> wins") — a **parimutuel** estimate via `parimutuel_payout(tokens, amt, win_bets, a_bets+b_bets, win_weight+tokens, fee_permille)`.
  - `bet.preview_share` ("your share: <tokens>") — CPMM tokens from `calculate_bet(side, amount, ra, rb, k)`.
  - `bet.preview_parimutuel_note` — floating-payout disclaimer.
  - `market_detail.price_odds` + `bet.preview_slippage` ("Slippage: X%") when slippage>0; if slippage>5% adds red `bet.preview_slippage_high` ("⚠ High slippage! Insufficient liquidity for this amount.").
- **Submit** — `.place-bet-action` button `bet.place_button`; status line `.bet-loading` shows `bet.sending`/`bet.queued_success`/`bet.placed_success`/`bet.commit_reveal_success` or `common.error_prefix + message`.

**Submit dispatch** (`place_bet_action`, app.js 2051): reads amount (VIZ string, sent as-is — server multiplies ×1000), side, `risk_confirm` (1 if the blocked checkbox is ticked). Then:
- **hidden** checked → `pm_commit_reveal(...)` (two endpoints, below); no `place-bet` call.
- **batch** checked → `place-bet` with `mode:1, min_tokens:0`; on success shows `bet.queued_success`.
- otherwise → `place-bet` with `mode:0`.
On success it calls `update_user_data()` and reloads the detail after ~1s.

#### `place-bet` — place / queue a binary bet

- **Method:** POST `/api/place-bet/`
- **Fires:** instant submit (`mode:0`) or batch submit (`mode:1`) from the bet form.
- **Auth/role:** authenticated user; sufficient `balance`.

**Request**

| field | type | meaning |
|---|---|---|
| `market_id` | int | target market |
| `side` | int | 0 = A, 1 = B (validated; else "Invalid side") |
| `amount` | number (VIZ) | bet size; server does `floor(amount*1000)` (must be >0) |
| `risk_confirm` | int (0/1) | required 1 when the market's risk score < `min_risk_score_betting`, else rejected |
| `mode` | int | 0 = instant CPMM (default), 1 = batch/uniform-price queue |
| `min_tokens` | int | optional slippage floor (tokens); batch passes it, instant checks `tokens_received>=min_tokens` |

**Response — instant (`mode:0`)**

| field | type | UI use |
|---|---|---|
| `status` | bool | success flag |
| `bet_id` | int | new bet id |
| `tokens` | int | shown in `bet.placed_success` ("Bet placed! Tokens: …") |
| `price` | int | per-token price (precision-6) — not displayed directly |
| `time_penalty` | int | late-bet penalty applied (precision-6) |

**Response — batch (`mode:1`)**

| field | type | UI use |
|---|---|---|
| `status`,`queued` | bool | success; `bet.queued_success` shown |
| `bet_id` | int | queued bet id (appears as status "Queued (batch)") |
| `epoch` | int | batch epoch it joined |
| `settle_after` | int (unix) | when the epoch settles |

**Errors surfaced** (as `common.error_prefix + message`): "Market not found", "Market is not active", "Betting period expired", "Insufficient balance", risk-score message ("Market risk score too low … Send risk_confirm=1 …"), "Batch betting is not enabled for this market", "Amount below minimum for batch betting", "Instant betting is disabled for this market — submit with mode=1 …", "Trade too small", "Tokens received (X) below minimum (Y)".

**VIZ thin-client note (suggestion):** an instant bet is a signed `place_bet`/AMM-buy operation (market_id, side, amount, optional min_tokens for slippage). Batch mode maps to submitting into a per-epoch batch/queue operation; the client would read the epoch length and `settle_after` from chain params.

---

### Screen section: Hidden bet (commit-reveal, binary only)

Chosen via the **`bet.mode_hidden`** checkbox. The client hashes the bet locally, escrows funds with `commit-bet`, then immediately calls `reveal-bet`; the bet settles in the next batch epoch. Reveals are persisted to `localStorage` (`pm_pending_reveals`) and retried on next screen load so a dropped reveal doesn't forfeit the escrow.

**Client hashing (`pm_commit_reveal`, app.js 2123):**
- `salt` = 16 random bytes hex (`pm_random_salt`).
- `preimage = market_id:user_id:side:-1:amount_milli:min_tokens:salt` (the `-1` is the binary outcome_index placeholder).
- `commitment = SHA-256(preimage)` hex (`pm_sha256hex`) — **must exactly match the server's canonical preimage**, which is the load-bearing contract:
  ```
  market:user:side:-1:amount:min_tokens:salt
  ```
- `no_reveal_fee_permille` = `get_setting('commit_no_reveal_penalty_permille', 200)`; the server rejects the commit unless this equals the current chain value.

Flow: show `bet.committing` → `commit-bet` → store pending reveal → show `bet.revealing` → `reveal-bet` → remove pending → show `bet.commit_reveal_success` ("Committed & revealed — settles at the next batch epoch."). On error: `common.error_prefix + message`.

**Resilient re-reveal (`pm_process_pending_reveals`, app.js 2114):** on every authenticated detail load, iterates stored `{commit_id, side, amount, min_tokens, salt, reveal_deadline}`; drops entries past `reveal_deadline`, otherwise re-calls `reveal-bet` and removes on success.

#### `commit-bet` — commit-reveal phase 1

- **Method:** POST `/api/commit-bet/`
- **Fires:** hidden-mode submit (first call).
- **Auth/role:** authenticated; balance ≥ escrow.

**Request**

| field | type | meaning |
|---|---|---|
| `market_id` | int | target market (binary only) |
| `commitment` | hex str (64 chars) | SHA-256 of the canonical preimage; server strips non-hex, requires length 64 |
| `escrow_amount` | number (VIZ) | escrowed funds (`floor×1000`), ≥ `min_batch_bet` |
| `no_reveal_fee_permille` | int | must equal chain `commit_no_reveal_penalty_permille` |

**Response**

| field | type | UI use |
|---|---|---|
| `status` | bool | success |
| `commit_id` | int | stored for the reveal + pending-reveal record |
| `reveal_deadline` | int (unix) | stored; pending reveals past it are dropped |
| `no_reveal_fee_permille` | int | echoed chain value |

**Errors:** "Invalid commitment hash", "Invalid escrow amount", "Market not found/not active", "Betting period expired", "Batch betting is not enabled for this market", "Commit-reveal supported for binary markets only", "Commit-reveal is disabled", "Escrow below minimum", "no_reveal_fee_permille must equal current chain value (X)", "Insufficient balance".

#### `reveal-bet` — commit-reveal phase 2

- **Method:** POST `/api/reveal-bet/`
- **Fires:** immediately after `commit-bet`, and on retry from `pm_process_pending_reveals`.
- **Auth/role:** authenticated; must own the commitment.

**Request**

| field | type | meaning |
|---|---|---|
| `commit_id` | int | commitment to reveal |
| `side` | int | 0/1 |
| `amount` | number (VIZ) | `floor×1000`; must be >0 and ≤ escrow (surplus refunded) |
| `min_tokens` | int | slippage floor carried into the batch bet |
| `salt` | str | the salt used in the preimage |

**Response**

| field | type | UI use |
|---|---|---|
| `status` | bool | success |
| `bet_id` | int | resulting queued bet |
| `epoch` | int | batch epoch |
| `settle_after` | int (unix) | epoch settlement time |

**Errors:** "Commitment not found", "Not your commitment", "Commitment already revealed or forfeited", "Reveal deadline passed", "Invalid side/amount", "Amount exceeds escrow", "Reveal does not match commitment", "Market no longer accepting bets".

**VIZ thin-client note (suggestion):** implement as a two-op flow — a `commit` op carrying the SHA-256 commitment + escrow, then a `reveal` op with `(side, amount, min_tokens, salt)`. Keep the exact preimage `market:user:side:-1:amount:min_tokens:salt` and persist the salt/deadline client-side for retry. The no-reveal penalty per-mille must be signed as the current consensus value.

---

### Screen section: "My bets" table

Rendered when `auth && data.my_bets.length>0`, heading `bet.my_bets` ("My bets"). Columns: `bet.outcome_header` (Outcome), `bet.amount_header` (Amount), `bet.tokens_header` (Tokens), `bet.status_header` (Status), `bet.payout_if_win_header` ("If win (≈)"), plus an actions column.

Per-row from each `my_bets` entry:
- **Outcome** — binary: `m.a`/`m.b` by `b.side`; multi: `outcomes[b.outcome_index].label`.
- **Amount** — `b.amount` (rendered VIZ).
- **Tokens** — `b.weight`.
- **Status** — indexed by `b.status`: `0`=`bet.status_active` (Active), `1`=`bet.status_canceled` (Canceled), `2`=`bet.status_refund` (Refund), `3`=`bet.status_resolved` (Resolved), `4`=`bet.status_transferred` (Transferred), `5`&`6`=`bet.status_queued` ("Queued (batch)") — status 5 = batch-queued, 6 = revealed-pending.
- **If win (≈)** — resolved (`status==3 && resolved_amount>0`): exact `b.resolved_amount` Ƶ. Active (`status==0`): `≈` parimutuel estimate from `parimutuel_estimate(m, side/outcome, b.weight, b.amount, false)`. Otherwise `—`.
- **Actions** — only when `b.status==0 && m.status==1 && time_now()<betting_expiration`: a **Cancel** button (`bet.cancel_button`; `.cancel-bet-action` for binary carrying `data-bet/-amount/-weight/-side/-reserve-a/-reserve-b/-k`) and a **Transfer** button (`bet.transfer_button`; `.transfer-position-action` carrying `data-bet/-weight/-amount`).

---

### Action: Cancel bet (binary)

`cancel_bet_action` (app.js 2147) builds a confirmation modal entirely client-side (no request yet) using reverse-CPMM math on the button's data-attributes:
- computes `amount_returned` by adding `weight` tokens back to the bet's reserve side and reading the counter-side delta.
- `diff = amount_returned - bet_amount`, `loss_pct = diff/bet_amount*100`.

**Modal** (`bet.cancel_confirm_title`, "Confirm bet cancellation"): a table with `bet.cancel_your_bet_label` (your bet Ƶ), `bet.cancel_return_label` (will be returned Ƶ), `bet.cancel_diff_label` (difference, colored loss/gain with %). If it's a loss beyond -2%, shows `bet.cancel_loss_explanation` ("⚠️ Warning: you will lose X% … Cancellation sells your position at the current market price … slippage."). Buttons: `bet.cancel_confirm_button` ("Confirm cancellation", `.cancel-confirm-yes` carrying `data-bet` and `data-min-return=amount_returned`) and `bet.cancel_button_label` ("Cancel", dismiss).

Confirm (`cancel_bet_confirmed`) calls `cancel-bet`, on success shows `bet.canceled_success` ("Bet canceled. Returned: … Ƶ"), calls `update_user_data()` and reloads the detail.

> Multi-outcome markets use `.cancel-bet-multi-action` → modal `bet.cancel_multi_title` → `cancel-bet-multi` `{bet_id}`; return is server-computed via LMSR (`bet.cancel_lmsr_note`). Documented here for completeness; the field-level contract below is for the binary `cancel-bet`.

#### `cancel-bet` — cancel an active binary bet

- **Method:** POST `/api/cancel-bet/`
- **Fires:** cancel confirmation.
- **Auth/role:** authenticated; must own the bet; market active and within betting window.

**Request**

| field | type | meaning |
|---|---|---|
| `bet_id` | int | bet to cancel |
| `min_return` | int (milli-VIZ) | slippage floor; server rejects if `amount_returned<min_return`. The client passes the previewed `amount_returned` as this floor. |

**Response**

| field | type | UI use |
|---|---|---|
| `status` | bool | success |
| `returned` | int (milli-VIZ) | shown in `bet.canceled_success` |

**Errors:** "Bet not found", "Not your bet", "Bet already closed", "Market not found/not active", "Betting period expired, cannot cancel", "Returned amount (X) below minimum (Y)".

**VIZ thin-client note (suggestion):** a signed `cancel_bet`/AMM-sell op with `bet_id` (or position handle) + `min_return` for slippage; the reverse-CPMM preview is pure client math from current reserves and can be reproduced from chain state.

---

### Action: Transfer position

`open_transfer_dialog` (app.js 2248) opens a modal `bet.transfer_title` ("Transfer position") showing the current position (`bet.cancel_multi_preview`: amount Ƶ + tokens) and three inputs:
- `transfer_to` — `bet.transfer_recipient` ("Recipient (username or ID)").
- `transfer_amount` — `bet.transfer_amount` ("Token amount (0 = all)"), number, default 0.
- `transfer_memo` — `bet.transfer_memo` ("Memo (optional)"), maxlength 255.
Buttons: `bet.transfer_confirm` (Transfer) and `bet.transfer_cancel` (Cancel).

`execute_transfer_position` (app.js 2268): requires `transfer_to` (else `bet.transfer_specify_recipient`), then calls `transfer-position` with `amount = Math.floor(transfer_amount*1000)` (tokens, milli units; 0 = all). On success shows `bet.transferred_success` ("Position transferred!"), `update_user_data()`, reloads detail.

#### `transfer-position` — transfer bet tokens to another user

- **Method:** POST `/api/transfer-position/`
- **Fires:** transfer modal confirm.
- **Auth/role:** authenticated; must own the bet; market status 1 (active) or 2 (closed).

**Request**

| field | type | meaning |
|---|---|---|
| `bet_id` | int | source position |
| `to_user` | int (user id) | recipient (server resolves; must exist, not self) |
| `amount` | int (tokens, milli) | tokens to transfer; `0` = all (`weight`) |
| `memo` | str (≤255) | optional memo (HTML-escaped server-side; sent to recipient notification) |

**Response**

| field | type | UI use |
|---|---|---|
| `status` | bool | success |
| `transfer_id` | int | recorded transfer id |
| `new_bet_id` | int | recipient's new bet record |
| `tokens_transferred` | int | tokens moved |
| `viz_equivalent` | number | proportional VIZ value of the moved tokens |

Server splits the position: recipient gets a new `status=0` bet with proportional `amount`; the source bet's weight/amount are reduced, or set to `status=4` (transferred) if fully moved. The client currently only checks `status` and shows a generic success — it doesn't render the returned ids/amounts.

**Errors:** "Bet not found", "You do not own this bet", "Bet is not active (status must be 0)", "Market not found", "Market must be active or closed for transfers", "Recipient not found", "Cannot transfer to yourself", "No tokens to transfer", "Insufficient tokens (have: X, requested: Y)".

**VIZ thin-client note (suggestion):** a signed `transfer_position` op `(bet_id/position_handle, to_account, tokens, memo)` that moves outcome tokens between accounts and splits the position proportionally on-chain; `to_user` on the thin client would be a VIZ account name rather than a numeric id.
## 5. Multi-outcome markets (create, bet, cancel, liquidity, resolve)

This section documents everything specific to **multi-outcome (categorical) markets** — internally `market_type=1`, branded **"Onix Multi"** — and contrasts it point-by-point with the binary ("Onix Binary", `market_type=0`) flows in section 4. A multi market has **N labelled outcomes** (`market.outcome_count`, 3–10 by default) instead of a fixed A/B pair. Pricing is done via LMSR (client mirrors the server via `market_math.js`), betting cost is LMSR-priced, but **settlement is parimutuel** (winners split losers' pool). All UI strings below are taken from `i18n/en.json`.

Key data-model differences the client relies on (returned inside the market-detail response `m`):

| Field | Meaning (multi) | Binary equivalent |
|---|---|---|
| `market_type` | `1` for multi | `0` |
| `outcome_count` | number of outcomes (N) | n/a |
| `outcomes[]` | array of `{outcome_index, label, price, q, bets_sum, weight_sum}` | n/a (uses `a`/`b`, `reserve_a/b`, `k`) |
| `lmsr_b` | LMSR depth parameter (milli-VIZ) | n/a |
| `lmsr_subsidy` | total LP subsidy (milli-VIZ) | n/a |
| `resolved_outcome` | winning `outcome_index` after resolution | `0`/`1` |
| bet `outcome_index` | which outcome the bet backs | bet `side` (0/1) |

`get_outcome_label(m, bet)` (app.js 749-758) maps a bet to its display label: for multi it looks up `m.outcomes[]` by `bet.outcome_index` (falling back to `market.outcome_label_fallback` = `"Outcome #%%IDX%%"`); for binary it returns `m.a`/`m.b` by `bet.side`.

---

### Screen: Create market — switching to Onix Multi

Reached from the **Create** nav tab (`nav.create`). The create form has a market-type dropdown (`select[name="market_type"]`, rendered app.js ~834):

- `option value="0"` → `market.type_binary` = **"Onix Binary (A/B)"** (default, selected)
- `option value="1"` → `market.type_multi` = **"Onix Multi (multiple outcomes)"**

Changing it fires `onchange="toggle_market_type(); render_lang_fields()"`.

**`toggle_market_type()`** (app.js 711-723):
- If multi (`mt==1`): hides `.binary-outcomes`, shows `.multi-outcomes`, hides every `.only-binary` control (binary-only options such as batch/instant-bet toggles), and calls `update_multi_odds_info()`.
- If binary: reverse.

**`render_lang_fields()`** (app.js 766-782): per language block it always renders title / description / resolution-rules inputs; **only in binary mode (`market_type==0`)** does it additionally render `outcome_a_<lang>` / `outcome_b_<lang>` inputs (placeholder `market.outcome_label_lang` = `"Outcome %%N%% (%%LANG%%)"`). In multi mode the A/B per-language inputs are omitted — outcome labels come from the flat `.multi-outcomes-list` inputs instead.

#### Outcome list editor (multi only)

The `.multi-outcomes` block contains `.multi-outcomes-list` (list of `input[name="outcome_<i>"]`), Add/Remove buttons, and a `.multi-info` note. Initial render seeds it (app.js 858-865) and shows `market.initial_odds_info` = `"Initial odds: 1/%%COUNT%% = %%PERCENT%%% per outcome"` for COUNT=3, 33.3%.

- **`add_outcome()`** (724-730) — button label `market.add_outcome` = **"➕ Add outcome"** (class `.add-outcome-btn`). Appends `input[name="outcome_<count>"]` with placeholder `market.outcome_n_placeholder` = `"Outcome %%N%%"`. If already at max (`lmsr_max_outcomes`, default **10**) it aborts with warning `market.max_outcomes` = `"Maximum %%MAX%% outcomes"`.
- **`remove_outcome()`** (731-738) — button label `market.remove_outcome` = **"➖ Remove"** (class `.remove-outcome-btn`). Removes the last input. If at min (`lmsr_min_outcomes`, default **3**) it aborts with warning `market.min_outcomes` = `"Minimum %%MIN%% outcomes"`.
- **`update_multi_odds_info()`** (739-748) — recomputes `.multi-info` to `1/COUNT = (100/COUNT)%` and disables the Add button at max / Remove button at min (via `.disabled` class).

`lmsr_min_outcomes` / `lmsr_max_outcomes` come from client `get_setting(...)`; the server re-validates them.

**`get_multi_outcomes()`** (759-765) collects all non-empty `.multi-outcomes-list input` values into a string array — this becomes the `outcomes` payload field.

#### Submit — `create_market()`

`create_market()` (app.js 1392+) reads `market_type` then validates shared fields. **Note (client quirk):** it still validates the hidden `a` and `b` inputs (1408-1423) even in multi mode; the multi template auto-populates a legacy hidden `q`/A/B so submission proceeds. The POST body is chosen by `market_type` (app.js 1484-1499): multi sends `outcomes: get_multi_outcomes()` and `market_type: 1` and omits `a`/`b` and `allow_instant_bet`.

VIZ thin-client note (suggestion): outcome labels are just an ordered string array; index positions (`outcome_index`) are 0-based and become the canonical outcome IDs used by every later call. A thin client should broadcast a single "create multi market" op carrying `q`, `resolution`, the ordered `outcomes[]`, liquidity/fee params and expirations, and read back the assigned `market_id` + `outcome_count`.

#### `create-market-multi`

- **Method:** POST `/api/create-market-multi/`
- **Fires when:** user submits the Create form with type = Onix Multi.
- **Auth/role required:** authenticated; `user_arr['create_market']==1`; must not be creator-banned.

**Request fields:**

| Field | Type | Meaning |
|---|---|---|
| `q` | string | Market question/title (legacy field, still required) |
| `outcomes` | string[] | Ordered outcome labels; server assigns `outcome_index` = array index. Count must be within `lmsr_min_outcomes`..`lmsr_max_outcomes`; each label trimmed & non-empty |
| `resolution` | string | Resolution rules text |
| `liquidity` | number (VIZ) | LP subsidy; ×1000 server-side; **min 100 VIZ** (100000 milli) |
| `liquidity_fee` | int (permille) | LP fee taken from losers |
| `oracle_id` | int | Oracle user id (must have `oracle=1`) |
| `oracle_fee` | int (permille) | Must be ≥ oracle's minimum |
| `oracle_fixed_fee` | int | Passed but server overrides from oracle record |
| `creator_fee` | int (permille) | Clamped 0..100 |
| `betting_expiration` | int (unix) | Clamped ≤ `result_expiration` |
| `result_expiration` | int (unix) | Result deadline |
| `allow_early_resolution` | 0/1 | |
| `allow_cancellation` | 0/1 | Enables `cancel-bet-multi` later |
| `allow_instant_bet` | 0/1 | **Ignored** — multi has no batch path, server forces `=1` (api.php 2787-2789) |
| `time_penalty_type` | int (0/1) | |
| `time_penalty_value` | int | |
| `penalty_curve_type` | int (0/1) | |
| `committee_id` | int | Optional dispute committee |

**Response fields:**

| Field | Type | UI use |
|---|---|---|
| `status` | bool | Success flag |
| `error` | string[] | Array of validation errors shown to user (e.g. "Minimum N outcomes required", "Insufficient balance (need X VIZ)", "Liquidity too low for N outcomes (min b=…)") |
| `market_id` | int | New market id (client navigates to detail) |
| `market_type` | int | `1` |
| `lmsr_b` | int | Computed depth parameter |
| `outcome_count` | int | N |

**Errors/edge states:** "No permission to create markets"; "Creator is banned…"; "Outcomes must be an array"; min/max outcome count; empty outcome label; "Minimum liquidity is 100 VIZ"; "Oracle not found"; "Oracle fee must be >= oracle minimum"; "Insufficient balance…"; "Liquidity too low for N outcomes (min b=…)"; self-oracle "Insufficient oracle insurance". **Contrast with binary `create-market`:** binary sends `a`/`b` labels (+ per-language A/B) and honours `allow_instant_bet`/batch; multi sends `outcomes[]`, ignores instant-bet, and additionally enforces the LMSR `min_b` floor per outcome count.

---

### Screen: Market detail — multi rendering (`load_market_detail`)

`load_market_detail(market_id)` (app.js 1555+) branches on `is_multi = parseInt(m.market_type)==1`.

**Header badge** (1262-1264): multi shows a purple badge `market.type_multi_badge` = `"Onix Multi (%%COUNT%% outcomes)"` (COUNT=`outcome_count`).

**Outcomes bar / detail list** (1266-1279 list card, 1592-1607 detail): iterates `m.outcomes[]`, each colored from a 10-color palette, showing:
- `answer-prob` = `(o.price/10000).toFixed(1)`% — the LMSR implied probability.
- outcome `o.label`.
- detail card adds `market_detail.multi_bets_detail` = `"(bets: %%AMOUNT%% Ƶ, q: %%Q%%)"` using `o.bets_sum` and `o.q/1000`.
- if resolved (`m.status==3` and `resolved_outcome==oi`) a ✅ winner badge is appended.
- a depth line: `market.depth_line_full` = `"Depth (b): %%DEPTH%% | Subsidy: %%SUBSIDY%% Ƶ | Liquidity: %%LIQUIDITY%% Ƶ"` from `lmsr_b`, `lmsr_subsidy`, `liquidity_sum`.

Binary instead renders the A/B bets-bar, per-side price/odds via `calculate_bet`, and `market.liquidity_label`.

**Resolution info** (1625-1636): if resolved, the winner name is `m.outcomes[resolved_outcome].label` for multi (vs `m.a`/`m.b` for binary), shown under `resolution.result_label` = "Result:".

The market params card, fees, penalties, creation/expiration timestamps (1638-1699) render identically for both types.

---

### Screen: Place a bet (multi bet form)

Rendered inside market detail when `m.status==1`, user authed, and before `betting_expiration` (app.js 1702-1739). The bet form `div.bet-form` carries multi state in data-attributes (1710): `data-market-type`, `data-lmsr-b`, `data-bets-sum`, and CSV `data-oc-q` / `data-oc-bets` / `data-oc-weight` (per-outcome q, bets_sum, weight_sum) so the client can price bets locally.

- Title `bet.place_title` = "Place bet".
- **Outcome selector** (1716-1721, multi): `select[name="bet_outcome"]` with one `option value="<oi>"` per outcome label. (Binary uses `select[name="bet_side"]` with A/B.)
- Amount input `input[name="bet_amount"]` (placeholder `bet.amount_placeholder` = "Amount VIZ").
- **No batch/hidden mode controls** for multi — the opt-in front-running toggles are rendered only for binary (`!is_multi`, 1726).
- Live preview `.bet-preview`.

**Live preview** (app.js 1949-1971, multi branch): on input change it LMSR-prices the amount client-side via `lmsr_tokens_for_amount(q, lmsr_b, outcome_i, amt)`, then estimates a parimutuel payout via `parimutuel_payout(...)`. It shows:
- `bet.preview_est_payout` = `"≈ <b>%%PAYOUT%% Ƶ</b> if %%SIDE%% wins"` (SIDE = selected outcome label).
- `bet.preview_share` = `"your share: %%TOKENS%%"` (LMSR tokens).
- `bet.preview_parimutuel_note` = `"≈ Estimate. Winners split the losers' pool in proportion to their share — your final payout depends on all bets placed by the time the market resolves."` — this is the **parimutuel disclaimer** shown for both types, emphasising the estimate is non-final.

**Submit** — `place_bet_action` (app.js 2054-2092). It reads `mtype = form.data('market-type')`; for multi (2069-2071) it reads `outcome_index` from `select[name=bet_outcome]` and calls `api_post('place-bet-multi', {market_id, outcome_index, amount, risk_confirm})`. (Binary branch instead reads `side` and may route to hidden/batch flows.) If a risk-score warning is shown, the user must tick `.risk-confirm-check` or the form aborts with `bet.risk_must_confirm`. On success the detail view reloads after ~1s.

#### `place-bet-multi`

- **Method:** POST `/api/place-bet-multi/`
- **Fires when:** user clicks "Place bet" (`bet.place_button`) on a multi market.
- **Auth/role required:** authenticated.

**Request fields:**

| Field | Type | Meaning |
|---|---|---|
| `market_id` | int | Target market (must be `market_type=1`, `status=1`) |
| `outcome_index` | int | Chosen outcome; must be `0 <= idx < outcome_count` |
| `amount` | number (VIZ) | Spend; ×1000 server-side; must be > 0 and ≤ balance |
| `min_tokens` | int | Slippage floor (tokens). Client currently does **not** send this (defaults 0). |
| `risk_confirm` | 0/1 | Sent by client when risk warning present (not consumed by this endpoint's core logic) |

**Response fields:**

| Field | Type | UI use |
|---|---|---|
| `status` | bool | success |
| `bet_id` | int | new bet id |
| `tokens` | int | LMSR tokens acquired (`weight`) |
| `cost` | int | actual VIZ cost (may be < `amount`) |
| `refund` | int | unspent amount (excess capped) |
| `prices` | number[] | updated per-outcome LMSR prices |

*(Client success handler shows `bet.canceled_success`-style success and reloads; note the success toast in the shared handler reads `data.returned` for cancels — for the bet it simply reloads the detail.)*

**Errors:** "Market not found"; "Not a multi-outcome market"; "Market is not active"; "Invalid outcome index"; "Amount must be positive"; "Insufficient balance"; "Instant betting is disabled for this market" (if `allow_instant_bet=0`, which blocks all multi betting since there is no batch path); "Bet amount too small for any tokens"; "Slippage exceeded: would get X tokens, min=…". **Contrast with binary `place-bet`:** binary takes `side` (0/1) and supports batch/hidden (commit-reveal) modes; multi takes `outcome_index`, is always instant, LMSR-prices the cost, and returns `tokens`/`cost`/`refund`/`prices`.

VIZ thin-client note (suggestion): the thin client would sign a "buy outcome tokens" op with `{market_id, outcome_index, amount, min_tokens}` and read back tokens/cost/refund and the new price vector to update the UI. `min_tokens` is the on-chain slippage guard — the current web client leaves it 0, but the node client should expose it.

---

### Action: Cancel a multi bet

In the **My bets** table (app.js 1757-1790), for an active bet on an active market before expiration, multi bets render a `.cancel-bet-multi-action` button (1781) carrying `data-bet`, `data-amount`, `data-weight`, `data-outcome`, `data-market`. (Binary uses `.cancel-bet-action` with reserve/k data.) Button label `bet.cancel_button` = "Cancel".

**`cancel_bet_multi_action(el)`** (app.js 2213-2229) opens a confirm modal:
- Title `bet.cancel_multi_title` = **"Confirm bet cancellation (Onix Multi)"**.
- `bet.cancel_multi_preview` = `"Bet: %%AMOUNT%% Ƶ, Tokens: %%TOKENS%%"`.
- `bet.cancel_lmsr_note` = **"Return is calculated by server via LMSR. The return amount may differ from the original bet."** — the LMSR-return disclaimer (distinct from the binary cancel note).
- Confirm button `bet.cancel_confirm_button` = "Confirm cancellation" (`.cancel-multi-confirm-yes`); dismiss `bet.cancel_button_label` = "Cancel" (`.cancel-multi-confirm-no`).

**`cancel_bet_multi_confirmed(el)`** (app.js 2231-2246) calls `api_post('cancel-bet-multi', {bet_id})` (note: **no `min_return` sent** by the client, defaults 0). On success shows `bet.canceled_success` = `"Bet canceled. Returned: %%AMOUNT%% Ƶ"` using `data.returned`, then reloads. **(Client quirk:** the endpoint returns `return_amount`, not `returned`, so the toast AMOUNT is currently blank — see below.)

#### `cancel-bet-multi`

- **Method:** POST `/api/cancel-bet-multi/`
- **Fires when:** user confirms cancellation of a multi bet.
- **Auth/role required:** authenticated; must own the bet.

**Request fields:**

| Field | Type | Meaning |
|---|---|---|
| `bet_id` | int | Bet to cancel (must be owner's, `status=0`) |
| `min_return` | int | Slippage floor on VIZ returned. Client does **not** send it (0). |

**Response fields:**

| Field | Type | UI use |
|---|---|---|
| `status` | bool | success |
| `return_amount` | int | VIZ returned (LMSR sell). *UI toast reads `data.returned` — mismatch, shows empty* |
| `original_amount` | int | original bet amount |
| `slippage` | int | `original_amount - return_amount` |

**Errors:** "Bet not found"; "Not your bet"; "Bet is not active"; "Market not found"; "Not a multi-outcome market"; "Market is not active"; "Cancellation not allowed for this market" (needs `allow_cancellation=1`); "Slippage exceeded: would get X, min=…". **Contrast with binary `cancel-bet`:** binary reverses the CPMM reserves and the client reads `data.returned`; multi does an LMSR sell (`lmsr_sell_return`) and returns `return_amount`/`slippage`. Cancellation returns can be **less than the original bet** (LMSR price impact) — hence the explicit note.

VIZ thin-client note (suggestion): a "sell/cancel outcome position" op keyed by `bet_id` with a `min_return` guard; read back the actual VIZ returned. The node client should send `min_return` and read the correct return field.

---

### Action: Add / Withdraw liquidity (multi)

**Client status:** the market-detail **Add liquidity** form (app.js 1747-1755) is rendered for any active market where the user has `add_liquidity` permission, regardless of type. Its handler **`add_liquidity_action`** (2290-2303) always calls the **binary** `add-liquidity` endpoint — it does **not** branch on `market_type`. There is **no** UI caller for `add-liquidity-multi` or `withdraw-liquidity-multi` in `app.js`. These two endpoints exist server-side and are the correct ones for multi markets, but the current web client never invokes them. **This is a known gap the VIZ thin client must close** (call the `-multi` variants when `market_type==1`).

Form labels: title `liquidity.add_title` = "Add liquidity", input placeholder `liquidity.amount_placeholder` = "Amount VIZ", button `liquidity.add_button` = "Add", success `liquidity.added_success` = "Liquidity added!".

#### `add-liquidity-multi`

- **Method:** POST `/api/add-liquidity-multi/`
- **Fires when:** (server-supported) LP adds subsidy to a multi market. *No current app.js caller.*
- **Auth/role required:** authenticated (server does not re-check `add_liquidity` in this branch, unlike the UI gate).

**Request fields:**

| Field | Type | Meaning |
|---|---|---|
| `market_id` | int | Target multi market (`market_type=1`, `status=1`) |
| `amount` | number (VIZ) | Subsidy added; ×1000; > 0 and ≤ balance |

**Response fields:**

| Field | Type | UI use |
|---|---|---|
| `status` | bool | success |
| `liquidity_id` | int | new LP record id (needed later for withdraw) |
| `delta_b` | int | increase in `lmsr_b` from this deposit |
| `new_b` | int | resulting `lmsr_b` |

**Errors:** "Market not found"; "Not a multi-outcome market"; "Market is not active"; "Amount must be positive"; "Insufficient balance". **Contrast with binary `add-liquidity`:** binary adds symmetric CPMM reserves; multi increases the LMSR `b` depth (`delta_b`) and `lmsr_subsidy`.

#### `withdraw-liquidity-multi`

- **Method:** POST `/api/withdraw-liquidity-multi/`
- **Fires when:** (server-supported) LP withdraws subsidy from a multi market. *No current app.js caller.*
- **Auth/role required:** authenticated; must own the liquidity record.

**Request fields:**

| Field | Type | Meaning |
|---|---|---|
| `liquidity_id` | int | LP record to withdraw (owner's, `status=0`) |

**Response fields:**

| Field | Type | UI use |
|---|---|---|
| `status` | bool | success |
| `return_amount` | int | principal + fee share returned |
| `principal` | int | original LP amount |
| `fee_share` | int | time-discounted fee share earned |

**Errors:** "Liquidity record not found"; "Not your liquidity"; "Liquidity is not active"; "Market not found"; "Not a multi-outcome market"; "Market is not active"; "Cannot withdraw: would drop b below minimum (X VIZ)" (LMSR `min_b` floor). **Contrast with binary withdraw:** the multi floor is on the LMSR `b` (removing `lmsr_b_share` must keep `b >= lmsr_min_b_parameter`), and early withdrawers get a time-discounted fee share.

VIZ thin-client note (suggestion): expose two ops keyed on `market_id`/`liquidity_id`. Add returns `liquidity_id` + `delta_b`/`new_b`; withdraw enforces the `min_b` floor and returns principal + fee_share. The node client must select the `-multi` variant based on `market_type` — the reference web client's shared button wrongly hits the binary path.

---

### Screen: Oracle resolution (multi)

The **Submit result** form (app.js 1911-1928) is shown when the authed user is the market's oracle, the market is active/closed (`status` 1 or 2) and not yet paid out (`payout_status==0`). The form `div.resolve-form` carries `data-market-type`.

- Title `resolution.submit_title` = "Submit result (oracle)".
- **Winning-outcome selector** `select[name="resolve_outcome"]`: for multi (1916-1919) one `option value="<oi>"` per `m.outcomes[oi].label`; binary shows A/B.
- Justification input `input[name="resolve_decision"]` (placeholder `resolution.justification` = "Justification").
- Confirm button `resolution.confirm_button` = "Confirm result".

**`resolve_market_action(el)`** (app.js 2755-2770): reads `mtype`, `outcome` (selected value), `decision`; picks `endpoint = mtype==1 ? 'resolve-market-multi' : 'resolve-market'`; calls `api_post(endpoint, {market_id, outcome, decision})`; on success shows `resolution.submitted_success` = "Result submitted!" and reloads.

**Client quirk (important for the node devs):** the client sends the winning index as **`outcome`**, but the server's `resolve-market-multi` reads it from **`winning_outcome`** (api.php 3155). As written these keys don't match — the thin client must send `winning_outcome` (and may also send `decision_url`).

#### `resolve-market-multi`

- **Method:** POST `/api/resolve-market-multi/`
- **Fires when:** oracle confirms the winning outcome.
- **Auth/role required:** authenticated; `market.oracle == user.id`; oracle not banned.

**Request fields:**

| Field | Type | Meaning |
|---|---|---|
| `market_id` | int | Multi market to resolve (`market_type=1`) |
| `winning_outcome` | int | Winning `outcome_index`; must be `0 <= x < outcome_count`. *(Web client mislabels this as `outcome`.)* |
| `decision` | string | Oracle justification |
| `decision_url` | string | Optional evidence URL (client does not send it) |

**Response fields:**

| Field | Type | UI use |
|---|---|---|
| `status` | bool | success → toast + reload |
| `winning_outcome` | int | echoes winner index |
| `losers_sum` | int | total staked on losing outcomes |
| `oracle_fee` | int | oracle fee taken from losers |
| `creator_fee` | int | creator fee taken from losers |
| `winners_pool` | int | net pool distributed to winners |

**Errors:** "Market not found"; "Not a multi-outcome market"; "You are not the oracle"; "Oracle is banned"; "Market cannot be resolved in current status"; "Invalid winning outcome"; "Betting period not expired and early resolution not allowed" (when `status=1`, `allow_early_resolution!=1`, before `betting_expiration`). **Contrast with binary `resolve-market`:** binary resolves on `side` 0/1; multi resolves on `winning_outcome` index. Settlement is **parimutuel** in both — losers' stake (minus oracle/creator/LP fees, plus forfeit pool) is split among winners in proportion to their **tokens (`weight`)**, minus each winner's time penalty; if no one bet the winning outcome, `winners_pool` flows to LPs. Resolution flips `market.status=3`, sets `resolved_outcome`, and queues bettor notifications (dispute grace window).

VIZ thin-client note (suggestion): the resolution op is signed by the oracle account and carries `{market_id, winning_outcome, decision, decision_url}`. Ensure the key is `winning_outcome` (the reference web UI sends `outcome`, which the multi endpoint ignores) and surface the returned `losers_sum` / fee / `winners_pool` figures for the resolution receipt.
## 6. Creating a market (creator role)

This section documents the **Create** tab: the full market-creation form, every field in the order it is rendered, the supporting picker/lookup calls (`load-oracles`, `load-oracle-profile`, `load-committees`), and the `create-market` / `create-market-multi` submission contracts. All field names below are taken verbatim from `app.js` (`select_tab` create branch, lines 814–942; `create_market`, lines 1392–1525; helpers 616–810) and `module/api.php` (lines 437–776, 2728–2896). UI label text is quoted from `i18n/en.json`.

---

### Screen: Create market (Create tab)

**Route:** `#tab/create` (rendered by `select_tab()` when `path[1]=='create'`). On entry the hash is normalised via `navigate_replace('#tab/'+name)`.

**Title:** `market.create_title` → **"Create prediction market"**.

**Access gating (rendered before any form):**
- If the user is **not authenticated** (`!auth`): only a paragraph `market.not_logged_in` → **"You are not logged in."** is shown. No form, no lookups fire.
- If authenticated but `user.create_market != 1`: only a paragraph `market.cannot_create` → **"You cannot create markets."** is shown. No form, no lookups fire.
- If authenticated **and** `user.create_market == 1`: the full form (`.create-market`) is rendered, then two GET lookups fire immediately (`load-oracles`, `load-committees`) to populate the oracle and committee dropdowns.

**Required role/auth:** authenticated session **with creator role** (`user.create_market == 1`). The server re-checks this (`create-market` throws `No permission to create markets` if `create_market != 1`) and additionally blocks banned creators (`creator_banned == 1` and either `creator_ban_until == 0` or still in the future → throws `Creator is banned from creating markets`). The banned state is **not** surfaced client-side before submit; it appears only as a thrown error on submit.

#### Form fields, in render order

All inputs live inside `.create-market`. Fields marked *(hidden)* are populated programmatically, not shown to the user.

| # | Field (name attr) | Control / i18n label | Meaning & behaviour |
|---|---|---|---|
| 1 | `market_type` | `<select>`; options `market.type_binary` → **"Onix Binary (A/B)"** (value `0`, default) and `market.type_multi` → **"Onix Multi (multiple outcomes)"** (value `1`) | `onchange="toggle_market_type();render_lang_fields()"`. Binary shows `.binary-outcomes` + `.only-binary`; multi shows `.multi-outcomes` and hides the instant-bet toggle. **Note:** the caption reuses `market.penalty_type_label` (a label-key mismatch in the prototype). |
| 2 | `category` | `<select>`, label `market.category_label` → **"Category"** | `onchange="render_subcategory_options()"`. Options built from `categories_cache.categories` (each rendered as `icon + localized label`, value = category `id`). First option is `—` (empty). |
| 3 | `subcategory` | `<select>`, label `market.subcategory_label` → **"Subcategory"** | Populated by `render_subcategory_options()` from the selected category's `subcategories[]` (value = subcategory `id`, label via `resolve_i18n`). First option `—`. |
| 4 | `tags` | `<input type=text>`, label `market.tags_label` → **"Tags"**, placeholder `market.tags_placeholder` → **"Add tags (comma separated)"** | Free text; server runs `validate_tags`. |
| 5 | `default_lang` | `<select>`, label `market.default_lang_label` → **"Default language"**; options `English` (`en`) / `Русский` (`ru`) | Chooses which language's title/outcome/rules become the legacy `q`/`a`/`b`/`resolution` fallback server-side. |
| 6 | *(per-language blocks)* `.lang-fields-container` | Rendered by `render_lang_fields()` | One `.lang-block` per language in `create_form_langs` (starts `['en']`). Each block has: `title_<lang>` (`market.title_label` → **"Title (LANG)"**), `description_<lang>` textarea (`market.description_label` → **"Description (LANG)"**), `resolution_rules_<lang>` textarea (`market.resolution_rules_label` → **"Resolution rules (LANG)"**), and — **only for binary** (`market_type==0`) — `outcome_a_<lang>` and `outcome_b_<lang>` (`market.outcome_label_lang` → **"Outcome A/B (LANG)"**). Each block after the first shows a `✕` remove button (`remove-lang-btn`, `data-lang`). |
| 7 | `add-lang-btn` | `<a>` `market.add_language` → **"➕ Add language"** | Calls `add_create_lang()` → adds next unused of `['en','ru']`, re-renders lang blocks. Cap is those two languages only. |
| 8 | `q` *(hidden)* | — | Legacy question. Rendered as hidden, empty. |
| 9 | `a`, `b` *(hidden)* | — | Legacy binary outcome labels. Hidden, empty. |
| 10 | `resolution` *(hidden)* | — | Legacy resolution text. Hidden, empty. |
| 11 | `.multi-outcomes` block (hidden unless multi) | — | Contains `.multi-outcomes-list` with N `<input name="outcome_<i>">` (`market.outcome_n_placeholder` → **"Outcome N"**), initially 3 (`outcome_0..2`). Buttons: `add-outcome-btn` (`market.add_outcome`) and `remove-outcome-btn` (`market.remove_outcome`, starts disabled). Note line `.multi-info` shows `market.initial_odds_info` → **"Initial odds: 1/COUNT = PERCENT% per outcome"**. |
| 12 | `liquidity` | `<input type=text>`, placeholder `market.liquidity_placeholder` → **"Initial liquidity (VIZ)"**, hint `market.liquidity_min_hint` → **"(minimum amount 100 VIZ)"** | Initial liquidity in VIZ. |
| 13 | `resolution_url` | `<input type=url>`, label `market.resolution_url_label` → **"Resolution URL"**, placeholder `https://...` | Optional evidence source URL. **Sent only implicitly** — see the payload note: `create_market()` does **not** include this key in its request body (it is defined in the server handler but not sent by the current client). |
| 14 | `jurisdiction_relevant` | `<input type=text>`, label `market.jurisdiction_relevant_label` → **"Relevant jurisdictions"**, placeholder `US, GB, DE...` | Country list. **Not sent** by `create_market()`. |
| 15 | `jurisdiction_banned` | `<input type=text>`, label `market.jurisdiction_banned_label` → **"Restricted jurisdictions"**, placeholder `CN, KP...` | Country list. **Not sent** by `create_market()`. |
| 16 | `oracle` | `<select>`, label `market.oracle_label` → **"Oracle:"**; first option value `0` = `market.oracle_not_selected` → **"Not selected"** | `onchange="select_oracle()"`. Options loaded via `load-oracles` (see below). Each option carries `rel="<fee>"`. |
| 17 | `liquidity_fee` *(hidden)* | shown as `market.lp_reward` → **"LP reward: 0.005%"** | Hidden input, hard-coded `value="5"` (‰). |
| 18 | `oracle_fee` *(hidden)* | shown as `market.oracle_reward` → **"Oracle reward:"** + `<span class="oracle-fee">` | Hidden input default `value="5"`. Updated by `select_oracle()` to the chosen oracle's minimum fee (`rel`), and the span shows `fee/price_precision_ratio + "%"`. |
| 19 | `creator_fee` | `<input type=number>` min 0 max 100 step 1, `value="5"`, placeholder `market.creator_fee_placeholder` → **"Creator reward (‰, 0-100)"**, hint `market.creator_fee_hint` → **"(creator receives X‰ from losing bets)"** | Creator reward in ‰ (0–100). |
| 20 | `oracle_fixed_fee` | `<input type=number>` step 0.001 min 0 `value="0"`, placeholder `market.oracle_fixed_fee_placeholder` → **"Fixed oracle reward (VIZ)"**, hint `market.oracle_fixed_fee_hint` → **"(one-time payment when market is accepted)"** | **Sent** in the payload, but the server **overrides** it with the oracle's own `oracle_fixed_fee` from the oracle profile — the client value is informational only. |
| 21 | `betting_expiration` | `<input type=datetime-local>`, label `market.betting_expiration_label` → **"Betting deadline:"**, hint `market.betting_expiration_hint` → **"(local time)"** | Deadline for accepting bets. Converted to a unix seconds integer on submit (`new Date(v).getTime()/1000|0`). |
| 22 | `result_expiration` | `<input type=datetime-local>`, label `market.result_expiration_label` → **"Result submission deadline:"**, hint `market.result_expiration_hint` → **"(local time)"** | Deadline for the oracle to submit a result. Converted to unix seconds on submit. |
| 23 | `timezone_offset` *(hidden)* | shown via `market.timezone_label` → **"Local timezone:"** + `.local-timezone` | Hidden input set to `timezone_offset()`. **Not sent** by `create_market()` (commented out both client- and server-side). |
| 24 | `allow_early_resolution` | checkbox, checked by default, label `market.early_resolution_label` → **"Early resolution"**, hint `market.early_resolution_hint` → **"(if result is known earlier)"** | Allows resolving before `result_expiration`. |
| 25 | `allow_cancellation` | checkbox, checked by default, label `market.allow_cancel_label` → **"Allow event cancellation"**, hint `market.allow_cancel_hint` | Allows cancel/refund on event postponement/cancellation. |
| 26 | `allow_instant_bet` | checkbox (`.only-binary`, hidden for multi), checked by default, label `market.allow_instant_bet_label` → **"Allow instant betting"**, hint `market.allow_instant_bet_hint` (binary only; uncheck forces batch/commit-reveal, anti-MEV) | Binary only. For multi the server forces this to `1` regardless. |
| 27 | `time_penalty_type` | `<select>`, label `market.penalty_type_label` → **"Late bet penalty type:"**; options `market.penalty_type_percent` → **"% of duration"** (value `1`, default) and `market.penalty_type_fixed` → **"Fixed seconds"** (value `0`) | Late-bet penalty mode. |
| 28 | `time_penalty_value` | `<input type=number>` `value="10"`, placeholder `market.penalty_value_placeholder` → **"Penalty value (% or sec.)"**, hint `market.penalty_value_hint` → **"(penalty quadratic/linear from 0% to X%)"** | Penalty magnitude. |
| 29 | `committee_id` | `<select>`, label `market.committee_label` → **"Dispute judge:"**; first option value `0` = `market.committee_not_selected` → **"Not selected"** | Dispute committee/DAO. Options loaded via `load-committees`. |
| 30 | `create-market-action` | `<a class="btn btn-success">` `market.create_button` → **"Create market"** | Submit button. Click → `create_market()` (button gets `disabled` while in flight). |
| — | `.loading` | `market.creating_wait` → **"Please wait…"** | Status line reused for validation errors, progress, and success. |
| — | `.info` | `market.create_info_note` → note about oracle non-acceptance refunding liquidity minus penalty | Static informational note. |

#### Multi-outcome outcome management (client helpers)

- **`add-outcome-btn`** → `add_outcome()`: appends `<input name="outcome_<count>">`. Blocked at `lmsr_max_outcomes` (setting, default 10) with warning `market.max_outcomes` → **"Maximum MAX outcomes"**.
- **`remove-outcome-btn`** → `remove_outcome()`: removes the last outcome input. Blocked at `lmsr_min_outcomes` (setting, default 3) with warning `market.min_outcomes` → **"Minimum MIN outcomes"**.
- `update_multi_odds_info()` recomputes the odds hint (`100/count`%) and disables the add/remove buttons at the caps.
- `get_multi_outcomes()` collects non-empty outcome input values into an array (sent as `outcomes`).

#### Client-side validation (in `create_market()`, before the request)

Each required field, when empty, focuses the field, re-enables the submit button, and writes an error into `.loading`:
- `q`, `a`, `b`, `liquidity`, `resolution`, `betting_expiration`, `result_expiration` empty → `form.fill_form` → **"Fill out the form"**.
- `oracle` still `0` → `form.select_oracle` → **"Select an oracle"**.

> **Prototype caveat (must be preserved or fixed by the new client):** the hidden `q`/`a`/`b`/`resolution` inputs are rendered **empty and never auto-populated** from the per-language title/outcome/rules fields in the code paths read. As written, `create_market()`'s empty-check on `q`/`a`/`b`/`resolution` will fail unless those hidden inputs are filled elsewhere. The new thin client should map: default-language title → `q`, outcome A/B labels → `a`/`b`, resolution rules → `resolution` (the server applies exactly this fallback from `title_<lang>` / `outcome_*_<lang>` / `resolution_rules_<lang>` when `q`/`a`/`b`/`resolution` are absent — see `create-market` lines 568–571). Prefer sending the localized fields and letting the server derive legacy fields.

> **Prototype caveat (multi routing):** `create_market()` **always POSTs to `/api/create-market/`**, adding `market_type:1` to the body for multi markets. The server's `create-market` handler does **not** branch on `market_type` and would treat the submission as binary (empty `a`/`b`). The dedicated `create-market-multi` endpoint (documented below) exists and is the correct target for multi markets, but the current client never calls it. **The new client should POST multi markets to `create-market-multi`.**

---

#### `load-oracles`

- **Method:** GET (`/api/load-oracles/`)
- **When:** immediately after the create form is rendered (creator role), to populate the `oracle` `<select>`.
- **Role/auth:** none enforced on this endpoint (public list of oracle users).
- **Request:** none.
- **Response:** JSON **array**, each element:

| Field | Type | UI use |
|---|---|---|
| `id` | int | `<option value>` |
| `fee` | int (‰×precision) | option `rel` attr + used by `select_oracle()` to prefill `oracle_fee`; displayed as `fee/price_precision_ratio + "%"` |
| `name` | string (account) | option text (escaped) |
| `rules` | string | (not shown in the option list) |
| `insurance` | int | (not shown in the option list) |
| `reliability_score` | int (0–100) | shown in option label; falls back to `50` if missing |
| `is_new` | 0/1 | (not shown in the option list) |

- **Option label format:** `name [oracle.fee_label <fee%>, oracle.rate_resolution: <reliability_score>]` → e.g. **"@alice [Fee: 0.5%, resolution rate: 72]"**.
- **Error state:** on non-OK response → `show_message('warning', market.oracle_load_error)` → **"Error: failed to load oracles"**; oracle select stays at "Not selected".

> **VIZ thin-client note (suggestion):** replace this with a chain/index query returning the set of registered oracle accounts and their advertised minimum fee + reliability metrics. No signing needed — read-only.

---

#### `load-oracle-profile`

- **Method:** POST (`api_post('load-oracle-profile', {oracle_id})`)
- **When:** rendered on the standalone oracle profile route (`#tab/market/oracle/<id>`) via `load_oracle_profile()`. (Not embedded in the create form itself, but it is the detail view a creator opens to vet an oracle before selecting it.)
- **Role/auth:** none enforced.
- **Request:**

| Field | Type | Meaning |
|---|---|---|
| `oracle_id` | int | Oracle user id to inspect |

- **Response (object):**

| Field | Type | UI use |
|---|---|---|
| `id` | int | fallback name `#id` |
| `name` | string | header (`oracle.profile_title`) |
| `rules` | string | shown as `oracle.rules_label` if present |
| `fee` | int | shown as `fee/price_precision_ratio %` (`oracle.fee_label`) |
| `fixed_fee` | int | if >0, appended as `+ <n> Ƶ` (`oracle.fee_fixed`) |
| `insurance` | int | `oracle.insurance_fund` amount |
| `banned` | 0/1 | if 1, shows `oracle.banned_label` |
| `ban_until` | int (unix) | if >0 → `oracle.banned_until` date, else `oracle.banned_forever` |
| `reliability_score` | int 0–100 | `oracle.reliability_label` `/100` |
| `is_new` | 0/1 | drives "new oracle" label + `oracle.hint_new` |
| `risk_score` | float (or ≥999 = "—") | financial coverage `×n` (`oracle.coverage`) |
| `trust_score` | int 0–100 | big composite score card |
| `metrics` | object | detailed stats table (see below) |
| `derived` | object | derived-rate table (see below) |

  `metrics` object fields (all int unless noted): `markets_accepted`, `markets_resolved`, `markets_no_contest`, `markets_missed`, `disputes_received`, `disputes_lost`, `disputes_won`, `disputes_auto_closed`, `dispute_responses_missed`, `total_volume_resolved`, `total_insurance_slashed`, `avg_resolution_time` (sec; shown as hours), `bans_received`, `active_since` (unix), `last_active_time` (unix).

  `derived` object fields (0..1 floats, shown as %): `resolution_rate`, `dispute_loss_rate`, `no_contest_rate`, `deadline_miss_rate`, `dispute_response_rate`.

- **Summary block** shows top signals: `oracle.resolved` (resolution_rate), `oracle.disputes_lost` (dispute_loss_rate), `oracle.volume` (total_volume_resolved), `oracle.coverage` (risk). Context hints: `oracle.hint_new`, `oracle.hint_underfunded` (risk<1 & reliability≥60), `oracle.hint_low_reliability` (reliability<40).
- **Error state:** on throw → `.market-detail` shows `oracle.load_error` + message.

> **VIZ thin-client note (suggestion):** oracle reputation is entirely derived from on-chain history (markets accepted/resolved/disputed, insurance slashed). The thin client can compute trust/risk from indexed chain events; only `oracle_id` is needed as input. Read-only.

---

#### `load-committees`

- **Method:** GET (`/api/load-committees/`)
- **When:** immediately after the create form renders, to populate the `committee_id` `<select>` (dispute judge).
- **Role/auth:** none enforced.
- **Request:** none.
- **Response:** JSON **array**, each element:

| Field | Type | UI use |
|---|---|---|
| `id` | int | `<option value>` |
| `name` | string | option text; server derives display name from account, else `first_name last_name`, else `@username`, else `User #id` |

- **Option label format:** `name [#id]`.
- **Error state:** if not OK, the committee select simply keeps only "Not selected" (no explicit warning is shown).

> **VIZ thin-client note (suggestion):** query the set of accounts flagged as dispute committees/DAOs. Read-only.

---

#### `create-market` (binary — Onix Binary)

- **Method:** POST (`/api/create-market/`)
- **When:** user clicks **"Create market"** (`create-market-action`) → `create_market()`, with `market_type == 0`.
- **Role/auth:** authenticated + creator role (`user.create_market == 1`); server also blocks banned creators.
- **Request body (exact keys sent for binary):**

| Field | Type | Meaning |
|---|---|---|
| `q` | string | Question (from hidden `q`; server falls back to `title_<default_lang>`/`title_en` if empty) |
| `a` | string | Outcome A label (fallback: `outcome_a_<lang>`) |
| `b` | string | Outcome B label (fallback: `outcome_b_<lang>`) |
| `resolution` | string | Resolution rules text (fallback: `resolution_rules_<lang>`) |
| `liquidity` | string/number | Initial liquidity in VIZ (server ×1000; **min 100 VIZ / 100000 units**) |
| `liquidity_fee` | int (‰) | LP reward, clamped ≥0 |
| `oracle_id` | int | Selected oracle user id |
| `oracle_fee` | int (‰×precision) | Must be ≥ oracle's own minimum, else error |
| `oracle_fixed_fee` | number | Sent but **overridden** server-side by oracle's profile value |
| `creator_fee` | int (‰) | Creator reward, clamped 0–100 |
| `betting_expiration` | int (unix) | Bet deadline |
| `result_expiration` | int (unix) | Result deadline (server clamps `betting_expiration` down if it exceeds this) |
| `allow_early_resolution` | 0/1 | Early resolution toggle |
| `allow_cancellation` | 0/1 | Allow cancellation toggle |
| `allow_instant_bet` | 0/1 | Instant vs batch/commit-reveal (binary only) |
| `time_penalty_type` | int (0 fixed / 1 percent) | Late-bet penalty mode |
| `time_penalty_value` | int | Penalty magnitude |
| `committee_id` | int | Dispute judge (0 = none; validated as a committee user if >0) |

  *Additional keys the server reads but the current client does not send:* `category`, `subcategory`, `tags`, `default_lang`, `title_en`/`title_ru`, `description_en`/`ru`, `resolution_rules_en`/`ru`, `resolution_url`, `image_url_en`/`ru`, `outcome_a_en`/`ru`, `outcome_b_en`/`ru`, `penalty_curve_type`. The new client should send the localized metadata fields (the form collects them) so the server can build `metadata` and derive legacy `q`/`a`/`b`/`resolution`.

- **Success response:**

| Field | Type | UI use |
|---|---|---|
| `status` | bool | `true` = success; `false` triggers the error path |
| `market_id` | int | On success: `.loading` shows `market.redirect_success` → **"Redirecting to the created market..."**, then `update_user_data()` + `navigate('#tab/market/'+market_id)` |
| `liquidity_id` | int | (informational) |
| `error` | array/string | Present on failure; if `status==0` but market created hidden, contains a "waiting for oracle" notice |

- **Server-side validation errors (returned in `error[]`, surfaced by the client as `common.error_prefix + err`):**
  - Liquidity < 100 VIZ → "Минимальная ликвидность для создания рынка 100 viz".
  - Oracle not found → "Оракул не найден".
  - `oracle_fee` below oracle minimum → "Комиссия оракулу не должна быть меньше…".
  - Insufficient balance (needs liquidity + market creation fee) → "Insufficient balance (need … VIZ: … liquidity + … creation fee)".
  - Committee id set but not a committee user → "Committee user not found".
  - `betting_expiration + 86400 > result_expiration` and early resolution off → must leave ≥24h between betting close and result deadline.
  - Self-oracle (creator == oracle) with insurance below `min_oracle_insurance` → "Insufficient oracle insurance (min: … VIZ)".
  - DB failures → "Рынок не создан…", "Ликвидность не была добавлена…", etc. (rolled back).

- **Status semantics:** newly created market status is `0` (**hidden, awaiting oracle acceptance**) unless the creator is also the oracle and passes the insurance check, in which case status `1` (accepted immediately). When status is `0` the server adds error note "Рынок создан и скрыт, ждем подтверждение от оракула" — but `status` is still `true`, so the client still redirects to the market.

- **Client error UI:** on any thrown error the submit button is re-enabled, `.loading` gets `negative-info` and shows `common.error_prefix + ' ' + err`.

> **VIZ thin-client note (suggestion):** market creation should become a signed `create_market` operation broadcast by the creator's key, carrying: question/outcome/resolution metadata (or an IPFS/content hash), oracle account, committee account, fee parameters (‰), expirations (unix), and the toggles. The initial-liquidity deposit + anti-spam creation fee should be a transfer/stake bundled in the same transaction (creation fee → DAO fund account). The node should reject if oracle_fee < oracle minimum, liquidity < min, or insurance insufficient for self-oracle — mirroring the checks above.

---

#### `create-market-multi` (Onix Multi — LMSR, multiple outcomes)

- **Method:** POST (`/api/create-market-multi/`)
- **When:** intended for `market_type == 1` submissions. **See prototype caveat above:** the current `create_market()` does not target this endpoint (it posts to `create-market` with `market_type:1`). The new client should route multi markets here.
- **Role/auth:** authenticated + creator role; banned creators blocked (same checks as binary).
- **Request body:**

| Field | Type | Meaning |
|---|---|---|
| `q` | string | Question |
| `resolution` | string | Resolution rules text |
| `outcomes` | array<string> | Outcome labels; must be an array, each non-empty (server trims + escapes). Count must be within `lmsr_min_outcomes`..`lmsr_max_outcomes` |
| `liquidity` | string/number | Initial liquidity (min 100 VIZ) |
| `liquidity_fee` | int (‰) | LP reward, clamped ≥0 |
| `oracle_id` | int | Oracle user id (must exist as oracle) |
| `oracle_fee` | int | Must be ≥ oracle minimum |
| `creator_fee` | int (‰) | Clamped 0–100 |
| `betting_expiration` | int (unix) | Bet deadline |
| `result_expiration` | int (unix) | Result deadline |
| `allow_early_resolution` | 0/1 | Early resolution toggle |
| `allow_cancellation` | 0/1 | Allow cancellation toggle |
| `allow_instant_bet` | 0/1 | **Ignored — server forces `1`** (multi has no batch settlement yet) |
| `time_penalty_type` | int (0/1) | Late-bet penalty mode |
| `time_penalty_value` | int | Penalty magnitude |
| `committee_id` | int | Dispute judge |

  *(`oracle_fixed_fee` is taken from the oracle profile, not the request; `penalty_curve_type` optional.)*

- **Success response:**

| Field | Type | UI use |
|---|---|---|
| `status` | bool | success flag |
| `market_id` | int | redirect target (`#tab/market/<id>`) |
| `market_type` | int | `1` |
| `lmsr_b` | int | LMSR liquidity parameter (informational) |
| `outcome_count` | int | number of outcomes created |

- **Validation errors (thrown as `Exception` → surfaced as `common.error_prefix + err`):**
  - `outcomes` not an array → "Outcomes must be an array".
  - Fewer than `lmsr_min_outcomes` → "Minimum N outcomes required"; more than `lmsr_max_outcomes` → "Maximum N outcomes allowed".
  - Empty outcome label → "Outcome label cannot be empty".
  - Liquidity < 100 VIZ → "Minimum liquidity is 100 VIZ".
  - Oracle not found / `oracle_fee` too low / insufficient balance / self-oracle insufficient insurance (same family as binary).
  - Liquidity too low for N outcomes (derived `lmsr_b` < `lmsr_min_b_parameter`) → "Liquidity too low for N outcomes (min b=… VIZ)".
  - The whole insert (market row, `market_outcomes`, LP row, balance deduction, creation fee to DAO, self-oracle accept increment) runs in a DB transaction; any failure rolls back and throws "Market creation failed: …".

- **Status semantics:** same as binary — status `0` (hidden, awaiting oracle) unless self-oracle with sufficient insurance → status `1`.

> **VIZ thin-client note (suggestion):** identical to the binary create op but with an `outcomes[]` label array and no `allow_instant_bet` (force batch/instant per protocol rules). The node computes the LMSR `b` parameter from liquidity + outcome count and should reject if below the minimum. One signed transaction: create op + liquidity deposit + creation fee transfer.
## 7. Oracle role: acceptance, insurance, pending queue & resolution

This section documents everything an **Oracle**-role user sees and does, from the client's point of view: the oracle profile / trust screen, the insurance fund, the pending-markets queue (accept / reject), and market resolution (binary, multi, and the "no-contest" refund path). Disputes are covered in section 8 and are excluded here.

**Role gate (client-side):** The Oracle UI lives inside the **Profile** tab (`nav.profile`). The oracle blocks in `render_profile_extra()` only render when `user.oracle == 1`. The register-oracle form renders in the `else` branch. All oracle endpoints additionally require `auth` server-side and re-check `user_arr['oracle']==1` (except registration).

**Money / precision convention:** All VIZ amounts are integers in the protocol at precision 3 (1 VIZ = 1000 units). The client sends human VIZ (e.g. `"5.000"`) and the server does `floor(floatval(amount)*1000)`. Fees expressed in **‰ (permille)** are integers `0..1000` (server divides by 1000); the UI displays them via `fee/price_precision_ratio + '%'`. The currency glyph shown everywhere is `Ƶ`.

---

### Screen: Oracle profile / Trust Index (read view)

**Where:** Opened by `load_oracle_profile(oracle_id)` (app.js 616-702), rendered into `.market-detail`. This is the public view of any oracle (also shown when picking an oracle during market creation), and it is what a self-oracle sees about their own reputation. It fires `load-oracle-profile` (POST) with `{oracle_id}`.

**What the user sees** (all labels from the `oracle.*` namespace):

- **Title** — `oracle.profile_title` → "Oracle profile: %%NAME%%" (uses `data.name` or `#`+`data.id`).
- **Big trust score card** — `data.trust_score` rendered large, colored by score class, with a label and the sublabel `oracle.trust_index` ("Trust Index"). If `data.is_new==1` the label becomes `oracle.new_oracle` ("New oracle").
- **Trust summary rows**:
  - `oracle.resolved` ("Resolved") → `derived.resolution_rate*100` %
  - `oracle.disputes_lost` ("Disputes lost") → `derived.dispute_loss_rate*100` %
  - `oracle.volume` ("Volume") → `metrics.total_volume_resolved` Ƶ
  - `oracle.coverage` ("Coverage (insurance/bets)") → `×`+`data.risk_score`, or `—` when `risk_score>=999` (treated as "effectively infinite / no bets to cover").
- **Contextual hint** (one of):
  - `oracle.hint_new` if `is_new`
  - `oracle.hint_underfunded` if `risk_score<1` and `reliability_score>=60`
  - `oracle.hint_low_reliability` if `reliability_score<40`
- **Info block** (`oracle.info_label`): `oracle.fee_label` + `data.fee` as %, plus `+ <fixed_fee> Ƶ oracle.fee_fixed` when `data.fixed_fee>0`; `oracle.insurance_fund` + `data.insurance` Ƶ; `oracle.rules_label` + `data.rules`; **banned state** (`oracle.banned_label` "⛔ Banned" + `oracle.banned_until {DATE}` or `oracle.banned_forever`) when `data.banned==1`; `oracle.active_since` (from `metrics.active_since`); `oracle.last_active` (from `metrics.last_active_time`); a `<small>` line with `oracle.reliability_label` `reliability_score/100` and `oracle.coverage_label`.
- **Expandable "Detailed statistics"** (`oracle.toggle_details`), containing two tables:
  - **Statistics** (`oracle.statistics_title`): `stat_accepted` (markets_accepted), `stat_resolved` (markets_resolved), `stat_no_contest` (markets_no_contest), `stat_missed` (markets_missed), `stat_disputes_received`, `stat_disputes_lost`, `stat_disputes_won`, `stat_disputes_auto` (disputes_auto_closed), `stat_responses_missed` (dispute_responses_missed), `stat_volume_resolved` (Ƶ), `stat_insurance_slashed` (total_insurance_slashed, Ƶ), `stat_avg_resolution` (avg_resolution_time/3600, in `stat_hours` "h."), `stat_bans` (bans_received).
  - **Indicators** (`oracle.indicators_title`): `rate_resolution`, `rate_dispute_loss`, `rate_no_contest`, `rate_deadline_miss`, `rate_dispute_response` — each `derived.<rate>*100` to 1 decimal.

**Score classes / labels** (`reliability_score_class` / `reliability_score_label`, app.js 595-610). Given `score` (0-100) and `is_new`:

| Condition | CSS class | Label key | English |
|---|---|---|---|
| `is_new` | `reliability-new` | `oracle.score_new` | New |
| `score>=80` | `reliability-excellent` | `oracle.score_excellent` | Excellent |
| `score>=60` | `reliability-good` | `oracle.score_good` | Good |
| `score>=40` | `reliability-average` | `oracle.score_average` | Average |
| `score>=20` | `reliability-poor` | `oracle.score_poor` | Poor |
| else | `reliability-unreliable` | `oracle.score_unreliable` | Unreliable |

`render_reliability_badge(score,is_new)` (app.js 611-615) renders a `<span class="reliability-badge <class>">score — label</span>` used inline elsewhere (e.g. oracle selection lists).

**Displayed metrics come from `compute_oracle_reliability_score` server-side** (api.php 1-81). Node devs only need the *displayed* fields; the formula is already ported. Metrics that feed the UI: markets accepted/resolved/no_contest/missed; disputes received/lost/won/auto_closed; dispute responses missed; total volume resolved; insurance slashed; average resolution time; bans received; active_since; last_active_time. Derived rates shown: resolution_rate, dispute_loss_rate, no_contest_rate, deadline_miss_rate, dispute_response_rate. `is_new` is true when fewer than 5 resolved+no_contest+missed outcomes exist.

#### `load-oracle-profile`
- **Method:** POST · **When:** oracle profile opened (creation flow oracle picker, market detail oracle link). · **Auth:** none required for reading.

| Request field | Type | Meaning |
|---|---|---|
| `oracle_id` | int | User id of the oracle to inspect |

Response (fields the UI consumes; nested objects `metrics` and `derived`):

| Response field | Type | UI use |
|---|---|---|
| `id` / `name` | int / string | Title, fallback `#id` |
| `trust_score` | int | Big score card + class |
| `reliability_score` | int | "Reliability" line, hint thresholds |
| `risk_score` | number/string | "Coverage" (`×value`, `—` if `>=999`), underfunded hint |
| `is_new` | 0/1 | New-oracle label + hint |
| `fee` | int (‰) | Fee % display |
| `fixed_fee` | int (units) | Fixed reward display |
| `insurance` | int (units) | Insurance fund display |
| `rules` | string | Rules line |
| `banned` | 0/1 | Banned badge |
| `ban_until` | int (unixtime) | Ban expiry / "forever" if 0 |
| `metrics.{markets_accepted, markets_resolved, markets_no_contest, markets_missed, disputes_received, disputes_lost, disputes_won, disputes_auto_closed, dispute_responses_missed, total_volume_resolved, total_insurance_slashed, avg_resolution_time, bans_received, active_since, last_active_time}` | int | Statistics table |
| `derived.{resolution_rate, dispute_loss_rate, no_contest_rate, deadline_miss_rate, dispute_response_rate}` | float 0..1 | Indicators table + summary |

**UI states/errors:** on failure `.market-detail` shows `oracle.load_error` + `err.message`.

> **VIZ thin-client note (suggestion):** the trust/reputation numbers are aggregate counters maintained off-chain by the settlement layer. The thin client should treat this as a **read-only query** against the node/indexer (e.g. a `get_oracle_profile` custom API), not a signed action. Only `oracle_id`, the counters, and the derived rates need to be exposed.

---

### Screen: Oracle settings (self, edit) & registration

**Where:** Profile tab, `render_profile_extra()` (app.js 2787-2865).

**If `user.oracle==1`** the client shows an **Oracle settings** editor (`profile.oracle_settings_title`):
- Header `profile.oracle_title` "Oracle: %%NAME%%" and the current insurance via `profile.oracle_insurance_display`.
- Fields: `account` (`profile.oracle_name`), `oracle_rules` textarea (`profile.oracle_rules`), `oracle_fee_reg` number (`profile.oracle_fee` "Fee (‰ from market)", default `user.oracle_fee||5`), `oracle_fixed_fee` number (`profile.oracle_fixed_fee` "Fixed reward (VIZ)", default `(user.oracle_fixed_fee||5000)/1000`).
- Button `profile.save_settings` → `oracle_update_action()` (app.js 2932-2946) which calls **`register-oracle`** with `{account,oracle_rules,oracle_fee,oracle_fixed_fee}`. On success shows `profile.settings_saved` and refreshes user data. (Same endpoint, no fee re-charged because already an oracle.)

**If `user.oracle!=1`** the client shows a **"Become an oracle"** form (`profile.become_oracle`): `account`, `oracle_rules`, `oracle_fee_reg`, button `profile.register` → `register_oracle_action()` (app.js 2917-2930) calling **`register-oracle`** with `{account,oracle_rules,oracle_fee}` (no `oracle_fixed_fee` sent on first registration → server defaults it to 5 VIZ). On success `profile.registered_success`.

#### `register-oracle`
- **Method:** POST · **When:** "Register" (become oracle) or "Save settings" (edit). · **Auth:** required.

| Request field | Type | Meaning |
|---|---|---|
| `account` | string | Oracle display name (required; server rejects empty with "Oracle name is required") |
| `oracle_rules` | string | Free-text operating rules / statement of intent |
| `oracle_fee` | int (‰) | Percentage fee taken from losing pool |
| `oracle_fixed_fee` | float VIZ | One-time fee charged to creator on accept; server `floor(*1000)`, defaults to 5000 units (5 VIZ) if `<=0`. Omitted on first registration. |

| Response field | Type | UI use |
|---|---|---|
| `status` | bool | Success flag |
| `updated` | bool (edit path only) | Present when profile was updated vs. first registration |

**Behavior:** First registration debits a `oracle_registration_fee` (settings) from balance; edit path charges nothing. **UI errors:** "Oracle name is required", "Insufficient balance for registration fee", "Registration failed" / "Profile update failed" — surfaced inline via the loading paragraph with `common.error_prefix`.

> **VIZ thin-client note (suggestion):** registration = an on-chain account flag + a fee transfer. The thin client would sign a custom operation (e.g. `oracle_register`) carrying name/rules/fee/fixed_fee, and pay the registration fee via a transfer to the protocol account. Editing settings is the same op without the fee.

---

### Screen: Insurance fund (deposit / withdraw)

**Where:** Profile tab, inside the oracle block (`render_profile_extra` 2807-2813). Shows current insurance (`profile.oracle_insurance_display`), an amount input (`form.amount_placeholder`, `step=0.001`), and two buttons: `oracle.insurance_deposit` ("Deposit", `.oracle-deposit-ins-action`) and `oracle.insurance_withdraw` ("Withdraw", `.oracle-withdraw-ins-action`). Both call `oracle_insurance_action(type)` (app.js 2961-2972), which posts the raw `amount` string and on success shows `common.success` and refreshes user data.

The insurance fund is a **gate for accepting markets** (see accept below) and the pool that gets **slashed** on no-contest / lost disputes.

#### `oracle-deposit-insurance`
- **Method:** POST · **When:** "Deposit" button. · **Auth:** required; must be oracle.

| Request field | Type | Meaning |
|---|---|---|
| `amount` | float VIZ | Amount to move from balance into insurance; server `floor(*1000)`, must be `>0` |

| Response field | Type | UI use |
|---|---|---|
| `status` | bool | Success |

**Errors:** "Not an oracle", "Invalid amount", "Insufficient balance", "Deposit failed".

#### `oracle-withdraw-insurance`
- **Method:** POST · **When:** "Withdraw" button. · **Auth:** required; must be oracle.

| Request field | Type | Meaning |
|---|---|---|
| `amount` | float VIZ | Amount to move from insurance back to balance; must be `>0` and `<= oracle_insurance` |

| Response field | Type | UI use |
|---|---|---|
| `status` | bool | Success |

**Errors:** "Not an oracle", "Invalid amount", "Insufficient insurance balance", **"Cannot withdraw insurance while you have active markets"** (blocked when the oracle has any market in status 0 or 1), "Withdraw failed".

> **VIZ thin-client note (suggestion):** insurance is escrow tied to the oracle account. Deposit/withdraw are two signed transfers between the oracle's balance and a locked insurance sub-balance. The node must enforce the "no active markets" lock on withdraw, and expose the current insurance amount for the accept-gate check.

---

### Screen: Pending markets queue (accept / reject)

**Where:** Profile tab, `.oracle-pending-markets` region, rendered by `render_profile_oracle_section()` (app.js 2867-2881). It fires **`load-pending-markets`** (POST, empty body). Only markets where the current user is the assigned oracle AND `status==0` (Awaiting oracle) are returned.

**What the user sees:** header `profile.pending_markets` ("Awaiting your confirmation:") then, per market, an `.outcome-card` with the question text `m.q` and two buttons:
- `resolution.accept_button` ("Accept", `.oracle-accept-btn` carrying `data-mid`) → `oracle_accept_action(market_id)` (app.js 2772-2778).
- `resolution.reject_button` ("Reject", `.oracle-reject-btn`) → `oracle_reject_action(market_id)` (app.js 2779-2785).

Empty state: `profile.no_pending_markets` ("No pending markets"). Load error: `profile.load_error`.

#### `load-pending-markets`
- **Method:** POST · **When:** oracle section of profile renders / after an accept/reject. · **Auth:** required. Returns `[]` if the user is not an oracle.

**Request:** empty `{}`.

**Response:** a JSON **array** of market rows (`SELECT * FROM markets ... status=0 ORDER BY id DESC LIMIT 50`). The UI consumes:

| Response field | Type | UI use |
|---|---|---|
| `id` | int | button `data-mid` |
| `q` | string | market question shown on the card |

(Full market rows are returned; only `id` and `q` are read by this view.)

#### `oracle-accept-market`
- **Method:** POST · **When:** "Accept". · **Auth:** required; caller must be the market's oracle; market must be `status==0`.

| Request field | Type | Meaning |
|---|---|---|
| `market_id` | int | Market to accept |

| Response field | Type | UI use |
|---|---|---|
| `status` | bool | Success → `resolution.market_accepted`, re-render queue |

**Server-side gating the client must anticipate (all surface as `err.message`):**
- "Market not found"; "You are not the oracle for this market"; "Market is not waiting for oracle approval" (status ≠ 0).
- **Ban check:** "Oracle is banned from accepting markets" when `oracle_banned==1` and ban not expired.
- **Insurance minimum:** "Insufficient oracle insurance (min: X VIZ)" when `oracle_insurance < min_oracle_insurance`.
- **Creator fixed-fee funding:** "Creator has insufficient balance for oracle fixed fee (X VIZ)".
- Generic "Accept failed".

**Side effects (context only):** market → `status=1` (Active), fixed fee moved creator→oracle, oracle's `markets_accepted` incremented, and a possible Lazy-Pool auto-allocation of liquidity. The client just needs the `status:true`.

#### `oracle-reject-market`
- **Method:** POST · **When:** "Reject". · **Auth:** required; caller must be the market's oracle; market must be `status==0`.

| Request field | Type | Meaning |
|---|---|---|
| `market_id` | int | Market to reject |

| Response field | Type | UI use |
|---|---|---|
| `status` | bool | Success → `resolution.market_rejected`, re-render queue |

**Behavior:** returns the creator's provided liquidity in full and **deletes** the market row. **Errors:** "Market not found", "You are not the oracle for this market", "Market is not waiting for oracle approval", "Reject failed".

> **VIZ thin-client note (suggestion):** accept/reject are signed oracle operations referencing `market_id`. The node must, on accept, verify the ban window and `insurance >= min_oracle_insurance`, and atomically transfer the creator's fixed fee to the oracle; on reject, refund liquidity and cancel the market. The thin client should pre-check insurance/ban locally (from the profile query) to avoid a failed broadcast, but the node stays authoritative.

---

### Screen: Resolution (submit result)

**Where:** Market detail page, `.resolve-form` (app.js 1911-1928). Rendered **only** when `auth && data.oracle && user.id==data.oracle.id && (m.status==1 || m.status==2) && m.payout_status==0` — i.e. the current user is this market's oracle and the market is Active (1) or Closed (2) and not yet paid out.

**What the user sees** (`resolution.*`):
- Title `resolution.submit_title` ("Submit result (oracle)").
- A `select[name=resolve_outcome]`:
  - **Binary:** two options `0 → m.a`, `1 → m.b`.
  - **Multi** (`is_multi && m.outcomes`): one option per outcome, `value=oi`, label `m.outcomes[oi].label`.
- A text input `resolve_decision` (`resolution.justification`).
- Button `resolution.confirm_button` ("Confirm result", `.resolve-market-action`) → `resolve_market_action(el)` (app.js 2755-2770).

**Client dispatch logic:** `resolve_market_action` reads `market_id`, `mtype` (from `data-market-type`), the selected `outcome`, and `decision`. It picks the endpoint by type: `mtype==1 → resolve-market-multi`, else `resolve-market`. It posts `{market_id, outcome, decision}` for BOTH. On success it shows `resolution.submitted_success` and reloads the market detail after ~1s.

> ⚠️ **Field-name mismatch the node devs MUST handle (real code):** the multi endpoint (`resolve-market-multi`, api.php 3152) reads `$request['winning_outcome']`, but the client (`resolve_market_action`) sends the key `outcome`, not `winning_outcome`. It also does not send `decision_url` (the server reads an optional `decision_url` but the form has no such field). A faithful port should either accept `outcome` as an alias for `winning_outcome` on the multi endpoint, or the new client should send `winning_outcome` for multi. Binary (`resolve-market`) correctly reads `outcome`.

#### `resolve-market` (binary)
- **Method:** POST · **When:** "Confirm result" on a binary market. · **Auth:** required; caller must be the market's oracle.

| Request field | Type | Meaning |
|---|---|---|
| `market_id` | int | Market to resolve |
| `outcome` | int (0 or 1) | Winning side; 0→outcome A, 1→outcome B. Server rejects any other value ("Invalid outcome"). |
| `decision` | string | Free-text justification (stored) |
| `decision_url` | string (optional) | Evidence URL; **not sent by current UI** but read if present |

| Response field | Type | UI use |
|---|---|---|
| `status` | bool | Success → `resolution.submitted_success`, reload detail |

**Gating / errors (surface as `err.message`):**
- "Invalid outcome" (outcome ∉ {0,1}); "Market not found"; "You are not the oracle".
- **Ban:** "Oracle is banned from resolving markets".
- **Status:** "Market cannot be resolved in current status" (only status 1 or 2 allowed).
- **Early-resolution timing:** when `status==1` and `allow_early_resolution!=1`, resolving before `betting_expiration` fails with "Betting period not expired and early resolution not allowed".
- "Resolution failed: …".

**Effect (context):** market → `status=3` (Resolved), `resolved_outcome`, `payout_status=1` (Calculated), parimutuel payouts created (winners split losers' pool by weight, minus oracle/creator/LP fees; time penalties; LP principal+fee). Bettors are notified of a **dispute grace window** (`dispute_grace_hours`). The detail view then starts a grace countdown if `m.grace_period_end` is present (app.js 1945-1947, label `status.grace_countdown`).

#### `resolve-market-multi` (multi-outcome)
- **Method:** POST · **When:** "Confirm result" on a multi market (`market_type==1`). · **Auth:** required; caller must be the market's oracle.

| Request field | Type | Meaning |
|---|---|---|
| `market_id` | int | Market to resolve |
| `winning_outcome` | int | Winning outcome index, `0 <= idx < outcome_count`. **(Client currently sends this value under the key `outcome` — see mismatch note above.)** |
| `decision` | string | Justification |
| `decision_url` | string (optional) | Evidence URL |

| Response field | Type | UI use |
|---|---|---|
| `status` | bool | Success → `resolution.submitted_success`, reload |
| `winning_outcome` | int | (echo) |
| `losers_sum` | int (units) | (echo) |
| `oracle_fee` | int (units) | (echo) |
| `creator_fee` | int (units) | (echo) |
| `winners_pool` | int (units) | (echo) |

The current UI ignores the extra echo fields and only branches on success/`err.message`.

**Gating / errors:** "Not a multi-outcome market", "You are not the oracle", "Oracle is banned", "Market cannot be resolved in current status", "Invalid winning outcome" (out of `[0,outcome_count)`), early-resolution timing (same rule as binary), "Resolution failed: …".

**Result-expiration timing shown to user (context):** market detail displays the **result submission deadline** (`market_detail.result_deadline`, from `market.result_expiration`) and the **early-resolution** flag (✅/❌ `market_detail.early_resolution`, app.js 1692). These tell the oracle whether they may resolve before betting closes and by when they must resolve. Missing the result deadline is what drives the `markets_missed` / auto-refund penalty path (server-side cron; no dedicated client action).

> **VIZ thin-client note (suggestion):** resolution is the core signed oracle op: `resolve_market(market_id, winning_outcome, decision, decision_url?)`. The node must enforce (a) caller == assigned oracle, (b) ban window, (c) market status ∈ {active, closed}, (d) `allow_early_resolution` vs. `betting_expiration`, (e) valid outcome index, then run the (already-ported) parimutuel settlement and open the dispute grace window. Recommend the new client always send a single canonical key (`winning_outcome`) for both binary and multi to avoid the legacy `outcome`/`winning_outcome` split.

---

### "No contest" (oracle refund resolution)

`oracle-no-contest` (api.php 1912-2027) lets an oracle declare it cannot verify the outcome: it behaves like a resolution with `resolved_outcome=-1`, refunding every bet (full stake) and every non-lazy-pool LP (principal), slashing part of the oracle's insurance as a penalty distributed pro-rata to participants, and opening the same dispute grace window.

> ⚠️ **Not wired in the current front-end.** A full search of `app.js` finds **no** button, form, or `api_post('oracle-no-contest', …)` call. The endpoint exists and is reachable, and the profile screen *displays* the resulting counters (`oracle.stat_no_contest` / `oracle.rate_no_contest`), but there is no UI to trigger it in this prototype. The new client should **add** a "No contest" action on the resolution screen.

#### `oracle-no-contest`
- **Method:** POST · **When:** (intended) an oracle chooses "cannot verify" instead of picking a side. · **Auth:** required; caller must be the market's oracle; market status must be 1 (Active) or 2 (Closed).

| Request field | Type | Meaning |
|---|---|---|
| `market_id` | int | Market to declare no-contest |
| `reason` | string | Required explanation (server rejects empty: "Reason is required") |

| Response field | Type | UI use |
|---|---|---|
| `status` | bool | Success |
| `penalty` | int (units) | Insurance slashed (capped at oracle's insurance; `oracle_no_contest_penalty_percent` of `dispute_fee`, default 50%) |
| `refund_payouts_created` | int | Count of refund payouts created (bets + LP) |
| `grace_period_hours` | int | Dispute grace window length |

**Errors:** "Reason is required", "Market not found", "You are not the oracle", "Market is not in a state that allows no-contest", "No-contest failed: …".

**Effect (context):** all active bets → refunded at full stake (pending payouts type 0); personal LPs refunded principal (type 1); lazy-pool principal returned to pool; oracle insurance slashed and distributed proportionally to participants' stakes (immediate balance credit + payout type 6); market → `status=3`, `resolved_outcome=-1`, `decision="NO-CONTEST: <reason>"`; oracle `markets_no_contest`++ and `total_insurance_slashed`+=penalty; participants notified they may dispute during the grace period.

> **VIZ thin-client note (suggestion):** model as a distinct signed op `oracle_no_contest(market_id, reason)`. The node computes the insurance slash (`no_contest_penalty_percent` × `dispute_fee`, capped at insurance), refunds all stakes/LP principal, distributes the slash pro-rata, and opens the grace window. Since the legacy UI never exposed this, the new client should add the button; the response's `penalty` / `refund_payouts_created` / `grace_period_hours` are useful to confirm the action to the user.
## 8. Disputes & DAO / committee governance

This section documents the full dispute lifecycle and the governance surfaces (committee / DAO arbitrator, unban) exactly as the current PHP + `app.js` prototype implements them. It is written from the user's point of view: what each role sees on the market-detail screen, which button fires which API call, the exact request payload the client sends, and the exact response fields the UI consumes.

All dispute UI lives **inside the market-detail screen** (`load_market_detail` in `app.js`, roughly lines 1866–1909). There is no separate "disputes" page. The blocks that appear are gated by market status, the caller's role, and whether a dispute object was returned. The three action handlers are `create_dispute_action` (`app.js` 2710), `oracle_respond_action` (2725), and `resolve_dispute_action` (2739).

Key data the market-detail response must carry for this section to render:
- `m.status` — market lifecycle status. `3` = resolved. Dispute affordances only appear at `status==3`.
- `m.payout_status` — payout lifecycle. `1` = calculated/disputable (`status.payout_1` = "Calculated"), `3` = disputed (`status.payout_3` = "Disputed"). The dispute-open form only renders when `payout_status==1`.
- `m.committee` — id of the committee/arbitrator assigned at market creation. A dispute is impossible if this is `0` (server: "No committee assigned"). Shown in detail via `market_detail.committee_id` = "Committee: #%%ID%%".
- `m.update` — last-update unixtime; combined with the `dispute_grace_hours` setting (default 12) it drives the grace-period countdown badge (`status.grace_countdown` = "Payout in %%TIME%% (if no dispute)").
- `data.oracle` — `{ id, ... }` of the market's oracle. Used to decide whether to show the oracle-response form (`user.id == data.oracle.id`).
- `data.dispute` — the dispute record (present once one exists). Fields consumed by the UI: `id`, `status` (0/1/2), `claim_url`, `oracle_response`, `committee_decision`.
- `user.committee` — `1` if the logged-in user is a committee member / arbitrator. Gates the resolution form.

### Screen: Market detail — dispute affordances (bettor / disputer view)

When a market is resolved and still in the disputable window (`auth && m.status==3 && m.payout_status==1`), the client renders the **"Dispute result"** block (`dispute.open_title`):

- Heading: **"Dispute result"**.
- A single text input `claim_url` with placeholder **"Claim link"** (`dispute.claim_placeholder`). This is the evidence/claim URL — the only user input for opening a dispute. There is no separate free-text claim body; the "claim" is expressed as a link.
- A red button **"Open dispute"** (`dispute.open_button`, class `create-dispute-action`).
- A `.dispute-loading` status line that shows `bet.sending` while in-flight, then `dispute.opened_success` ("Dispute opened!") on success or `common.error_prefix` + message on failure.

The button is a soft double-submit guard: the click handler adds a `disabled` class before firing (`app.js` 3267). On success the client calls `update_user_data()` (to reflect the debited fee) and re-loads the market detail after 1s.

The client does **not** pre-check eligibility beyond rendering the form when `status==3 && payout_status==1`. All the real gating (must have a bet, within grace, committee assigned, no open dispute, sufficient balance) is enforced server-side and surfaced as an error string in `.dispute-loading`.

Note on cost/bond: there is **no bond input in the UI**. The dispute costs a fixed fee read from the `dispute_fee` setting (the About FAQ, `about.faq_a8`, states "1000 VIZ"). The fee is debited from the user's balance by the server; the disputer never types an amount.

#### API: `create-dispute`

- **Method:** POST `/api/create-dispute/`
- **Fires when:** user clicks **"Open dispute"** (`create_dispute_action`, `app.js` 2710).
- **Role/auth:** authenticated user who **has at least one bet on the market** (server checks `bets` count > 0). Any bettor may dispute; there is no separate "disputer" role.

**Request fields**

| Field | Type | Meaning |
|---|---|---|
| `market_id` | int | Market being disputed (`form.data('market')`). |
| `claim_url` | string | The claim/evidence link. **Required** — server rejects empty with "Claim URL is required". Server `htmlspecialchars`-escapes it. |

**Server preconditions (each returns a distinct error string shown in `.dispute-loading`)**

| Condition | Error message |
|---|---|
| Market missing | "Market not found" |
| `status != 3` | "Market is not resolved" |
| `payout_status != 1` | "Market payouts not in disputable state" |
| `committee == 0` | "No committee assigned to this market, dispute not possible" |
| `now > update + dispute_grace_hours*3600` | "Dispute grace period expired" |
| caller has no bet | "You have no bets on this market" |
| an open dispute already exists (`status==0`) | "Dispute already open for this market" |
| balance < `dispute_fee` | "Insufficient balance for dispute fee" |

**Response fields**

| Field | Type | UI use |
|---|---|---|
| `status` | bool `true` | Success flag. |
| `dispute_id` | int | New dispute id. Not directly rendered; the client re-loads the market detail which returns the full `data.dispute`. |

**Server side effects (for reviewer context, not client contract):** debits `dispute_fee` from the disputer, sets market `payout_status=3` ("Disputed"), increments oracle's `oracle_disputes_received`, writes a `history` row (type `12` = "Dispute payment") and a `market_log` `dispute` entry.

**VIZ thin-client note (suggestion):** A dispute would be a signed `custom`/`account_metadata`-style operation from the disputer's account referencing the market id and claim URL, escrowing the `dispute_fee` bond (e.g. a transfer to a dispute-escrow account or a lock). The node/indexer must enforce: market resolved, within grace window, committee assigned, caller holds a position, no open dispute. Emit a `dispute_opened` event carrying `dispute_id`, `market_id`, `claim_url`.

### Screen: Market detail — existing dispute card (all viewers)

Once `data.dispute` is present, every viewer sees an **outcome card** (`app.js` 1876–1883):

- Title **"Dispute #<id>"** (`dispute.title`, ID interpolated) followed by a status label derived from `d.status`:
  - `0` → **"Open"** (`dispute.status_0`)
  - `1` → **"Resolved for complaint"** (`dispute.status_1`, i.e. oracle was wrong)
  - `2` → **"Resolved for oracle"** (`dispute.status_2`, i.e. oracle was right)
- If `d.claim_url`: **"Claim:"** (`dispute.claim_label`) plus a hyperlink labelled **"link"** (`dispute.claim_link`) opening the URL in a new tab.
- If `d.oracle_response`: **"Oracle response:"** (`dispute.oracle_response_label`) followed by the escaped text.
- If `d.committee_decision`: **"Committee decision:"** (`dispute.committee_decision_label`) followed by the escaped justification text.

This card is the **dispute verification view**: reviewers read the claim link, the oracle's rebuttal, and the committee's justification side by side. Broader outcome verification (the numeric result, payouts, and the full market log including `dispute`, `dispute_resolve`, `dispute_oracle_response` entries) is rendered by the surrounding market-detail screen and market-log table (`app.js` ~1930), so committee members can check the resolved outcome and payout recalculation before deciding.

### Screen: Oracle response form (oracle only)

Rendered only when `auth && d.status==0 && data.oracle && user.id==data.oracle.id` (`app.js` 1885). The market's oracle sees:

- A text input `oracle_resp` with placeholder **"Oracle response"** (`dispute.oracle_response_placeholder`).
- A blue button **"Reply"** (`dispute.oracle_response_button`, class `oracle-respond-action`).

On success the client shows a success toast **"Oracle response sent"** (`dispute.oracle_responded_success`) and re-loads the current market from the URL hash.

#### API: `dispute-oracle-response`

- **Method:** POST `/api/dispute-oracle-response/`
- **Fires when:** oracle clicks **"Reply"** (`oracle_respond_action`, `app.js` 2725).
- **Role/auth:** authenticated user who is the market's oracle (`market.oracle == user.id`).

**Request fields**

| Field | Type | Meaning |
|---|---|---|
| `dispute_id` | int | Dispute being answered (`form.data('dispute')`). |
| `oracle_response` | string | The oracle's rebuttal text (server `htmlspecialchars`-escaped). |

**Server preconditions / errors**

| Condition | Error |
|---|---|
| Dispute not found | "Dispute not found" |
| `dispute.status != 0` | "Dispute is not open" |
| caller is not the oracle | "You are not the oracle" |

**Response fields**

| Field | Type | UI use |
|---|---|---|
| `status` | bool `true` | Success; triggers toast + reload. |

Server stores `oracle_response`, `oracle_response_time`, `update`. (No fee, no state change on the market; dispute stays `status==0` awaiting the committee.)

**VIZ thin-client note (suggestion):** A signed operation from the oracle account referencing `dispute_id`, carrying the rebuttal text, valid only while the dispute is open. Emit `dispute_oracle_responded`.

### Screen: Committee / DAO resolution form (committee member / arbitrator)

Rendered only when `auth && d.status==0 && user.committee==1` (`app.js` 1892). This is the arbitrator role the About page calls the **"Arbitrator (Judge)"** (`about.role_judge`) who "resolves disputes between players and oracle". In the current prototype there is a **single committee/arbitrator role** — there is no multi-voter tally UI; one committee member's submission resolves the dispute outright. (A "private resolver"/DAO-fund payout account exists only server-side as a settings-configured recipient of resolver rewards, not as a distinct client role.)

The form (`.committee-form`, carries `data-dispute` and `data-market`) contains:

- Heading **"Committee decision"** (`dispute.committee_title`).
- Select `committee_decision_type` with two options:
  - value `0` → **"Oracle is right"** (`dispute.committee_oracle_right`)
  - value `1` → **"Oracle is wrong"** (`dispute.committee_oracle_wrong`)
- Select `correct_outcome` — the outcome the committee deems correct. For binary markets: value `0` = side A label (`m.a`), value `1` = side B label (`m.b`). For multi-outcome markets it enumerates `m.outcomes[i].label` at index `i`.
- Text input `committee_text` — placeholder **"Justification"** (`dispute.committee_justification`).
- Amber button **"Issue decision"** (`dispute.committee_submit`, class `resolve-dispute-action`).

On success: toast **"Dispute resolved"** (`dispute.resolved_success`) and market re-load after 1s.

#### API: `resolve-dispute`

- **Method:** POST `/api/resolve-dispute/`
- **Fires when:** committee member clicks **"Issue decision"** (`resolve_dispute_action`, `app.js` 2739).
- **Role/auth:** authenticated user with `user_arr['committee'] == 1`, else "Committee permission required". This is the single governance gate for the entire resolution.

**Request fields — what the CLIENT currently sends** (`app.js` 2746):

| Field | Type | Meaning |
|---|---|---|
| `dispute_id` | int | Dispute being resolved. |
| `decision` | int | Value of the `committee_decision_type` select (0 = oracle right, 1 = oracle wrong). **Note:** the server does *not* read `decision`; it derives "oracle wrong" purely by comparing `correct_outcome` to the market's `resolved_outcome`. This field is effectively informational/legacy from the client. |
| `correct_outcome` | int | The authoritative decision: `-1` = no-contest (refund everyone), `0` = outcome A wins, `1` = outcome B wins. Server rejects anything else with "Invalid correct outcome (-1, 0, or 1)". |
| `committee_decision` | string | The justification text (`committee_text`). Stored as `disputes.committee_decision` and shown back in the dispute card. |

**Additional request fields the SERVER accepts but the current UI does NOT send** (`api.php` 2100–2106). Node devs must map these as first-class governance parameters even though the prototype form omits them — the endpoint reads them via `intval($request[...])`, so they default to `0` when absent:

| Field | Type | Meaning / server behaviour |
|---|---|---|
| `penalty_amount` | int | Extra insurance slash beyond the automatic dispute reward pool. Clamped to `>=0` and capped at the oracle's remaining `oracle_insurance`. If > 0, moved from oracle `oracle_insurance` into the `dao_fund_account_id` balance (history type `18`). Available for **any** outcome (committee discretion). |
| `ban_oracle` | int (0/1) | If `1`, sets the oracle's `oracle_banned=1` and `oracle_ban_until` = `ban_oracle_until`; increments `oracle_bans_received`. |
| `ban_oracle_until` | int | Unixtime the oracle ban lifts; `0` = permanent. |
| `ban_creator` | int (0/1) | If `1`, sets the market creator's `creator_banned=1` and `creator_ban_until` = `ban_creator_until`. |
| `ban_creator_until` | int | Unixtime the creator ban lifts; `0` = permanent. |

**Full governance effect of a single `resolve-dispute` call** (enumerated so every possible on-chain transaction is covered):

1. **Determine fault.** `oracle_was_wrong = (correct_outcome != market.resolved_outcome)`.
2. **Fee distribution — oracle wrong:** compute `reward_pool = min(dispute_fee * dispute_reward_multiplier, oracle_insurance)` (multiplier default 3.0). Disputer is refunded `dispute_fee + disputer_reward` (`disputer_reward = reward_pool / multiplier`; history type `13` = "Dispute refund", payout type `5`). The remainder `voter_reward` goes to the settings-configured `dispute_resolver_account_id` (history type `20`, payout type `8`). `reward_pool` is slashed from the oracle's `oracle_insurance`.
3. **Fee distribution — oracle right:** dispute fee is split by `dispute_rejected_voter_permille` (default 500 = 50%): resolver account gets `voter_share` (history type `20`, payout type `8`); the oracle gets the remaining `oracle_share` as compensation (history type `21`, payout type `9`). The disputer is **not** refunded.
4. **Extra sanctions (any outcome):** apply `penalty_amount` slash to `dao_fund_account_id`; apply `ban_oracle` / `ban_creator` flags with their until-timestamps.
5. **Payout recalculation:**
   - `correct_outcome == -1` (no-contest): delete pending payouts, refund every active bet at face value (payout type `0`), refund every LP its principal (payout type `1`, lazy-pool principal returned to pool), set market `resolved_outcome=-1`, `payout_status=1`.
   - `correct_outcome == 0|1`: recompute parimutuel payouts — winners get `bet + profit_share - time_penalty` (payout type `0`), oracle fee (payout type `2`), creator fee (payout type `7`), LP fee pool + penalty pool distributed to liquidity (payout type `1`); set `resolved_outcome` and `payout_status=1`.
6. **Dispute record close-out:** `disputes.status` set to `1` (oracle wrong) or `2` (oracle right); stores `committee_decision`, `committee_user` (the acting member), `resolved_time`, and the echoed `penalty_amount` / `ban_*` fields.
7. **Reputation:** increments oracle `oracle_disputes_lost` (+ lazy-pool fault stamp) or `oracle_disputes_won`; accumulates `oracle_total_insurance_slashed` by `reward_pool + penalty_amount`.
8. Writes a `market_log` `dispute_resolve` entry summarising `correct_outcome`, `penalty_amount`, `ban_oracle`, `ban_creator`.

**Response fields**

| Field | Type | UI use |
|---|---|---|
| `status` | bool `true` | Success; toast "Dispute resolved" + market re-load. The UI reads nothing else — all recomputed payouts/bans are re-fetched via the subsequent `load_market_detail`. |

**Error states:** "Committee permission required", "Invalid correct outcome (-1, 0, or 1)", "Dispute not found", "Dispute is not open", "Market not found", or "Dispute resolution failed: …" (transaction rollback). All shown via `show_message('danger', …)`.

**VIZ thin-client note (suggestion):** This is the heaviest governance transaction and must be signed by a committee-authorised account. A single operation should carry: `dispute_id`, `correct_outcome` (−1/0/1), `justification`, and the discretionary `penalty_amount`, `ban_oracle` + `ban_oracle_until`, `ban_creator` + `ban_creator_until`. Broadcasting it must atomically trigger, on-chain/indexer: insurance slash + reward split to disputer and resolver/DAO-fund accounts, ban flags, full payout recomputation for the market, and dispute close-out. If governance moves to true multi-signer voting, model each committee member's submission as a separate `dispute_vote` op and resolve on quorum — the current single-submission flow is the minimum to replicate. The `decision` field can be dropped (server ignores it).

### Data feed: `load-committees` (market creation, arbitrator picker)

- **Method:** GET `/api/load-committees/`
- **Fires when:** the create-market screen initialises (`app.js` ~920), to populate the **"Dispute judge:"** selector (`market.committee_label`; default option **"Not selected"**, `market.committee_not_selected`).
- **Role/auth:** none (public GET).

**Response:** a JSON array of objects `{ id, name }`, one per user with `committee==1` (max 100). `name` is the account handle, or a name/username derived from the user's `data` blob, or `User #<id>` fallback. The client renders each as `<option value="{id}">{name} [#{id}]</option>`.

**VIZ thin-client note (suggestion):** the equivalent is a chain/indexer query returning accounts holding a "committee"/arbitrator flag, so the market creator can bind a committee at creation time (the `committee` field that later gates disputes).

### Screen / action: Unban user (committee / governance)

There is **no dedicated unban screen in the shipped `app.js`** — the `unban-user` endpoint exists server-side for governance tooling but no button in the reviewed client calls it. It is the inverse of the ban flags a committee can set via `resolve-dispute`. Documented here for completeness because node devs must map it as a governance transaction.

#### API: `unban-user`

- **Method:** POST `/api/unban-user/`
- **Fires when:** governance/admin action (no UI trigger in current `app.js`).
- **Role/auth:** `user_arr['committee'] == 1`, else "Committee permission required".

**Request fields**

| Field | Type | Meaning |
|---|---|---|
| `user_id` | int | Target user to unban. Must be > 0 and exist ("Invalid user ID" / "User not found"). |
| `unban_oracle` | int (0/1) | Clear the target's oracle ban (`oracle_banned=0`, `oracle_ban_until=0`). |
| `unban_creator` | int (0/1) | Clear the target's market-creator ban (`creator_banned=0`, `creator_ban_until=0`). |

If both flags are `0`, server returns "Nothing to unban". Writes a `history` row type `19` for the target.

**Response fields**

| Field | Type | UI use |
|---|---|---|
| `status` | bool `true` | Success. |
| `unbanned_oracle` | int (0/1) | Echo of which ban was cleared. |
| `unbanned_creator` | int (0/1) | Echo of which ban was cleared. |

**Error states:** "Committee permission required", "Invalid user ID", "User not found", "Nothing to unban", "Unban failed" (rollback).

**VIZ thin-client note (suggestion):** a committee-signed op targeting an account, with two booleans (clear oracle ban / clear creator ban). Should reverse whatever `resolve-dispute` set. Emit an `unban` governance event for auditability.

### Cross-role summary of dispute-related history / payout labels

Reviewers verifying money movement will encounter these labels (from `i18n/en.json`), all produced by the flows above:

| Code | Label | Where produced |
|---|---|---|
| history `12` | "Dispute payment" | disputer pays fee (`create-dispute`) |
| history `13` | "Dispute refund" | disputer refunded when oracle wrong (`resolve-dispute`) |
| history `14` | "Oracle penalty" | oracle insurance penalty |
| history `15` | "Committee reward" | committee/resolver reward |
| history `18` | (extra penalty → DAO fund) | `penalty_amount` slash in `resolve-dispute` |
| history `19` | (unban marker) | `unban-user` |
| history `20`/`21` | resolver share / oracle compensation | `resolve-dispute` |
| payout type `4` | "Committee" | committee payout |
| payout type `5` | "Dispute refund" | disputer refund |
| payout type `6` | "Oracle penalty bonus" | penalty bonus |
| market status | "Disputed" (`status.payout_3`) | market `payout_status==3` after `create-dispute` |
## 9. Wallet, balance, history, liquidity pools & leverage

This section documents the **Balance / Wallet tab**, transaction history, the user's positions & payouts views, liquidity provision (add/withdraw), the lazy pool endpoints, and the Boost (leverage) flow. All request/response field names below are taken verbatim from `app.js` and `module/api.php`.

**Conventions used throughout:**
- Amounts are internally in **milli-VIZ** (1 VIZ = 1000 units; `price_precision_ratio` = 1000). Inputs typed by the user in VIZ are multiplied by 1000 client-side (`Math.floor(x*1000)`) OR by the server (`floor(floatval(...)*1000)`) — noted per endpoint. The UI renders amounts back with `render_number()` and appends the `Ƶ` glyph.
- All money endpoints in this section require `$auth` (a logged-in session cookie). Endpoints throw `Auth required` (HTTP 400 JSON `{success:false, error:...}`) when not authenticated.
- `api_post(endpoint, body)` (app.js:2044) is the shared POST helper: it `POST`s JSON to `/api/<endpoint>/`, parses JSON, and **throws `new Error(data.error)` on any non-2xx** — every caller shows that `.message` in a red/`negative-info` element.

---

### Screen: Balance / Wallet tab (`#tab/history`)

Rendered in `select_tab()` at app.js:943-1015. The tab is internally named `history`; its title uses `balance.title` = **"Balance change history"**.

**What the user sees (top to bottom):**
1. Title **"Balance change history"** (`balance.title`).
2. If not logged in: **"You are not logged in"** (`market.not_logged_in`) and nothing else.
3. Available balance line (only if `user.balance` is defined): **"Available balance: %AMOUNT% Ƶ"** (`balance.available`), rendered green (`positive-balance`) from the cached `user['balance']` (no dedicated fetch — balance comes from session/user-data).
4. Two toggle buttons: **"Top up"** (`balance.topup_button`, `rel="deposit"`) and **"Withdraw"** (`balance.withdraw_button`, `rel="withdraw"`), separated by " / ". Clicking a `.toggle-addon` (handler app.js:3144) expands an inline `.addon` panel rendered by `render_addon(rel)` (app.js:1304). Clicking again collapses it; only one addon is open at a time.
5. A horizontal rule, then the **history table** with three columns: **Date** (`balance.table_date`), **Operation type** (`balance.table_type`), **Amount** (`balance.table_amount`). While loading it shows a spanning row **"Loading…"** (`balance.loading`).
6. Footer note (`balance.history_note`): *"History shows the last 100 records. The balance is displayed considering the precision (1 VIZ = 1000 units)."*

On tab render, the table body is filled by a **GET** to `load-history` (below). Each row's amount cell is colored `positive-balance` when `history.negative==0`, else `negative-balance`.

**Operation-type labels** are looked up by numeric `type` into an array built from `balance.history_type_0..16` (app.js:987). The full map:

| type | i18n key | English label |
|---|---|---|
| 0 | `balance.history_type_0` | VIZ Deposit |
| 1 | `balance.history_type_1` | TON/USDT Conversion |
| 2 | `balance.history_type_2` | Withdrawal |
| 3 | `balance.history_type_3` | Bet |
| 4 | `balance.history_type_4` | Bet cancellation |
| 5 | `balance.history_type_5` | Bet result |
| 6 | `balance.history_type_6` | Liquidity provision |
| 7 | `balance.history_type_7` | Liquidity result |
| 8 | `balance.history_type_8` | Oracle registration |
| 9 | `balance.history_type_9` | Oracle insurance deposit |
| 10 | `balance.history_type_10` | Oracle insurance withdrawal |
| 11 | `balance.history_type_11` | Creator registration |
| 12 | `balance.history_type_12` | Dispute payment |
| 13 | `balance.history_type_13` | Dispute refund |
| 14 | `balance.history_type_14` | Oracle penalty |
| 15 | `balance.history_type_15` | Committee reward |
| 16 | `balance.history_type_16` | Payout |

> Note: the server also writes history rows with `type` 19 (unban), 20/21/22 (lazy-pool deposit/withdraw/emergency), 30/34 (leverage open/convert). These numeric types have **no label** in the `0..16` array, so they render as `undefined` in the table. The VIZ thin client should extend this label map if lazy-pool/leverage rows must be human-readable.

**Error states:** if `load-history` returns non-OK, the loading row text becomes **"Failed to load history"** (`balance.load_error`) and a warning toast is shown.

---

#### `load-history` (GET)
- **When:** on opening the Balance tab (app.js:976).
- **Role/auth:** logged-in. If not `$auth`, server returns an empty array.
- **Request:** none (GET, no query params).
- **Response:** a JSON **array**, newest first (`ORDER BY id DESC LIMIT 100`). Each element (api.php:433):

| field | type | UI use |
|---|---|---|
| `time` | int (unix seconds) | `new Date(time*1000).toLocaleString()` → Date column |
| `type` | int | index into the label array above → Type column |
| `negative` | int (0/1) | 1 → red amount, 0 → green amount |
| `amount` | int (milli-VIZ) | `render_number(amount)+' Ƶ'` → Amount column |

- **UI states:** empty array → single row **"No records"** (`balance.no_records`).

> **VIZ thin-client note (suggestion):** history is a per-user ledger. On chain, the equivalent is the account's operation history — the thin client would query the node's `get_account_history`/custom-operation feed and map each op (transfer in/out, bet, liquidity op, leverage op) to these same type labels rather than reading a server table.

---

### Deposit (Top up) addon — `render_addon('deposit')`

Rendered inline when **"Top up"** is clicked (app.js:1304-1332). This is a **display-only** panel: no `deposit` API endpoint exists; funding is done by sending VIZ/TON to a well-known account and the backend credits the balance asynchronously (`balance.deposit_processing_note`). Two sub-sections:

**A) Convert TON or USDT** (`balance.deposit_ton_title`)
- Instruction `balance.deposit_ton_instruction`.
- **Recipient** (`balance.deposit_recipient`): a copyable input pre-filled with the constant `ton_deposit` (app.js:9 = `UQCyJpqQKDodPO_GyGyRmrJccp7-54idxXmdvnK1EbdnrPoD`).
- **Comment** (`balance.deposit_comment`): a copyable input pre-filled with `user['memo']` — this memo is how the backend attributes the incoming transfer to the user.
- If the `tonkeeper` bridge object exists: a **"Open transfer"** button (`balance.deposit_open_transfer`, class `tonkeeper-transfer`). Handler (app.js:3138) navigates to `ton://transfer/<ton_deposit>?text=<user.memo>` to open Tonkeeper.

**B) Top up VIZ** (`balance.deposit_viz_title`)
- Instruction `balance.deposit_viz_instruction`.
- **Recipient** copyable input = constant `viz_deposit` (app.js:8 = `forecaster`).
- **Comment** copyable input = `user['memo']`.
- If the `vizonator` bridge exists: three preset buttons **10 / 100 / 1000 VIZ** (class `vizonator-transfer`, `rel="10.000 VIZ"` etc.). Handler (app.js:3132) calls `vizonator_transfer(rel, el)` (app.js:1355) which invokes `vizonator.transfer({to:viz_deposit, amount, memo:user['memo'], force_memo_encoding:false}, cb)`; on success it adds `.btn-success` to the button. This is an **in-wallet-extension broadcast**, not a Forecaster API call.
- Footer rate note `balance.deposit_rate_note` (shows `viz_rate`).

Copyable inputs carry classes `select-all-action copy-action`; the click handler (app.js:3158) copies the value to clipboard and appends a ✔️.

> **VIZ thin-client note (suggestion):** the "VIZ top up" flow is literally a chain `transfer` operation to account `forecaster` with the user's memo. A native VIZ thin client would **sign & broadcast a `transfer` op** (from → `forecaster`, amount, memo = the deposit tag) directly via viz-js instead of showing a copyable address. The TON path is external and out of scope for the VIZ node.

---

### Withdraw addon — `render_addon('withdraw')`

Rendered when **"Withdraw"** is clicked (app.js:1334-1348). Two sub-sections:

**A) Withdraw VIZ to TON** (`balance.withdraw_ton_title`) — informational only: shows `balance.deposit_ton_token_note` ("The VIZ token launch on the TON network is planned for 2025."). No form, no endpoint.

**B) Withdraw VIZ** (`balance.withdraw_viz_title`) — the working form:
- Instruction `balance.withdraw_viz_instruction`.
- Input **Recipient** (`name=account`, placeholder `balance.withdraw_recipient_placeholder`).
- Input **Amount** (`name=amount`, placeholder `balance.withdraw_amount_placeholder`).
- Input **Comment** (`name=memo`, placeholder `balance.withdraw_memo_placeholder`).
- Button **"Confirm"** (`balance.withdraw_confirm`, class `withdraw-action`, `rel="viz"`).
- A `.loading` line showing `balance.withdraw_wait` while pending.

**Submit flow** (handler app.js:3091 → `withdraw()` app.js:1366): reads the three fields, POSTs to `withdraw-<rel>` i.e. `withdraw-viz`. On success the loading line shows `common.success` (green) and the Amount field is cleared; on error it shows `common.error_prefix + result.error` (red) and re-enables the button.

#### `withdraw-viz` (POST)
- **When:** user clicks **Confirm** in the Withdraw VIZ form.
- **Role/auth:** logged-in.
- **Request** (app.js:1375):

| field | type | meaning / server handling |
|---|---|---|
| `account` | string | recipient VIZ account; server lowercases and strips to `[a-z0-9\-\.]` (api.php:781) |
| `amount` | string | VIZ amount typed by user; server sanitizes, converts `,`→`.`, then `floor(value*1000)` → milli-VIZ |
| `memo` | string | free-text comment stored on the withdraw record |

- **Response** (api.php:798-814):

| field | type | UI use |
|---|---|---|
| `status` | bool | `true` → success path; `false` → error path. The client computes `error = !result.status` |
| `error` | string | present only when `status=false` (e.g. `"Недостаточно баланса"` / `"Withdraw failed"`); shown after `common.error_prefix` |

- **Server side effects:** inserts a `withdraw` row (`type=0`), decrements `users.balance` by `amount`, writes a `history` row `type=2` (`negative=1`). Withdrawals are queued and processed off-band.
- **UI/edge states:** if `amount >= balance` → `status=false, error="Недостаточно баланса"` (insufficient balance). A network/HTTP failure throws `try_check_session` inside `withdraw()` and the callback fires with `error=true`.

> **VIZ thin-client note (suggestion):** a withdrawal maps to a chain `transfer` from the platform custodial account to `account` with `memo`. The custodial model means the server signs; a fully non-custodial VIZ client would instead not need a "withdraw" concept at all (the user already holds the funds). If keeping custody, the thin client just needs to submit `{account, amount, memo}` and poll balance.

---

### Screen: My Positions & My Payouts (profile)

These two tables are rendered into `.my-positions` and `.my-payouts` containers by `load_positions()` (app.js:2883) and `load_payouts()` (app.js:2903), called when the profile/portfolio view loads.

#### `load-positions` (POST)
- **When:** profile render (`load_positions()`).
- **Role/auth:** logged-in (throws `Auth required` otherwise).
- **Request:** `{}` (no fields; user derived from session).
- **Response:** JSON **array** of the user's bets with `status IN (0,3)` (active or resolved), newest first, LIMIT 100. Each row joins market columns. Fields consumed by the UI (app.js:2888-2896):

| field | type | UI use |
|---|---|---|
| `market` | int | market id → link target (`clickable-market rel`) |
| `market_q` | string | market question → link text (falls back to `#<market>`) |
| `side` | int (0/1) | picks `market_a`/`market_b` as the outcome name (binary) |
| `market_a` / `market_b` | string | binary outcome labels |
| `market_status` | int | index into `market_status_labels` → Status column |
| `amount` | int (milli-VIZ) | Amount column (`render_number`) |
| `expected_payout` | int (milli-VIZ) | Expected column; only shown if `>0` |
| `expected_payout_approx` | int (0/1) | if `1`, prefixes the payout with **"≈ "** (floating parimutuel estimate); resolved markets show exact value |

Column headers: **Market / Outcome / Amount / Status / Expected** (`table.market`, `table.outcome`, `table.amount`, `table.status`, `table.expected`).

- **Empty/edge:** empty array → **"No active positions"** (`profile.no_active_positions`); a thrown error → generic `common.error`.
- (Server also returns `resolved_amount`, `outcome_index`, `weight`, `final_payout`, and many raw market fields; the positions table above only uses the subset listed. `final_payout` is `{amount,status}` or null.)

#### `load-payouts` (POST)
- **When:** profile render (`load_payouts()`).
- **Role/auth:** logged-in (throws `Auth required`).
- **Request:** `{}`.
- **Response:** JSON **array** of the user's `payouts` rows joined to market question, newest first LIMIT 100. Fields used (app.js:2908-2910):

| field | type | UI use |
|---|---|---|
| `market` | int | link target |
| `market_q` | string | link text (fallback `#<market>`) |
| `type` | int | index into `payout_type_labels` (see below) → Type column |
| `amount` | int (milli-VIZ) | Amount column |
| `status` | int | `1` → **"Paid"** (`payout.status_paid`), else **"Pending"** (`payout.status_pending`) |

Column headers: **Market / Type / Amount / Status** (`table.market`, `table.type`, `table.amount`, `table.status`).

**Payout type labels** (`payout.type_0..7`): 0 Win, 1 Liquidity, 2 Oracle, 3 Creator, 4 Committee, 5 Dispute refund, 6 Oracle penalty bonus, 7 Creator reward.

- **Empty:** **"No payouts"** (`profile.no_payouts`).

#### `load-market-payouts` (POST) — public per-market payouts
- **When:** *No direct app.js caller.* The public payouts table shown on a market detail page (**"Payouts (N)"** `payouts_public.title`, rendered at app.js:1849-1863) is populated from the `all_payouts` array **embedded in the `load-market` detail response**, not from this endpoint. `load-market-payouts` exists as a standalone endpoint the VIZ client may call directly.
- **Role/auth:** none required (public).
- **Request:** `{ market_id: int }`.
- **Response:** JSON **array** of `payouts` rows for that market (`ORDER BY id DESC LIMIT 200`), all columns. The market-detail public table uses per-row: `user` (+ optional `user_data.{username,first_name}` for display name), `type` (→ `payout_type_labels`), `amount`, `status` (0 Pending / 1 Paid / else Error via `payout.status_error`). Columns: **User / Type / Amount / Status**.

> **VIZ thin-client note (suggestion):** positions and payouts are derived state. On chain they correspond to the user's open bet operations and the settlement/payout operations emitted at market resolution. The thin client can reconstruct "My positions" from the user's bet ops filtered by market status, and "My payouts" from resolution payout ops — mirroring the `type` label taxonomy above.

---

### Liquidity provision (on the market detail page)

Liquidity UX lives on the **market detail** screen (not the wallet tab). Three related blocks are rendered by `load_market_detail`:

**1) Add-liquidity form** (app.js:1747-1755) — shown only when the market is active (`status==1`), the user is logged in, `user['add_liquidity']==1`, and betting has not expired. Contains:
- Heading **"Add liquidity"** (`liquidity.add_title`).
- Number input `name=liq_amount` (placeholder `liquidity.amount_placeholder` = "Amount VIZ", step 0.001).
- Button **"Add"** (`liquidity.add_button`, class `add-liquidity-action`).
- A `.liq-loading` line.

Submit (handler app.js:3262 → `add_liquidity_action()` app.js:2290): reads `liq_amount`, calls `add-liquidity`, on success shows **"Liquidity added!"** (`liquidity.added_success`, green), refreshes user data and reloads the market after 1s.

**2) My liquidity** (app.js:1794-1803) — shown when the market-detail response contains `data.my_liquidity[]`. For each position renders a `liquidity-card`: **"Amount: %AMOUNT% Ƶ, Status: %STATUS%"** (`liquidity.amount_display`), plus **", Earned: %AMOUNT% Ƶ"** (`liquidity.earned_display`) when `earned_fee>0`. Status labels array: 0 Active (`liquidity.status_active`), 1 "—" (`liquidity.status_1`), 2 Returned (`liquidity.status_returned`), 3 Resolved (`liquidity.status_resolved`). Fields consumed: `amount`, `status`, `earned_fee`.

**3) All liquidity (public)** (app.js:1830-1846) — public providers table **"Liquidity providers (N)"** (`liquidity.providers_title`). Columns **User / Amount / Time to expir. / Earned / Status** (`liquidity.table_*`). Per row uses `user` (+ `user_data`), `amount`, `sec_to_expiration` (rendered as hours if >3600s else seconds, using `liquidity.hours`/`liquidity.seconds`), `earned_fee`, `status`.

The LP creation `lp_reward` (`market.lp_reward`) fee is set at market-create time (hidden `liquidity_fee=5`, i.e. 0.5%) — see the create-market screen; LPs earn a share of that fee, surfaced as `earned_fee` above.

#### `add-liquidity` (POST)
- **When:** user clicks **Add** in the add-liquidity form.
- **Role/auth:** logged-in **and** `user_arr.add_liquidity==1` (server throws `No liquidity permission` otherwise).
- **Request** (app.js:2296):

| field | type | meaning |
|---|---|---|
| `market_id` | int | target market |
| `amount` | string/number | VIZ amount; server does `floor(value*1000)` → milli-VIZ |

- **Response** (api.php:1537):

| field | type | UI use |
|---|---|---|
| `status` | bool `true` | success (the client only inspects thrown errors; a 400 throws) |
| `liquidity_id` | int | id of the new `liquidity` row (not displayed) |

- **Server side effects:** splits `amount` across `reserve_a`/`reserve_b` proportionally, updates market reserves/`k`/`liquidity_sum`, decrements balance, writes `history` type=6, and a `market_log` `liquidity_add` row.
- **Edge/errors thrown (shown via toast/loading line):** `Invalid amount`, `Market not found`, `Market is not active`, `Betting period expired`, `Insufficient balance`.

#### `withdraw-liquidity` (POST)
- **When:** *No app.js caller wires this button in the reviewed code* — the endpoint is fully implemented server-side and is available for the VIZ client to call (LPs currently see their positions read-only in "My liquidity"). Documented here for coverage.
- **Role/auth:** logged-in; the position must belong to the caller and be `status==0`.
- **Request:**

| field | type | meaning |
|---|---|---|
| `liquidity_id` | int | the `liquidity` row to withdraw |
| `withdraw_amount` | string/number (optional) | VIZ to withdraw; server `floor(value*1000)`. If `<=0` or `>=` the position amount → **full** withdrawal; otherwise partial with proportional weight reduction |

- **Response** (api.php:1643-1650):

| field | type | meaning |
|---|---|---|
| `status` | bool `true` | success |
| `returned` | int (milli-VIZ) | principal withdrawn + `fee_share` returned to balance |
| `fee_share` | int (milli-VIZ) | time-weighted fee earned on the withdrawn amount |
| `time_ratio` | float | fraction of market duration the LP was in (fee discount factor) |
| `is_full` | bool | whether the whole position was withdrawn |
| `remaining_amount` | int | present only when partial: leftover LP principal |

- **Edge/errors:** `Liquidity position not found`, `Not your liquidity position`, `Liquidity position is not active`, `Market is not active`, `Betting period expired, withdrawal not possible`, `Withdrawal amount too small`, `Cannot withdraw: would deplete reserves`, `Cannot withdraw: would reduce market liquidity below minimum (100 VIZ)`.

> **VIZ thin-client note (suggestion):** add/withdraw liquidity are custom market operations that adjust AMM reserves. On chain they'd be two custom ops (`liquidity_add` / `liquidity_withdraw`) carrying `{market_id, amount}` / `{liquidity_id, withdraw_amount}`; the node computes reserve split and fee share deterministically. The thin client should surface a Withdraw button per "My liquidity" card wired to `withdraw-liquidity`.

---

### Lazy pool (endpoints only — no UI wired in reviewed app.js)

The **lazy pool** is a shared yield pool: users deposit VIZ for locked shares that earn rewards and back leverage loans. The four endpoints below are fully implemented in `module/api.php` but **have no caller in the reviewed `app.js`** (the Boost feature reads leverage-fund availability indirectly via `leverage-preview`). They are documented so the VIZ client can build the lazy-pool screen. All require login (`Auth required`), except `lazy-pool-info` which is partly public.

#### `lazy-pool-deposit` (POST)
- **Request:** `{ amount: string/number }` — VIZ, server `floor(value*1000)` → milli-VIZ.
- **Response** (api.php:2487-2491): `status` (bool), `deposit_id` (int), `shares` (int, minted shares — 1:1 on first pool deposit else pro-rata to `free_balance`), `unlock_time` (unix, = now + lock_days×86400), `lock_days` (int, from settings, default 30).
- **Side effects:** debits `users.balance`, credits `lazy_pool_balance`/`lazy_pool_shares`, inserts locked `lazy_pool_deposits` row (`status=0`), history type=20.
- **Errors:** `Invalid amount`, `Insufficient balance`, `Pool has no free balance`, `Deposit too small`.

#### `lazy-pool-withdraw` (POST)
- **Request:** `{ withdraw_percent: float (optional, default 100; clamped 0<..<=100) }`. Withdraws from the user's single **unlocked** deposit record (`status=1`).
- **Response** (api.php:2570-2575): `status`, `payout` (int milli-VIZ, = share value + reward portion), `share_value` (int), `reward_portion` (int), `shares_burned` (int), `is_full` (bool).
- **Side effects:** burns shares, credits balance, history type=21.
- **Errors:** `No unlocked shares available for withdrawal`, `Nothing to withdraw`, `Pool is empty`, `Insufficient pool free balance`.

#### `lazy-pool-emergency-withdraw` (POST)
- **Request:** `{}` — withdraws **all** of the user's shares (locked + unlocked) immediately, paying a penalty on the profit attributable to still-locked shares.
- **Response** (api.php:2651-2656): `status`, `total_payout` (int milli-VIZ), `penalty` (int), `penalty_percent` (int, from settings), `locked_shares` (int), `profit` (int).
- **Side effects:** marks all deposits `status=3`, penalty stays in pool (raises `reward_per_share` for remaining participants), zeroes the user's lazy-pool columns, history type=22.
- **Errors:** `No shares in lazy pool`.

#### `lazy-pool-info` (POST)
- **Role/auth:** the `pool` and `active_allocations` sections are **public**; `user`/`deposits` sections and `oracle_penalty` are only added when `$auth`.
- **Request:** `{}`.
- **Response** (api.php:2666-2723):
  - `pool`: `{ total_shares, free_balance, allocated_balance, total_pool_value (=free+allocated), reward_per_share }` (all int).
  - `active_allocations[]`: per market allocation `{ market, amount, original_amount, recalled_amount, check_step, last_check_time, market_bets_sum }`.
  - `oracle_penalty` (only if caller `is_oracle`): `{ active_stamps, oracle_multiplier }`.
  - `user` (auth only): `{ lazy_pool_shares, lazy_pool_balance, lazy_pool_reward_snapshot, lazy_pool_pending_rewards }`.
  - `deposits[]` (auth only): each `{ id, amount, shares, unlock_time, status (0 locked / 1 unlocked), time }`.
  - `status`: `true`.

> **VIZ thin-client note (suggestion):** the lazy pool is a share-accounting vault (deposit → shares, withdraw after lock, emergency-exit with penalty). On chain these map to custom vault ops; the node holds `total_shares`/`free_balance`/`reward_per_share` as global state and the per-user share/lock records. The thin client would render a pool dashboard from `lazy-pool-info` and offer deposit/withdraw/emergency actions.

---

### Boost / Leverage (on the market detail page)

Boost lets a user open a **leveraged bet** funded partly by a loan from the lazy pool. All UI lives on the market detail page. The Boost form renders (`render_boost_form`, app.js:2313) only when the market is active (`status==1`), the user is logged in, and betting hasn't expired (app.js:1742). The user's active boosted positions render separately in the profile via `load_boost_info()` (app.js:2651).

**Boost form contents** (`render_boost_form`):
- Heading 🚀 **"Boost Your Bet"** (`boost.title`).
- Outcome selector: for multi markets a `select[name=boost_outcome]` of outcome labels; for binary a `select[name=boost_side]` (A/B).
- **"Your collateral:"** (`boost.your_collateral`) number input `name=boost_collateral` (placeholder `boost.collateral_placeholder`, step 0.001).
- **Boost slider** `input[type=range name=boost_leverage]` in **0.01× units** (100 = 1.00×), min 100, initially disabled/max 100. Labels **"no boost"** … **1.00×** … **"max available"** (`boost.no_boost`, `boost.max_available`). A CSS `--danger-start` marks the danger zone.
- A `.boost-detail-box` populated after preview.
- **"Slippage tolerance:"** (`boost.slippage_tolerance`) chips 0.5% / 1% / 2% / 3% (`slippage-chip`, default 2% active; handler app.js:3346 toggles `.active`).
- **Auto-close acknowledgment** checkbox `name=boost_auto_close_ack` (label text `boost.auto_close_checkbox` with the auto-close DATE and buffer HOURS). Must be checked to enable Open.
- **"Open Boosted Position"** button (`boost.open_button`, class `boost-open-action`, starts `disabled`).

**Live preview:** whenever collateral changes, `boost_fetch_preview(form)` (app.js:2353) calls `leverage-preview`. The response either drives the slider/detail box or shows an **"Boost unavailable"** panel (`boost.unavailable`, reason from `failed_constraints`).

As the slider moves, `boost_render_detail` (app.js:2406) interpolates between `slider_stops` and shows a table: **Pool loan / interest / Total bet / Current position value / Auto-close at** (`boost.pool_loan`, `boost.interest`, `boost.total_bet`, `boost.current_value`, `boost.auto_close_at`) plus a risk note (`boost.risk_note`), info note (`boost.info_note`), safety note (`boost.safety_note`), and a **"Danger zone"** warning (`boost.danger_zone`) when leverage ≥ `danger_zone_leverage`.

#### `leverage-preview` (POST)
- **When:** collateral input changes (`boost_fetch_preview`).
- **Role/auth:** logged-in.
- **Request** (app.js:2363):

| field | type | meaning |
|---|---|---|
| `market_id` | int | target market |
| `collateral` | int | `Math.floor(collateral_VIZ*1000)` (client already multiplies; server multiplies again by 1000 — see edge note) |
| `outcome_index` | int | chosen outcome (0/1 for binary) |

- **Response** (api.php:3437; body = `ok:true` plus all keys from `leverage_compute_max_leverage`). Fields the UI consumes (app.js:2382-2450):

| field | type | UI use |
|---|---|---|
| `unavailable_reason` | string (optional) | if present → render "Boost unavailable" panel, disable slider/open |
| `failed_constraints[]` | array of `{constraint, reason}` (optional) | reason text via `boost_format_failed_constraints` (priority: `min_market_liquidity` > `market_liquidity` > `fund_availability` > `position_size`); full list in a `<details>` |
| `max_leverage` | int (0.01 units) | slider max; `100` default |
| `danger_zone_leverage` | int (0.01 units, optional) | computes `--danger-start` CSS %; danger warning threshold |
| `auto_close_date` | string | shown in auto-close checkbox and notes |
| `expiration_buffer_hours` | int | hours-before-resolution shown in notes (default 24) |
| `safety_margin_percent` | number | shown in safety note (default 1) |
| `liquidation_threshold` | int | fallback threshold for interpolated stops |
| `slider_stops[]` | array of `{leverage_01, loan, total_bet, pool_profit, current_cancel_value, liquidation_threshold, expected_tokens}` | per-leverage detail table + slippage baseline |

> **Edge note:** the client sends `collateral` **already ×1000** (app.js:2363) and the server does `floor(floatval(collateral)*1000)` **again** (api.php:3424). Treat this as an existing quirk to reconcile in the new client (likely the intended contract is raw VIZ in, ×1000 on server).

#### `leverage-open` (POST)
- **When:** user clicks **Open Boosted Position** (`boost_open_action`, app.js:2476).
- **Role/auth:** logged-in.
- **Request** (app.js:2493):

| field | type | meaning |
|---|---|---|
| `market_id` | int | target market |
| `outcome_index` | int | chosen outcome |
| `collateral` | int | `Math.floor(collateral_VIZ*1000)` (server multiplies again by 1000) |
| `leverage` | float | selected leverage as X.XX (from slider/100); server requires ≥1.01 |
| `max_slippage_percent` | float | from the active slippage chip (default 2) |

- **Response** (api.php:3545-3551): `ok`, `position_id` (int), `bet_id` (int), `tokens` (int), `loan` (int milli-VIZ), `total_bet` (int milli-VIZ), `liquidation_threshold` (int). On success the loading line shows **"Boosted position opened! Total bet: %AMOUNT% Ƶ"** (`boost.opened_success`) using `total_bet`, refreshes user data, reloads market after 1s.
- **Side effects:** places the underlying bet (CPMM or LMSR), inserts a `leveraged_positions` row, debits collateral from balance, draws `loan` from the lazy pool (`leverage_fund_used += loan`), history type=30.
- **Errors thrown (shown red):** `Invalid collateral`, `Leverage too low (min 1.01x)`, `Market is not active`, `Too close to expiration for leverage`, `Insufficient balance`, `Requested leverage Nx exceeds maximum Mx`, `Trade too small`, slippage failure (`Tokens received (...) below minimum (...). Slippage too high.`), or safety failure (`Position fails safety check: cancel_value < liquidation_threshold`).

#### Active boosts table & actions — `load_boost_info()`

`leverage-info` populates the **"Your Active Boosts"** table (`boost.info_title`) in `.boost-positions-container` (app.js:2667-2704). Columns: **Market / Outcome / Boost / Collateral / Value / Auto-closes: / Actions** (`boost.info_*`). Each row shows leverage `= total_bet/collateral` as `N.N×`, collateral, `cancel_value`, and the auto-close date + live countdown (`boost.info_countdown`, `format_countdown`). Two action buttons per row: **"Close"** (`boost.info_close_btn`, class `boost-close-btn`) and **"Convert"** (`boost.info_convert_btn`, class `boost-convert-btn`). It also fills `boost_active_market_ids` so market cards can show a boost banner. Empty → **"No active boosted positions"** (`boost.info_no_positions`).

#### `leverage-info` (POST)
- **When:** on profile/market render (`load_boost_info`, `select_tab` app.js:520).
- **Role/auth:** logged-in.
- **Request:** `{}`.
- **Response** (api.php:3757-3759): `ok`, `active_count` (int), and `positions[]` where each (api.php:3741):

| field | type | UI use |
|---|---|---|
| `position_id` | int | data attr for Close/Convert buttons |
| `market_id` | int | market link `#<id>` |
| `outcome_index` | int | Outcome column |
| `collateral` | int | Collateral column; also `leverage = total_bet/collateral` |
| `total_bet` | int | leverage numerator |
| `leverage` | float | (also provided server-side, 2dp) |
| `cancel_value` | int | Value column |
| `liquidation_threshold` | int | (context) |
| `current_profit` | int | (context) |
| `auto_close_date` | string/null | Auto-closes column |
| `countdown_seconds` | int | live countdown; also drives per-market boost banners |
| `open_time` | int | (context) |

#### Close flow — `leverage-close-preview` → `leverage-close`

Clicking **Close** calls `boost_close_action(position_id)` (app.js:2502) → `leverage-close-preview`, then renders a modal **"Close Boosted Position"** (`boost.close_title`) with a warning (`boost.close_warning`) and a breakdown table.

##### `leverage-close-preview` (POST)
- **Request:** `{ position_id: int }`.
- **Response** (api.php:3577-3591). Fields the modal uses (app.js:2507-2551):

| field | type | UI use |
|---|---|---|
| `cancel_value` | int | **"Position value (cancel):"** (`boost.close_position_value`) |
| `pool_obligation` | int | **"Pool receives:"** (`boost.close_pool_receives`) |
| `bettor_receives` | int | **"You receive:"** (`boost.close_you_receive`); also becomes `data-min-return` on confirm |
| `collateral` | int | **"Your collateral:"** (`boost.close_your_collateral`) |
| `loss` *(read as `data.loss`)* | int | drives loss section & `loss_pct`; NOTE server key is `loss_vs_collateral` (see edge) |
| `leverage` | number | in warning text; NOTE server does not return `leverage` here (see edge) |
| `loan_repayment` | int (optional) | **"Loan repayment:"** (`boost.close_loan_repayment`) — server does not return this key (see edge) |
| `pool_profit_charge` | int (optional) | **"Pool profit:"** (`boost.close_pool_profit`) — server does not return top-level; it's inside `loss_breakdown` |
| `loss_breakdown.market_loss` | int | **"Market moved against you:"** (`boost.close_market_moved`); server key is `market_movement_loss` (see edge) |
| `loss_breakdown.pool_profit_charge` | int | **"Pool profit charge:"** (`boost.close_profit_charge`) + 1× comparison (`boost.close_comparison`) |

> **Edge note (field-name drift to reconcile):** the modal reads `data.loss`, `data.leverage`, `data.loan_repayment`, `data.pool_profit_charge` and `loss_breakdown.market_loss`, but the server (api.php:3579-3591) returns `loss_vs_collateral`, no `leverage`, no `loan_repayment`, `loss_breakdown.market_movement_loss`, and `loss_breakdown.pool_profit_charge`/`total_loss`/`total_loss_pct`. The new VIZ client should align on the server names (or the server should be adjusted) — several close-modal figures currently read as `0`/`undefined`.

On confirm, **"Confirm Close"** (`boost.close_confirm_button`) carries `data-position` and `data-min-return` (= `bettor_receives`) → `boost_close_confirmed` (app.js:2568).

##### `leverage-close` (POST)
- **Request** (app.js:2574): `{ position_id: int, min_return: int }` (`min_return` from the preview's `bettor_receives`; a slippage floor).
- **Response** (api.php:3623-3627): `ok`, `pool_received` (int), `bettor_received` (int), `cancel_value` (int), optional `warning` ("Position was already closed"). On success shows **"Boosted position closed. You received: %AMOUNT% Ƶ"** (`boost.close_success`) using `bettor_received`, refreshes user data, reloads market.
- **Errors:** `Position not found`, `Position is not active`, voluntary-close guard (`Position cannot be voluntarily closed: cancel_value < liquidation_threshold. Position will be liquidated by the protocol.`), or `Return amount (...) below minimum (...)`.

#### Convert flow — `leverage-convert-preview` → `leverage-convert`

Clicking **Convert** calls `boost_convert_action(position_id)` (app.js:2586) → `leverage-convert-preview`, then renders modal 🔄 **"Convert Boost to Normal Bet"** (`boost.convert_title`).

##### `leverage-convert-preview` (POST)
- **Request:** `{ position_id: int }`.
- **Response** (api.php:3652-3660). Fields used by the modal (app.js:2590-2611):

| field | type | UI use |
|---|---|---|
| `cancel_value` | int | **"Current position value:"** (`boost.convert_current_value`) |
| `current_profit` | int | **"Your unrealized profit:"** (`boost.convert_unrealized_profit`) — read as `data.current_profit` |
| `pool_obligation` | int | **"Loan + pool profit:"** (`boost.convert_loan_profit`) |
| `conversion_fee` | int | **"Conversion fee (PCT%):"** (`boost.convert_fee`) |
| `total_user_payment` | int | **"Total:"** (`boost.convert_total`) — read as `data.total_user_payment` |
| `conversion_profit_cost` | number | fee % shown, and echoed back on confirm (`data-conv-pct`) |
| `leverage` | number | intro text — NOTE server does not return `leverage` here |

The modal lists post-conversion effects (`boost.convert_after_*`) and a risk warning (`boost.convert_risk`). Confirm button **"Convert to Normal Bet"** (`boost.convert_confirm_button`) carries `data-conv-pct` → `boost_convert_confirmed` (app.js:2633).

##### `leverage-convert` (POST)
- **Request** (app.js:2639): `{ position_id: int, conversion_profit_cost: number }` (must match server setting within 0.01 or it throws `conversion_profit_cost mismatch`).
- **Response** (api.php:3716-3720): `ok`, `position_id`, `total_paid` (int), `conversion_fee` (int), `pool_profit` (int). On success shows **"Position converted to normal bet!"** (`boost.convert_success`), refreshes user data, reloads market.
- **Side effects:** marks position `status=5`, debits `total_user_payment` from balance, repays loan to pool, keeps the underlying bet as a normal 100%-owned bet, history type=34.
- **Errors:** `Position not found`, `Position is not active`, `Position has no unrealized profit. Conversion not available.`, `Insufficient balance for conversion. Need: N VIZ`.

#### `leverage-pool-state` (POST)
- **When:** *No app.js caller in the reviewed code* — available for a leverage/pool dashboard.
- **Role/auth:** logged-in.
- **Request:** `{}`.
- **Response** (api.php:3767-3772): `ok`, `leverage_fund_total`, `leverage_fund_available`, `leverage_fund_used`, `free_balance`, `earned_balance`, `free_amount` (= `free_balance − leverage_fund_used`) — all int milli-VIZ.

> **VIZ thin-client note (suggestion):** a boosted position = a normal bet op + a loan drawn from the lazy pool, plus a protocol-enforced liquidation threshold. On chain the client would (1) query `leverage-preview` equivalent (deterministic max-leverage/quote from reserves + pool free fund), (2) broadcast an `open_leveraged_position` op `{market_id, outcome_index, collateral, leverage, max_slippage}`, and (3) later a `close`/`convert` op referencing `position_id`. The node enforces the safety check (`cancel_value ≥ liquidation_threshold`) and auto-liquidation at `auto_close_date`. Reconcile the preview/close field names noted above before implementing.
## 10. Master API / transaction coverage matrix

Every `/api/<endpoint>/` call the current client makes, grouped by area. Node developers should map each **write** endpoint to a signed VIZ operation (or off-chain-then-settled action) and each **read** endpoint to a chain/index query, and tick that all are covered. `M` = HTTP method. `Role`: A=any authenticated, P=player, C=creator, O=oracle, L=LP, D=disputer, J=arbitrator/committee/DAO, G=guest/unauth ok. Detailed request/response contracts are in the referenced section.

### 10.1 Session, identity, config, preferences (§2)

| Endpoint | M | Kind | Role | Fires when | § |
|----------|---|------|------|-----------|---|
| `check-session` | POST | write | G | Telegram initData present on load | 2 |
| `auth-code` | GET | read | G | Website login (no Telegram) — get one-time code | 2 |
| `load-session` | POST | read | A | Every app load, and after any balance-changing action (`update_user_data`) | 2 |
| `load-settings` | GET | read | G | App boot | 2 |
| `update-user-preferences` | POST | write | A | Save jurisdiction/country + language | 2 |
| `register-oracle` | POST | write | A→O | Become / update oracle settings | 2, 7 |
| `register-creator` | POST | write | A→C | Become market creator | 2, 6 |
| `load-oracle-profile` | POST | read | G | Open oracle picker / oracle public profile | 2, 6, 7 |

### 10.2 Browse & filter (§3)

| Endpoint | M | Kind | Role | Fires when | § |
|----------|---|------|------|-----------|---|
| `load-markets` | POST | read | G | Market list load / filter / paginate | 3 |
| `load-markets-catalog` | POST | read | G | Catalog/enriched list variant | 3 |
| `load-categories` | POST | read | G | App boot (filter chips + create form) | 3 |
| `load-oracles` | GET | read | G | Create form oracle dropdown | 3, 6 |
| `load-committees` | GET | read | G | Create form committee dropdown | 3, 6, 8 |

### 10.3 Market detail & binary betting (§4)

| Endpoint | M | Kind | Role | Fires when | § |
|----------|---|------|------|-----------|---|
| `load-market-enriched` | POST | read | G | Open market detail | 4 |
| `load-market` | POST | read | G | Detail refresh / lightweight reload | 4 |
| `place-bet` | POST | write | P | Place binary bet (instant / batch) | 4 |
| `commit-bet` | POST | write | P | Hidden bet — commit phase (client hash) | 4 |
| `reveal-bet` | POST | write | P | Hidden bet — reveal phase | 4 |
| `cancel-bet` | POST | write | P | Cancel a binary bet | 4 |
| `transfer-position` | POST | write | P | Transfer a position to another user | 4 |

### 10.4 Multi-outcome markets (§5)

| Endpoint | M | Kind | Role | Fires when | § |
|----------|---|------|------|-----------|---|
| `create-market-multi` | POST | write | C | Create Onix Multi market | 5, 6 |
| `place-bet-multi` | POST | write | P | Bet on an outcome index | 5 |
| `cancel-bet-multi` | POST | write | P | Cancel a multi bet | 5 |
| `add-liquidity-multi` | POST | write | L | Add LP to a multi market | 5, 9 |
| `withdraw-liquidity-multi` | POST | write | L | Withdraw LP from a multi market | 5, 9 |
| `resolve-market-multi` | POST | write | O | Oracle resolves winning outcome index | 5, 7 |

### 10.5 Market creation (§6)

| Endpoint | M | Kind | Role | Fires when | § |
|----------|---|------|------|-----------|---|
| `create-market` | POST | write | C | Create Onix Binary market | 6 |

### 10.6 Oracle role & resolution (§7)

| Endpoint | M | Kind | Role | Fires when | § |
|----------|---|------|------|-----------|---|
| `oracle-deposit-insurance` | POST | write | O | Fund oracle insurance | 7 |
| `oracle-withdraw-insurance` | POST | write | O | Withdraw oracle insurance | 7 |
| `oracle-accept-market` | POST | write | O | Accept an assigned market | 7 |
| `oracle-reject-market` | POST | write | O | Reject an assigned market | 7 |
| `load-pending-markets` | POST | read | O | Oracle's pending-acceptance queue | 7 |
| `resolve-market` | POST | write | O | Resolve binary market outcome | 7 |
| `oracle-no-contest` | POST | write | O | Declare no-contest (refund) | 7 |

### 10.7 Disputes & governance (§8)

| Endpoint | M | Kind | Role | Fires when | § |
|----------|---|------|------|-----------|---|
| `create-dispute` | POST | write | D | Bettor opens a dispute | 8 |
| `dispute-oracle-response` | POST | write | O | Oracle rebuts a dispute | 8 |
| `resolve-dispute` | POST | write | J | Committee/DAO/private resolver decides | 8 |
| `unban-user` | POST | write | J | Governance lifts a ban | 8 |

### 10.8 Wallet, history, positions, LP, leverage (§9)

| Endpoint | M | Kind | Role | Fires when | § |
|----------|---|------|------|-----------|---|
| `load-history` | GET | read | A | Balance tab transaction history | 9 |
| `load-positions` | POST | read | P | "My positions" | 9 |
| `load-payouts` | POST | read | P | "My payouts" | 9 |
| `load-market-payouts` | POST | read | G | Per-market payout breakdown | 9 |
| `withdraw-viz` | POST | write | A | Withdraw VIZ to external address | 9 |
| `add-liquidity` | POST | write | L | Add LP (binary) | 9 |
| `withdraw-liquidity` | POST | write | L | Withdraw LP (binary) | 9 |
| `lazy-pool-deposit` | POST | write | L | Lazy pool deposit | 9 |
| `lazy-pool-withdraw` | POST | write | L | Lazy pool withdraw | 9 |
| `lazy-pool-emergency-withdraw` | POST | write | L | Lazy pool emergency exit | 9 |
| `lazy-pool-info` | POST | read | A | Lazy pool state for user | 9 |
| `leverage-preview` | POST | read | L | Preview opening a boost | 9 |
| `leverage-open` | POST | write | L | Open a boosted position | 9 |
| `leverage-close-preview` | POST | read | L | Preview closing a boost | 9 |
| `leverage-close` | POST | write | L | Close a boosted position | 9 |
| `leverage-convert-preview` | POST | read | L | Preview convert boost→bet | 9 |
| `leverage-convert` | POST | write | L | Convert boost to plain bet | 9 |
| `leverage-info` | POST | read | A | User's boost positions (card banners) | 9 |
| `leverage-pool-state` | POST | read | G | Leverage pool global state | 9 |

**Total: 58 endpoints** (39 write / transaction-bearing, 19 read/query).

### 10.9 Transaction-type ledger (history)

The balance history renders 17 core types plus 5 boost types (`balance.history_type_*`). Each is a settled balance movement the thin client must be able to reconstruct from chain/indexer events:

`0` VIZ Deposit · `1` TON/USDT Conversion · `2` Withdrawal · `3` Bet · `4` Bet cancellation · `5` Bet result · `6` Liquidity provision · `7` Liquidity result · `8` Oracle registration · `9` Oracle insurance deposit · `10` Oracle insurance withdrawal · `11` Creator registration · `12` Dispute payment · `13` Dispute refund · `14` Oracle penalty · `15` Committee reward · `16` Payout · `30` Boost opened · `31` Boost liquidated · `32` Boost resolved (won) · `33` Boost resolved (lost) · `34` Boost converted to bet.
