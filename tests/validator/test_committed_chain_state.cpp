// Native integration companion to test_signing_cache.py, built by consensus_sim.
#include <boost/test/unit_test.hpp>
#include "simulated_node.hpp"
#include "genesis_factory.hpp"
#include "virtual_clock.hpp"
#include <graphene/chain/validator_objects.hpp>
#include <graphene/protocol/chain_operations.hpp>
#ifdef BUILD_TESTNET
#include <graphene/protocol/config_testnet.hpp>
#else
#include <graphene/protocol/config.hpp>
#endif

using namespace consensus_sim;
using namespace graphene::protocol;

BOOST_AUTO_TEST_SUITE(validator_committed_state_suite)

BOOST_AUTO_TEST_CASE(committed_signal_precedes_pending_restoration) {
    auto gp = make_genesis_params(0xCA5E16u, 1);
    auto t0 = fc::time_point_sec::from_iso_string("2026-01-01T00:00:00");
    virtual_clock clk(t0);
    simulated_node producer("committed-producer", gp, clk);
    simulated_node receiver("committed-receiver", gp, clk);
    auto produce_next = [&](fc::time_point_sec when) {
        const auto owner = producer.db().get_scheduled_validator(1);
        BOOST_REQUIRE(owner == gp.genesis_witness_name || owner == gp.initiator_name);
        return producer.produce_block(owner, owner == gp.genesis_witness_name ?
                                      gp.genesis_witness_key : gp.initiator_key, when);
    };
    auto first = produce_next(t0 + CHAIN_BLOCK_INTERVAL);
    BOOST_REQUIRE(receiver.receive_block(first) == block_outcome::accepted_extends_head);
    auto &db = receiver.db();
    const auto *before = db.find_validator(gp.initiator_name);
    const bool existed = before != nullptr;
    const auto old_key = before ? before->signing_key : public_key_type();
    const auto rotated_key = fc::ecc::private_key::regenerate(fc::sha256::hash("pending-key")).get_public_key();
    validator_update_operation update;
    update.owner = gp.initiator_name;
    update.url = "viz";
    update.block_signing_key = rotated_key;
    signed_transaction tx;
    tx.set_reference_block(receiver.head_block_id());
    tx.set_expiration(receiver.head_block_time() + fc::seconds(60));
    tx.operations.emplace_back(update);
    tx.sign(gp.initiator_key, receiver.chain_id());
    receiver.push_pending_transaction(tx);
    BOOST_REQUIRE(db.get_validator(gp.initiator_name).signing_key == rotated_key);

    unsigned committed_calls = 0, applied_calls = 0;
    boost::signals2::scoped_connection committed = db.committed_chain_state.connect([&]() {
        ++committed_calls;
        const auto *v = db.find_validator(gp.initiator_name);
        BOOST_CHECK_EQUAL(v != nullptr, existed);
        if (v) BOOST_CHECK(v->signing_key == old_key);
        BOOST_CHECK_EQUAL(db.head_block_num(), 2u);
    });
    boost::signals2::scoped_connection applied = db.applied_block.connect([&](const signed_block &) {
        ++applied_calls;
        BOOST_CHECK_EQUAL(committed_calls, 1u);
        BOOST_CHECK(db.get_validator(gp.initiator_name).signing_key == rotated_key);
    });
    // Producer never saw the receiver's pending rotation; its next block is empty.
    auto second = produce_next(t0 + 2 * CHAIN_BLOCK_INTERVAL);
    BOOST_REQUIRE(second.transactions.empty());
    BOOST_REQUIRE(receiver.receive_block(second) == block_outcome::accepted_extends_head);
    BOOST_CHECK_EQUAL(committed_calls, 1u);
    BOOST_CHECK_EQUAL(applied_calls, 1u);
    BOOST_CHECK(db.get_validator(gp.initiator_name).signing_key == rotated_key);
}

BOOST_AUTO_TEST_SUITE_END()
