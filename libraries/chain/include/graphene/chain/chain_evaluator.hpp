#pragma once

#include <graphene/protocol/authority.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <graphene/protocol/proposal_operations.hpp>
#include <graphene/chain/evaluator.hpp>

#define ASSERT_REQ_HF(HF, FEATURE) \
    FC_ASSERT(db().has_hardfork(HF), FEATURE " is not enabled until HF " BOOST_PP_STRINGIZE(HF));

namespace graphene { namespace chain {
        using namespace graphene::protocol;

        DEFINE_EVALUATOR(account_create)
        DEFINE_EVALUATOR(account_update)
        DEFINE_EVALUATOR(account_metadata)
        DEFINE_EVALUATOR(transfer)
        DEFINE_EVALUATOR(transfer_to_vesting)
        DEFINE_EVALUATOR(validator_update)
        DEFINE_EVALUATOR(account_validator_vote)
        DEFINE_EVALUATOR(account_validator_proxy)
        DEFINE_EVALUATOR(withdraw_vesting)
        DEFINE_EVALUATOR(set_withdraw_vesting_route)
        DEFINE_EVALUATOR(content)
        DEFINE_EVALUATOR(delete_content)
        DEFINE_EVALUATOR(vote)
        DEFINE_EVALUATOR(custom)
        DEFINE_EVALUATOR(escrow_transfer)
        DEFINE_EVALUATOR(escrow_approve)
        DEFINE_EVALUATOR(escrow_dispute)
        DEFINE_EVALUATOR(escrow_release)
        DEFINE_EVALUATOR(request_account_recovery)
        DEFINE_EVALUATOR(recover_account)
        DEFINE_EVALUATOR(change_recovery_account)
        DEFINE_EVALUATOR(delegate_vesting_shares)
        DEFINE_EVALUATOR(proposal_delete)
        DEFINE_EVALUATOR(chain_properties_update)
        DEFINE_EVALUATOR(versioned_chain_properties_update)
        DEFINE_EVALUATOR(committee_worker_create_request)
        DEFINE_EVALUATOR(committee_worker_cancel_request)
        DEFINE_EVALUATOR(committee_vote_request)
        DEFINE_EVALUATOR(create_invite)
        DEFINE_EVALUATOR(claim_invite_balance)
        DEFINE_EVALUATOR(invite_registration)
        DEFINE_EVALUATOR(award)
        DEFINE_EVALUATOR(set_paid_subscription)
        DEFINE_EVALUATOR(paid_subscribe)
        DEFINE_EVALUATOR(set_account_price)
        DEFINE_EVALUATOR(set_subaccount_price)
        DEFINE_EVALUATOR(buy_account)
        DEFINE_EVALUATOR(use_invite_balance)
        DEFINE_EVALUATOR(fixed_award)
        DEFINE_EVALUATOR(target_account_sale)
        DEFINE_EVALUATOR(set_reward_sharing)

        class proposal_create_evaluator: public evaluator_impl<proposal_create_evaluator> {
        public:
            using operation_type = proposal_create_operation;

            proposal_create_evaluator(database& db);

            void do_apply(const operation_type& o);

        protected:
            int depth_ = 0;
        };

        class proposal_update_evaluator: public evaluator_impl<proposal_update_evaluator> {
        public:
            using operation_type = proposal_update_operation;

            proposal_update_evaluator(database& db);

            void do_apply(const operation_type& o);

        protected:
            int depth_ = 0;
        };

} } // graphene::chain
