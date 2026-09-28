// HF15 agent access — chain-level tests for the authority hook in database.cpp.
//
// A principal issues agents: a label plus a public key, each allowed to sign a listed set of
// operations on the principal's behalf. The agent is not an account. tests/pm/agent_access_test.cpp covers the protocol half (operation
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
           const account_name_type& name, const public_key_type& key, const std::vector<std::string>& ops,
           fc::time_point_sec expiration = fc::time_point_sec()) {
    set_agent_permission_operation op;
    op.account = principal;
    op.agent_name = name;
    op.agent_key = key;
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

    agent_fixture(uint64_t seed, const char* label)
            : gp(make_genesis_params(seed, 1)), clk(sim_start()), node(label, gp, clk),
              when(clk.now() - fc::seconds(CHAIN_BLOCK_INTERVAL)) {
        bring_to_hf14(node, gp, when);
        BOOST_REQUIRE_MESSAGE(node.db().has_hardfork(CHAIN_HARDFORK_14),
                              "harness could not reach HF14 (needs BUILD_TESTNET)");
        enable_hf15(node);
        create_account(node, gp, when, principal, principal_key, 100000);
        vest(node, gp, when, principal, principal_key, 50000);
    }

    void issue(const std::vector<std::string>& ops, fc::time_point_sec exp = fc::time_point_sec()) {
        grant(node, gp, when, principal, principal_key, bot, agent_key.get_public_key(), ops, exp);
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
    bad.operations.insert("account_update");
    expect_rejected(f.node, sign_ops({bad}, f.principal_key, f.node), "granting account_update was accepted");

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

// One key, one agent: the same key under a second name is refused.
BOOST_AUTO_TEST_CASE(agent_access_key_is_unique_per_principal) {
    agent_fixture f(0xAA9E25, "aa-key-unique");
    f.issue({"transfer"});

    set_agent_permission_operation dup;
    dup.account = f.principal;
    dup.agent_name = "other-bot";
    dup.agent_key = f.agent_key.get_public_key();
    dup.operations.insert("award");
    expect_rejected(f.node, sign_ops({dup}, f.principal_key, f.node), "one key accepted for two agents");

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
