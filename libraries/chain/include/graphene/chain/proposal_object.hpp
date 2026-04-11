#pragma once

#include <graphene/chain/chain_object_types.hpp>

#include <chainbase/chainbase.hpp>

#include <graphene/protocol/transaction.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <graphene/protocol/sign_state.hpp>
#include <graphene/protocol/operations.hpp>

#include <boost/multi_index/composite_key.hpp>

#include <boost/interprocess/containers/flat_set.hpp>

namespace graphene { namespace chain {

    namespace bip = boost::interprocess;
    using protocol::public_key_type;
    using chainbase::allocator;
    class database;

    /**
     *  @brief Tracks the approval of a partially approved transaction
     *  @ingroup objects
     *  @ingroup protocol
     */
    class proposal_object : public object<proposal_object_type, proposal_object> {
    public:
        proposal_object() = delete;

        template <typename Constructor, typename Allocator>
        proposal_object(Constructor&& c, allocator <Allocator> a)
            : title(a),
              memo(a),
              proposed_operations(a),
              required_active_approvals(a),
              available_active_approvals(a),
              required_master_approvals(a),
              available_master_approvals(a),
              required_regular_approvals(a),
              available_regular_approvals(a),
              available_key_approvals(a) {
            c(*this);
        };

        id_type id;

        account_name_type author;
        shared_string title;

        shared_string memo;

        time_point_sec expiration_time;
        optional<time_point_sec> review_period_time;

        buffer_type proposed_operations;

        using name_allocator_type = allocator<account_name_type>;
        using name_set_type = bip::flat_set<account_name_type, std::less<account_name_type>, name_allocator_type>;

        using key_allocator_type = allocator<public_key_type>;
        using key_set_type = bip::flat_set<public_key_type, std::less<public_key_type>, key_allocator_type>;

        name_set_type required_active_approvals;
        name_set_type available_active_approvals;
        name_set_type required_master_approvals;
        name_set_type available_master_approvals;
        name_set_type required_regular_approvals;
        name_set_type available_regular_approvals;
        key_set_type available_key_approvals;

        bool is_authorized_to_execute(const database& db) const;

        void verify_authority(
            const database& db,
            const fc::flat_set<account_name_type>& active_approvals = fc::flat_set<account_name_type>(),
            const fc::flat_set<account_name_type>& master_approvals = fc::flat_set<account_name_type>(),
            const fc::flat_set<account_name_type>& regular_approvals = fc::flat_set<account_name_type>()
        ) const;

        std::vector<protocol::operation> operations() const;
    };

    /**
     *  @brief Tracks all of the proposal objects that requrie approval of an individual account.
     *  @ingroup objects
     *  @ingroup protocol
     */
    class required_approval_object: public object<required_approval_object_type, required_approval_object> {
    public:
        required_approval_object() = default;

        template <typename Constructor, typename Allocator>
        required_approval_object(Constructor&& c, allocator <Allocator>) {
            c(*this);
        }

        id_type id;

        account_name_type account;
        proposal_object_id_type proposal;
    };

    struct by_account;
    struct by_expiration;

    using proposal_index = boost::multi_index_container<
        proposal_object,
        indexed_by<
            ordered_unique<
                tag<by_id>,
                member<proposal_object, proposal_object_id_type, &proposal_object::id>>,
            ordered_unique<
                tag<by_account>,
                composite_key<
                    proposal_object,
                    member<proposal_object, account_name_type, &proposal_object::author>,
                    member<proposal_object, shared_string, &proposal_object::title>>,
                composite_key_compare<
                    std::less<account_name_type>,
                    chainbase::strcmp_less>>,
            ordered_non_unique<
                tag<by_expiration>,
                member<proposal_object, time_point_sec, &proposal_object::expiration_time>>>,
        allocator<proposal_object>>;

    using required_approval_index = boost::multi_index_container<
        required_approval_object,
        indexed_by<
            ordered_unique<
                tag<by_id>,
                member<required_approval_object, required_approval_object_id_type, &required_approval_object::id>>,
            ordered_unique<
                tag<by_account>,
                composite_key<
                    required_approval_object,
                    member<required_approval_object, account_name_type, &required_approval_object::account>,
                    member<required_approval_object, proposal_object_id_type, &required_approval_object::proposal>>>>,
        allocator<required_approval_object>>;

} } // graphene::chain

CHAINBASE_SET_INDEX_TYPE(graphene::chain::proposal_object, graphene::chain::proposal_index);
CHAINBASE_SET_INDEX_TYPE(graphene::chain::required_approval_object, graphene::chain::required_approval_index);

FC_REFLECT((graphene::chain::proposal_object),
    (id)(author)(title)(memo)(expiration_time)(review_period_time)
    (proposed_operations)
    (required_active_approvals)(available_active_approvals)
    (required_master_approvals)(available_master_approvals)
    (required_regular_approvals)(available_regular_approvals)
    (available_key_approvals))

FC_REFLECT((graphene::chain::required_approval_object),
    (id)(account)(proposal))