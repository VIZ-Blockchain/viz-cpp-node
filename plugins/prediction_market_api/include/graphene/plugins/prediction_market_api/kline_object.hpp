#pragma once

#include <chainbase/chainbase.hpp>
#include <graphene/chain/index.hpp>
#include <graphene/chain/chain_object_types.hpp>
#include <graphene/chain/pm_objects.hpp>

#include <boost/interprocess/containers/vector.hpp>
#include <boost/multi_index/composite_key.hpp>

// HF14 Prediction-Market kline / time-series history (NON-consensus plugin state, owned by the
// prediction_market_api plugin — never part of the state hash, fully undo/redo-safe because it lives
// in chainbase). One record is appended every time a market's per-outcome weights change (a bet, a
// cancel, a liquidation, a batch settle, a leverage open/resolve). Each record is a timestamped
// snapshot of the parimutuel weight (staked amount) on every outcome, so a thin client can plot the
// outcomes' weight ratio over time: x = `timestamp` (unix seconds), y[i] = `weights[i]`.

#ifndef PM_META_SPACE_ID
#define PM_META_SPACE_ID 30
#endif

namespace graphene { namespace plugins { namespace prediction_market_api {

    namespace bip = boost::interprocess;

    enum pm_kline_object_types {
        pm_market_kline_object_type = (PM_META_SPACE_ID << 8) + 1
    };

    using namespace graphene::chain;
    using namespace chainbase;

    class pm_market_kline_object final : public object<pm_market_kline_object_type, pm_market_kline_object> {
    public:
        template<typename Constructor, typename Allocator>
        pm_market_kline_object(Constructor&& c, allocator<Allocator> a) : weights(a) { c(*this); }

        id_type           id;
        pm_market_id_type market;
        uint32_t          seq = 0;        ///< 0-based, contiguous, monotonic per market (append index)
        time_point_sec    timestamp;      ///< block time of the change; x = timestamp.sec_since_epoch()
        uint8_t           reason = 0;     ///< 0 bet, 1 cancel, 2 liquidation, 3 batch settle, 4 leverage open, 5 leverage resolve
        share_type        bets_sum;       ///< total staked across all outcomes at this point
        /// Per-outcome parimutuel weight (staked amount); size == market.outcome_count. The ratio
        /// weights[i] / Σweights is the implied probability of outcome i.
        bip::vector<share_type, allocator<share_type>> weights;
    };

    using pm_market_kline_id_type = object_id<pm_market_kline_object>;

    struct by_kline_market_seq;
    typedef boost::multi_index_container<
        pm_market_kline_object,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_unique<boost::multi_index::tag<by_id>,
                boost::multi_index::member<pm_market_kline_object, pm_market_kline_id_type, &pm_market_kline_object::id>>,
            boost::multi_index::ordered_unique<boost::multi_index::tag<by_kline_market_seq>,
                boost::multi_index::composite_key<pm_market_kline_object,
                    boost::multi_index::member<pm_market_kline_object, pm_market_id_type, &pm_market_kline_object::market>,
                    boost::multi_index::member<pm_market_kline_object, uint32_t, &pm_market_kline_object::seq>
                >,
                boost::multi_index::composite_key_compare<std::less<pm_market_id_type>, std::less<uint32_t>>
            >
        >,
        allocator<pm_market_kline_object>
    > pm_market_kline_index;

} } } // graphene::plugins::prediction_market_api

FC_REFLECT((graphene::plugins::prediction_market_api::pm_market_kline_object),
    (id)(market)(seq)(timestamp)(reason)(bets_sum)(weights))
CHAINBASE_SET_INDEX_TYPE(graphene::plugins::prediction_market_api::pm_market_kline_object,
    graphene::plugins::prediction_market_api::pm_market_kline_index)
