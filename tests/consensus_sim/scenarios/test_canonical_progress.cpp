// Native canonical-progress regression with synthetic keys and controlled HF16.
// This exercises database ingress, not honest-signer or network reachability.
#include <boost/test/unit_test.hpp>
#include "simulated_node.hpp"
#include "genesis_factory.hpp"
#include "virtual_clock.hpp"
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/database_exceptions.hpp>
#include <graphene/chain/fork_database.hpp>
#include <graphene/chain/validator_objects.hpp>
#include <graphene/protocol/block_vote.hpp>
#include <graphene/protocol/chain_operations.hpp>

#ifdef BUILD_TESTNET
#include <graphene/protocol/config_testnet.hpp>

using namespace consensus_sim;
using namespace graphene::chain;
using namespace graphene::protocol;

namespace {

fc::ecc::private_key key_for(const genesis_params& gp, const account_name_type& v) {
    return (v == gp.genesis_witness_name || v == account_name_type("validator-two"))
        ? gp.genesis_witness_key : gp.initiator_key;
}

// These local helpers follow test_hf16_boundary.cpp; they are not harness exports.
void vote_for_head(simulated_node& n, const genesis_params& gp) {
    auto& db = n.db();
    if (!db.has_hardfork(CHAIN_HARDFORK_16) || !db.head_block_num() ||
        db.get_block_qc(db.head_block_num())) return;
    const auto head = db.fetch_block_by_id(db.head_block_id());
    BOOST_REQUIRE(head.valid());
    const auto round = db.round_of(head->timestamp);
    const auto digest = block_vote_digest(db.get_chain_id(), head->block_num(), head->id(), round);
    const auto& schedule = db.get_validator_schedule_object();
    for (int i = 0; i < schedule.num_scheduled_validators; i += CHAIN_BLOCK_VALIDATOR_REPEAT) {
        const auto v = schedule.current_shuffled_validators[i];
        BOOST_REQUIRE(db.push_block_vote(head->block_num(), head->id(), round, v,
                                        key_for(gp, v).sign_compact(digest)));
    }
    BOOST_REQUIRE(db.get_block_qc(db.head_block_num()).valid());
}

signed_block produce(simulated_node& n, const genesis_params& gp, fc::time_point_sec& when) {
    vote_for_head(n, gp);
    when += fc::seconds(n.db().block_interval());
    const auto v = n.db().get_scheduled_validator(1);
    return n.produce_block(v, key_for(gp, v), when);
}

signed_transaction sign_ops(const std::vector<operation>& ops,
                            const fc::ecc::private_key& key, const simulated_node& n) {
    signed_transaction tx;
    tx.set_reference_block(n.head_block_id());
    tx.set_expiration(n.head_block_time() + fc::seconds(60));
    for (const auto& op : ops) tx.operations.emplace_back(op);
    tx.sign(key, n.chain_id());
    return tx;
}

bool bring_to_hf14(simulated_node& n, const genesis_params& gp, fc::time_point_sec& when) {
    produce(n, gp, when);
    const auto bal = n.db().get_account(gp.initiator_name).balance.amount;
    transfer_to_vesting_operation vest;
    vest.from = gp.initiator_name; vest.to = gp.initiator_name;
    vest.amount = asset(share_type(bal.value / 2), TOKEN_SYMBOL);
    validator_update_operation reg;
    reg.owner = gp.initiator_name; reg.url = "viz";
    reg.block_signing_key = gp.initiator_key.get_public_key();
    account_validator_vote_operation vote;
    vote.account = gp.initiator_name; vote.validator = gp.initiator_name; vote.approve = true;
    n.push_pending_transaction(sign_ops({vest, reg, vote}, gp.initiator_key, n));
    for (int i = 0; i < 300 && !n.db().has_hardfork(CHAIN_HARDFORK_14); ++i)
        produce(n, gp, when);
    return n.db().has_hardfork(CHAIN_HARDFORK_14);
}

} // namespace

namespace {
// Raw native DB regression, not a network or honest-signer reachability proof.
// All branch votes/keys are synthetic; HF16 is explicitly activated as in the original.
void state(const char* tag, simulated_node& n) {
    const auto fh = n.db().get_fork_db().head();
    BOOST_TEST_MESSAGE(tag << " applied=" << n.head_block_num() << ":" << n.head_block_id().str()
        << " fork=" << (fh ? fh->num : 0) << ":" << (fh ? fh->id.str() : block_id_type().str())
        << " LIB=" << n.last_irreversible_block_num()
        << " canonical_head_retained=" << n.db().get_fork_db().is_known_block(n.head_block_id())
        << " linked=" << n.db().get_fork_db().linked_size()
        << " max_size=no_public_getter(exception_records_actual_value)");
}
void progress_case(int rival_length, bool rotated, bool local_production) {
    auto gp = make_genesis_params(0xB16CEu, 2);
    fc::time_point_sec start(fc::time_point::now());
    if (fc::time_point_sec(CHAIN_HARDFORK_14_TIME) > start)
        start = fc::time_point_sec(CHAIN_HARDFORK_14_TIME);
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node branch_a("canonical-progress-affected", gp, clk);
    simulated_node branch_b("canonical-progress-rival", gp, clk);
    simulated_node replay("canonical-progress-rival-replay", gp, clk);
    simulated_node canonical("canonical-progress-producer", gp, clk);
    simulated_node control("canonical-progress-linear-control", gp, clk);
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);
    BOOST_REQUIRE(bring_to_hf14(branch_a, gp, when));

    // As in test_hf16_boundary: create a second signable validator rather than
    // trying to authorize a committee-account key update (its authority is special).
    const auto pub = gp.genesis_witness_key.get_public_key();
    authority auth; auth.weight_threshold = 1; auth.key_auths[pub] = 1;
    account_create_operation create;
    create.creator = gp.initiator_name; create.new_account_name = "validator-two";
    create.fee = branch_a.db().get_validator_schedule_object().median_props.account_creation_fee;
    create.delegation = asset(0, SHARES_SYMBOL);
    create.master = auth; create.active = auth; create.regular = auth; create.memo_key = pub;
    transfer_operation fund;
    fund.from = gp.initiator_name; fund.to = "validator-two";
    fund.amount = asset(100000000, TOKEN_SYMBOL);
    branch_a.push_pending_transaction(sign_ops({create, fund}, gp.initiator_key, branch_a));
    produce(branch_a, gp, when);
    validator_update_operation reg;
    reg.owner = "validator-two"; reg.url = "viz"; reg.block_signing_key = pub;
    account_validator_vote_operation vote;
    vote.account = gp.initiator_name; vote.validator = "validator-two"; vote.approve = true;
    auto registration = sign_ops({reg, vote}, gp.initiator_key, branch_a);
    registration.sign(gp.genesis_witness_key, branch_a.chain_id());
    branch_a.push_pending_transaction(registration);
    for (int i = 0; i < 100; ++i) {
        produce(branch_a, gp, when);
        if (branch_a.db().get_validator_schedule_object().num_scheduled_validators /
            CHAIN_BLOCK_VALIDATOR_REPEAT >= 2) break;
    }
    const auto& schedule = branch_a.db().get_validator_schedule_object();
    BOOST_REQUIRE_EQUAL(schedule.num_scheduled_validators / CHAIN_BLOCK_VALIDATOR_REPEAT, 2);
    for (int i = 0; i < schedule.num_scheduled_validators; i += CHAIN_BLOCK_VALIDATOR_REPEAT) {
        const auto v = schedule.current_shuffled_validators[i];
        BOOST_REQUIRE(v == gp.initiator_name || v == account_name_type("validator-two"));
    }
    for (const auto& b : branch_a.canonical_blocks_from(1)) {
        branch_b.receive_block(b);
        replay.receive_block(b); canonical.receive_block(b); control.receive_block(b);
    }
    BOOST_REQUIRE(branch_b.head_block_id() == branch_a.head_block_id());
    BOOST_REQUIRE(replay.head_block_id() == branch_a.head_block_id());
    for (auto* n : {&branch_a, &branch_b, &replay, &canonical, &control})
        n->db().set_hardfork(CHAIN_HARDFORK_16, true);

    for (int i = 0; i < 3; ++i) {
        const auto b = produce(branch_a, gp, when);
        branch_b.receive_block(b); replay.receive_block(b); canonical.receive_block(b); control.receive_block(b);
    }
    const auto base = branch_a.head_block_num();
    const auto ancestor = branch_a.head_block_id();
    BOOST_REQUIRE(branch_b.head_block_id() == ancestor);
    BOOST_REQUIRE(replay.head_block_id() == ancestor);
    auto b_when = when;
    const auto a_tip = produce(branch_a, gp, when);
    BOOST_REQUIRE(database::block_cert_round(a_tip) > 0);
    BOOST_REQUIRE_LE(branch_a.last_irreversible_block_num(), base);

    canonical.receive_block(a_tip); control.receive_block(a_tip);
    BOOST_REQUIRE(canonical.head_block_id() == a_tip.id());
    BOOST_REQUIRE(control.head_block_id() == a_tip.id());
    std::vector<signed_block> rival;
    if (rotated) {
    auto rotated = gp;
    rotated.initiator_key = gp.witness_keys[0].second;
    rotated.genesis_witness_key = gp.witness_keys[1].second;
    validator_update_operation rotate_one;
    rotate_one.owner = gp.initiator_name; rotate_one.url = "viz";
    rotate_one.block_signing_key = rotated.initiator_key.get_public_key();
    validator_update_operation rotate_two;
    rotate_two.owner = "validator-two"; rotate_two.url = "viz";
    rotate_two.block_signing_key = rotated.genesis_witness_key.get_public_key();
    vote_for_head(branch_b, gp); // Certify committed head before pending keys change.
    auto rotation = sign_ops({rotate_one, rotate_two}, gp.initiator_key, branch_b);
    rotation.sign(gp.genesis_witness_key, branch_b.chain_id());
    branch_b.push_pending_transaction(rotation);

    rival.push_back(produce(branch_b, gp, b_when));
    BOOST_REQUIRE_EQUAL(rival.front().transactions.size(), 1u);
    BOOST_REQUIRE(rival.front().transactions.front().id() == rotation.id());
    BOOST_REQUIRE(rival.front().previous == ancestor);
    BOOST_REQUIRE(rival.front().id() != a_tip.id());
    BOOST_REQUIRE_EQUAL(database::block_cert_round(rival.front()), database::block_cert_round(a_tip));
    for (int i = 1; i < rival_length; ++i) rival.push_back(produce(branch_b, rotated, b_when));
    } else {
        for (int i = 0; i < rival_length; ++i) {
            b_when += fc::seconds(CHAIN_EMERGENCY_UNSTICK_TIMEOUT_SEC + 1);
            const auto v = branch_b.db().get_scheduled_validator(branch_b.db().get_slot_at_time(b_when));
            rival.push_back(branch_b.produce_block(v, key_for(gp, v), b_when));
            BOOST_REQUIRE_EQUAL(database::block_cert_round(rival.back()), 0u);
        }
    }
    for (const auto& b : rival) {
        replay.db().push_block(b, database::skip_nothing);
        BOOST_REQUIRE(replay.head_block_id() == b.id());
        branch_a.db().push_block(b, database::skip_nothing);
        BOOST_REQUIRE(branch_a.db().get_fork_db().is_known_block(b.id()));
        BOOST_REQUIRE(branch_a.head_block_id() == a_tip.id());
    }
    const auto entry_lib = branch_a.last_irreversible_block_num();
    state("after-retention", branch_a);
    // Produce independently, then replay in an uncontaminated DB using skip_nothing.
    // Repeat the very same first valid child before trying later valid descendants.
    for (int i = 0; i < 4; ++i) {
        const auto candidate = produce(canonical, gp, when);
        control.db().push_block(candidate, database::skip_nothing);
        BOOST_REQUIRE(control.head_block_id() == candidate.id());
        BOOST_REQUIRE_EQUAL(control.head_block_num(), a_tip.block_num() + i + 1);
        const int attempts = i == 0 ? 2 : 1;
        for (int attempt = 0; attempt < attempts; ++attempt) {
            state("before-valid-candidate", branch_a);
            bool completed = false;
            try {
                if (local_production) {
                    vote_for_head(branch_a, gp);
                    const auto slot = branch_a.db().get_slot_at_time(candidate.timestamp);
                    const auto v = branch_a.db().get_scheduled_validator(slot);
                    const auto made = branch_a.db().generate_block(candidate.timestamp, v,
                        key_for(gp, v), database::skip_nothing);
                    completed = branch_a.head_block_id() == made.id();
                } else {
                    branch_a.db().push_block(candidate, database::skip_nothing);
                    completed = branch_a.head_block_id() == candidate.id();
                }
            } catch (const fc::exception& e) {
                BOOST_TEST_MESSAGE("candidate=" << candidate.block_num() << " attempt=" << attempt
                    << " native_exception=" << e.to_detail_string());
            }
            state("after-valid-candidate", branch_a);
            BOOST_CHECK_MESSAGE(completed, "independently valid canonical candidate did not advance applied head");
            if (completed) break;
        }
        BOOST_CHECK_EQUAL(branch_a.head_block_num(), candidate.block_num());
        const auto selected = branch_a.db().fetch_block_by_number(candidate.block_num());
        BOOST_CHECK(selected.valid() && selected->id() == candidate.id());
    }
    BOOST_REQUIRE_GT(control.last_irreversible_block_num(), entry_lib);
    BOOST_CHECK_GT(branch_a.last_irreversible_block_num(), entry_lib);
    BOOST_TEST_MESSAGE("final control_head=" << control.head_block_num()
        << " control_LIB=" << control.last_irreversible_block_num());
}
} // namespace
BOOST_AUTO_TEST_SUITE(canonical_progress_suite)
BOOST_AUTO_TEST_CASE(no_competitor_advances) { progress_case(0, false, false); }
BOOST_AUTO_TEST_CASE(short_uncertified_retention_advances) { progress_case(2, false, false); }
BOOST_AUTO_TEST_CASE(long_uncertified_retention_advances) { progress_case(5, false, false); }
BOOST_AUTO_TEST_CASE(rotated_retention_accepts_canonical_children) { progress_case(4, true, false); }
BOOST_AUTO_TEST_CASE(long_rotated_retention_accepts_canonical_children) { progress_case(5, true, false); }
BOOST_AUTO_TEST_CASE(rotated_retention_allows_local_production) { progress_case(5, true, true); }
BOOST_AUTO_TEST_SUITE_END()
#endif
