// HF14 chain_properties_pm::validate() boundary sweep (audit 2026-08-13, batch C / L3).
// Governance parameters are median-voted, so validate() is the ONLY consensus gate against
// pathological values — every ratio cap, floor and cross-field invariant gets a boundary
// probe here (just-below / at / just-above), including the audit-added floors:
//   M6: pm_dispute_grace_sec >= 3600 (structural floor, zero grace races the cleanup crons)
//   M4: pm_min_batch_bet.amount >= 100 (0.1 VIZ, keeps commit-spam non-free)

#define BOOST_TEST_MODULE pm_props_validate
#include <boost/test/unit_test.hpp>

#include <graphene/protocol/chain_operations.hpp>
#include <fc/exception/exception.hpp>

using graphene::protocol::chain_properties_pm;

namespace {

    // Mutator-style checks: a fresh default-constructed props object must validate clean,
    // then each mutation is probed independently against the assert.
    template <typename Mutate>
    void expect_fail(const Mutate& m) {
        chain_properties_pm p;
        m(p);
        BOOST_CHECK_THROW(p.validate(), fc::assert_exception);
    }

    template <typename Mutate>
    void expect_ok(const Mutate& m) {
        chain_properties_pm p;
        m(p);
        BOOST_CHECK_NO_THROW(p.validate());
    }

} // namespace

BOOST_AUTO_TEST_CASE(defaults_validate_clean) {
    chain_properties_pm p;
    BOOST_CHECK_NO_THROW(p.validate());
}

BOOST_AUTO_TEST_CASE(token_assets_must_be_positive_viz) {
    expect_fail([](chain_properties_pm& p) { p.pm_dispute_fee.amount = 0; });
    expect_fail([](chain_properties_pm& p) { p.pm_min_batch_bet.amount = 0; });
    expect_fail([](chain_properties_pm& p) { p.pm_market_creation_fee.symbol = SHARES_SYMBOL; });
}

BOOST_AUTO_TEST_CASE(max_outcomes_bounds) {
    expect_fail([](chain_properties_pm& p) { p.pm_max_outcomes = 1; });
    expect_ok  ([](chain_properties_pm& p) { p.pm_max_outcomes = 2; });
    expect_ok  ([](chain_properties_pm& p) { p.pm_max_outcomes = MAX_PM_OUTCOMES_PER_MARKET; });
    expect_fail([](chain_properties_pm& p) { p.pm_max_outcomes = MAX_PM_OUTCOMES_PER_MARKET + 1; });
}

BOOST_AUTO_TEST_CASE(time_and_fee_caps) {
    expect_fail([](chain_properties_pm& p) { p.pm_max_market_duration = 0; });
    expect_fail([](chain_properties_pm& p) { p.pm_max_oracle_fee_percent = 10001; });
    expect_ok  ([](chain_properties_pm& p) { p.pm_max_oracle_fee_percent = 10000; });
    expect_fail([](chain_properties_pm& p) { p.pm_oracle_accept_window_sec = 0; });
    // B9 spend-path bound: > 1e6 (100% of profit) is unsound.
    expect_ok  ([](chain_properties_pm& p) { p.pm_max_time_penalty = 1000000; });
    expect_fail([](chain_properties_pm& p) { p.pm_max_time_penalty = 1000001; });
}

BOOST_AUTO_TEST_CASE(coverage_cross_field) {
    expect_fail([](chain_properties_pm& p) {
        p.pm_betting_min_coverage_percent = p.pm_listing_min_coverage_percent + 1;
    });
}

BOOST_AUTO_TEST_CASE(dispute_params_bounds) {
    expect_fail([](chain_properties_pm& p) { p.pm_dispute_approve_min_percent = 10001; });
    expect_fail([](chain_properties_pm& p) { p.pm_oracle_penalty_percent = 10001; });
    expect_fail([](chain_properties_pm& p) { p.pm_no_contest_penalty_percent = 10001; });
    // M6: grace floor. Zero grace races the cleanup crons against settlement; default 12 h.
    expect_fail([](chain_properties_pm& p) { p.pm_dispute_grace_sec = 0; });
    expect_fail([](chain_properties_pm& p) { p.pm_dispute_grace_sec = 3599; });
    expect_ok  ([](chain_properties_pm& p) { p.pm_dispute_grace_sec = 3600; });
    // Reward multiplier: [10000, 1000000] (1x..100x).
    expect_fail([](chain_properties_pm& p) { p.pm_dispute_reward_multiplier = 9999; });
    expect_ok  ([](chain_properties_pm& p) { p.pm_dispute_reward_multiplier = 10000; });
    expect_ok  ([](chain_properties_pm& p) { p.pm_dispute_reward_multiplier = 1000000; });
    expect_fail([](chain_properties_pm& p) { p.pm_dispute_reward_multiplier = 1000001; });
}

// q#689 (2026-08-21): the four vote caps moved from #define constants to median-voted chain
// properties. validate() is their only consensus gate. Committee caps live on the base hf9
// struct (inherited by pm), the dispute caps on pm.
BOOST_AUTO_TEST_CASE(vote_caps_bounds) {
    // Committee (base hf9): per-request ballot cap + per-voter vesting floor.
    expect_fail([](chain_properties_pm& p) { p.committee_votes_per_request = 0; });
    expect_fail([](chain_properties_pm& p) { p.committee_vote_min_vesting.amount = 0; });
    expect_fail([](chain_properties_pm& p) { p.committee_vote_min_vesting.symbol = SHARES_SYMBOL; });
    // PM dispute: per-market ballot cap + per-voter vesting floor.
    expect_fail([](chain_properties_pm& p) { p.pm_dispute_votes_per_market = 0; });
    expect_fail([](chain_properties_pm& p) { p.pm_dispute_vote_min_vesting.amount = 0; });
    expect_fail([](chain_properties_pm& p) { p.pm_dispute_vote_min_vesting.symbol = SHARES_SYMBOL; });
}

BOOST_AUTO_TEST_CASE(batch_commit_reveal_bounds) {
    expect_fail([](chain_properties_pm& p) { p.pm_commit_no_reveal_penalty_percent = 10001; });
    // M4: batch-bet spam floor — votable down to 1 satoshi before the audit fix.
    expect_fail([](chain_properties_pm& p) { p.pm_min_batch_bet.amount = 99; });
    expect_ok  ([](chain_properties_pm& p) { p.pm_min_batch_bet.amount = 100; });
    expect_fail([](chain_properties_pm& p) { p.pm_batch_epoch_blocks = 0; });
    expect_fail([](chain_properties_pm& p) { p.pm_reveal_window_blocks = 0; });
    // Constructive GC invariant: retention must STRICTLY exceed the worst-case reveal deadline.
    const uint32_t worst = (chain_properties_pm().pm_batch_epoch_blocks
                          + chain_properties_pm().pm_reveal_window_blocks) * CHAIN_BLOCK_INTERVAL;
    expect_fail([&](chain_properties_pm& p) { p.pm_closed_market_retention_sec = worst; });
    expect_ok  ([&](chain_properties_pm& p) { p.pm_closed_market_retention_sec = worst + 1; });
}

BOOST_AUTO_TEST_CASE(cron_and_pool_bounds) {
    expect_fail([](chain_properties_pm& p) { p.pm_processing_cap_per_block = 0; });
    expect_fail([](chain_properties_pm& p) { p.pm_lazy_alloc_percent = 10001; });
    expect_fail([](chain_properties_pm& p) { p.pm_lazy_max_total_alloc_percent = 10001; });
    expect_fail([](chain_properties_pm& p) { p.pm_lazy_recall_step_percent = 10001; });
    expect_fail([](chain_properties_pm& p) { p.pm_lazy_emergency_penalty_percent = 10001; });
    expect_fail([](chain_properties_pm& p) { p.pm_lazy_min_liquidity_fee_percent = 10001; });
    expect_ok  ([](chain_properties_pm& p) { p.pm_early_exit_reward_cap_percent = 10000; });
}

// #432 A/D: the anti-dust floor of the instant bet path and the per-block row budget of the
// incremental settlement sweep. Both are median-voted, so validate() is their only gate: voting
// pm_min_bet toward zero brings back free row-spam, and a zero/unbounded row budget either wedges
// every settlement or re-opens the unbounded-block hole the sweep exists to close.
BOOST_AUTO_TEST_CASE(settle_work_bounds) {
    expect_fail([](chain_properties_pm& p) { p.pm_min_bet.amount = 0; });
    expect_fail([](chain_properties_pm& p) { p.pm_min_bet.symbol = SHARES_SYMBOL; });
    expect_fail([](chain_properties_pm& p) { p.pm_min_bet.amount = 99; });   // just below 0.1 VIZ
    expect_ok  ([](chain_properties_pm& p) { p.pm_min_bet.amount = 100; });  // exactly 0.1 VIZ

    expect_fail([](chain_properties_pm& p) { p.pm_settle_rows_per_block = 0; });
    expect_fail([](chain_properties_pm& p) { p.pm_settle_rows_per_block = 99; });
    expect_ok  ([](chain_properties_pm& p) { p.pm_settle_rows_per_block = 100; });
    expect_ok  ([](chain_properties_pm& p) { p.pm_settle_rows_per_block = 100000; });
    expect_fail([](chain_properties_pm& p) { p.pm_settle_rows_per_block = 100001; });
}

BOOST_AUTO_TEST_CASE(leverage_bounds) {
    expect_fail([](chain_properties_pm& p) { p.pm_leverage_fund_percent = 101; });
    expect_fail([](chain_properties_pm& p) { p.pm_leverage_max_per_position_bp = 10001; });
    expect_fail([](chain_properties_pm& p) { p.pm_leverage_pool_profit_percent = 101; });
    expect_fail([](chain_properties_pm& p) { p.pm_leverage_safety_margin_percent = 101; });
}
