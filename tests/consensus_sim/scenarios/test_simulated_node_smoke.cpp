#include <boost/test/unit_test.hpp>
#include "simulated_node.hpp"
#include "genesis_factory.hpp"
#include "virtual_clock.hpp"

#include <graphene/protocol/config.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/database_exceptions.hpp>
#include <graphene/chain/db_with.hpp>
#include <graphene/protocol/operations.hpp>

using namespace consensus_sim;

BOOST_AUTO_TEST_SUITE(simulated_node_smoke_suite)

// Milestone 1 produces blocks as the single witness VIZ's init_genesis
// actually creates (CHAIN_INITIATOR_NAME, "viz"). genesis_factory's
// witness_keys vector is reserved for Milestone 3 — see the comment in
// simulated_node::register_witness_keys_.

BOOST_AUTO_TEST_CASE(single_node_produces_one_block) {
    auto params = make_genesis_params(0xCAFE, 1);
    auto t0 = fc::time_point_sec::from_iso_string("2026-01-01T00:00:00");
    virtual_clock clk(t0);

    simulated_node node("node-0", params, clk);

    auto block = node.produce_block(params.genesis_witness_name,
                                    params.genesis_witness_key,
                                    t0 + CHAIN_BLOCK_INTERVAL);

    BOOST_CHECK_EQUAL(node.head_block_num(), 1u);
    BOOST_CHECK(node.head_block_id() == block.id());
    BOOST_CHECK(node.head_block_time() == t0 + fc::seconds(CHAIN_BLOCK_INTERVAL));
}

BOOST_AUTO_TEST_CASE(single_node_produces_100_blocks) {
    auto params = make_genesis_params(0xBEEF, 1);
    auto t0 = fc::time_point_sec::from_iso_string("2026-01-01T00:00:00");
    virtual_clock clk(t0);

    simulated_node node("node-0", params, clk);

    fc::time_point_sec t = t0;
    for (int i = 0; i < 100; ++i) {
        t += CHAIN_BLOCK_INTERVAL;
        clk.advance_to(t);
        node.produce_block(params.genesis_witness_name, params.genesis_witness_key, t);
    }
    BOOST_CHECK_EQUAL(node.head_block_num(), 100u);
}

// canonical_blocks_from exposes full signed_block bodies so the equivocation
// fault injector can catch a shadow node up to canonical state and then fork it.
// Behavior contract: returns blocks at heights [from_height, head] in ascending
// order; returns empty when from_height > head or == 0; preserves block ids
// byte-for-byte so the shadow's receive_block accepts them as canonical.
BOOST_AUTO_TEST_CASE(canonical_blocks_from_returns_full_bodies) {
    auto params = make_genesis_params(0xC011, 1);
    auto t0 = fc::time_point_sec::from_iso_string("2026-01-01T00:00:00");
    virtual_clock clk(t0);

    simulated_node node("node-0", params, clk);

    std::vector<graphene::protocol::signed_block> produced;
    fc::time_point_sec t = t0;
    for (int i = 0; i < 5; ++i) {
        t += CHAIN_BLOCK_INTERVAL;
        clk.advance_to(t);
        produced.push_back(
            node.produce_block(params.genesis_witness_name,
                               params.genesis_witness_key, t));
    }

    auto all = node.canonical_blocks_from(1);
    BOOST_REQUIRE_EQUAL(all.size(), produced.size());
    for (size_t i = 0; i < produced.size(); ++i) {
        BOOST_CHECK(all[i].id() == produced[i].id());
    }

    auto tail = node.canonical_blocks_from(3);
    BOOST_REQUIRE_EQUAL(tail.size(), 3u);
    BOOST_CHECK(tail.front().id() == produced[2].id());
    BOOST_CHECK(tail.back().id() == produced.back().id());

    BOOST_CHECK(node.canonical_blocks_from(0).empty());
    BOOST_CHECK(node.canonical_blocks_from(6).empty());
}

// Deliberately violate only the name-index ordering in an isolated fixture. Restore the
// original keys before teardown; production code must never mutate indexed keys this way.
BOOST_AUTO_TEST_CASE(account_name_index_damage_is_typed_corruption) {
    using namespace graphene::chain;
    using namespace graphene::protocol;
    auto params = make_genesis_params(0xC077, 1);
    virtual_clock clk(fc::time_point_sec::from_iso_string("2026-01-01T00:00:00"));
    simulated_node node("corruption-probe", params, clk);
    auto& db = node.db();
    db.with_strong_write_lock([&]() {
        const auto skip = database::skip_validate_operations |
            database::skip_authority_check | database::skip_tapos_check;
        transfer_operation absent;
        absent.from = "absent-account"; absent.to = params.initiator_name;
        absent.amount = asset(1, TOKEN_SYMBOL);
        signed_transaction absent_tx;
        absent_tx.operations.emplace_back(absent);
        bool ordinary_rejection = false;
        try { db._push_transaction(absent_tx, skip); }
        catch (const shared_memory_corruption_exception&) {
            BOOST_FAIL("A nonexistent account must not trigger snapshot recovery");
        }
        catch (const fc::exception&) { ordinary_rejection = true; }
        BOOST_REQUIRE(ordinary_rejection);
        BOOST_CHECK_THROW(graphene::chain::detail::without_pending_transactions(db, skip,
            std::vector<signed_transaction>{absent_tx}, []() { FC_ASSERT(false, "callback failure"); }),
            fc::assert_exception); // Invalid pending tx is culled; the original failure survives.

        const auto& ids = db.get_index<account_index>().indices().get<by_id>();
        struct restore_names {
            std::vector<std::pair<account_object*, account_name_type>> rows;
            ~restore_names() { for (const auto& row : rows) row.first->name = row.second; }
        } restore;
        for (const auto& a : ids)
            restore.rows.emplace_back(const_cast<account_object*>(&a), a.name);
        BOOST_REQUIRE(restore.rows.size() > 1);
        for (size_t i = 0; i < restore.rows.size(); ++i)
            restore.rows[i].first->name = restore.rows[restore.rows.size() - 1 - i].second;

        account_name_type missing;
        for (const auto& a : ids)
            if (!db.find_account(a.name)) { missing = a.name; break; }
        BOOST_REQUIRE_MESSAGE(!std::string(missing).empty(), "Fault must actually break name lookup");

        transfer_operation op;
        op.from = missing; op.to = params.initiator_name;
        op.amount = asset(1, TOKEN_SYMBOL);
        signed_transaction tx;
        tx.operations.emplace_back(op);
        // Isolate the account-index diagnostic from authority/signature validation.
        BOOST_CHECK_THROW(db._push_transaction(tx, skip),
            shared_memory_corruption_exception);
        BOOST_CHECK_THROW(graphene::chain::detail::without_pending_transactions(db, skip,
            std::vector<signed_transaction>{tx}, []() {}), shared_memory_corruption_exception);
        db._popped_tx.push_back(tx);
        BOOST_CHECK_THROW(graphene::chain::detail::without_pending_transactions(db, skip,
            std::vector<signed_transaction>{}, []() {}), shared_memory_corruption_exception);
        db._popped_tx.clear();
    });
}

BOOST_AUTO_TEST_SUITE_END()
