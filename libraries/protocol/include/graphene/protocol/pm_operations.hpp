#pragma once

#include <graphene/protocol/base.hpp>
#include <graphene/protocol/block_header.hpp>
#include <graphene/protocol/asset.hpp>

#include <fc/crypto/sha256.hpp>
#include <fc/utf8.hpp>

namespace graphene { namespace protocol {

        // HF14 Prediction Markets (Onix). All financial params are `asset` (VIZ, 3 decimals);
        // stored internally as share_type. Object references are passed as plain int64 ids that
        // the evaluator resolves to the corresponding pm_* chainbase object.
        //
        // Op-id NOTE: these are appended to the single `operation` static_variant (see operations.hpp);
        // the variant index IS the consensus op-id. The spec's "ids start at 64" numbering does NOT
        // apply to VIZ and is intentionally ignored.

        using pm_object_id_type = int64_t;

        /// 1. Register a new oracle with a bonded insurance deposit.
        struct pm_oracle_register_operation : public base_operation {
            account_name_type owner;
            asset             insurance;        ///< VIZ, locked from owner; >= pm_min_oracle_insurance
            uint16_t          fee_percent = 0; ///< <= pm_max_oracle_fee_percent
            asset             fixed_fee;         ///< VIZ, per-market fixed fee (>= 0)
            string            rules_url;         ///< <= MAX_PM_PROFILE_URL_LEN
            // Auto-accept policy (anti-collusion). When `auto_accept`, a new market naming this oracle is
            // accepted at creation only if its creator matches `auto_accept_creator` (empty = any) and its
            // dispute routing matches `auto_accept_resolver` (empty = committee-mode only; set = only
            // account-mode markets whose `dispute_resolver` equals it) — otherwise it stays pending for
            // manual accept. Prevents a creator from slipping in a colluding sham resolver.
            account_name_type auto_accept_creator;
            account_name_type auto_accept_resolver;
            bool              auto_accept = false;

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(owner); }
        };

        /// 2. Top-up / withdraw insurance, change fee policy or rules url.
        struct pm_oracle_update_operation : public base_operation {
            account_name_type   owner;
            optional<asset>     insurance_delta;     ///< signed: >0 top-up, <0 withdraw (blocked while active markets / below min)
            optional<uint16_t>  fee_percent;
            optional<asset>     fixed_fee;
            optional<string>    rules_url;
            optional<account_name_type> auto_accept_creator;   ///< auto-accept policy (see register op)
            optional<account_name_type> auto_accept_resolver;
            optional<bool>              auto_accept;

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(owner); }
        };

        /// 3. Create a market (binary CPMM or multi LMSR). Creator becomes the first LP.
        struct pm_create_market_operation : public base_operation {
            account_name_type     creator;
            account_name_type     oracle;            ///< registered oracle (or creator for self-oracle)
            uint8_t               market_type = 0;   ///< 0 binary (CPMM), 1 multi (LMSR)
            vector<string>        outcomes;          ///< size 2 (binary) or 3..pm_max_outcomes (multi)
            string                url;               ///< resolution criteria, <= MAX_PM_MARKET_TITLE_LEN
            // Oracle terms here are the creator's OFFER CEILING (max the maker will pay the oracle).
            // The oracle locks in its actual quote (<= these) at accept; a self-oracle freezes them
            // as-is at creation. All percents are bp (10000 = 100.00%).
            uint16_t              oracle_fee_percent = 0;   ///< offered max oracle % of losers' pool
            asset                 oracle_fixed_fee;         ///< offered max oracle fixed fee (VIZ, >= 0)
            uint16_t              creator_fee_percent = 0;
            uint16_t              liquidity_fee_percent = 0;
            asset                 liquidity;         ///< VIZ seed; >= pm_min_liquidity
            share_type            lmsr_b = 0;        ///< multi only: client-computed b; node checks floor(liquidity/ln_q(N))==b (lmsr-spec §7.7)
            time_point_sec        betting_expiration;
            time_point_sec        result_expiration; ///< > betting_expiration; <= now + pm_max_market_duration
            uint8_t               time_penalty_type = 0;
            uint32_t              time_penalty_value = 0;
            uint8_t               penalty_curve_type = 0;
            bool                  allow_early_resolution = false;
            bool                  allow_cancellation = false;
            bool                  allow_batch = false;
            bool                  allow_instant_bet = true; ///< multi forces true (no LMSR batch yet); see evaluator
            uint8_t               endogeneity_tier = 2;     ///< 1 econ-data / 2 sports / 3 political
            uint8_t               dispute_mode = 0;         ///< 0 committee / 1 account
            account_name_type     dispute_resolver;         ///< required & must exist iff dispute_mode==1; must NOT equal oracle/creator
            int16_t               dispute_penalty_percent = 0; ///< −10000..+10000 oracle penalty policy on a
                                                               ///< successful dispute (>0 slash, <0 good-faith bonus)
            string                metadata;          ///< free-form client JSON (no length cap, like custom_op);
                                                     ///< consensus-opaque, parsed off-chain by the meta plugin

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(creator); }
        };

        /// 4. Oracle accepts or rejects a pending market. On accept the oracle quotes its actual
        /// terms (<= the creator's offered ceiling and <= the median cap); these are frozen onto
        /// the market and a pm_market_accepted virtual op is emitted. Quote fields are ignored on
        /// reject and for self-oracle markets (which auto-accept at creation).
        struct pm_oracle_accept_market_operation : public base_operation {
            account_name_type   oracle;
            pm_object_id_type   market_id = 0;
            bool                accept = true;
            uint16_t            oracle_fee_percent = 0; ///< oracle's quoted % (bp); <= market offer & median cap
            asset               oracle_fixed_fee;       ///< oracle's quoted fixed fee; <= market offer

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(oracle); }
        };

        /// 5. Place a bet (instant or batch).
        struct pm_place_bet_operation : public base_operation {
            account_name_type   account;
            pm_object_id_type   market_id = 0;
            int8_t              side = -1;          ///< binary: 0/1; multi: -1
            int16_t             outcome_index = -1; ///< multi: 0..N-1; binary: -1
            asset               amount;             ///< VIZ, > 0
            share_type          min_tokens = 0;     ///< slippage floor (0 = none)
            uint8_t             mode = 0;           ///< 0 instant, 1 batch

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(account); }
        };

        /// 6. Commit-reveal phase 1: hidden bet commitment.
        struct pm_commit_bet_operation : public base_operation {
            account_name_type   account;
            pm_object_id_type   market_id = 0;
            fc::sha256          commitment;            ///< H(market||account||side/outcome||amount||min_tokens||salt)
            asset               escrow_amount;         ///< VIZ, locked; >= pm_min_batch_bet
            uint16_t            no_reveal_fee_percent = 0; ///< MUST equal median(pm_commit_no_reveal_penalty_percent) — consensus check

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(account); }
        };

        /// 7. Commit-reveal phase 2: reveal & enqueue for the next batch epoch.
        struct pm_reveal_bet_operation : public base_operation {
            account_name_type   account;
            pm_object_id_type   commit_id = 0;
            int8_t              side = -1;
            int16_t             outcome_index = -1;
            asset               amount;             ///< <= escrow_amount; surplus refunded
            string              salt;               ///< entropy bound into the commitment hash
            share_type          min_tokens = 0;

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(account); }
        };

        /// 8. Cancel an open / queued bet (requires allow_cancellation).
        struct pm_cancel_bet_operation : public base_operation {
            account_name_type   account;
            pm_object_id_type   bet_id = 0;
            share_type          min_return = 0;     ///< slippage floor on the refund

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(account); }
        };

        /// 9. Add liquidity to a market.
        struct pm_add_liquidity_operation : public base_operation {
            account_name_type   provider;
            pm_object_id_type   market_id = 0;
            asset               amount;             ///< VIZ, > 0

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(provider); }
        };

        /// 10. Withdraw liquidity (principal-safe; locked after betting_expiration until resolution).
        struct pm_withdraw_liquidity_operation : public base_operation {
            account_name_type   provider;
            pm_object_id_type   liquidity_id = 0;
            asset               amount;             ///< VIZ to withdraw; 0 = full position

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(provider); }
        };

        /// 11. Oracle resolves the market to a winning outcome.
        struct pm_resolve_market_operation : public base_operation {
            account_name_type   oracle;
            pm_object_id_type   market_id = 0;
            int16_t             winning_outcome = -1;
            string              decision_url;       ///< <= MAX_PM_DECISION_URL_LEN

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(oracle); }
        };

        /// 12. Oracle declares the market void (refund all; disputable).
        struct pm_no_contest_operation : public base_operation {
            account_name_type   oracle;
            pm_object_id_type   market_id = 0;
            string              reason;             ///< <= MAX_PM_DISPUTE_REASON_LEN

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(oracle); }
        };

        /// 13. File a dispute against a resolution (escrows pm_dispute_fee).
        struct pm_dispute_create_operation : public base_operation {
            account_name_type   disputer;
            pm_object_id_type   market_id = 0;
            int16_t             proposed_outcome = -1;
            string              reason;             ///< <= MAX_PM_DISPUTE_REASON_LEN

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(disputer); }
        };

        /// 14. Committee-mode dispute vote (regular auth, like committee_vote_request).
        struct pm_dispute_vote_operation : public base_operation {
            account_name_type   voter;
            pm_object_id_type   market_id = 0;
            int16_t             vote_outcome = -1;  ///< correct outcome, or -1 to uphold oracle
            int16_t             vote_percent = 0;   ///< conviction/penalty intensity [-10000, 10000]

            extensions_type extensions;

            void validate() const;
            void get_required_regular_authorities(flat_set<account_name_type>& a) const { a.insert(voter); }
        };

        /// 15. Account-mode dispute verdict by the market's configured resolver.
        struct pm_dispute_resolve_operation : public base_operation {
            account_name_type   resolver;
            pm_object_id_type   market_id = 0;
            int16_t             correct_outcome = -1;
            asset               penalty_amount;     ///< insurance to slash
            bool                ban_oracle = false;
            time_point_sec      ban_oracle_until;
            bool                ban_creator = false;
            time_point_sec      ban_creator_until;

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(resolver); }
        };

        /// 16. Transfer all/part of a bet's weight to another account (no market impact).
        struct pm_transfer_position_operation : public base_operation {
            account_name_type   from;
            pm_object_id_type   bet_id = 0;
            account_name_type   to;
            share_type          amount = 0;         ///< weight to reassign (0 = full)
            string              memo;               ///< plaintext or '#'-prefixed ECIES (same as VIZ memos)

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(from); }
        };

        /// 17. Deposit into the lazy liquidity pool (allocation-only; no leverage in HF14).
        struct pm_lazy_deposit_operation : public base_operation {
            account_name_type   account;
            asset               amount;             ///< VIZ, > 0

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(account); }
        };

        /// 18. Withdraw from the lazy liquidity pool (planned or emergency w/ penalty).
        struct pm_lazy_withdraw_operation : public base_operation {
            account_name_type   account;
            share_type          shares = 0;         ///< pool shares to burn (0 = all)
            bool                emergency = false;  ///< true => penalty on locked-share profit

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(account); }
        };

        /// 19. Open a leveraged (boosted) CPMM position: collateral C + pool loan L.
        struct pm_leverage_open_operation : public base_operation {
            account_name_type   account;
            pm_object_id_type   market_id = 0;
            int16_t             outcome_index = 0;  ///< binary: 0/1
            asset               collateral;         ///< bettor's own stake, VIZ
            asset               loan;               ///< pool loan, VIZ
            share_type          min_tokens = 0;     ///< slippage floor (consensus check)
            uint16_t            max_slippage_percent = 0; ///< user-facing front-run guard

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(account); }
        };

        /// 20. Voluntarily close a leveraged position (only if cancel_value >= threshold).
        struct pm_leverage_close_operation : public base_operation {
            account_name_type   account;
            pm_object_id_type   position_id = 0;
            share_type          min_return = 0;     ///< slippage floor on the bettor's return

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(account); }
        };

        /// 21. Convert a leveraged position to a normal bet (pay off the loan + fee).
        struct pm_leverage_convert_operation : public base_operation {
            account_name_type   account;
            pm_object_id_type   position_id = 0;
            uint16_t            conversion_profit_cost = 0; ///< MUST equal median(pm_conversion_profit_cost_percent)

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(account); }
        };

} } // graphene::protocol

FC_REFLECT((graphene::protocol::pm_oracle_register_operation),
    (owner)(insurance)(fee_percent)(fixed_fee)(rules_url)
    (auto_accept_creator)(auto_accept_resolver)(auto_accept)(extensions))
FC_REFLECT((graphene::protocol::pm_oracle_update_operation),
    (owner)(insurance_delta)(fee_percent)(fixed_fee)(rules_url)
    (auto_accept_creator)(auto_accept_resolver)(auto_accept)(extensions))
FC_REFLECT((graphene::protocol::pm_create_market_operation),
    (creator)(oracle)(market_type)(outcomes)(url)(oracle_fee_percent)(oracle_fixed_fee)(creator_fee_percent)
    (liquidity_fee_percent)(liquidity)(lmsr_b)(betting_expiration)(result_expiration)
    (time_penalty_type)(time_penalty_value)(penalty_curve_type)(allow_early_resolution)
    (allow_cancellation)(allow_batch)(allow_instant_bet)(endogeneity_tier)(dispute_mode)
    (dispute_resolver)(dispute_penalty_percent)(metadata)(extensions))
FC_REFLECT((graphene::protocol::pm_oracle_accept_market_operation),
    (oracle)(market_id)(accept)(oracle_fee_percent)(oracle_fixed_fee)(extensions))
FC_REFLECT((graphene::protocol::pm_place_bet_operation),
    (account)(market_id)(side)(outcome_index)(amount)(min_tokens)(mode)(extensions))
FC_REFLECT((graphene::protocol::pm_commit_bet_operation),
    (account)(market_id)(commitment)(escrow_amount)(no_reveal_fee_percent)(extensions))
FC_REFLECT((graphene::protocol::pm_reveal_bet_operation),
    (account)(commit_id)(side)(outcome_index)(amount)(salt)(min_tokens)(extensions))
FC_REFLECT((graphene::protocol::pm_cancel_bet_operation),
    (account)(bet_id)(min_return)(extensions))
FC_REFLECT((graphene::protocol::pm_add_liquidity_operation),
    (provider)(market_id)(amount)(extensions))
FC_REFLECT((graphene::protocol::pm_withdraw_liquidity_operation),
    (provider)(liquidity_id)(amount)(extensions))
FC_REFLECT((graphene::protocol::pm_resolve_market_operation),
    (oracle)(market_id)(winning_outcome)(decision_url)(extensions))
FC_REFLECT((graphene::protocol::pm_no_contest_operation),
    (oracle)(market_id)(reason)(extensions))
FC_REFLECT((graphene::protocol::pm_dispute_create_operation),
    (disputer)(market_id)(proposed_outcome)(reason)(extensions))
FC_REFLECT((graphene::protocol::pm_dispute_vote_operation),
    (voter)(market_id)(vote_outcome)(vote_percent)(extensions))
FC_REFLECT((graphene::protocol::pm_dispute_resolve_operation),
    (resolver)(market_id)(correct_outcome)(penalty_amount)(ban_oracle)(ban_oracle_until)
    (ban_creator)(ban_creator_until)(extensions))
FC_REFLECT((graphene::protocol::pm_transfer_position_operation),
    (from)(bet_id)(to)(amount)(memo)(extensions))
FC_REFLECT((graphene::protocol::pm_lazy_deposit_operation),
    (account)(amount)(extensions))
FC_REFLECT((graphene::protocol::pm_lazy_withdraw_operation),
    (account)(shares)(emergency)(extensions))
FC_REFLECT((graphene::protocol::pm_leverage_open_operation),
    (account)(market_id)(outcome_index)(collateral)(loan)(min_tokens)(max_slippage_percent)(extensions))
FC_REFLECT((graphene::protocol::pm_leverage_close_operation),
    (account)(position_id)(min_return)(extensions))
FC_REFLECT((graphene::protocol::pm_leverage_convert_operation),
    (account)(position_id)(conversion_profit_cost)(extensions))
