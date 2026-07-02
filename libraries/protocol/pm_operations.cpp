#include <graphene/protocol/pm_operations.hpp>

namespace graphene { namespace protocol {

        // Local validation helpers (mirror chain_operations.cpp conventions; kept TU-local).
        inline void pm_validate_account_name(const string& name) {
            FC_ASSERT(is_valid_account_name(name), "Account name ${n} is invalid", ("n", name));
        }
        inline bool pm_is_token(const asset& a) { return a.symbol == TOKEN_SYMBOL; }
        inline void pm_validate_utf8_cap(const string& s, size_t cap, const char* field) {
            FC_ASSERT(s.size() <= cap, "${f} exceeds byte cap ${c}", ("f", field)("c", cap));
            FC_ASSERT(fc::is_utf8(s), "${f} not formatted in UTF8", ("f", field));
        }

        void pm_oracle_register_operation::validate() const {
            pm_validate_account_name(owner);
            FC_ASSERT(pm_is_token(insurance), "insurance must be VIZ (TOKEN_SYMBOL)");
            FC_ASSERT(insurance.amount > 0, "insurance must be positive");
            FC_ASSERT(pm_is_token(fixed_fee), "fixed_fee must be VIZ (TOKEN_SYMBOL)");
            FC_ASSERT(fixed_fee.amount >= 0, "fixed_fee cannot be negative");
            FC_ASSERT(fee_percent <= 10000, "fee_percent out of range (bp, max 10000)");
            pm_validate_utf8_cap(rules_url, MAX_PM_PROFILE_URL_LEN, "rules_url");
        }

        void pm_oracle_update_operation::validate() const {
            pm_validate_account_name(owner);
            FC_ASSERT(insurance_delta || fee_percent || fixed_fee || rules_url,
                "pm_oracle_update must change at least one field");
            if (insurance_delta) FC_ASSERT(pm_is_token(*insurance_delta), "insurance_delta must be VIZ");
            if (fixed_fee) {
                FC_ASSERT(pm_is_token(*fixed_fee), "fixed_fee must be VIZ");
                FC_ASSERT(fixed_fee->amount >= 0, "fixed_fee cannot be negative");
            }
            if (fee_percent) FC_ASSERT(*fee_percent <= 10000, "fee_percent out of range (bp, max 10000)");
            if (rules_url) pm_validate_utf8_cap(*rules_url, MAX_PM_PROFILE_URL_LEN, "rules_url");
        }

        void pm_create_market_operation::validate() const {
            pm_validate_account_name(creator);
            pm_validate_account_name(oracle);
            FC_ASSERT(market_type == 0 || market_type == 1, "market_type must be 0 (binary) or 1 (multi)");
            if (market_type == 0) {
                FC_ASSERT(outcomes.size() == 2, "binary market must have exactly 2 outcomes");
                FC_ASSERT(lmsr_b == 0, "lmsr_b must be 0 for binary markets");
            } else {
                FC_ASSERT(outcomes.size() >= 3 && outcomes.size() <= MAX_PM_OUTCOMES_PER_MARKET,
                    "multi market must have 3..${m} outcomes", ("m", MAX_PM_OUTCOMES_PER_MARKET));
                FC_ASSERT(lmsr_b > 0, "lmsr_b must be positive for multi markets");
                FC_ASSERT(allow_instant_bet, "multi markets must keep allow_instant_bet=true (no LMSR batch yet)");
            }
            for (const auto& label : outcomes) {
                FC_ASSERT(label.size() > 0, "outcome label cannot be empty");
                pm_validate_utf8_cap(label, MAX_PM_OUTCOME_LABEL_LEN, "outcome label");
            }
            pm_validate_utf8_cap(url, MAX_PM_MARKET_TITLE_LEN, "url");
            FC_ASSERT(pm_is_token(liquidity), "liquidity must be VIZ (TOKEN_SYMBOL)");
            FC_ASSERT(liquidity.amount > 0, "liquidity must be positive");
            FC_ASSERT(result_expiration > betting_expiration, "result_expiration must be after betting_expiration");
            // All percents are bp (10000 = 100.00%). The oracle fee here is the OFFER CEILING; the
            // worst-case sum (offer + creator + liquidity) must stay solvent (<= 100%), so the
            // winners' pool can never go negative regardless of the oracle's later quote.
            FC_ASSERT(oracle_fee_percent <= 10000 && creator_fee_percent <= 10000 && liquidity_fee_percent <= 10000,
                "individual fee_percent out of range (bp, max 10000)");
            FC_ASSERT(uint32_t(oracle_fee_percent) + creator_fee_percent + liquidity_fee_percent <= 10000,
                "sum of fees exceeds 100% (10000 bp)");
            FC_ASSERT(pm_is_token(oracle_fixed_fee), "oracle_fixed_fee must be VIZ (TOKEN_SYMBOL)");
            FC_ASSERT(oracle_fixed_fee.amount >= 0, "oracle_fixed_fee cannot be negative");
            FC_ASSERT(endogeneity_tier >= 1 && endogeneity_tier <= 3, "endogeneity_tier must be 1..3");
            FC_ASSERT(dispute_penalty_percent >= -10000 && dispute_penalty_percent <= 10000,
                "dispute_penalty_percent must be in [-10000, 10000]");
            FC_ASSERT(dispute_mode == 0 || dispute_mode == 1, "dispute_mode must be 0 (committee) or 1 (account)");
            FC_ASSERT(allow_instant_bet || allow_batch, "market would be unbettable: need allow_instant_bet or allow_batch");
            if (dispute_mode == 1) {
                pm_validate_account_name(dispute_resolver);
                // Static anti-self-judging guard (threat-model §4.11); also re-checked statefully in evaluator.
                FC_ASSERT(dispute_resolver != oracle, "dispute_resolver must not be the oracle");
                FC_ASSERT(dispute_resolver != creator, "dispute_resolver must not be the creator");
            } else {
                FC_ASSERT(dispute_resolver == account_name_type(), "dispute_resolver must be empty for committee mode");
            }
        }

        void pm_oracle_accept_market_operation::validate() const {
            pm_validate_account_name(oracle);
            FC_ASSERT(market_id >= 0, "market_id must be non-negative");
            FC_ASSERT(oracle_fee_percent <= 10000, "oracle_fee_percent out of range (bp, max 10000)");
            FC_ASSERT(pm_is_token(oracle_fixed_fee), "oracle_fixed_fee must be VIZ (TOKEN_SYMBOL)");
            FC_ASSERT(oracle_fixed_fee.amount >= 0, "oracle_fixed_fee cannot be negative");
        }

        void pm_place_bet_operation::validate() const {
            pm_validate_account_name(account);
            FC_ASSERT(market_id >= 0, "market_id must be non-negative");
            FC_ASSERT(pm_is_token(amount), "amount must be VIZ (TOKEN_SYMBOL)");
            FC_ASSERT(amount.amount > 0, "bet amount must be positive");
            FC_ASSERT(min_tokens >= 0, "min_tokens cannot be negative");
            FC_ASSERT(mode == 0 || mode == 1, "mode must be 0 (instant) or 1 (batch)");
            FC_ASSERT(side >= -1 && side <= 1, "side must be -1, 0 or 1");
            FC_ASSERT(outcome_index >= -1, "outcome_index must be >= -1");
            // Exactly one selector populated: binary uses side (0/1), multi uses outcome_index (>=0).
            FC_ASSERT((side >= 0) != (outcome_index >= 0), "set exactly one of side / outcome_index");
        }

        void pm_commit_bet_operation::validate() const {
            pm_validate_account_name(account);
            FC_ASSERT(market_id >= 0, "market_id must be non-negative");
            FC_ASSERT(commitment != fc::sha256(), "commitment cannot be empty");
            FC_ASSERT(pm_is_token(escrow_amount), "escrow_amount must be VIZ (TOKEN_SYMBOL)");
            FC_ASSERT(escrow_amount.amount > 0, "escrow_amount must be positive");
            FC_ASSERT(no_reveal_fee_percent <= 10000, "no_reveal_fee_percent out of range (bp, max 10000)");
        }

        void pm_reveal_bet_operation::validate() const {
            pm_validate_account_name(account);
            FC_ASSERT(commit_id >= 0, "commit_id must be non-negative");
            FC_ASSERT(pm_is_token(amount), "amount must be VIZ (TOKEN_SYMBOL)");
            FC_ASSERT(amount.amount > 0, "reveal amount must be positive");
            FC_ASSERT(min_tokens >= 0, "min_tokens cannot be negative");
            FC_ASSERT(side >= -1 && side <= 1, "side must be -1, 0 or 1");
            FC_ASSERT(outcome_index >= -1, "outcome_index must be >= -1");
            FC_ASSERT((side >= 0) != (outcome_index >= 0), "set exactly one of side / outcome_index");
            pm_validate_utf8_cap(salt, MAX_PM_PROFILE_URL_LEN, "salt");
        }

        void pm_cancel_bet_operation::validate() const {
            pm_validate_account_name(account);
            FC_ASSERT(bet_id >= 0, "bet_id must be non-negative");
            FC_ASSERT(min_return >= 0, "min_return cannot be negative");
        }

        void pm_add_liquidity_operation::validate() const {
            pm_validate_account_name(provider);
            FC_ASSERT(market_id >= 0, "market_id must be non-negative");
            FC_ASSERT(pm_is_token(amount), "amount must be VIZ (TOKEN_SYMBOL)");
            FC_ASSERT(amount.amount > 0, "liquidity amount must be positive");
        }

        void pm_withdraw_liquidity_operation::validate() const {
            pm_validate_account_name(provider);
            FC_ASSERT(liquidity_id >= 0, "liquidity_id must be non-negative");
            FC_ASSERT(pm_is_token(amount), "amount must be VIZ (TOKEN_SYMBOL)");
            FC_ASSERT(amount.amount >= 0, "amount cannot be negative (0 = full position)");
        }

        void pm_resolve_market_operation::validate() const {
            pm_validate_account_name(oracle);
            FC_ASSERT(market_id >= 0, "market_id must be non-negative");
            FC_ASSERT(winning_outcome >= -1, "winning_outcome must be >= -1");
            pm_validate_utf8_cap(decision_url, MAX_PM_DECISION_URL_LEN, "decision_url");
            pm_validate_utf8_cap(decision_reason, MAX_PM_DISPUTE_REASON_LEN, "decision_reason");
        }

        void pm_no_contest_operation::validate() const {
            pm_validate_account_name(oracle);
            FC_ASSERT(market_id >= 0, "market_id must be non-negative");
            pm_validate_utf8_cap(reason, MAX_PM_DISPUTE_REASON_LEN, "reason");
        }

        void pm_dispute_create_operation::validate() const {
            pm_validate_account_name(disputer);
            FC_ASSERT(market_id >= 0, "market_id must be non-negative");
            FC_ASSERT(proposed_outcome >= -1, "proposed_outcome must be >= -1");
            pm_validate_utf8_cap(reason, MAX_PM_DISPUTE_REASON_LEN, "reason");
        }

        void pm_dispute_vote_operation::validate() const {
            pm_validate_account_name(voter);
            FC_ASSERT(market_id >= 0, "market_id must be non-negative");
            FC_ASSERT(vote_outcome >= -1, "vote_outcome must be >= -1");
            FC_ASSERT(vote_percent >= -10000 && vote_percent <= 10000, "vote_percent must be in [-10000, 10000]");
        }

        void pm_dispute_resolve_operation::validate() const {
            pm_validate_account_name(resolver);
            FC_ASSERT(market_id >= 0, "market_id must be non-negative");
            FC_ASSERT(correct_outcome >= -1, "correct_outcome must be >= -1");
            FC_ASSERT(pm_is_token(penalty_amount), "penalty_amount must be VIZ (TOKEN_SYMBOL)");
            FC_ASSERT(penalty_amount.amount >= 0, "penalty_amount cannot be negative");
        }

        void pm_transfer_position_operation::validate() const {
            pm_validate_account_name(from);
            pm_validate_account_name(to);
            FC_ASSERT(from != to, "cannot transfer a position to yourself");
            FC_ASSERT(bet_id >= 0, "bet_id must be non-negative");
            FC_ASSERT(amount >= 0, "amount cannot be negative (0 = full position)");
            pm_validate_utf8_cap(memo, MAX_PM_DISPUTE_REASON_LEN, "memo");
        }

        void pm_lazy_deposit_operation::validate() const {
            pm_validate_account_name(account);
            FC_ASSERT(pm_is_token(amount), "amount must be VIZ (TOKEN_SYMBOL)");
            FC_ASSERT(amount.amount > 0, "deposit amount must be positive");
        }

        void pm_lazy_withdraw_operation::validate() const {
            pm_validate_account_name(account);
            FC_ASSERT(shares >= 0, "shares cannot be negative (0 = all)");
        }

        void pm_leverage_open_operation::validate() const {
            pm_validate_account_name(account);
            FC_ASSERT(outcome_index == 0 || outcome_index == 1, "leverage is CPMM-binary only (outcome 0/1)");
            FC_ASSERT(collateral.symbol == TOKEN_SYMBOL, "collateral must be VIZ");
            FC_ASSERT(loan.symbol == TOKEN_SYMBOL, "loan must be VIZ");
            FC_ASSERT(collateral.amount > 0, "collateral must be positive");
            FC_ASSERT(loan.amount > 0, "loan must be positive");
            FC_ASSERT(min_tokens >= 0, "min_tokens cannot be negative");
            FC_ASSERT(max_slippage_percent <= 100, "max_slippage_percent out of range");
        }

        void pm_leverage_close_operation::validate() const {
            pm_validate_account_name(account);
            FC_ASSERT(min_return >= 0, "min_return cannot be negative");
        }

        void pm_leverage_convert_operation::validate() const {
            pm_validate_account_name(account);
            FC_ASSERT(conversion_profit_cost <= 100, "conversion_profit_cost out of range");
        }

        void pm_dispute_oracle_respond_operation::validate() const {
            pm_validate_account_name(oracle);
            FC_ASSERT(market_id >= 0, "market_id must be non-negative");
            FC_ASSERT(response.size() > 0, "response cannot be empty");
            pm_validate_utf8_cap(response, MAX_PM_DISPUTE_REASON_LEN, "response");
        }

        void pm_unban_operation::validate() const {
            pm_validate_account_name(resolver);
            pm_validate_account_name(target);
            FC_ASSERT(unban_oracle || unban_creator, "nothing to unban: set unban_oracle and/or unban_creator");
        }

} } // graphene::protocol
