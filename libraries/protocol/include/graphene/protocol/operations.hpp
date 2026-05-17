#pragma once

#include <graphene/protocol/operation_util.hpp>
#include <graphene/protocol/proposal_operations.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <graphene/protocol/chain_virtual_operations.hpp>

namespace graphene { namespace protocol {

        /** NOTE: do not change the order of any operations prior to the virtual operations
         * or it will trigger a hardfork.
         */
        typedef fc::static_variant<
                vote_operation,//deprecated
                content_operation,//deprecated

                transfer_operation,
                transfer_to_vesting_operation,
                withdraw_vesting_operation,

                account_update_operation,

                witness_update_operation,
                account_witness_vote_operation,
                account_witness_proxy_operation,

                delete_content_operation,//deprecated
                custom_operation,
                set_withdraw_vesting_route_operation,
                request_account_recovery_operation,
                recover_account_operation,
                change_recovery_account_operation,
                escrow_transfer_operation,
                escrow_dispute_operation,
                escrow_release_operation,
                escrow_approve_operation,
                delegate_vesting_shares_operation,
                account_create_operation,
                account_metadata_operation,
                proposal_create_operation,
                proposal_update_operation,
                proposal_delete_operation,
                chain_properties_update_operation,

                // virtual operations:
                author_reward_operation,
                curation_reward_operation,
                content_reward_operation,
                fill_vesting_withdraw_operation,
                shutdown_witness_operation,
                hardfork_operation,
                content_payout_update_operation,
                content_benefactor_reward_operation,
                return_vesting_delegation_operation,

                // VIZ Committee operations:
                committee_worker_create_request_operation,
                committee_worker_cancel_request_operation,
                committee_vote_request_operation,
                // virtual operations:
                committee_cancel_request_operation,
                committee_approve_request_operation,
                committee_payout_request_operation,
                committee_pay_request_operation,

                witness_reward_operation,

                // VIZ Invite operations:
                create_invite_operation,
                claim_invite_balance_operation,
                invite_registration_operation,

                versioned_chain_properties_update_operation,
                award_operation,
                // virtual operations:
                receive_award_operation,
                benefactor_award_operation,

                // VIZ Paid subscription operations:
                set_paid_subscription_operation,
                paid_subscribe_operation,
                // virtual operations:
                paid_subscription_action_operation,
                cancel_paid_subscription_operation,

                // VIZ Account sales operations:
                set_account_price_operation,
                set_subaccount_price_operation,
                buy_account_operation,
                // virtual operations:
                account_sale_operation,

                use_invite_balance_operation,
                expire_escrow_ratification_operation,

                // VIZ HF 11:
                fixed_award_operation,
                target_account_sale_operation,
                // virtual operations:
                bid_operation,
                outbid_operation,

                // VIZ HF 13: Validator reward sharing
                set_reward_sharing_operation,
                // virtual operations:
                stakeholder_reward_operation
        > operation;

        /*void operation_get_required_authorities( const operation& op,
                                                 flat_set<string>& active,
                                                 flat_set<string>& master,
                                                 flat_set<string>& regular,
                                                 vector<authority>& other );

        void operation_validate( const operation& op );*/

        bool is_virtual_operation(const operation &op);
        bool is_data_operation(const operation &op);

        struct operation_wrapper {
            operation_wrapper(const operation& op = operation()) : op(op) {}

            operation op;
        };

} } // graphene::protocol

/*namespace fc {
    void to_variant(const graphene::protocol::operation& var, fc::variant& vo);
    void from_variant(const fc::variant& var, graphene::protocol::operation& vo);
}*/

DECLARE_OPERATION_TYPE(graphene::protocol::operation)
FC_REFLECT_TYPENAME((graphene::protocol::operation))
FC_REFLECT((graphene::protocol::operation_wrapper), (op));
