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

namespace graphene { namespace plugins { namespace prediction_market_api {

    namespace {

        // Non-consensus reliability score in basis points [0..10000]. Blends the
        // resolution success ratio with the dispute win ratio, then docks bans.
        uint32_t reliability_score(const pm_oracle_object& o) {
            uint64_t completed = (uint64_t)o.markets_resolved + o.missed_count;
            int64_t  base = completed ? (int64_t)((uint64_t)o.markets_resolved * 10000 / completed)
                                      : 10000; // unproven oracle starts optimistic
            int64_t  dtot = (int64_t)o.disputes_won + o.disputes_lost;
            int64_t  drep = dtot ? (int64_t)((uint64_t)o.disputes_won * 10000 / (uint64_t)dtot)
                                 : 10000;
            int64_t  score = (base + drep) / 2 - (int64_t)o.bans_received * 1000;
            if (score < 0)     score = 0;
            if (score > 10000) score = 10000;
            return (uint32_t)score;
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

            // Aggregate winning-side amount and curve weight (weight_sum is not stored).
            share_type    winners_amount = 0;
            fc::uint128_t win_weight = 0;
            const auto& bidx = db.get_index<pm_bet_index>().indices().get<by_market>();
            for (auto it = bidx.lower_bound(boost::make_tuple(mkt.id, pm_bet_id_type()));
                 it != bidx.end() && it->market == mkt.id; ++it) {
                if (it->status != 0 && it->status != 3) continue;
                int16_t s = binary ? (int16_t)it->side : it->outcome_index;
                if (s == winside) {
                    winners_amount += it->amount;
                    win_weight += fc::uint128_t((uint64_t)it->weight.value);
                }
            }

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

        // Listing risk floor (security-threat-model §4.3): a market whose oracle insurance
        // covers less than 2.5× its betting volume is hidden from the default listing
        // (revealed via show_risky). Per-market, non-consensus. Returns true if BELOW floor.
        bool below_risk_floor(const database& db, const pm_market_object& mkt) {
            if (mkt.bets_sum.value <= 0) return false; // no volume → no risk
            const auto& oidx = db.get_index<pm_oracle_index>().indices().get<by_owner>();
            auto it = oidx.find(mkt.oracle);
            int64_t ins = (it != oidx.end()) ? it->insurance.value : 0;
            // Governance-tunable coverage floor (percent of bets; 250 = 2.5×). Hidden below it.
            const uint16_t cov = db.get_validator_schedule_object().median_props.pm_listing_min_coverage_percent;
            return ins * 100 < mkt.bets_sum.value * (int64_t)cov;
        }

        // Per-outcome amount + curve-weight aggregate (live from active/resolved bets). Shared by
        // get_market_weight_sums and get_market_full.
        pm_market_weight_sums_api_object make_weight_sums(const database& db, const pm_market_object& mkt) {
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
            if (binary) {
                for (int16_t i = 0; i < (int16_t)mkt.outcome_count; ++i)
                    out.outcomes.push_back({i, (i == 0 ? "A" : "B"), amt[i], wgt[i]});
            } else {
                const auto& oidx = db.get_index<pm_outcome_index>().indices().get<by_market_outcome>();
                for (uint8_t i = 0; i < mkt.outcome_count; ++i) {
                    auto oit = oidx.find(boost::make_tuple(mkt.id, i));
                    std::string label = (oit != oidx.end()) ? to_string(oit->label) : std::string();
                    out.outcomes.push_back({(int16_t)i, label, amt[i], wgt[i]});
                }
            }
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
              ("description", to_string(it->description));
        }
        o["title"]    = title;
        o["image"]    = image;
        o["category"] = category;
        o["metadata"] = fc::variant(std::move(md));   // reconstructed; clients parse market.metadata
        return fc::variant(std::move(o));
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
    // order: "oldest" (id asc, default — legacy) · "newest" (id desc). The by_status index keeps
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
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<fc::variant> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_market_index>().indices().get<by_status>();
            auto range = idx.equal_range(status);
            if (order == "newest") {
                auto itr = range.second;                    // one past the last equal-status market
                while (from > 0 && itr != range.first) { --itr; --from; } // skip newest `from`
                while (result.size() < limit && itr != range.first) {
                    --itr;
                    if (show_risky || !below_risk_floor(db, *itr))
                        result.push_back(market_card(db, *itr));
                }
            } else {
                auto itr = range.first;
                while (from > 0 && itr != range.second) { ++itr; --from; }
                while (result.size() < limit && itr != range.second) {
                    if (show_risky || !below_risk_floor(db, *itr))
                        result.push_back(market_card(db, *itr));
                    ++itr;
                }
            }
            return result;
        });
    }

    DEFINE_API(prediction_market_api, list_markets_by_oracle) {
        CHECK_ARG_MIN_SIZE(3, 3)
        auto oracle = args.args->at(0).as<account_name_type>();
        auto from   = args.args->at(1).as<uint32_t>();
        auto limit  = args.args->at(2).as<uint32_t>();
        FC_ASSERT(limit <= 1000);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<fc::variant> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_market_index>().indices().get<by_oracle>();
            auto itr = idx.lower_bound(oracle);
            while (from > 0 && itr != idx.end() && itr->oracle == oracle) { ++itr; --from; }
            while (result.size() < limit && itr != idx.end() && itr->oracle == oracle) {
                result.push_back(market_card(db, *itr));
                ++itr;
            }
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

    DEFINE_API(prediction_market_api, list_markets_by_creator) {
        CHECK_ARG_MIN_SIZE(3, 3)
        auto creator = args.args->at(0).as<account_name_type>();
        auto from    = args.args->at(1).as<uint32_t>();
        auto limit   = args.args->at(2).as<uint32_t>();
        FC_ASSERT(limit <= 1000);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<fc::variant> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_market_index>().indices().get<by_creator>();
            auto itr = idx.lower_bound(creator);
            while (from > 0 && itr != idx.end() && itr->creator == creator) { ++itr; --from; }
            while (result.size() < limit && itr != idx.end() && itr->creator == creator) {
                result.push_back(market_card(db, *itr));
                ++itr;
            }
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

    DEFINE_API(prediction_market_api, get_market_bets) {
        CHECK_ARG_MIN_SIZE(3, 3)
        auto market_id = args.args->at(0).as<int64_t>();
        auto from      = args.args->at(1).as<uint32_t>();
        auto limit     = args.args->at(2).as<uint32_t>();
        FC_ASSERT(limit <= 1000);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_bet_object> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_bet_index>().indices().get<by_market>();
            auto itr = idx.lower_bound(boost::make_tuple(pm_market_id_type(market_id), pm_bet_id_type()));
            while (from > 0 && itr != idx.end() && itr->market == pm_market_id_type(market_id)) { ++itr; --from; }
            while (result.size() < limit && itr != idx.end() && itr->market == pm_market_id_type(market_id)) {
                result.push_back(pm_bet_object(*itr));
                ++itr;
            }
            return result;
        });
    }

    DEFINE_API(prediction_market_api, get_account_positions) {
        CHECK_ARG_MIN_SIZE(3, 3)
        auto account = args.args->at(0).as<account_name_type>();
        auto from    = args.args->at(1).as<uint32_t>();
        auto limit   = args.args->at(2).as<uint32_t>();
        FC_ASSERT(limit <= 1000);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_position_api_object> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_bet_index>().indices().get<by_account>();
            auto itr = idx.lower_bound(boost::make_tuple(account, pm_bet_id_type()));
            while (from > 0 && itr != idx.end() && itr->account == account) { ++itr; --from; }
            while (result.size() < limit && itr != idx.end() && itr->account == account) {
                const auto* m = db.find<pm_market_object>(itr->market);
                share_type ep = (m != nullptr) ? expected_payout(db, *itr, *m) : share_type(0);
                result.push_back(pm_position_api_object{
                    pm_bet_object(*itr), ep,
                    (m != nullptr) ? m->status : (int8_t)0,
                    (m != nullptr) ? m->resolved_outcome : (int16_t)-1});
                ++itr;
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

    DEFINE_API(prediction_market_api, get_account_leverage_positions) {
        CHECK_ARG_MIN_SIZE(3, 3)
        auto account = args.args->at(0).as<account_name_type>();
        auto from    = args.args->at(1).as<uint32_t>();
        auto limit   = args.args->at(2).as<uint32_t>();
        FC_ASSERT(limit <= 1000);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_leverage_position_object> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_leverage_position_index>().indices().get<by_lev_account>();
            auto itr = idx.lower_bound(boost::make_tuple(account, pm_leverage_position_id_type()));
            while (from > 0 && itr != idx.end() && itr->account == account) { ++itr; --from; }
            while (result.size() < limit && itr != idx.end() && itr->account == account) {
                result.push_back(pm_leverage_position_object(*itr));
                ++itr;
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
            return pm_oracle_api_object{pm_oracle_object(*itr), reliability_score(*itr)};
        });
    }

    DEFINE_API(prediction_market_api, list_oracles) {
        CHECK_ARG_MIN_SIZE(2, 2)
        auto from  = args.args->at(0).as<uint32_t>();
        auto limit = args.args->at(1).as<uint32_t>();
        FC_ASSERT(limit <= 1000);
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

    // list_markets_by_category(category, from, limit, [jurisdiction=""], [subcategory=""], [tag=""], [sort="newest"])
    // Optional filters: jurisdiction (exclude markets banning it), subcategory (exact), tag (CSV membership).
    // sort: "newest" (market id desc, default) · "oldest" (id asc) · "volume" (bets_sum desc) ·
    // "expiration" (betting_expiration asc). volume/expiration load each matching market, so they
    // scan the whole (non-pruned) category before paging; newest/oldest sort on the meta id alone.
    DEFINE_API(prediction_market_api, list_markets_by_category) {
        CHECK_ARG_MIN_SIZE(3, 7)
        auto category     = args.args->at(0).as<std::string>();
        auto from         = args.args->at(1).as<uint32_t>();
        auto limit        = args.args->at(2).as<uint32_t>();
        auto jurisdiction = GET_OPTIONAL_ARG(3, std::string, std::string()); // exclude markets banning it
        auto subcategory  = GET_OPTIONAL_ARG(4, std::string, std::string());
        auto tag          = GET_OPTIONAL_ARG(5, std::string, std::string());
        auto sort         = GET_OPTIONAL_ARG(6, std::string, std::string("newest"));
        FC_ASSERT(limit <= 1000);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const bool need_market = (sort == "volume" || sort == "expiration");
            struct entry { const pm_market_meta_object* m; int64_t vol; int64_t exp; int64_t mid; };
            std::vector<entry> es;
            const auto& idx = db.get_index<pm_market_meta_index>().indices().get<by_meta_category>();
            for (auto itr = idx.lower_bound(category);
                 itr != idx.end() && to_string(itr->category) == category; ++itr) {
                if (!jurisdiction.empty() && meta_csv_contains(to_string(itr->banned_jurisdictions), jurisdiction)) continue;
                if (!subcategory.empty() && to_string(itr->subcategory) != subcategory) continue;
                if (!tag.empty() && !meta_csv_contains(to_string(itr->tags), tag)) continue;
                const auto* mk = need_market ? db.find<pm_market_object>(itr->market) : nullptr;
                es.push_back({ &*itr,
                    mk ? mk->bets_sum.value : 0,
                    mk ? (int64_t)mk->betting_expiration.sec_since_epoch() : std::numeric_limits<int64_t>::max(),
                    (int64_t)itr->market._id });
            }
            if (sort == "volume")
                std::stable_sort(es.begin(), es.end(), [](const entry& a, const entry& b){ return a.vol > b.vol; });
            else if (sort == "expiration")
                std::stable_sort(es.begin(), es.end(), [](const entry& a, const entry& b){ return a.exp < b.exp; });
            else if (sort == "oldest")
                std::stable_sort(es.begin(), es.end(), [](const entry& a, const entry& b){ return a.mid < b.mid; });
            else // "newest"
                std::stable_sort(es.begin(), es.end(), [](const entry& a, const entry& b){ return a.mid > b.mid; });

            std::vector<pm_market_meta_object> result;
            result.reserve(std::min<size_t>(limit, es.size()));
            for (uint32_t i = from; i < es.size() && result.size() < limit; ++i)
                result.push_back(pm_market_meta_object(*es[i].m));
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
                if (oit != oidx.end())
                    oracle = pm_oracle_api_object{pm_oracle_object(*oit), reliability_score(*oit)};
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
                const auto& bidx = db.get_index<pm_bet_index>().indices().get<by_market_account>();
                for (auto it = bidx.lower_bound(boost::make_tuple(mkt.id, account, pm_bet_id_type()));
                     it != bidx.end() && it->market == mkt.id && it->account == account; ++it)
                    my_positions.push_back(pm_position_api_object{
                        pm_bet_object(*it), expected_payout(db, *it, mkt), mkt.status, mkt.resolved_outcome});
                const auto& lidx = db.get_index<pm_leverage_position_index>().indices().get<by_lev_market_status>();
                for (auto it = lidx.lower_bound(boost::make_tuple(mkt.id, (uint8_t)0, pm_leverage_position_id_type()));
                     it != lidx.end() && it->market == mkt.id; ++it)
                    if (it->account == account) my_leverage.push_back(pm_leverage_position_object(*it));
                const auto& qidx = db.get_index<pm_liquidity_index>().indices().get<by_market>();
                for (auto it = qidx.lower_bound(boost::make_tuple(mkt.id, pm_liquidity_id_type()));
                     it != qidx.end() && it->market == mkt.id; ++it)
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
