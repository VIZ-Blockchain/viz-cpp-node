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
#include <graphene/chain/emergency_exit.hpp>
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
    // The producer may vote for its own block: verify_block_vote() only checks that the voter is
    // scheduled and the signature matches (database.cpp:7368) - there is no self-vote ban. That
    // matters on small schedules: with four identities and quorum three, excluding the producer
    // would leave two voters and make a real QC unreachable (measured in CI: 2 != 3).
    for (const auto& v : scheduled(db)) {
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

// 18.11.2026 mainnet: the siblings of #86227200 never gathered a QC (27 of them, the best six
// votes of the seven a quorum needed), so the certified path above had nothing to take and every
// producer kept dropping its own head and minting a new sibling. These two cases cover the rule
// that closes that hole: converge on the sibling the votes name, QC or no QC.
BOOST_AUTO_TEST_CASE(converges_to_sibling_with_votes_but_no_qc) {
    BOOST_REQUIRE_MESSAGE(CHAIN_MAX_VALIDATORS >= 4,
                          "PREMISE: this build caps the schedule below four identities");
    fixture f(0xC0FFEEu);
    auto [x, y] = f.sibling_pair();
    auto& db = f.a->db();
    const auto quorum = database::block_qc_quorum(
        uint32_t(db.get_validator_schedule_object().num_scheduled_validators) / CHAIN_BLOCK_VALIDATOR_REPEAT);
    BOOST_REQUIRE_GT(quorum, 1u);

    push_quorum_votes(*f.a, f.gp, f.keys, y, 1);
    BOOST_REQUIRE_MESSAGE(!db.get_block_qc(y.block_num()).valid(),
                          "PREMISE: a single vote must not form a QC (otherwise this is the certified case)");
    BOOST_REQUIRE_MESSAGE(f.a->head_block_id() == x.id(), "PREMISE: head must still be the QC-less sibling");

    const auto canonical = db.canonical_sibling_id();
    BOOST_REQUIRE_MESSAGE(canonical.valid(), "the sibling carries a vote and head carries none: it is canonical");
    BOOST_CHECK(*canonical == y.id());

    BOOST_REQUIRE_MESSAGE(db.converge_to_canonical_sibling(),
                          "converge refused a sibling the network voted for while head had no votes");
    BOOST_CHECK(db.head_block_id() == y.id());
    BOOST_CHECK_EQUAL(db.head_block_num(), y.block_num());
    const auto applied = db.fetch_block_by_number(y.block_num());
    BOOST_REQUIRE(applied.valid());
    BOOST_CHECK(applied->id() == y.id());

    // Second call: head IS canonical now, nothing to move. The rule must not oscillate.
    BOOST_CHECK(!db.canonical_sibling_id().valid());
    BOOST_CHECK(!db.converge_to_canonical_sibling());
    BOOST_CHECK(db.head_block_id() == y.id());
}

BOOST_AUTO_TEST_CASE(keeps_head_when_nobody_voted_for_any_sibling) {
    BOOST_REQUIRE_MESSAGE(CHAIN_MAX_VALIDATORS >= 4,
                          "PREMISE: this build caps the schedule below four identities");
    fixture f(0x0DDF00Du);
    auto [x, y] = f.sibling_pair();
    auto& db = f.a->db();
    BOOST_REQUIRE(db.head_block_id() == x.id());

    // No votes at all: a switch on silence would be a coin flip between nodes that know
    // different subsets of the siblings, so the rule must not invent a winner.
    BOOST_CHECK(!db.canonical_sibling_id().valid());
    BOOST_CHECK(!db.converge_to_canonical_sibling());
    BOOST_CHECK(db.head_block_id() == x.id());
}

// 18.11.2026: emergency entry blanked every signing key and padded the schedule with `committee`,
// whose key no operator holds, while the certificate quorum in emergency is measured against the
// padded 21 slots. Ten keys of the fifteen that were needed meant the chain could never certify
// again. emergency_entry_allowed() is the door that refuses that shape; these cases pin the
// arithmetic at the boundary, because "almost enough keys" reads exactly like "enough" in a log.
BOOST_AUTO_TEST_CASE(emergency_entry_refuses_below_the_exit_threshold) {
    BOOST_REQUIRE_MESSAGE(CHAIN_MAX_VALIDATORS >= 4,
                          "PREMISE: this build caps the schedule below four identities");
    fixture f(0xEEEE0001u);
    f.sibling_pair();                                  // leave a normal HF16 chain behind
    auto& db = f.a->db();

    const uint32_t scheduled = uint32_t(db.get_validator_schedule_object().num_scheduled_validators
                                        / CHAIN_BLOCK_VALIDATOR_REPEAT);
    const uint32_t required = emergency_exit_threshold_effective(scheduled);

    std::vector<account_name_type> keyed;
    for (const auto& v : db.get_index<validator_index>().indices()) {
        if (v.owner == CHAIN_EMERGENCY_VALIDATOR_ACCOUNT) continue;
        if (v.signing_key != public_key_type()) keyed.push_back(v.owner);
    }

    // The guard must agree with its own arithmetic before anything is blanked: this is the line that
    // keeps the two from drifting when the threshold helpers change.
    BOOST_CHECK_EQUAL(db.emergency_entry_allowed(), keyed.size() >= required);

    if (keyed.size() < required) {
        // Builds whose floor (half of CHAIN_MAX_VALIDATORS) is above the schedule of this fixture can
        // never satisfy the exit threshold — so refusal is the correct and only verdict, there is no
        // boundary left to walk. Say so instead of skipping silently.
        BOOST_TEST_MESSAGE("schedule " << scheduled << " cannot reach exit threshold " << required
                                       << "; refusal is the expected verdict on this build");
        return;
    }
    BOOST_CHECK(db.emergency_entry_allowed());

    // At the threshold entry is still allowed: refusing there would block a mode the chain can leave.
    const auto saved = db.get_validator(keyed.front()).signing_key;
    for (size_t i = 0; i + required < keyed.size(); ++i)
        db.modify(db.get_validator(keyed[i]), [](validator_object& v) { v.signing_key = public_key_type(); });
    BOOST_CHECK_MESSAGE(db.emergency_entry_allowed(),
                        "exactly the exit threshold in keys must still allow entry");

    // One below it must refuse — that is the 18.11.2026 shape exactly.
    db.modify(db.get_validator(keyed[keyed.size() - required]), [](validator_object& v) {
        v.signing_key = public_key_type();
    });
    BOOST_CHECK_MESSAGE(!db.emergency_entry_allowed(),
                        "entry into emergency must be refused when the keys cannot reach the exit threshold");

    // And the refusal is not sticky: returning the keys re-opens the door, because the guard reads
    // chain state and nothing else.
    db.modify(db.get_validator(keyed.front()), [&](validator_object& v) { v.signing_key = saved; });
    BOOST_CHECK(db.emergency_entry_allowed());
}

// Adversarial review of the convergence rule (18.11.2026, after #280 merged): a vote is one per
// validator per ROUND, not per height, so a sibling can hold MORE distinct voters than a head that
// already carries a certificate - no fork required, just a rebuild. Without an own-QC guard the
// ranking would pull the node off a certified block, which is the network's answer. The certified
// path has that guard (switch_head_to_certified_sibling); this case is the same promise for the
// voted path, and its mirror case proves the test is not vacuous.
BOOST_AUTO_TEST_CASE(keeps_certified_head_even_when_a_sibling_has_more_votes) {
    BOOST_REQUIRE_MESSAGE(CHAIN_MAX_VALIDATORS >= 4,
                          "PREMISE: this build caps the schedule below four identities");
    fixture f(0xC0DE01u);
    auto [x, y] = f.sibling_pair();
    auto& db = f.a->db();
    const auto quorum = database::block_qc_quorum(
        uint32_t(db.get_validator_schedule_object().num_scheduled_validators) / CHAIN_BLOCK_VALIDATOR_REPEAT);
    BOOST_REQUIRE_MESSAGE(quorum > 1, "PREMISE: quorum must allow a certificate on this build");
    BOOST_REQUIRE_MESSAGE(db.head_block_id() == x.id(), "PREMISE: head must be x");

    // x becomes CERTIFIED: exactly a quorum of voters in x's own round.
    push_quorum_votes(*f.a, f.gp, f.keys, x, quorum);
    const auto qc = db.get_block_qc(x.block_num());
    BOOST_REQUIRE_MESSAGE(qc.valid() && qc->block_id == x.id(),
                          "PREMISE: x must hold its own QC (otherwise this is the voted case, not this one)");

    const size_t voters = scheduled(db).size();
    if (voters <= quorum) {
        // Reached on the 4-validator CI arm, measured: the schedule holds 3 names and the quorum for
        // 3 is 3. "More distinct voters than a certified head" then needs two votes from one
        // validator in one round, which the vote key (validator, round) forbids - the shape is not
        // constructible here, and pretending otherwise is how a green test proves nothing. The guard
        // still has to be there for the mainnet shape (21 slots, quorum 15, slack of six), and the
        // thing that holds it on THIS build is the source-level pin plus its mutation
        // (tests/chain/test_certified_sibling_wiring.py, converge-off-certified-head), not a case
        // that silently cannot run.
        BOOST_TEST_MESSAGE("schedule " << voters << " equals the quorum " << quorum
                           << ": a sibling cannot out-vote a certified head on this build (one vote per "
                              "validator per round); the guard is held by the wiring mutation");
        BOOST_CHECK_MESSAGE(!db.canonical_sibling_id().valid(),
                            "a certified head must not be a candidate even when no sibling can out-vote it");
        BOOST_CHECK(!db.converge_to_canonical_sibling());
        BOOST_CHECK(db.head_block_id() == x.id());
        return;
    }

    // The sibling collects ONE MORE distinct voter than the quorum, in a LATER round - legal,
    // because the vote key is (validator, round). Every scheduled validator may vote again.
    const uint32_t later_round = db.round_of(x.timestamp) + 1;
    const auto digest = block_vote_digest(db.get_chain_id(), y.block_num(), y.id(), later_round);
    uint32_t pushed = 0;
    for (const auto& v : scheduled(db)) {
        BOOST_REQUIRE_MESSAGE(db.push_block_vote(y.block_num(), y.id(), later_round, v,
                                                 key_for(f.gp, f.keys, v).sign_compact(digest)),
                              "PREMISE: a later-round vote for the sibling was refused");
        ++pushed;
    }
    BOOST_REQUIRE_MESSAGE(pushed > quorum,
                          "PREMISE: the sibling needs more distinct voters than the quorum, got " << pushed);

    // The guard: a certified head is not a candidate.
    BOOST_CHECK_MESSAGE(!db.canonical_sibling_id().valid(),
                        "a head holding its own QC must not be ranked against siblings at all");
    BOOST_CHECK_MESSAGE(!db.converge_to_canonical_sibling(),
                        "converge must refuse to leave a certified head, however many votes a sibling shows");
    BOOST_CHECK(db.head_block_id() == x.id());
    BOOST_CHECK_EQUAL(db.head_block_num(), x.block_num());
}

BOOST_AUTO_TEST_CASE(moves_to_a_sibling_with_more_votes_when_head_has_no_qc) {
    BOOST_REQUIRE_MESSAGE(CHAIN_MAX_VALIDATORS >= 4,
                          "PREMISE: this build caps the schedule below four identities");
    fixture f(0xC0DE02u);
    auto [x, y] = f.sibling_pair();
    auto& db = f.a->db();
    const auto quorum = database::block_qc_quorum(
        uint32_t(db.get_validator_schedule_object().num_scheduled_validators) / CHAIN_BLOCK_VALIDATOR_REPEAT);
    BOOST_REQUIRE_MESSAGE(quorum > 1, "PREMISE: quorum must allow a near-quorum");
    BOOST_REQUIRE_MESSAGE(db.head_block_id() == x.id(), "PREMISE: head must be x");

    // One vote SHORT of a certificate: the guard above must not apply, and the ranking must act.
    push_quorum_votes(*f.a, f.gp, f.keys, x, quorum - 1);
    BOOST_REQUIRE_MESSAGE(!db.get_block_qc(x.block_num()).valid(),
                          "PREMISE: x must NOT be certified (otherwise the own-QC guard is what refuses)");
    // Same height means the same round, and one vote per (validator, round) is the rule, so the
    // sibling's votes come in a later round - that is how a rebuild of one height actually votes.
    const uint32_t later_round = db.round_of(x.timestamp) + 1;
    const auto digest = block_vote_digest(db.get_chain_id(), y.block_num(), y.id(), later_round);
    uint32_t pushed = 0;
    for (const auto& v : scheduled(db)) {
        if (pushed >= quorum) break;
        db.push_block_vote(y.block_num(), y.id(), later_round, v, key_for(f.gp, f.keys, v).sign_compact(digest));
        ++pushed;
    }
    BOOST_REQUIRE_EQUAL(pushed, quorum);

    BOOST_CHECK_MESSAGE(db.canonical_sibling_id().valid(),
                        "an uncertified head with fewer votes must lose the ranking");
    BOOST_CHECK(db.converge_to_canonical_sibling());
    BOOST_CHECK(db.head_block_id() == y.id());
}

// Multi-node determinism of the convergence rule, end to end (18.11.2026 shape). The single-node
// cases above prove the rule moves a head; they do NOT prove the property that makes it a
// convergence rule at all - that independent nodes, each ranking on its own copy, land on the SAME
// block. On 18.11 the network died precisely because that property was absent: every producer made
// its own choice and no two agreed. Three nodes here share no state, receive the same blocks and
// the same votes, and must answer identically.
BOOST_AUTO_TEST_CASE(three_nodes_converge_on_the_same_sibling_without_talking) {
    BOOST_REQUIRE_MESSAGE(CHAIN_MAX_VALIDATORS >= 4,
                          "PREMISE: this build caps the schedule below four identities");
    fixture f(0x57057u);

    // The third peer joins on the SHARED PREFIX, before the split - the same way the fixture brings
    // up b, and the only order the harness supports: replaying a chain that already contains the
    // split fails header validation ("slot_num > 0", measured in CI twice). A neutral peer is also
    // the honest version of this test: it has no side, it only sees both blocks and the votes.
    simulated_node c("storm-third-peer", f.gp, f.clk);
    replay_to(*f.a, c);
    c.db().set_hardfork(CHAIN_HARDFORK_16, true);

    auto [x, y] = f.sibling_pair();        // a: head X (and Y in fork_db) · b: head Y
    BOOST_REQUIRE_MESSAGE(c.head_block_id() == x.previous, "PREMISE: the third peer must sit on the shared parent");

    // Gossip so that every node holds BOTH siblings: c takes X then Y, b takes X (it already has Y).
    c.receive_block(x);
    BOOST_REQUIRE_MESSAGE(c.head_block_id() == x.id(), "PREMISE: X did not apply on the neutral peer");
    c.receive_block(y);
    BOOST_REQUIRE_MESSAGE(c.db().get_fork_db().fetch_block(y.id()), "PREMISE: Y did not reach the neutral peer");
    f.b->receive_block(x);
    BOOST_REQUIRE_MESSAGE(f.b->db().get_fork_db().fetch_block(x.id()), "PREMISE: X did not reach b");

    // Identical vote set everywhere: one scheduled voter names Y, nobody names X. One vote is below
    // quorum by construction, so the certified path cannot be what resolves this - only the rule
    // under test can.
    for (auto* n : {f.a.get(), f.b.get(), &c}) push_quorum_votes(*n, f.gp, f.keys, y, 1);

    // Each node answers on its own copy, without consulting the others.
    const auto a_pick = f.a->db().canonical_sibling_id();
    const auto b_pick = f.b->db().canonical_sibling_id();
    const auto c_pick = c.db().canonical_sibling_id();
    BOOST_CHECK_MESSAGE(a_pick.valid() && *a_pick == y.id(), "peer a must rank Y as canonical");
    BOOST_CHECK_MESSAGE(!b_pick.valid(), "peer b already holds Y: it must see nothing to change");
    BOOST_CHECK_MESSAGE(c_pick.valid() && *c_pick == y.id(), "the neutral peer must rank Y as canonical");

    BOOST_CHECK(f.a->db().converge_to_canonical_sibling());
    BOOST_CHECK(!f.b->db().converge_to_canonical_sibling());
    BOOST_CHECK(c.db().converge_to_canonical_sibling());

    // The whole point: same head on every node, computed independently.
    BOOST_CHECK_MESSAGE(f.a->head_block_id() == y.id(), "peer a did not land on Y");
    BOOST_CHECK_MESSAGE(f.b->head_block_id() == y.id(), "peer b left Y");
    BOOST_CHECK_MESSAGE(c.head_block_id() == y.id(), "the neutral peer did not land on Y");
    BOOST_CHECK_EQUAL(f.a->head_block_num(), y.block_num());
    BOOST_CHECK_EQUAL(c.head_block_num(), y.block_num());
    // And the split is closed: nothing is left to converge towards.
    BOOST_CHECK(!f.a->db().canonical_sibling_id().valid());
    BOOST_CHECK(!c.db().canonical_sibling_id().valid());
}

BOOST_AUTO_TEST_SUITE_END()
#endif
