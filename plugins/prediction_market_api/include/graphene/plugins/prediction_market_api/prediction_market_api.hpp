#pragma once

#include <appbase/application.hpp>
#include <graphene/plugins/chain/plugin.hpp>
#include <graphene/plugins/json_rpc/utility.hpp>
#include <graphene/chain/pm_objects.hpp>
#include <graphene/plugins/prediction_market_api/meta_object.hpp>
#include <graphene/protocol/chain_operations.hpp>

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

    DEFINE_API_ARGS(get_market,                msg_pack, pm_market_object)
    DEFINE_API_ARGS(list_markets,              msg_pack, std::vector<pm_market_object>)
    DEFINE_API_ARGS(list_markets_by_oracle,    msg_pack, std::vector<pm_market_object>)
    DEFINE_API_ARGS(list_markets_by_creator,   msg_pack, std::vector<pm_market_object>)
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
    DEFINE_API_ARGS(get_pm_chain_properties,   msg_pack, chain_properties_pm)
    DEFINE_API_ARGS(get_market_meta,           msg_pack, pm_market_meta_object)
    DEFINE_API_ARGS(list_markets_by_category,  msg_pack, std::vector<pm_market_meta_object>)
    DEFINE_API_ARGS(get_market_kline,          msg_pack, std::vector<pm_kline_api_object>)

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
            (get_pm_chain_properties)
            (get_market_meta)
            (list_markets_by_category)
            (get_market_kline)
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
