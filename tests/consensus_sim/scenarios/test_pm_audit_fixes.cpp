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
// ONLY test state: fake the processed marker inside this disposable simulation.
// Production has CHAIN_NUM_HARDFORKS=14, so neither voting nor set_hardfork can do this.
void opt_in_unscheduled_fix(simulated_node& n) {
    BOOST_REQUIRE(!n.db().has_hardfork(CHAIN_PM_AUDIT_FIX_HARDFORK));
    const auto& hf = n.db().get_hardfork_property_object();
    n.db().modify(hf, [&](hardfork_property_object& h) {
        h.processed_hardforks.push_back(n.head_block_time());
    });
    BOOST_REQUIRE(n.db().has_hardfork(CHAIN_PM_AUDIT_FIX_HARDFORK));
}
void oracle(simulated_node& n, const genesis_params& gp, fc::time_point_sec& when) {
    pm_oracle_register_operation op;
    op.owner = gp.initiator_name;
    op.insurance = n.db().get_validator_schedule_object().median_props.pm_min_oracle_insurance;
    op.fixed_fee = asset(0, TOKEN_SYMBOL);
    apply(n, gp, when, op, gp.initiator_key);
}
}

BOOST_AUTO_TEST_CASE(pm_lmsr_partial_lp_replay_and_future_fix) {
    for (bool fixed : {false, true}) {
        auto gp = make_genesis_params(fixed ? 0xA15A : 0xA15B, 1);
        fc::time_point_sec start(fc::time_point::now());
        if (fc::time_point_sec(CHAIN_HARDFORK_14_TIME) > start) start = fc::time_point_sec(CHAIN_HARDFORK_14_TIME);
        start += fc::seconds(CHAIN_BLOCK_INTERVAL);
        virtual_clock clk(start);
        simulated_node n(fixed ? "pm-lp-fixed" : "pm-lp-legacy", gp, clk);
        fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);
        hf14(n, gp, when); oracle(n, gp, when);
        if (fixed) opt_in_unscheduled_fix(n);
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
        fc::time_point_sec start(fc::time_point::now());
        if (fc::time_point_sec(CHAIN_HARDFORK_14_TIME) > start) start = fc::time_point_sec(CHAIN_HARDFORK_14_TIME);
        start += fc::seconds(CHAIN_BLOCK_INTERVAL);
        virtual_clock clk(start);
        simulated_node n(fixed ? "pm-batch-fixed" : "pm-batch-legacy", gp, clk);
        fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);
        hf14(n, gp, when); oracle(n, gp, when);
        if (fixed) opt_in_unscheduled_fix(n);
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
