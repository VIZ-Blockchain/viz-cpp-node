// HF16: the no-contest fee is frozen when the oracle ANNOUNCES the void, not when the market
// settles. Without the freeze the bill is priced from whatever median the parameter vote has
// reached by settlement time — a price the oracle (and everyone else) only learns after the
// announcement. The regression pins all three states the flag distinguishes:
//   * a row announced BEFORE the fork keeps the legacy median-at-settlement pricing,
//   * a row announced AFTER the fork charges the frozen amount even if the vote moves,
//   * a frozen amount of 0 stays 0 (never falls back to a later vote).
// Only BUILD_TESTNET can reach HF14/HF16 with the single-validator harness (see CMakeLists).
#include <boost/test/unit_test.hpp>
#include "simulated_node.hpp"
#include "genesis_factory.hpp"
#include "virtual_clock.hpp"
#include <graphene/chain/database.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/validator_objects.hpp>
#include <graphene/protocol/block_vote.hpp>
#include <graphene/chain/pm_objects.hpp>
#include <graphene/protocol/pm_operations.hpp>
#include <graphene/protocol/chain_operations.hpp>
#ifdef BUILD_TESTNET
#include <graphene/protocol/config_testnet.hpp>
#else
#include <graphene/protocol/config.hpp>
#endif
#include <fc/crypto/sha256.hpp>

using namespace consensus_sim;
using namespace graphene::chain;
using namespace graphene::protocol;

namespace {

account_name_type next_validator(simulated_node& n) { return n.db().get_scheduled_validator(1); }

fc::ecc::private_key key_for(const genesis_params& gp, const account_name_type& v) {
    return (v == gp.initiator_name) ? gp.initiator_key : gp.genesis_witness_key;
}

// HF16 requires a QC of head before the next block. The harness has no p2p,
// so every scheduled validator (initiator + genesis) votes for head directly;
// with two scheduled the quorum is two.
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

// Identical operation batches signed by the same key in the same block hash to the same trx_id;
// the expiration is the only free field, so `nonce` shifts it.
signed_transaction sign_ops(const std::vector<operation>& ops,
                            const fc::ecc::private_key& key, const simulated_node& node,
                            uint32_t nonce = 0) {
    signed_transaction tx;
    tx.set_reference_block(node.head_block_id());
    tx.set_expiration(node.head_block_time() + fc::seconds(60 + (nonce % 1800)));
    for (const auto& op : ops) tx.operations.emplace_back(op);
    tx.sign(key, node.chain_id());
    return tx;
}

authority single_key_auth(const public_key_type& k) {
    authority a; a.weight_threshold = 1; a.key_auths[k] = 1; return a;
}

fc::ecc::private_key derive_key(const std::string& name) {
    return fc::ecc::private_key::regenerate(fc::sha256::hash(name));
}

// Register the initiator as a staked, self-voted validator and advance until HF14 activates.
bool bring_to_hf14(simulated_node& node, const genesis_params& gp, fc::time_point_sec& when) {
    try {
        produce(node, gp, when); // block 1 (committee gap-filler)
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

void create_and_fund(simulated_node& node, const genesis_params& gp, fc::time_point_sec& when,
                     const std::string& name, const fc::ecc::private_key& key, share_type liquid) {
    const auto pub = key.get_public_key();
    const auto& mp = node.db().get_validator_schedule_object().median_props;
    account_create_operation ac;
    ac.fee = mp.account_creation_fee;
    ac.delegation = asset(0, SHARES_SYMBOL);
    ac.creator = gp.initiator_name;
    ac.new_account_name = name;
    ac.master = single_key_auth(pub);
    ac.active = single_key_auth(pub);
    ac.regular = single_key_auth(pub);
    ac.memo_key = pub;
    transfer_operation tr;
    tr.from = gp.initiator_name; tr.to = name; tr.amount = asset(liquid, TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({ac, tr}, gp.initiator_key, node));
    produce(node, gp, when);
}

// Blocks sufficient for the settle cron to traverse the dispute grace from an announcement.
int cron_grace_blocks(simulated_node& node) {
    const uint64_t grace = node.db().get_validator_schedule_object().median_props.pm_dispute_grace_sec;
    return (int)(grace / node.db().block_interval()) + 500;
}

// Push a PM parameter vote and advance until the median actually reflects it. The harness runs a
// single validator, so its own vote IS the median once the tally recomputes.
void set_pm_median(simulated_node& node, const genesis_params& gp, fc::time_point_sec& when,
                   int64_t dispute_fee, uint16_t penalty_percent, uint32_t grace_sec) {
    chain_properties_pm props;
    props.pm_dispute_fee             = asset(share_type(dispute_fee), TOKEN_SYMBOL);
    props.pm_dispute_grace_sec       = grace_sec;
    props.pm_no_contest_penalty_percent = penalty_percent;
    versioned_chain_properties_update_operation vp;
    vp.owner = gp.initiator_name; vp.props = props;
    node.push_pending_transaction(sign_ops({vp}, gp.initiator_key, node));
    const auto& mp = node.db().get_validator_schedule_object().median_props;
    for (int i = 0; i < 120 && (mp.pm_dispute_fee.amount.value != dispute_fee
                                || mp.pm_no_contest_penalty_percent != penalty_percent); ++i)
        produce(node, gp, when);
    BOOST_REQUIRE_EQUAL(mp.pm_dispute_fee.amount.value, dispute_fee);
    BOOST_REQUIRE_EQUAL(mp.pm_no_contest_penalty_percent, penalty_percent);
}

// Create + accept a binary market, place two opposing bets of `unit`, return its id.
pm_market_id_type active_market_with_bets(simulated_node& node, const genesis_params& gp,
                                          fc::time_point_sec& when,
                                          const fc::ecc::private_key& orac_key,
                                          const std::string& creator,
                                          const fc::ecc::private_key& alice_key,
                                          const fc::ecc::private_key& bob_key,
                                          int64_t unit) {
    pm_create_market_operation cm;
    cm.creator = creator; cm.oracle = "orac";
    cm.market_type = 0; cm.outcomes = {"A", "B"}; cm.url = "criteria";
    cm.liquidity = asset(share_type(unit * 4), TOKEN_SYMBOL);
    cm.betting_expiration = node.head_block_time() + fc::seconds(60);
    cm.result_expiration  = node.head_block_time() + fc::seconds(120);
    cm.dispute_mode = 0;
    node.push_pending_transaction(sign_ops({cm}, gp.initiator_key, node, (uint32_t)node.head_block_num()));
    produce(node, gp, when);
    const auto mid = node.db().get_index<pm_market_index>().indices().get<by_id>().rbegin()->id;

    pm_oracle_accept_market_operation acc;
    acc.oracle = "orac"; acc.market_id = mid._id; acc.accept = true;
    node.push_pending_transaction(sign_ops({acc}, orac_key, node));
    produce(node, gp, when);

    pm_place_bet_operation ba;
    ba.account = "alice"; ba.market_id = mid._id; ba.side = 0; ba.outcome_index = -1;
    ba.amount = asset(share_type(unit), TOKEN_SYMBOL); ba.mode = 0;
    pm_place_bet_operation bb = ba;
    bb.account = "bob"; bb.side = 1;
    node.push_pending_transaction(sign_ops({ba}, alice_key, node));
    node.push_pending_transaction(sign_ops({bb}, bob_key, node));
    produce(node, gp, when);
    return mid;
}

void announce_no_contest(simulated_node& node, const genesis_params& gp, fc::time_point_sec& when,
                         const fc::ecc::private_key& orac_key, const pm_market_id_type& mid) {
    pm_no_contest_operation nc;
    nc.oracle = "orac"; nc.market_id = mid._id; nc.reason = "cannot judge";
    node.push_pending_transaction(sign_ops({nc}, orac_key, node));
    produce(node, gp, when);
}

} // namespace

BOOST_AUTO_TEST_CASE(pm_no_contest_fee_three_states) {
    auto gp = make_genesis_params(0xA16C0u, 1);
    fc::time_point_sec start(fc::time_point::now());
    if (fc::time_point_sec(CHAIN_HARDFORK_14_TIME) > start) start = fc::time_point_sec(CHAIN_HARDFORK_14_TIME);
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node node("pm-nc-freeze", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    if (!bring_to_hf14(node, gp, when)) {
        BOOST_TEST_MESSAGE("HF14 not reachable; skipping no-contest fee freeze.");
        return;
    }
    const int64_t unit = node.db().get_validator_schedule_object().median_props.pm_min_liquidity.amount.value;
    const int64_t min_insurance =
        node.db().get_validator_schedule_object().median_props.pm_min_oracle_insurance.amount.value;
    BOOST_REQUIRE_GE(min_insurance, unit * 10);   // the legacy leg below charges 8u in one go

    auto alice_key = derive_key("alice"), bob_key = derive_key("bob");
    create_and_fund(node, gp, when, "alice", alice_key, share_type(unit * 4));
    create_and_fund(node, gp, when, "bob",   bob_key,   share_type(unit * 4));

    auto orac_key = derive_key("orac");
    const int64_t reg_fee = node.db().get_validator_schedule_object().median_props.pm_oracle_registration_fee.amount.value;
    create_and_fund(node, gp, when, "orac", orac_key, share_type(min_insurance + reg_fee));
    pm_oracle_register_operation oreg;
    oreg.owner = "orac";
    oreg.insurance = asset(share_type(min_insurance), TOKEN_SYMBOL);
    oreg.fixed_fee = asset(0, TOKEN_SYMBOL); oreg.rules_url = "";
    node.push_pending_transaction(sign_ops({oreg}, orac_key, node));
    produce(node, gp, when);

    // Announcement-time price: 50% of a 2u dispute fee = 1u.
    set_pm_median(node, gp, when, unit * 2, 5000, 3600);

    // Market 0: announced BEFORE the fork. Legacy rows must keep the pre-fork pricing rule.
    const auto m0 = active_market_with_bets(node, gp, when, orac_key, gp.initiator_name,
                                            alice_key, bob_key, unit);
    announce_no_contest(node, gp, when, orac_key, m0);
    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(m0).no_contest_fee_frozen, false);
    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(m0).no_contest_fee.value, 0);

    // The fork activates while market 0 sits in its dispute window: the row carries no frozen
    // amount, so no seed pass is needed and settlement falls back to the median (legacy).
    node.db().set_hardfork(CHAIN_HARDFORK_16, true);
    BOOST_REQUIRE(node.db().has_hardfork(CHAIN_HARDFORK_16));

    // Market 1: announced UNDER the fork at the same 2u/50% median → frozen 1u.
    const auto m1 = active_market_with_bets(node, gp, when, orac_key, gp.initiator_name,
                                            alice_key, bob_key, unit);
    announce_no_contest(node, gp, when, orac_key, m1);
    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(m1).no_contest_fee_frozen, true);
    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(m1).no_contest_fee.value, unit);

    // Market 2: announced with a 0% penalty → frozen ZERO. The flag must still be set, otherwise
    // a later vote would silently attach a price to an already-announced void.
    set_pm_median(node, gp, when, unit * 2, 0, 3600);
    const auto m2 = active_market_with_bets(node, gp, when, orac_key, gp.initiator_name,
                                            alice_key, bob_key, unit);
    announce_no_contest(node, gp, when, orac_key, m2);
    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(m2).no_contest_fee_frozen, true);
    BOOST_CHECK_EQUAL(node.db().get<pm_market_object>(m2).no_contest_fee.value, 0);

    // Move the median AFTER all three announcements: 8u at 100%. Legacy market 0 should be charged
    // the new price (pre-fork behaviour of a row that never froze); frozen markets must not move.
    set_pm_median(node, gp, when, unit * 8, 10000, 3600);

    const auto& oidx = node.db().get_index<pm_oracle_index>().indices().get<by_owner>();
    const int64_t insurance_before = oidx.find(account_name_type("orac"))->insurance.value;
    const int64_t nc_slashed_before = oidx.find(account_name_type("orac"))->no_contest_slashed_total.value;

    for (int i = 0; i < cron_grace_blocks(node) &&
                    (node.db().get<pm_market_object>(m0).payout_status != 3 ||
                     node.db().get<pm_market_object>(m1).payout_status != 3 ||
                     node.db().get<pm_market_object>(m2).payout_status != 3); ++i)
        produce(node, gp, when);

    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(m0).payout_status, 3);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(m1).payout_status, 3);
    BOOST_REQUIRE_EQUAL(node.db().get<pm_market_object>(m2).payout_status, 3);

    const int64_t insurance_after = oidx.find(account_name_type("orac"))->insurance.value;
    const int64_t nc_slashed_after = oidx.find(account_name_type("orac"))->no_contest_slashed_total.value;

    // Legacy row: charged the CURRENT median (8u), exactly as before the fork.
    // Frozen rows: charged what they announced (1u and 0), despite the same median move.
    BOOST_TEST_MESSAGE("no-contest fees: legacy=" << (unit * 8) << " frozen=" << unit
                       << " zero=0; insurance " << insurance_before << "->" << insurance_after);
    BOOST_CHECK_EQUAL(insurance_before - insurance_after, unit * 9);
    BOOST_CHECK_EQUAL(nc_slashed_after - nc_slashed_before, unit * 9);
}
