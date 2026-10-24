// HF16 (#6737, Q#3103=A): the block slot shrinks from 3 s to 1 s at the fork block. The switch must be
// seamless at the boundary:
//   * block_interval() flips 3 -> 1 exactly when HF16 applies, and the next slot is head + 1 s;
//   * rounds are re-anchored at the fork block with no jump: round_of(fork block) is the pre-fork
//     round, and every later 1 s block advances the round by exactly one;
//   * 1 s blocks after the fork are applied with no missed slots;
//   * block-denominated chain props keep their wall-time meaning (Q#3108=A: x3), and the median
//     still holds the scaled value after a schedule round recomputes it from the stored votes.
// Only BUILD_TESTNET can reach HF14/HF16 with the single-validator harness (see CMakeLists).
#include <boost/test/unit_test.hpp>
#include "simulated_node.hpp"
#include "genesis_factory.hpp"
#include "virtual_clock.hpp"
#include <graphene/chain/database.hpp>
#include <graphene/chain/fork_database.hpp>
#include <graphene/chain/database_exceptions.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/validator_objects.hpp>
#include <graphene/protocol/block_vote.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <graphene/protocol/config.hpp>

using namespace consensus_sim;
using namespace graphene::chain;
using namespace graphene::protocol;

namespace {

account_name_type next_validator(simulated_node& n) { return n.db().get_scheduled_validator(1); }

fc::ecc::private_key key_for(const genesis_params& gp, const account_name_type& v) {
    return (v == gp.initiator_name) ? gp.initiator_key : gp.genesis_witness_key;
}

// The harness has no p2p: every scheduled validator votes for head directly (see test_pm_no_contest_fee).
void vote_for_head(simulated_node& n, const genesis_params& gp) {
    auto& db = n.db();
    if (!db.has_hardfork(CHAIN_HARDFORK_16) || db.head_block_num() == 0 || db.get_block_qc(db.head_block_num()))
        return;
    const auto head = db.fetch_block_by_number(db.head_block_num());
    const uint32_t round = db.round_of(head->timestamp);
    const auto digest = graphene::protocol::block_vote_digest(db.get_chain_id(), head->block_num(), head->id(), round);
    const auto& wso = db.get_validator_schedule_object();
    for (int i = 0; i < wso.num_scheduled_validators; i += CHAIN_BLOCK_VALIDATOR_REPEAT) {
        const auto v = wso.current_shuffled_validators[i];
        db.push_block_vote(head->block_num(), head->id(), round, v, key_for(gp, v).sign_compact(digest));
    }
}

void produce(simulated_node& n, const genesis_params& gp, fc::time_point_sec& when) {
    vote_for_head(n, gp);
    when += fc::seconds(n.db().block_interval());
    const auto v = next_validator(n);
    n.produce_block(v, key_for(gp, v), when);
}

signed_transaction sign_ops(const std::vector<operation>& ops,
                            const fc::ecc::private_key& key, const simulated_node& node) {
    signed_transaction tx;
    tx.set_reference_block(node.head_block_id());
    tx.set_expiration(node.head_block_time() + fc::seconds(60));
    for (const auto& op : ops) tx.operations.emplace_back(op);
    tx.sign(key, node.chain_id());
    return tx;
}

bool bring_to_hf14(simulated_node& node, const genesis_params& gp, fc::time_point_sec& when) {
    try {
        produce(node, gp, when);
        const share_type bal = node.db().get_account(gp.initiator_name).balance.amount;
        transfer_to_vesting_operation tv;
        tv.from = gp.initiator_name; tv.to = gp.initiator_name;
        tv.amount = asset(share_type(bal.value / 2), TOKEN_SYMBOL);
        validator_update_operation vu;
        vu.owner = gp.initiator_name; vu.url = "viz";
        vu.block_signing_key = gp.initiator_key.get_public_key();
        account_validator_vote_operation vv;
        vv.account = gp.initiator_name; vv.validator = gp.initiator_name; vv.approve = true;
        node.push_pending_transaction(sign_ops({tv, vu, vv}, gp.initiator_key, node));
        for (int i = 0; i < 300 && !node.db().has_hardfork(CHAIN_HARDFORK_14); ++i)
            produce(node, gp, when);
    } catch (const std::exception& e) {
        BOOST_TEST_MESSAGE(std::string("could not reach HF14 (harness/genesis limit): ") + e.what());
    }
    return node.db().has_hardfork(CHAIN_HARDFORK_14);
}

} // namespace

BOOST_AUTO_TEST_SUITE(hf16_boundary_suite)

BOOST_AUTO_TEST_CASE(slot_and_round_switch_at_fork_block) {
    auto gp = make_genesis_params(0xB16B0u, 1);
    fc::time_point_sec start(fc::time_point::now());
    if (fc::time_point_sec(CHAIN_HARDFORK_14_TIME) > start) start = fc::time_point_sec(CHAIN_HARDFORK_14_TIME);
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("hf16-boundary", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    BOOST_REQUIRE(bring_to_hf14(node, gp, when));
    auto& db = node.db();

    // Pre-fork: 3 s slots, no anchor.
    BOOST_REQUIRE(!db.has_hardfork(CHAIN_HARDFORK_16));
    BOOST_CHECK_EQUAL(db.block_interval(), (uint32_t)CHAIN_BLOCK_INTERVAL);
    BOOST_CHECK(db.get_dynamic_global_properties().round_anchor_time == fc::time_point_sec());
    const auto fork_time = db.head_block_time();
    const uint32_t pre_round = db.round_of(fork_time);
    BOOST_CHECK_EQUAL(pre_round, fork_time.sec_since_epoch() / CHAIN_BLOCK_INTERVAL);
    const uint32_t pre_recalc = db.get_validator_schedule_object().median_props.inflation_recalc_period;

    db.set_hardfork(CHAIN_HARDFORK_16, true);
    BOOST_REQUIRE(db.has_hardfork(CHAIN_HARDFORK_16));

    // The fork block is the anchor: same round as before the switch, next slot is +1 s.
    const auto& dgp = db.get_dynamic_global_properties();
    BOOST_CHECK_EQUAL(db.block_interval(), (uint32_t)CHAIN_BLOCK_INTERVAL_HF16);
    BOOST_CHECK(dgp.round_anchor_time == fork_time);
    BOOST_CHECK_EQUAL(dgp.round_anchor_round, pre_round);
    BOOST_CHECK_EQUAL(db.round_of(fork_time), pre_round);
    BOOST_CHECK(db.get_slot_time(1) == fork_time + fc::seconds(CHAIN_BLOCK_INTERVAL_HF16));
    BOOST_CHECK_EQUAL(db.get_validator_schedule_object().median_props.inflation_recalc_period,
                      std::min<uint64_t>(uint64_t(pre_recalc) * 3, std::numeric_limits<uint32_t>::max()));

    // 1 s blocks across more than one schedule round: no missed slot, round +1 per block.
    const uint32_t head0 = db.head_block_num();
    uint32_t prev_round = pre_round;
    fc::time_point_sec prev_time = fork_time;
    const int n = 3 * CHAIN_MAX_VALIDATORS;
    for (int i = 0; i < n; ++i) {
        produce(node, gp, when);
        const auto t = db.head_block_time();
        BOOST_REQUIRE(t == prev_time + fc::seconds(CHAIN_BLOCK_INTERVAL_HF16));
        const uint32_t r = db.round_of(t);
        BOOST_REQUIRE_EQUAL(r, prev_round + 1);
        prev_round = r; prev_time = t;
    }
    BOOST_CHECK_EQUAL(db.head_block_num(), head0 + n);
    // Pre-anchor times still map with the 3 s formula (parents/QCs just before the fork).
    BOOST_CHECK_EQUAL(db.round_of(fork_time - fc::seconds(CHAIN_BLOCK_INTERVAL)), pre_round - 1);
    // The median recomputed from the stored (scaled) votes keeps the x3 value.
    BOOST_CHECK_EQUAL(db.get_validator_schedule_object().median_props.inflation_recalc_period,
                      std::min<uint64_t>(uint64_t(pre_recalc) * 3, std::numeric_limits<uint32_t>::max()));
}


// A whole deferred chain can link before the database applies its first block.
// fork_db's pruning reference must not discard the applied branch's LIB ancestors.
BOOST_AUTO_TEST_CASE(deferred_chain_cascade_retains_irreversible_history) {
    auto gp = make_genesis_params(0xCA5CADEu, 1);
    fc::time_point_sec start(fc::time_point::now());
    if (fc::time_point_sec(CHAIN_HARDFORK_14_TIME) > start)
        start = fc::time_point_sec(CHAIN_HARDFORK_14_TIME);
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node source("cascade-source", gp, clk);
    simulated_node follower("cascade-follower", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);
    BOOST_REQUIRE(bring_to_hf14(source, gp, when));
    for (const auto& block : source.canonical_blocks_from(1))
        follower.receive_block(block);
    BOOST_REQUIRE(follower.head_block_id() == source.head_block_id());
    BOOST_REQUIRE(follower.db().has_hardfork(CHAIN_HARDFORK_14));
    source.db().set_hardfork(CHAIN_HARDFORK_16, true);
    follower.db().set_hardfork(CHAIN_HARDFORK_16, true);
    const uint32_t base = source.head_block_num();
    const uint32_t old_lib = follower.last_irreversible_block_num();
    for (int i = 0; i < 12; ++i) produce(source, gp, when);
    const auto blocks = source.canonical_blocks_from(base + 1);
    BOOST_REQUIRE_EQUAL(blocks.size(), 12u);
    // Cache descendants while their first parent is still missing.
    for (size_t i = 1; i < blocks.size(); ++i) {
        bool deferred = false;
        try { follower.db().get_fork_db().push_block(blocks[i]); }
        catch (const graphene::chain::unlinkable_block_exception&) { deferred = true; }
        BOOST_REQUIRE(deferred);
    }
    follower.receive_block(blocks.front());
    BOOST_REQUIRE_EQUAL(follower.head_block_num(), source.head_block_num());
    BOOST_REQUIRE(follower.head_block_id() == source.head_block_id());
    BOOST_REQUIRE_GT(follower.last_irreversible_block_num(), old_lib);
    // Every newly finalized ancestor must remain available, including via block_log
    // after fork_db's window shrinks back to the fully applied head.
    for (uint32_t n = old_lib + 1; n <= follower.last_irreversible_block_num(); ++n) {
        const auto ours = follower.db().fetch_block_by_number(n);
        const auto theirs = source.db().fetch_block_by_number(n);
        BOOST_REQUIRE(ours.valid());
        BOOST_REQUIRE(theirs.valid());
        BOOST_CHECK(ours->id() == theirs->id());
    }
}

BOOST_AUTO_TEST_CASE(committee_request_ceiling_tracks_current_supply) {
    auto gp = make_genesis_params(0xDA016u, 1);
    fc::time_point_sec start(fc::time_point::now());
    if (fc::time_point_sec(CHAIN_HARDFORK_14_TIME) > start)
        start = fc::time_point_sec(CHAIN_HARDFORK_14_TIME);
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("committee-ceiling", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);
    BOOST_REQUIRE(bring_to_hf14(node, gp, when));
    auto& db = node.db();
    committee_worker_create_request_operation op;
    op.creator = gp.initiator_name; op.worker = gp.initiator_name;
    op.url = "https://example.org/committee-ceiling";
    op.required_amount_min = asset(0, TOKEN_SYMBOL);
    op.duration = COMMITTEE_MIN_DURATION;
    op.required_amount_max = asset(COMMITTEE_MAX_REQUIRED_AMOUNT + 1, TOKEN_SYMBOL);
    // Stateless validation must permit amounts above the old ceiling; the database
    // supplies both the hardfork version and the live supply.
    BOOST_CHECK_NO_THROW(op.validate());
    BOOST_CHECK_THROW(node.push_pending_transaction(sign_ops({op}, gp.initiator_key, node)), std::exception);
    db.set_hardfork(CHAIN_HARDFORK_16, true);
    const int64_t raw_supply = CHAIN_INIT_SUPPLY * 2 + 99;
    db.modify(db.get_dynamic_global_properties(), [&](dynamic_global_property_object& p) {
        p.current_supply = asset(raw_supply, TOKEN_SYMBOL);
    });
    op.required_amount_max = asset(raw_supply / 100 + 1, TOKEN_SYMBOL);
    BOOST_CHECK_THROW(node.push_pending_transaction(sign_ops({op}, gp.initiator_key, node)), std::exception);
    // Recompute for the next request rather than freezing the activation supply.
    db.modify(db.get_dynamic_global_properties(), [&](dynamic_global_property_object& p) {
        p.current_supply = asset(CHAIN_INIT_SUPPLY, TOKEN_SYMBOL);
    });
    op.required_amount_max = asset(raw_supply / 100, TOKEN_SYMBOL);
    BOOST_CHECK_THROW(node.push_pending_transaction(sign_ops({op}, gp.initiator_key, node)), std::exception);
    db.modify(db.get_dynamic_global_properties(), [&](dynamic_global_property_object& p) {
        p.current_supply = asset(raw_supply, TOKEN_SYMBOL);
    });
    BOOST_CHECK_NO_THROW(node.push_pending_transaction(sign_ops({op}, gp.initiator_key, node)));
}

BOOST_AUTO_TEST_SUITE_END()
