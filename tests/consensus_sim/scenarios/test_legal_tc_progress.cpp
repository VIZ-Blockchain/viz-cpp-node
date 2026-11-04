// Bounded LOCAL native-DB falsification, not a network/plugin reachability proof.
// Controlled manual HF16, not natural current-HF18 history. No consensus skips.
// IMPORTANT: the existing CHAIN_TESTNET_VALIDATORS=2 build cannot satisfy the
// ordinary confirmation-list quorum premise. Full cases fail PREMiSE below;
// use a SEPARATE >=4-validator build, not producer votes to repair that premise.
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

// These are OBSERVATIONAL HARNESS constraints, NOT validator_plugin::may_vote /
// may_timeout (private methods with durable storage). No claim of real plugin
// persistence, timer execution, gossip or multi-process validator execution.
struct signing_ledger {
    struct last_action { uint32_t round = 0; block_id_type id; };
    std::map<account_name_type, last_action> last;
    std::map<std::pair<account_name_type, uint32_t>, block_id_type> votes, blocks;
    std::map<std::pair<account_name_type, uint32_t>, uint32_t> timeouts;

    void block(const signed_block& b, uint32_t round) {
        const auto k = std::make_pair(b.validator, round);
        BOOST_REQUIRE_MESSAGE(!blocks.count(k), "PREMISE: identity produced twice in one round");
        blocks.emplace(k, b.id());
        BOOST_TEST_MESSAGE("signed_block identity=" << std::string(b.validator) << " round=" << round
                           << " id=" << b.id().str());
    }
    void vote(const account_name_type& v, uint32_t round, const block_id_type& id) {
        auto& a = last[v];
        BOOST_REQUIRE_MESSAGE(round > a.round || (round == a.round && id == a.id),
                              "PREMISE: backward/conflicting/post-timeout block vote");
        const auto k = std::make_pair(v, round);
        const auto old = votes.find(k);
        BOOST_REQUIRE(old == votes.end() || old->second == id);
        a.round = round; a.id = id; votes[k] = id;
        BOOST_TEST_MESSAGE("signed_vote identity=" << std::string(v) << " round=" << round << " id=" << id.str());
    }
    void timeout(const account_name_type& v, uint32_t round, uint32_t high) {
        auto& a = last[v];
        BOOST_REQUIRE_MESSAGE(a.round <= round, "PREMISE: timeout after a newer block vote");
        const auto k = std::make_pair(v, round);
        BOOST_REQUIRE_MESSAGE(!timeouts.count(k), "PREMISE: second timeout for identity/round");
        if (a.round < round) { a.round = round; a.id = block_id_type(); }
        timeouts[k] = high;
        BOOST_TEST_MESSAGE("signed_timeout identity=" << std::string(v) << " round=" << round << " high_QC=" << high);
    }
};

std::set<account_name_type> scheduled(database& db) {
    const auto& s = db.get_validator_schedule_object();
    std::set<account_name_type> out;
    for (int i = 0; i < s.num_scheduled_validators; i += CHAIN_BLOCK_VALIDATOR_REPEAT)
        out.insert(s.current_shuffled_validators[i]);
    BOOST_REQUIRE_EQUAL(out.size(), uint32_t(s.num_scheduled_validators) / CHAIN_BLOCK_VALIDATOR_REPEAT);
    return out;
}

bool confirmation_lists_head(database& db, const account_name_type& v) {
    for (const auto& c : db.get_validator_confirmations(v))
        if (c.block_num == db.head_block_num() && c.block_id == db.head_block_id()) return true;
    return false;
}

const fc::ecc::private_key& key_for(database& db, const genesis_params& gp,
                                  const keys_type& keys, const account_name_type& v) {
    const auto it = keys.find(v);
    const auto& key = it != keys.end() ? it->second :
        (v == gp.genesis_witness_name ? gp.genesis_witness_key : gp.initiator_key);
    const auto* object = db.find_validator(v);
    BOOST_REQUIRE(object);
    BOOST_REQUIRE_MESSAGE(object->signing_key == key.get_public_key(), "PREMISE: unavailable scheduled signing key");
    return key;
}

signed_transaction ops(simulated_node& n, const genesis_params& gp,
                       const std::vector<operation>& operations) {
    signed_transaction tx;
    tx.set_reference_block(n.head_block_id());
    tx.set_expiration(n.head_block_time() + fc::seconds(60));
    for (const auto& op : operations) tx.operations.emplace_back(op);
    tx.sign(gp.initiator_key, n.chain_id());
    return tx;
}

signed_block make_block(simulated_node& n, const genesis_params& gp,
                        const keys_type& keys, fc::time_point_sec when, signing_ledger& ledger,
                        const fc::optional<block_timeout_certificate>& tc = {}) {
    auto& db = n.db();
    const auto v = db.get_scheduled_validator(db.get_slot_at_time(when));
    const auto& key = key_for(db, gp, keys, v);
    auto b = db.generate_block(when, v, key, database::skip_nothing, tc, {});
    ledger.block(b, db.round_of(b.timestamp));
    return b;
}

void apply_exact(simulated_node& n, const signed_block& b) {
    n.db().push_block(b, database::skip_nothing);
    BOOST_REQUIRE_MESSAGE(n.head_block_id() == b.id(), "PREMISE: independent linear replay did not apply block");
}

// Mirror only the inspected plugin's head-candidate Jolteon gate. This is not
// a reusable production gate: the plugin does not expose one. The confirmation
// list, QC conflict query, signature verification and vote tally ARE native.
void check_head_voting_gate(database& db, const signed_block& b) {
    uint32_t qr = 0, tr = 0; bool has_qc = false;
    for (const auto& e : b.extensions) {
        if (e.which() == 3) { has_qc = true; qr = e.get<block_qc_certificate>().round; }
        if (e.which() == 4) tr = e.get<block_timeout_certificate>().round;
    }
    const auto round = db.round_of(b.timestamp);
    const auto held = db.get_block_qc(b.block_num() - 1);
    const bool late = has_qc && qr + 2 == round && (!held || held->block_id == b.previous);
    BOOST_REQUIRE_MESSAGE(has_qc ? (qr + 1 == round || (tr > qr && tr < round) || late)
                                 : !db.qc_conflicts_with(b),
                          "PREMISE: plugin-equivalent head voting gate refuses block");
}

block_qc_certificate certify_head(simulated_node& n, const genesis_params& gp,
                                  const keys_type& keys, signing_ledger& ledger,
                                  const std::vector<simulated_node*>& voting_nodes = {}) {
    auto& db = n.db();
    const auto head = db.fetch_block_by_id(db.head_block_id());
    BOOST_REQUIRE(head.valid());
    check_head_voting_gate(db, *head);
    const auto voters = scheduled(db);
    const auto q = database::block_qc_quorum(uint32_t(voters.size()));
    uint32_t eligible = 0;
    for (const auto& v : voters) {
        const bool listed = confirmation_lists_head(db, v);
        if (v == head->validator) BOOST_REQUIRE_MESSAGE(!listed, "PREMISE: producer unexpectedly listed");
        if (listed) ++eligible;
    }
    BOOST_REQUIRE_MESSAGE(eligible >= q, "PREMISE: ordinary nonproducer confirmation candidates cannot form QC");
    const auto round = db.round_of(head->timestamp);
    const auto d = block_vote_digest(db.get_chain_id(), head->block_num(), head->id(), round);
    size_t voter_index = 0;
    for (const auto& v : voters) {
        database& signer_db = voting_nodes.empty() ? db : voting_nodes.at(voter_index++)->db();
        BOOST_REQUIRE(signer_db.head_block_id() == head->id());
        BOOST_REQUIRE(scheduled(signer_db) == voters);
        if (!confirmation_lists_head(db, v)) continue;
        BOOST_REQUIRE(confirmation_lists_head(signer_db, v));
        check_head_voting_gate(signer_db, *head);
        BOOST_REQUIRE(v != head->validator);
        ledger.vote(v, round, head->id());
        const auto signature = key_for(signer_db, gp, keys, v).sign_compact(d);
        BOOST_REQUIRE(db.verify_block_vote(head->block_num(), head->id(), round, v, signature));
        BOOST_REQUIRE(db.push_block_vote(head->block_num(), head->id(), round, v, signature));
    }
    const auto qc = db.get_block_qc(head->block_num());
    BOOST_REQUIRE(qc.valid());
    BOOST_REQUIRE(qc->block_id == head->id());
    block_qc_certificate out;
    out.block_num = head->block_num(); out.block_id = head->id(); out.round = round;
    for (const auto& sv : qc->signatures) out.signatures.emplace(sv.first, sv.second);
    BOOST_REQUIRE_GE(out.signatures.size(), q);
    BOOST_REQUIRE(!out.signatures.count(head->validator));
    for (auto* voter : voting_nodes)
        if (&voter->db() != &db) BOOST_REQUIRE(voter->db().accept_block_qc_certificate(out));
    return out;
}

void setup_voters(simulated_node& n, const genesis_params& gp, keys_type& keys,
                  fc::time_point_sec& when, signing_ledger& ledger) {
    const auto step = [&]() {
        when += fc::seconds(n.db().block_interval());
        return make_block(n, gp, keys, when, ledger);
    };
    step();
    transfer_to_vesting_operation vest;
    vest.from = gp.initiator_name; vest.to = gp.initiator_name;
    vest.amount = asset(share_type(n.db().get_account(gp.initiator_name).balance.amount.value / 2), TOKEN_SYMBOL);
    validator_update_operation reg;
    reg.owner = gp.initiator_name; reg.url = "viz"; reg.block_signing_key = gp.initiator_key.get_public_key();
    account_validator_vote_operation vote;
    vote.account = gp.initiator_name; vote.validator = gp.initiator_name; vote.approve = true;
    n.push_pending_transaction(ops(n, gp, {vest, reg, vote}));
    for (int i = 0; i < 300 && !n.db().has_hardfork(CHAIN_HARDFORK_14); ++i) step();
    BOOST_REQUIRE_MESSAGE(n.db().has_hardfork(CHAIN_HARDFORK_14), "PREMISE: HF14 not reached");
    keys[gp.initiator_name] = gp.initiator_key;
    // Register actual additional voting identities on chain. No schedule/index mutation.
    // Their account authorities use the synthetic initiator key; BLOCK keys are distinct.
    authority auth; auth.weight_threshold = 1; auth.key_auths[gp.initiator_key.get_public_key()] = 1;
    std::vector<operation> create_ops;
    for (uint32_t i = 0; i < 3; ++i) {
        const auto name = account_name_type(std::string("legal-voter-") + char('a' + i));
        account_create_operation create;
        create.creator = gp.initiator_name; create.new_account_name = name;
        create.fee = n.db().get_validator_schedule_object().median_props.account_creation_fee;
        create.delegation = asset(0, SHARES_SYMBOL);
        create.master = auth; create.active = auth; create.regular = auth;
        create.memo_key = gp.initiator_key.get_public_key();
        transfer_operation fund; fund.from = gp.initiator_name; fund.to = name;
        fund.amount = asset(100000000, TOKEN_SYMBOL);
        create_ops.emplace_back(create); create_ops.emplace_back(fund);
        keys[name] = gp.witness_keys.at(i).second;
    }
    n.push_pending_transaction(ops(n, gp, create_ops));
    step();
    std::vector<operation> registration;
    for (const auto& item : keys) {
        if (item.first == gp.initiator_name) continue;
        reg.owner = item.first; reg.block_signing_key = item.second.get_public_key();
        vote.validator = item.first;
        registration.emplace_back(reg); registration.emplace_back(vote);
    }
    n.push_pending_transaction(ops(n, gp, registration));
    const std::set<account_name_type> wanted = {gp.initiator_name, account_name_type("legal-voter-a"),
        account_name_type("legal-voter-b"), account_name_type("legal-voter-c")};
    for (int i = 0; i < 100; ++i) {
        step();
        if (scheduled(n.db()) == wanted) break;
    }
    BOOST_REQUIRE_MESSAGE(scheduled(n.db()) == wanted, "PREMISE: four registered voters not the actual on-chain schedule");
    BOOST_REQUIRE_MESSAGE(!n.db().has_hardfork(CHAIN_HARDFORK_16), "PREMISE: setup crossed HF16 before controlled activation");
}

void state(const char* tag, simulated_node& n) {
    const auto fork = n.db().get_fork_db().head();
    const auto lib = n.db().fetch_block_by_number(n.last_irreversible_block_num());
    BOOST_TEST_MESSAGE(tag << " head=" << n.head_block_num() << ':' << n.head_block_id().str()
        << " LIB=" << n.last_irreversible_block_num() << ':' << (lib ? lib->id().str() : std::string("none"))
        << " fork_tip=" << (fork ? fork->num : 0) << ':' << (fork ? fork->id.str() : std::string("none"))
        << " linked=" << n.db().get_fork_db().linked_size());
}

void deliver(simulated_node& n, const signed_block& b) {
    try { n.db().push_block(b, database::skip_nothing); }
    catch (const fc::exception& e) {
        BOOST_TEST_MESSAGE("delivery node=" << n.label() << " block=" << b.block_num()
                           << " id=" << b.id().str() << " exception=" << e.to_detail_string());
    }
}

void replacement_case(bool rotate, bool competitor = true) {
    BOOST_REQUIRE_MESSAGE(CHAIN_MAX_VALIDATORS >= 4,
        "PREMISE: this build caps the schedule below four identities; N=2 Q=2 but the producer is excluded. "
        "Do not count producer votes. Rebuild a separate native target with >=4 configured validators.");
    auto gp = make_genesis_params(0x1E6A17C, 8);
    auto start = fc::time_point_sec(CHAIN_HARDFORK_14_TIME);
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node source("legal-tc-source", gp, clk);
    simulated_node baseline("legal-tc-independent-linear", gp, clk);
    simulated_node first("legal-tc-passive-parent-first", gp, clk);
    simulated_node reordered("legal-tc-passive-children-first", gp, clk);
    std::vector<std::unique_ptr<simulated_node>> replicas;
    std::vector<simulated_node*> voting_nodes{&source};
    for (int i = 1; i < 4; ++i) {
        replicas.emplace_back(std::make_unique<simulated_node>("legal-tc-voter-" + std::to_string(i), gp, clk));
        voting_nodes.push_back(replicas.back().get());
    }
    signing_ledger ledger;
    keys_type old_keys;
    fc::time_point_sec when(start - fc::seconds(CHAIN_BLOCK_INTERVAL));
    setup_voters(source, gp, old_keys, when, ledger);
    for (const auto& b : source.canonical_blocks_from(1))
        for (auto* n : {&baseline, &first, &reordered}) apply_exact(*n, b);
    for (const auto& b : source.canonical_blocks_from(1))
        for (const auto& n : replicas) apply_exact(*n, b);
    for (auto* n : {&source, &baseline, &first, &reordered}) n->db().set_hardfork(CHAIN_HARDFORK_16, true);
    for (const auto& n : replicas) n->db().set_hardfork(CHAIN_HARDFORK_16, true);
    BOOST_TEST_MESSAGE("scope=controlled_HF16 native_confirmation_list+native_vote_checks+HARNESS_ledger "
                       "actual_plugin_persisted_guard=NOT_EXECUTED observers=PASSIVE rotation=" << rotate);
    // Leave activation exemptions behind without inventing a QC from a producer vote.
    for (int i = 0; i < 3; ++i) {
        if (source.head_block_num()) certify_head(source, gp, old_keys, ledger, voting_nodes);
        when += fc::seconds(source.db().block_interval());
        const auto b = make_block(source, gp, old_keys, when, ledger);
        for (auto* n : {&baseline, &first, &reordered}) apply_exact(*n, b);
        for (const auto& n : replicas) apply_exact(*n, b);
    }
    const auto ancestor_qc = certify_head(source, gp, old_keys, ledger, voting_nodes);
    for (auto* n : {&baseline, &first, &reordered}) BOOST_REQUIRE(n->db().accept_block_qc_certificate(ancestor_qc));
    const auto ancestor = source.head_block_id();
    const auto base = source.head_block_num();
    const auto n_voters = uint32_t(scheduled(source.db()).size());
    const auto quorum = database::block_qc_quorum(n_voters);
    BOOST_REQUIRE_EQUAL(n_voters, 4u);
    BOOST_REQUIRE_EQUAL(quorum, 3u);
    BOOST_REQUIRE_GT(base, source.last_irreversible_block_num());
    when += fc::seconds(source.db().block_interval());
    const auto a_round = source.db().round_of(when);
    const auto rival_producer = source.db().get_scheduled_validator(source.db().get_slot_at_time(when));
    fc::optional<signed_block> a;
    if (competitor) {
        a = make_block(first, gp, old_keys, when, ledger);
        apply_exact(reordered, *a);
        BOOST_REQUIRE_EQUAL(database::block_cert_round(*a), ancestor_qc.round);
        BOOST_REQUIRE(!first.db().get_block_qc(a->block_num()).valid());
        BOOST_REQUIRE(!source.db().get_block_qc(a->block_num()).valid());
        BOOST_REQUIRE_GT(a->block_num(), first.last_irreversible_block_num());
    }
    // Delayed A votes: NONE are signed. This is an ordinary missing-delivery
    // history, not quorum equivocation or a QC that can secretly conflict with B.
    // Choose a later scheduled slot for the SAME producer identity and OLD key.
    auto b_when = when;
    bool slot_found = false;
    for (uint32_t i = 0; i < 32; ++i) {
        b_when += fc::seconds(source.db().block_interval());
        const auto slot = source.db().get_slot_at_time(b_when);
        if (source.db().round_of(b_when) > a_round + 1 && source.db().get_scheduled_validator(slot) == rival_producer) {
            slot_found = true; break;
        }
    }
    BOOST_REQUIRE_MESSAGE(slot_found, "PREMISE: bounded later slot for original producer not found");
    const auto b_round = source.db().round_of(b_when);
    const auto timeout_round = b_round - 1;
    BOOST_REQUIRE_GT(timeout_round, ancestor_qc.round);
    // All timeout signers possess this exact common QC; nobody holds QC(A).
    // Under the plugin timer this is a fully passed empty round immediately
    // before B's slot; we do NOT execute that wall-clock timer here.
    const auto td = block_timeout_digest(source.chain_id(), timeout_round, ancestor_qc.round);
    size_t timeout_index = 0;
    for (const auto& v : scheduled(source.db())) {
        auto& signer_db = voting_nodes.at(timeout_index++)->db();
        const auto held = signer_db.get_block_qc(base);
        BOOST_REQUIRE(held.valid() && held->block_id == ancestor);
        ledger.timeout(v, timeout_round, ancestor_qc.round);
        const auto signature = key_for(signer_db, gp, old_keys, v).sign_compact(td);
        BOOST_REQUIRE(source.db().verify_timeout_vote(timeout_round, ancestor_qc.round, v, signature));
        BOOST_REQUIRE(source.db().push_timeout_vote(timeout_round, ancestor_qc.round, v, signature));
    }
    const auto tc = source.db().get_timeout_cert(timeout_round);
    BOOST_REQUIRE(tc.valid());
    BOOST_REQUIRE_GE(tc->votes.size(), quorum);
    for (const auto& v : tc->votes) BOOST_REQUIRE_EQUAL(v.second.high_qc_round, ancestor_qc.round);
    const auto original_producer_key = key_for(source.db(), gp, old_keys, rival_producer);
    auto active_keys = old_keys;
    fc::optional<signed_transaction> rotation;
    if (rotate) {
        std::vector<operation> changes;
        uint32_t i = 4;
        for (const auto& entry : old_keys) {
            active_keys[entry.first] = gp.witness_keys.at(i++).second;
            validator_update_operation reg;
            reg.owner = entry.first; reg.url = "viz";
            reg.block_signing_key = active_keys.at(entry.first).get_public_key();
            changes.emplace_back(reg);
        }
        rotation = ops(source, gp, changes);
        source.push_pending_transaction(*rotation);
    }
    // Registered canonical producer key was checked before pending rotation.
    const auto old_producer_key = original_producer_key.get_public_key();
    const auto b = source.db().generate_block(b_when, rival_producer, original_producer_key,
                                             database::skip_nothing, tc, {});
    ledger.block(b, b_round);
    BOOST_REQUIRE(b.validator == rival_producer);
    BOOST_REQUIRE_GT(b_round, a_round);
    BOOST_REQUIRE(b.previous == ancestor);
    BOOST_REQUIRE_EQUAL(b.block_num(), base + 1);
    if (a) BOOST_REQUIRE(b.id() != a->id());
    BOOST_REQUIRE(b.validate_signee(old_producer_key));
    BOOST_REQUIRE_EQUAL(database::block_cert_round(b), timeout_round);
    bool carried_qc = false, carried_tc = false;
    for (const auto& e : b.extensions) {
        if (e.which() == 3) {
            const auto& c = e.get<block_qc_certificate>();
            BOOST_REQUIRE(c.block_id == ancestor); BOOST_REQUIRE_EQUAL(c.round, ancestor_qc.round); carried_qc = true;
        }
        if (e.which() == 4) { BOOST_REQUIRE_EQUAL(e.get<block_timeout_certificate>().round, timeout_round); carried_tc = true; }
    }
    BOOST_REQUIRE(carried_qc && carried_tc);
    if (rotate) {
        BOOST_REQUIRE_EQUAL(b.transactions.size(), 1u);
        BOOST_REQUIRE(b.transactions.front().id() == rotation->id());
    }
    // Independent native validity control gets B before any descendants and no A.
    apply_exact(baseline, b);
    for (const auto& n : replicas) apply_exact(*n, b);
    state("entry-source", source); state("entry-parent-first", first); state("entry-children-first", reordered);
    const auto entry_lib = first.last_irreversible_block_num();
    deliver(first, b);
    BOOST_CHECK_MESSAGE(first.head_block_id() == b.id(), "later-round TC replacement should recover when delivered first");
    std::vector<signed_block> stream{b};
    when = b_when;
    for (int i = 0; i < 6; ++i) {
        certify_head(source, gp, active_keys, ledger, voting_nodes);
        when += fc::seconds(source.db().block_interval());
        const auto child = make_block(source, gp, active_keys, when, ledger);
        BOOST_REQUIRE(child.validate_signee(active_keys.at(child.validator).get_public_key()));
        apply_exact(baseline, child);
        for (const auto& n : replicas) apply_exact(*n, child);
        stream.push_back(child);
        deliver(first, child);
        deliver(reordered, child); // parent B genuinely absent, not a preloaded receipt.
    }
    state("before-late-parent", reordered);
    deliver(reordered, b);
    // Exact byte retransmission, not new signatures or same-round rival blocks.
    for (const auto& child : stream) deliver(reordered, child);
    state("after-retransmission", reordered);
    // Bounded ordinary healing: 100 consecutive native slots with valid canonical
    // QC-backed blocks, plus repeated missing prefix delivery. A stale observer
    // does not sign or reduce the four-validator quorum. This cannot prove halt.
    for (int slot = 0; slot < 100; ++slot) {
        certify_head(source, gp, active_keys, ledger, voting_nodes);
        when += fc::seconds(source.db().block_interval());
        const auto child = make_block(source, gp, active_keys, when, ledger);
        apply_exact(baseline, child);
        for (const auto& n : replicas) apply_exact(*n, child);
        stream.push_back(child);
        deliver(first, child);
        for (const auto& packet : stream) deliver(reordered, packet);
    }
    BOOST_REQUIRE(baseline.head_block_id() == source.head_block_id());
    BOOST_REQUIRE_GT(source.last_irreversible_block_num(), entry_lib);
    const auto source_lib = source.last_irreversible_block_num();
    const auto lib_block = source.db().fetch_block_by_number(source_lib);
    BOOST_REQUIRE(lib_block.valid());
    BOOST_REQUIRE(baseline.db().fetch_block_by_number(source_lib)->id() == lib_block->id());
    for (auto* observer : {&first, &reordered}) {
        state("bounded-final", *observer);
        BOOST_CHECK_MESSAGE(observer->head_block_id() == source.head_block_id(),
                            "passive observer did not converge after bounded legal TC history and retransmission");
        BOOST_CHECK_GT(observer->last_irreversible_block_num(), entry_lib);
        if (observer->head_block_id() == source.head_block_id()) {
            const auto common_lib = observer->db().fetch_block_by_number(source_lib);
            BOOST_CHECK(common_lib.valid() && common_lib->id() == lib_block->id());
        }
    }
    state("bounded-source", source); state("bounded-independent-linear", baseline);
    for (auto* voter : voting_nodes) {
        state(voter->label().c_str(), *voter);
        BOOST_REQUIRE(voter->head_block_id() == source.head_block_id());
        BOOST_REQUIRE_EQUAL(voter->last_irreversible_block_num(), source.last_irreversible_block_num());
    }
    BOOST_TEST_MESSAGE("Interpretation: native passive-follower convergence only; no network-wide voting halt claim. "
                       "Production may change concurrently: integrator must record source revision/diff and cache patch state.");
}
void linear_baseline() {
    auto gp = make_genesis_params(0x1E6A17C, 8);
    auto start = fc::time_point_sec(CHAIN_HARDFORK_14_TIME);
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    simulated_node source("legal-tc-linear-source", gp, clk);
    std::vector<std::unique_ptr<simulated_node>> peers;
    std::vector<simulated_node*> voters{&source};
    for (int i = 1; i < 4; ++i) {
        peers.emplace_back(std::make_unique<simulated_node>("legal-tc-linear-voter-" + std::to_string(i), gp, clk));
        voters.push_back(peers.back().get());
    }
    keys_type keys; signing_ledger ledger;
    fc::time_point_sec when(start - fc::seconds(CHAIN_BLOCK_INTERVAL));
    setup_voters(source, gp, keys, when, ledger);
    for (const auto& b : source.canonical_blocks_from(1))
        for (const auto& n : peers) apply_exact(*n, b);
    for (auto* n : voters) n->db().set_hardfork(CHAIN_HARDFORK_16, true);
    const auto entry = source.head_block_num();
    const auto entry_lib = source.last_irreversible_block_num();
    for (int i = 0; i < 24; ++i) {
        certify_head(source, gp, keys, ledger, voters);
        when += fc::seconds(source.db().block_interval());
        const auto b = make_block(source, gp, keys, when, ledger);
        for (const auto& n : peers) apply_exact(*n, b);
    }
    BOOST_REQUIRE_EQUAL(source.head_block_num(), entry + 24);
    BOOST_REQUIRE_GT(source.last_irreversible_block_num(), entry_lib);
    for (auto* n : voters) {
        state(n->label().c_str(), *n);
        BOOST_REQUIRE(n->head_block_id() == source.head_block_id());
        BOOST_REQUIRE_EQUAL(n->last_irreversible_block_num(), source.last_irreversible_block_num());
    }
}
} // private namespace

BOOST_AUTO_TEST_SUITE(legal_tc_progress_suite)
BOOST_AUTO_TEST_CASE(ordinary_quorum_configuration_premise) {
    // Runtime native formula, no model arithmetic substituting for consensus.
    BOOST_TEST_MESSAGE("compiled schedule cap=" << CHAIN_MAX_VALIDATORS
        << " native_quorum=" << database::block_qc_quorum(CHAIN_MAX_VALIDATORS)
        << " native_quorum_N2=" << database::block_qc_quorum(2));
    BOOST_CHECK_MESSAGE(CHAIN_MAX_VALIDATORS >= 4,
        "PREMISE BLOCKED: existing two-validator build has insufficient nonproducer voters; no TC reachability result");
}
BOOST_AUTO_TEST_CASE(linear_four_validator_baseline) { linear_baseline(); }
BOOST_AUTO_TEST_CASE(tc_control_without_competitor) { replacement_case(true, false); }
BOOST_AUTO_TEST_CASE(tc_replacement_delivery_orders_without_rotation) { replacement_case(false); }
BOOST_AUTO_TEST_CASE(tc_replacement_delivery_orders_with_authorized_rotation) { replacement_case(true); }
BOOST_AUTO_TEST_SUITE_END()
#endif
