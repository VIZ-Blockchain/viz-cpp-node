// Local native cache lifecycle regression; synthetic histories, not network proof.
#include <boost/test/unit_test.hpp>
#include "simulated_node.hpp"
#include <boost/filesystem.hpp>
#include <chainbase/chainbase.hpp>
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
class native_fixture {
    fc::path data_, shm_;
    uint64_t supply_;
    std::unique_ptr<database> db_;
    void open() {
        boost::filesystem::create_directories(data_.string());
        boost::filesystem::create_directories(shm_.string());
        db_.reset(new database());
        db_->open(data_, shm_, supply_, 64ull * 1024 * 1024,
                  chainbase::database::read_write);
    }
public:
    native_fixture(const std::string& label, const genesis_params& gp, virtual_clock&)
      : data_((boost::filesystem::temp_directory_path() /
               boost::filesystem::unique_path("canonical-fix-" + label + "-data-%%%%%%%%")).string()),
        shm_((boost::filesystem::temp_directory_path() /
              boost::filesystem::unique_path("canonical-fix-" + label + "-shm-%%%%%%%%")).string()),
        supply_(gp.initial_supply) { open(); }
    ~native_fixture() {
        try { db_->close(); } catch (...) {}
        db_.reset();
        boost::filesystem::remove_all(data_.string());
        boost::filesystem::remove_all(shm_.string());
    }
    database& db() { return *db_; }
    const database& db() const { return *db_; }
    uint32_t head_block_num() const { return db_->head_block_num(); }
    block_id_type head_block_id() const { return db_->head_block_id(); }
    fc::time_point_sec head_block_time() const { return db_->head_block_time(); }
    chain_id_type chain_id() const { return db_->get_chain_id(); }
    uint32_t last_irreversible_block_num() const { return db_->last_non_undoable_block_num(); }
    signed_block produce_block(const account_name_type& v, const fc::ecc::private_key& key,
                               fc::time_point_sec t) {
        return db_->generate_block(t, v, key, database::skip_nothing);
    }
    void receive_block(const signed_block& b) { db_->push_block(b, database::skip_nothing); }
    void push_pending_transaction(const signed_transaction& tx) { db_->push_transaction(tx); }
    std::vector<signed_block> canonical_blocks_from(uint32_t first) const {
        std::vector<signed_block> out;
        for (auto h = first; h <= head_block_num(); ++h) {
            const auto b = db_->fetch_block_by_number(h);
            BOOST_REQUIRE(b.valid()); out.push_back(*b);
        }
        return out;
    }
    void reopen() { db_->close(false); db_.reset(); open(); }
};

fc::ecc::private_key key_for(const genesis_params& gp, const account_name_type& v) {
    return (v == gp.genesis_witness_name || v == account_name_type("validator-two"))
        ? gp.genesis_witness_key : gp.initiator_key;
}

// These local helpers follow test_hf16_boundary.cpp; they are not harness exports.
void vote_for_head(native_fixture& n, const genesis_params& gp) {
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

signed_block produce(native_fixture& n, const genesis_params& gp, fc::time_point_sec& when) {
    vote_for_head(n, gp);
    when += fc::seconds(n.db().block_interval());
    const auto v = n.db().get_scheduled_validator(1);
    return n.produce_block(v, key_for(gp, v), when);
}

signed_transaction sign_ops(const std::vector<operation>& ops,
                            const fc::ecc::private_key& key, const native_fixture& n) {
    signed_transaction tx;
    tx.set_reference_block(n.head_block_id());
    tx.set_expiration(n.head_block_time() + fc::seconds(60));
    for (const auto& op : ops) tx.operations.emplace_back(op);
    tx.sign(key, n.chain_id());
    return tx;
}

bool bring_to_hf14(native_fixture& n, const genesis_params& gp, fc::time_point_sec& when) {
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
void cache_lifecycle_case() {
    auto gp = make_genesis_params(0xB16CEu, 2);
    fc::time_point_sec start(fc::time_point::now());
    if (fc::time_point_sec(CHAIN_HARDFORK_14_TIME) > start)
        start = fc::time_point_sec(CHAIN_HARDFORK_14_TIME);
    start += fc::seconds(CHAIN_BLOCK_INTERVAL);
    virtual_clock clk(start);
    native_fixture branch_a("canonical-progress-affected", gp, clk);
    native_fixture branch_b("canonical-progress-rival", gp, clk);
    native_fixture replay("canonical-progress-rival-replay", gp, clk);
    native_fixture canonical("canonical-progress-producer", gp, clk);
    native_fixture control("canonical-progress-linear-control", gp, clk);
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
    std::vector<signed_block> rival;
    // Independently valid uncertified unstick blocks, retained but not preferred.
    for (int i = 0; i < 5; ++i) {
        b_when += fc::seconds(CHAIN_EMERGENCY_UNSTICK_TIMEOUT_SEC + 1);
        const auto v = branch_b.db().get_scheduled_validator(branch_b.db().get_slot_at_time(b_when));
        rival.push_back(branch_b.produce_block(v, key_for(gp, v), b_when));
        BOOST_REQUIRE_EQUAL(database::block_cert_round(rival.back()), 0u);
        replay.receive_block(rival.back());
        BOOST_REQUIRE(replay.head_block_id() == rival.back().id());
        branch_a.receive_block(rival.back());
        BOOST_REQUIRE(branch_a.head_block_id() == a_tip.id());
    }
    const auto entry_lib = branch_a.last_irreversible_block_num();
    const auto candidate = produce(canonical, gp, when);
    auto bad = candidate;
    bad.transaction_merkle_root = checksum_type::hash("invalid canonical child");
    bad.sign(key_for(gp, bad.validator));
    BOOST_CHECK_THROW(branch_a.receive_block(bad), fc::exception);
    BOOST_CHECK(branch_a.head_block_id() == a_tip.id());
    BOOST_CHECK_EQUAL(branch_a.last_irreversible_block_num(), entry_lib);
    BOOST_CHECK(!branch_a.db().get_fork_db().is_known_block(bad.id()));

    branch_a.receive_block(candidate); control.receive_block(candidate);
    BOOST_REQUIRE(branch_a.head_block_id() == candidate.id());
    const auto after_lib = branch_a.last_irreversible_block_num();
    BOOST_REQUIRE_GT(database::block_cert_round(candidate), 0u);
    branch_a.receive_block(candidate); // duplicate certificate-bearing child
    BOOST_CHECK(branch_a.head_block_id() == candidate.id());
    BOOST_CHECK_EQUAL(branch_a.last_irreversible_block_num(), after_lib);

    // A valid later competing arrival must still link after canonical progress.
    b_when += fc::seconds(CHAIN_EMERGENCY_UNSTICK_TIMEOUT_SEC + 1);
    const auto v = branch_b.db().get_scheduled_validator(branch_b.db().get_slot_at_time(b_when));
    const auto rival_next = branch_b.produce_block(v, key_for(gp, v), b_when);
    replay.receive_block(rival_next);
    branch_a.receive_block(rival_next);
    BOOST_REQUIRE(branch_a.head_block_id() == candidate.id());
    const auto cached_rival = branch_a.db().get_fork_db().fetch_block(rival_next.id());
    BOOST_REQUIRE(cached_rival);
    BOOST_REQUIRE(cached_rival->prev.lock());
    BOOST_CHECK(cached_rival->prev.lock()->id == rival.back().id());
    const auto branches = branch_a.db().get_fork_db().fetch_branch_from(rival_next.id(), candidate.id());
    BOOST_REQUIRE(!branches.first.empty() && !branches.second.empty());
    BOOST_CHECK(branches.first.back()->data.previous == branches.second.back()->data.previous);

    BOOST_REQUIRE_GT(branch_a.head_block_num(), branch_a.last_irreversible_block_num());
    branch_a.db().pop_block();
    BOOST_REQUIRE(branch_a.head_block_id() == a_tip.id());
    branch_a.receive_block(candidate);
    BOOST_REQUIRE(branch_a.head_block_id() == candidate.id());
    BOOST_CHECK_EQUAL(branch_a.last_irreversible_block_num(), after_lib);

    // Redeliver the exact applied block after cache loss. In this synthetic
    // non-DLT fixture its reversible body exists only in fork_db; redelivery is
    // required to refill it before a later QC advances LIB through that body.
    branch_a.db().get_fork_db().remove(candidate.id());
    BOOST_REQUIRE(!branch_a.db().get_fork_db().is_known_block(candidate.id()));
    branch_a.receive_block(candidate);
    branch_a.receive_block(a_tip); // restore the base entry's parent link too
    BOOST_REQUIRE(branch_a.db().get_fork_db().is_known_block(candidate.id()));
    BOOST_CHECK(branch_a.head_block_id() == candidate.id());
    for (int i = 0; i < 8; ++i) {
        const auto next = produce(canonical, gp, when);
        branch_a.receive_block(next); control.receive_block(next);
        BOOST_REQUIRE(branch_a.head_block_id() == next.id());
        BOOST_CHECK_EQUAL(branch_a.last_irreversible_block_num(), control.last_irreversible_block_num());
    }
    BOOST_CHECK(!branch_a.db().get_fork_db().is_known_block(rival_next.id()));
    BOOST_CHECK(branch_a.db().get_fork_db().is_known_block(branch_a.head_block_id()));
    const auto retained_lib = branch_a.db().fetch_block_by_number(branch_a.last_irreversible_block_num());
    BOOST_REQUIRE(retained_lib.valid());
    BOOST_CHECK(branch_a.db().get_fork_db().is_known_block(retained_lib->id()));

    // The LIB-bounded range still rejects a different ancient block.
    const auto old = control.db().fetch_block_by_number(2);
    BOOST_REQUIRE(old.valid());
    auto ancient = *old;
    ancient.timestamp += fc::seconds(1);
    ancient.sign(key_for(gp, ancient.validator));
    const auto before_ancient = branch_a.head_block_id();
    BOOST_CHECK_THROW(branch_a.receive_block(ancient), block_too_old_exception);
    BOOST_CHECK(branch_a.head_block_id() == before_ancient);
    BOOST_CHECK_EQUAL(branch_a.last_irreversible_block_num(), control.last_irreversible_block_num());

    // Fresh open undoes reversible blocks to LIB, even after close(false).
    // The undone head session also contained its LIB increment: in this fixture
    // (head=LIB+1), reopening restores (head=old LIB, LIB=old LIB-1).
    const auto expected_lib = control.last_irreversible_block_num();
    const auto expected_reopened_lib = expected_lib - 1;
    const auto lib_block = control.db().fetch_block_by_number(expected_lib);
    BOOST_REQUIRE(lib_block.valid());
    const auto before_head = branch_a.head_block_num();
    branch_a.reopen(); control.reopen();
    BOOST_TEST_MESSAGE("reopen before_head=" << before_head << " expected_LIB=" << expected_lib
                       << " reopened_head=" << branch_a.head_block_num()
                       << " reopened_LIB=" << branch_a.last_irreversible_block_num());
    BOOST_CHECK(branch_a.head_block_id() == control.head_block_id());
    BOOST_CHECK(branch_a.head_block_id() == lib_block->id());
    BOOST_CHECK_EQUAL(branch_a.head_block_num(), expected_lib);
    BOOST_CHECK_EQUAL(branch_a.last_irreversible_block_num(), expected_reopened_lib);
    BOOST_CHECK_EQUAL(control.last_irreversible_block_num(), expected_reopened_lib);
}
} // namespace
BOOST_AUTO_TEST_SUITE(canonical_cache_lifecycle_suite)
BOOST_AUTO_TEST_CASE(invalid_duplicate_competing_rollback_seed_and_reopen) { cache_lifecycle_case(); }
BOOST_AUTO_TEST_SUITE_END()
#endif
