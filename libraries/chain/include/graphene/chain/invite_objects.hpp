#pragma once

#include <graphene/protocol/authority.hpp>
#include <graphene/protocol/chain_operations.hpp>

#include <graphene/chain/chain_object_types.hpp>
#include <graphene/chain/shared_authority.hpp>
#include <graphene/chain/validator_objects.hpp>

#include <boost/multi_index/composite_key.hpp>

namespace graphene {
    namespace chain {
        class invite_object
                : public object<invite_object_type, invite_object> {
        public:
            invite_object() = delete;

            template<typename Constructor, typename Allocator>
            invite_object(Constructor &&c, allocator <Allocator> a)
                    :invite_secret(a) {
                c(*this);
            }

            id_type id;

            account_name_type creator;
            account_name_type receiver;
            public_key_type invite_key;
            shared_string invite_secret;

            asset balance = asset(0, TOKEN_SYMBOL);
            asset claimed_balance = asset(0, TOKEN_SYMBOL);

            time_point_sec create_time;
            time_point_sec claim_time;
            uint16_t status;
        };

        struct by_invite_key;
        struct by_status;
        struct by_creator;
        struct by_receiver;
        typedef multi_index_container <
            invite_object,
            indexed_by<
                ordered_unique<
                    tag<by_id>,
                    member<invite_object, invite_object_id_type, &invite_object::id>
                >,
                ordered_non_unique<tag<by_invite_key>,
                    member<invite_object, public_key_type, &invite_object::invite_key>
                >,
                ordered_non_unique<tag<by_status>,
                    member<invite_object, uint16_t, &invite_object::status>
                >,
                ordered_non_unique<tag<by_creator>,
                    member<invite_object, account_name_type, &invite_object::creator>
                >,
                ordered_non_unique<tag<by_receiver>,
                    member<invite_object, account_name_type, &invite_object::receiver>
                >
            >,
            allocator <invite_object>
        >
        invite_index;
    }
} // graphene::chain

FC_REFLECT((graphene::chain::invite_object),(id)(creator)(receiver)(invite_key)(invite_secret)(balance)(claimed_balance)(create_time)(claim_time)(status))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::invite_object, graphene::chain::invite_index)
