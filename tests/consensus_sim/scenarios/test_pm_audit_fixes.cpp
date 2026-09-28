// PM audit regressions: legacy replay and explicit, test-only opt-in to the
// unscheduled next hardfork. No production activation or wire-layout changes.
#include <boost/test/unit_test.hpp>
#include "simulated_node.hpp"
#include "genesis_factory.hpp"
#include "virtual_clock.hpp"
#include <graphene/chain/database.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/validator_objects.hpp>
#include <graphene/chain/pm_objects.hpp>
#include <graphene/chain/pm/lmsr_q96.hpp>
#include <graphene/protocol/pm_operations.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <graphene/protocol/config.hpp>
#include <fc/crypto/sha256.hpp>

using namespace consensus_sim;
using namespace graphene::chain;
using namespace graphene::protocol;

namespace {
void produce(simulated_node& n, const genesis_params& gp, fc::time_point_sec& when) {
    when += fc::seconds(CHAIN_BLOCK_INTERVAL);
    auto validator = n.db().get_scheduled_validator(1);
    n.produce_block(validator, validator == gp.initiator_name ? gp.initiator_key : gp.genesis_witness_key, when);
}
signed_transaction sign(const operation& op, const fc::ecc::private_key& key, const simulated_node& n, int nonce = 0) {
    signed_transaction tx;
    tx.set_reference_block(n.head_block_id());
    tx.set_expiration(n.head_block_time() + fc::seconds(60 + nonce));
    tx.operations.emplace_back(op);
    tx.sign(key, n.chain_id());
    return tx;
}
void apply(simulated_node& n, const genesis_params& gp, fc::time_point_sec& when,
           const operation& op, const fc::ecc::private_key& key, int nonce = 0) {
    n.push_pending_transaction(sign(op, key, n, nonce));
    produce(n, gp, when);
}
void hf14(simulated_node& n, const genesis_params& gp, fc::time_point_sec& when) {
    // Register the initiator as a staked, self-voted validator and advance until
    // HF14 activates. In a BUILD_TESTNET harness (CHAIN_HARDFORK_REQUIRED_VALIDATORS=1)
    // the single validator's version vote reaches quorum; in a mainnet build the
    // 17/21 quorum is unreachable and the existing pm_lifecycle tests soft-skip.
    // These regressions are therefore only exercised under BUILD_TESTNET, matching
    // the rest of the PM consensus suite.
    produce(n, gp, when); // block 1 (committee gap-filler)
    const auto bal = n.db().get_account(gp.initiator_name).balance.amount;
    transfer_to_vesting_operation tv;
    tv.from = gp.initiator_name; tv.to = gp.initiator_name;
    tv.amount = asset(share_type(bal.value / 2), TOKEN_SYMBOL);
    validator_update_operation vu;
    vu.owner = gp.initiator_name; vu.url = "viz";
    vu.block_signing_key = gp.initiator_key.get_public_key();
    account_validator_vote_operation vv;
    vv.account = gp.initiator_name; vv.validator = gp.initiator_name; vv.approve = true;
    signed_transaction tx;
    tx.set_reference_block(n.head_block_id());
    tx.set_expiration(n.head_block_time() + fc::seconds(60));
    tx.operations = {tv, vu, vv};
    tx.sign(gp.initiator_key, n.chain_id());
    n.push_pending_transaction(tx);
    for (int i = 0; i < 300 && !n.db().has_hardfork(CHAIN_HARDFORK_14); ++i) produce(n, gp, when);
    BOOST_REQUIRE(n.db().has_hardfork(CHAIN_HARDFORK_14));
}
// Test-only switch for the PM audit fork. On a BUILD_TESTNET build the fork is REGISTERED
// (CHAIN_NUM_HARDFORKS=15) and the harness validator reaches quorum on its own, so — unlike the
// first cut of these regressions — "fixed" is not something the test grants, it is what the node
// does by itself once the fork's activation time is reached. The deterministic way to get both
// sides is therefore to keep the simulation clock BEFORE CHAIN_HARDFORK_15_TIME (see sim_start)
// and to toggle the processed marker here: push to enable, pop to get the legacy rules back.
//
// Popping is stable: process_hardforks() only re-applies while _hardfork_versions[last_hardfork]
// < next_hardfork, and by the time the marker exists the tally has already pinned next_hardfork to
// the audit-fork version, so the removed marker stays removed for the rest of the run. Re-pushing
// while it is already set would corrupt the arithmetic (processed_hardforks.size() would no longer
// equal last_hardfork+1 and the next apply_hardfork would fail its own sanity assert), hence the
// idempotent early return.
void set_pm_audit_fix(simulated_node& n, bool enabled) {
    if (n.db().has_hardfork(CHAIN_PM_AUDIT_FIX_HARDFORK) == enabled) return;
    const auto& hf = n.db().get_hardfork_property_object();
    n.db().modify(hf, [&](hardfork_property_object& h) {
        if (enabled) h.processed_hardforks.push_back(n.head_block_time());
        else h.processed_hardforks.pop_back();
    });
    BOOST_REQUIRE_EQUAL(n.db().has_hardfork(CHAIN_PM_AUDIT_FIX_HARDFORK), enabled);
}

// Start the simulation just past HF14 so the chain clock never reaches CHAIN_HARDFORK_15_TIME: the
// legacy legs below are only legacy while the audit fork is pending, and a clock that tracks
// fc::time_point::now() would silently flip them to the fixed rules on the day the fork's testnet
// activation time passes (the first cut of these tests had exactly that time bomb). Both constants
// are compile-time, so the pair stays ordered forever.
fc::time_point_sec sim_start() {
    return fc::time_point_sec(CHAIN_HARDFORK_14_TIME) + fc::seconds(CHAIN_BLOCK_INTERVAL * 3);
}
void oracle(simulated_node& n, const genesis_params& gp, fc::time_point_sec& when) {
    pm_oracle_register_operation op;
    op.owner = gp.initiator_name;
    op.insurance = n.db().get_validator_schedule_object().median_props.pm_min_oracle_insurance;
    op.fixed_fee = asset(0, TOKEN_SYMBOL);
    apply(n, gp, when, op, gp.initiator_key);
}
// Σ b_share over the market's STILL-ACTIVE LP rows. A fully withdrawn row is kept with status 3
// (the object is the position's history), and those no longer carry curve weight.
int64_t active_rows_b_share(simulated_node& n, pm_market_id_type mid) {
    int64_t sum = 0;
    for (const auto& l : n.db().get_index<pm_liquidity_index>().indices())
        if (l.market == mid && l.status != 3) sum += l.b_share.value;
    return sum;
}
}

BOOST_AUTO_TEST_CASE(pm_lmsr_partial_lp_replay_and_future_fix) {
    for (bool fixed : {false, true}) {
        auto gp = make_genesis_params(fixed ? 0xA15A : 0xA15B, 1);
        virtual_clock clk(sim_start());
        simulated_node n(fixed ? "pm-lp-fixed" : "pm-lp-legacy", gp, clk);
        fc::time_point_sec when = clk.now() - fc::seconds(CHAIN_BLOCK_INTERVAL);
        hf14(n, gp, when); oracle(n, gp, when);
        set_pm_audit_fix(n, fixed);
        const int64_t unit = n.db().get_validator_schedule_object().median_props.pm_min_liquidity.amount.value;
        pm_create_market_operation cm;
        cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
        cm.market_type = 1; cm.outcomes = {"A", "B", "C"}; cm.url = "criteria";
        cm.liquidity = asset(share_type(unit), TOKEN_SYMBOL);
        cm.lmsr_b = lmsr::lmsr_b_from_liquidity(unit, 3);
        cm.betting_expiration = n.head_block_time() + fc::seconds(3600);
        cm.result_expiration = n.head_block_time() + fc::seconds(7200);
        apply(n, gp, when, cm, gp.initiator_key);
        const pm_market_id_type mid(0);
        const auto seed_b = n.db().get<pm_market_object>(mid).lmsr_b.value;
        pm_add_liquidity_operation add;
        add.provider = gp.initiator_name; add.market_id = 0;
        add.amount = asset(share_type(unit * 2), TOKEN_SYMBOL);
        apply(n, gp, when, add, gp.initiator_key);
        const pm_liquidity_id_type lid(1);
        const auto added_b = n.db().get<pm_liquidity_object>(lid).b_share.value;
        BOOST_REQUIRE_GT(added_b, 0);
        pm_withdraw_liquidity_operation w;
        w.provider = gp.initiator_name; w.liquidity_id = 1;
        w.amount = asset(share_type(unit), TOKEN_SYMBOL);
        apply(n, gp, when, w, gp.initiator_key);
        const int64_t removed = added_b / 2;
        BOOST_CHECK_EQUAL(n.db().get<pm_market_object>(mid).lmsr_b.value, seed_b + added_b - removed);
        BOOST_CHECK_EQUAL(n.db().get<pm_liquidity_object>(lid).b_share.value,
                          fixed ? added_b - removed : added_b);
        w.amount = asset(0, TOKEN_SYMBOL);
        apply(n, gp, when, w, gp.initiator_key);
        BOOST_CHECK_EQUAL(n.db().get<pm_market_object>(mid).lmsr_b.value, fixed ? seed_b : seed_b - removed);
        BOOST_CHECK_EQUAL(n.db().get<pm_market_object>(mid).liquidity_sum.value, unit);
    }
}

BOOST_AUTO_TEST_CASE(pm_mode_one_requires_commit_reveal_after_gate) {
    for (bool fixed : {false, true}) {
        auto gp = make_genesis_params(fixed ? 0xA151 : 0xA152, 1);
        virtual_clock clk(sim_start());
        simulated_node n(fixed ? "pm-batch-fixed" : "pm-batch-legacy", gp, clk);
        fc::time_point_sec when = clk.now() - fc::seconds(CHAIN_BLOCK_INTERVAL);
        hf14(n, gp, when); oracle(n, gp, when);
        set_pm_audit_fix(n, fixed);
        const auto& mp = n.db().get_validator_schedule_object().median_props;
        BOOST_REQUIRE(mp.pm_commit_reveal_enabled);
        const int64_t unit = mp.pm_min_liquidity.amount.value;
        pm_create_market_operation cm;
        cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
        cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
        cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
        cm.betting_expiration = n.head_block_time() + fc::seconds(3600);
        cm.result_expiration = n.head_block_time() + fc::seconds(7200);
        cm.allow_batch = true; cm.allow_instant_bet = false;
        apply(n, gp, when, cm, gp.initiator_key);
        pm_place_bet_operation bet;
        bet.account = gp.initiator_name; bet.market_id = 0;
        bet.side = 0; bet.outcome_index = -1;
        bet.amount = asset(share_type(unit), TOKEN_SYMBOL); bet.mode = 1;
        const auto before = n.db().get_account(gp.initiator_name).balance;
        const auto reserve = n.db().get<pm_market_object>(pm_market_id_type(0)).reserve_a.value;
        if (fixed) {
            BOOST_CHECK_THROW(n.push_pending_transaction(sign(bet, gp.initiator_key, n)), std::runtime_error);
            BOOST_CHECK_EQUAL(n.db().get_account(gp.initiator_name).balance.amount.value, before.amount.value);
            BOOST_CHECK_EQUAL(n.db().get<pm_market_object>(pm_market_id_type(0)).reserve_a.value, reserve);
            BOOST_CHECK_EQUAL(n.db().get_index<pm_bet_index>().indices().size(), 0u);

            // The supported batch path still escrows once, reveals a queued row,
            // then moves stake into the curve at the epoch boundary (no double debit).
            const int64_t mid = 0, amount = unit, min_tokens = 0;
            const int8_t side = 0;
            const int16_t outcome = -1;
            const account_name_type account = gp.initiator_name;
            const std::string salt = "audit-batch";
            fc::sha256::encoder enc;
            enc.write((const char*)&mid, sizeof(mid));
            enc.write((const char*)&account.data, sizeof(account.data));
            enc.write((const char*)&side, sizeof(side));
            enc.write((const char*)&outcome, sizeof(outcome));
            enc.write((const char*)&amount, sizeof(amount));
            enc.write((const char*)&min_tokens, sizeof(min_tokens));
            enc.write(salt.data(), (uint32_t)salt.size());
            pm_commit_bet_operation commit;
            commit.account = account; commit.market_id = mid;
            commit.commitment = enc.result();
            commit.escrow_amount = asset(share_type(unit), TOKEN_SYMBOL);
            commit.no_reveal_fee_percent = mp.pm_commit_no_reveal_penalty_percent;
            apply(n, gp, when, commit, gp.initiator_key);
            const auto after_commit = n.db().get_account(account).balance;
            BOOST_CHECK_EQUAL((before - after_commit).amount.value, unit);
            pm_reveal_bet_operation reveal;
            reveal.account = account; reveal.commit_id = 0;
            reveal.side = side; reveal.outcome_index = outcome;
            reveal.amount = asset(share_type(unit), TOKEN_SYMBOL);
            reveal.min_tokens = share_type(0); reveal.salt = salt;
            apply(n, gp, when, reveal, gp.initiator_key);
            const pm_bet_id_type bid(0);
            BOOST_CHECK_EQUAL(n.db().get_account(account).balance.amount.value, after_commit.amount.value);
            BOOST_CHECK_EQUAL(n.db().get<pm_bet_object>(bid).mode, 1);
            BOOST_CHECK_EQUAL(n.db().get<pm_bet_object>(bid).status, 5);
            BOOST_CHECK_EQUAL(n.db().get<pm_market_object>(pm_market_id_type(0)).reserve_a.value, reserve);
            for (int i = 0; i < 150 && n.db().get<pm_bet_object>(bid).status == 5; ++i)
                produce(n, gp, when);
            BOOST_CHECK_EQUAL(n.db().get<pm_bet_object>(bid).status, 0);
            BOOST_CHECK_GT(n.db().get<pm_bet_object>(bid).weight.value, 0);
            BOOST_CHECK_EQUAL(n.db().get<pm_market_object>(pm_market_id_type(0)).reserve_a.value,
                              reserve + unit);
            BOOST_CHECK_EQUAL(n.db().get_account(account).balance.amount.value, after_commit.amount.value);
        } else {
            apply(n, gp, when, bet, gp.initiator_key);
            const auto& row = *n.db().get_index<pm_bet_index>().indices().begin();
            BOOST_CHECK_EQUAL(row.mode, 1);
            BOOST_CHECK_EQUAL(row.status, 0); // historical immediate fill, preserved for replay
            BOOST_CHECK_GT(row.weight.value, 0);
            BOOST_CHECK_GT(n.db().get<pm_market_object>(pm_market_id_type(0)).reserve_a.value, reserve);
        }
    }
}

// Why the b_share fix matters at all, and what the gate does with state the legacy path already
// broke. The market is created at exactly pm_min_liquidity and topped up 3× that, so the top-up row
// carries exactly 3·seed_b of the curve's 4·seed_b. Half of that row leaves (the withdrawal is
// inside the live-market floor, so it is allowed):
//   * legacy — the curve gives up the b_removed but the row keeps its full 3·seed_b, i.e. the row
//     now claims more than the market holds. The final exit takes ALL of it and lmsr_b lands at
//     −0.5·seed_b. That is not a merely flat curve: lmsr_q96 fails soft for b <= 0 (price/buy-cost/
//     tokens-for-amount return 0 without ever reaching validate_domain), so every outcome prices at
//     0 and a bet costs nothing while the market still holds the remaining LP capital and the
//     bettors' stakes — the drain, not a cosmetic bookkeeping drift.
//   * fixed — both records shrank by the same floored amount, so the exit removes exactly the row's
//     share and the curve lands back on the untouched seed row: lmsr_b == seed_b, Σ active rows ==
//     lmsr_b, and the market still prices.
//   * stale + fix (the real testnet case) — the row keeps the legacy claim, so the fix's gate
//     refuses the exit instead of clamping the curve to zero: the position keeps its principal
//     (settlement returns it in full, the live-market floor does not apply there), the curve keeps
//     pricing, and the inconsistency surfaces as a loud assert rather than as free tokens.
BOOST_AUTO_TEST_CASE(pm_lmsr_partial_withdraw_curve_drain_and_stale_row_gate) {
    const std::vector<int64_t> q0 = {0, 0, 0}; // no bets: the drain is pure LP bookkeeping
    enum leg_t { LEGACY = 0, FIXED = 1, STALE_GATE = 2 };
    for (int leg = 0; leg < 3; ++leg) {
        const leg_t mode = (leg_t)leg;
        auto gp = make_genesis_params(0xA160u + (uint64_t)leg, 1);
        virtual_clock clk(sim_start());
        simulated_node n("pm-lp-drain" + std::to_string(leg), gp, clk);
        fc::time_point_sec when = clk.now() - fc::seconds(CHAIN_BLOCK_INTERVAL);
        hf14(n, gp, when); oracle(n, gp, when);
        if (mode == FIXED) set_pm_audit_fix(n, true);

        const int64_t unit = n.db().get_validator_schedule_object().median_props.pm_min_liquidity.amount.value;
        pm_create_market_operation cm;
        cm.creator = gp.initiator_name; cm.oracle = gp.initiator_name;
        cm.market_type = 1; cm.outcomes = {"A", "B", "C"}; cm.url = "criteria";
        cm.liquidity = asset(share_type(unit), TOKEN_SYMBOL);
        cm.lmsr_b = lmsr::lmsr_b_from_liquidity(unit, 3);
        cm.betting_expiration = n.head_block_time() + fc::seconds(3600);
        cm.result_expiration = n.head_block_time() + fc::seconds(7200);
        apply(n, gp, when, cm, gp.initiator_key);
        const pm_market_id_type mid(0);
        const pm_liquidity_id_type seed(0), topup(1);
        const int64_t seed_b = n.db().get<pm_market_object>(mid).lmsr_b.value;
        BOOST_REQUIRE_GT(seed_b, 1); // the exact half-share split below assumes a non-degenerate b

        pm_add_liquidity_operation add;
        add.provider = gp.initiator_name; add.market_id = 0;
        add.amount = asset(share_type(unit * 3), TOKEN_SYMBOL);
        apply(n, gp, when, add, gp.initiator_key);
        const int64_t added_b = n.db().get<pm_liquidity_object>(topup).b_share.value;
        BOOST_REQUIRE_EQUAL(added_b, seed_b * 3); // top-up share is exactly proportional
        BOOST_REQUIRE_EQUAL(n.db().get<pm_market_object>(mid).lmsr_b.value, seed_b * 4);

        const int64_t w1 = unit * 3 / 2;
        const int64_t b_removed = added_b * w1 / (unit * 3); // mirrors the evaluator's floored share
        BOOST_REQUIRE_GT(b_removed, 0);
        pm_withdraw_liquidity_operation w;
        w.provider = gp.initiator_name; w.liquidity_id = topup._id;
        w.amount = asset(share_type(w1), TOKEN_SYMBOL);
        apply(n, gp, when, w, gp.initiator_key);
        BOOST_REQUIRE_EQUAL(n.db().get<pm_market_object>(mid).lmsr_b.value, seed_b * 4 - b_removed);
        BOOST_REQUIRE_EQUAL(n.db().get<pm_liquidity_object>(topup).amount.value, w1);
        if (mode == FIXED)
            BOOST_REQUIRE_EQUAL(n.db().get<pm_liquidity_object>(topup).b_share.value, added_b - b_removed);
        else // the legacy divergence the whole case is about
            BOOST_REQUIRE_EQUAL(n.db().get<pm_liquidity_object>(topup).b_share.value, added_b);

        if (mode == STALE_GATE) set_pm_audit_fix(n, true); // fix arrives AFTER the state was broken
        const auto balance_before = n.db().get_account(gp.initiator_name).balance;
        w.amount = asset(0, TOKEN_SYMBOL); // 0 = the rest of the position

        if (mode == STALE_GATE) {
            BOOST_CHECK_THROW(apply(n, gp, when, w, gp.initiator_key), std::runtime_error);
            // Refused, not clamped: row, curve and balance are exactly as the legacy path left them,
            // and the market keeps pricing.
            BOOST_CHECK_EQUAL(n.db().get<pm_market_object>(mid).lmsr_b.value, seed_b * 4 - b_removed);
            BOOST_CHECK_EQUAL(n.db().get<pm_liquidity_object>(topup).b_share.value, added_b);
            BOOST_CHECK_EQUAL(n.db().get<pm_liquidity_object>(topup).amount.value, w1);
            BOOST_CHECK_EQUAL(n.db().get<pm_liquidity_object>(topup).status, 0);
            BOOST_CHECK_EQUAL(n.db().get_account(gp.initiator_name).balance.amount.value,
                              balance_before.amount.value);
            BOOST_CHECK_GT(lmsr::lmsr_price(q0, n.db().get<pm_market_object>(mid).lmsr_b.value, 0), 0);
            BOOST_CHECK_GT(lmsr::lmsr_buy_cost(q0, n.db().get<pm_market_object>(mid).lmsr_b.value, 0, unit), 0);
            continue;
        }

        apply(n, gp, when, w, gp.initiator_key);
        const int64_t final_b = n.db().get<pm_market_object>(mid).lmsr_b.value;
        if (mode == LEGACY) {
            BOOST_CHECK_LT(final_b, 0);
            // The row asked for more b than the curve held even before the exit — that gap IS the
            // drain; the negative b is only how it surfaces.
            BOOST_CHECK_LT(seed_b * 4 - b_removed - added_b, 0);
            BOOST_CHECK_EQUAL(lmsr::lmsr_price(q0, final_b, 0), 0);          // every outcome free
            BOOST_CHECK_EQUAL(lmsr::lmsr_buy_cost(q0, final_b, 0, unit), 0); // a bet costs nothing
            BOOST_CHECK_GT(n.db().get<pm_market_object>(mid).liquidity_sum.value, 0); // capital parked
        } else {
            BOOST_CHECK_EQUAL(final_b, seed_b); // back to the untouched row's b
            BOOST_CHECK_EQUAL(active_rows_b_share(n, mid), final_b);
            BOOST_CHECK_EQUAL(n.db().get<pm_liquidity_object>(seed).b_share.value, final_b);
            BOOST_CHECK_EQUAL(n.db().get<pm_liquidity_object>(topup).status, 3);
            BOOST_CHECK_GT(lmsr::lmsr_price(q0, final_b, 0), 0);
            BOOST_CHECK_GT(lmsr::lmsr_buy_cost(q0, final_b, 0, unit), 0);
        }
    }
}
