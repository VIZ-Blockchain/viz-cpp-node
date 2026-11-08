// Behavioural coverage for database::switch_head_to_certified_sibling() (#257, #262).
//
// Why this file exists: the only other check of that function is a wiring test that greps the source
// (tests/chain/test_certified_sibling_wiring.py), and the network stand cannot build its trigger state
// deterministically (measured 08.11.2026 on the 21-validator qc-bench: isolating a producer does create
// a sibling at one height, but on heal the node reorgs to the certified branch before it produces, so
// the switch line never appears). Here the state is built directly, with native primitives:
//
//   1. head X at height H, produced by validator A, carrying NO QC;
//   2. sibling Y at the same height H with the same parent, produced by validator B;
//   3. Y delivered into the node's fork_db — head stays X (equal length keeps the current head);
//   4. quorum votes for Y pushed, so get_block_qc(H) names Y while head is still X.
//
// Then the switch must move head to Y. That is exactly the 05.11.2026 mainnet shape (database.cpp
// above the function: two siblings gathered QCs, head was a third, QC-less one, nobody could build).
//
// Negatives: without quorum votes there is no QC and the switch must refuse and leave head alone;
// a second call after a successful switch must also refuse (head IS certified now).
#include <boost/test/unit_test.hpp>
#include "simulated_node.hpp"
#include "genesis_factory.hpp"
#include "virtual_clock.hpp"
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/fork_database.hpp>
#include <graphene/chain/validator_objects.hpp>
#include <graphene/protocol/block_vote.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#ifdef BUILD_TESTNET
#include <graphene/protocol/config_testnet.hpp>

namespace {
using namespace consensus_sim;
using namespace graphene::chain;
using namespace graphene::protocol;

using keys_type = std::map<account_name_type, fc::ecc::private_key>;

std::set<account_name_type> scheduled(database& db) {
    const auto& s = db.get_validator_schedule_object();
    std::set<account_name_type> out;
    for (int i = 0; i < s.num_scheduled_validators; i += CHAIN_BLOCK_VALIDATOR_REPEAT)
        out.insert(s.current_shuffled_validators[i]);
    return out;
}

const fc::ecc::private_key& key_for(const genesis_params& gp, const keys_type& keys,
                                    const account_name_type& v) {
    const auto it = keys.find(v);
    if (it != keys.end()) return it->second;
    // Testnet genesis with -DCHAIN_TESTNET_VALIDATORS=N creates viz, viz1..viz(N-1), all scheduled
    // with the SAME initiator signing key (database.cpp init_genesis). That is the schedule this
    // fixture runs on: no registration needed, and the quorum is the real one for the build
    // (15 of 21 locally, 3 of 4 in CI).
    const std::string name(v);
    if (name == "viz" || name.compare(0, 3, "viz") == 0) return gp.initiator_key;
    if (v == gp.genesis_witness_name) return gp.genesis_witness_key;
    BOOST_REQUIRE_MESSAGE(false, "PREMISE: no private key for scheduled validator " << name);
    return gp.initiator_key;
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

// The harness has no p2p: the scheduled validators vote for head directly (same helper the other
// HF16 scenarios use). Kept separate from production here, because the fixture needs a block that
// deliberately carries NO QC.
void vote_for_head(simulated_node& n, const genesis_params& gp, const keys_type& keys) {
    auto& db = n.db();
    if (!db.has_hardfork(CHAIN_HARDFORK_16) || db.head_block_num() == 0 || db.get_block_qc(db.head_block_num()))
        return;
    const auto head = db.fetch_block_by_id(db.head_block_id());
    if (!head.valid()) return;
    const uint32_t round = db.round_of(head->timestamp);
    const auto digest = block_vote_digest(db.get_chain_id(), head->block_num(), head->id(), round);
    const auto voters = scheduled(db);
    for (const auto& v : voters) {
        if (v == head->validator) continue;
        db.push_block_vote(head->block_num(), head->id(), round, v, key_for(gp, keys, v).sign_compact(digest));
    }
}

// Voting happens BEFORE production, because HF16 refuses to hand the turn over without a QC of the
// parent (database.cpp: "No QC for head ... cannot hand over the turn"). The block that comes out is
// certified ONLY if someone then votes for it — which is exactly the freedom this fixture uses.
signed_block produce(simulated_node& n, const genesis_params& gp, const keys_type& keys,
                     fc::time_point_sec& when) {
    vote_for_head(n, gp, keys);
    when += fc::seconds(n.db().block_interval());
    const auto v = n.db().get_scheduled_validator(n.db().get_slot_at_time(when));
    const auto& key = key_for(gp, keys, v);
    return n.produce_block(v, key, when);
}

bool bring_to_hf14(simulated_node& node, const genesis_params& gp, const keys_type& keys,
                   fc::time_point_sec& when) {
    try {
        produce(node, gp, keys, when);
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
            produce(node, gp, keys, when);
    } catch (const std::exception& e) {
        BOOST_TEST_MESSAGE(std::string("could not reach HF14 (harness/genesis limit): ") + e.what());
    }
    return node.db().has_hardfork(CHAIN_HARDFORK_14);
}

void replay_to(simulated_node& from, simulated_node& to) {
    for (const auto& block : from.canonical_blocks_from(1)) to.receive_block(block);
    BOOST_REQUIRE_MESSAGE(to.head_block_id() == from.head_block_id(),
                          "PREMISE: follower did not replay the producer's chain");
}

void push_quorum_votes(simulated_node& n, const genesis_params& gp, const keys_type& keys,
                       const signed_block& target, uint32_t limit) {
    auto& db = n.db();
    const uint32_t round = db.round_of(target.timestamp);
    const auto digest = block_vote_digest(db.get_chain_id(), target.block_num(), target.id(), round);
    uint32_t pushed = 0;
    for (const auto& v : scheduled(db)) {
        if (v == target.validator) continue;      // a producer never votes for its own block
        if (pushed >= limit) break;
        BOOST_REQUIRE_MESSAGE(db.push_block_vote(target.block_num(), target.id(), round, v,
                                                 key_for(gp, keys, v).sign_compact(digest)),
                              "PREMISE: quorum vote was refused");
        ++pushed;
    }
    BOOST_REQUIRE_EQUAL(pushed, limit);
}

fc::time_point_sec fixture_start() {
    fc::time_point_sec start(fc::time_point::now());
    if (fc::time_point_sec(CHAIN_HARDFORK_14_TIME) > start) start = fc::time_point_sec(CHAIN_HARDFORK_14_TIME);
    return start + fc::seconds(CHAIN_BLOCK_INTERVAL);
}

struct fixture {
    genesis_params gp;
    keys_type keys;
    fc::time_point_sec when;
    virtual_clock clk;
    std::unique_ptr<simulated_node> a, b;

    explicit fixture(uint64_t seed)
        : gp(make_genesis_params(seed, 4)),
          when(fixture_start() - fc::seconds(CHAIN_BLOCK_INTERVAL)),
          clk(fixture_start()) {
        a = std::make_unique<simulated_node>("sibling-producer", gp, clk);
        keys[gp.initiator_name] = gp.initiator_key;
        BOOST_REQUIRE_MESSAGE(bring_to_hf14(*a, gp, keys, when), "PREMISE: HF14 not reached");
        b = std::make_unique<simulated_node>("sibling-rival", gp, clk);
        replay_to(*a, *b);
        a->db().set_hardfork(CHAIN_HARDFORK_16, true);
        b->db().set_hardfork(CHAIN_HARDFORK_16, true);
    }

    // X on `a` (nothing votes for it afterwards, so no QC names it), Y on `b` at the same height
    // and parent. Both producers first certify the shared parent H — that is what lets them produce.
    std::pair<signed_block, signed_block> sibling_pair() {
        auto x = produce(*a, gp, keys, when);
        fc::time_point_sec when_b = x.timestamp;
        auto y = produce(*b, gp, keys, when_b);
        when = when_b;                      // the shared clock moved with B; keep the fixture in step
        BOOST_REQUIRE_EQUAL(x.block_num(), y.block_num());
        BOOST_REQUIRE(x.previous == y.previous);
        BOOST_REQUIRE(x.id() != y.id());
        BOOST_REQUIRE(a->head_block_id() == x.id());
        // Y must reach the producer's fork_db without displacing the equal-length head. The
        // outcome code is NOT the assertion here: push_block returns false for a block that is
        // stored but not applied, and receive_block() labels every false as rejected_duplicate.
        // The fact that matters is in fork_db itself.
        const auto outcome = a->receive_block(y);
        BOOST_TEST_MESSAGE("sibling delivery outcome=" << static_cast<int>(outcome));
        const auto sib = a->db().get_fork_db().fetch_block(y.id());
        BOOST_REQUIRE_MESSAGE(sib && !sib->invalid, "PREMISE: sibling was not stored in fork_db");
        BOOST_REQUIRE_MESSAGE(a->head_block_id() == x.id(),
                              "PREMISE: receiving an equal-length sibling moved the head");
        return {x, y};
    }
};

} // namespace

BOOST_AUTO_TEST_SUITE(certified_sibling_switch_suite)

BOOST_AUTO_TEST_CASE(switches_head_to_certified_sibling) {
    BOOST_REQUIRE_MESSAGE(CHAIN_MAX_VALIDATORS >= 4,
                          "PREMISE: this build caps the schedule below four identities");
    fixture f(0xC3E7u);
    auto [x, y] = f.sibling_pair();
    auto& db = f.a->db();
    const uint32_t num = y.block_num();
    const auto quorum = database::block_qc_quorum(
        uint32_t(db.get_validator_schedule_object().num_scheduled_validators) / CHAIN_BLOCK_VALIDATOR_REPEAT);
    BOOST_REQUIRE_GT(quorum, 0u);

    push_quorum_votes(*f.a, f.gp, f.keys, y, quorum);
    const auto qc = db.get_block_qc(num);
    BOOST_REQUIRE_MESSAGE(qc.valid(), "PREMISE: quorum votes did not form a QC");
    BOOST_REQUIRE(qc->block_id == y.id());
    BOOST_REQUIRE_MESSAGE(db.head_block_id() == x.id(), "PREMISE: head must still be the QC-less sibling");

    const uint32_t lib_before = f.a->last_irreversible_block_num();
    BOOST_REQUIRE_MESSAGE(db.switch_head_to_certified_sibling(),
                          "switch refused a head whose sibling has a quorum QC");
    BOOST_TEST_MESSAGE("switched #" << num << " to the certified sibling: head now " << db.head_block_id().str());
    BOOST_CHECK(db.head_block_id() == y.id());
    BOOST_CHECK_EQUAL(db.head_block_num(), num);
    BOOST_CHECK_GE(f.a->last_irreversible_block_num(), lib_before);
    // The applied branch and fork_db must agree: the next block is built on Y.
    const auto applied = db.fetch_block_by_number(num);
    BOOST_REQUIRE(applied.valid());
    BOOST_CHECK(applied->id() == y.id());

    // Second call: head IS the certified block now, nothing to switch.
    BOOST_CHECK(!db.switch_head_to_certified_sibling());
    BOOST_CHECK(db.head_block_id() == y.id());
}

BOOST_AUTO_TEST_CASE(keeps_head_without_quorum) {
    BOOST_REQUIRE_MESSAGE(CHAIN_MAX_VALIDATORS >= 4,
                          "PREMISE: this build caps the schedule below four identities");
    fixture f(0xB0A7u);
    auto [x, y] = f.sibling_pair();
    auto& db = f.a->db();
    const auto quorum = database::block_qc_quorum(
        uint32_t(db.get_validator_schedule_object().num_scheduled_validators) / CHAIN_BLOCK_VALIDATOR_REPEAT);
    BOOST_REQUIRE_GT(quorum, 1u);
    push_quorum_votes(*f.a, f.gp, f.keys, y, quorum - 1);
    BOOST_REQUIRE_MESSAGE(!db.get_block_qc(y.block_num()).valid(),
                          "PREMISE: sub-quorum votes must not produce a QC");

    BOOST_CHECK(!db.switch_head_to_certified_sibling());
    BOOST_CHECK(db.head_block_id() == x.id());
    BOOST_CHECK_EQUAL(db.head_block_num(), y.block_num());
}

BOOST_AUTO_TEST_SUITE_END()
#endif
