// HF15 agent access — chain-level tests for the authority hook in database.cpp.
//
// A principal issues agents: a label plus a public key, each allowed to sign a listed set of
// operations on the principal's behalf. The agent is not an account. tests/pm/agent_access_test.cpp covers the protocol half (operation
// validation and the packed name list); it links the protocol library only
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
#include <graphene/chain/key_history_objects.hpp>
#include <graphene/chain/agent_evaluator.hpp>
#include <graphene/chain/validator_objects.hpp>
#include <graphene/protocol/agent_operations.hpp>
#include <graphene/protocol/proposal_operations.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <graphene/protocol/config.hpp>
#include <graphene/plugins/database_api/signature_discovery.hpp>
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
           const account_name_type& name, const public_key_type& key, const std::vector<std::string>& ops,
           fc::time_point_sec expiration = fc::time_point_sec(), const std::vector<std::string>& addons = {}) {
    set_agent_permission_operation op;
    op.account = principal;
    op.agent_name = name;
    op.agent_key = key;
    op.expiration = expiration;
    for (const auto& s : ops) op.operations.insert(s);
    for (const auto& s : addons) op.addons.insert(s);
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

bool has_row(simulated_node& n, const account_name_type& p, const account_name_type& a) {   // a = agent name
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

// One node per case: HF14, then the HF15 marker, then one ordinary account (the principal) and an
// agent key that belongs to no account. An ordinary account rather than the genesis initiator, so
// the authority path under test is the one a real user has.
struct agent_fixture {
    genesis_params gp;
    virtual_clock clk;
    simulated_node node;
    fc::time_point_sec when;

    account_name_type principal = "principal";
    account_name_type bot = "trading-bot";
    fc::ecc::private_key principal_key = derive_key("principal-key");
    fc::ecc::private_key agent_key = derive_key("agent-key");

    agent_fixture(uint64_t seed, const char* label, bool hf15 = true)
            : gp(make_genesis_params(seed, 1)), clk(sim_start()), node(label, gp, clk),
              when(clk.now() - fc::seconds(CHAIN_BLOCK_INTERVAL)) {
        bring_to_hf14(node, gp, when);
        BOOST_REQUIRE_MESSAGE(node.db().has_hardfork(CHAIN_HARDFORK_14),
                              "harness could not reach HF14 (needs BUILD_TESTNET)");
        if (hf15) enable_hf15(node);
        create_account(node, gp, when, principal, principal_key, 100000);
        vest(node, gp, when, principal, principal_key, 50000);
    }

    void issue(const std::vector<std::string>& ops, fc::time_point_sec exp = fc::time_point_sec(),
               const std::vector<std::string>& addons = {}) {
        grant(node, gp, when, principal, principal_key, bot, agent_key.get_public_key(), ops, exp, addons);
    }
    transfer_operation pay(share_type amount) {
        return transfer_op(principal, gp.initiator_name, amount, TOKEN_SYMBOL);
    }
};

// Account sales. A price set on testnet opens a 10-minute auction window
// (CHAIN_ACCOUNT_ON_SALE_DELAY): a buy inside the window is a BID and the account changes hands when
// the auction closes; a buy after the window is a direct sale. Two code paths, both tested.
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

} // anonymous namespace

// The plain positive: a transaction signed ONLY by the agent key passes for the principal.
BOOST_AUTO_TEST_CASE(agent_access_agent_key_signs_granted_operation) {
    agent_fixture f(0xAA9E17, "aa-granted");
    f.issue({"transfer"});

    const auto before = liquid(f.node, f.gp.initiator_name);
    f.node.push_pending_transaction(sign_ops({f.pay(1000)}, f.agent_key, f.node));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_EQUAL(liquid(f.node, f.gp.initiator_name) - before, 1000);
}

std::string stored_addons(simulated_node& n, const account_name_type& p, const account_name_type& a) {
    const auto& idx = n.db().get_index<agent_permission_index>().indices().get<by_permission_account>();
    auto it = idx.find(boost::make_tuple(p, a));
    return it == idx.end() ? std::string("<no row>") : to_string(it->addons);
}

// Addons (q1718=A) are off-chain scopes: an addon-only agent is a real row the services can read,
// but on chain its key signs for nothing. Adding operations later keeps the addons; clearing both
// lists revokes.
BOOST_AUTO_TEST_CASE(agent_access_addons_are_stored_and_grant_nothing_on_chain) {
    agent_fixture f(0xAA9E30, "aa-addons");
    f.issue({}, fc::time_point_sec(), {"vizhub", "mail"});
    BOOST_REQUIRE(has_row(f.node, f.principal, f.bot));
    BOOST_CHECK_EQUAL(stored_addons(f.node, f.principal, f.bot), "mail,vizhub");

    expect_rejected(f.node, sign_ops({f.pay(1000)}, f.agent_key, f.node), "addon-only agent moved funds");

    f.issue({"transfer"}, fc::time_point_sec(), {"vizhub"});
    BOOST_CHECK_EQUAL(stored_addons(f.node, f.principal, f.bot), "vizhub");
    const auto before = liquid(f.node, f.gp.initiator_name);
    f.node.push_pending_transaction(sign_ops({f.pay(1000)}, f.agent_key, f.node));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_EQUAL(liquid(f.node, f.gp.initiator_name) - before, 1000);

    f.issue({});   // both lists empty = revoke
    BOOST_CHECK(!has_row(f.node, f.principal, f.bot));
}

// Issuing an agent must not break the principal's own transactions.
BOOST_AUTO_TEST_CASE(agent_access_principal_still_signs_for_itself) {
    agent_fixture f(0xAA9E18, "aa-principal");
    f.issue({"transfer"});

    const auto before = liquid(f.node, f.gp.initiator_name);
    f.node.push_pending_transaction(sign_ops({f.pay(2000)}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_EQUAL(liquid(f.node, f.gp.initiator_name) - before, 2000);
}

// A key that was never issued — or was issued by someone else — grants nothing.
BOOST_AUTO_TEST_CASE(agent_access_unknown_key_grants_nothing) {
    agent_fixture f(0xAA9E24, "aa-unknown-key");
    f.issue({"transfer"});
    expect_rejected(f.node, sign_ops({f.pay(1000)}, derive_key("stranger"), f.node),
                    "a key that is not an agent of the principal signed for it");
}

// An agent's list is not a blank cheque: EVERY authority-requiring operation must be on it.
BOOST_AUTO_TEST_CASE(agent_access_requires_full_coverage_of_the_transaction) {
    agent_fixture f(0xAA9E19, "aa-coverage");
    f.issue({"transfer"});

    transfer_to_vesting_operation tv;
    tv.from = f.principal; tv.to = f.principal;
    tv.amount = asset(3000, TOKEN_SYMBOL);
    expect_rejected(f.node, sign_ops({f.pay(3000), tv}, f.agent_key, f.node),
                    "agent acted outside its list: one operation of the transaction was not listed");

    // Control: the principal signing the same pair is accepted.
    const auto before = liquid(f.node, f.gp.initiator_name);
    f.node.push_pending_transaction(sign_ops({f.pay(3000), tv}, f.principal_key, f.node, 1));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_EQUAL(liquid(f.node, f.gp.initiator_name) - before, 3000);
}

// A transaction that needs master authority is never answered by an agent key, even when the rest
// of it is on the list. (Master-only operations are also on the deny-list, so this is the second,
// structural wall.)
BOOST_AUTO_TEST_CASE(agent_access_never_reaches_master_authority) {
    agent_fixture f(0xAA9E1A, "aa-master");
    f.issue({"transfer"});

    change_recovery_account_operation cr;
    cr.account_to_recover = f.principal;
    cr.new_recovery_account = f.gp.initiator_name;
    expect_rejected(f.node, sign_ops({f.pay(1000), cr}, f.agent_key, f.node),
                    "agent key answered inside a master-authority transaction");
}

// Row validity: each way a row can be dead or illegal grants nothing.
BOOST_AUTO_TEST_CASE(agent_access_invalid_rows_grant_nothing) {
    agent_fixture f(0xAA9E1B, "aa-rows");

    // 1) No row at all.
    expect_rejected(f.node, sign_ops({f.pay(4000)}, f.agent_key, f.node), "agent key acted with no row");

    // 2) An expiration in the past revokes rather than errors, so no row is left behind.
    f.issue({"transfer"}, fc::time_point_sec(f.node.db().head_block_time() - fc::seconds(60)));
    BOOST_CHECK_MESSAGE(!has_row(f.node, f.principal, f.bot), "an expired grant left a row behind");

    // 3) A never-delegable name is refused at grant time.
    set_agent_permission_operation bad;
    bad.account = f.principal;
    bad.agent_name = f.bot;
    bad.agent_key = f.agent_key.get_public_key();
    bad.operations.insert("recover_account");
    expect_rejected(f.node, sign_ops({bad}, f.principal_key, f.node), "granting master-only op was accepted");
    for (const char* name : {"vote", "content", "delete_content"}) {
        bad.operations.clear();
        bad.operations.insert(name);
        expect_rejected(f.node, sign_ops({bad}, f.principal_key, f.node, 3),
                        "granting deprecated op was accepted");
        BOOST_CHECK(!has_row(f.node, f.principal, f.bot));
    }

    // 4) Unknown name.
    bad.operations.clear();
    bad.operations.insert("no_such_operation");
    expect_rejected(f.node, sign_ops({bad}, f.principal_key, f.node, 1), "granting an unknown name was accepted");

    // 5) Revoke by name: a live agent, then an empty list removes it and its key stops working.
    f.issue({"transfer"});
    BOOST_REQUIRE(has_row(f.node, f.principal, f.bot));
    grant(f.node, f.gp, f.when, f.principal, f.principal_key, f.bot, public_key_type(), {});
    BOOST_CHECK(!has_row(f.node, f.principal, f.bot));
    expect_rejected(f.node, sign_ops({f.pay(4000)}, f.agent_key, f.node, 2), "revoked agent key still signs");
}

// The agent key stands in for the principal only where the principal is required — never wherever
// the principal's authority happens to be nested in another account.
BOOST_AUTO_TEST_CASE(agent_access_does_not_leak_through_nested_authorities) {
    agent_fixture f(0xAA9E1C, "aa-nested");

    const account_name_type second = "second";
    fc::ecc::private_key second_key = derive_key("second-key");
    create_account(f.node, f.gp, f.when, second, second_key, 100000);

    authority nested;
    nested.weight_threshold = 1;
    nested.account_auths[f.principal] = 1;
    account_update_operation au;
    au.account = second;
    au.master = single_key_auth(second_key.get_public_key());
    au.active = nested;
    f.node.push_pending_transaction(sign_ops({au}, second_key, f.node));
    produce(f.node, f.gp, f.when);

    // Control: the principal's own key reaches second's authority by nesting.
    const auto before = liquid(f.node, f.gp.initiator_name);
    f.node.push_pending_transaction(
        sign_ops({transfer_op(second, f.gp.initiator_name, 5000, TOKEN_SYMBOL)}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_EQUAL(liquid(f.node, f.gp.initiator_name) - before, 5000);

    f.issue({"transfer"});
    expect_rejected(f.node,
                    sign_ops({transfer_op(second, f.gp.initiator_name, 6000, TOKEN_SYMBOL)}, f.agent_key, f.node),
                    "the principal's agent key reached an account that only nests the principal");
}

BOOST_AUTO_TEST_CASE(agent_access_two_transfers_cannot_spend_nested_account) {
    agent_fixture f(0xAA9E31, "aa-two-transfers");
    const account_name_type second = "second";
    const auto second_key = derive_key("second-owner");
    create_account(f.node, f.gp, f.when, second, second_key, 100000);
    authority nested;
    nested.weight_threshold = 1;
    nested.account_auths[f.principal] = 1;
    account_update_operation au;
    au.account = second;
    au.active = nested;
    f.node.push_pending_transaction(sign_ops({au}, second_key, f.node));
    produce(f.node, f.gp, f.when);
    f.issue({"transfer"});
    const auto before = liquid(f.node, f.gp.initiator_name);
    expect_rejected(f.node, sign_ops({f.pay(1000),
        transfer_op(second, f.gp.initiator_name, 1000, TOKEN_SYMBOL)}, f.agent_key, f.node),
        "agent of A spent B via nested A authority in the same transaction");
    BOOST_CHECK_EQUAL(liquid(f.node, f.gp.initiator_name), before);
    // Positive control: the identical agent key still transfers from A alone.
    f.node.push_pending_transaction(sign_ops({f.pay(1000)}, f.agent_key, f.node));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_EQUAL(liquid(f.node, f.gp.initiator_name) - before, 1000);
    auto mixed = sign_ops({f.pay(1000),
        transfer_op(second, f.gp.initiator_name, 1000, TOKEN_SYMBOL)}, f.agent_key, f.node, 1);
    mixed.sign(second_key, f.node.chain_id());
    f.node.push_pending_transaction(mixed);
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_EQUAL(liquid(f.node, f.gp.initiator_name) - before, 3000);
}

BOOST_AUTO_TEST_CASE(agent_access_regular_and_same_transaction_revoke) {
    agent_fixture f(0xAA9E32, "aa-regular-revoke");
    f.issue({"account_metadata", "set_agent_permission", "transfer"});
    account_metadata_operation metadata;
    metadata.account = f.principal;
    metadata.json_metadata = "{}";
    f.node.push_pending_transaction(sign_ops({metadata}, f.agent_key, f.node));
    produce(f.node, f.gp, f.when);
    set_agent_permission_operation revoke;
    revoke.account = f.principal;
    revoke.agent_name = f.bot;
    expect_rejected(f.node, sign_ops({revoke, f.pay(1000)}, f.agent_key, f.node),
                    "agent used revoked permission in later operation of same transaction");
    BOOST_CHECK(has_row(f.node, f.principal, f.bot));
}

BOOST_AUTO_TEST_CASE(agent_access_management_requires_explicit_scope) {
    agent_fixture f(0xAA9E33, "aa-agent-management");
    f.issue({"set_agent_permission"});
    set_agent_permission_operation wider;
    wider.account = f.principal;
    wider.agent_name = "expanded";
    wider.agent_key = f.agent_key.get_public_key();
    wider.operations.insert("transfer");
    f.node.push_pending_transaction(sign_ops({wider}, f.agent_key, f.node));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK(has_row(f.node, f.principal, "expanded"));
    f.node.push_pending_transaction(sign_ops({f.pay(1000)}, f.agent_key, f.node));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK(has_row(f.node, f.principal, f.bot));
}

BOOST_AUTO_TEST_CASE(agent_access_proposal_update_cannot_approve_unscoped_transfer) {
    agent_fixture f(0xAA9E36, "aa-proposal-escape");
    const auto author_key = derive_key("proposal-author");
    create_account(f.node, f.gp, f.when, "author", author_key, 100000);
    vest(f.node, f.gp, f.when, "author", author_key, 50000);
    proposal_create_operation create;
    create.author = "author";
    create.title = "principal-transfer";
    create.expiration_time = f.node.head_block_time() + fc::seconds(600);
    create.proposed_operations.push_back(operation_wrapper(f.pay(1000)));
    f.node.push_pending_transaction(sign_ops({create}, author_key, f.node));
    produce(f.node, f.gp, f.when);
    BOOST_REQUIRE(f.node.db().find_proposal("author", create.title) != nullptr);

    // A previously issued proposal_update grant must not turn into an unrestricted
    // active approval. Construct the row directly to exercise execution-time denial too.
    f.node.db().create<agent_permission_object>([&](agent_permission_object& row) {
        row.account = f.principal;
        row.agent_name = f.bot;
        row.agent_key = f.agent_key.get_public_key();
        from_string(row.operations, std::string("proposal_update"));
    });
    proposal_update_operation approve;
    approve.author = create.author;
    approve.title = create.title;
    approve.active_approvals_to_add.insert(f.principal);
    const auto before = liquid(f.node, f.principal);
    expect_rejected(f.node, sign_ops({approve}, f.agent_key, f.node),
                    "proposal_update-only agent approved an unscoped transfer");
    BOOST_CHECK_EQUAL(liquid(f.node, f.principal), before);
    // Ordinary owner approval still executes the proposed transfer.
    f.node.push_pending_transaction(sign_ops({approve}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_EQUAL(before - liquid(f.node, f.principal), 1000);
}

BOOST_AUTO_TEST_CASE(agent_access_account_update_is_not_delegable) {
    agent_fixture f(0xAA9E35, "aa-account-update-roles");
    set_agent_permission_operation bad;
    bad.account = f.principal;
    bad.agent_name = f.bot;
    bad.agent_key = f.agent_key.get_public_key();
    bad.operations.insert("account_update");
    expect_rejected(f.node, sign_ops({bad}, f.principal_key, f.node),
                    "account_update grant rotated active authority");
    account_update_operation master_change;
    master_change.account = f.principal;
    master_change.master = single_key_auth(derive_key("new-master").get_public_key());
    expect_rejected(f.node, sign_ops({master_change}, f.agent_key, f.node),
                    "agent satisfied master account_update");
    account_update_operation active_change;
    active_change.account = f.principal;
    active_change.active = single_key_auth(derive_key("new-active").get_public_key());
    expect_rejected(f.node, sign_ops({active_change}, f.agent_key, f.node),
                    "agent rotated active authority without grant");
    f.node.push_pending_transaction(sign_ops({active_change}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK((f.node.db().get<account_authority_object, by_account>(f.principal).active ==
                 *active_change.active));
}

BOOST_AUTO_TEST_CASE(agent_access_failed_active_branch_does_not_consume_partial_signature) {
    agent_fixture f(0xAA9E37, "aa-fallback-keys");
    const auto partial = derive_key("partial-active");
    const auto second = derive_key("second-active");
    account_update_operation rotate;
    rotate.account = f.principal;
    authority two;
    two.weight_threshold = 2;
    two.key_auths[partial.get_public_key()] = 1;
    two.key_auths[second.get_public_key()] = 1;
    rotate.active = two;
    f.node.push_pending_transaction(sign_ops({rotate}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);

    auto surplus = sign_ops({f.pay(1000)}, partial, f.node);
    surplus.sign(f.principal_key, f.node.chain_id()); // unchanged master, sufficient alone
    BOOST_CHECK_THROW(verify_agent_transaction(f.node.db(), surplus,
                        surplus.get_signature_keys(f.node.chain_id())), tx_irrelevant_sig);
    expect_rejected(f.node, surplus, "partial active signature survived master fallback");
    const auto before = liquid(f.node, f.gp.initiator_name);
    f.node.push_pending_transaction(sign_ops({f.pay(1000)}, f.principal_key, f.node, 1));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_EQUAL(liquid(f.node, f.gp.initiator_name) - before, 1000);
}

BOOST_AUTO_TEST_CASE(agent_access_ordinary_rotation_then_active_operation_keeps_entry_authority) {
    agent_fixture f(0xAA9E38, "aa-ordinary-rotation");
    account_update_operation rotate;
    rotate.account = f.principal;
    rotate.active = single_key_auth(derive_key("new-ordinary-active").get_public_key());
    const auto before = liquid(f.node, f.gp.initiator_name);
    f.node.push_pending_transaction(sign_ops({rotate, f.pay(1000)}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_EQUAL(liquid(f.node, f.gp.initiator_name) - before, 1000);
    BOOST_CHECK((f.node.db().get<account_authority_object, by_account>(f.principal).active ==
                 *rotate.active));
}

BOOST_AUTO_TEST_CASE(agent_access_failed_regular_branch_does_not_consume_partial_signature) {
    agent_fixture f(0xAA9E39, "aa-regular-fallback");
    const auto partial = derive_key("partial-regular");
    const auto second = derive_key("second-regular");
    account_update_operation rotate;
    rotate.account = f.principal;
    authority two;
    two.weight_threshold = 2;
    two.key_auths[partial.get_public_key()] = 1;
    two.key_auths[second.get_public_key()] = 1;
    rotate.regular = two;
    f.node.push_pending_transaction(sign_ops({rotate}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);
    account_metadata_operation metadata;
    metadata.account = f.principal;
    metadata.json_metadata = "{}";
    auto surplus = sign_ops({metadata}, partial, f.node);
    surplus.sign(f.principal_key, f.node.chain_id());
    BOOST_CHECK_THROW(verify_agent_transaction(f.node.db(), surplus,
                        surplus.get_signature_keys(f.node.chain_id())), tx_irrelevant_sig);
    expect_rejected(f.node, surplus, "partial regular signature survived active fallback");
    f.node.push_pending_transaction(sign_ops({metadata}, f.principal_key, f.node, 1));
    produce(f.node, f.gp, f.when);
}

BOOST_AUTO_TEST_CASE(agent_access_rpc_signature_discovery_returns_partial_multisig_keys) {
    agent_fixture f(0xAA9E40, "aa-rpc-partial");
    const auto first = derive_key("rpc-first");
    const auto second = derive_key("rpc-second");
    account_update_operation rotate;
    rotate.account = f.principal;
    authority two;
    two.weight_threshold = 2;
    two.key_auths[first.get_public_key()] = 1;
    two.key_auths[second.get_public_key()] = 1;
    rotate.active = two;
    f.node.push_pending_transaction(sign_ops({rotate}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);

    signed_transaction unsigned_tx;
    unsigned_tx.operations.push_back(f.pay(1000));
    const auto partial = graphene::plugins::database_api::get_required_signatures_for_api(
        f.node.db(), unsigned_tx, {first.get_public_key()});
    BOOST_CHECK_EQUAL(partial.size(), 1u);
    BOOST_CHECK(partial.count(first.get_public_key()) == 1);
    const auto sufficient = graphene::plugins::database_api::get_required_signatures_for_api(
        f.node.db(), unsigned_tx, {first.get_public_key(), second.get_public_key()});
    BOOST_CHECK_EQUAL(sufficient.size(), 2u);
    BOOST_CHECK(sufficient.count(first.get_public_key()) == 1);
    BOOST_CHECK(sufficient.count(second.get_public_key()) == 1);
}

BOOST_AUTO_TEST_CASE(agent_access_rpc_authority_core_uses_same_rules_as_chain) {
    agent_fixture f(0xAA9E34, "aa-rpc-parity");
    f.issue({"transfer", "account_metadata"});
    auto transfer = sign_ops({f.pay(1000)}, f.agent_key, f.node);
    fc::flat_set<public_key_type> used;
    verify_agent_transaction(f.node.db(), transfer,
                             transfer.get_signature_keys(f.node.chain_id()), false, &used);
    BOOST_CHECK(used.count(f.agent_key.get_public_key()));
    // get_required_signatures passes the available keys without a signature; the same core
    // decides which direct principal key is consumed. get_potential_signatures enumerates rows.
    signed_transaction unsigned_transfer;
    unsigned_transfer.operations.push_back(f.pay(1000));
    const fc::flat_set<public_key_type> candidates{f.agent_key.get_public_key()};
    verify_agent_transaction(f.node.db(), unsigned_transfer, candidates, true, &used);
    BOOST_CHECK(used.count(f.agent_key.get_public_key()));
    account_metadata_operation metadata;
    metadata.account = f.principal;
    metadata.json_metadata = "{}";
    signed_transaction unsigned_regular;
    unsigned_regular.operations.push_back(metadata);
    verify_agent_transaction(f.node.db(), unsigned_regular, candidates, true, &used);
    BOOST_CHECK(used.count(f.agent_key.get_public_key()));
    const fc::flat_set<public_key_type> empty;
    BOOST_CHECK_THROW(verify_agent_transaction(f.node.db(), unsigned_regular, empty, true, &used),
                      tx_missing_regular_auth);
}

// Multiple labels may share a key; replacement of one label does not mutate the other.
BOOST_AUTO_TEST_CASE(agent_access_shared_key_and_rotation) {
    agent_fixture f(0xAA9E25, "aa-key-unique");
    f.issue({"transfer"});

    set_agent_permission_operation dup;
    dup.account = f.principal;
    dup.agent_name = "other-bot";
    dup.agent_key = f.agent_key.get_public_key();
    dup.operations.insert("award");
    f.node.push_pending_transaction(sign_ops({dup}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK(has_row(f.node, f.principal, "other-bot"));

    // Re-issuing the SAME name with a new key replaces the key: the old one stops working.
    const auto new_key = derive_key("agent-key-2");
    grant(f.node, f.gp, f.when, f.principal, f.principal_key, f.bot, new_key.get_public_key(), {"transfer"});
    expect_rejected(f.node, sign_ops({f.pay(1000)}, f.agent_key, f.node), "replaced key still signs");
    const auto before = liquid(f.node, f.gp.initiator_name);
    f.node.push_pending_transaction(sign_ops({f.pay(1000)}, new_key, f.node, 1));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_EQUAL(liquid(f.node, f.gp.initiator_name) - before, 1000);
}

// Wipes: the agents never outlive the owner keys they were issued under.
BOOST_AUTO_TEST_CASE(agent_access_active_change_wipes) {
    agent_fixture f(0xAA9E1D, "aa-wipe-active");
    f.issue({"transfer"});

    account_update_operation au;
    au.account = f.principal;
    au.active = single_key_auth(derive_key("principal-key-2").get_public_key());
    f.node.push_pending_transaction(sign_ops({au}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);

    BOOST_CHECK_MESSAGE(!has_row(f.node, f.principal, f.bot), "active change left the agent");
    expect_rejected(f.node, sign_ops({f.pay(1000)}, f.agent_key, f.node), "agent key signed after active change");
}

BOOST_AUTO_TEST_CASE(agent_access_master_change_wipes) {
    agent_fixture f(0xAA9E1F, "aa-wipe-master");
    f.issue({"transfer"});

    account_update_operation au;
    au.account = f.principal;
    au.master = single_key_auth(derive_key("principal-master-2").get_public_key());
    f.node.push_pending_transaction(sign_ops({au}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);

    BOOST_CHECK_MESSAGE(!has_row(f.node, f.principal, f.bot), "master change left the agent");
}

// A regular-only change keeps the agents: they never stood on regular keys.
BOOST_AUTO_TEST_CASE(agent_access_regular_change_keeps_agents) {
    agent_fixture f(0xAA9E1E, "aa-keep-regular");
    f.issue({"transfer"});

    account_update_operation au;
    au.account = f.principal;
    au.regular = single_key_auth(derive_key("principal-regular-2").get_public_key());
    f.node.push_pending_transaction(sign_ops({au}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);

    BOOST_CHECK(has_row(f.node, f.principal, f.bot));
}

BOOST_AUTO_TEST_CASE(agent_access_direct_sale_wipes) {
    agent_fixture f(0xAA9E21, "aa-wipe-sale");
    f.issue({"transfer"});
    put_on_sale(f);
    const auto until = f.node.db().get_account(f.principal).account_on_sale_start_time;
    for (int i = 0; i < 400 && f.node.head_block_time() <= until; ++i) produce(f.node, f.gp, f.when);
    buy(f);
    BOOST_REQUIRE_MESSAGE(sold_to_buyer(f), "direct sale did not go through — the wipe check would be vacuous");
    BOOST_CHECK_MESSAGE(!has_row(f.node, f.principal, f.bot), "sold account kept its agent");
}

BOOST_AUTO_TEST_CASE(agent_access_auction_close_wipes) {
    agent_fixture f(0xAA9E20, "aa-wipe-auction");
    f.issue({"transfer"});
    put_on_sale(f);
    buy(f);   // inside the window: a bid
    BOOST_REQUIRE_MESSAGE(!sold_to_buyer(f), "expected a bid, got an immediate sale");
    BOOST_CHECK_MESSAGE(has_row(f.node, f.principal, f.bot), "a mere bid already wiped the agent");
    for (int i = 0; i < 400 && !sold_to_buyer(f); ++i) produce(f.node, f.gp, f.when);
    BOOST_REQUIRE_MESSAGE(sold_to_buyer(f), "auction did not close — the wipe check would be vacuous");
    BOOST_CHECK_MESSAGE(!has_row(f.node, f.principal, f.bot), "account sold at auction kept its agent");
}

// Recovery: the principal first rotates master (recovery needs a "recent" master to point at), then
// issues the agent, then recovers — so the row the recovery must wipe really exists at that moment.
BOOST_AUTO_TEST_CASE(agent_access_recovery_wipes) {
    agent_fixture f(0xAA9E22, "aa-wipe-recover");
    const auto stolen = derive_key("principal-master-stolen");
    const auto restored = derive_key("principal-master-restored");

    account_update_operation au;
    au.account = f.principal;
    au.master = single_key_auth(stolen.get_public_key());
    f.node.push_pending_transaction(sign_ops({au}, f.principal_key, f.node));
    produce(f.node, f.gp, f.when);

    f.issue({"transfer"});
    BOOST_REQUIRE(has_row(f.node, f.principal, f.bot));

    request_account_recovery_operation rq;
    rq.recovery_account = f.node.db().get_account(f.principal).recovery_account;
    rq.account_to_recover = f.principal;
    rq.new_master_authority = single_key_auth(restored.get_public_key());
    BOOST_REQUIRE_MESSAGE(rq.recovery_account == f.gp.initiator_name, "fixture assumes the creator recovers");
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
    BOOST_CHECK_MESSAGE(!has_row(f.node, f.principal, f.bot), "recovered account kept its agent");
}

// Cap: at most CHAIN_AGENT_MAX_PER_ACCOUNT live agents; re-issuing a name is an overwrite; an
// expired agent is swept on the principal's next grant and frees its slot.
BOOST_AUTO_TEST_CASE(agent_access_cap_and_expired_sweep) {
    agent_fixture f(0xAA9E23, "aa-cap");
    auto name = [](int i) { return account_name_type("bot-" + std::to_string(i)); };
    auto key = [](int i) { return derive_key("bot-key-" + std::to_string(i)).get_public_key(); };

    for (int i = 0; i < CHAIN_AGENT_MAX_PER_ACCOUNT; ++i)
        grant(f.node, f.gp, f.when, f.principal, f.principal_key, name(i), key(i), {"transfer"});
    BOOST_REQUIRE(has_row(f.node, f.principal, name(CHAIN_AGENT_MAX_PER_ACCOUNT - 1)));

    set_agent_permission_operation op;
    op.account = f.principal;
    op.agent_name = name(CHAIN_AGENT_MAX_PER_ACCOUNT);
    op.agent_key = key(CHAIN_AGENT_MAX_PER_ACCOUNT);
    op.operations.insert("transfer");
    expect_rejected(f.node, sign_ops({op}, f.principal_key, f.node), "17th agent accepted over the cap");

    // Re-issuing an existing name at the cap is an overwrite: here it shortens bot-0 to a few blocks.
    grant(f.node, f.gp, f.when, f.principal, f.principal_key, name(0), key(0), {"transfer"},
          fc::time_point_sec(f.node.head_block_time() + fc::seconds(CHAIN_BLOCK_INTERVAL * 2)));
    BOOST_REQUIRE(has_row(f.node, f.principal, name(0)));

    for (int i = 0; i < 4; ++i) produce(f.node, f.gp, f.when);
    BOOST_REQUIRE_MESSAGE(has_row(f.node, f.principal, name(0)), "expired row vanished before any grant");
    f.node.push_pending_transaction(sign_ops({op}, f.principal_key, f.node, 1));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_MESSAGE(!has_row(f.node, f.principal, name(0)), "expired agent not swept on the next grant");
    BOOST_CHECK(has_row(f.node, f.principal, name(CHAIN_AGENT_MAX_PER_ACCOUNT)));

    op.agent_name = "bot-extra";
    op.agent_key = derive_key("bot-key-extra").get_public_key();
    expect_rejected(f.node, sign_ops({op}, f.principal_key, f.node, 2), "agent over the cap accepted");
}

// ───────────────────────── HF15 key history ─────────────────────────
// Every key an account stops standing behind is kept forever: a DLT node has no past blocks, so
// without these rows nobody could prove which key an account held when it signed something.

namespace {

std::vector<key_history_object> history(simulated_node& n, const account_name_type& who) {
    std::vector<key_history_object> rows;
    const auto& idx = n.db().get_index<key_history_index>().indices().get<by_id>();
    for (const auto& r : idx) if (r.account == who) rows.push_back(r);
    return rows;
}

size_t rows_of(simulated_node& n, const account_name_type& who, uint8_t role) {
    size_t c = 0;
    for (const auto& r : history(n, who)) if (r.role == role) ++c;
    return c;
}

void update(agent_fixture& f, const account_update_operation& au, uint32_t nonce = 0) {
    f.node.push_pending_transaction(sign_ops({au}, f.principal_key, f.node, nonce));
    produce(f.node, f.gp, f.when);
}

void advance(agent_fixture& f, fc::microseconds d) {
    const auto until = f.node.head_block_time() + d;
    while (f.node.head_block_time() <= until) produce(f.node, f.gp, f.when);
}

} // anonymous namespace

// The shape of a row: old key, weight, threshold, the block and time it stopped being valid.
BOOST_AUTO_TEST_CASE(key_history_active_change_records_old_key) {
    agent_fixture f(0xAB0001, "kh-active");
    const auto old_pub = f.principal_key.get_public_key();
    BOOST_REQUIRE(history(f.node, f.principal).empty());

    account_update_operation au;
    au.account = f.principal;
    au.active = single_key_auth(derive_key("principal-active-2").get_public_key());
    update(f, au);

    const auto rows = history(f.node, f.principal);
    BOOST_REQUIRE_EQUAL(rows.size(), 1u);
    BOOST_CHECK_EQUAL(rows[0].role, key_role_active);
    BOOST_CHECK(rows[0].key == old_pub);
    BOOST_CHECK(rows[0].auth_account == account_name_type());
    BOOST_CHECK_EQUAL(rows[0].weight, 1);
    BOOST_CHECK_EQUAL(rows[0].weight_threshold, 1u);
    // The change landed in the head block; the old key held through the block before it.
    BOOST_CHECK_EQUAL(rows[0].valid_until_block, f.node.db().head_block_num() - 1);
    BOOST_CHECK(rows[0].valid_until_time == f.node.head_block_time() - fc::seconds(CHAIN_BLOCK_INTERVAL));
}

// Master, regular and memo each land under their own role; nothing is written for an unchanged role.
BOOST_AUTO_TEST_CASE(key_history_each_role_recorded_separately) {
    agent_fixture f(0xAB0002, "kh-roles");
    account_update_operation au;
    au.account = f.principal;
    au.master = single_key_auth(derive_key("m2").get_public_key());
    au.regular = single_key_auth(derive_key("r2").get_public_key());
    au.memo_key = derive_key("memo2").get_public_key();
    update(f, au);

    BOOST_CHECK_EQUAL(rows_of(f.node, f.principal, key_role_master), 1u);
    BOOST_CHECK_EQUAL(rows_of(f.node, f.principal, key_role_regular), 1u);
    BOOST_CHECK_EQUAL(rows_of(f.node, f.principal, key_role_memo), 1u);
    BOOST_CHECK_EQUAL(rows_of(f.node, f.principal, key_role_active), 0u);
    for (const auto& r : history(f.node, f.principal)) {
        BOOST_CHECK(r.key == f.principal_key.get_public_key());
        if (r.role == key_role_memo) { BOOST_CHECK_EQUAL(r.weight, 0); BOOST_CHECK_EQUAL(r.weight_threshold, 0u); }
    }
}

// A multi-member authority: one row per member, keys and accounts alike, each with its own weight.
BOOST_AUTO_TEST_CASE(key_history_multi_member_authority) {
    agent_fixture f(0xAB0003, "kh-multi");
    authority multi;
    multi.weight_threshold = 3;
    multi.key_auths[f.principal_key.get_public_key()] = 2;
    multi.key_auths[derive_key("second").get_public_key()] = 1;
    multi.account_auths[f.gp.initiator_name] = 1;
    account_update_operation au;
    au.account = f.principal;
    au.regular = multi;
    update(f, au);
    BOOST_REQUIRE_EQUAL(rows_of(f.node, f.principal, key_role_regular), 1u);   // the old single key

    advance(f, CHAIN_MASTER_UPDATE_LIMIT);
    au.regular = single_key_auth(derive_key("r3").get_public_key());
    update(f, au, 1);

    size_t keys = 0, accounts = 0;
    for (const auto& r : history(f.node, f.principal)) {
        if (r.role != key_role_regular || r.id == history(f.node, f.principal)[0].id) continue;
        BOOST_CHECK_EQUAL(r.weight_threshold, 3u);
        if (r.auth_account == f.gp.initiator_name) { ++accounts; BOOST_CHECK_EQUAL(r.weight, 1); }
        else if (r.key == f.principal_key.get_public_key()) { ++keys; BOOST_CHECK_EQUAL(r.weight, 2); }
        else { ++keys; BOOST_CHECK_EQUAL(r.weight, 1); }
    }
    BOOST_CHECK_EQUAL(keys, 2u);
    BOOST_CHECK_EQUAL(accounts, 1u);
}

// Re-sending the current authority is not a change: no row, and no limit either.
BOOST_AUTO_TEST_CASE(key_history_same_authority_writes_nothing) {
    agent_fixture f(0xAB0004, "kh-same");
    account_update_operation au;
    au.account = f.principal;
    au.active = single_key_auth(f.principal_key.get_public_key());
    au.memo_key = f.principal_key.get_public_key();
    update(f, au);
    update(f, au, 1);
    BOOST_CHECK(history(f.node, f.principal).empty());
}

// Once an hour per role: a second real change of the same role is refused inside the hour, another
// role is not blocked by it, and the same role goes through once the hour is over.
BOOST_AUTO_TEST_CASE(key_history_hourly_limit_per_role) {
    agent_fixture f(0xAB0005, "kh-limit");
    account_update_operation au;
    au.account = f.principal;
    au.active = single_key_auth(derive_key("a2").get_public_key());
    update(f, au);
    const auto active2 = derive_key("a2");

    account_update_operation again;
    again.account = f.principal;
    again.active = single_key_auth(derive_key("a3").get_public_key());
    expect_rejected(f.node, sign_ops({again}, active2, f.node), "second active change inside the hour");

    account_update_operation memo;
    memo.account = f.principal;
    memo.memo_key = derive_key("memo2").get_public_key();
    f.node.push_pending_transaction(sign_ops({memo}, active2, f.node));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_EQUAL(rows_of(f.node, f.principal, key_role_memo), 1u);

    account_update_operation memo3 = memo;
    memo3.memo_key = derive_key("memo3").get_public_key();
    expect_rejected(f.node, sign_ops({memo3}, active2, f.node, 1), "second memo change inside the hour");
    advance(f, CHAIN_MASTER_UPDATE_LIMIT);
    f.node.push_pending_transaction(sign_ops({again}, active2, f.node, 2));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK_EQUAL(rows_of(f.node, f.principal, key_role_active), 2u);
}

// Recovery and sale change keys outside account_update — they must leave history too, and are not
// subject to the hourly limit.
BOOST_AUTO_TEST_CASE(key_history_direct_sale_records_all_roles) {
    agent_fixture f(0xAB0006, "kh-sale");
    put_on_sale(f);
    const auto until = f.node.db().get_account(f.principal).account_on_sale_start_time;
    for (int i = 0; i < 400 && f.node.head_block_time() <= until; ++i) produce(f.node, f.gp, f.when);
    buy(f);
    BOOST_REQUIRE(sold_to_buyer(f));
    for (uint8_t role : {key_role_master, key_role_active, key_role_regular, key_role_memo})
        BOOST_CHECK_EQUAL(rows_of(f.node, f.principal, role), 1u);
}

BOOST_AUTO_TEST_CASE(key_history_auction_close_records_all_roles) {
    agent_fixture f(0xAB0007, "kh-auction");
    put_on_sale(f);
    buy(f);
    BOOST_REQUIRE(!sold_to_buyer(f));
    BOOST_CHECK(history(f.node, f.principal).empty());
    for (int i = 0; i < 400 && !sold_to_buyer(f); ++i) produce(f.node, f.gp, f.when);
    BOOST_REQUIRE(sold_to_buyer(f));
    for (uint8_t role : {key_role_master, key_role_active, key_role_regular, key_role_memo})
        BOOST_CHECK_EQUAL(rows_of(f.node, f.principal, role), 1u);
}

// Lookup by key finds the owner and the block the key stopped being valid.
BOOST_AUTO_TEST_CASE(key_history_lookup_by_key) {
    agent_fixture f(0xAB0008, "kh-by-key");
    account_update_operation au;
    au.account = f.principal;
    au.active = single_key_auth(derive_key("a2").get_public_key());
    update(f, au);
    const auto& idx = f.node.db().get_index<key_history_index>().indices().get<by_key>();
    auto itr = idx.lower_bound(boost::make_tuple(public_key_type(f.principal_key.get_public_key())));
    BOOST_REQUIRE(itr != idx.end());
    BOOST_CHECK(itr->account == f.principal);
    BOOST_CHECK_EQUAL(itr->valid_until_block, f.node.db().head_block_num() - 1);
}

// Before HF15 nothing is written and the hourly limit does not exist.
BOOST_AUTO_TEST_CASE(key_history_inactive_before_hf15) {
    agent_fixture f(0xAB0009, "kh-pre", false);
    BOOST_REQUIRE(!f.node.db().has_hardfork(CHAIN_HARDFORK_15));
    account_update_operation au;
    au.account = f.principal;
    au.active = single_key_auth(derive_key("a2").get_public_key());
    update(f, au);
    au.active = single_key_auth(derive_key("a3").get_public_key());
    f.node.push_pending_transaction(sign_ops({au}, derive_key("a2"), f.node, 1));
    produce(f.node, f.gp, f.when);
    BOOST_CHECK(history(f.node, f.principal).empty());
}
