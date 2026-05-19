#pragma once

#include <graphene/protocol/base.hpp>
#include <graphene/protocol/block_header.hpp>
#include <graphene/protocol/asset.hpp>

#include <fc/utf8.hpp>

namespace graphene { namespace protocol {

        struct author_reward_operation : public virtual_operation {
            author_reward_operation() {
            }

            author_reward_operation(const account_name_type &a, const string &p, const asset &st, const asset &v)
                    : author(a), permlink(p), token_payout(st),
                      vesting_payout(v) {
            }

            account_name_type author;
            string permlink;
            asset token_payout;
            asset vesting_payout;
        };


        struct curation_reward_operation : public virtual_operation {
            curation_reward_operation() {
            }

            curation_reward_operation(const string &c, const asset &r, const string &a, const string &p)
                    : curator(c), reward(r), content_author(a),
                      content_permlink(p) {
            }

            account_name_type curator;
            asset reward;
            account_name_type content_author;
            string content_permlink;
        };


        struct content_reward_operation : public virtual_operation {
            content_reward_operation() {
            }

            content_reward_operation(const account_name_type &a, const string &pl, const asset &p)
                    : author(a), permlink(pl), payout(p) {
            }

            account_name_type author;
            string permlink;
            asset payout;
        };


        struct fill_vesting_withdraw_operation : public virtual_operation {
            fill_vesting_withdraw_operation() {
            }

            fill_vesting_withdraw_operation(const string &f, const string &t, const asset &w, const asset &d)
                    : from_account(f), to_account(t), withdrawn(w),
                      deposited(d) {
            }

            account_name_type from_account;
            account_name_type to_account;
            asset withdrawn;
            asset deposited;
        };


        struct shutdown_validator_operation : public virtual_operation {
            shutdown_validator_operation() {
            }

            shutdown_validator_operation(const string &o) : owner(o) {
            }

            account_name_type owner;
        };


        struct hardfork_operation : public virtual_operation {
            hardfork_operation() {
            }

            hardfork_operation(uint32_t hf_id) : hardfork_id(hf_id) {
            }

            uint32_t hardfork_id = 0;
        };

        struct content_payout_update_operation : public virtual_operation {
            content_payout_update_operation() {
            }

            content_payout_update_operation(const account_name_type &a, const string &p)
                    : author(a), permlink(p) {
            }

            account_name_type author;
            string permlink;
        };

        struct content_benefactor_reward_operation : public virtual_operation {
            content_benefactor_reward_operation() {
            }

            content_benefactor_reward_operation(const account_name_type &b, const account_name_type &a, const string &p, const asset &r)
                    : benefactor(b), author(a), permlink(p), reward(r) {
            }

            account_name_type benefactor;
            account_name_type author;
            string permlink;
            asset reward;
        };

        struct return_vesting_delegation_operation: public virtual_operation {
            return_vesting_delegation_operation() {
            }
            return_vesting_delegation_operation(const account_name_type& a, const asset& v)
            :   account(a), vesting_shares(v) {
            }

            account_name_type account;
            asset vesting_shares;
        };

        struct committee_cancel_request_operation : public virtual_operation {
            committee_cancel_request_operation() {
            }

            committee_cancel_request_operation(const uint32_t &a)
                    : request_id(a) {
            }

            uint32_t request_id;
        };

        struct committee_approve_request_operation : public virtual_operation {
            committee_approve_request_operation() {
            }

            committee_approve_request_operation(const uint32_t &a)
                    : request_id(a) {
            }

            uint32_t request_id;
        };

        struct committee_payout_request_operation : public virtual_operation {
            committee_payout_request_operation() {
            }

            committee_payout_request_operation(const uint32_t &a)
                    : request_id(a) {
            }

            uint32_t request_id;
        };

        struct committee_pay_request_operation : public virtual_operation {
            committee_pay_request_operation() {
            }

            committee_pay_request_operation(const account_name_type& w, const uint32_t &a, const asset& t)
                    : worker(w), request_id(a), tokens(t) {
            }

            account_name_type worker;
            uint32_t request_id;
            asset tokens;
        };

        struct validator_reward_operation : public virtual_operation {
            validator_reward_operation() {
            }

            validator_reward_operation(const account_name_type& w, const asset& s)
                    : validator(w), shares(s) {
            }

            account_name_type validator;
            asset shares;
        };

        struct receive_award_operation : public virtual_operation {
            receive_award_operation() {
            }

            receive_award_operation(const account_name_type& i, const account_name_type& r, const uint64_t &c, const string &m, const asset& s)
                    : initiator(i), receiver(r), custom_sequence(c), memo(m), shares(s) {
            }

            account_name_type initiator;
            account_name_type receiver;
            uint64_t custom_sequence;
            string memo;
            asset shares;
        };

        struct benefactor_award_operation : public virtual_operation {
            benefactor_award_operation() {
            }

            benefactor_award_operation(const account_name_type& i, const account_name_type& b, const account_name_type& r, const uint64_t &c, const string &m, const asset& s)
                    : initiator(i), benefactor(b), receiver(r), custom_sequence(c), memo(m), shares(s) {
            }

            account_name_type initiator;
            account_name_type benefactor;
            account_name_type receiver;
            uint64_t custom_sequence;
            string memo;
            asset shares;
        };

        struct paid_subscription_action_operation : public virtual_operation {
            paid_subscription_action_operation() {
            }

            paid_subscription_action_operation(const account_name_type& s, const account_name_type& a, const uint16_t &l, const asset& am, const uint16_t &p, const uint64_t &sds, const asset& sam)
                    : subscriber(s), account(a), level(l), amount(am), period(p), summary_duration_sec(sds), summary_amount(sam) {
            }

            account_name_type subscriber;
            account_name_type account;
            uint16_t level;
            asset amount;
            uint16_t period;
            uint64_t summary_duration_sec;
            asset summary_amount;
        };

        struct cancel_paid_subscription_operation : public virtual_operation {
            cancel_paid_subscription_operation() {
            }

            cancel_paid_subscription_operation(const account_name_type& s, const account_name_type& a)
                    : subscriber(s), account(a) {
            }

            account_name_type subscriber;
            account_name_type account;
        };

        struct account_sale_operation : public virtual_operation {
            account_sale_operation() {
            }

            account_sale_operation(const account_name_type& a, const asset& p, const account_name_type& b, const account_name_type& s)
                    : account(a), price(p), buyer(b), seller(s) {
            }

            account_name_type account;
            asset price;
            account_name_type buyer;
            account_name_type seller;
        };

        struct expire_escrow_ratification_operation : public virtual_operation {
            expire_escrow_ratification_operation() {
            }

            expire_escrow_ratification_operation(const account_name_type& f, const account_name_type& t, const account_name_type& a, const uint32_t& id, const asset& ta, const asset& fee, const time_point_sec& rd )
                    : from(f), to(t), agent(a), escrow_id(id), token_amount(ta), fee(fee), ratification_deadline(rd) {
            }

            account_name_type from;
            account_name_type to;
            account_name_type agent;
            uint32_t escrow_id;

            asset token_amount;
            asset fee;

            time_point_sec ratification_deadline;
        };

        struct bid_operation : public virtual_operation {
            bid_operation() {
            }

            bid_operation(const account_name_type& a, const account_name_type& b, const asset& p)
                    : account(a), bidder(b), bid(p) {
            }
            account_name_type account;
            account_name_type bidder;
            asset bid;
        };

        struct outbid_operation : public virtual_operation {
            outbid_operation() {
            }

            outbid_operation(const account_name_type& a, const account_name_type& b, const asset& p)
                    : account(a), bidder(b), bid(p) {
            }
            account_name_type account;
            account_name_type bidder;
            asset bid;
        };

        // HF13: Validator reward sharing
        /// Emitted once per stakeholder per validator at each distribution epoch end.
        struct stakeholder_reward_operation : public virtual_operation {
            stakeholder_reward_operation() {}

            stakeholder_reward_operation(
                const account_name_type& v,
                const account_name_type& stakeholder,
                const asset& s)
                : validator(v), stakeholder(stakeholder), shares(s) {}

            account_name_type validator;
            account_name_type stakeholder;
            asset shares;   ///< SHARES credited to stakeholder
        };
} } //graphene::protocol

FC_REFLECT((graphene::protocol::author_reward_operation), (author)(permlink)(token_payout)(vesting_payout))
FC_REFLECT((graphene::protocol::curation_reward_operation), (curator)(reward)(content_author)(content_permlink))
FC_REFLECT((graphene::protocol::content_reward_operation), (author)(permlink)(payout))
FC_REFLECT((graphene::protocol::fill_vesting_withdraw_operation), (from_account)(to_account)(withdrawn)(deposited))
FC_REFLECT((graphene::protocol::shutdown_validator_operation), (owner))
FC_REFLECT((graphene::protocol::hardfork_operation), (hardfork_id))
FC_REFLECT((graphene::protocol::content_payout_update_operation), (author)(permlink))
FC_REFLECT((graphene::protocol::content_benefactor_reward_operation), (benefactor)(author)(permlink)(reward))
FC_REFLECT((graphene::protocol::return_vesting_delegation_operation), (account)(vesting_shares))
FC_REFLECT((graphene::protocol::committee_cancel_request_operation), (request_id))
FC_REFLECT((graphene::protocol::committee_approve_request_operation), (request_id))
FC_REFLECT((graphene::protocol::committee_payout_request_operation), (request_id))
FC_REFLECT((graphene::protocol::committee_pay_request_operation), (worker)(request_id)(tokens))
FC_REFLECT((graphene::protocol::validator_reward_operation), (validator)(shares))
FC_REFLECT((graphene::protocol::receive_award_operation), (initiator)(receiver)(custom_sequence)(memo)(shares))
FC_REFLECT((graphene::protocol::benefactor_award_operation), (initiator)(benefactor)(receiver)(custom_sequence)(memo)(shares))
FC_REFLECT((graphene::protocol::paid_subscription_action_operation), (subscriber)(account)(level)(amount)(period)(summary_amount))
FC_REFLECT((graphene::protocol::cancel_paid_subscription_operation), (subscriber)(account))
FC_REFLECT((graphene::protocol::account_sale_operation), (account)(price)(buyer)(seller))
FC_REFLECT((graphene::protocol::expire_escrow_ratification_operation), (from)(to)(agent)(escrow_id)(token_amount)(fee)(ratification_deadline))
FC_REFLECT((graphene::protocol::bid_operation), (account)(bidder)(bid))
FC_REFLECT((graphene::protocol::outbid_operation), (account)(bidder)(bid))
// HF13
FC_REFLECT((graphene::protocol::stakeholder_reward_operation), (validator)(stakeholder)(shares))
