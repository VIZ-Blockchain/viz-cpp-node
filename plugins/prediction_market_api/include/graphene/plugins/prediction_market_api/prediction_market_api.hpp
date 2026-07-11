#pragma once

#include <appbase/application.hpp>
#include <graphene/plugins/chain/plugin.hpp>
#include <graphene/plugins/json_rpc/utility.hpp>
#include <graphene/chain/pm_objects.hpp>
#include <graphene/plugins/prediction_market_api/meta_object.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <fc/optional.hpp>

namespace graphene { namespace plugins { namespace prediction_market_api {
    using plugins::json_rpc::msg_pack;
    using namespace graphene::chain;
    using graphene::protocol::chain_properties_pm;

    // ── Computed read-only DTOs (raw consensus objects + derived fields) ─────────

    // Oracle with a non-consensus reliability score (basis points, API heuristic).
    struct pm_oracle_api_object {
        pm_oracle_object oracle;
        uint32_t         reliability_score; // [0..10000] bp, see plugin impl
    };

    // A bet plus the parimutuel payout it would receive if its side wins (or its
    // realized payout once settled). Mirrors settle_market() exactly.
    struct pm_position_api_object {
        pm_bet_object bet;
        share_type    expected_payout;
        int8_t        market_status;
        int16_t       resolved_outcome;
    };

    struct pm_weight_entry {
        int16_t     outcome_index;
        std::string label;
        share_type  bets_sum;
        share_type  weight_sum;
    };

    // Per-side / per-outcome amount and curve-weight aggregates for a market.
    struct pm_market_weight_sums_api_object {
        uint8_t                      market_type;
        share_type                   bets_sum;
        std::vector<pm_weight_entry> outcomes;
    };

    // Live committee-dispute tally + a stake-weighted projection of the finalize cron, so a
    // caller can see the quorum status and the verdict that WOULD be applied under current votes.
    struct pm_dispute_votes_api_object {
        std::vector<pm_dispute_vote_object> votes;
        // Legacy rough tally (weight = |vote_percent|, NOT stake) — kept for compatibility.
        int64_t  uphold_weight;
        int64_t  challenge_weight;
        int64_t  total_weight;
        bool     challenger_leads;
        int16_t  proposed_outcome;
        // ── Accurate stake-weighted projection (mirrors pm_dispute_finalize) ──
        // Every *_shares value is in vesting-shares: effective_vesting_shares + lazy-pool stake→shares.
        int64_t  participation_shares      = 0;   ///< Σ weight of accounts that have voted (= max_rshares)
        int64_t  electorate_shares         = 0;   ///< total_vesting_shares + pool_NAV→shares (quorum base)
        int64_t  quorum_required_shares    = 0;   ///< electorate × pm_dispute_approve_min_percent
        int32_t  quorum_percent_bp         = 0;   ///< participation / electorate (bp, 10000 = 100.00%)
        bool     quorum_reached            = false; ///< participation_shares >= quorum_required_shares
        int64_t  oracle_defense_shares     = 0;   ///< Σ rshares defending the oracle (uphold side)
        int64_t  change_shares             = 0;   ///< Σ rshares backing an outcome change
        std::vector<int64_t> outcome_change_shares; ///< per-outcome backing rshares (size = outcome_count)
        bool     expected_uphold           = true;  ///< true ⇒ oracle resolution stands if finalized now
        int16_t  expected_outcome          = -1;  ///< outcome that would be set at finalize now
        int32_t  expected_consensus_strength_bp = 0; ///< winning / participation (bp); 0 when uphold
    };

    // One kline / time-series point for a market: a timestamped snapshot of every outcome's parimutuel
    // weight, recorded whenever the weights change. For charting: x = `timestamp` (unix seconds),
    // y[i] = `weights[i]`; the ratio weights[i]/Σweights is outcome i's implied probability.
    struct pm_kline_api_object {
        uint32_t                seq = 0;       ///< 0-based contiguous index of the change within the market
        uint32_t                timestamp = 0; ///< unix seconds (x coordinate)
        uint8_t                 reason = 0;    ///< 0 bet, 1 cancel, 2 liquidation, 3 batch settle, 4 leverage open, 5 leverage resolve
        share_type              bets_sum;      ///< total staked across all outcomes at this point
        std::vector<share_type> weights;       ///< per-outcome staked weight (y values), index = outcome_index
    };

    // ── Leverage previews (read-only projections of the in-node margin math) ─────────
    // All three below call the SAME frozen pm::leverage::* functions the evaluators use, so a
    // preview matches what pm_leverage_open/close/convert would compute in the next block. They
    // are non-consensus quotes: reserves move, so treat the numbers as an estimate at head block.

    // One reason a market/collateral pair cannot support (more) leverage.
    struct pm_leverage_constraint {
        std::string constraint; ///< leverage_disabled|cpmm_binary_only|market_inactive|expiration_buffer|min_market_liquidity|fund_availability|position_size|solvency
        std::string reason;     ///< human-readable explanation
    };

    // One point on the leverage slider: everything the open evaluator would derive for this loan.
    struct pm_leverage_stop {
        uint32_t   leverage_x100 = 0;        ///< (collateral+loan)/collateral × 100 (100 = 1.00×)
        share_type loan;                     ///< pool loan at this stop
        share_type total_bet;                ///< collateral + loan (fed into the CPMM)
        share_type expected_tokens;          ///< weight the AMM would return
        share_type pool_profit;              ///< liquidation_threshold − loan (pool's cut)
        share_type liquidation_threshold;    ///< loan × (1 + r%/100) = pool obligation
        share_type current_cancel_value;     ///< cancel value right after open (no opposing bet)
        share_type worst_case_cancel_value;  ///< cancel value after the worst opposing bet (solvency basis)
    };

    // get_leverage_quote result: max leverage + a slider of stops, mirroring pm_leverage_open.
    struct pm_leverage_quote_api_object {
        bool           available = false;               ///< true ⇒ max_loan > 0 (some leverage possible)
        int16_t        outcome_index = -1;
        share_type     collateral;
        share_type     max_loan;                        ///< largest solvent loan (0 if none qualifies)
        uint32_t       max_leverage_x100 = 100;         ///< (collateral+max_loan)/collateral × 100
        share_type     pool_free_amount;                ///< free_balance − leverage_fund_used
        share_type     fund_available;                  ///< free_balance×fund% − leverage_fund_used
        share_type     per_position_cap;                ///< fund_available × max_per_position_bp
        share_type     market_position_cap;             ///< liquidity_sum × max_position_ratio%
        uint16_t       pool_profit_percent = 0;         ///< r (plain %)
        uint16_t       safety_margin_percent = 0;       ///< s (plain %)
        uint16_t       max_slippage_percent = 0;        ///< sl (plain %)
        uint16_t       m_factor_percent = 0;            ///< worst-opposing m-factor (plain %)
        uint32_t       expiration_buffer_sec = 0;       ///< leverage disabled this long before betting_expiration
        uint32_t       funding_rate_ppm_per_day = 0;    ///< carry cost on the loan per 24h, in ppm (1e6)
        time_point_sec auto_close_time;                 ///< betting_expiration − buffer (protocol force-close point)
        std::vector<pm_leverage_stop>       stops;      ///< up to 12 evenly-spaced solvent stops (0 < loan ≤ max_loan)
        std::vector<pm_leverage_constraint> failed_constraints; ///< populated when !available
    };

    // get_leverage_close_preview result: mirrors pm_leverage_close at current reserves.
    struct pm_leverage_close_preview_api_object {
        int64_t    position_id = 0;
        int16_t    outcome_index = -1;
        share_type cancel_value;        ///< VIZ the tokens fetch from the curve now
        share_type pool_obligation;     ///< liquidation_threshold → returned to the pool
        share_type bettor_receives;     ///< cancel_value − pool_obligation (floored 0)
        share_type collateral;          ///< original bettor stake
        share_type loan;                ///< pool loan
        share_type pool_profit_charge;  ///< pool's fixed profit on the loan
        bool       closeable = false;   ///< cancel_value ≥ pool_obligation (else protocol liquidates)
        int64_t    loss_vs_collateral = 0; ///< collateral − bettor_receives (negative = profit)
        int32_t    loss_percent_bp = 0;    ///< loss_vs_collateral / collateral (bp)
    };

    // get_leverage_convert_preview result: mirrors pm_leverage_convert at current reserves.
    struct pm_leverage_convert_preview_api_object {
        int64_t    position_id = 0;
        int16_t    outcome_index = -1;
        share_type cancel_value;                   ///< VIZ the tokens fetch now
        share_type pool_obligation;                ///< loan + pool profit (repaid on convert)
        share_type current_profit;                 ///< cancel_value − pool_obligation
        uint16_t   conversion_profit_cost_percent = 0; ///< median value the op MUST echo
        share_type conversion_fee;                 ///< current_profit × cost% /100
        share_type total_user_payment;             ///< pool_obligation + conversion_fee (debited on convert)
        bool       convertible = false;            ///< current_profit > 0
    };

    // ── Category taxonomy + live counts (non-consensus, from the meta index) ─────────
    // Aggregated over currently-indexed (non-pruned) markets, so counts reflect live/recent
    // markets — pruned-out expired markets drop off, matching a "browse now" catalog.
    struct pm_subcategory_count {
        std::string subcategory;
        uint32_t    count = 0;
    };
    struct pm_category_count {
        std::string                       category;
        uint32_t                          count = 0;
        std::vector<pm_subcategory_count> subcategories;
    };
    struct pm_tag_count {
        std::string tag;
        uint32_t    count = 0;
    };
    struct pm_market_categories_api_object {
        std::vector<pm_category_count> categories; ///< sorted by count desc
        std::vector<pm_tag_count>      hot_tags;   ///< top tags by count (jurisdiction-ban tags excluded)
    };

    // One-call enriched market view (saves the thin client several round-trips). The account-scoped
    // vectors are populated only when get_market_full is called with a non-empty account argument.
    struct pm_market_full_api_object {
        pm_market_object                          market;
        std::vector<pm_outcome_object>            outcomes;      ///< empty for binary markets
        pm_market_weight_sums_api_object          weight_sums;   ///< per-outcome amount + curve weight
        fc::optional<pm_oracle_api_object>        oracle;        ///< the market's oracle (+ reliability)
        fc::optional<pm_market_meta_object>       meta;          ///< parsed metadata, if indexed
        std::vector<pm_position_api_object>       my_positions;          ///< account's bets on THIS market
        std::vector<pm_leverage_position_object>  my_leverage_positions; ///< account's leverage on THIS market
        std::vector<pm_liquidity_object>          my_liquidity;          ///< account's LP on THIS market
    };

    DEFINE_API_ARGS(get_market,                msg_pack, fc::variant)
    DEFINE_API_ARGS(list_markets,              msg_pack, std::vector<fc::variant>)
    DEFINE_API_ARGS(list_markets_by_oracle,    msg_pack, std::vector<fc::variant>)
    DEFINE_API_ARGS(list_markets_awaiting_resolution, msg_pack, std::vector<fc::variant>)
    DEFINE_API_ARGS(list_markets_by_creator,   msg_pack, std::vector<fc::variant>)
    DEFINE_API_ARGS(get_market_outcomes,       msg_pack, std::vector<pm_outcome_object>)
    DEFINE_API_ARGS(get_market_weight_sums,    msg_pack, pm_market_weight_sums_api_object)
    DEFINE_API_ARGS(get_market_bets,           msg_pack, std::vector<pm_bet_object>)
    DEFINE_API_ARGS(get_account_positions,     msg_pack, std::vector<pm_position_api_object>)
    DEFINE_API_ARGS(get_market_liquidity,      msg_pack, std::vector<pm_liquidity_object>)
    DEFINE_API_ARGS(get_account_leverage_positions, msg_pack, std::vector<pm_leverage_position_object>)
    DEFINE_API_ARGS(get_market_leverage_positions,  msg_pack, std::vector<pm_leverage_position_object>)
    DEFINE_API_ARGS(get_creator_ban,           msg_pack, pm_creator_ban_object)
    DEFINE_API_ARGS(get_oracle,                msg_pack, pm_oracle_api_object)
    DEFINE_API_ARGS(list_oracles,              msg_pack, std::vector<pm_oracle_object>)
    DEFINE_API_ARGS(get_dispute,               msg_pack, pm_dispute_object)
    DEFINE_API_ARGS(get_dispute_votes,         msg_pack, pm_dispute_votes_api_object)
    DEFINE_API_ARGS(get_lazy_pool,             msg_pack, pm_lazy_pool_object)
    DEFINE_API_ARGS(get_lazy_deposit,          msg_pack, pm_lazy_deposit_object)
    DEFINE_API_ARGS(get_lazy_withdraw_requests, msg_pack, std::vector<pm_lazy_withdraw_request_object>)
    DEFINE_API_ARGS(get_pm_chain_properties,   msg_pack, chain_properties_pm)
    DEFINE_API_ARGS(get_market_meta,           msg_pack, pm_market_meta_object)
    DEFINE_API_ARGS(list_markets_by_category,  msg_pack, std::vector<fc::variant>)
    DEFINE_API_ARGS(list_markets_by_event,     msg_pack, std::vector<fc::variant>)
    DEFINE_API_ARGS(get_market_kline,          msg_pack, std::vector<pm_kline_api_object>)
    DEFINE_API_ARGS(get_leverage_quote,           msg_pack, pm_leverage_quote_api_object)
    DEFINE_API_ARGS(get_leverage_close_preview,   msg_pack, pm_leverage_close_preview_api_object)
    DEFINE_API_ARGS(get_leverage_convert_preview, msg_pack, pm_leverage_convert_preview_api_object)
    DEFINE_API_ARGS(get_market_categories,     msg_pack, pm_market_categories_api_object)
    DEFINE_API_ARGS(get_category_tag_counts,   msg_pack, pm_market_categories_api_object)
    DEFINE_API_ARGS(get_market_full,           msg_pack, fc::variant)
    DEFINE_API_ARGS(get_lazy_allocations,      msg_pack, std::vector<pm_lazy_allocation_object>)
    DEFINE_API_ARGS(get_market_lazy_allocation, msg_pack, pm_lazy_allocation_object)

    class prediction_market_api final : public appbase::plugin<prediction_market_api> {
    public:
        APPBASE_PLUGIN_REQUIRES (
            (chain::plugin)
            (json_rpc::plugin)
        )

        DECLARE_API(
            (get_market)
            (list_markets)
            (list_markets_by_oracle)
            (list_markets_awaiting_resolution)
            (list_markets_by_creator)
            (get_market_outcomes)
            (get_market_weight_sums)
            (get_market_bets)
            (get_account_positions)
            (get_market_liquidity)
            (get_account_leverage_positions)
            (get_market_leverage_positions)
            (get_creator_ban)
            (get_oracle)
            (list_oracles)
            (get_dispute)
            (get_dispute_votes)
            (get_lazy_pool)
            (get_lazy_deposit)
            (get_lazy_withdraw_requests)
            (get_pm_chain_properties)
            (get_market_meta)
            (list_markets_by_category)
            (list_markets_by_event)
            (get_market_kline)
            (get_leverage_quote)
            (get_leverage_close_preview)
            (get_leverage_convert_preview)
            (get_market_categories)
            (get_category_tag_counts)
            (get_market_full)
            (get_lazy_allocations)
            (get_market_lazy_allocation)
        )

        prediction_market_api();
        ~prediction_market_api();

        void set_program_options(
            boost::program_options::options_description&,
            boost::program_options::options_description& config_file_options
        ) override;

        static const std::string& name();

        void plugin_initialize(const boost::program_options::variables_map& options) override;

        void plugin_startup() override;
        void plugin_shutdown() override;

    private:
        struct impl;
        std::unique_ptr<impl> pimpl;
    };
} } } // graphene::plugins::prediction_market_api

FC_REFLECT((graphene::plugins::prediction_market_api::pm_oracle_api_object),
    (oracle)(reliability_score))
FC_REFLECT((graphene::plugins::prediction_market_api::pm_position_api_object),
    (bet)(expected_payout)(market_status)(resolved_outcome))
FC_REFLECT((graphene::plugins::prediction_market_api::pm_weight_entry),
    (outcome_index)(label)(bets_sum)(weight_sum))
FC_REFLECT((graphene::plugins::prediction_market_api::pm_market_weight_sums_api_object),
    (market_type)(bets_sum)(outcomes))
FC_REFLECT((graphene::plugins::prediction_market_api::pm_dispute_votes_api_object),
    (votes)(uphold_weight)(challenge_weight)(total_weight)(challenger_leads)(proposed_outcome)
    (participation_shares)(electorate_shares)(quorum_required_shares)(quorum_percent_bp)
    (quorum_reached)(oracle_defense_shares)(change_shares)(outcome_change_shares)
    (expected_uphold)(expected_outcome)(expected_consensus_strength_bp))
FC_REFLECT((graphene::plugins::prediction_market_api::pm_kline_api_object),
    (seq)(timestamp)(reason)(bets_sum)(weights))
FC_REFLECT((graphene::plugins::prediction_market_api::pm_leverage_constraint),
    (constraint)(reason))
FC_REFLECT((graphene::plugins::prediction_market_api::pm_leverage_stop),
    (leverage_x100)(loan)(total_bet)(expected_tokens)(pool_profit)(liquidation_threshold)
    (current_cancel_value)(worst_case_cancel_value))
FC_REFLECT((graphene::plugins::prediction_market_api::pm_leverage_quote_api_object),
    (available)(outcome_index)(collateral)(max_loan)(max_leverage_x100)(pool_free_amount)
    (fund_available)(per_position_cap)(market_position_cap)(pool_profit_percent)
    (safety_margin_percent)(max_slippage_percent)(m_factor_percent)(expiration_buffer_sec)
    (funding_rate_ppm_per_day)(auto_close_time)(stops)(failed_constraints))
FC_REFLECT((graphene::plugins::prediction_market_api::pm_leverage_close_preview_api_object),
    (position_id)(outcome_index)(cancel_value)(pool_obligation)(bettor_receives)(collateral)
    (loan)(pool_profit_charge)(closeable)(loss_vs_collateral)(loss_percent_bp))
FC_REFLECT((graphene::plugins::prediction_market_api::pm_leverage_convert_preview_api_object),
    (position_id)(outcome_index)(cancel_value)(pool_obligation)(current_profit)
    (conversion_profit_cost_percent)(conversion_fee)(total_user_payment)(convertible))
FC_REFLECT((graphene::plugins::prediction_market_api::pm_subcategory_count),
    (subcategory)(count))
FC_REFLECT((graphene::plugins::prediction_market_api::pm_category_count),
    (category)(count)(subcategories))
FC_REFLECT((graphene::plugins::prediction_market_api::pm_tag_count),
    (tag)(count))
FC_REFLECT((graphene::plugins::prediction_market_api::pm_market_categories_api_object),
    (categories)(hot_tags))
FC_REFLECT((graphene::plugins::prediction_market_api::pm_market_full_api_object),
    (market)(outcomes)(weight_sums)(oracle)(meta)(my_positions)(my_leverage_positions)(my_liquidity))
