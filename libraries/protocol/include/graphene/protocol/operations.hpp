#pragma once

#include <graphene/protocol/operation_util.hpp>
#include <graphene/protocol/proposal_operations.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <graphene/protocol/chain_virtual_operations.hpp>
#include <graphene/protocol/pm_operations.hpp>
#include <graphene/protocol/pm_virtual_operations.hpp>

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

                validator_update_operation,
                account_validator_vote_operation,
                account_validator_proxy_operation,

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
                shutdown_validator_operation,
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

                validator_reward_operation,

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
                stakeholder_reward_operation,

                // VIZ HF 14: Prediction Markets (Onix) — APPEND ONLY, never reorder.
                // The variant index is the consensus op-id; order is frozen by the plan.
                pm_oracle_register_operation,
                pm_oracle_update_operation,
                pm_create_market_operation,
                pm_oracle_accept_market_operation,
                pm_place_bet_operation,
                pm_commit_bet_operation,
                pm_reveal_bet_operation,
                pm_cancel_bet_operation,
                pm_add_liquidity_operation,
                pm_withdraw_liquidity_operation,
                pm_resolve_market_operation,
                pm_no_contest_operation,
                pm_dispute_create_operation,
                pm_dispute_vote_operation,
                pm_dispute_resolve_operation,
                pm_transfer_position_operation,
                pm_lazy_deposit_operation,
                pm_lazy_withdraw_operation,
                // virtual operations:
                pm_batch_settle_operation,
                pm_commit_forfeit_operation,
                pm_auto_payout_operation,
                pm_dispute_finalize_operation,
                pm_dispute_auto_close_operation,
                pm_oracle_missed_penalty_operation,
                pm_lazy_recall_operation,
                pm_leverage_open_operation,
                pm_leverage_close_operation,
                pm_leverage_convert_operation,
                pm_leverage_liquidate_operation,
                pm_leverage_resolve_operation,
                pm_market_accepted_operation,
                pm_payout_operation,

                // HF14 PM follow-ups (appended after the initial block — indices stay stable):
                pm_dispute_oracle_respond_operation,
                pm_unban_operation,
                // virtual operation:
                pm_ban_expired_operation,
                pm_market_expired_operation,
                // P1 oracle-metrics: dispute filed -> oracle+disputer history (virtual)
                pm_dispute_opened_operation,
                // F1/#300 early-exit deferred claim paid at settlement (virtual)
                pm_early_exit_claim_paid_operation,
                // #442/#681=D: LP income paid at settlement -> LP's own history (virtual)
                pm_lp_payout_operation
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
