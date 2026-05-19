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
        class paid_subscription_object
                : public object<paid_subscription_object_type, paid_subscription_object> {
        public:
            paid_subscription_object() = delete;

            template<typename Constructor, typename Allocator>
            paid_subscription_object(Constructor &&c, allocator <Allocator> a)
                    :url(a) {
                c(*this);
            }

            id_type id;
            account_name_type creator;
            shared_string url;
            uint16_t levels;
            share_type amount;
            uint16_t period;
            time_point_sec update_time;
        };

        struct by_creator;
        typedef multi_index_container <
            paid_subscription_object,
            indexed_by<
                ordered_unique<
                    tag<by_id>,
                    member<paid_subscription_object, paid_subscription_object_id_type, &paid_subscription_object::id>
                >,
                ordered_unique<
                    tag<by_creator>,
                    member<paid_subscription_object, account_name_type, &paid_subscription_object::creator>
                >
            >,
            allocator <paid_subscription_object>
        >
        paid_subscription_index;


        class paid_subscribe_object
                : public object<paid_subscribe_object_type, paid_subscribe_object> {
        public:
            paid_subscribe_object() = delete;

            template<typename Constructor, typename Allocator>
            paid_subscribe_object(Constructor &&c, allocator <Allocator> a) {
                c(*this);
            }

            id_type id;

            account_name_type subscriber;
            account_name_type creator;
            uint16_t level;
            share_type amount;
            uint16_t period;
            time_point_sec start_time;
            time_point_sec next_time;
            time_point_sec end_time;
            bool active = false;
            bool auto_renewal = false;
        };

        struct by_subscriber;
        struct by_creator;
        struct by_next_time;
        struct by_subscribe;
        typedef multi_index_container <
            paid_subscribe_object,
            indexed_by<
                ordered_unique<
                    tag<by_id>,
                    member<paid_subscribe_object, paid_subscribe_object_id_type, &paid_subscribe_object::id>
                >,
                ordered_non_unique<
                    tag<by_subscriber>,
                    member<paid_subscribe_object, account_name_type, &paid_subscribe_object::subscriber>
                >,
                ordered_non_unique<
                    tag<by_creator>,
                    member<paid_subscribe_object, account_name_type, &paid_subscribe_object::creator>
                >,
                ordered_non_unique<
                    tag<by_next_time>,
                    member<paid_subscribe_object, time_point_sec, &paid_subscribe_object::next_time>
                >,
                ordered_unique<
                    tag<by_subscribe>,
                    composite_key<
                        paid_subscribe_object,
                        member<paid_subscribe_object, account_name_type, &paid_subscribe_object::subscriber>,
                        member<paid_subscribe_object, account_name_type, &paid_subscribe_object::creator>
                    >,
                    composite_key_compare<protocol::string_less, protocol::string_less>
                >
            >,
            allocator <paid_subscribe_object>
        >
        paid_subscribe_index;
    }
} // graphene::chain

FC_REFLECT((graphene::chain::paid_subscription_object),(id)(creator)(levels)(amount)(period)(url)(update_time))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::paid_subscription_object, graphene::chain::paid_subscription_index)

FC_REFLECT((graphene::chain::paid_subscribe_object),(id)(subscriber)(creator)(level)(amount)(period)(start_time)(next_time)(end_time)(active)(auto_renewal))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::paid_subscribe_object, graphene::chain::paid_subscribe_index)
