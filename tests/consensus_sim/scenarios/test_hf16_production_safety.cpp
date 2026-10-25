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
#include <graphene/chain/operation_notification.hpp>
#include <graphene/chain/validator_objects.hpp>
#include <graphene/protocol/block_vote.hpp>
#include <graphene/protocol/chain_operations.hpp>
#ifdef BUILD_TESTNET
#include <graphene/protocol/config_testnet.hpp>
#else
#include <graphene/protocol/config.hpp>
#endif
#include <graphene/protocol/get_config.hpp>
#include <fc/io/raw.hpp>

using namespace consensus_sim;
using namespace graphene::chain;
using namespace graphene::protocol;

namespace {

account_name_type next_validator(simulated_node& n) { return n.db().get_scheduled_validator(1); }

fc::ecc::private_key key_for(const genesis_params& gp, const account_name_type& v) {
    return (v == gp.genesis_witness_name || v == account_name_type("validator-two"))
        ? gp.genesis_witness_key : gp.initiator_key;
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

// Isolated native production regressions: no p2p, no external chain state.
struct production_fixture {
    genesis_params gp = make_genesis_params(0x5AFE16u, 1);
    fc::time_point_sec start = fc::time_point_sec(CHAIN_HARDFORK_14_TIME) + fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk{start};
    simulated_node node{"production-safety", gp, clk};
    fc::time_point_sec when = start - fc::seconds(CHAIN_BLOCK_INTERVAL);

    production_fixture() {
        BOOST_REQUIRE(bring_to_hf14(node, gp, when));
        node.db().set_hardfork(CHAIN_HARDFORK_16, true);
        for (int i = 0; i < 3; ++i) produce(node, gp, when);
    }

    block_equivocation_proof queue_proof(uint32_t round, const account_name_type& voter) {
        auto& db = node.db();
        signed_block first = *db.fetch_block_by_number(db.head_block_num());
        // Keep speculative votes clear of the next few real heights: a one-voter
        // fixture otherwise creates a QC for a fake immediate child as a side effect.
        if (round > db.round_of(first.timestamp))
            for (int i = 0; i < 32; ++i) first.previous = first.id();
        signed_block second = first;
        second.timestamp += fc::seconds(1);
        const auto key = key_for(gp, voter);
        block_equivocation_proof proof;
        proof.validator = voter; proof.round = round;
        const auto first_digest = block_vote_digest(db.get_chain_id(), first.block_num(), first.id(), round);
        const auto second_digest = block_vote_digest(db.get_chain_id(), second.block_num(), second.id(), round);
        proof.first = block_vote_evidence{first.block_num(), first.id(), key.sign_compact(first_digest)};
        proof.second = block_vote_evidence{second.block_num(), second.id(), key.sign_compact(second_digest)};
        BOOST_REQUIRE(db.push_block_vote(proof.first.block_num, proof.first.block_id, round, voter, proof.first.signature));
        BOOST_REQUIRE(!db.push_block_vote(proof.second.block_num, proof.second.block_id, round, voter, proof.second.signature));
        return proof;
    }
};

bool carries_proof(const signed_block& b) {
    for (const auto& e : b.extensions) if (e.which() == 5) return true;
    return false;
}

} // namespace

BOOST_AUTO_TEST_SUITE(hf16_production_safety_suite)

BOOST_AUTO_TEST_CASE(production_defers_future_evidence_until_strictly_later_round) {
    production_fixture f;
    auto& db = f.node.db();
    const auto voter = db.get_validator_schedule_object().current_shuffled_validators[0];
    const uint32_t round = db.round_of(f.when) + 2;
    f.queue_proof(round, voter);
    for (int i = 0; i < 2; ++i) {
        produce(f.node, f.gp, f.when);
        BOOST_CHECK(!carries_proof(*db.fetch_block_by_number(db.head_block_num())));
    }
    // The equivocator already voted for another block in this round, so its
    // local tally correctly refuses another vote for the real head. Supply the
    // independently signed parent certificate directly; consensus still verifies
    // every signature (no skip flag and no mutation of the vote cache).
    const auto head = *db.fetch_block_by_number(db.head_block_num());
    block_qc_certificate cert;
    cert.block_num = head.block_num(); cert.block_id = head.id(); cert.round = db.round_of(head.timestamp);
    const auto digest = block_vote_digest(db.get_chain_id(), cert.block_num, cert.block_id, cert.round);
    const auto& wso = db.get_validator_schedule_object();
    for (int i = 0; i < wso.num_scheduled_validators; i += CHAIN_BLOCK_VALIDATOR_REPEAT) {
        const auto v = wso.current_shuffled_validators[i];
        cert.signatures.emplace(v, key_for(f.gp, v).sign_compact(digest));
    }
    f.when += fc::seconds(db.block_interval());
    const auto producer = db.get_scheduled_validator(1);
    db.generate_block(f.when, producer, key_for(f.gp, producer), database::skip_nothing, {}, cert);
    BOOST_CHECK(carries_proof(*db.fetch_block_by_number(db.head_block_num())));
    BOOST_CHECK_EQUAL(db.get_validator(voter).last_equivocation_round, round);
}

BOOST_AUTO_TEST_CASE(production_discards_evidence_signed_with_replaced_key) {
    production_fixture f;
    auto& db = f.node.db();
    const auto voter = db.get_validator_schedule_object().current_shuffled_validators[0];
    const uint32_t round = db.round_of(f.when) - 1;
    f.queue_proof(round, voter);
    const auto punished = db.get_validator(voter).last_equivocation_round;
    // Fixture-only committed key rotation. Native signature verification, not a
    // skip flag, must make the queued old-key evidence ineligible.
    const auto replacement = fc::ecc::private_key::regenerate(fc::sha256::hash("production-rotation"));
    db.modify(db.get_validator(voter), [&](validator_object& v) { v.signing_key = replacement.get_public_key(); });
    const auto head = *db.fetch_block_by_number(db.head_block_num());
    const auto digest = block_vote_digest(db.get_chain_id(), head.block_num(), head.id(), db.round_of(head.timestamp));
    const auto& wso = db.get_validator_schedule_object();
    for (int i = 0; i < wso.num_scheduled_validators; i += CHAIN_BLOCK_VALIDATOR_REPEAT) {
        const auto v = wso.current_shuffled_validators[i];
        const auto key = v == voter ? replacement : key_for(f.gp, v);
        BOOST_REQUIRE(db.push_block_vote(head.block_num(), head.id(), db.round_of(head.timestamp), v, key.sign_compact(digest)));
    }
    f.when += fc::seconds(db.block_interval());
    const auto producer = db.get_scheduled_validator(1);
    const auto block = db.generate_block(f.when, producer,
        producer == voter ? replacement : key_for(f.gp, producer), database::skip_nothing);
    BOOST_CHECK(!carries_proof(block));
    BOOST_CHECK_EQUAL(db.get_validator(voter).last_equivocation_round, punished);
}

BOOST_AUTO_TEST_CASE(production_budgets_certificates_and_proofs_before_transactions) {
    production_fixture f;
    auto& db = f.node.db();
    const auto voter = db.get_validator_schedule_object().current_shuffled_validators[0];
    const auto proof = f.queue_proof(db.round_of(f.when), voter);
    vote_for_head(f.node, f.gp);
    transfer_operation transfer;
    transfer.from = f.gp.initiator_name; transfer.to = f.gp.genesis_witness_name;
    transfer.amount = asset(1, TOKEN_SYMBOL); transfer.memo = std::string(512, 'x');
    const auto tx = sign_ops({transfer}, f.gp.initiator_key, f.node);
    const auto producer = db.get_scheduled_validator(1);
    const auto cert = db.parent_qc_for_next_block(producer);
    BOOST_REQUIRE(cert.valid());
    const auto legacy_estimate = fc::raw::pack_size(signed_block_header()) + 4;
    const uint32_t limit = std::max<uint32_t>(legacy_estimate + fc::raw::pack_size(tx) + 16,
                                             fc::raw::pack_size(tx) + 256);
    signed_block projected;
    projected.previous = db.head_block_id(); projected.validator = producer;
    projected.timestamp = f.when + fc::seconds(db.block_interval());
    projected.extensions.insert(block_header_extensions(*cert));
    projected.extensions.insert(block_header_extensions(proof));
    projected.transactions.push_back(tx);
    BOOST_REQUIRE_GT(fc::raw::pack_size(projected), limit);
    // Fixture-only near-full block. Set the committed cap BEFORE creating a
    // pending undo session; otherwise reset() would undo this fixture change.
    // The cap still admits the transaction through the ordinary 256-byte reserve.
    db.modify(db.get_dynamic_global_properties(), [&](dynamic_global_property_object& p) { p.maximum_block_size = limit; });
    f.node.push_pending_transaction(tx);
    produce(f.node, f.gp, f.when);
    const auto block = *db.fetch_block_by_number(db.head_block_num());
    BOOST_CHECK_LE(fc::raw::pack_size(block), limit);
    BOOST_CHECK(block.transactions.empty());
    BOOST_CHECK(carries_proof(block));
    // Observe retention, then let the next block consume the same pending pool.
    // The one-shot proof is gone, so the transaction fits at the SAME cap.
    BOOST_REQUIRE_EQUAL(db._pending_tx.size(), 1u);
    BOOST_CHECK(db._pending_tx.front().id() == tx.id());
    produce(f.node, f.gp, f.when);
    const auto next = *db.fetch_block_by_number(db.head_block_num());
    BOOST_CHECK_LE(fc::raw::pack_size(next), limit);
    BOOST_REQUIRE_EQUAL(next.transactions.size(), 1u);
    BOOST_CHECK(next.transactions[0].id() == tx.id());
}


BOOST_AUTO_TEST_CASE(production_preserves_pending_signing_key_rotation) {
    production_fixture f;
    auto& db = f.node.db();
    vote_for_head(f.node, f.gp);
    const auto owner = db.get_scheduled_validator(1);
    const auto old_key = key_for(f.gp, owner);
    const auto new_key = fc::ecc::private_key::regenerate(fc::sha256::hash("pending-production-rotation"));
    validator_update_operation op;
    op.owner = owner;
    op.url = "https://example.invalid/validator";
    op.block_signing_key = new_key.get_public_key();
    const auto tx = sign_ops({op}, old_key, f.node);
    f.node.push_pending_transaction(tx);
    BOOST_REQUIRE(db.get_validator(owner).signing_key == new_key.get_public_key());
    f.when += fc::seconds(db.block_interval());
    const auto block = db.generate_block(f.when, owner, old_key, database::skip_nothing);
    BOOST_REQUIRE_EQUAL(block.transactions.size(), 1u);
    BOOST_CHECK(block.transactions.front().id() == tx.id());
    BOOST_CHECK(db.get_validator(owner).signing_key == new_key.get_public_key());
}

BOOST_AUTO_TEST_SUITE_END()
