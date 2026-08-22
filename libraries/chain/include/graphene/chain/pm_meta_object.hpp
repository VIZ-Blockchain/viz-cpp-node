#pragma once

#include <chainbase/chainbase.hpp>
#include <graphene/chain/index.hpp>
#include <graphene/chain/chain_object_types.hpp>
#include <graphene/chain/pm_objects.hpp>

#include <boost/multi_index/composite_key.hpp>

// HF14 Prediction-Market metadata index (NON-consensus state). Each market carries a free-form
// `metadata` JSON string; the prediction_market_api plugin extracts the keys it indexes (category /
// subcategory / tags / banned jurisdictions / title / image / event …), builds queryable indexes,
// and prunes them X days after the market's dispute window closes. Localization is a client concern;
// jurisdiction filtering is done here.
//
// The object/index TYPE lives in libraries/chain so shared consumers (notably the snapshot plugin,
// which serializes it into the snapshot when `snapshot-include-pm-meta` is on) can reference it
// WITHOUT a plugin-to-plugin dependency. Runtime REGISTRATION of the index (add_plugin_index) still
// happens in the prediction_market_api plugin — nodes that don't load that plugin simply never
// create the index (snapshot export/import guard on db.has_index<...>()).
//
// NB: this is a chainbase object at space id 30 (unchanged since it was a plugin-only type), so its
// numeric object-type id is stable across the move — existing shared_memory / snapshots keep working.

#ifndef PM_META_SPACE_ID
#define PM_META_SPACE_ID 30
#endif

namespace graphene { namespace chain {

    enum pm_meta_object_types {
        pm_market_meta_object_type = (PM_META_SPACE_ID << 8)
    };

    using namespace chainbase;

    class pm_market_meta_object final : public object<pm_market_meta_object_type, pm_market_meta_object> {
    public:
        template<typename Constructor, typename Allocator>
        pm_market_meta_object(Constructor&& c, allocator<Allocator> a)
            : category(a), subcategory(a), tags(a), banned_jurisdictions(a),
              title(a), image(a), condition_id(a), description(a), event(a), event_title(a) { c(*this); }

        id_type           id;
        pm_market_id_type market;
        shared_string     category;
        shared_string     subcategory;
        shared_string     tags;                 ///< comma-joined
        shared_string     banned_jurisdictions; ///< comma-joined ISO codes; empty = allowed everywhere
        shared_string     title;                ///< human-readable market question (for cards/detail)
        shared_string     image;                ///< icon/cover URL (hotlinked by clients; not hosted)
        shared_string     condition_id;         ///< source dedup id (e.g. Polymarket conditionId) for back-link
        shared_string     description;          ///< short resolution rules (how the oracle resolves); url holds full legal terms
        shared_string     event;                ///< parent grouping key (siblings of one match/game share it); empty = standalone
        shared_string     event_title;          ///< human-readable event label (e.g. "Dota 2: A vs B") for event page/cards
        bool              child = false;        ///< true = a child/prop market of a parent event; hidden from category/tag listings by default
        time_point_sec    expiry;               ///< prune after: dispute window close + TTL
    };

    using pm_market_meta_id_type = object_id<pm_market_meta_object>;

    struct by_meta_market;
    struct by_meta_category;
    struct by_meta_event;
    struct by_meta_expiry;
    typedef boost::multi_index_container<
        pm_market_meta_object,
        boost::multi_index::indexed_by<
            boost::multi_index::ordered_unique<boost::multi_index::tag<by_id>,
                boost::multi_index::member<pm_market_meta_object, pm_market_meta_id_type, &pm_market_meta_object::id>>,
            boost::multi_index::ordered_unique<boost::multi_index::tag<by_meta_market>,
                boost::multi_index::member<pm_market_meta_object, pm_market_id_type, &pm_market_meta_object::market>>,
            boost::multi_index::ordered_unique<boost::multi_index::tag<by_meta_category>,
                boost::multi_index::composite_key<pm_market_meta_object,
                    boost::multi_index::member<pm_market_meta_object, shared_string, &pm_market_meta_object::category>,
                    boost::multi_index::member<pm_market_meta_object, pm_market_meta_id_type, &pm_market_meta_object::id>
                >,
                boost::multi_index::composite_key_compare<chainbase::strcmp_less, std::less<pm_market_meta_id_type>>
            >,
            boost::multi_index::ordered_unique<boost::multi_index::tag<by_meta_event>,
                boost::multi_index::composite_key<pm_market_meta_object,
                    boost::multi_index::member<pm_market_meta_object, shared_string, &pm_market_meta_object::event>,
                    boost::multi_index::member<pm_market_meta_object, pm_market_meta_id_type, &pm_market_meta_object::id>
                >,
                boost::multi_index::composite_key_compare<chainbase::strcmp_less, std::less<pm_market_meta_id_type>>
            >,
            boost::multi_index::ordered_unique<boost::multi_index::tag<by_meta_expiry>,
                boost::multi_index::composite_key<pm_market_meta_object,
                    boost::multi_index::member<pm_market_meta_object, time_point_sec, &pm_market_meta_object::expiry>,
                    boost::multi_index::member<pm_market_meta_object, pm_market_meta_id_type, &pm_market_meta_object::id>
                >,
                boost::multi_index::composite_key_compare<std::less<time_point_sec>, std::less<pm_market_meta_id_type>>
            >
        >,
        allocator<pm_market_meta_object>
    > pm_market_meta_index;

} } // graphene::chain

FC_REFLECT((graphene::chain::pm_market_meta_object),
    (id)(market)(category)(subcategory)(tags)(banned_jurisdictions)(title)(image)(condition_id)(description)(event)(event_title)(child)(expiry))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::pm_market_meta_object,
    graphene::chain::pm_market_meta_index)
