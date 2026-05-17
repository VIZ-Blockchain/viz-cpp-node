#include <graphene/protocol/chain_operations.hpp>
#include <fc/io/json.hpp>

namespace graphene { namespace protocol {

        /// TODO: after the hardfork, we can rename this method validate_permlink because it is strictily less restrictive than before
        ///  Issue #56 contains the justificiation for allowing any UTF-8 string to serve as a permlink, content will be grouped by tags
        ///  going forward.
        inline void validate_permlink(const string &permlink) {
            FC_ASSERT(permlink.size() <
                      CHAIN_MAX_URL_LENGTH, "permlink is too long");
            FC_ASSERT(fc::is_utf8(permlink), "permlink not formatted in UTF8");
        }

        inline void validate_domain_name( const string& name, const string& creator )
        {
           FC_ASSERT( is_valid_domain_name( name, creator ), "Domain name ${n} is invalid, creator name ${c}", ("n", name)("c", creator) );
        }

        inline void validate_create_account_name(const string &name) {
            FC_ASSERT(is_valid_create_account_name(name), "Account name ${n} is invalid", ("n", name));
        }

        inline void validate_account_name(const string &name) {
            FC_ASSERT(is_valid_account_name(name), "Account name ${n} is invalid", ("n", name));
        }

        inline void validate_json_metadata(const string& json_metadata) {
            if (json_metadata.size() > 0) {
                FC_ASSERT(fc::is_utf8(json_metadata), "JSON Metadata not formatted in UTF8");
                FC_ASSERT(fc::json::is_valid(json_metadata), "JSON Metadata not valid JSON");
            }
        }

        bool inline is_asset_type(asset asset, asset_symbol_type symbol) {
            return asset.symbol == symbol;
        }

        void account_create_operation::validate() const {
            validate_create_account_name(new_account_name);
            validate_account_name(new_account_name);
            validate_account_name(creator);
            validate_domain_name(new_account_name, creator);
            FC_ASSERT(is_asset_type(fee, TOKEN_SYMBOL), "Account creation fee must be TOKEN_SYMBOL");
            FC_ASSERT(is_asset_type(delegation, SHARES_SYMBOL), "Delegation must be SHARES");
            FC_ASSERT(fee.amount >= 0, "Account creation fee cannot be negative");
            FC_ASSERT(delegation.amount >= 0, "Delegation cannot be negative");
            master.validate();
            active.validate();
            regular.validate();
            validate_json_metadata(json_metadata);
        }

        void account_update_operation::validate() const {
            validate_account_name(account);
            /*if( master )
               master->validate();
            if( active )
               active->validate();
            if( regular )
               regular->validate();*/
            validate_json_metadata(json_metadata);
        }

        void account_metadata_operation::validate() const {
            validate_account_name(account);
            FC_ASSERT(json_metadata.size() > 0, "json_metadata can't be empty");
            validate_json_metadata(json_metadata);
        }

        struct content_extension_validate_visitor {
            content_extension_validate_visitor() {
            }

            using result_type = void;

            void operator()( const content_payout_beneficiaries& cpb ) const {
                cpb.validate();
            }
        };

        void content_payout_beneficiaries::validate() const {
            uint32_t sum = 0;

            FC_ASSERT(beneficiaries.size(), "Must specify at least one beneficiary");
            FC_ASSERT(beneficiaries.size() < 128,
                      "Cannot specify more than 127 beneficiaries."); // Require size serialization fits in one byte.

            validate_account_name(beneficiaries[0].account);
            FC_ASSERT(beneficiaries[0].weight <= CHAIN_100_PERCENT,
                      "Cannot allocate more than 100% of rewards to one account");
            sum += beneficiaries[0].weight;
            FC_ASSERT(sum <= CHAIN_100_PERCENT,
                      "Cannot allocate more than 100% of rewards to a content"); // Have to check incrementally to avoid overflow

            for (size_t i = 1; i < beneficiaries.size(); i++) {
                validate_account_name( beneficiaries[i].account);
                FC_ASSERT(beneficiaries[i].weight <= CHAIN_100_PERCENT,
                          "Cannot allocate more than 100% of rewards to one account");
                sum += beneficiaries[i].weight;
                FC_ASSERT(sum <= CHAIN_100_PERCENT,
                          "Cannot allocate more than 100% of rewards to a content"); // Have to check incrementally to avoid overflow
                FC_ASSERT(beneficiaries[i - 1] < beneficiaries[i],
                          "Benficiaries must be specified in sorted order (account ascending)");
            }
        }

        void content_operation::validate() const {
            FC_ASSERT(title.size() < 256, "Title larger than size limit");
            FC_ASSERT(fc::is_utf8(title), "Title not formatted in UTF8");
            FC_ASSERT(body.size() > 0, "Body is empty");
            FC_ASSERT(fc::is_utf8(body), "Body not formatted in UTF8");

            if (parent_author.size()) {
                validate_account_name(parent_author);
            }
            validate_account_name(author);
            validate_permlink(parent_permlink);
            validate_permlink(permlink);

            FC_ASSERT(curation_percent >= 0);
            FC_ASSERT(curation_percent <= CHAIN_100_PERCENT);

            if (json_metadata.size() > 0) {
                FC_ASSERT(fc::json::is_valid(json_metadata), "JSON Metadata not valid JSON");
            }

            for (auto &e : extensions) {
                e.visit(content_extension_validate_visitor());
            }
        }

        void delete_content_operation::validate() const {
            validate_permlink(permlink);
            validate_account_name(author);
        }

        void vote_operation::validate() const {
            validate_account_name(voter);
            validate_account_name(author);\
      FC_ASSERT(abs(weight) <=
                CHAIN_100_PERCENT, "Weight is not a percentage");
            validate_permlink(permlink);
        }

        void transfer_operation::validate() const {
            try {
                validate_account_name(from);
                validate_account_name(to);
                FC_ASSERT(amount.symbol !=
                          SHARES_SYMBOL, "transferring of SHARES is not allowed.");
                FC_ASSERT(amount.amount >
                          0, "Cannot transfer a negative amount (aka: stealing)");
                FC_ASSERT(memo.size() <
                          CHAIN_MAX_MEMO_LENGTH, "Memo is too large");
                FC_ASSERT(fc::is_utf8(memo), "Memo is not UTF8");
            } FC_CAPTURE_AND_RETHROW((*this))
        }

        void transfer_to_vesting_operation::validate() const {
            validate_account_name(from);
            FC_ASSERT(is_asset_type(amount, TOKEN_SYMBOL), "Amount must be TOKEN_SYMBOL");
            if (to != account_name_type()) {
                validate_account_name(to);
            }
            FC_ASSERT(amount >
                      asset(0, TOKEN_SYMBOL), "Must transfer a nonzero amount");
        }

        void withdraw_vesting_operation::validate() const {
            validate_account_name(account);
            FC_ASSERT(is_asset_type(vesting_shares, SHARES_SYMBOL), "Amount must be SHARES");
            FC_ASSERT(vesting_shares.amount >= 0, "Cannot withdraw negative SHARES");
        }

        void set_withdraw_vesting_route_operation::validate() const {
            validate_account_name(from_account);
            validate_account_name(to_account);
            FC_ASSERT(0 <= percent && percent <=
                                      CHAIN_100_PERCENT, "Percent must be valid percent");
        }

        void validator_update_operation::validate() const {
            validate_account_name(owner);
            FC_ASSERT(url.size() > 0, "URL size must be greater than 0");
            FC_ASSERT(url.size() < CHAIN_MAX_WITNESS_URL_LENGTH, "URL size must be lesser than CHAIN_MAX_WITNESS_URL_LENGTH");
            FC_ASSERT(fc::is_utf8(url), "URL is not valid UTF8");
        }

        struct chain_properties_validator {
            using result_type = void;

            template <typename Props>
            void operator()(const Props& p) const {
                p.validate();
            }
        };

        void chain_properties_update_operation::validate() const {
            validate_account_name(owner);
            props.validate();
        }

        void versioned_chain_properties_update_operation::validate() const {
            validate_account_name(owner);
            props.visit(chain_properties_validator());
        }

        void account_validator_vote_operation::validate() const {
            validate_account_name(account);
            validate_account_name(validator);
        }

        void account_validator_proxy_operation::validate() const {
            validate_account_name(account);
            if (proxy.size()) {
                validate_account_name(proxy);
            }
            FC_ASSERT(proxy != account, "Cannot proxy to self");
        }

        void custom_operation::validate() const {
            /// required auth accounts are the ones whose bandwidth is consumed
            FC_ASSERT((required_active_auths.size() + required_regular_auths.size()) >
                      0, "at least one account must be specified");
            FC_ASSERT(id.size() <= 32, "id is too long");
            FC_ASSERT(fc::is_utf8(json), "JSON Metadata not formatted in UTF8");
            FC_ASSERT(fc::json::is_valid(json), "JSON Metadata not valid JSON");
        }

        void escrow_transfer_operation::validate() const {
            validate_account_name(from);
            validate_account_name(to);
            validate_account_name(agent);
            FC_ASSERT(fee.amount >= 0, "fee cannot be negative");
            FC_ASSERT(token_amount.amount >=
                      0, "tokens amount cannot be negative");
            FC_ASSERT(from != agent &&
                      to != agent, "agent must be a third party");
            FC_ASSERT(fee.symbol == TOKEN_SYMBOL, "fee must be TOKEN_SYMBOL");
            FC_ASSERT(token_amount.symbol ==
                      TOKEN_SYMBOL, "amount must be TOKEN_SYMBOL");
            FC_ASSERT(ratification_deadline <
                      escrow_expiration, "ratification deadline must be before escrow expiration");
            validate_json_metadata(json_metadata);
        }

        void escrow_approve_operation::validate() const {
            validate_account_name(from);
            validate_account_name(to);
            validate_account_name(agent);
            validate_account_name(who);
            FC_ASSERT(who == to ||
                      who == agent, "to or agent must approve escrow");
        }

        void escrow_dispute_operation::validate() const {
            validate_account_name(from);
            validate_account_name(to);
            validate_account_name(agent);
            validate_account_name(who);
            FC_ASSERT(who == from || who == to, "who must be from or to");
        }

        void escrow_release_operation::validate() const {
            validate_account_name(from);
            validate_account_name(to);
            validate_account_name(agent);
            validate_account_name(who);
            validate_account_name(receiver);
            FC_ASSERT(who == from || who == to ||
                      who == agent, "who must be from or to or agent");
            FC_ASSERT(receiver == from ||
                      receiver == to, "receiver must be from or to");
            FC_ASSERT(token_amount.amount >=
                      0, "amount cannot be negative");
            FC_ASSERT(token_amount.amount > 0, "escrow must release a non-zero amount");
            FC_ASSERT(token_amount.symbol ==
                      TOKEN_SYMBOL, "amount must be TOKEN_SYMBOL");
        }

        void request_account_recovery_operation::validate() const {
            validate_account_name(recovery_account);
            validate_account_name(account_to_recover);
            new_master_authority.validate();
        }

        void recover_account_operation::validate() const {
            validate_account_name(account_to_recover);
            FC_ASSERT(!(new_master_authority ==
                        recent_master_authority), "Cannot set new master authority to the recent master authority");
            FC_ASSERT(!new_master_authority.is_impossible(), "new master authority cannot be impossible");
            FC_ASSERT(!recent_master_authority.is_impossible(), "recent master authority cannot be impossible");
            FC_ASSERT(new_master_authority.weight_threshold, "new master authority cannot be trivial");
            new_master_authority.validate();
            recent_master_authority.validate();
        }

        void change_recovery_account_operation::validate() const {
            validate_account_name(account_to_recover);
            validate_account_name(new_recovery_account);
        }

        void delegate_vesting_shares_operation::validate() const {
            validate_account_name(delegator);
            validate_account_name(delegatee);
            FC_ASSERT(delegator != delegatee, "You cannot delegate SHARES to yourself");
            FC_ASSERT(is_asset_type(vesting_shares, SHARES_SYMBOL), "Delegation must be SHARES");
            FC_ASSERT(vesting_shares.amount >= 0, "Delegation cannot be negative");
        }

        void create_invite_operation::validate() const {
            validate_account_name(creator);
            FC_ASSERT(is_asset_type(balance, TOKEN_SYMBOL), "Invite creation balance must be TOKEN_SYMBOL");
            FC_ASSERT(balance > asset(0, TOKEN_SYMBOL), "Invite creation balance must be nonzero amount");
        }

        void claim_invite_balance_operation::validate() const {
            validate_account_name(initiator);
            validate_account_name(receiver);
        }

        void invite_registration_operation::validate() const {
            validate_account_name(initiator);
            validate_account_name(new_account_name);
            validate_domain_name(new_account_name, initiator);
        }

        void award_operation::validate() const {
            validate_account_name(initiator);
            validate_account_name(receiver);
            FC_ASSERT(energy >= 0);
            FC_ASSERT(energy <= CHAIN_100_PERCENT);
            FC_ASSERT(custom_sequence >= 0);
            FC_ASSERT(custom_sequence <= uint64_t(std::numeric_limits<int64_t>::max()));
            FC_ASSERT(memo.size() <
                      CHAIN_MAX_MEMO_LENGTH, "Memo is too large");
            FC_ASSERT(fc::is_utf8(memo), "Memo is not UTF8");

            uint32_t sum = 0;

            FC_ASSERT(beneficiaries.size() < 128,
                      "Cannot specify more than 127 beneficiaries."); // Require size serialization fits in one byte.

            for (size_t i = 0; i < beneficiaries.size(); i++) {
                validate_account_name( beneficiaries[i].account);
                FC_ASSERT(beneficiaries[i].weight <= CHAIN_100_PERCENT,
                          "Cannot allocate more than 100% of rewards to one account");
                sum += beneficiaries[i].weight;
                FC_ASSERT(sum <= CHAIN_100_PERCENT,
                          "Cannot allocate more than 100% of rewards to a content"); // Have to check incrementally to avoid overflow
            }
        }

        void set_paid_subscription_operation::validate() const {
            validate_account_name(account);
            FC_ASSERT(url.size() <
                      CHAIN_MAX_MEMO_LENGTH, "URL is too large");
            FC_ASSERT(fc::is_utf8(url), "URL is not UTF8");
            FC_ASSERT(levels >= 0);
            FC_ASSERT(levels <= PAID_SUBSCRIPTION_MAX_LEVEL);
            FC_ASSERT(amount.symbol !=
                      SHARES_SYMBOL, "Amount of SHARES is not allowed.");
            FC_ASSERT(amount.amount >
                      0, "Cannot set subscription with negative or zero amount");
            FC_ASSERT(period > 0, "Cannot set negative or zero period");
            FC_ASSERT(period <= PAID_SUBSCRIPTION_MAX_PERIOD);
        }

        void paid_subscribe_operation::validate() const {
            validate_account_name(subscriber);
            validate_account_name(account);
            FC_ASSERT(level >= 0);
            FC_ASSERT(level <= PAID_SUBSCRIPTION_MAX_LEVEL);
            FC_ASSERT(amount.symbol !=
                      SHARES_SYMBOL, "Amount of SHARES is not allowed.");
            FC_ASSERT(amount.amount >
                      0, "Cannot set subscription with negative or zero amount");
            FC_ASSERT(period > 0, "Cannot set negative or zero period");
            FC_ASSERT(period <= PAID_SUBSCRIPTION_MAX_PERIOD);
        }

        void set_account_price_operation::validate() const {
            validate_account_name(account);
            validate_account_name(account_seller);
            FC_ASSERT(is_asset_type(account_offer_price, TOKEN_SYMBOL), "Account offer price must be TOKEN_SYMBOL");
            FC_ASSERT(account_offer_price.amount > 0, "Cannot set account offer price with negative or zero amount");
        }

        void set_subaccount_price_operation::validate() const {
            validate_account_name(account);
            validate_account_name(subaccount_seller);
            FC_ASSERT(is_asset_type(subaccount_offer_price, TOKEN_SYMBOL), "Subaccount offer price must be TOKEN_SYMBOL");
            FC_ASSERT(subaccount_offer_price.amount > 0, "Cannot set subaccount offer price with negative or zero amount");
        }

        void buy_account_operation::validate() const {
            validate_account_name(buyer);
            validate_account_name(account);
            FC_ASSERT(is_asset_type(account_offer_price, TOKEN_SYMBOL), "Account offer price must be TOKEN_SYMBOL");
            FC_ASSERT(account_offer_price.amount > 0, "Offer price cannot be negative or zero amount");
            FC_ASSERT(is_asset_type(tokens_to_shares, TOKEN_SYMBOL), "Token to shares must be TOKEN_SYMBOL");
            FC_ASSERT(tokens_to_shares.amount > 0, "Token to shares cannot be negative or zero amount");
        }

        void use_invite_balance_operation::validate() const {
            validate_account_name(initiator);
            validate_account_name(receiver);
        }

        void fixed_award_operation::validate() const {
            validate_account_name(initiator);
            validate_account_name(receiver);
            FC_ASSERT(is_asset_type(reward_amount, TOKEN_SYMBOL), "Reward amount must be TOKEN_SYMBOL");
            FC_ASSERT(reward_amount.amount > 0, "Reward amount cannot be negative or zero amount");
            FC_ASSERT(max_energy >= 0);
            FC_ASSERT(max_energy <= CHAIN_100_PERCENT);
            FC_ASSERT(custom_sequence >= 0);
            FC_ASSERT(custom_sequence <= uint64_t(std::numeric_limits<int64_t>::max()));
            FC_ASSERT(memo.size() <
                      CHAIN_MAX_MEMO_LENGTH, "Memo is too large");
            FC_ASSERT(fc::is_utf8(memo), "Memo is not UTF8");

            uint32_t sum = 0;

            FC_ASSERT(beneficiaries.size() < 128,
                      "Cannot specify more than 127 beneficiaries."); // Require size serialization fits in one byte.

            for (size_t i = 0; i < beneficiaries.size(); i++) {
                validate_account_name( beneficiaries[i].account);
                FC_ASSERT(beneficiaries[i].weight <= CHAIN_100_PERCENT,
                          "Cannot allocate more than 100% of rewards to one account");
                sum += beneficiaries[i].weight;
                FC_ASSERT(sum <= CHAIN_100_PERCENT,
                          "Cannot allocate more than 100% of rewards to a content"); // Have to check incrementally to avoid overflow
            }
        }

        void target_account_sale_operation::validate() const {
            validate_account_name(account);
            validate_account_name(account_seller);
            validate_account_name(target_buyer);
            FC_ASSERT(is_asset_type(account_offer_price, TOKEN_SYMBOL), "Account offer price must be TOKEN_SYMBOL");
            FC_ASSERT(account_offer_price.amount > 0, "Cannot set account offer price with negative or zero amount");
        }

        void set_reward_sharing_operation::validate() const {
            validate_account_name(owner);
            FC_ASSERT(sharing_rate <= CHAIN_VALIDATOR_MAX_SHARING_RATE,
                "sharing_rate ${r} exceeds maximum ${max}",
                ("r", sharing_rate)("max", CHAIN_VALIDATOR_MAX_SHARING_RATE));
        }

} } // graphene::protocol
