#pragma once

#include <graphene/protocol/authority.hpp>
#include <graphene/protocol/chain_operations.hpp>

#include <graphene/chain/chain_object_types.hpp>
#include <graphene/chain/shared_authority.hpp>
#include <graphene/chain/validator_objects.hpp>
#include <graphene/chain/content_object.hpp>

#include <boost/multi_index/composite_key.hpp>

namespace graphene {
    namespace chain {
        class committee_request_object
                : public object<committee_request_object_type, committee_request_object> {
        public:
            committee_request_object() = delete;

            template<typename Constructor, typename Allocator>
            committee_request_object(Constructor &&c, allocator <Allocator> a)
                    :url(a) {
                c(*this);
            }

            id_type id;
            uint32_t request_id;

            shared_string url;
            account_name_type creator;
            account_name_type worker;

            asset required_amount_min = asset(0, TOKEN_SYMBOL);
            asset required_amount_max = asset(0, TOKEN_SYMBOL);

            time_point_sec start_time;
            uint32_t duration;
            time_point_sec end_time;
            uint16_t status;
            uint32_t votes_count;
            time_point_sec conclusion_time;
            asset conclusion_payout_amount = asset(0, TOKEN_SYMBOL);
            asset payout_amount = asset(0, TOKEN_SYMBOL);
            asset remain_payout_amount = asset(0, TOKEN_SYMBOL);
            time_point_sec last_payout_time;
            time_point_sec payout_time;
        };

        struct by_request_id;
        struct by_status;
        struct by_creator;
        struct by_worker;
        struct by_creator_url;
        typedef multi_index_container <
            committee_request_object,
            indexed_by<
                ordered_unique<
                    tag<by_id>,
                    member<committee_request_object, committee_request_object_id_type, &committee_request_object::id>
                >,
                ordered_non_unique<tag<by_request_id>,
                    member<committee_request_object, uint32_t, &committee_request_object::request_id>
                >,
                ordered_non_unique<tag<by_status>,
                    member<committee_request_object, uint16_t, &committee_request_object::status>
                >,
                ordered_non_unique<tag<by_creator>,
                    member<committee_request_object, account_name_type, &committee_request_object::creator>
                >,
                ordered_non_unique<tag<by_worker>,
                    member<committee_request_object, account_name_type, &committee_request_object::worker>
                >,
                ordered_unique<tag<by_creator_url>,
                    composite_key<
                        committee_request_object,
                        member<committee_request_object, account_name_type, &committee_request_object::creator>,
                        member<committee_request_object, shared_string, &committee_request_object::url>
                    >,
                    composite_key_compare <std::less<account_name_type>, strcmp_less>
                >
            >,
            allocator <committee_request_object>
        >
        committee_request_index;


        class committee_vote_object
                : public object<committee_vote_object_type, committee_vote_object> {
        public:
            committee_vote_object() = delete;

            template<typename Constructor, typename Allocator>
            committee_vote_object(Constructor &&c, allocator <Allocator> a) {
                c(*this);
            }

            id_type id;
            uint32_t request_id;

            account_name_type voter;
            int16_t vote_percent = 0;
            time_point_sec last_update;
        };

        struct by_request_id;
        struct by_voter;
        typedef multi_index_container <
            committee_vote_object,
            indexed_by<
                ordered_unique<tag<by_id>,
                    member<committee_vote_object, committee_vote_object_id_type, &committee_vote_object::id>
                >,
                ordered_non_unique<tag<by_voter>,
                    member<committee_vote_object, account_name_type, &committee_vote_object::voter>
                >,
                ordered_non_unique<tag<by_request_id>,
                    member<committee_vote_object, uint32_t, &committee_vote_object::request_id>
                >
            >,
            allocator <committee_vote_object>
        >
        committee_vote_index;
    }
} // graphene::chain

FC_REFLECT((graphene::chain::committee_request_object),
(id)(request_id)(url)(creator)(worker)
(required_amount_min)(required_amount_max)
(start_time)(duration)(end_time)
(status)(votes_count)(conclusion_time)
(conclusion_payout_amount)(payout_amount)(remain_payout_amount)
(last_payout_time)(payout_time))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::committee_request_object, graphene::chain::committee_request_index)

FC_REFLECT((graphene::chain::committee_vote_object),(id)(request_id)(voter)(vote_percent)(last_update))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::committee_vote_object, graphene::chain::committee_vote_index)
