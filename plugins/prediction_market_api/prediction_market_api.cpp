#include <boost/program_options/options_description.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <graphene/plugins/prediction_market_api/prediction_market_api.hpp>
#include <graphene/plugins/prediction_market_api/meta_parse.hpp>
#include <graphene/plugins/prediction_market_api/kline_object.hpp>
#include <graphene/chain/index.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/pm_objects.hpp>
#include <graphene/chain/validator_objects.hpp>
#include <graphene/chain/operation_notification.hpp>
#include <graphene/protocol/pm_operations.hpp>
#include <graphene/protocol/pm_virtual_operations.hpp>

#include <fc/uint128_t.hpp>
#include <fc/io/json.hpp>

#include <limits>

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
            return ins * 2 < mkt.bets_sum.value * 5; // insurance / bets < 2.5×
        }

    } // anonymous namespace

    struct prediction_market_api::impl final {
        impl(): database_(appbase::app().get_plugin<chain::plugin>().db()) {}
        ~impl() = default;

        graphene::chain::database& database() { return database_; }
        graphene::chain::database& database() const { return database_; }

        // Metadata indexer (non-consensus): parse each new market's free-form `metadata` JSON
        // into queryable meta entries, then prune entries past their TTL.
        void on_block() {
            auto& db = database_;
            const uint32_t grace = (uint32_t)db.get_validator_schedule_object().median_props.pm_dispute_grace_sec;
            const auto now = db.head_block_time();

            const auto& midx = db.get_index<pm_market_index>().indices().get<by_id>();
            const auto& meta_by_market = db.get_index<pm_market_meta_index>().indices().get<by_meta_market>();

            auto it = midx.lower_bound(pm_market_id_type(last_market_id_));
            for (; it != midx.end(); ++it) {
                if (meta_by_market.find(it->id) != meta_by_market.end()) {
                    last_market_id_ = it->id._id + 1; continue;
                }
                const parsed_meta pm = parse_market_metadata(to_string(it->metadata));
                const pm_market_id_type mkt_id = it->id;
                const time_point_sec res_exp = it->result_expiration;
                db.create<pm_market_meta_object>([&](pm_market_meta_object& m) {
                    m.market = mkt_id;
                    from_string(m.category, pm.category);
                    from_string(m.subcategory, pm.subcategory);
                    from_string(m.tags, pm.tags);
                    from_string(m.banned_jurisdictions, pm.banned_jurisdictions);
                    m.expiry = res_exp + fc::seconds(grace) + fc::seconds((int64_t)ttl_days_ * 86400);
                });
                last_market_id_ = it->id._id + 1;
            }

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

        // A plugin observer must never throw out of the apply path — swallow everything.
        void on_post_apply_operation(const graphene::chain::operation_notification& note) {
            try {
                const auto ev = note.op.visit(kline_event_visitor{database_});
                if (ev) record_kline(ev->first, ev->second);
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
        uint64_t last_market_id_ = 0;
        uint32_t ttl_days_ = 7;

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
            ("pmm-ttl-days", boost::program_options::value<uint32_t>()->default_value(7),
             "Days to keep PM market metadata after the dispute window closes");
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

    DEFINE_API(prediction_market_api, get_market) {
        CHECK_ARG_SIZE(1)
        auto market_id = args.args->at(0).as<int64_t>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto* m = db.find<pm_market_object>(pm_market_id_type(market_id));
            FC_ASSERT(m != nullptr, "Market not found");
            return pm_market_object(*m);
        });
    }

    DEFINE_API(prediction_market_api, list_markets) {
        CHECK_ARG_MIN_SIZE(3, 4)
        auto status = args.args->at(0).as<int8_t>();
        auto from   = args.args->at(1).as<uint32_t>();
        auto limit  = args.args->at(2).as<uint32_t>();
        auto show_risky = GET_OPTIONAL_ARG(3, bool, false); // reveal under-insured markets
        FC_ASSERT(limit <= 1000);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_market_object> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_market_index>().indices().get<by_status>();
            auto itr = idx.lower_bound(status);
            while (from > 0 && itr != idx.end() && itr->status == status) { ++itr; --from; }
            while (result.size() < limit && itr != idx.end() && itr->status == status) {
                if (show_risky || !below_risk_floor(db, *itr))
                    result.push_back(pm_market_object(*itr));
                ++itr;
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
            std::vector<pm_market_object> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_market_index>().indices().get<by_oracle>();
            auto itr = idx.lower_bound(oracle);
            while (from > 0 && itr != idx.end() && itr->oracle == oracle) { ++itr; --from; }
            while (result.size() < limit && itr != idx.end() && itr->oracle == oracle) {
                result.push_back(pm_market_object(*itr));
                ++itr;
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
            std::vector<pm_market_object> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_market_index>().indices().get<by_creator>();
            auto itr = idx.lower_bound(creator);
            while (from > 0 && itr != idx.end() && itr->creator == creator) { ++itr; --from; }
            while (result.size() < limit && itr != idx.end() && itr->creator == creator) {
                result.push_back(pm_market_object(*itr));
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
            const auto& mkt = *m;
            const bool binary = (mkt.market_type == 0);

            std::vector<share_type> amt(mkt.outcome_count, share_type(0));
            std::vector<share_type> wgt(mkt.outcome_count, share_type(0));
            const auto& bidx = db.get_index<pm_bet_index>().indices().get<by_market>();
            for (auto it = bidx.lower_bound(boost::make_tuple(mkt.id, pm_bet_id_type()));
                 it != bidx.end() && it->market == mkt.id; ++it) {
                if (it->status != 0 && it->status != 3) continue;
                int16_t s = binary ? (int16_t)it->side : it->outcome_index;
                if (s >= 0 && s < (int16_t)mkt.outcome_count) {
                    amt[s] += it->amount;
                    wgt[s] += it->weight;
                }
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

    DEFINE_API(prediction_market_api, list_markets_by_category) {
        CHECK_ARG_MIN_SIZE(3, 4)
        auto category     = args.args->at(0).as<std::string>();
        auto from         = args.args->at(1).as<uint32_t>();
        auto limit        = args.args->at(2).as<uint32_t>();
        auto jurisdiction = GET_OPTIONAL_ARG(3, std::string, std::string()); // exclude markets banning it
        FC_ASSERT(limit <= 1000);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<pm_market_meta_object> result;
            result.reserve(limit);
            const auto& idx = db.get_index<pm_market_meta_index>().indices().get<by_meta_category>();
            auto itr = idx.lower_bound(category);
            uint32_t skipped = 0;
            while (itr != idx.end() && to_string(itr->category) == category && result.size() < limit) {
                bool allowed = jurisdiction.empty() ||
                               !meta_csv_contains(to_string(itr->banned_jurisdictions), jurisdiction);
                if (allowed) {
                    if (skipped < from) ++skipped;
                    else result.push_back(pm_market_meta_object(*itr));
                }
                ++itr;
            }
            return result;
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

} } } // graphene::plugins::prediction_market_api
