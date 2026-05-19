#pragma once

#include <graphene/protocol/base.hpp>
#include <graphene/protocol/block_header.hpp>
#include <graphene/protocol/asset.hpp>

#include <fc/utf8.hpp>

namespace graphene { namespace protocol {

        struct account_create_operation: public base_operation {
            asset fee;
            asset delegation;
            account_name_type creator;
            account_name_type new_account_name;
            authority master;
            authority active;
            authority regular;
            public_key_type memo_key;
            string json_metadata;
            account_name_type referrer;

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const {
                a.insert(creator);
            }
        };

        struct account_update_operation : public base_operation {
            account_name_type account;
            optional<authority> master;
            optional<authority> active;
            optional<authority> regular;
            public_key_type memo_key;
            string json_metadata;

            void validate() const;

            void get_required_master_authorities(flat_set<account_name_type> &a) const {
                if (master) {
                    a.insert(account);
                }
            }

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                if (!master) {
                    a.insert(account);
                }
            }
        };

        struct account_metadata_operation : public base_operation {
            account_name_type account;
            string json_metadata;

            void validate() const;
            void get_required_regular_authorities(flat_set<account_name_type>& a) const {
                a.insert(account);
            }
        };


        struct beneficiary_route_type {
            beneficiary_route_type() {
            }

            beneficiary_route_type(const account_name_type &a, const uint16_t &w)
                    : account(a), weight(w) {
            }

            account_name_type account;
            uint16_t weight;

            // For use by std::sort such that the route is sorted first by name (ascending)
            bool operator<(const beneficiary_route_type &o) const {
                return string_less()(account, o.account);
            }
        };

        struct content_payout_beneficiaries {
            vector <beneficiary_route_type> beneficiaries;

            void validate() const;
        };

        typedef static_variant <
            content_payout_beneficiaries
        > content_extension;

        typedef flat_set <content_extension> content_extensions_type;

        struct content_operation : public base_operation {
            account_name_type parent_author;
            string parent_permlink;

            account_name_type author;
            string permlink;

            string title;
            string body;
            int16_t curation_percent;
            string json_metadata;
            content_extensions_type extensions;

            void validate() const;

            void get_required_regular_authorities(flat_set<account_name_type> &a) const {
                a.insert(author);
            }
        };


        struct delete_content_operation : public base_operation {
            account_name_type author;
            string permlink;

            void validate() const;

            void get_required_regular_authorities(flat_set<account_name_type> &a) const {
                a.insert(author);
            }
        };


        struct vote_operation : public base_operation {
            account_name_type voter;
            account_name_type author;
            string permlink;
            int16_t weight = 0;

            void validate() const;

            void get_required_regular_authorities(flat_set<account_name_type> &a) const {
                a.insert(voter);
            }
        };


        /**
         * @ingroup operations
         *
         * @brief Transfers tokens from one account to another.
         */
        struct transfer_operation : public base_operation {
            account_name_type from;
            /// Account to transfer asset to
            account_name_type to;
            /// The amount of asset to transfer from @ref from to @ref to
            asset amount;

            /// The memo is plain-text, any encryption on the memo is up to
            /// a higher level protocol.
            string memo;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                if (amount.symbol != SHARES_SYMBOL) {
                    a.insert(from);
                }
            }

            void get_required_master_authorities(flat_set<account_name_type> &a) const {
                if (amount.symbol == SHARES_SYMBOL) {
                    a.insert(from);
                }
            }
        };


        /**
         *  The purpose of this operation is to enable someone to send money contingently to
         *  another individual. The funds leave the *from* account and go into a temporary balance
         *  where they are held until *from* releases it to *to* or *to* refunds it to *from*.
         *
         *  In the event of a dispute the *agent* can divide the funds between the to/from account.
         *  Disputes can be raised any time before or on the dispute deadline time, after the escrow
         *  has been approved by all parties.
         *
         *  This operation only creates a proposed escrow transfer. Both the *agent* and *to* must
         *  agree to the terms of the arrangement by approving the escrow.
         *
         *  The escrow agent is paid the fee on approval of all parties. It is up to the escrow agent
         *  to determine the fee.
         *
         *  Escrow transactions are uniquely identified by 'from' and 'escrow_id', the 'escrow_id' is defined
         *  by the sender.
         */
        struct escrow_transfer_operation : public base_operation {
            account_name_type from;
            account_name_type to;
            account_name_type agent;
            uint32_t escrow_id = 30;

            asset token_amount = asset(0, TOKEN_SYMBOL);
            asset fee;

            time_point_sec ratification_deadline;
            time_point_sec escrow_expiration;

            string json_metadata;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(from);
            }
        };


        /**
         *  The agent and to accounts must approve an escrow transaction for it to be valid on
         *  the blockchain. Once a part approves the escrow, the cannot revoke their approval.
         *  Subsequent escrow approve operations, regardless of the approval, will be rejected.
         */
        struct escrow_approve_operation : public base_operation {
            account_name_type from;
            account_name_type to;
            account_name_type agent;
            account_name_type who; // Either to or agent

            uint32_t escrow_id = 30;
            bool approve = true;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(who);
            }
        };


        /**
         *  If either the sender or receiver of an escrow payment has an issue, they can
         *  raise it for dispute. Once a payment is in dispute, the agent has authority over
         *  who gets what.
         */
        struct escrow_dispute_operation : public base_operation {
            account_name_type from;
            account_name_type to;
            account_name_type agent;
            account_name_type who;

            uint32_t escrow_id = 30;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(who);
            }
        };


        /**
         *  This operation can be used by anyone associated with the escrow transfer to
         *  release funds if they have permission.
         *
         *  The permission scheme is as follows:
         *  If there is no dispute and escrow has not expired, either party can release funds to the other.
         *  If escrow expires and there is no dispute, either party can release funds to either party.
         *  If there is a dispute regardless of expiration, the agent can release funds to either party
         *     following whichever agreement was in place between the parties.
         */
        struct escrow_release_operation : public base_operation {
            account_name_type from;
            account_name_type to; ///< the original 'to'
            account_name_type agent;
            account_name_type who; ///< the account that is attempting to release the funds, determines valid 'receiver'
            account_name_type receiver; ///< the account that should receive funds (might be from, might be to)

            uint32_t escrow_id = 30;
            asset token_amount = asset(0, TOKEN_SYMBOL); ///< the amount of tokens to release

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(who);
            }
        };


        /**
         *  This operation converts tokens into SHARES at
         *  the current exchange rate. With this operation it is possible to
         *  give another account vesting shares so that faucets can
         *  pre-fund new accounts with vesting shares.
         */
        struct transfer_to_vesting_operation : public base_operation {
            account_name_type from;
            account_name_type to; ///< if null, then same as from
            asset amount; ///< must be token

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(from);
            }
        };


        /**
         * At any given point in time an account can be withdrawing from their
         * vesting shares. A user may change the number of shares they wish to
         * cash out at any time between 0 and their total vesting stake.
         *
         * After applying this operation, vesting_shares will be withdrawn
         * at a rate of vesting_shares/104 per week for two years starting
         * one week after this operation is included in the blockchain.
         *
         * This operation is not valid if the user has no vesting shares.
         */
        struct withdraw_vesting_operation : public base_operation {
            account_name_type account;
            asset vesting_shares;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(account);
            }
        };


        /**
         * Allows an account to setup a vesting withdraw but with the additional
         * request for the funds to be transferred directly to another account's
         * balance rather than the withdrawing account. In addition, those funds
         * can be immediately vested again, circumventing the conversion from
         * SHARES to token and back, guaranteeing they maintain their value.
         */
        struct set_withdraw_vesting_route_operation : public base_operation {
            account_name_type from_account;
            account_name_type to_account;
            uint16_t percent = 0;
            bool auto_vest = false;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(from_account);
            }
        };

        struct chain_properties_hf4;
        struct chain_properties_hf6;
        struct chain_properties_hf9;
        struct chain_properties_hf13;

        /**
         * Witnesses must vote on how to set certain chain properties to ensure a smooth
         * and well functioning network. Any time @owner is in the active set of witnesses these
         * properties will be used to control the blockchain configuration.
         */
        struct chain_properties_init {
            /**
             *  This fee, paid in token, is converted into SHARES for the new account. Accounts
             *  without vesting shares cannot earn usage rations and therefore are powerless. This minimum
             *  fee requires all accounts to have some kind of commitment to the network that includes the
             *  ability to vote and make transactions.
             */
            asset account_creation_fee = asset(CHAIN_MIN_ACCOUNT_CREATION_FEE, TOKEN_SYMBOL);

            /**
             *  This witnesses vote for the maximum_block_size which is used by the network
             *  to tune rate limiting and capacity
             */
            uint32_t maximum_block_size = CHAIN_MIN_BLOCK_SIZE_LIMIT * 2;

            /**
             *  Ratio for delegated VIZ on account creation
             *
             *  target_delegation = create_account_delegation_ratio * account_creation_fee
             */
            uint32_t create_account_delegation_ratio = CHAIN_CREATE_ACCOUNT_DELEGATION_RATIO;

            /**
             * Minimum time of delegated SHARES on create account
             */
            uint32_t create_account_delegation_time = (CHAIN_CREATE_ACCOUNT_DELEGATION_TIME).to_seconds();

            /**
             * Minimum delegated VIZ
             */
            asset min_delegation = asset(CHAIN_MIN_DELEGATION, TOKEN_SYMBOL);

            /**
             *  Curation percent range, check median value on payout
             */
            int16_t min_curation_percent = CHAIN_REWARD_FUND_CURATOR_PERCENT;
            int16_t max_curation_percent = CHAIN_REWARD_FUND_CURATOR_PERCENT;

            /**
             *  Consensus - bandwidth reserve percent for account below X shares
             */
            int16_t bandwidth_reserve_percent = CONSENSUS_BANDWIDTH_RESERVE_PERCENT;
            asset bandwidth_reserve_below = asset(CONSENSUS_BANDWIDTH_RESERVE_BELOW, SHARES_SYMBOL);

            /**
             *  Consensus - Flag/Downvote energy cost may be higher from 0 to CHAIN_100_PERCENT
             */
            int16_t flag_energy_additional_cost = CONSENSUS_FLAG_ENERGY_ADDITIONAL_COST;

            /**
             *  Consensus - Minimal vote rshares amount for accounting by payout from reward pool
             */
            uint32_t vote_accounting_min_rshares = CONSENSUS_VOTE_ACCOUNTING_MIN_RSHARES;

            /**
             *  Consensus - Minimal shares percent for approving committee request
             */
            int16_t committee_request_approve_min_percent = CONSENSUS_COMMITTEE_REQUEST_APPROVE_MIN_PERCENT;

            void validate() const {
                FC_ASSERT(account_creation_fee.amount >= CHAIN_MIN_ACCOUNT_CREATION_FEE);
                FC_ASSERT(account_creation_fee.symbol == TOKEN_SYMBOL);
                FC_ASSERT(maximum_block_size >= CHAIN_MIN_BLOCK_SIZE_LIMIT);
                FC_ASSERT(maximum_block_size <= CHAIN_MAX_BLOCK_SIZE_LIMIT);
                FC_ASSERT(create_account_delegation_ratio > 0);
                FC_ASSERT(create_account_delegation_time >= 0);
                FC_ASSERT(create_account_delegation_time >= CHAIN_ENERGY_REGENERATION_SECONDS);//prevent delegation abuse (energy double use)
                FC_ASSERT(min_delegation.amount > 0);
                FC_ASSERT(min_delegation.symbol == TOKEN_SYMBOL);
                FC_ASSERT(min_curation_percent >= 0);
                FC_ASSERT(max_curation_percent <= CHAIN_100_PERCENT);
                FC_ASSERT(min_curation_percent <= max_curation_percent);
                FC_ASSERT(bandwidth_reserve_percent >= 0);
                FC_ASSERT(bandwidth_reserve_percent <= CHAIN_100_PERCENT);
                FC_ASSERT(bandwidth_reserve_below.amount >= 0);
                FC_ASSERT(bandwidth_reserve_below.symbol == SHARES_SYMBOL);
                FC_ASSERT(flag_energy_additional_cost >= 0);
                FC_ASSERT(flag_energy_additional_cost <= CHAIN_100_PERCENT);
                FC_ASSERT(committee_request_approve_min_percent >= 0);
                FC_ASSERT(committee_request_approve_min_percent <= CHAIN_100_PERCENT);
            }

            chain_properties_init& operator=(const chain_properties_init&) = default;
            chain_properties_init& operator=(const chain_properties_hf4& src);
            chain_properties_init& operator=(const chain_properties_hf6& src);
            chain_properties_init& operator=(const chain_properties_hf9& src);
            chain_properties_init& operator=(const chain_properties_hf13& src);
        };

        struct chain_properties_hf4: public chain_properties_init {
            /**
             *  Consensus - Witness reward percent from block inflation
             */
            int16_t inflation_validator_percent = CHAIN_CONSENSUS_INFLATION_VALIDATOR_PERCENT;

            /**
             *  Consensus - Inflation ratio between committee and reward fund
             */
            int16_t inflation_ratio_committee_vs_reward_fund = CHAIN_CONSENSUS_INFLATION_RATIO;

            /**
             *  Consensus - Inflation ratio between committee and reward fund
             */
            uint32_t inflation_recalc_period = CHAIN_CONSENSUS_INFLATION_RECALC_PERIOD;

            void validate() const {
                chain_properties_init::validate();
                FC_ASSERT(inflation_validator_percent >= 0);
                FC_ASSERT(inflation_validator_percent <= CHAIN_100_PERCENT);
                FC_ASSERT(inflation_ratio_committee_vs_reward_fund >= 0);
                FC_ASSERT(inflation_ratio_committee_vs_reward_fund <= CHAIN_100_PERCENT);
                FC_ASSERT(inflation_recalc_period >= 0);
                FC_ASSERT(inflation_recalc_period <= CHAIN_BLOCKS_PER_YEAR);
            }

            chain_properties_hf4& operator=(const chain_properties_init& src) {
                chain_properties_init::operator=(src);
                return *this;
            }

            chain_properties_hf4& operator=(const chain_properties_hf4&) = default;
        };

        struct chain_properties_hf6: public chain_properties_hf4 {
            /**
             *  Consensus - Operations with raw data can cost additional bandwidth (in percent ratio)
             */
            uint32_t data_operations_cost_additional_bandwidth = CONSENSUS_DATA_OPERATIONS_COST_ADDITIONAL_BANDWIDTH;

            /**
             *  Consensus - Witness who missed the block will receive a penality of a percentage of the votes
             */
            int16_t validator_miss_penalty_percent = CONSENSUS_VALIDATOR_MISS_PENALTY_PERCENT;

            /**
             *  Consensus - Witness who missed the block will receive a penality with duration (in seconds)
             */
            uint32_t validator_miss_penalty_duration = CONSENSUS_VALIDATOR_MISS_PENALTY_DURATION;

            void validate() const {
                chain_properties_hf4::validate();
                FC_ASSERT(data_operations_cost_additional_bandwidth >= 0);
                FC_ASSERT(validator_miss_penalty_percent >= 0);
                FC_ASSERT(validator_miss_penalty_percent <= CHAIN_100_PERCENT);
                FC_ASSERT(validator_miss_penalty_duration >= 0);
                FC_ASSERT(validator_miss_penalty_duration <= (CHAIN_BLOCKS_PER_YEAR * CHAIN_BLOCK_INTERVAL));
            }

            chain_properties_hf6& operator=(const chain_properties_init& src) {
                chain_properties_init::operator=(src);
                return *this;
            }

            chain_properties_hf6& operator=(const chain_properties_hf4& src) {
                chain_properties_hf4::operator=(src);
                return *this;
            }

            chain_properties_hf6& operator=(const chain_properties_hf6&) = default;
        };

         struct chain_properties_hf9: public chain_properties_hf6 {
            /**
             *  Consensus - Minimum amount of tokens to create an invite
             */
            asset create_invite_min_balance = asset(CONSENSUS_CREATE_INVITE_MIN_BALANCE, TOKEN_SYMBOL);

            /**
             *  Consensus - Fee to the network committee for create request to committee for review
             */
            asset committee_create_request_fee = asset(CONSENSUS_COMMITTEE_CREATE_REQUEST_FEE, TOKEN_SYMBOL);

            /**
             *  Consensus - Fee to the network committee for create paid subscription
             */
            asset create_paid_subscription_fee = asset(CONSENSUS_CREATE_PAID_SUBSCRIPTION_FEE, TOKEN_SYMBOL);

            /**
             *  Consensus - Fee to the network committee for setting account on sale
             */
            asset account_on_sale_fee = asset(CONSENSUS_ACCOUNT_ON_SALE_FEE, TOKEN_SYMBOL);

            /**
             *  Consensus - Fee to the network committee for setting subaccounts on sale
             */
            asset subaccount_on_sale_fee = asset(CONSENSUS_SUBACCOUNT_ON_SALE_FEE, TOKEN_SYMBOL);

            /**
             *  Consensus - Fee to the network committee for declare account as witness
             */
            asset validator_declaration_fee = asset(CONSENSUS_VALIDATOR_DECLARATION_FEE, TOKEN_SYMBOL);

            /**
             *  Consensus - withdraw intervals (duration defined as CHAIN_VESTING_WITHDRAW_INTERVAL_SECONDS equal 1 day)
             */
            uint16_t withdraw_intervals = CHAIN_VESTING_WITHDRAW_INTERVALS;


            void validate() const {
                chain_properties_hf6::validate();
                FC_ASSERT(create_invite_min_balance.amount > 0);
                FC_ASSERT(create_invite_min_balance.symbol == TOKEN_SYMBOL);
                FC_ASSERT(committee_create_request_fee.amount > 0);
                FC_ASSERT(committee_create_request_fee.symbol == TOKEN_SYMBOL);
                FC_ASSERT(create_paid_subscription_fee.amount > 0);
                FC_ASSERT(create_paid_subscription_fee.symbol == TOKEN_SYMBOL);
                FC_ASSERT(account_on_sale_fee.amount > 0);
                FC_ASSERT(account_on_sale_fee.symbol == TOKEN_SYMBOL);
                FC_ASSERT(subaccount_on_sale_fee.amount > 0);
                FC_ASSERT(subaccount_on_sale_fee.symbol == TOKEN_SYMBOL);
                FC_ASSERT(validator_declaration_fee.amount > 0);
                FC_ASSERT(validator_declaration_fee.symbol == TOKEN_SYMBOL);
                FC_ASSERT(withdraw_intervals > 0);
            }

            chain_properties_hf9& operator=(const chain_properties_init& src) {
                chain_properties_init::operator=(src);
                return *this;
            }

            chain_properties_hf9& operator=(const chain_properties_hf4& src) {
                chain_properties_hf4::operator=(src);
                return *this;
            }

            chain_properties_hf9& operator=(const chain_properties_hf6& src) {
                chain_properties_hf6::operator=(src);
                return *this;
            }

            chain_properties_hf9& operator=(const chain_properties_hf9&) = default;
        };

        struct chain_properties_hf13: public chain_properties_hf9 {
            /**
             *  Consensus - Period in blocks between delegator reward distributions.
             *  Each validator accumulates block rewards during this epoch and
             *  distributes the delegator share proportionally at epoch end.
             *  Allowed range: [CHAIN_MIN_DISTRIBUTION_EPOCH_LENGTH, CHAIN_BLOCKS_PER_YEAR].
             */
            uint32_t distribution_epoch_length = CHAIN_DEFAULT_DISTRIBUTION_EPOCH_LENGTH;

            void validate() const {
                chain_properties_hf9::validate();
                FC_ASSERT(distribution_epoch_length >= CHAIN_MIN_DISTRIBUTION_EPOCH_LENGTH,
                    "distribution_epoch_length must be at least ${min} blocks",
                    ("min", CHAIN_MIN_DISTRIBUTION_EPOCH_LENGTH));
                FC_ASSERT(distribution_epoch_length <= uint32_t(CHAIN_BLOCKS_PER_YEAR),
                    "distribution_epoch_length must not exceed one year in blocks");
            }

            chain_properties_hf13& operator=(const chain_properties_init& src) {
                chain_properties_init::operator=(src);
                return *this;
            }

            chain_properties_hf13& operator=(const chain_properties_hf4& src) {
                chain_properties_hf4::operator=(src);
                return *this;
            }

            chain_properties_hf13& operator=(const chain_properties_hf6& src) {
                chain_properties_hf6::operator=(src);
                return *this;
            }

            chain_properties_hf13& operator=(const chain_properties_hf9& src) {
                chain_properties_hf9::operator=(src);
                return *this;
            }

            chain_properties_hf13& operator=(const chain_properties_hf13&) = default;
        };

        inline chain_properties_init& chain_properties_init::operator=(const chain_properties_hf13& src) {
            account_creation_fee = src.account_creation_fee;
            maximum_block_size = src.maximum_block_size;
            create_account_delegation_ratio = src.create_account_delegation_ratio;
            create_account_delegation_time = src.create_account_delegation_time;
            min_delegation = src.min_delegation;
            min_curation_percent = src.min_curation_percent;
            max_curation_percent = src.max_curation_percent;
            bandwidth_reserve_percent = src.bandwidth_reserve_percent;
            bandwidth_reserve_below = src.bandwidth_reserve_below;
            flag_energy_additional_cost = src.flag_energy_additional_cost;
            vote_accounting_min_rshares = src.vote_accounting_min_rshares;
            committee_request_approve_min_percent = src.committee_request_approve_min_percent;
            return *this;
        }

        inline chain_properties_init& chain_properties_init::operator=(const chain_properties_hf4& src) {
            account_creation_fee = src.account_creation_fee;
            maximum_block_size = src.maximum_block_size;
            create_account_delegation_ratio = src.create_account_delegation_ratio;
            create_account_delegation_time = src.create_account_delegation_time;
            min_delegation = src.min_delegation;
            max_curation_percent = src.max_curation_percent;
            min_curation_percent = src.min_curation_percent;
            bandwidth_reserve_percent = src.bandwidth_reserve_percent;
            bandwidth_reserve_below = src.bandwidth_reserve_below;
            flag_energy_additional_cost = src.flag_energy_additional_cost;
            vote_accounting_min_rshares = src.vote_accounting_min_rshares;
            committee_request_approve_min_percent = src.committee_request_approve_min_percent;
            return *this;
        }

        inline chain_properties_init& chain_properties_init::operator=(const chain_properties_hf6& src) {
            account_creation_fee = src.account_creation_fee;
            maximum_block_size = src.maximum_block_size;
            create_account_delegation_ratio = src.create_account_delegation_ratio;
            create_account_delegation_time = src.create_account_delegation_time;
            min_delegation = src.min_delegation;
            max_curation_percent = src.max_curation_percent;
            min_curation_percent = src.min_curation_percent;
            bandwidth_reserve_percent = src.bandwidth_reserve_percent;
            bandwidth_reserve_below = src.bandwidth_reserve_below;
            flag_energy_additional_cost = src.flag_energy_additional_cost;
            vote_accounting_min_rshares = src.vote_accounting_min_rshares;
            committee_request_approve_min_percent = src.committee_request_approve_min_percent;
            return *this;
        }

        inline chain_properties_init& chain_properties_init::operator=(const chain_properties_hf9& src) {
            account_creation_fee = src.account_creation_fee;
            maximum_block_size = src.maximum_block_size;
            create_account_delegation_ratio = src.create_account_delegation_ratio;
            create_account_delegation_time = src.create_account_delegation_time;
            min_delegation = src.min_delegation;
            max_curation_percent = src.max_curation_percent;
            min_curation_percent = src.min_curation_percent;
            bandwidth_reserve_percent = src.bandwidth_reserve_percent;
            bandwidth_reserve_below = src.bandwidth_reserve_below;
            flag_energy_additional_cost = src.flag_energy_additional_cost;
            vote_accounting_min_rshares = src.vote_accounting_min_rshares;
            committee_request_approve_min_percent = src.committee_request_approve_min_percent;
            return *this;
        }

        using versioned_chain_properties = fc::static_variant<
            chain_properties_init,
            chain_properties_hf4,
            chain_properties_hf6,
            chain_properties_hf9,
            chain_properties_hf13
        >;

        /**
         *  If the owner isn't a witness they will become a witness.
         *
         *  If the block_signing_key is null then the witness is removed from
         *  contention. The network will pick the top 21 witnesses for
         *  producing blocks.
         */
        struct validator_update_operation : public base_operation {
            account_name_type owner;
            string url;
            public_key_type block_signing_key;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(owner);
            }
        };

        /**
         *  Wintesses can change some dynamic votable params to control the blockchain configuration
         */
        struct chain_properties_update_operation : public base_operation {
            account_name_type owner;
            chain_properties_init props;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(owner);
            }
        };

        /**
         *  Wintesses can change some dynamic votable params to control the blockchain configuration
         */
        struct versioned_chain_properties_update_operation : public base_operation {
            account_name_type owner;
            versioned_chain_properties props;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(owner);
            }
        };


        /**
         * All accounts with a VFS can vote for or against any witness.
         *
         * If a proxy is specified then all existing votes are removed.
         */
        struct account_validator_vote_operation : public base_operation {
            account_name_type account;
            account_name_type validator;
            bool approve = true;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(account);
            }
        };


        struct account_validator_proxy_operation : public base_operation {
            account_name_type account;
            account_name_type proxy;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(account);
            }
        };


        struct custom_operation : public base_operation {
            flat_set<account_name_type> required_active_auths;
            flat_set<account_name_type> required_regular_auths;
            string id; ///< must be less than 32 characters long
            string json; ///< must be proper utf8 / JSON string.

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                for (const auto &i : required_active_auths) {
                    a.insert(i);
                }
            }

            void get_required_regular_authorities(flat_set<account_name_type> &a) const {
                for (const auto &i : required_regular_auths) {
                    a.insert(i);
                }
            }
        };


        /**
         * All account recovery requests come from a listed recovery account. This
         * is secure based on the assumption that only a trusted account should be
         * a recovery account. It is the responsibility of the recovery account to
         * verify the identity of the account holder of the account to recover by
         * whichever means they have agreed upon. The blockchain assumes identity
         * has been verified when this operation is broadcast.
         *
         * This operation creates an account recovery request which the account to
         * recover has 24 hours to respond to before the request expires and is
         * invalidated.
         *
         * There can only be one active recovery request per account at any one time.
         * Pushing this operation for an account to recover when it already has
         * an active request will either update the request to a new new master authority
         * and extend the request expiration to 24 hours from the current head block
         * time or it will delete the request. To cancel a request, simply set the
         * weight threshold of the new master authority to 0, making it an open authority.
         *
         * Additionally, the new master authority must be satisfiable. In other words,
         * the sum of the key weights must be greater than or equal to the weight
         * threshold.
         *
         * This operation only needs to be signed by the the recovery account.
         * The account to recover confirms its identity to the blockchain in
         * the recover account operation.
         */
        struct request_account_recovery_operation : public base_operation {
            account_name_type recovery_account;       ///< The recovery account is listed as the recovery account on the account to recover.

            account_name_type account_to_recover;     ///< The account to recover. This is likely due to a compromised master authority.

            authority new_master_authority;    ///< The new master authority the account to recover wishes to have. This is secret
            ///< known by the account to recover and will be confirmed in a recover_account_operation

            extensions_type extensions;             ///< Extensions. Not currently used.

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(recovery_account);
            }

            void validate() const;
        };


        /**
         * Recover an account to a new authority using a previous authority and verification
         * of the recovery account as proof of identity. This operation can only succeed
         * if there was a recovery request sent by the account's recover account.
         *
         * In order to recover the account, the account holder must provide proof
         * of past ownership and proof of identity to the recovery account. Being able
         * to satisfy an master authority that was used in the past 30 days is sufficient
         * to prove past ownership. The get_master_history function in the database API
         * returns past master authorities that are valid for account recovery.
         *
         * Proving identity is an off chain contract between the account holder and
         * the recovery account. The recovery request contains a new authority which
         * must be satisfied by the account holder to regain control. The actual process
         * of verifying authority may become complicated, but that is an application
         * level concern, not a blockchain concern.
         *
         * This operation requires both the past and future master authorities in the
         * operation because neither of them can be derived from the current chain state.
         * The operation must be signed by keys that satisfy both the new master authority
         * and the recent master authority. Failing either fails the operation entirely.
         *
         * If a recovery request was made inadvertantly, the account holder should
         * contact the recovery account to have the request deleted.
         *
         * The two setp combination of the account recovery request and recover is
         * safe because the recovery account never has access to secrets of the account
         * to recover. They simply act as an on chain endorsement of off chain identity.
         * In other systems, a fork would be required to enforce such off chain state.
         * Additionally, an account cannot be permanently recovered to the wrong account.
         * While any master authority from the past 30 days can be used, including a compromised
         * authority, the account can be continually recovered until the recovery account
         * is confident a combination of uncompromised authorities were used to
         * recover the account. The actual process of verifying authority may become
         * complicated, but that is an application level concern, not the blockchain's
         * concern.
         */
        struct recover_account_operation : public base_operation {
            account_name_type account_to_recover;        ///< The account to be recovered

            authority new_master_authority;       ///< The new master authority as specified in the request account recovery operation.

            authority recent_master_authority;    ///< A previous master authority that the account holder will use to prove past ownership of the account to be recovered.

            extensions_type extensions;                ///< Extensions. Not currently used.

            void get_required_authorities(vector<authority> &a) const {
                a.push_back(new_master_authority);
                a.push_back(recent_master_authority);
            }

            void validate() const;
        };


        /**
         * Each account lists another account as their recovery account.
         * The recovery account has the ability to create account_recovery_requests
         * for the account to recover. An account can change their recovery account
         * at any time with a 30 day delay. This delay is to prevent
         * an attacker from changing the recovery account to a malicious account
         * during an attack. These 30 days match the 30 days that an
         * master authority is valid for recovery purposes.
         *
         * On account creation the recovery account is set either to the creator of
         * the account (The account that pays the creation fee and is a signer on the transaction)
         * or to the empty string if the account was in snapshot. An account with no recovery
         * has the top voted witness as a recovery account, at the time the recover
         * request is created. Note: This does mean the effective recovery account
         * of an account with no listed recovery account can change at any time as
         * witness vote weights. The top voted witness is explicitly the most trusted
         * witness according to stake.
         */
        struct change_recovery_account_operation : public base_operation {
            account_name_type account_to_recover;     ///< The account that would be recovered in case of compromise
            account_name_type new_recovery_account;   ///< The account that creates the recover request
            extensions_type extensions;             ///< Extensions. Not currently used.

            void get_required_master_authorities(flat_set<account_name_type> &a) const {
                a.insert(account_to_recover);
            }

            void validate() const;
        };


/**
 * Delegate vesting shares from one account to the other. The vesting shares are still owned
 * by the original account, but content voting rights and bandwidth allocation are transferred
 * to the receiving account. This sets the delegation to `vesting_shares`, increasing it or
 * decreasing it as needed. (i.e. a delegation of 0 removes the delegation)
 *
 * When a delegation is removed the shares are placed in limbo for a week to prevent a satoshi
 * of SHARES from voting on the same content twice.
 */
        class delegate_vesting_shares_operation: public base_operation {
        public:
            account_name_type delegator;    ///< The account delegating vesting shares
            account_name_type delegatee;    ///< The account receiving vesting shares
            asset vesting_shares;           ///< The amount of vesting shares delegated

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const {
                a.insert(delegator);
            }
        };

        struct committee_worker_create_request_operation : public base_operation {
            account_name_type creator;
            string url;
            account_name_type worker;
            asset required_amount_min;
            asset required_amount_max;
            uint32_t duration;

            void validate() const {
                FC_ASSERT(url.size() > 0, "URL size must be greater than 0");
                FC_ASSERT(url.size() < CHAIN_MAX_URL_LENGTH, "URL size must be lesser than 256");
                FC_ASSERT(required_amount_min.amount >= 0);
                FC_ASSERT(required_amount_min.symbol == TOKEN_SYMBOL);
                FC_ASSERT(required_amount_max.amount > required_amount_min.amount);
                FC_ASSERT(required_amount_max.symbol == TOKEN_SYMBOL);
                FC_ASSERT(duration >= COMMITTEE_MIN_DURATION);
                FC_ASSERT(duration <= COMMITTEE_MAX_DURATION);
                FC_ASSERT(required_amount_max.amount <= COMMITTEE_MAX_REQUIRED_AMOUNT);
            }

            void get_required_regular_authorities(flat_set<account_name_type> &a) const {
                a.insert(creator);
            }
        };


        struct committee_worker_cancel_request_operation : public base_operation {
            account_name_type creator;
            uint32_t request_id;

            void validate() const {}

            void get_required_regular_authorities(flat_set<account_name_type> &a) const {
                a.insert(creator);
            }
        };


        struct committee_vote_request_operation : public base_operation {
            account_name_type voter;
            uint32_t request_id;
            int16_t vote_percent;

            void validate() const {
                FC_ASSERT(vote_percent >= -CHAIN_100_PERCENT);
                FC_ASSERT(vote_percent <= CHAIN_100_PERCENT);
            }

            void get_required_regular_authorities(flat_set<account_name_type> &a) const {
                a.insert(voter);
            }
        };


        struct create_invite_operation : public base_operation {
            account_name_type creator;
            asset balance;
            public_key_type invite_key;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(creator);
            }
        };

        struct claim_invite_balance_operation : public base_operation {
            account_name_type initiator;
            account_name_type receiver;
            string invite_secret;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(initiator);
            }
        };

        struct invite_registration_operation : public base_operation {
            account_name_type initiator;
            account_name_type new_account_name;
            string invite_secret;
            public_key_type new_account_key;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(initiator);
            }
        };

        struct award_operation : public base_operation {
            account_name_type initiator;
            account_name_type receiver;
            uint16_t energy = 0;
            uint64_t custom_sequence = 0;
            string memo;
            vector <beneficiary_route_type> beneficiaries;

            void validate() const;

            void get_required_regular_authorities(flat_set<account_name_type> &a) const {
                a.insert(initiator);
            }
        };

        struct set_paid_subscription_operation : public base_operation {
            account_name_type account;
            string url;
            uint16_t levels;
            asset amount;
            uint16_t period;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(account);
            }
        };

        struct paid_subscribe_operation : public base_operation {
        	account_name_type subscriber;
            account_name_type account;
            uint16_t level;
            asset amount;
            uint16_t period;
            bool auto_renewal = true;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(subscriber);
            }
        };

        struct set_account_price_operation : public base_operation {
            account_name_type account;
            account_name_type account_seller;
            asset account_offer_price;
            bool account_on_sale;

            void validate() const;

            void get_required_master_authorities(flat_set<account_name_type> &a) const {
                a.insert(account);
            }
        };

        struct set_subaccount_price_operation : public base_operation {
            account_name_type account;
            account_name_type subaccount_seller;
            asset subaccount_offer_price;
            bool subaccount_on_sale;

            void validate() const;

            void get_required_master_authorities(flat_set<account_name_type> &a) const {
                a.insert(account);
            }
        };

        struct buy_account_operation : public base_operation {
        	account_name_type buyer;
            account_name_type account;
            asset account_offer_price;
            public_key_type account_authorities_key;
            asset tokens_to_shares;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(buyer);
            }
        };

        struct use_invite_balance_operation : public base_operation {
            account_name_type initiator;
            account_name_type receiver;
            string invite_secret;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(initiator);
            }
        };

        struct fixed_award_operation : public base_operation {
            account_name_type initiator;
            account_name_type receiver;
            asset reward_amount;
            uint16_t max_energy = 0;
            uint64_t custom_sequence = 0;
            string memo;
            vector <beneficiary_route_type> beneficiaries;

            void validate() const;

            void get_required_regular_authorities(flat_set<account_name_type> &a) const {
                a.insert(initiator);
            }
        };

        struct target_account_sale_operation : public base_operation {
            account_name_type account;
            account_name_type account_seller;
            account_name_type target_buyer;
            asset account_offer_price;
            bool account_on_sale;

            void validate() const;

            void get_required_master_authorities(flat_set<account_name_type> &a) const {
                a.insert(account);
            }
        };

        // HF13: Validator reward sharing
        /**
         *  Validators set the fraction of their block rewards to share with stakeholders
         *  (accounts that voted for them).  At each distribution epoch end the accumulated
         *  stakeholder reward pool is split proportionally by time-weighted vote weight.
         */
        struct set_reward_sharing_operation : public base_operation {
            account_name_type owner;

            /// Fraction of block reward forwarded to stakeholders, in basis points (0 = none, 10000 = 100%).
            uint16_t sharing_rate = 0;

            void validate() const;

            void get_required_active_authorities(flat_set<account_name_type> &a) const {
                a.insert(owner);
            }
        };
} } // graphene::protocol


FC_REFLECT(
    (graphene::protocol::chain_properties_init),
    (account_creation_fee)(maximum_block_size)
    (create_account_delegation_ratio)
    (create_account_delegation_time)(min_delegation)
    (min_curation_percent)(max_curation_percent)
    (bandwidth_reserve_percent)(bandwidth_reserve_below)
    (flag_energy_additional_cost)(vote_accounting_min_rshares)
    (committee_request_approve_min_percent))
FC_REFLECT_DERIVED(
    (graphene::protocol::chain_properties_hf4),((graphene::protocol::chain_properties_init)),
    (inflation_validator_percent)(inflation_ratio_committee_vs_reward_fund)(inflation_recalc_period))
FC_REFLECT_DERIVED(
    (graphene::protocol::chain_properties_hf6),((graphene::protocol::chain_properties_hf4)),
    (data_operations_cost_additional_bandwidth)(validator_miss_penalty_percent)(validator_miss_penalty_duration))
FC_REFLECT_DERIVED(
    (graphene::protocol::chain_properties_hf9),((graphene::protocol::chain_properties_hf6)),
    (create_invite_min_balance)(committee_create_request_fee)(create_paid_subscription_fee)(account_on_sale_fee)(subaccount_on_sale_fee)(validator_declaration_fee)(withdraw_intervals))
FC_REFLECT_DERIVED(
    (graphene::protocol::chain_properties_hf13),((graphene::protocol::chain_properties_hf9)),
    (distribution_epoch_length))

FC_REFLECT_TYPENAME((graphene::protocol::versioned_chain_properties))

FC_REFLECT((graphene::protocol::account_create_operation),
    (fee)(delegation)(creator)(new_account_name)(master)(active)(regular)(memo_key)(json_metadata)(referrer)(extensions));

FC_REFLECT((graphene::protocol::account_update_operation),
        (account)
                (master)
                (active)
                (regular)
                (memo_key)
                (json_metadata))

FC_REFLECT((graphene::protocol::account_metadata_operation), (account)(json_metadata))

FC_REFLECT((graphene::protocol::transfer_operation), (from)(to)(amount)(memo))
FC_REFLECT((graphene::protocol::transfer_to_vesting_operation), (from)(to)(amount))
FC_REFLECT((graphene::protocol::withdraw_vesting_operation), (account)(vesting_shares))
FC_REFLECT((graphene::protocol::set_withdraw_vesting_route_operation), (from_account)(to_account)(percent)(auto_vest))
FC_REFLECT((graphene::protocol::validator_update_operation), (owner)(url)(block_signing_key))
FC_REFLECT((graphene::protocol::account_validator_vote_operation), (account)(validator)(approve))
FC_REFLECT((graphene::protocol::account_validator_proxy_operation), (account)(proxy))
FC_REFLECT((graphene::protocol::content_operation), (parent_author)(parent_permlink)(author)(permlink)(title)(body)(curation_percent)(json_metadata)(extensions))
FC_REFLECT((graphene::protocol::vote_operation), (voter)(author)(permlink)(weight))
FC_REFLECT((graphene::protocol::custom_operation), (required_active_auths)(required_regular_auths)(id)(json))

FC_REFLECT((graphene::protocol::delete_content_operation), (author)(permlink));

FC_REFLECT((graphene::protocol::beneficiary_route_type), (account)(weight))
FC_REFLECT((graphene::protocol::content_payout_beneficiaries), (beneficiaries));
FC_REFLECT_TYPENAME((graphene::protocol::content_extension));

FC_REFLECT((graphene::protocol::escrow_transfer_operation), (from)(to)(token_amount)(escrow_id)(agent)(fee)(json_metadata)(ratification_deadline)(escrow_expiration));
FC_REFLECT((graphene::protocol::escrow_approve_operation), (from)(to)(agent)(who)(escrow_id)(approve));
FC_REFLECT((graphene::protocol::escrow_dispute_operation), (from)(to)(agent)(who)(escrow_id));
FC_REFLECT((graphene::protocol::escrow_release_operation), (from)(to)(agent)(who)(receiver)(escrow_id)(token_amount));
FC_REFLECT((graphene::protocol::request_account_recovery_operation), (recovery_account)(account_to_recover)(new_master_authority)(extensions));
FC_REFLECT((graphene::protocol::recover_account_operation), (account_to_recover)(new_master_authority)(recent_master_authority)(extensions));
FC_REFLECT((graphene::protocol::change_recovery_account_operation), (account_to_recover)(new_recovery_account)(extensions));
FC_REFLECT((graphene::protocol::delegate_vesting_shares_operation), (delegator)(delegatee)(vesting_shares));
FC_REFLECT((graphene::protocol::chain_properties_update_operation), (owner)(props));
FC_REFLECT((graphene::protocol::committee_worker_create_request_operation), (creator)(url)(worker)(required_amount_min)(required_amount_max)(duration));
FC_REFLECT((graphene::protocol::committee_worker_cancel_request_operation), (creator)(request_id));
FC_REFLECT((graphene::protocol::committee_vote_request_operation), (voter)(request_id)(vote_percent));
FC_REFLECT((graphene::protocol::create_invite_operation), (creator)(balance)(invite_key));
FC_REFLECT((graphene::protocol::claim_invite_balance_operation), (initiator)(receiver)(invite_secret));
FC_REFLECT((graphene::protocol::invite_registration_operation), (initiator)(new_account_name)(invite_secret)(new_account_key));
FC_REFLECT((graphene::protocol::versioned_chain_properties_update_operation), (owner)(props));
FC_REFLECT((graphene::protocol::award_operation), (initiator)(receiver)(energy)(custom_sequence)(memo)(beneficiaries));
FC_REFLECT((graphene::protocol::set_paid_subscription_operation), (account)(url)(levels)(amount)(period));
FC_REFLECT((graphene::protocol::paid_subscribe_operation), (subscriber)(account)(level)(amount)(period)(auto_renewal));
FC_REFLECT((graphene::protocol::set_account_price_operation), (account)(account_seller)(account_offer_price)(account_on_sale));
FC_REFLECT((graphene::protocol::set_subaccount_price_operation), (account)(subaccount_seller)(subaccount_offer_price)(subaccount_on_sale));
FC_REFLECT((graphene::protocol::buy_account_operation), (buyer)(account)(account_offer_price)(account_authorities_key)(tokens_to_shares));
FC_REFLECT((graphene::protocol::use_invite_balance_operation), (initiator)(receiver)(invite_secret));
FC_REFLECT((graphene::protocol::fixed_award_operation), (initiator)(receiver)(reward_amount)(max_energy)(custom_sequence)(memo)(beneficiaries));
FC_REFLECT((graphene::protocol::target_account_sale_operation), (account)(account_seller)(target_buyer)(account_offer_price)(account_on_sale));
// HF13
FC_REFLECT((graphene::protocol::set_reward_sharing_operation), (owner)(sharing_rate));
