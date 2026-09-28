// HF15 agent access — chain-level tests for the authority hook in database.cpp.
//
// A principal may authorise an agent to broadcast a listed set of operations, and the agent signs
// with its OWN active key. tests/pm/agent_access_test.cpp covers the protocol half (operation
// validation, the never-delegable list, the packed name list); it links the protocol library only
// and has no chain state, so it cannot reach the part where the delegation actually takes effect.
// That is what this file exercises: the transaction is pushed into a real database and accepted or
// rejected by the ordinary sign_state path.
//
// BUILD_TESTNET-only, like test_pm_audit_fixes.cpp: the single-validator fixture reaches HF14
// quorum (CHAIN_HARDFORK_REQUIRED_VALIDATORS=1) only with testnet constants, and the mainnet build
// would leave every case below soft-skipping. HF15 itself is switched on with the same
// deterministic marker trick, so nothing here depends on the wall clock reaching an activation
// time.
#include <boost/test/unit_test.hpp>
#include "simulated_node.hpp"
#include "genesis_factory.hpp"
#include "virtual_clock.hpp"
#include <graphene/chain/database.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/agent_objects.hpp>
#include <graphene/chain/validator_objects.hpp>
#include <graphene/protocol/agent_operations.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <graphene/protocol/config.hpp>
#include <fc/crypto/sha256.hpp>

#include <string>
#include <vector>

using namespace consensus_sim;
using namespace graphene::chain;
using namespace graphene::protocol;

namespace {

account_name_type next_validator(simulated_node& n) { return n.db().get_scheduled_validator(1); }

fc::ecc::private_key key_for(const genesis_params& gp, const account_name_type& v) {
    return (v == gp.initiator_name) ? gp.initiator_key : gp.genesis_witness_key;
}

void produce(simulated_node& n, const genesis_params& gp, fc::time_point_sec& when) {
    when += fc::seconds(CHAIN_BLOCK_INTERVAL);
    const auto v = next_validator(n);
    n.produce_block(v, key_for(gp, v), when);
}

// `nonce` shifts the expiration by a second: two transactions with identical operations signed by
// the same key in the same block hash to the same trx_id and the second is dropped as a duplicate,
// so the expiration is the only free field (the chain has no per-transaction nonce).
signed_transaction sign_ops(const std::vector<operation>& ops, const fc::ecc::private_key& key,
                            const simulated_node& node, uint32_t nonce = 0) {
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

// Start just past HF14, the same start the PM audit regressions use: the simulation clock is a
// virtual one, so it stays BEFORE CHAIN_HARDFORK_15_TIME and the fork can only ever be switched on
// deliberately (see enable_hf15), never by time passing. Both constants are compile-time, so the
// ordering cannot drift at runtime.
fc::time_point_sec sim_start() {
    return fc::time_point_sec(CHAIN_HARDFORK_14_TIME) + fc::seconds(CHAIN_BLOCK_INTERVAL * 3);
}

// Register "viz" as a staked, self-voted validator and advance until HF14 activates.
void bring_to_hf14(simulated_node& node, const genesis_params& gp, fc::time_point_sec& when) {
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
}

// Deterministic HF15 switch: the same mechanism as set_pm_audit_fix in test_pm_audit_fixes.cpp.
// Pushing the marker makes has_hardfork(CHAIN_HARDFORK_15) true without waiting for the activation
// time. Idempotent on purpose — a second push would break the processed_hardforks / last_hardfork
// arithmetic and the next apply_hardfork would fail its own sanity assert.
void enable_hf15(simulated_node& n) {
    if (n.db().has_hardfork(CHAIN_HARDFORK_15)) return;
    const auto& hf = n.db().get_hardfork_property_object();
    n.db().modify(hf, [&](hardfork_property_object& h) {
        h.processed_hardforks.push_back(n.head_block_time());
    });
    BOOST_REQUIRE(n.db().has_hardfork(CHAIN_HARDFORK_15));
}

void create_account(simulated_node& node, const genesis_params& gp, fc::time_point_sec& when,
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

// Accounts transact on bandwidth, so a fixture account that is only funded with liquid TOKEN
// cannot sign anything yet — it has to be vested. Its own block, for the same reason as in
// test_pm_lifecycle.cpp: inside one block the two transactions would be ordered by trx_id, not by
// intent.
void vest(simulated_node& node, const genesis_params& gp, fc::time_point_sec& when,
          const std::string& name, const fc::ecc::private_key& key, share_type token) {
    transfer_to_vesting_operation tv;
    tv.from = name; tv.to = name;
    tv.amount = asset(token, TOKEN_SYMBOL);
    node.push_pending_transaction(sign_ops({tv}, key, node));
    produce(node, gp, when);
}

void grant(simulated_node& node, const genesis_params& gp, fc::time_point_sec& when,
           const account_name_type& principal, const fc::ecc::private_key& pkey,
           const account_name_type& agent, const std::vector<std::string>& ops,
           fc::time_point_sec expiration = fc::time_point_sec()) {
    set_agent_permission_operation op;
    op.account = principal;
    op.agent = agent;
    op.expiration = expiration;
    for (const auto& s : ops) op.operations.insert(s);
    node.push_pending_transaction(sign_ops({op}, pkey, node));
    produce(node, gp, when);
}

/// Push and require rejection. A transaction that the chain refuses must be refused at push time:
/// database::push_transaction validates it through the same hook the block path uses, so an
/// accepted-into-the-pool transaction is a failure of the assertion, not a timing artefact.
void expect_rejected(simulated_node& n, const signed_transaction& tx, const char* what) {
    bool threw = false;
    try {
        n.push_pending_transaction(tx);
    } catch (const std::exception&) {
        threw = true;
    }
    BOOST_CHECK_MESSAGE(threw, what);
}

int64_t liquid(const simulated_node& n, const account_name_type& who) {
    return n.db().get_account(who).balance.amount.value;
}

bool has_row(simulated_node& n, const account_name_type& p, const account_name_type& a) {
    const auto& idx = n.db().get_index<agent_permission_index>().indices().get<by_permission_account>();
    return idx.find(boost::make_tuple(p, a)) != idx.end();
}

transfer_operation transfer_op(const account_name_type& from, const account_name_type& to,
                               share_type amount, const asset_symbol_type& sym) {
    transfer_operation t;
    t.from = from;
    t.to = to;
    t.amount = asset(amount, sym);
    return t;
}

// One node per case: HF14, then the HF15 marker, then two ordinary accounts with independent keys
// — a principal and its agent. Ordinary accounts rather than the genesis initiator, so the
// authority path under test is the one a real user has (the initiator's key is wired into all
// three of its authorities, which would hide a mis-substitution).
struct agent_fixture {
    genesis_params gp;
    virtual_clock clk;
    simulated_node node;
    fc::time_point_sec when;

    account_name_type principal = "principal";
    account_name_type agent = "agent";
    fc::ecc::private_key principal_key = derive_key("principal-key");
    fc::ecc::private_key agent_key = derive_key("agent-key");

    agent_fixture(uint64_t seed, const char* label)
            : gp(make_genesis_params(seed, 1)), clk(sim_start()), node(label, gp, clk),
              when(clk.now() - fc::seconds(CHAIN_BLOCK_INTERVAL)) {
        bring_to_hf14(node, gp, when);
        BOOST_REQUIRE_MESSAGE(node.db().has_hardfork(CHAIN_HARDFORK_14),
                              "harness could not reach HF14 (needs BUILD_TESTNET)");
        enable_hf15(node);
        create_account(node, gp, when, principal, principal_key, 100000);
        create_account(node, gp, when, agent, agent_key, 100000);
        vest(node, gp, when, principal, principal_key, 50000);
        vest(node, gp, when, agent, agent_key, 50000);
    }
};

} // anonymous namespace

// The plain positive: an agent signs, with its OWN key, an operation the principal granted it.
BOOST_AUTO_TEST_CASE(agent_access_agent_signs_granted_operation) {
    agent_fixture f(0xAA9E17, "aa-granted");
    grant(f.node, f.gp, f.when, f.principal, f.principal_key, f.agent, {"transfer"});

    const auto before = liquid(f.node, f.gp.initiator_name);
    f.node.push_pending_transaction(
        sign_ops({transfer_op(f.principal, f.gp.initiator_name, 1000, TOKEN_SYMBOL)}, f.agent_key, f.node));
    produce(f.node, f.gp, f.when);

    BOOST_CHECK_EQUAL(liquid(f.node, f.gp.initiator_name) - before, 1000);
}

// The regression the hook's first rule exists for: a delegation row must not break the principal's
// own transactions. Substituting unconditionally would do exactly that, the moment any row existed.
BOOST_AUTO_TEST_CASE(agent_access_principal_still_signs_for_itself) {
    agent_fixture f(0xAA9E18, "aa-principal");
    grant(f.node, f.gp, f.when, f.principal, f.principal_key, f.agent, {"transfer"});

    const auto before = liquid(f.node, f.gp.initiator_name);
    f.node.push_pending_transaction(
        sign_ops({transfer_op(f.principal, f.gp.initiator_name, 2000, TOKEN_SYMBOL)}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);

    BOOST_CHECK_EQUAL(liquid(f.node, f.gp.initiator_name) - before, 2000);
}

// A grant is a list, not a blank cheque: the agent may only act in transactions where EVERY
// authority-requiring operation is on the list.
BOOST_AUTO_TEST_CASE(agent_access_requires_full_coverage_of_the_transaction) {
    agent_fixture f(0xAA9E19, "aa-coverage");
    grant(f.node, f.gp, f.when, f.principal, f.principal_key, f.agent, {"transfer"});

    transfer_operation t = transfer_op(f.principal, f.gp.initiator_name, 3000, TOKEN_SYMBOL);
    transfer_to_vesting_operation tv;
    tv.from = f.principal; tv.to = f.principal;
    tv.amount = asset(3000, TOKEN_SYMBOL);

    // `transfer` is granted, `transfer_to_vesting` is not, and both require the principal's active
    // authority — so the pair is outside the grant even though the first half looks covered.
    expect_rejected(f.node, sign_ops({t, tv}, f.agent_key, f.node),
                    "agent acted outside its grant: one operation of the transaction was not listed");

    // Control: the principal signing the same two operations is accepted. Without this the case
    // above would also pass if the batch itself were invalid for some unrelated reason.
    const auto before = liquid(f.node, f.gp.initiator_name);
    f.node.push_pending_transaction(sign_ops({t, tv}, f.principal_key, f.node, 1));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_EQUAL(liquid(f.node, f.gp.initiator_name) - before, 3000);
}

// Master authority is structurally out of reach. Every master-only operation is on the
// never-delegable list, so it cannot be granted in the first place; the case that remains is a
// transaction that MIXES a granted active operation of the principal with a master requirement the
// agent really does hold (its own). The hook refuses to substitute anything in such a transaction,
// so the principal's half stays unsatisfied. (Coverage alone would also refuse this pair, because
// the master operation is not on the list — the master rule is the second, structural wall; it
// cannot be isolated in a test precisely because the deny-list makes it unreachable.)
BOOST_AUTO_TEST_CASE(agent_access_never_reaches_master_authority) {
    agent_fixture f(0xAA9E1A, "aa-master");
    grant(f.node, f.gp, f.when, f.principal, f.principal_key, f.agent, {"transfer"});

    change_recovery_account_operation cr;   // the agent's OWN master requirement
    cr.account_to_recover = f.agent;
    cr.new_recovery_account = f.gp.initiator_name;
    const auto t = transfer_op(f.principal, f.gp.initiator_name, 1000, TOKEN_SYMBOL);

    expect_rejected(f.node, sign_ops({t, cr}, f.agent_key, f.node),
                    "agent acted for the principal inside a master-authority transaction");

    // Control: both parties signing the same pair is accepted, so the rejection above is about the
    // delegation and not about the operations themselves.
    signed_transaction both = sign_ops({t, cr}, f.agent_key, f.node, 1);
    both.sign(f.principal_key, f.node.chain_id());
    const auto before = liquid(f.node, f.gp.initiator_name);
    f.node.push_pending_transaction(both);
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_EQUAL(liquid(f.node, f.gp.initiator_name) - before, 1000);
}

// Row validity: the hook trusts rows, so each way a row can be a lie has to be refused.
BOOST_AUTO_TEST_CASE(agent_access_invalid_rows_grant_nothing) {
    agent_fixture f(0xAA9E1B, "aa-rows");
    transfer_operation t = transfer_op(f.principal, f.gp.initiator_name, 4000, TOKEN_SYMBOL);

    // 1) No row at all — the agent is just an unrelated account.
    expect_rejected(f.node, sign_ops({t}, f.agent_key, f.node),
                    "agent acted with no delegation row");

    // 2) A row that is already dead when it lands: an expiration in the past revokes rather than
    // errors (set_agent_permission_evaluator), so the grant must leave no row behind.
    grant(f.node, f.gp, f.when, f.principal, f.principal_key, f.agent, {"transfer"},
          fc::time_point_sec(f.node.db().head_block_time() - fc::seconds(60)));
    BOOST_CHECK_MESSAGE(!has_row(f.node, f.principal, f.agent),
                        "an expired grant left a usable row behind");
    expect_rejected(f.node, sign_ops({t}, f.agent_key, f.node),
                    "agent acted on an expired grant");

    // 3) A never-delegable name is refused at grant time, not at use time.
    set_agent_permission_operation bad;
    bad.account = f.principal;
    bad.agent = f.agent;
    bad.operations.insert("account_update");
    expect_rejected(f.node, sign_ops({bad}, f.principal_key, f.node),
                    "granting account_update was accepted");

    // 4) Unknown name — a silently dead permission is refused too.
    bad.operations.clear();
    bad.operations.insert("no_such_operation");
    expect_rejected(f.node, sign_ops({bad}, f.principal_key, f.node, 1),
                    "granting an unknown operation name was accepted");
}

// The anti-escalation property that a shorter hook would miss. "second" has an ACTIVE authority
// made of an account_auth to the principal, so the principal's key satisfies it through the
// ordinary nested resolution — while the principal's AGENT must not inherit that reach: the agent
// stands in for the principal only in transactions that name the principal, never wherever the
// principal's authority happens to be nested.
BOOST_AUTO_TEST_CASE(agent_access_does_not_leak_through_nested_authorities) {
    agent_fixture f(0xAA9E1C, "aa-nested");

    const account_name_type second = "second";
    fc::ecc::private_key second_key = derive_key("second-key");
    create_account(f.node, f.gp, f.when, second, second_key, 100000);

    authority nested;   // active = "the principal may act for second"
    nested.weight_threshold = 1;
    nested.account_auths[f.principal] = 1;
    account_update_operation au;
    au.account = second;
    au.master = single_key_auth(second_key.get_public_key());
    au.active = nested;
    f.node.push_pending_transaction(sign_ops({au}, second_key, f.node));
    produce(f.node, f.gp, f.when);

    // Control: the principal's own key reaches second's authority by nesting → accepted.
    const auto before = liquid(f.node, f.gp.initiator_name);
    f.node.push_pending_transaction(
        sign_ops({transfer_op(second, f.gp.initiator_name, 5000, TOKEN_SYMBOL)}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_EQUAL(liquid(f.node, f.gp.initiator_name) - before, 5000);

    // And now the delegation for the principal exists — the agent must still not be able to act
    // for "second", which never named the principal in its required authorities.
    grant(f.node, f.gp, f.when, f.principal, f.principal_key, f.agent, {"transfer"});
    expect_rejected(f.node,
                    sign_ops({transfer_op(second, f.gp.initiator_name, 6000, TOKEN_SYMBOL)}, f.agent_key, f.node),
                    "the principal's agent inherited the principal's nested authority elsewhere");
}

// Wipe rules: a delegation must not outlive the keys it was granted under — on either side.
// Principal rotates its ACTIVE key → the row goes and the agent can no longer act.
BOOST_AUTO_TEST_CASE(agent_access_principal_active_change_wipes) {
    agent_fixture f(0xAA9E1D, "aa-wipe-principal");
    grant(f.node, f.gp, f.when, f.principal, f.principal_key, f.agent, {"transfer"});
    BOOST_REQUIRE(has_row(f.node, f.principal, f.agent));

    account_update_operation au;
    au.account = f.principal;
    au.active = single_key_auth(derive_key("principal-key-2").get_public_key());
    f.node.push_pending_transaction(sign_ops({au}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);

    BOOST_CHECK_MESSAGE(!has_row(f.node, f.principal, f.agent), "principal's active change left the row");
    expect_rejected(f.node,
                    sign_ops({transfer_op(f.principal, f.gp.initiator_name, 1000, TOKEN_SYMBOL)}, f.agent_key, f.node),
                    "agent acted after the principal rotated its active key");
}

// Principal changes only REGULAR → the grant is untouched (agents sign with active).
BOOST_AUTO_TEST_CASE(agent_access_regular_change_keeps_row) {
    agent_fixture f(0xAA9E1E, "aa-keep-regular");
    grant(f.node, f.gp, f.when, f.principal, f.principal_key, f.agent, {"transfer"});

    account_update_operation au;
    au.account = f.principal;
    au.regular = single_key_auth(derive_key("principal-regular-2").get_public_key());
    f.node.push_pending_transaction(sign_ops({au}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);

    BOOST_CHECK(has_row(f.node, f.principal, f.agent));
}

// The AGENT rotates its master key (the sale path rewrites master too) → rows where it is the
// agent go as well, so a delegation never passes to whoever controls the agent account next.
BOOST_AUTO_TEST_CASE(agent_access_agent_master_change_wipes) {
    agent_fixture f(0xAA9E1F, "aa-wipe-agent");
    grant(f.node, f.gp, f.when, f.principal, f.principal_key, f.agent, {"transfer"});

    account_update_operation au;
    au.account = f.agent;
    au.master = single_key_auth(derive_key("agent-master-2").get_public_key());
    f.node.push_pending_transaction(sign_ops({au}, f.agent_key, f.node));
    produce(f.node, f.gp, f.when);

    BOOST_CHECK_MESSAGE(!has_row(f.node, f.principal, f.agent), "agent's master change left the row");
    expect_rejected(f.node,
                    sign_ops({transfer_op(f.principal, f.gp.initiator_name, 1000, TOKEN_SYMBOL)}, f.agent_key, f.node),
                    "agent acted after its own master rotation");
}

// Account sales. A price set on testnet opens a 10-minute auction window
// (CHAIN_ACCOUNT_ON_SALE_DELAY): a buy inside the window is a BID and the account changes hands when
// the auction closes (database::account_on_auction_expiration); a buy after the window is a direct
// sale (buy_account_evaluator). Two separate code paths rewrite the authorities, so both are tested.
namespace {
void put_on_sale(agent_fixture& f) {
    set_account_price_operation sp;
    sp.account = f.principal;
    sp.account_seller = f.principal;
    sp.account_offer_price = asset(10000, TOKEN_SYMBOL);
    sp.account_on_sale = true;
    f.node.push_pending_transaction(sign_ops({sp}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);
}
void buy(agent_fixture& f) {
    buy_account_operation bo;
    bo.buyer = f.gp.initiator_name;
    bo.account = f.principal;
    bo.account_offer_price = asset(10000, TOKEN_SYMBOL);
    bo.account_authorities_key = derive_key("buyer-key").get_public_key();
    bo.tokens_to_shares = f.node.db().get_validator_schedule_object().median_props.account_creation_fee;
    f.node.push_pending_transaction(sign_ops({bo}, f.gp.initiator_key, f.node));
    produce(f.node, f.gp, f.when);
}
bool sold_to_buyer(agent_fixture& f) {
    return f.node.db().get<account_authority_object, by_account>(f.principal).active ==
           single_key_auth(derive_key("buyer-key").get_public_key());
}
void produce_past_sale_window(agent_fixture& f) {
    const auto until = f.node.db().get_account(f.principal).account_on_sale_start_time;
    for (int i = 0; i < 400 && f.node.head_block_time() <= until; ++i) produce(f.node, f.gp, f.when);
}
} // anonymous namespace

// The auction-close path (database::account_on_auction_expiration) also wipes, but it cannot be
// tested here yet: a bid extends the auction by the node's WALL CLOCK (buy_account_evaluator), so
// under the virtual clock the auction closes months after the simulation ends. Fixed separately in
// branch fix-auction-wallclock (HF15); add the auction case once that lands in pm.

BOOST_AUTO_TEST_CASE(agent_access_direct_sale_wipes) {
    agent_fixture f(0xAA9E21, "aa-wipe-sale");
    grant(f.node, f.gp, f.when, f.principal, f.principal_key, f.agent, {"transfer"});
    put_on_sale(f);
    produce_past_sale_window(f);
    buy(f);   // after the window: direct sale
    BOOST_REQUIRE_MESSAGE(sold_to_buyer(f), "direct sale did not go through — the wipe check would be vacuous");
    BOOST_CHECK_MESSAGE(!has_row(f.node, f.principal, f.agent), "directly sold account kept its delegation");
}

// recover_account rewrites master through update_master_authority → the delegation goes. To make
// recovery possible the principal first rotates its master (history needs a "recent" master), and
// only then grants — so the row the recovery must wipe really exists at that moment.
BOOST_AUTO_TEST_CASE(agent_access_recovery_wipes) {
    agent_fixture f(0xAA9E22, "aa-wipe-recover");
    const auto stolen = derive_key("principal-master-stolen");
    const auto restored = derive_key("principal-master-restored");

    account_update_operation au;   // the "attacker" swaps master; active stays principal_key
    au.account = f.principal;
    au.master = single_key_auth(stolen.get_public_key());
    f.node.push_pending_transaction(sign_ops({au}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);

    grant(f.node, f.gp, f.when, f.principal, f.principal_key, f.agent, {"transfer"});
    BOOST_REQUIRE(has_row(f.node, f.principal, f.agent));

    request_account_recovery_operation rq;
    rq.recovery_account = f.node.db().get_account(f.principal).recovery_account;
    rq.account_to_recover = f.principal;
    rq.new_master_authority = single_key_auth(restored.get_public_key());
    const auto& ra = rq.recovery_account;
    BOOST_REQUIRE_MESSAGE(ra == f.gp.initiator_name, "fixture assumes the creator is the recovery account");
    f.node.push_pending_transaction(sign_ops({rq}, f.gp.initiator_key, f.node));
    produce(f.node, f.gp, f.when);

    recover_account_operation rc;
    rc.account_to_recover = f.principal;
    rc.new_master_authority = single_key_auth(restored.get_public_key());
    rc.recent_master_authority = single_key_auth(f.principal_key.get_public_key());
    signed_transaction tx = sign_ops({rc}, restored, f.node);
    tx.sign(f.principal_key, f.node.chain_id());
    f.node.push_pending_transaction(tx);
    produce(f.node, f.gp, f.when);

    const bool recovered = f.node.db().get<account_authority_object, by_account>(f.principal).master ==
                           single_key_auth(restored.get_public_key());
    BOOST_REQUIRE_MESSAGE(recovered, "recovery did not go through — the wipe check would be vacuous");
    BOOST_CHECK_MESSAGE(!has_row(f.node, f.principal, f.agent), "recovered account kept its delegation");
}

// Cap: a principal holds at most CHAIN_AGENT_MAX_PER_ACCOUNT live delegations; re-granting an
// existing pair is an overwrite and does not count as a new slot. An expired row is swept on the
// principal's next grant and frees its slot.
BOOST_AUTO_TEST_CASE(agent_access_cap_and_expired_sweep) {
    agent_fixture f(0xAA9E23, "aa-cap");
    std::vector<account_name_type> agents;
    for (int i = 0; i < CHAIN_AGENT_MAX_PER_ACCOUNT + 1; ++i) {
        const std::string n = "agentx" + std::string(1, char('a' + i));
        create_account(f.node, f.gp, f.when, n, derive_key(n), 1000);
        agents.push_back(n);
    }
    // Fill the cap with perpetual grants.
    for (int i = 0; i < CHAIN_AGENT_MAX_PER_ACCOUNT; ++i)
        grant(f.node, f.gp, f.when, f.principal, f.principal_key, agents[i], {"transfer"});
    BOOST_REQUIRE(has_row(f.node, f.principal, agents[CHAIN_AGENT_MAX_PER_ACCOUNT - 1]));

    // The 17th distinct agent is refused.
    set_agent_permission_operation op;
    op.account = f.principal; op.agent = agents[CHAIN_AGENT_MAX_PER_ACCOUNT];
    op.operations.insert("transfer");
    expect_rejected(f.node, sign_ops({op}, f.principal_key, f.node), "17th agent accepted over the cap");

    // Re-granting an existing pair at the cap is an overwrite, not a new slot: here it shortens
    // agents[0]'s grant to a few blocks.
    grant(f.node, f.gp, f.when, f.principal, f.principal_key, agents[0], {"transfer"},
          fc::time_point_sec(f.node.head_block_time() + fc::seconds(CHAIN_BLOCK_INTERVAL * 2)));
    BOOST_REQUIRE(has_row(f.node, f.principal, agents[0]));

    // Once it has expired, the next grant sweeps it and takes the freed slot.
    for (int i = 0; i < 4; ++i) produce(f.node, f.gp, f.when);
    BOOST_REQUIRE_MESSAGE(has_row(f.node, f.principal, agents[0]), "expired row vanished before any grant touched it");
    f.node.push_pending_transaction(sign_ops({op}, f.principal_key, f.node, 1));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_MESSAGE(!has_row(f.node, f.principal, agents[0]), "expired row not swept on the next grant");
    BOOST_CHECK(has_row(f.node, f.principal, agents[CHAIN_AGENT_MAX_PER_ACCOUNT]));

    // Now the cap is full again with live rows only: one more distinct agent is refused.
    const std::string extra = "agentextra";
    create_account(f.node, f.gp, f.when, extra, derive_key(extra), 1000);
    op.agent = extra;
    expect_rejected(f.node, sign_ops({op}, f.principal_key, f.node, 2), "agent over the cap accepted");
}
