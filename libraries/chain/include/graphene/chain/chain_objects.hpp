#pragma once

#include <graphene/protocol/authority.hpp>
#include <graphene/protocol/chain_operations.hpp>

#include <graphene/chain/chain_object_types.hpp>

#include <boost/multi_index/composite_key.hpp>
#include <boost/multiprecision/cpp_int.hpp>


namespace graphene {
    namespace chain {

        using graphene::protocol::asset;
        using graphene::protocol::price;
        using graphene::protocol::asset_symbol_type;
        using graphene::protocol::block_id_type;

        class escrow_object : public object<escrow_object_type, escrow_object> {
        public:
            template<typename Constructor, typename Allocator>
            escrow_object(Constructor &&c, allocator <Allocator> a) {
                c(*this);
            }

            escrow_object() {
            }

            id_type id;

            uint32_t escrow_id = 20;
            account_name_type from;
            account_name_type to;
            account_name_type agent;
            time_point_sec ratification_deadline;
            time_point_sec escrow_expiration;
            asset token_balance;
            asset pending_fee;
            bool to_approved = false;
            bool agent_approved = false;
            bool disputed = false;

            bool is_approved() const {
                return to_approved && agent_approved;
            }
        };


        /**
         * @breif a route to send withdrawn vesting shares.
         */
        class withdraw_vesting_route_object
                : public object<withdraw_vesting_route_object_type, withdraw_vesting_route_object> {
        public:
            template<typename Constructor, typename Allocator>
            withdraw_vesting_route_object(Constructor &&c, allocator <Allocator> a) {
                c(*this);
            }

            withdraw_vesting_route_object() {
            }

            id_type id;

            account_id_type from_account;
            account_id_type to_account;
            uint16_t percent = 0;
            bool auto_vest = false;
        };


        struct by_withdraw_route;
        struct by_destination;
        typedef multi_index_container <
        withdraw_vesting_route_object,
        indexed_by<
                ordered_unique < tag <
                by_id>, member<withdraw_vesting_route_object, withdraw_vesting_route_id_type, &withdraw_vesting_route_object::id>>,
        ordered_unique <tag<by_withdraw_route>,
        composite_key<withdraw_vesting_route_object,
                member <
                withdraw_vesting_route_object, account_id_type, &withdraw_vesting_route_object::from_account>,
        member<withdraw_vesting_route_object, account_id_type, &withdraw_vesting_route_object::to_account>
        >,
        composite_key_compare <std::less<account_id_type>, std::less<account_id_type>>
        >,
        ordered_unique <tag<by_destination>,
        composite_key<withdraw_vesting_route_object,
                member <
                withdraw_vesting_route_object, account_id_type, &withdraw_vesting_route_object::to_account>,
        member<withdraw_vesting_route_object, withdraw_vesting_route_id_type, &withdraw_vesting_route_object::id>
        >
        >
        >,
        allocator <withdraw_vesting_route_object>
        >
        withdraw_vesting_route_index;

        struct by_from_id;
        struct by_to;
        struct by_agent;
        struct by_ratification_deadline;
        typedef multi_index_container <
        escrow_object,
        indexed_by<
                ordered_unique < tag <
                by_id>, member<escrow_object, escrow_id_type, &escrow_object::id>>,
        ordered_unique <tag<by_from_id>,
        composite_key<escrow_object,
                member <
                escrow_object, account_name_type, &escrow_object::from>,
        member<escrow_object, uint32_t, &escrow_object::escrow_id>
        >
        >,
        ordered_unique <tag<by_to>,
        composite_key<escrow_object,
                member < escrow_object, account_name_type, &escrow_object::to>,
        member<escrow_object, escrow_id_type, &escrow_object::id>
        >
        >,
        ordered_unique <tag<by_agent>,
        composite_key<escrow_object,
                member <
                escrow_object, account_name_type, &escrow_object::agent>,
        member<escrow_object, escrow_id_type, &escrow_object::id>
        >
        >,
        ordered_unique <tag<by_ratification_deadline>,
        composite_key<escrow_object,
                const_mem_fun <
                escrow_object, bool, &escrow_object::is_approved>,
        member<escrow_object, time_point_sec, &escrow_object::ratification_deadline>,
        member<escrow_object, escrow_id_type, &escrow_object::id>
        >,
        composite_key_compare <std::less<bool>, std::less<time_point_sec>, std::less<escrow_id_type>>
        >
        >,
        allocator <escrow_object>
        >
        escrow_index;

        class award_shares_expire_object
                : public object<award_shares_expire_object_type, award_shares_expire_object> {
        public:
            template<typename Constructor, typename Allocator>
            award_shares_expire_object(Constructor &&c, allocator <Allocator> a) {
                c(*this);
            }

            award_shares_expire_object() {
            }

            id_type id;

            time_point_sec expires;
            share_type rshares;
        };
        struct by_expiration;
        typedef multi_index_container <
            award_shares_expire_object,
            indexed_by<
                ordered_unique<tag<by_id>,
                    member<award_shares_expire_object, award_shares_expire_object_id_type, &award_shares_expire_object::id>
                >,
                ordered_non_unique<tag<by_expiration>,
                    member<award_shares_expire_object, time_point_sec, &award_shares_expire_object::expires>
                >
            >,
            allocator <award_shares_expire_object>
        >
        award_shares_expire_index;

        class validator_confirmation_object
                : public object<validator_confirmation_object_type, validator_confirmation_object> {
        public:
            template<typename Constructor, typename Allocator>
            validator_confirmation_object(Constructor &&c, allocator <Allocator> a) {
                c(*this);
            }

            validator_confirmation_object() {
            }

            id_type id;

            uint32_t block_num = 0;
            block_id_type block_id;
            fc::array<account_name_type, CHAIN_MAX_WITNESSES> current_shuffled_witnesses;
            fc::array<bool, CHAIN_MAX_WITNESSES> current_shuffled_witnesses_validations;
        };
        typedef multi_index_container <
            validator_confirmation_object,
            indexed_by<
                ordered_unique<tag<by_id>,
                    member<validator_confirmation_object, validator_confirmation_object_id_type, &validator_confirmation_object::id>
                >
            >,
            allocator <validator_confirmation_object>
        >
        validator_confirmation_index;
    }
} // graphene::chain

#include <graphene/chain/content_object.hpp>
#include <graphene/chain/account_object.hpp>

FC_REFLECT((graphene::chain::withdraw_vesting_route_object),
        (id)(from_account)(to_account)(percent)(auto_vest))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::withdraw_vesting_route_object, graphene::chain::withdraw_vesting_route_index)

FC_REFLECT((graphene::chain::escrow_object),
        (id)(escrow_id)(from)(to)(agent)
                (ratification_deadline)(escrow_expiration)
                (token_balance)(pending_fee)
                (to_approved)(agent_approved)(disputed))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::escrow_object, graphene::chain::escrow_index)

FC_REFLECT((graphene::chain::award_shares_expire_object),
    (id)(expires)(rshares))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::award_shares_expire_object, graphene::chain::award_shares_expire_index)

FC_REFLECT((graphene::chain::validator_confirmation_object),
    (id)(block_num)(block_id)(current_shuffled_witnesses)(current_shuffled_witnesses_validations))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::validator_confirmation_object, graphene::chain::validator_confirmation_index)
