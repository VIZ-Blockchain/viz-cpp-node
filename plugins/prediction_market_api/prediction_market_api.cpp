#include <boost/program_options/options_description.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <graphene/plugins/prediction_market_api/prediction_market_api.hpp>
#include <graphene/plugins/prediction_market_api/meta_parse.hpp>
#include <graphene/plugins/prediction_market_api/kline_object.hpp>
#include <graphene/chain/index.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/pm_objects.hpp>
#include <graphene/chain/pm/leverage.hpp>
#include <graphene/chain/validator_objects.hpp>
#include <graphene/chain/operation_notification.hpp>
#include <graphene/protocol/pm_operations.hpp>
#include <graphene/protocol/pm_virtual_operations.hpp>

#include <fc/uint128_t.hpp>
#include <fc/io/json.hpp>
#include <fc/variant_object.hpp>

#include <algorithm>
#include <limits>
#include <map>

#define CHECK_ARG_SIZE(_S)                                 \
   FC_ASSERT(                                              \
       args.args->size() == _S,                            \
       "Expected #_S argument(s), was ${n}",               \
       ("n", args.args->size()) );

#define CHECK_ARG_MIN_SIZE(_S, _M)                         \
   FC_ASSERT(                                              \
       args.args->size() >= _S && args.args->size() <= _M, \
       "Expected #_S (maximum #_M) argument(s), was ${n}", \
       ("n", args.args->size()) );

#define GET_OPTIONAL_ARG(_I, _T, _D)   \
   (args.args->size() > _I) ?          \
   (args.args->at(_I).as<_T>()) :      \
   static_cast<_T>(_D)

// Batch-D DoS guards (adversarial review 2026-08-14). The webserver response cache only hits
// byte-identical requests; an attacker walks `from` (+1 per call) to miss it every time, so the
// work itself must be bounded, not just the page:
//  - MAX_PM_PAGE_FROM: no skip-walk deeper than this (limit caps the page, from caps the skip).
//  - MAX_PM_SORT_POOL: volume/expiration sorts materialize the whole matching set before sorting;
//    above this size the listing degrades to newest-first paging instead of sorting.
static const uint32_t MAX_PM_PAGE_FROM = 1000000u;
static const uint32_t MAX_PM_SORT_POOL = 32768u;

namespace graphene { namespace plugins { namespace prediction_market_api {

    namespace {

        // Non-consensus reliability score in basis points [0..10000]. Blends resolution accuracy,
        // dispute verdicts, responsiveness and timeliness, then docks penalty stamps and bans.
        // reliability_score v2 (display-only, bp 0..10000). Weaves in 12 of the 14 oracle counters
        // (v1 used only 5): four reputation ratios blended, minus time-decayed penalty stamps and
        // bans, then confidence-shrunk toward a neutral prior for oracles with a thin track record.
        // Non-consensus; tune the weights freely. Timeliness (on-time resolution ratio derived from
        // resolved_late_count) is the 4th ratio; avg_resolution_time stays out — it measures latency
        // from betting close, not deadline overrun, and can't be normalized without the market length.
        uint32_t reliability_score(const pm_oracle_object& o, fc::time_point_sec now) {
            // (1) Resolution accuracy — resolved vs missed-deadline. Optimistic when unproven.
            uint64_t completed = (uint64_t)o.markets_resolved + o.missed_count;
            int64_t acc = completed ? (int64_t)((uint64_t)o.markets_resolved * 10000 / completed) : 10000;
            // (2) Dispute verdicts — upheld (won) vs overturned (lost).
            int64_t dtot = (int64_t)o.disputes_won + o.disputes_lost;
            int64_t drep = dtot ? (int64_t)((uint64_t)o.disputes_won * 10000 / (uint64_t)dtot) : 10000;
            // (3) Dispute responsiveness — of disputes filed, how many the oracle engaged rather than
            // ignored into auto-close (dispute_responses_missed counts the ignored ones).
            int64_t drecv = (int64_t)o.disputes_received;
            int64_t missed = (int64_t)o.dispute_responses_missed; if (missed > drecv) missed = drecv;
            int64_t resp = drecv > 0 ? (int64_t)((uint64_t)(drecv - missed) * 10000 / (uint64_t)drecv) : 10000;
            // (4) Timeliness — of the resolutions delivered, the share that landed by the advertised
            // deadline (resolved_late_count is the past-deadline tally). Late-but-delivered resolves
            // otherwise earn full accuracy credit, so without this a chronically-late oracle scores
            // identical to a punctual one. A ratio (not the raw count) keeps it fair to high volume.
            // Optimistic 10000 until the oracle has resolved anything.
            int64_t rres = (int64_t)o.markets_resolved;
            int64_t late = (int64_t)o.resolved_late_count; if (late > rres) late = rres;
            int64_t timely = rres > 0 ? (int64_t)((uint64_t)(rres - late) * 10000 / (uint64_t)rres) : 10000;

            // Weighted blend: accuracy 40% · verdicts 30% · responsiveness 15% · timeliness 15%.
            int64_t score = (acc * 40 + drep * 30 + resp * 15 + timely * 15) / 100;

            // Time-decayed penalty stamps: 300 bp each, halved per 10 days since the most recent stamp
            // (mirrors the object's last_penalty_stamp_time 10-day decay note). disputes_auto_closed is
            // already folded into penalty_stamps at slash time, so it is not double-charged here.
            // penalty_stamps is a LIFETIME cumulative counter (one per zero-volume resolution, pm_evaluator
            // §4.10) — on a busy oracle it reaches tens of thousands, so the raw count MUST be capped or
            // the linear cost buries the score at 0. Cap the effective stamps the same way the lazy-alloc
            // consumer does (pm_evaluator ~line 740 caps at 4) so both readers agree: max 4×300 = 1200 bp.
            if (o.penalty_stamps > 0) {
                int64_t stamps = o.penalty_stamps > 4 ? 4 : (int64_t)o.penalty_stamps;
                int64_t cost = stamps * 300;
                int64_t age  = (int64_t)now.sec_since_epoch() - (int64_t)o.last_penalty_stamp_time.sec_since_epoch();
                if (age < 0) age = 0;
                int64_t halvings = age / 864000; if (halvings > 16) halvings = 16;
                cost >>= halvings;
                score -= cost;
            }
            // Bans are severe and lasting.
            score -= (int64_t)o.bans_received * 1500;

            // Confidence shrink: a thin track record shouldn't sit at a hard 100 (or crater on one
            // dispute). Blend toward a neutral 6000 prior until ~20 markets have been resolved.
            int64_t n = (int64_t)o.markets_resolved;
            const int64_t FULL = 20;
            if (n < FULL) score = (score * n + 6000 * (FULL - n)) / FULL;

            if (score < 0) score = 0;
            if (score > 10000) score = 10000;
            return (uint32_t)score;
        }

        // This oracle's status-1 markets whose betting has closed (betting_expiration in
        // (epoch, head_block_time]) — i.e. awaiting the oracle's resolution: their count and the age
        // (seconds since betting close) of the OLDEST such market. Walks only the oracle's own active
        // set via by_oracle_status(owner, 1), so it is O(this oracle's active markets), not the global
        // closed-market prefix. Display-only; both are time-dependent so they're computed on read.
        struct oracle_awaiting_info { uint32_t count = 0; uint32_t oldest_age = 0; };
        oracle_awaiting_info oracle_awaiting(const database& db, const account_name_type& owner) {
            // D5: cached per (head block, oracle) — the walk is O(this oracle's active markets) and
            // get_market_full / get_oracle used to redo it on every call (winner_agg pattern).
            struct oa_entry { uint32_t epoch = 0; oracle_awaiting_info v; };
            static thread_local std::map<account_name_type, oa_entry> cache;
            const uint32_t epoch = db.head_block_num();
            auto cit = cache.find(owner);
            if (cit != cache.end() && cit->second.epoch == epoch) return cit->second.v;
            if (cache.size() > 512) cache.clear(); // practically bounded by the oracle count anyway

            oracle_awaiting_info r;
            int64_t oldest = 0;
            const auto now = db.head_block_time();
            const auto& idx = db.get_index<pm_market_index>().indices().get<by_oracle_status>();
            auto it = idx.lower_bound(boost::make_tuple(owner, (int8_t)1, pm_market_id_type()));
            for (; it != idx.end() && it->oracle == owner && it->status == 1; ++it)
                if (it->betting_expiration != fc::time_point_sec() && it->betting_expiration <= now) {
                    ++r.count;
                    int64_t age = (int64_t)now.sec_since_epoch() - (int64_t)it->betting_expiration.sec_since_epoch();
                    if (age > oldest) oldest = age;
                }
            r.oldest_age = (uint32_t)(oldest < 0 ? 0 : oldest);
            cache[owner] = oa_entry{epoch, r};
            return r;
        }

        // p50/p95 resolution latency (seconds) from the oracle's 8-bucket histogram: the UPPER
        // boundary of the bucket where the cumulative count first reaches the target percentile. The
        // open top bucket (>30d) reports its lower edge (2592000) as a floor. 0 if no resolutions yet.
        uint32_t rt_percentile(const pm_oracle_object& o, int pct) {
            static const uint32_t ub[8] = {3600u,21600u,86400u,259200u,604800u,1209600u,2592000u,2592000u};
            uint64_t total = 0;
            for (int i = 0; i < 8; ++i) total += (uint64_t)o.resolution_time_hist[i].value;
            if (total == 0) return 0;
            uint64_t target = (total * (uint64_t)pct + 99) / 100;   // ceil(total*pct/100)
            if (target == 0) target = 1;
            uint64_t cum = 0;
            for (int i = 0; i < 8; ++i) { cum += (uint64_t)o.resolution_time_hist[i].value; if (cum >= target) return ub[i]; }
            return ub[7];
        }

        // H3: expected_payout used to walk every bet of the market PER POSITION — O(N²) from
        // get_market_full / get_account_positions, so one natural UI call on a market built with N
        // cheap bets was a read-DoS. Cache the per-market, per-side winner aggregate for the head
        // block: a single O(N) pass per market per block, however many positions query it. Same
        // thread_local-per-head-block pattern as oracle_below_risk_floor (display-only plugin cache,
        // no consensus input).
        const std::pair<share_type, fc::uint128_t>& winner_agg(const database& db,
                                                               const pm_market_object& mkt,
                                                               int16_t side) {
            struct entry { uint32_t epoch = 0; std::map<int16_t, std::pair<share_type, fc::uint128_t>> sides; };
            static thread_local std::map<pm_market_id_type, entry> cache;
            const uint32_t epoch = db.head_block_num();
            auto cit = cache.find(mkt.id);
            if (cit == cache.end() || cit->second.epoch != epoch) {
                if (cache.size() > 512) cache.clear(); // bound per-block working set
                entry e; e.epoch = epoch;
                const bool binary = (mkt.market_type == 0);
                const auto& bidx = db.get_index<pm_bet_index>().indices().get<by_market>();
                for (auto it = bidx.lower_bound(boost::make_tuple(mkt.id, pm_bet_id_type()));
                     it != bidx.end() && it->market == mkt.id; ++it) {
                    if (it->status != 0 && it->status != 3) continue;
                    int16_t s = binary ? (int16_t)it->side : it->outcome_index;
                    auto& a = e.sides[s];
                    a.first  += it->amount;
                    a.second += fc::uint128_t((uint64_t)it->weight.value);
                }
                cit = cache.emplace(mkt.id, std::move(e)).first;
            }
            static const std::pair<share_type, fc::uint128_t> zero{share_type(0), fc::uint128_t(0)};
            auto sit = cit->second.sides.find(side);
            return sit != cit->second.sides.end() ? sit->second : zero;
        }

        // Parimutuel payout this bet would receive if its side wins (or its realized
        // payout once settled). Byte-mirrors settle_market() in pm_evaluator.cpp.
        share_type expected_payout(const database& db, const pm_bet_object& bet,
                                   const pm_market_object& mkt) {
            if (bet.status == 3) return bet.resolved_amount; // already settled
            if (bet.status != 0) return 0;                   // cancelled / refunded / queued

            const bool binary  = (mkt.market_type == 0);
            const int16_t myside = binary ? (int16_t)bet.side : bet.outcome_index;
            const bool resolved = (mkt.status == 3 && mkt.resolved_outcome >= 0);
            if (resolved && myside != mkt.resolved_outcome) return 0; // already a loser
            const int16_t winside = resolved ? mkt.resolved_outcome : myside;

            // Aggregate winning-side amount and curve weight (weight_sum is not stored) —
            // cached per market per head block (H3), so position loops stay O(N + positions).
            const auto& agg = winner_agg(db, mkt, winside);
            const share_type    winners_amount = agg.first;
            const fc::uint128_t win_weight     = agg.second;

            int64_t losers_sum = mkt.bets_sum.value - winners_amount.value;
            if (losers_sum < 0) losers_sum = 0;
            int64_t fees = losers_sum * ((int64_t)mkt.oracle_fee_percent
                            + mkt.creator_fee_percent + mkt.liquidity_fee_percent) / 10000;
            int64_t avail = losers_sum - fees;
            if (avail < 0) avail = 0;
            int64_t fixed_paid = (mkt.oracle_fixed_fee.value < avail) ? mkt.oracle_fixed_fee.value : avail;
            int64_t winners_pool = avail - fixed_paid + mkt.forfeit_pool.value;

            if (win_weight == 0) return bet.amount; // sole/edge winner refunded
            int64_t profit = (int64_t)(fc::uint128_t((uint64_t)winners_pool)
                              * fc::uint128_t((uint64_t)bet.weight.value) / win_weight).lo;
            int64_t penalty = (int64_t)(fc::uint128_t((uint64_t)profit)
                              * fc::uint128_t((uint64_t)bet.time_penalty)
                              / fc::uint128_t((uint64_t)1000000)).lo;
            return share_type(bet.amount.value + profit - penalty);
        }

        // Listing risk floor (security-threat-model §4.3) — AGGREGATE, per-ORACLE (owner 2026-07-13).
        // Insurance backs the oracle's WHOLE book, not each market in isolation, so the verdict is on
        // the oracle, not one market. Hidden from default listing (revealed via show_risky) when EITHER:
        //   (A) insurance < pm_min_oracle_insurance — stake gone/insufficient (e.g. slashed to ~0) → the
        //       entire book is hidden, INCLUDING zero-bet markets. Fixes the old bug where a broke oracle
        //       kept full visibility because each fresh market had bets_sum==0 (no per-market "risk").
        //   (B) insurance×100 < coverage% × Σ bets_sum over the oracle's OPEN markets — aggregate exposure
        //       outgrew the stake (an oracle can't honestly back 2.5× its whole open volume).
        // Non-consensus (API-only, no HF/replay). Cached per oracle per head block (thread_local) so a
        // listing loop over N markets stays O(N + distinct-oracles), not O(N × book).
        bool oracle_below_risk_floor(const database& db, const account_name_type& oracle) {
            static thread_local std::map<account_name_type, std::pair<uint32_t, bool>> cache;
            const uint32_t epoch = db.head_block_num();
            auto cit = cache.find(oracle);
            if (cit != cache.end() && cit->second.first == epoch) return cit->second.second;
            const auto& mp = db.get_validator_schedule_object().median_props;
            const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
            auto oit = oidx.find(oracle);
            const int64_t ins = (oit != oidx.end()) ? oit->insurance.value : 0;
            bool verdict;
            if (ins < mp.pm_min_oracle_insurance.amount.value) {
                verdict = true;                                                  // (A) below minimum stake
            } else {
                int64_t agg = 0;                                                 // (B) aggregate open exposure
                const auto& midx = db.get_index<pm_market_index>().indices().get<by_oracle>();
                for (auto mit = midx.lower_bound(oracle); mit != midx.end() && mit->oracle == oracle; ++mit)
                    if (mit->status == 1) agg += mit->bets_sum.value;
                verdict = (ins * 100 < agg * (int64_t)mp.pm_listing_min_coverage_percent);
            }
            cache[oracle] = { epoch, verdict };
            return verdict;
        }

        // Back-compat thin wrapper (list_markets et al. call this per market).
        bool below_risk_floor(const database& db, const pm_market_object& mkt) {
            return oracle_below_risk_floor(db, mkt.oracle);
        }

        // Per-outcome amount + curve-weight aggregate (live from active/resolved bets). Shared by
        // get_market_weight_sums and get_market_full.
        pm_market_weight_sums_api_object make_weight_sums(const database& db, const pm_market_object& mkt) {
            // D5: cached per (head block, market) — winner_agg pattern. Repeated get_market_full /
            // get_market_weight_sums calls on one market used to redo this O(bets) walk every time.
            struct ws_entry { uint32_t epoch = 0; pm_market_weight_sums_api_object v; };
            static thread_local std::map<pm_market_id_type, ws_entry> cache;
            const uint32_t epoch = db.head_block_num();
            auto cit = cache.find(mkt.id);
            if (cit != cache.end() && cit->second.epoch == epoch) return cit->second.v;
            if (cache.size() > 512) cache.clear(); // bound per-block working set

            const bool binary = (mkt.market_type == 0);
            std::vector<share_type> amt(mkt.outcome_count, share_type(0));
            std::vector<share_type> wgt(mkt.outcome_count, share_type(0));
            const auto& bidx = db.get_index<pm_bet_index>().indices().get<by_market>();
            for (auto it = bidx.lower_bound(boost::make_tuple(mkt.id, pm_bet_id_type()));
                 it != bidx.end() && it->market == mkt.id; ++it) {
                if (it->status != 0 && it->status != 3) continue;
                int16_t s = binary ? (int16_t)it->side : it->outcome_index;
                if (s >= 0 && s < (int16_t)mkt.outcome_count) { amt[s] += it->amount; wgt[s] += it->weight; }
            }
            pm_market_weight_sums_api_object out;
            out.market_type = mkt.market_type;
            out.bets_sum    = mkt.bets_sum;
            // Real per-outcome labels are stored on-chain (pm_outcome_object) for BOTH binary and
            // multi markets (see pm_create_market_evaluator). Serve them for binary too so clients
            // show the actual outcome names (team A / team B, Yes/No, Over/Under) instead of "A"/"B".
            // Fallback to "A"/"B" only when a label is missing (legacy/blank).
            {
                const auto& oidx = db.get_index<pm_outcome_index>().indices().get<by_market_outcome>();
                for (uint8_t i = 0; i < mkt.outcome_count; ++i) {
                    auto oit = oidx.find(boost::make_tuple(mkt.id, i));
                    std::string label = (oit != oidx.end()) ? to_string(oit->label) : std::string();
                    if (binary && label.empty()) label = (i == 0 ? "A" : "B");
                    out.outcomes.push_back({(int16_t)i, label, amt[i], wgt[i]});
                }
            }
            cache[mkt.id] = ws_entry{epoch, out};
            return out;
        }

    } // anonymous namespace

    struct prediction_market_api::impl final {
        impl(): database_(appbase::app().get_plugin<chain::plugin>().db()) {}
        ~impl() = default;

        graphene::chain::database& database() { return database_; }
        graphene::chain::database& database() const { return database_; }

        // Off-chain metadata is ingested per-operation in ingest_market_meta() (from the create op,
        // since `metadata` is no longer kept in consensus state). on_block() only prunes expired data.
        void on_block() {
            auto& db = database_;
            run_meta_backfill();        // one-shot DLT meta recovery (no-op once complete)
            if (ttl_days_ == 0) return; // archival node: keep off-chain metadata/klines forever
            const auto now = db.head_block_time();

            // Prune expired markets. Both the metadata and the (potentially large) kline history are
            // dropped together: drain the market's kline points first (bounded), then remove the meta
            // once its klines are gone. A market with more klines than the per-block budget finishes
            // over several blocks — its meta stays as the "still pruning" marker until then — so both
            // plugin indexes stay bounded without stalling block processing.
            const auto& exp_idx = db.get_index<pm_market_meta_index>().indices().get<by_meta_expiry>();
            const auto& kseq = db.get_index<pm_market_kline_index>().indices().get<by_kline_market_seq>();
            uint32_t budget = 200; // total object removals this block
            for (auto eit = exp_idx.begin(); eit != exp_idx.end() && eit->expiry <= now && budget > 0; ) {
                const pm_market_id_type mkt = eit->market;
                auto kit = kseq.lower_bound(boost::make_tuple(mkt, (uint32_t)0));
                while (kit != kseq.end() && kit->market == mkt && budget > 0) {
                    const auto& k = *kit; ++kit; db.remove(k); --budget;
                }
                const bool kline_drained = (kit == kseq.end() || kit->market != mkt);
                if (kline_drained && budget > 0) {
                    const auto& meta = *eit; ++eit; db.remove(meta); --budget;
                } else {
                    break; // out of budget mid-market; resume this market next block (meta still present)
                }
            }
        }

        // ── Kline / time-series recorder (non-consensus) ──────────────────────────────
        // Maps a weight-changing operation to (market, reason); returns none for everything else.
        struct kline_event_visitor {
            typedef fc::optional<std::pair<pm_market_id_type, uint8_t>> result_type;
            const graphene::chain::database& db;
            result_type operator()(const protocol::pm_place_bet_operation& o)         const {
                if (o.mode != 0) return result_type(); // batched bet changes weights only at pm_batch_settle
                return std::make_pair(pm_market_id_type(o.market_id), (uint8_t)0);
            }
            result_type operator()(const protocol::pm_leverage_open_operation& o)     const { return std::make_pair(pm_market_id_type(o.market_id), (uint8_t)4); }
            result_type operator()(const protocol::pm_batch_settle_operation& o)      const { return std::make_pair(pm_market_id_type(o.market_id), (uint8_t)3); }
            result_type operator()(const protocol::pm_leverage_liquidate_operation& o)const { return std::make_pair(pm_market_id_type(o.market_id), (uint8_t)2); }
            result_type operator()(const protocol::pm_leverage_resolve_operation& o)  const { return std::make_pair(pm_market_id_type(o.market_id), (uint8_t)5); }
            result_type operator()(const protocol::pm_cancel_bet_operation& o)        const {
                const auto* b = db.find<pm_bet_object>(pm_bet_id_type(o.bet_id));
                if (b) return std::make_pair(b->market, (uint8_t)1);
                return result_type();
            }
            template<typename T> result_type operator()(const T&) const { return result_type(); }
        };

        // Picks out a pm_create_market_operation from the variant (nullptr for anything else),
        // so we can ingest its metadata off-chain without storing it in consensus state.
        struct create_market_visitor {
            typedef const protocol::pm_create_market_operation* result_type;
            result_type operator()(const protocol::pm_create_market_operation& o) const { return &o; }
            template<typename T> result_type operator()(const T&) const { return nullptr; }
        };

        // Parse and index a new market's free-form metadata off-chain (non-consensus, prunable).
        // The raw blob lives only in the block log / operation — never in chainbase state — so each
        // node keeps it only as long as --pmm-ttl-days, and clients stay free to shape it ("like custom").
        // Create the off-chain meta object for a SPECIFIC market from its create op. Idempotent.
        // res_exp is the market's result_expiration (== op.result_expiration at creation time).
        void create_meta_for(pm_market_id_type mkt_id, time_point_sec res_exp,
                             const protocol::pm_create_market_operation& op) {
            auto& db = database_;
            const auto& meta_by_market = db.get_index<pm_market_meta_index>().indices().get<by_meta_market>();
            if (meta_by_market.find(mkt_id) != meta_by_market.end()) return;
            const uint32_t grace = (uint32_t)db.get_validator_schedule_object().median_props.pm_dispute_grace_sec;
            const parsed_meta pm = parse_market_metadata(op.metadata);
            db.create<pm_market_meta_object>([&](pm_market_meta_object& m) {
                m.market = mkt_id;
                from_string(m.category, pm.category);
                from_string(m.subcategory, pm.subcategory);
                from_string(m.tags, pm.tags);
                from_string(m.banned_jurisdictions, pm.banned_jurisdictions);
                from_string(m.title, pm.title);
                from_string(m.image, pm.image);
                from_string(m.condition_id, pm.condition_id);
                from_string(m.description, pm.description);
                from_string(m.event, pm.event);
                from_string(m.event_title, pm.event_title);
                m.child = pm.child;
                m.expiry = res_exp + fc::seconds(grace) + fc::seconds((int64_t)ttl_days_ * 86400);
            });
        }

        void ingest_market_meta(const protocol::pm_create_market_operation& op) {
            auto& db = database_;
            // The market this op just created is the newest one; guard by creator + idempotency.
            const auto& midx = db.get_index<pm_market_index>().indices().get<by_id>();
            if (midx.begin() == midx.end()) return;
            auto rit = midx.rbegin();
            if (rit->creator != op.creator) return;
            create_meta_for(rit->id, rit->result_expiration, op);
        }

        // ── One-shot meta backfill from the DLT block log (fallback) ──────────────────
        // After --replay-from-snapshot, meta is rebuilt only for markets whose create op was in the
        // reindex window (snapshot_head+1..head). But the DLT rolling block log can reach FURTHER back
        // than the snapshot — those older create ops are still on disk, just never re-applied. This
        // one-shot pass scans the whole DLT log and re-ingests meta for any market still missing it.
        // Runs from on_block() (a safe chainbase write context, like pruning), budgeted across blocks.
        //
        // op→market mapping is by a STRONG IDENTITY KEY (creator+url+both expirations+outcome count),
        // all consensus fields the evaluator copies verbatim from the op into the market. This can never
        // mis-assign: a key mismatch just leaves that market empty (no wrong title), unlike a positional
        // scheme. Duplicate keys (identical re-created markets) are matched in id order via a small list.
        bool     mb_done_    = false;
        bool     mb_started_ = false;
        uint32_t mb_next_    = 0;   // next DLT block number to scan
        uint32_t mb_end_     = 0;   // head block of the DLT log at scan start
        std::map<std::string, std::vector<pm_market_id_type>> mb_index_; // identity key → candidate market ids (asc)

        static std::string mb_key(const std::string& creator, const std::string& url,
                                  uint32_t bexp, uint32_t rexp, uint32_t ocount) {
            return creator + '\x1f' + url + '\x1f' + std::to_string(bexp) + '\x1f'
                 + std::to_string(rexp) + '\x1f' + std::to_string(ocount);
        }

        void run_meta_backfill() {
            if (mb_done_) return;
            auto& db = database_;
            const auto& dlt = db.get_dlt_block_log();
            if (!mb_started_) {
                mb_started_ = true;
                const uint32_t s = dlt.start_block_num(), h = dlt.head_block_num();
                if (s == 0 || h == 0 || h < s) { mb_done_ = true; return; } // no DLT log → nothing to do
                const auto& midx = db.get_index<pm_market_index>().indices().get<by_id>();
                const auto& meta_by_market = db.get_index<pm_market_meta_index>().indices().get<by_meta_market>();
                uint64_t missing = 0;
                for (auto it = midx.begin(); it != midx.end(); ++it) {
                    if (meta_by_market.find(it->id) != meta_by_market.end()) continue; // already has meta
                    mb_index_[mb_key(std::string(it->creator), to_string(it->url),
                                     it->betting_expiration.sec_since_epoch(),
                                     it->result_expiration.sec_since_epoch(),
                                     (uint32_t)it->outcome_count)].push_back(it->id);
                    ++missing;
                }
                mb_next_ = s; mb_end_ = h;
                if (missing == 0 || mb_index_.empty()) { mb_done_ = true; mb_index_.clear(); return; } // nothing to recover (normal restart)
                wlog("pm meta backfill: scanning DLT blocks ${s}..${h} for ${n} markets missing meta",
                     ("s", s)("h", h)("n", missing));
            }
            uint32_t budget = 500; // DLT block reads per block-apply (bounded to avoid stalls)
            uint64_t recovered = 0;
            while (mb_next_ <= mb_end_ && budget > 0) {
                const uint32_t bn = mb_next_++; --budget;
                auto blk = dlt.read_block_by_num(bn);
                if (!blk) continue;
                for (const auto& trx : blk->transactions)
                    for (const auto& opv : trx.operations) {
                        const auto* op = opv.visit(create_market_visitor{});
                        if (!op) continue;
                        auto kit = mb_index_.find(mb_key(std::string(op->creator), op->url,
                                     op->betting_expiration.sec_since_epoch(),
                                     op->result_expiration.sec_since_epoch(),
                                     (uint32_t)op->outcomes.size()));
                        if (kit == mb_index_.end() || kit->second.empty()) continue;
                        const pm_market_id_type mid = kit->second.front();
                        kit->second.erase(kit->second.begin());
                        create_meta_for(mid, op->result_expiration, *op);
                        ++recovered;
                    }
            }
            if (recovered) wlog("pm meta backfill: recovered ${n} markets (up to block ${b})", ("n", recovered)("b", mb_next_ - 1));
            if (mb_next_ > mb_end_) { mb_done_ = true; mb_index_.clear(); wlog("pm meta backfill: complete"); }
        }

        // A plugin observer must never throw out of the apply path — swallow everything.
        void on_post_apply_operation(const graphene::chain::operation_notification& note) {
            try {
                const auto ev = note.op.visit(kline_event_visitor{database_});
                if (ev) record_kline(ev->first, ev->second);
            } catch (...) {}
            try {
                if (const auto* cop = note.op.visit(create_market_visitor{}))
                    ingest_market_meta(*cop);
            } catch (...) {}
        }

        // Append one snapshot of every outcome's parimutuel weight (staked amount) for `market`.
        void record_kline(pm_market_id_type market, uint8_t reason) {
            auto& db = database_;
            const auto* m = db.find<pm_market_object>(market);
            if (!m) return;

            // Next sequence = (max existing seq for this market) + 1; contiguous and undo-safe.
            const auto& kidx = db.get_index<pm_market_kline_index>().indices().get<by_kline_market_seq>();
            uint32_t next_seq = 0;
            auto it = kidx.upper_bound(boost::make_tuple(market, std::numeric_limits<uint32_t>::max()));
            if (it != kidx.begin()) {
                --it;
                if (it->market == market) next_seq = it->seq + 1;
            }

            const bool binary = (m->market_type == 0);
            const auto& oidx = db.get_index<pm_outcome_index>().indices().get<by_market_outcome>();
            db.create<pm_market_kline_object>([&](pm_market_kline_object& k) {
                k.market    = market;
                k.seq       = next_seq;
                k.timestamp = db.head_block_time();
                k.reason    = reason;
                k.bets_sum  = m->bets_sum;
                if (binary) {
                    k.weights.push_back(m->a_bets_sum); // outcome 0
                    k.weights.push_back(m->b_bets_sum); // outcome 1
                } else {
                    for (uint8_t i = 0; i < m->outcome_count; ++i) {
                        auto oit = oidx.find(boost::make_tuple(market, i));
                        k.weights.push_back(oit != oidx.end() ? oit->bets_sum : share_type(0));
                    }
                }
            });
        }

        boost::signals2::connection block_conn_;
        boost::signals2::connection op_conn_;
        uint32_t ttl_days_ = 5;

    private:
        graphene::chain::database& database_;
    };

    void prediction_market_api::plugin_startup() {
        wlog("prediction_market_api plugin: plugin_startup()");
    }

    void prediction_market_api::plugin_shutdown() {
        if (pimpl) {
            pimpl->block_conn_.disconnect();
            pimpl->op_conn_.disconnect();
        }
        wlog("prediction_market_api plugin: plugin_shutdown()");
    }

    const std::string& prediction_market_api::name() {
        static const std::string name = "prediction_market_api";
        return name;
    }

    prediction_market_api::prediction_market_api() = default;
    prediction_market_api::~prediction_market_api() = default;

    void prediction_market_api::set_program_options(
        boost::program_options::options_description&,
        boost::program_options::options_description& cfg) {
        cfg.add_options()
            ("pmm-ttl-days", boost::program_options::value<uint32_t>()->default_value(5),
             "Days to keep PM market metadata/klines (off-chain) after the dispute window closes; 0 keeps forever");
    }

    void prediction_market_api::plugin_initialize(const boost::program_options::variables_map& options) {
        pimpl = std::make_unique<impl>();
        if (options.count("pmm-ttl-days"))
            pimpl->ttl_days_ = options.at("pmm-ttl-days").as<uint32_t>();
        graphene::chain::add_plugin_index<pm_market_meta_index>(pimpl->database());
        graphene::chain::add_plugin_index<pm_market_kline_index>(pimpl->database());
        pimpl->block_conn_ = pimpl->database().applied_block.connect(
            [&](const protocol::signed_block&) { pimpl->on_block(); });
        // Record a kline point after every weight-changing PM operation (undo/redo-safe: the kline
        // objects live in chainbase, so pending-tx writes roll back and re-apply with the block).
        pimpl->op_conn_ = pimpl->database().post_apply_operation.connect(
            [&](const graphene::chain::operation_notification& note) { pimpl->on_post_apply_operation(note); });
        JSON_RPC_REGISTER_API(name());
    }

    // ── Markets ──────────────────────────────────────────────────────────────────

    // comma-joined meta string → JSON array (clients expect tags / banned_jurisdictions as arrays)
    static fc::variant csv_to_array(const std::string& csv) {
        std::vector<fc::variant> out;
        size_t start = 0;
        while (start <= csv.size()) {
            size_t comma = csv.find(',', start);
            std::string tok = csv.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
            if (!tok.empty()) out.push_back(fc::variant(tok));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        return fc::variant(std::move(out));
    }

    // Build a client "market card": the consensus pm_market_object plus the parsed metadata the
    // browse/detail views need but the on-chain object doesn't carry (metadata is off-chain, in
    // pm_market_meta_index). We inject BOTH a reconstructed `metadata` object — so clients that
    // parse market.metadata keep working with no change — AND flat title/image/category fields for
    // clients that read them directly. Empty when the meta was pruned or never ingested.
    static fc::variant market_card(const graphene::chain::database& db, const pm_market_object& m) {
        fc::variant v;
        fc::to_variant(m, v);
        fc::mutable_variant_object o(v.get_object());
        const auto& midx = db.get_index<pm_market_meta_index>().indices().get<by_meta_market>();
        auto it = midx.find(m.id);
        std::string title, image, category;
        fc::mutable_variant_object md;
        if (it != midx.end()) {
            title = to_string(it->title); image = to_string(it->image); category = to_string(it->category);
            md("title", title)("image", image)("category", category)
              ("subcategory", to_string(it->subcategory))
              ("tags", csv_to_array(to_string(it->tags)))
              ("banned_jurisdictions", csv_to_array(to_string(it->banned_jurisdictions)))
              ("condition_id", to_string(it->condition_id))
              ("description", to_string(it->description))
              ("event", to_string(it->event))
              ("event_title", to_string(it->event_title));
        }
        o["title"]    = title;
        o["image"]    = image;
        o["category"] = category;
        o["metadata"] = fc::variant(std::move(md));   // reconstructed; clients parse market.metadata
        return fc::variant(std::move(o));
    }

    // Volume/expiration sort for account-scoped market listings (list_markets_by_oracle / _by_creator).
    // newest/oldest are handled cheaply by reverse/forward index traversal at the call site; these two
    // orders need the whole matching set materialized, sorted, then paged — the same approach
    // list_markets_by_category uses for its volume/expiration sorts.
    static std::vector<fc::variant> page_markets_sorted(
        const graphene::chain::database& db, std::vector<const pm_market_object*>& ms,
        const std::string& order, uint32_t from, uint32_t limit) {
        if (order == "volume")
            std::stable_sort(ms.begin(), ms.end(), [](const pm_market_object* a, const pm_market_object* b){
                return a->bets_sum.value > b->bets_sum.value; });                    // busiest first
        else // "expiration": soonest-closing first; open-ended (betting_expiration==0) sorts last
            std::stable_sort(ms.begin(), ms.end(), [](const pm_market_object* a, const pm_market_object* b){
                int64_t ea = a->betting_expiration.sec_since_epoch(); if (ea == 0) ea = std::numeric_limits<int64_t>::max();
                int64_t eb = b->betting_expiration.sec_since_epoch(); if (eb == 0) eb = std::numeric_limits<int64_t>::max();
                return ea < eb; });
        std::vector<fc::variant> result;
        result.reserve(std::min<size_t>(limit, ms.size()));
        for (uint32_t i = from; i < ms.size() && result.size() < limit; ++i)
            result.push_back(market_card(db, *ms[i]));
        return result;
    }

    // Id-order paging over a market index range: newest = reverse walk, oldest = forward. No
    // materialization, no sort, early stop after the page — the cheap path, and the fallback for
    // volume/expiration when the matching set exceeds MAX_PM_SORT_POOL.
    template <typename It>
    static std::vector<fc::variant> page_markets_id_order(
        const graphene::chain::database& db, It first, It last,
        bool newest, uint32_t from, uint32_t limit) {
        std::vector<fc::variant> result;
        result.reserve(limit);
        if (newest) {
            auto itr = last;
            while (from > 0 && itr != first) { --itr; --from; }
            while (result.size() < limit && itr != first) { --itr; result.push_back(market_card(db, *itr)); }
        } else {
            auto itr = first;
            while (from > 0 && itr != last) { ++itr; --from; }
            while (result.size() < limit && itr != last) { result.push_back(market_card(db, *itr)); ++itr; }
        }
        return result;
    }

    DEFINE_API(prediction_market_api, get_market) {
        CHECK_ARG_SIZE(1)
        auto market_id = args.args->at(0).as<int64_t>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto* m = db.find<pm_market_object>(pm_market_id_type(market_id));
            FC_ASSERT(m != nullptr, "Market not found");
            return market_card(db, *m);
        });
    }

    // list_markets(status, from, limit, [show_risky=false], [order="oldest"])
    // order: "oldest" (id asc, default — legacy) · "newest" (id desc) · "expiration" (soonest-closing
    //        first via by_betting_expiration, still-open only — global "ending soon" feed). The by_status index keeps
    // equal-status elements in insertion order; markets are born into their status (active markets
    // never re-enter the group), so within a status insertion order == id order → reverse traversal
    // of the equal-range yields newest-first without a full scan/sort. Discovery feeds pass "newest".
    DEFINE_API(prediction_market_api, list_markets) {
        CHECK_ARG_MIN_SIZE(3, 5)
        auto status = args.args->at(0).as<int8_t>();
        auto from   = args.args->at(1).as<uint32_t>();
        auto limit  = args.args->at(2).as<uint32_t>();
        auto show_risky = GET_OPTIONAL_ARG(3, bool, false); // reveal under-insured markets
        auto order  = GET_OPTIONAL_ARG(4, std::string, std::string("oldest"));
        FC_ASSERT(limit <= 1000);
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<fc::variant> result;
            result.reserve(limit);
            if (order == "expiration") {
                // Still-open markets, SOONEST-CLOSING first (global "ending soon" feed). Walk the
                // by_betting_expiration index (status, betting_expiration, id) from head_block_time
                // forward: this skips already-closed markets (betting_expiration <= now) and
                // open-ended ones (betting_expiration == 0 sorts before `now`), leaving exactly the
                // markets whose betting window is about to end, nearest deadline first.
                const auto now = db.head_block_time();
                const auto& eidx = db.get_index<pm_market_index>().indices().get<by_betting_expiration>();
                auto itr = eidx.lower_bound(boost::make_tuple(status, now, pm_market_id_type()));
                // `from` counts ROWS THE CALLER SAW, not raw index positions: the fill loop below
                // hides under-insured markets, so a skip loop that charged for them too would leave
                // the caller stuck — see the note above the risk-floor filter in the by_status branch.
                while (from > 0 && itr != eidx.end() && itr->status == status) {
                    if (show_risky || !below_risk_floor(db, *itr)) --from;
                    ++itr;
                }
                while (result.size() < limit && itr != eidx.end() && itr->status == status) {
                    if (show_risky || !below_risk_floor(db, *itr))
                        result.push_back(market_card(db, *itr));
                    ++itr;
                }
            } else {
                const auto& idx = db.get_index<pm_market_index>().indices().get<by_status>();
                auto range = idx.equal_range(status);
                if (order == "newest") {
                    auto itr = range.second;                    // one past the last equal-status market
                    // Skip `from` markets THE CALLER WOULD HAVE SEEN. Charging the offset for rows the
                    // fill loop hides (under-insured oracles) makes paging stall: the caller advances
                    // from += limit, lands back inside the hidden run, and the fill loop walks out of it
                    // to the very same visible markets — the page repeats forever instead of advancing.
                    // Every other listing here already counts `from` after its filters
                    // (list_markets_by_category / _by_event); this one used to be the odd one out.
                    while (from > 0 && itr != range.first) {
                        --itr;
                        if (show_risky || !below_risk_floor(db, *itr)) --from;
                    }
                    while (result.size() < limit && itr != range.first) {
                        --itr;
                        if (show_risky || !below_risk_floor(db, *itr))
                            result.push_back(market_card(db, *itr));
                    }
                } else {
                    auto itr = range.first;
                    while (from > 0 && itr != range.second) {
                        if (show_risky || !below_risk_floor(db, *itr)) --from;
                        ++itr;
                    }
                    while (result.size() < limit && itr != range.second) {
                        if (show_risky || !below_risk_floor(db, *itr))
                            result.push_back(market_card(db, *itr));
                        ++itr;
                    }
                }
            }
            return result;
        });
    }

    // list_markets_by_oracle(oracle, from, limit, [order="newest"]) — an oracle's markets. order:
    // "newest" (id desc, default) · "oldest" (id asc) · "volume" (bets_sum desc) · "expiration"
    // (soonest-closing first). newest/oldest reverse/forward the equal-range cheaply (insertion==id
    // order, same assumption list_markets relies on); volume/expiration materialize the oracle's set and
    // sort it (page_markets_sorted). (The oracle work-queue methods — awaiting_resolution /
    // in_dispute_window / by_oracle_status — stay oldest-first on purpose: a queue is worked oldest-first.)
    DEFINE_API(prediction_market_api, list_markets_by_oracle) {
        CHECK_ARG_MIN_SIZE(3, 4)
        auto oracle = args.args->at(0).as<account_name_type>();
        auto from   = args.args->at(1).as<uint32_t>();
        auto limit  = args.args->at(2).as<uint32_t>();
        auto order  = GET_OPTIONAL_ARG(3, std::string, std::string("newest"));
        FC_ASSERT(limit <= 1000);
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<fc::variant> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_market_index>().indices().get<by_oracle>();
            auto range = idx.equal_range(oracle);
            if (order == "volume" || order == "expiration") {
                std::vector<const pm_market_object*> ms;
                for (auto itr = range.first; itr != range.second; ++itr) {
                    ms.push_back(&*itr);
                    if (ms.size() > MAX_PM_SORT_POOL) break;   // D1: too big to sort — degrade below
                }
                if (ms.size() <= MAX_PM_SORT_POOL)
                    return page_markets_sorted(db, ms, order, from, limit);
                // set too large for a full sort: degrade to newest-first paging without sorting
            }
            result = page_markets_id_order(db, range.first, range.second, order != "oldest", from, limit);
            return result;
        });
    }

    // list_markets_by_oracle_status(oracle, status, from, limit, [order="newest"]): this oracle's markets
    // in ONE status, walked over the (oracle, status, id) prefix. Lets a client pull e.g. an oracle's
    // active (1) or resolved (3) markets directly instead of fetching by_oracle and filtering. order:
    // "newest" (id desc, default — profile browse leads with the latest) · "oldest" · "volume" (bets_sum
    // desc) · "expiration" (soonest-closing first). This is the oracle PROFILE feed; the work-queue
    // methods (awaiting_resolution / in_dispute_window) are separate and stay oldest-first on purpose.
    DEFINE_API(prediction_market_api, list_markets_by_oracle_status) {
        CHECK_ARG_MIN_SIZE(4, 5)
        auto oracle = args.args->at(0).as<account_name_type>();
        auto status = args.args->at(1).as<int8_t>();
        auto from   = args.args->at(2).as<uint32_t>();
        auto limit  = args.args->at(3).as<uint32_t>();
        auto order  = GET_OPTIONAL_ARG(4, std::string, std::string("newest"));
        FC_ASSERT(limit <= 1000);
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<fc::variant> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_market_index>().indices().get<by_oracle_status>();
            auto range = idx.equal_range(boost::make_tuple(oracle, status));
            if (order == "volume" || order == "expiration") {
                std::vector<const pm_market_object*> ms;
                for (auto itr = range.first; itr != range.second; ++itr) {
                    ms.push_back(&*itr);
                    if (ms.size() > MAX_PM_SORT_POOL) break;   // D1: too big to sort — degrade below
                }
                if (ms.size() <= MAX_PM_SORT_POOL)
                    return page_markets_sorted(db, ms, order, from, limit);
                // set too large for a full sort: degrade to newest-first paging without sorting
            }
            result = page_markets_id_order(db, range.first, range.second, order != "oldest", from, limit);
            return result;
        });
    }

    // Markets awaiting THIS oracle's result: active (status 1) markets whose betting window has
    // already closed (betting_expiration <= head_block_time) and that are therefore not yet resolved.
    // "Awaiting" is not a distinct status — a market stays status 1 from open through close until
    // resolve — so it can't be isolated by status alone. The by_betting_expiration index is
    // (status, betting_expiration, id): every status-1 market sits contiguously, ordered by
    // betting_expiration, so we walk only the bounded prefix whose betting has passed and keep this
    // oracle's rows. This is far cheaper than scanning the oracle's entire (mostly resolved) history.
    DEFINE_API(prediction_market_api, list_markets_awaiting_resolution) {
        CHECK_ARG_MIN_SIZE(3, 3)
        auto oracle = args.args->at(0).as<account_name_type>();
        auto from   = args.args->at(1).as<uint32_t>();
        auto limit  = args.args->at(2).as<uint32_t>();
        FC_ASSERT(limit <= 1000);
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<fc::variant> result;
            result.reserve(limit);
            const auto now = db.head_block_time();
            const auto& idx = db.get_index<pm_market_index>().indices().get<by_betting_expiration>();
            auto itr = idx.lower_bound(boost::make_tuple((int8_t)1, time_point_sec(0), pm_market_id_type()));
            for (; itr != idx.end() && itr->status == 1 && itr->betting_expiration <= now; ++itr) {
                if (itr->oracle != oracle) continue;
                if (from > 0) { --from; continue; }
                result.push_back(market_card(db, *itr));
                if (result.size() >= limit) break;
            }
            return result;
        });
    }

    // list_markets_in_dispute_window(oracle, from, limit): this oracle's resolved(3) markets whose
    // payout is still pending(1) and that carry NO dispute row yet — i.e. still inside the window
    // where a dispute can be filed. Mirrors the stored markets_in_dispute_window gauge on the oracle.
    // Walks by_oracle_status(oracle, 3) (this oracle's resolved set only), filtering payout==1 and
    // absence of a dispute; far cheaper than scanning the global market set.
    DEFINE_API(prediction_market_api, list_markets_in_dispute_window) {
        CHECK_ARG_MIN_SIZE(3, 3)
        auto oracle = args.args->at(0).as<account_name_type>();
        auto from   = args.args->at(1).as<uint32_t>();
        auto limit  = args.args->at(2).as<uint32_t>();
        FC_ASSERT(limit <= 1000);
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<fc::variant> result;
            result.reserve(limit);
            const auto& midx = db.get_index<pm_market_index>().indices().get<by_oracle_status>();
            const auto& didx = db.get_index<pm_dispute_index>().indices().get<by_market>();
            auto itr = midx.lower_bound(boost::make_tuple(oracle, (int8_t)3, pm_market_id_type()));
            for (; itr != midx.end() && itr->oracle == oracle && itr->status == 3; ++itr) {
                if (itr->payout_status != 1) continue;
                if (didx.find(itr->id) != didx.end()) continue;   // disputed → not in the disputable window
                if (from > 0) { --from; continue; }
                result.push_back(market_card(db, *itr));
                if (result.size() >= limit) break;
            }
            return result;
        });
    }

    // list_markets_by_creator(creator, from, limit, [order="newest"]) — a creator's markets. order:
    // "newest" (id desc, default) · "oldest" (id asc) · "volume" (bets_sum desc) · "expiration"
    // (soonest-closing first). by_creator is ordered_non_unique on the name (insertion==id order), so
    // newest/oldest reverse/forward the equal-range cheaply; volume/expiration materialize the creator's
    // set and sort it (page_markets_sorted), same as list_markets_by_category / _by_oracle.
    DEFINE_API(prediction_market_api, list_markets_by_creator) {
        CHECK_ARG_MIN_SIZE(3, 4)
        auto creator = args.args->at(0).as<account_name_type>();
        auto from    = args.args->at(1).as<uint32_t>();
        auto limit   = args.args->at(2).as<uint32_t>();
        auto order   = GET_OPTIONAL_ARG(3, std::string, std::string("newest"));
        FC_ASSERT(limit <= 1000);
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<fc::variant> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_market_index>().indices().get<by_creator>();
            auto range = idx.equal_range(creator);
            if (order == "volume" || order == "expiration") {
                std::vector<const pm_market_object*> ms;
                for (auto itr = range.first; itr != range.second; ++itr) {
                    ms.push_back(&*itr);
                    if (ms.size() > MAX_PM_SORT_POOL) break;   // D1: too big to sort — degrade below
                }
                if (ms.size() <= MAX_PM_SORT_POOL)
                    return page_markets_sorted(db, ms, order, from, limit);
                // set too large for a full sort: degrade to newest-first paging without sorting
            }
            result = page_markets_id_order(db, range.first, range.second, order != "oldest", from, limit);
            return result;
        });
    }

    DEFINE_API(prediction_market_api, get_market_outcomes) {
        CHECK_ARG_SIZE(1)
        auto market_id = args.args->at(0).as<int64_t>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_outcome_object> result;
            const auto& idx = db.get_index<pm_outcome_index>().indices().get<by_market_outcome>();
            auto itr = idx.lower_bound(boost::make_tuple(pm_market_id_type(market_id), (uint8_t)0));
            while (itr != idx.end() && itr->market == pm_market_id_type(market_id)) {
                result.push_back(pm_outcome_object(*itr));
                ++itr;
            }
            return result;
        });
    }

    DEFINE_API(prediction_market_api, get_market_weight_sums) {
        CHECK_ARG_SIZE(1)
        auto market_id = args.args->at(0).as<int64_t>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto* m = db.find<pm_market_object>(pm_market_id_type(market_id));
            FC_ASSERT(m != nullptr, "Market not found");
            return make_weight_sums(db, *m);
        });
    }

    // get_market_bets(market, from, limit, [order="newest"]) — the "recent bets" feed, so newest-first
    // by default (id desc); "oldest" preserves the legacy id-asc order. by_market is composite (market, id).
    DEFINE_API(prediction_market_api, get_market_bets) {
        CHECK_ARG_MIN_SIZE(3, 4)
        auto market_id = args.args->at(0).as<int64_t>();
        auto from      = args.args->at(1).as<uint32_t>();
        auto limit     = args.args->at(2).as<uint32_t>();
        auto order     = GET_OPTIONAL_ARG(3, std::string, std::string("newest"));
        FC_ASSERT(limit <= 1000);
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_bet_object> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_bet_index>().indices().get<by_market>();
            auto range = idx.equal_range(pm_market_id_type(market_id));
            if (order == "oldest") {
                auto itr = range.first;
                while (from > 0 && itr != range.second) { ++itr; --from; }
                while (result.size() < limit && itr != range.second) { result.push_back(pm_bet_object(*itr)); ++itr; }
            } else { // "newest" (default)
                auto itr = range.second;
                while (from > 0 && itr != range.first) { --itr; --from; }
                while (result.size() < limit && itr != range.first) { --itr; result.push_back(pm_bet_object(*itr)); }
            }
            return result;
        });
    }

    // get_account_positions(account, from, limit, [order="newest"])
    // order: "newest" (bet id desc, default — a user's activity should surface their latest bets first,
    // not stale positions from months ago) · "oldest" (id asc, legacy). by_account is composite
    // (account, id): reverse traversal of the account's equal-range yields newest-first deterministically.
    DEFINE_API(prediction_market_api, get_account_positions) {
        CHECK_ARG_MIN_SIZE(3, 4)
        auto account = args.args->at(0).as<account_name_type>();
        auto from    = args.args->at(1).as<uint32_t>();
        auto limit   = args.args->at(2).as<uint32_t>();
        auto order   = GET_OPTIONAL_ARG(3, std::string, std::string("newest"));
        FC_ASSERT(limit <= 1000);
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_position_api_object> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_bet_index>().indices().get<by_account>();
            auto range = idx.equal_range(account);
            auto emit = [&](const pm_bet_object& b) {
                const auto* m = db.find<pm_market_object>(b.market);
                share_type ep = (m != nullptr) ? expected_payout(db, b, *m) : share_type(0);
                result.push_back(pm_position_api_object{
                    pm_bet_object(b), ep,
                    (m != nullptr) ? m->status : (int8_t)0,
                    (m != nullptr) ? m->resolved_outcome : (int16_t)-1});
            };
            if (order == "oldest") {
                auto itr = range.first;
                while (from > 0 && itr != range.second) { ++itr; --from; }
                while (result.size() < limit && itr != range.second) { emit(*itr); ++itr; }
            } else { // "newest" (default)
                auto itr = range.second;
                while (from > 0 && itr != range.first) { --itr; --from; }
                while (result.size() < limit && itr != range.first) { --itr; emit(*itr); }
            }
            return result;
        });
    }

    DEFINE_API(prediction_market_api, get_market_liquidity) {
        CHECK_ARG_MIN_SIZE(3, 3)
        auto market_id = args.args->at(0).as<int64_t>();
        auto from      = args.args->at(1).as<uint32_t>();
        auto limit     = args.args->at(2).as<uint32_t>();
        FC_ASSERT(limit <= 1000);
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_liquidity_object> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_liquidity_index>().indices().get<by_market>();
            auto itr = idx.lower_bound(boost::make_tuple(pm_market_id_type(market_id), pm_liquidity_id_type()));
            while (from > 0 && itr != idx.end() && itr->market == pm_market_id_type(market_id)) { ++itr; --from; }
            while (result.size() < limit && itr != idx.end() && itr->market == pm_market_id_type(market_id)) {
                result.push_back(pm_liquidity_object(*itr));
                ++itr;
            }
            return result;
        });
    }

    // ── Leverage positions ─────────────────────────────────────────────────────────

    // get_account_leverage_positions(account, from, limit, [order="newest"]) — newest-first by default
    // (latest leverage activity surfaces first), "oldest" for the legacy id-asc order.
    DEFINE_API(prediction_market_api, get_account_leverage_positions) {
        CHECK_ARG_MIN_SIZE(3, 4)
        auto account = args.args->at(0).as<account_name_type>();
        auto from    = args.args->at(1).as<uint32_t>();
        auto limit   = args.args->at(2).as<uint32_t>();
        auto order   = GET_OPTIONAL_ARG(3, std::string, std::string("newest"));
        FC_ASSERT(limit <= 1000);
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_leverage_position_object> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_leverage_position_index>().indices().get<by_lev_account>();
            auto range = idx.equal_range(account);
            if (order == "oldest") {
                auto itr = range.first;
                while (from > 0 && itr != range.second) { ++itr; --from; }
                while (result.size() < limit && itr != range.second) { result.push_back(pm_leverage_position_object(*itr)); ++itr; }
            } else { // "newest" (default)
                auto itr = range.second;
                while (from > 0 && itr != range.first) { --itr; --from; }
                while (result.size() < limit && itr != range.first) { --itr; result.push_back(pm_leverage_position_object(*itr)); }
            }
            return result;
        });
    }

    DEFINE_API(prediction_market_api, get_market_leverage_positions) {
        CHECK_ARG_MIN_SIZE(3, 3)
        auto market_id = args.args->at(0).as<int64_t>();
        auto from      = args.args->at(1).as<uint32_t>();
        auto limit     = args.args->at(2).as<uint32_t>();
        FC_ASSERT(limit <= 1000);
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_leverage_position_object> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_leverage_position_index>().indices().get<by_lev_market_status>();
            auto itr = idx.lower_bound(boost::make_tuple(pm_market_id_type(market_id), (uint8_t)0, pm_leverage_position_id_type()));
            while (from > 0 && itr != idx.end() && itr->market == pm_market_id_type(market_id)) { ++itr; --from; }
            while (result.size() < limit && itr != idx.end() && itr->market == pm_market_id_type(market_id)) {
                result.push_back(pm_leverage_position_object(*itr));
                ++itr;
            }
            return result;
        });
    }

    DEFINE_API(prediction_market_api, get_creator_ban) {
        CHECK_ARG_SIZE(1)
        auto account = args.args->at(0).as<account_name_type>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto& idx = db.get_index<pm_creator_ban_index>().indices().get<by_ban_account>();
            auto itr = idx.find(account);
            FC_ASSERT(itr != idx.end(), "No creator ban for account");
            return pm_creator_ban_object(*itr);
        });
    }

    // ── Oracles ──────────────────────────────────────────────────────────────────

    DEFINE_API(prediction_market_api, get_oracle) {
        CHECK_ARG_SIZE(1)
        auto owner = args.args->at(0).as<account_name_type>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto& idx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
            auto itr = idx.find(owner);
            FC_ASSERT(itr != idx.end(), "Oracle not found");
            auto aw = oracle_awaiting(db, itr->owner);
            return pm_oracle_api_object{pm_oracle_object(*itr), reliability_score(*itr, db.head_block_time()),
                                        aw.count, aw.oldest_age,
                                        rt_percentile(*itr, 50), rt_percentile(*itr, 95)};
        });
    }

    DEFINE_API(prediction_market_api, list_oracles) {
        CHECK_ARG_MIN_SIZE(2, 2)
        auto from  = args.args->at(0).as<uint32_t>();
        auto limit = args.args->at(1).as<uint32_t>();
        FC_ASSERT(limit <= 1000);
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_oracle_object> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
            auto itr = idx.begin();
            while (from > 0 && itr != idx.end()) { ++itr; --from; }
            while (result.size() < limit && itr != idx.end()) {
                result.push_back(pm_oracle_object(*itr));
                ++itr;
            }
            return result;
        });
    }

    // ── Disputes ─────────────────────────────────────────────────────────────────

    DEFINE_API(prediction_market_api, get_dispute) {
        CHECK_ARG_SIZE(1)
        auto market_id = args.args->at(0).as<int64_t>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto& idx = db.get_index<pm_dispute_index>().indices().get<by_market>();
            auto itr = idx.find(pm_market_id_type(market_id));
            FC_ASSERT(itr != idx.end(), "Dispute not found");
            return pm_dispute_object(*itr);
        });
    }

    // D6 note: the per-vote get_account() walk below is bounded — pm_dispute_vote_evaluator caps
    // ballots at pm_dispute_votes_per_market (M3, now 100k per q#686), so this API is O(≤100k)
    // per call. No pagination: the projection fields must tally every ballot.
    DEFINE_API(prediction_market_api, get_dispute_votes) {
        CHECK_ARG_SIZE(1)
        auto market_id = args.args->at(0).as<int64_t>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            pm_dispute_votes_api_object out;
            out.uphold_weight = out.challenge_weight = out.total_weight = 0;
            out.challenger_leads = false;
            out.proposed_outcome = -1;

            const auto& didx = db.get_index<pm_dispute_index>().indices().get<by_market>();
            auto dit = didx.find(pm_market_id_type(market_id));
            if (dit == didx.end()) return out;          // no dispute → all projection fields stay default
            out.proposed_outcome = dit->proposed_outcome;

            const auto& mkt = db.get<pm_market_object>(pm_market_id_type(market_id));
            out.expected_outcome = mkt.resolved_outcome;
            out.outcome_change_shares.assign(mkt.outcome_count, 0);

            // Lazy-pool → vesting-shares bridge, IDENTICAL to pm_dispute_finalize so the projected
            // verdict matches what the cron will actually apply at voting_end_time.
            const auto& gpo  = db.get_dynamic_global_properties();
            const auto vprice = gpo.get_vesting_share_price();
            const auto* lpool = db.find<pm_lazy_pool_object>(pm_lazy_pool_id_type(0));
            const int64_t pool_nav = (lpool && lpool->total_shares.value > 0)
                ? lpool->free_balance.value + lpool->allocated_balance.value + lpool->leverage_fund_used.value : 0;
            const int64_t pool_total_shares = (lpool ? lpool->total_shares.value : 0);
            const int64_t pool_nav_shares = (pool_nav > 0)
                ? (asset(share_type(pool_nav), TOKEN_SYMBOL) * vprice).amount.value : 0;
            const auto& ldidx = db.get_index<pm_lazy_deposit_index>().indices().get<by_deposit_account>();
            auto lazy_vote_weight = [&](const account_name_type& acct) -> int64_t {
                if (pool_nav <= 0 || pool_total_shares <= 0) return 0;
                auto d = ldidx.find(acct);
                if (d == ldidx.end() || d->shares.value <= 0) return 0;
                int64_t viz = (int64_t)(fc::uint128_t((uint64_t)pool_nav)
                              * fc::uint128_t((uint64_t)d->shares.value)
                              / fc::uint128_t((uint64_t)pool_total_shares)).lo;
                if (viz <= 0) return 0;
                return (asset(share_type(viz), TOKEN_SYMBOL) * vprice).amount.value;
            };

            int64_t max_rshares = 0, oracle_defense = 0, total_change = 0;
            const auto& vidx = db.get_index<pm_dispute_vote_index>().indices().get<by_market_voter>();
            auto vit = vidx.lower_bound(boost::make_tuple(pm_market_id_type(market_id), account_name_type()));
            while (vit != vidx.end() && vit->market == pm_market_id_type(market_id)) {
                // (a) legacy |vote_percent| proxy — preserved for compatibility
                int64_t pw = std::abs((int64_t)vit->vote_percent);
                if (vit->vote_outcome == -1) out.uphold_weight    += pw;
                else                         out.challenge_weight += pw;
                // (b) accurate stake-weighted tally — mirrors pm_dispute_finalize step 1
                int64_t w = db.get_account(vit->voter).effective_vesting_shares().amount.value
                          + lazy_vote_weight(vit->voter);
                max_rshares += w;
                int32_t pct = (int32_t)vit->vote_percent;
                if (pct > 0 && vit->vote_outcome >= 0 && vit->vote_outcome < (int16_t)mkt.outcome_count) {
                    int64_t r = w * pct / 10000;        // CHAIN_100_PERCENT
                    out.outcome_change_shares[vit->vote_outcome] += r;
                    total_change += r;
                } else {
                    int32_t a = pct < 0 ? -pct : pct;
                    oracle_defense += w * a / 10000;
                }
                out.votes.push_back(pm_dispute_vote_object(*vit));
                ++vit;
            }
            out.total_weight = out.uphold_weight + out.challenge_weight;

            const auto& mp = db.get_validator_schedule_object().median_props;
            if (out.total_weight > 0)
                out.challenger_leads =
                    (out.challenge_weight * 10000 / out.total_weight >= (int64_t)mp.pm_dispute_approve_min_percent);

            // ── Quorum + projected verdict (mirrors pm_dispute_finalize steps 2–5) ──
            const int64_t electorate = gpo.total_vesting_shares.amount.value + pool_nav_shares;
            const int64_t approve_min = (int64_t)(fc::uint128_t((uint64_t)electorate)
                * mp.pm_dispute_approve_min_percent / fc::uint128_t(10000)).lo;
            out.participation_shares   = max_rshares;
            out.electorate_shares      = electorate;
            out.quorum_required_shares = approve_min;
            out.quorum_percent_bp      = electorate > 0
                ? (int32_t)((fc::uint128_t((uint64_t)max_rshares) * 10000 / fc::uint128_t((uint64_t)electorate)).lo) : 0;
            out.quorum_reached         = max_rshares >= approve_min;
            out.oracle_defense_shares  = oracle_defense;
            out.change_shares          = total_change;

            const bool uphold = (max_rshares < approve_min) || (total_change <= 0) || (oracle_defense >= total_change);
            out.expected_uphold = uphold;
            if (uphold) {
                out.expected_outcome = mkt.resolved_outcome;
                out.expected_consensus_strength_bp = 0;
            } else {
                int best = 0;
                for (uint8_t i = 1; i < mkt.outcome_count; ++i)
                    if (out.outcome_change_shares[i] > out.outcome_change_shares[best]) best = (int)i;
                out.expected_outcome = (int16_t)best;
                const int64_t winning = out.outcome_change_shares[best];
                out.expected_consensus_strength_bp = max_rshares > 0
                    ? (int32_t)((fc::uint128_t((uint64_t)winning) * 10000 / fc::uint128_t((uint64_t)max_rshares)).lo) : 0;
            }
            return out;
        });
    }

    // list_oracle_disputes(oracle, from, limit): this oracle's currently OPEN disputes (status 0),
    // each tagged with its `stage` — "awaiting_response" (oracle has not answered) or
    // "awaiting_decision" (oracle answered, committee/resolver verdict pending) — mirroring the stored
    // disputes_awaiting_response / disputes_awaiting_decision gauges. Disputes are indexed by market,
    // not oracle, so we walk the small open-dispute set via by_auto_close(status 0) and keep those
    // whose market names this oracle. Each row carries the enriched market card for the UI.
    DEFINE_API(prediction_market_api, list_oracle_disputes) {
        CHECK_ARG_MIN_SIZE(3, 3)
        auto oracle = args.args->at(0).as<account_name_type>();
        auto from   = args.args->at(1).as<uint32_t>();
        auto limit  = args.args->at(2).as<uint32_t>();
        FC_ASSERT(limit <= 1000);
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<fc::variant> result;
            result.reserve(limit);
            const auto& didx  = db.get_index<pm_dispute_index>().indices().get<by_auto_close>();
            const auto& mbyid = db.get_index<pm_market_index>().indices().get<by_id>();
            auto itr = didx.lower_bound(boost::make_tuple((uint8_t)0, time_point_sec(0), pm_dispute_id_type()));
            for (; itr != didx.end() && itr->status == 0; ++itr) {
                auto mit = mbyid.find(itr->market);
                if (mit == mbyid.end() || mit->oracle != oracle) continue;
                if (from > 0) { --from; continue; }
                fc::mutable_variant_object o;
                o["market"]                   = market_card(db, *mit);
                o["market_id"]                = mit->id._id;
                o["disputer"]                 = itr->disputer;
                o["proposed_outcome"]         = itr->proposed_outcome;
                o["filed_time"]               = itr->filed_time;
                o["oracle_response_deadline"] = itr->oracle_response_deadline;
                o["oracle_response_time"]     = itr->oracle_response_time;
                o["voting_end_time"]          = itr->voting_end_time;
                o["auto_close_time"]          = itr->auto_close_time;
                o["dispute_mode"]             = itr->dispute_mode;
                o["stage"]                    = (itr->oracle_response_time == fc::time_point_sec())
                                                  ? "awaiting_response" : "awaiting_decision";
                result.push_back(fc::variant(std::move(o)));
                if (result.size() >= limit) break;
            }
            return result;
        });
    }

    // ── Lazy pool & chain properties ───────────────────────────────────────────────

    DEFINE_API(prediction_market_api, get_lazy_pool) {
        CHECK_ARG_SIZE(0)
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto* p = db.find<pm_lazy_pool_object>(pm_lazy_pool_id_type(0));
            FC_ASSERT(p != nullptr, "Lazy pool not initialized");
            return pm_lazy_pool_object(*p);
        });
    }

    DEFINE_API(prediction_market_api, get_lazy_deposit) {
        CHECK_ARG_SIZE(1)
        auto account = args.args->at(0).as<account_name_type>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto& idx = db.get_index<pm_lazy_deposit_index>().indices().get<by_deposit_account>();
            auto itr = idx.find(account);
            FC_ASSERT(itr != idx.end(), "No lazy deposit for account");
            return pm_lazy_deposit_object(*itr);
        });
    }

    // Pending (queued) lazy-pool withdrawals for an account: amounts still owed that could not be
    // paid in full from free_balance yet. Empty = nothing queued (already paid or never queued).
    DEFINE_API(prediction_market_api, get_lazy_withdraw_requests) {
        CHECK_ARG_SIZE(1)
        auto account = args.args->at(0).as<account_name_type>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_lazy_withdraw_request_object> out;
            const auto& idx = db.get_index<pm_lazy_withdraw_request_index>().indices().get<by_request_account>();
            auto range = idx.equal_range(account);
            for (auto it = range.first; it != range.second; ++it) out.push_back(*it);
            return out;
        });
    }

    // get_deferred_claims(market, [from=0], [limit=100])
    // F1/#300: the outcome-contingent early-exit claims still pending on a market, in FIFO exit order
    // (by_claim_market == id order). Each carries {account, kind (0 bet-cancel / 1 leverage-close),
    // outcome_index (pays only if this outcome wins), claim_amount, exit_time}. Claims are consumed and
    // removed at settlement, so a resolved market returns []. `from` is a starting claim id (0 = oldest),
    // `limit` caps the page (<= 1000). Lets a client show a position's "pending early-exit claim" and its
    // FIFO standing against the bounded reward bucket.
    DEFINE_API(prediction_market_api, get_deferred_claims) {
        CHECK_ARG_MIN_SIZE(1, 3)
        auto market = args.args->at(0).as<int64_t>();
        uint32_t from  = args.args->size() > 1 ? args.args->at(1).as<uint32_t>() : 0;
        uint32_t limit = args.args->size() > 2 ? args.args->at(2).as<uint32_t>() : 100;
        FC_ASSERT(limit <= 1000, "limit must be <= 1000");
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_deferred_claim_object> out;
            const auto& idx = db.get_index<pm_deferred_claim_index>().indices().get<by_claim_market>();
            auto it = idx.lower_bound(boost::make_tuple(pm_market_id_type(market),
                                                        pm_deferred_claim_id_type(from)));
            for (; it != idx.end() && it->market == pm_market_id_type(market) && out.size() < limit; ++it)
                out.push_back(*it);
            return out;
        });
    }

    // get_account_commits(account, [open_only=true])
    // The account's commit-reveal commitments (pm_commit_object) in id order. A hidden/batch bet is a
    // two-step flow: pm_commit_bet stores a commitment + escrow, then pm_reveal_bet(commit_id, …) reveals
    // it — and the reveal op needs the on-chain commit_id, which the client otherwise had no way to look
    // up (only the by_commit_account index existed, unexposed). By default returns only OPEN commits
    // (status==0, still revealable); pass open_only=false to include revealed(1)/forfeited(2) for history.
    DEFINE_API(prediction_market_api, get_account_commits) {
        CHECK_ARG_MIN_SIZE(1, 2)
        auto account = args.args->at(0).as<account_name_type>();
        bool open_only = args.args->size() > 1 ? args.args->at(1).as<bool>() : true;
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_commit_object> out;
            const auto& idx = db.get_index<pm_commit_index>().indices().get<by_commit_account>();
            auto range = idx.equal_range(account);
            for (auto it = range.first; it != range.second; ++it)
                if (!open_only || it->status == 0) out.push_back(*it);
            return out;
        });
    }

    DEFINE_API(prediction_market_api, get_pm_chain_properties) {
        CHECK_ARG_SIZE(0)
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            return db.get_validator_schedule_object().median_props;
        });
    }

    // ── Metadata (parsed off-chain from each market's `metadata` JSON) ──────────────

    DEFINE_API(prediction_market_api, get_market_meta) {
        CHECK_ARG_SIZE(1)
        auto market_id = args.args->at(0).as<int64_t>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto& idx = db.get_index<pm_market_meta_index>().indices().get<by_meta_market>();
            auto itr = idx.find(pm_market_id_type(market_id));
            FC_ASSERT(itr != idx.end(), "No metadata for market");
            return pm_market_meta_object(*itr);
        });
    }

    // list_markets_by_category(category, from, limit, [jurisdiction=""], [subcategory=""], [tag=""], [sort="newest"], [hide_children=true], [show_risky=false], [status=-1])
    // Optional filters: jurisdiction (exclude markets banning it), subcategory (exact), tag (CSV membership).
    // status (default -1 = any): keep only markets in this lifecycle status (0 pending · 1 active · 3
    // resolved …). Filtering server-side lets a client page an "active only" browse accurately instead
    // of over-fetching and dropping rows locally.
    // hide_children (default true): drop child/prop markets of a split match so the listing shows only
    // parent markets; the props stay reachable via the parent's event page. Pass false to include them.
    // sort: "newest" (market id desc, default) · "oldest" (id asc) · "volume" (bets_sum desc) ·
    // "expiration" (betting_expiration asc). volume/expiration load each matching market, so they
    // scan the whole (non-pruned) category before paging; newest/oldest sort on the meta id alone.
    // When the market is loaded (volume/expiration sort or a status filter) each returned row also carries
    // a `volume` field (= bets_sum, raw shares) so clients render exact volume badges and rank across
    // categories without a second round-trip; unfiltered newest/oldest rows omit it (client fills lazily).
    DEFINE_API(prediction_market_api, list_markets_by_category) {
        CHECK_ARG_MIN_SIZE(3, 10)
        auto category     = args.args->at(0).as<std::string>();
        auto from         = args.args->at(1).as<uint32_t>();
        auto limit        = args.args->at(2).as<uint32_t>();
        auto jurisdiction = GET_OPTIONAL_ARG(3, std::string, std::string()); // exclude markets banning it
        auto subcategory  = GET_OPTIONAL_ARG(4, std::string, std::string());
        auto tag          = GET_OPTIONAL_ARG(5, std::string, std::string());
        auto sort         = GET_OPTIONAL_ARG(6, std::string, std::string("newest"));
        auto hide_children= GET_OPTIONAL_ARG(7, bool, true); // drop child/prop markets by default
        auto show_risky   = GET_OPTIONAL_ARG(8, bool, false); // reveal markets of under-insured oracles
        auto status       = GET_OPTIONAL_ARG(9, int8_t, (int8_t)-1); // -1 = any lifecycle status
        FC_ASSERT(limit <= 1000);
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const bool need_status = (status != (int8_t)-1);
            const bool need_market = (sort == "volume" || sort == "expiration" || need_status);
            const bool sorted = (sort == "volume" || sort == "expiration");
            const auto& idx = db.get_index<pm_market_meta_index>().indices().get<by_meta_category>();

            // D2: shared row builder (byte-identical output for both paths).
            auto emit_row = [&](const pm_market_meta_object& mm, const pm_market_object* mk) -> fc::variant {
                fc::variant v; fc::to_variant(pm_market_meta_object(mm), v);
                fc::mutable_variant_object o(v.get_object());
                if (need_market)
                    // surface the bets_sum already loaded → exact client-side volume
                    // badge + global cross-category ranking, no extra get_market_weight_sums call
                    // (pruned market → 0, same shape the pre-D2 code emitted)
                    o["volume"] = mk ? mk->bets_sum.value : 0;
                // The meta object carries no lifecycle state, so category/tag browsing rendered every
                // card as "active". Attach the live status/result from the market (query-time, bounded
                // by `limit`) so cards show the right badge and can mark the resolved winner.
                if (const auto* mko = db.find<pm_market_object>(mm.market)) {
                    o["status"]           = mko->status;
                    o["payout_status"]    = mko->payout_status;
                    o["resolved_outcome"] = mko->resolved_outcome;
                }
                // Emit tags / banned_jurisdictions as ARRAYS (not raw CSV) and expose a reconstructed
                // `metadata` object, matching market_card so clients read market.metadata.tags uniformly
                // across every listing — the UI never parses raw meta CSV (owner 2026-07-12).
                fc::variant tags_arr   = csv_to_array(to_string(mm.tags));
                fc::variant banned_arr = csv_to_array(to_string(mm.banned_jurisdictions));
                o["tags"]                 = tags_arr;
                o["banned_jurisdictions"] = banned_arr;
                fc::mutable_variant_object md;
                md("title", to_string(mm.title))("image", to_string(mm.image))
                  ("category", to_string(mm.category))("subcategory", to_string(mm.subcategory))
                  ("tags", tags_arr)("banned_jurisdictions", banned_arr)
                  ("condition_id", to_string(mm.condition_id))("description", to_string(mm.description))
                  ("event", to_string(mm.event))("event_title", to_string(mm.event_title));
                o["metadata"] = fc::variant(std::move(md));
                return fc::variant(std::move(o));
            };

            std::vector<fc::variant> result;
            result.reserve(limit);

            // newest/oldest: by_meta_category is (category, market id), so the range walked in the
            // desired direction IS the sort order — filter on the fly and stop at the page. No
            // full-slice scan, no sort (the old materialize+stable_sort-by-id was redundant).
            if (!sorted) {
                const bool newest = (sort != "oldest");
                auto first = idx.lower_bound(category);
                auto last  = idx.upper_bound(category);
                auto take = [&](decltype(first) itr) {
                    const auto* mk = need_market ? db.find<pm_market_object>(itr->market) : nullptr;
                    if (hide_children && itr->child) return;                    // parent-only by default
                    if (!jurisdiction.empty() && meta_csv_contains(to_string(itr->banned_jurisdictions), jurisdiction)) return;
                    if (!subcategory.empty() && to_string(itr->subcategory) != subcategory) return;
                    if (!tag.empty() && !meta_csv_contains_ci(to_string(itr->tags), tag)) return; // case-insensitive tags
                    if (need_status && (!mk || mk->status != status)) return;   // server-side lifecycle filter
                    if (!show_risky) { // hide markets whose oracle is under-insured (aggregate risk floor)
                        const auto* mko = mk ? mk : db.find<pm_market_object>(itr->market);
                        if (mko && oracle_below_risk_floor(db, mko->oracle)) return;
                    }
                    if (from > 0) { --from; return; }
                    if (result.size() < limit) result.push_back(emit_row(*itr, mk));
                };
                if (newest) {
                    for (auto itr = last; itr != first && (from > 0 || result.size() < limit); ) { --itr; take(itr); }
                } else {
                    for (auto itr = first; itr != last && (from > 0 || result.size() < limit); ++itr) take(itr);
                }
                return result;
            }

            // volume/expiration: the sort needs the matching set — materialize it, but capped;
            // beyond MAX_PM_SORT_POOL the listing degrades to a newest-first walk instead.
            struct entry { const pm_market_meta_object* m; const pm_market_object* mk; int64_t key; };
            std::vector<entry> es;
            bool over = false;
            for (auto itr = idx.lower_bound(category);
                 itr != idx.end() && to_string(itr->category) == category; ++itr) {
                const auto* mk = db.find<pm_market_object>(itr->market); // need_market is true here
                if (hide_children && itr->child) continue;
                if (!jurisdiction.empty() && meta_csv_contains(to_string(itr->banned_jurisdictions), jurisdiction)) continue;
                if (!subcategory.empty() && to_string(itr->subcategory) != subcategory) continue;
                if (!tag.empty() && !meta_csv_contains_ci(to_string(itr->tags), tag)) continue;
                if (need_status && (!mk || mk->status != status)) continue;
                if (!show_risky) {
                    if (mk && oracle_below_risk_floor(db, mk->oracle)) continue;
                }
                es.push_back({ &*itr, mk,
                    !mk ? ((sort == "volume") ? (int64_t)0 : std::numeric_limits<int64_t>::max())
                        : ((sort == "volume") ? (int64_t)mk->bets_sum.value
                                              : (int64_t)mk->betting_expiration.sec_since_epoch()) });
                if (es.size() > MAX_PM_SORT_POOL) { over = true; break; }
            }
            if (over) {
                auto first = idx.lower_bound(category);
                for (auto itr = idx.upper_bound(category); itr != first && (from > 0 || result.size() < limit); ) {
                    --itr;
                    const auto* mk = need_market ? db.find<pm_market_object>(itr->market) : nullptr;
                    if (hide_children && itr->child) continue;
                    if (!jurisdiction.empty() && meta_csv_contains(to_string(itr->banned_jurisdictions), jurisdiction)) continue;
                    if (!subcategory.empty() && to_string(itr->subcategory) != subcategory) continue;
                    if (!tag.empty() && !meta_csv_contains_ci(to_string(itr->tags), tag)) continue;
                    if (need_status && (!mk || mk->status != status)) continue;
                    if (!show_risky) {
                        const auto* mko = mk ? mk : db.find<pm_market_object>(itr->market);
                        if (mko && oracle_below_risk_floor(db, mko->oracle)) continue;
                    }
                    if (from > 0) { --from; continue; }
                    result.push_back(emit_row(*itr, mk));
                }
                return result;
            }
            if (sort == "volume")
                std::stable_sort(es.begin(), es.end(), [](const entry& a, const entry& b){ return a.key > b.key; });
            else // "expiration"
                std::stable_sort(es.begin(), es.end(), [](const entry& a, const entry& b){ return a.key < b.key; });
            for (uint32_t i = from; i < es.size() && result.size() < limit; ++i)
                result.push_back(emit_row(*es[i].m, es[i].mk));
            return result;
        });
    }

    // list_markets_by_event(event, from, limit) — sibling markets sharing a parent event key
    // (one match/game), oldest-first (meta id asc). Returns full market cards so a client can render
    // an event page with each child's outcomes/volume in one round-trip. Empty event key yields
    // nothing (standalone markets are not an "event"). Pruned markets are skipped.
    DEFINE_API(prediction_market_api, list_markets_by_event) {
        CHECK_ARG_MIN_SIZE(3, 4)
        auto event = args.args->at(0).as<std::string>();
        auto from  = args.args->at(1).as<uint32_t>();
        auto limit = args.args->at(2).as<uint32_t>();
        auto show_risky = GET_OPTIONAL_ARG(3, bool, false); // reveal markets of under-insured oracles
        FC_ASSERT(limit <= 1000);
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<fc::variant> result;
            if (event.empty()) return result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_market_meta_index>().indices().get<by_meta_event>();
            for (auto itr = idx.lower_bound(event);
                 itr != idx.end() && to_string(itr->event) == event && result.size() < limit; ++itr) {
                const auto* mk = db.find<pm_market_object>(itr->market);
                if (!mk) continue;
                if (!show_risky && oracle_below_risk_floor(db, mk->oracle)) continue;
                if (from > 0) { --from; continue; }
                result.push_back(market_card(db, *mk));
            }
            return result;
        });
    }

    // get_market_categories() — taxonomy + live counts + hot tags, aggregated over the currently
    // indexed (non-pruned) markets. Categories sorted by count desc; hot_tags = top 20 by count
    // (jurisdiction-* tags excluded, matching the browse filter). No args.
    DEFINE_API(prediction_market_api, get_market_categories) {
        CHECK_ARG_SIZE(0)
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            // D3: the result only changes when meta rows change, i.e. on a new block at the earliest —
            // cache it per head block (winner_agg pattern) instead of rescanning the whole meta index
            // (~56k rows × 3 to_string + CSV parse + 2 sorts) on every call.
            static thread_local uint32_t cache_epoch = 0;
            static thread_local pm_market_categories_api_object cache;
            const uint32_t epoch = db.head_block_num();
            if (cache_epoch == epoch) return cache;
            cache = pm_market_categories_api_object();
            std::map<std::string, uint32_t>                            cat_total;
            std::map<std::string, std::map<std::string, uint32_t>>     cat_sub;
            std::map<std::string, uint32_t>                            tag_count;
            const auto& cidx = db.get_index<pm_market_meta_index>().indices().get<by_meta_category>();
            for (auto it = cidx.begin(); it != cidx.end(); ++it) {
                const std::string cat = to_string(it->category);
                if (cat.empty()) continue;
                cat_total[cat]++;
                const std::string sub = to_string(it->subcategory);
                if (!sub.empty()) cat_sub[cat][sub]++;
                const std::string tags = to_string(it->tags); // comma-joined
                size_t start = 0;
                while (start <= tags.size()) {
                    size_t comma = tags.find(',', start);
                    std::string t = tags.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                    if (!t.empty() && t.rfind("jurisdiction", 0) != 0) tag_count[t]++;
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
            }

            pm_market_categories_api_object out;
            for (auto& kv : cat_total) {
                pm_category_count c;
                c.category = kv.first;
                c.count    = kv.second;
                auto sit = cat_sub.find(kv.first);
                if (sit != cat_sub.end())
                    for (auto& sc : sit->second) c.subcategories.push_back({sc.first, sc.second});
                out.categories.push_back(std::move(c));
            }
            std::stable_sort(out.categories.begin(), out.categories.end(),
                [](const pm_category_count& a, const pm_category_count& b){ return a.count > b.count; });
            for (auto& kv : tag_count) out.hot_tags.push_back({kv.first, kv.second});
            std::stable_sort(out.hot_tags.begin(), out.hot_tags.end(),
                [](const pm_tag_count& a, const pm_tag_count& b){ return a.count > b.count; });
            if (out.hot_tags.size() > 20) out.hot_tags.resize(20);
            cache = out;
            cache_epoch = epoch;
            return cache;
        });
    }

    // get_category_tag_counts(category) — authoritative per-tag market counts WITHIN one category
    // (all tags, not the global top-20 of get_market_categories). Lets a browse UI show a stable tag
    // count that doesn't change with the currently-loaded page. Scans only the category's index slice.
    // jurisdiction-* tags excluded (they aren't browse tags). Returns tags in the `hot_tags` field
    // (the `categories` field is left empty), sorted by count desc.
    DEFINE_API(prediction_market_api, get_category_tag_counts) {
        CHECK_ARG_SIZE(1)
        auto category = args.args->at(0).as<std::string>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::map<std::string, uint32_t> tag_count;
            const auto& idx = db.get_index<pm_market_meta_index>().indices().get<by_meta_category>();
            for (auto it = idx.lower_bound(category); it != idx.end() && to_string(it->category) == category; ++it) {
                const std::string tags = to_string(it->tags); // comma-joined
                size_t start = 0;
                while (start <= tags.size()) {
                    size_t comma = tags.find(',', start);
                    std::string t = tags.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                    if (!t.empty() && t.rfind("jurisdiction", 0) != 0) tag_count[t]++;
                    if (comma == std::string::npos) break;
                    start = comma + 1;
                }
            }
            pm_market_categories_api_object out;
            for (auto& kv : tag_count) out.hot_tags.push_back({kv.first, kv.second});
            std::stable_sort(out.hot_tags.begin(), out.hot_tags.end(),
                [](const pm_tag_count& a, const pm_tag_count& b){ return a.count > b.count; });
            return out;
        });
    }

    // get_market_kline(market_id, from = 0, limit = 1000)
    // Returns a time-window of per-outcome weight snapshots, ascending by seq (oldest→newest), for a
    // thin client to plot. Pagination is offset-from-newest: `from` newest points are skipped, then up
    // to `limit` (≤ 1000) are returned. So (from=0, limit=1000) is the latest ≤1000 changes, and
    // (from=1000, limit=1000) steps another 1000 further back — repeat to lazy-load older history.
    DEFINE_API(prediction_market_api, get_market_kline) {
        CHECK_ARG_MIN_SIZE(1, 3)
        auto market_id = args.args->at(0).as<int64_t>();
        auto from      = GET_OPTIONAL_ARG(1, uint32_t, 0u);
        auto limit     = GET_OPTIONAL_ARG(2, uint32_t, 1000u);
        FC_ASSERT(limit <= 1000, "limit must be <= 1000");
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_kline_api_object> out;
            const auto market = pm_market_id_type(market_id);
            const auto& idx = db.get_index<pm_market_kline_index>().indices().get<by_kline_market_seq>();

            // Highest seq present (records are contiguous 0..max ⇒ count = max + 1).
            auto last = idx.upper_bound(boost::make_tuple(market, std::numeric_limits<uint32_t>::max()));
            if (last == idx.begin()) return out;
            --last;
            if (last->market != market) return out;
            const uint32_t count = last->seq + 1;
            if (from >= count) return out;

            const uint32_t end_excl = count - from;                        // newest seq not skipped (exclusive)
            const uint32_t start    = end_excl > limit ? end_excl - limit : 0;
            out.reserve(end_excl - start);
            for (auto it = idx.lower_bound(boost::make_tuple(market, start));
                 it != idx.end() && it->market == market && it->seq < end_excl; ++it) {
                pm_kline_api_object r;
                r.seq       = it->seq;
                r.timestamp = it->timestamp.sec_since_epoch();
                r.reason    = it->reason;
                r.bets_sum  = it->bets_sum;
                r.weights.assign(it->weights.begin(), it->weights.end());
                out.push_back(std::move(r));
            }
            return out;
        });
    }

    // ── Leverage previews ───────────────────────────────────────────────────────────
    // These reuse pm::leverage::* (the SAME frozen math the evaluators run) so a preview equals
    // what pm_leverage_open/close/convert would compute at the head block. Non-consensus quotes.

    // get_leverage_quote(market_id, outcome_index, collateral)
    // Mirrors pm_leverage_open_evaluator: same constraint ladder + max_leverage_loan() search.
    DEFINE_API(prediction_market_api, get_leverage_quote) {
        CHECK_ARG_SIZE(3)
        auto market_id     = args.args->at(0).as<int64_t>();
        auto outcome_index = args.args->at(1).as<int16_t>();
        auto collateral    = args.args->at(2).as<int64_t>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto& mp = db.get_validator_schedule_object().median_props;
            const auto now = db.head_block_time();
            const auto* m  = db.find<pm_market_object>(pm_market_id_type(market_id));
            FC_ASSERT(m != nullptr, "Market not found");
            const auto& mkt = *m;

            pm_leverage_quote_api_object out;
            out.outcome_index         = outcome_index;
            out.collateral            = share_type(collateral);
            out.pool_profit_percent   = mp.pm_leverage_pool_profit_percent;
            out.safety_margin_percent = mp.pm_leverage_safety_margin_percent;
            out.max_slippage_percent  = mp.pm_leverage_max_slippage_percent;
            out.m_factor_percent      = mp.pm_leverage_m_factor_percent;
            out.expiration_buffer_sec = mp.pm_leverage_expiration_buffer_sec;
            out.funding_rate_ppm_per_day = mp.pm_leverage_funding_rate_ppm_per_day;
            // Open-ended markets (betting_expiration == 0) have no protocol force-close point.
            out.auto_close_time       = (mkt.betting_expiration == fc::time_point_sec())
                                          ? fc::time_point_sec()
                                          : fc::time_point_sec(mkt.betting_expiration - fc::seconds(mp.pm_leverage_expiration_buffer_sec));

            auto fail = [&](const char* c, const std::string& why) {
                out.failed_constraints.push_back({std::string(c), why});
            };
            // Eligibility ladder — mirrors the evaluator's FC_ASSERTs (collected, not thrown).
            if (!mp.pm_leverage_enabled)              fail("leverage_disabled", "Leverage is disabled by governance");
            if (mkt.market_type != 0)                 fail("cpmm_binary_only", "Leverage is CPMM-binary only");
            if (mkt.status != 1)                      fail("market_inactive", "Market is not active");
            if (outcome_index != 0 && outcome_index != 1) fail("cpmm_binary_only", "outcome_index must be 0/1");
            if (mkt.betting_expiration != fc::time_point_sec()
                && now >= mkt.betting_expiration - fc::seconds(mp.pm_leverage_expiration_buffer_sec))
                fail("expiration_buffer", "Too close to betting expiration for leverage");
            if (mkt.liquidity_sum < mp.pm_leverage_min_market_liquidity.amount)
                fail("min_market_liquidity", "Market liquidity below leverage minimum");

            const auto* pool = db.find<pm_lazy_pool_object>(pm_lazy_pool_id_type(0));
            const int64_t free_bal   = pool ? pool->free_balance.value : 0;
            const int64_t fund_used  = pool ? pool->leverage_fund_used.value : 0;
            const int64_t free_amount = free_bal - fund_used;
            const int64_t fund_total  = (int64_t)(fc::uint128_t((uint64_t)std::max<int64_t>(free_bal, 0))
                                        * fc::uint128_t(mp.pm_leverage_fund_percent) / fc::uint128_t(100u)).lo;
            const int64_t fund_available = fund_total - fund_used;
            const int64_t per_pos_cap = fund_available > 0
                ? (int64_t)(fc::uint128_t((uint64_t)fund_available)
                    * fc::uint128_t(mp.pm_leverage_max_per_position_bp) / fc::uint128_t(10000u)).lo : 0;
            const int64_t pos_cap = (int64_t)(fc::uint128_t((uint64_t)std::max<int64_t>(mkt.liquidity_sum.value, 0))
                                    * fc::uint128_t(mp.pm_leverage_max_position_ratio_percent) / fc::uint128_t(100u)).lo;
            out.pool_free_amount    = share_type(free_amount);
            out.fund_available      = share_type(fund_available);
            out.per_position_cap    = share_type(per_pos_cap);
            out.market_position_cap = share_type(pos_cap);

            if (free_amount <= 0 || fund_available <= 0) fail("fund_availability", "Leverage fund exhausted");
            const int64_t pos_room = pos_cap - collateral; // loan headroom vs market-size cap
            if (pos_room <= 0) fail("position_size", "Collateral already at/above market position cap");
            // Per-position cap vs loan floor: if pool is too small, max loan < pm_min_liquidity → no valid loan exists.
            // The evaluator enforces loan >= pm_min_liquidity (anti-Sybil, pm_evaluator.cpp:1439), so quote must
            // surface this impossibility rather than returning available:true for loans that will fail at apply.
            if (per_pos_cap < mp.pm_min_liquidity.amount)
                fail("loan_floor_above_cap", "Per-position cap below minimum loan (pool too small for leverage)");

            // Only search when structurally eligible (no blocking constraint above, valid collateral).
            const bool eligible = out.failed_constraints.empty() && collateral > 0;
            int64_t max_loan = 0;
            if (eligible) {
                int64_t hi = std::min(free_amount, per_pos_cap);
                hi = std::min(hi, pos_room);
                if (hi < 0) hi = 0;
                max_loan = pm::leverage::max_leverage_loan(
                    mkt.reserve_a.value, mkt.reserve_b.value, mkt.k, collateral, (int)outcome_index, hi,
                    mp.pm_leverage_pool_profit_percent, mp.pm_leverage_safety_margin_percent,
                    mp.pm_leverage_max_slippage_percent, mp.pm_leverage_m_factor_percent);
                if (max_loan <= 0) fail("solvency", "No loan size passes the worst-case solvency check");
            }

            out.max_loan  = share_type(max_loan);
            out.available = (max_loan > 0);
            out.max_leverage_x100 = (collateral > 0)
                ? (uint32_t)(((int64_t)(collateral + max_loan) * 100) / collateral) : 100;

            // Build up to 12 evenly-spaced stops in (0, max_loan]; skip sub-1.01× points.
            if (out.available) {
                const int N = 12;
                uint32_t last_lev = 0;
                for (int i = 1; i <= N; ++i) {
                    int64_t loan = (int64_t)((fc::uint128_t((uint64_t)max_loan) * (uint64_t)i / (uint64_t)N).lo);
                    if (loan <= 0) continue;
                    uint32_t lev = (uint32_t)(((int64_t)(collateral + loan) * 100) / collateral);
                    if (lev < 101 || lev == last_lev) continue; // min 1.01×, dedupe
                    last_lev = lev;
                    pm::leverage::cpmm_fill f = pm::leverage::cpmm_buy(
                        mkt.reserve_a.value, mkt.reserve_b.value, mkt.k, collateral + loan, (int)outcome_index);
                    int64_t thr = pm::leverage::liquidation_threshold(loan, mp.pm_leverage_pool_profit_percent);
                    int64_t cur_cv = pm::leverage::cancel_value(f.new_reserve_a, f.new_reserve_b, mkt.k,
                                                                f.tokens, (int)outcome_index);
                    int64_t mm  = pm::leverage::worst_opposing_bet(f.new_reserve_a, f.new_reserve_b,
                                    mp.pm_leverage_max_slippage_percent, mp.pm_leverage_m_factor_percent);
                    int64_t cvw = pm::leverage::cancel_value_after_opposing(f.new_reserve_a, f.new_reserve_b, mkt.k,
                                    f.tokens, (int)outcome_index, mm);
                    pm_leverage_stop s;
                    s.leverage_x100           = lev;
                    s.loan                    = share_type(loan);
                    s.total_bet               = share_type(collateral + loan);
                    s.expected_tokens         = share_type(f.tokens);
                    s.pool_profit             = share_type(thr - loan);
                    s.liquidation_threshold   = share_type(thr);
                    s.current_cancel_value    = share_type(cur_cv);
                    s.worst_case_cancel_value = share_type(cvw);
                    out.stops.push_back(std::move(s));
                }
            }
            return out;
        });
    }

    // get_leverage_close_preview(position_id) — mirrors pm_leverage_close_evaluator at head reserves.
    DEFINE_API(prediction_market_api, get_leverage_close_preview) {
        CHECK_ARG_SIZE(1)
        auto position_id = args.args->at(0).as<int64_t>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto* p = db.find<pm_leverage_position_object>(pm_leverage_position_id_type(position_id));
            FC_ASSERT(p != nullptr, "Position not found");
            const auto& pos = *p;
            const auto& mkt = db.get<pm_market_object, by_id>(pos.market);

            const int64_t cv = pm::leverage::cancel_value(mkt.reserve_a.value, mkt.reserve_b.value, mkt.k,
                                                          pos.tokens.value, (int)pos.outcome_index);
            const int64_t obligation = pos.liquidation_threshold.value;
            const int64_t bettor = cv >= obligation ? cv - obligation : 0;

            pm_leverage_close_preview_api_object out;
            out.position_id       = position_id;
            out.outcome_index     = pos.outcome_index;
            out.cancel_value      = share_type(cv);
            out.pool_obligation   = share_type(obligation);
            out.bettor_receives   = share_type(bettor);
            out.collateral        = pos.collateral;
            out.loan              = pos.loan;
            out.pool_profit_charge = pos.pool_profit;
            out.closeable         = (cv >= obligation);
            out.loss_vs_collateral = pos.collateral.value - bettor;
            out.loss_percent_bp   = pos.collateral.value > 0
                ? (int32_t)((out.loss_vs_collateral * 10000) / pos.collateral.value) : 0;
            return out;
        });
    }

    // get_leverage_convert_preview(position_id) — mirrors pm_leverage_convert_evaluator at head reserves.
    DEFINE_API(prediction_market_api, get_leverage_convert_preview) {
        CHECK_ARG_SIZE(1)
        auto position_id = args.args->at(0).as<int64_t>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto& mp = db.get_validator_schedule_object().median_props;
            const auto* p = db.find<pm_leverage_position_object>(pm_leverage_position_id_type(position_id));
            FC_ASSERT(p != nullptr, "Position not found");
            const auto& pos = *p;
            const auto& mkt = db.get<pm_market_object, by_id>(pos.market);

            const int64_t cv = pm::leverage::cancel_value(mkt.reserve_a.value, mkt.reserve_b.value, mkt.k,
                                                          pos.tokens.value, (int)pos.outcome_index);
            const int64_t obligation = pos.liquidation_threshold.value;
            const int64_t profit = cv > obligation ? cv - obligation : 0;
            const int64_t fee = (int64_t)(fc::uint128_t((uint64_t)profit)
                                * fc::uint128_t(mp.pm_conversion_profit_cost_percent) / fc::uint128_t(100u)).lo;

            pm_leverage_convert_preview_api_object out;
            out.position_id                    = position_id;
            out.outcome_index                  = pos.outcome_index;
            out.cancel_value                   = share_type(cv);
            out.pool_obligation                = share_type(obligation);
            out.current_profit                 = share_type(profit);
            out.conversion_profit_cost_percent = mp.pm_conversion_profit_cost_percent;
            out.conversion_fee                 = share_type(fee);
            out.total_user_payment             = share_type(obligation + fee);
            out.convertible                    = (profit > 0);
            return out;
        });
    }

    // ── Enriched market view + lazy allocations ─────────────────────────────────────

    // get_market_full(market_id, [account]) — one call: market + outcomes + weight sums + oracle +
    // metadata, plus (when account given) that account's bets / leverage / LP on THIS market.
    DEFINE_API(prediction_market_api, get_market_full) {
        CHECK_ARG_MIN_SIZE(1, 2)
        auto market_id = args.args->at(0).as<int64_t>();
        auto account   = GET_OPTIONAL_ARG(1, account_name_type, account_name_type());
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto* m = db.find<pm_market_object>(pm_market_id_type(market_id));
            FC_ASSERT(m != nullptr, "Market not found");
            const auto& mkt = *m;

            std::vector<pm_outcome_object> outcomes;
            {
                const auto& idx = db.get_index<pm_outcome_index>().indices().get<by_market_outcome>();
                for (auto it = idx.lower_bound(boost::make_tuple(mkt.id, (uint8_t)0));
                     it != idx.end() && it->market == mkt.id; ++it)
                    outcomes.push_back(pm_outcome_object(*it));
            }

            fc::optional<pm_oracle_api_object> oracle;
            {
                const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
                auto oit = oidx.find(mkt.oracle);
                if (oit != oidx.end()) {
                    auto oaw = oracle_awaiting(db, oit->owner);
                    oracle = pm_oracle_api_object{pm_oracle_object(*oit), reliability_score(*oit, db.head_block_time()),
                                                  oaw.count, oaw.oldest_age,
                                                  rt_percentile(*oit, 50), rt_percentile(*oit, 95)};
                }
            }

            fc::optional<pm_market_meta_object> meta;
            {
                const auto& midx = db.get_index<pm_market_meta_index>().indices().get<by_meta_market>();
                auto mit = midx.find(mkt.id);
                if (mit != midx.end()) meta = pm_market_meta_object(*mit);
            }

            std::vector<pm_position_api_object>      my_positions;
            std::vector<pm_leverage_position_object> my_leverage;
            std::vector<pm_liquidity_object>         my_liquidity;
            if (account != account_name_type()) {
                // D5: each sub-list capped — a repeated get_market_full on a heavily-used market
                // must not rebuild unbounded per-account sections.
                static const size_t MY_CAP = 1000;
                const auto& bidx = db.get_index<pm_bet_index>().indices().get<by_market_account>();
                for (auto it = bidx.lower_bound(boost::make_tuple(mkt.id, account, pm_bet_id_type()));
                     it != bidx.end() && it->market == mkt.id && it->account == account
                     && my_positions.size() < MY_CAP; ++it)
                    my_positions.push_back(pm_position_api_object{
                        pm_bet_object(*it), expected_payout(db, *it, mkt), mkt.status, mkt.resolved_outcome});
                const auto& lidx = db.get_index<pm_leverage_position_index>().indices().get<by_lev_market_status>();
                for (auto it = lidx.lower_bound(boost::make_tuple(mkt.id, (uint8_t)0, pm_leverage_position_id_type()));
                     it != lidx.end() && it->market == mkt.id && my_leverage.size() < MY_CAP; ++it)
                    if (it->account == account) my_leverage.push_back(pm_leverage_position_object(*it));
                const auto& qidx = db.get_index<pm_liquidity_index>().indices().get<by_market>();
                for (auto it = qidx.lower_bound(boost::make_tuple(mkt.id, pm_liquidity_id_type()));
                     it != qidx.end() && it->market == mkt.id && my_liquidity.size() < MY_CAP; ++it)
                    if (it->provider == account) my_liquidity.push_back(pm_liquidity_object(*it));
            }

            pm_market_full_api_object full{
                pm_market_object(mkt), std::move(outcomes), make_weight_sums(db, mkt),
                oracle, meta, std::move(my_positions), std::move(my_leverage), std::move(my_liquidity)};
            // Overlay the metadata-enriched market card so detail views (which read market.metadata)
            // get title/image without depending on the separate `meta` field.
            fc::variant fv;
            fc::to_variant(full, fv);
            fc::mutable_variant_object fo(fv.get_object());
            fo["market"] = market_card(db, mkt);
            return fc::variant(std::move(fo));
        });
    }

    DEFINE_API(prediction_market_api, get_lazy_allocations) {
        CHECK_ARG_MIN_SIZE(2, 2)
        auto from  = args.args->at(0).as<uint32_t>();
        auto limit = args.args->at(1).as<uint32_t>();
        FC_ASSERT(limit <= 1000);
        FC_ASSERT(from <= MAX_PM_PAGE_FROM);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_lazy_allocation_object> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_lazy_allocation_index>().indices().get<by_id>();
            auto it = idx.begin();
            while (from > 0 && it != idx.end()) { ++it; --from; }
            while (result.size() < limit && it != idx.end()) { result.push_back(pm_lazy_allocation_object(*it)); ++it; }
            return result;
        });
    }

    DEFINE_API(prediction_market_api, get_market_lazy_allocation) {
        CHECK_ARG_SIZE(1)
        auto market_id = args.args->at(0).as<int64_t>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto& idx = db.get_index<pm_lazy_allocation_index>().indices().get<by_market>();
            auto it = idx.find(pm_market_id_type(market_id));
            FC_ASSERT(it != idx.end(), "No lazy allocation for market");
            return pm_lazy_allocation_object(*it);
        });
    }

} } } // graphene::plugins::prediction_market_api
