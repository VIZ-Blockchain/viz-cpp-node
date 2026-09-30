// HF15 agent access — wire contract of set_agent_permission_operation.
//
// Two things are pinned here, and both are cheap to get silently wrong:
//  1. the op is APPENDED to the operation static_variant: its index (the consensus op-id, 105)
//     and the indices of its neighbours must not move, or old transactions re-interpret as new ops;
//  2. validate() is the only gate against a delegation list that looks fine and does nothing
//     (typo / virtual name) or grants account takeover/persistent proposal approvals.
//     Proposal creation/deletion alone does not grant approval of the inner operations.
//
// Not a consensus test: it links the protocol library only, no chain.

#define BOOST_TEST_MODULE pm_agent_access
#include <boost/test/unit_test.hpp>

#include <graphene/protocol/operations.hpp>
#include <graphene/protocol/agent_operations.hpp>
#include <graphene/protocol/transaction.hpp>
#include <graphene/protocol/exceptions.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/crypto/elliptic.hpp>

using namespace graphene::protocol;

namespace {

set_agent_permission_operation grant(std::initializer_list<const char*> ops) {
    set_agent_permission_operation g;
    g.account = "alice";
    g.agent_name = "trading-bot";
    g.agent_key = fc::ecc::private_key::regenerate(fc::sha256::hash(std::string("agent"))).get_public_key();
    for (const char* o : ops) g.operations.insert(o);
    g.expiration = fc::time_point_sec();   // epoch = perpetual
    return g;
}

bool accepts(const set_agent_permission_operation& op) {
    try { op.validate(); return true; } catch (const fc::exception&) { return false; }
}

} // namespace

BOOST_AUTO_TEST_SUITE(agent_access)

BOOST_AUTO_TEST_CASE(op_id_is_appended_never_renumbered) {
    BOOST_CHECK_EQUAL(operation::count(), 106);

    // Anchors on both sides of the append: if either index moves, the wire format broke.
    operation probe;
    probe.set_which(104);
    BOOST_CHECK_EQUAL(operation_wire_name(probe), "pm_lp_payout");
    probe.set_which(105);
    BOOST_CHECK_EQUAL(operation_wire_name(probe), "set_agent_permission");

    // The wire name clients write as the first element of the operation array.
    BOOST_CHECK_EQUAL(operation_wire_name(operation(set_agent_permission_operation())),
                      "set_agent_permission");
}

BOOST_AUTO_TEST_CASE(wire_names_exclude_virtual_operations) {
    // A delegation is meaningless for an operation the chain generates itself.
    BOOST_CHECK(is_broadcastable_operation_wire_name("transfer"));
    BOOST_CHECK(is_broadcastable_operation_wire_name("pm_place_bet"));
    BOOST_CHECK(is_broadcastable_operation_wire_name("set_agent_permission"));
    BOOST_CHECK(!is_broadcastable_operation_wire_name("pm_lp_payout"));
    BOOST_CHECK(!is_broadcastable_operation_wire_name("pm_ban_expired"));
    BOOST_CHECK(!is_broadcastable_operation_wire_name("nonsense_op"));
    BOOST_CHECK(!is_broadcastable_operation_wire_name("transfer_operation"));
}

BOOST_AUTO_TEST_CASE(validate_accepts_plain_grants) {
    BOOST_CHECK(accepts(grant({"transfer"})));
    BOOST_CHECK(accepts(grant({"transfer", "pm_place_bet", "pm_resolve_market"})));
    BOOST_CHECK(accepts(grant({})));                     // empty list = revoke
    BOOST_CHECK(accepts(grant({"transfer_to_vesting"}))); // money-moving, but explicit and active-only
}

BOOST_AUTO_TEST_CASE(validate_bounds_addons_only) {
    auto g = grant({});
    g.addons = {"vizhub"};
    BOOST_CHECK(accepts(g));                               // addon-only agent (q1718=A)
    g.addons = {"Any Text/with:chars", std::string(63, 'x')};
    BOOST_CHECK(accepts(g));                               // opaque to the node
    g.addons = {std::string(64, 'x')};
    BOOST_CHECK(!accepts(g));                              // must be shorter than 64
    g.addons = {""};
    BOOST_CHECK(!accepts(g));
    g.addons = {"a,b"};
    BOOST_CHECK(!accepts(g));                              // ',' is the storage separator
    g.addons.clear();
    for (int i = 0; i < 10; ++i) g.addons.insert("s" + std::to_string(i));
    BOOST_CHECK(accepts(g));
    g.addons.insert("s10");
    BOOST_CHECK(!accepts(g));                              // at most 10
    g.addons = {"vizhub"};
    g.agent_key = public_key_type();
    BOOST_CHECK(!accepts(g));                              // addon-only still needs a key
}
BOOST_AUTO_TEST_CASE(validate_refuses_escalation_but_accepts_safe_proposals) {
    BOOST_CHECK(!accepts(grant({"set_agent_permission"})));  // would let an agent re-delegate
    BOOST_CHECK(accepts(grant({"proposal_create"}))); // creates no approval
    BOOST_CHECK(!accepts(grant({"proposal_update"}))); // persistent approval is not delegated
    BOOST_CHECK(accepts(grant({"proposal_delete"}))); // ordinary requester checks still apply
    // An active-signed account_update without the master field may rewrite the ACTIVE authority,
    // i.e. rotate it to a key the agent controls: one granted op would be ownership itself.
    BOOST_CHECK(!accepts(grant({"account_update"})));
}

BOOST_AUTO_TEST_CASE(validate_refuses_master_only_operations) {
    // Not reachable through a delegation (the hook never substitutes master), so granting one
    // would be a permission that can never succeed.
    BOOST_CHECK(!accepts(grant({"recover_account"})));
    BOOST_CHECK(!accepts(grant({"change_recovery_account"})));
    BOOST_CHECK(!accepts(grant({"set_account_price"})));
    BOOST_CHECK(!accepts(grant({"set_subaccount_price"})));
    BOOST_CHECK(!accepts(grant({"target_account_sale"})));
}

BOOST_AUTO_TEST_CASE(validate_refuses_dead_permissions) {
    BOOST_CHECK(!accepts(grant({"transfer_typo"})));         // no silent no-op
    BOOST_CHECK(!accepts(grant({"pm_lp_payout"})));          // virtual: never broadcast
    BOOST_CHECK(!accepts(grant({"witness_update"})));        // legacy alias of validator_update
    BOOST_CHECK(!accepts(grant({"Transfer"})));
    BOOST_CHECK(!accepts(grant({""})));
}

BOOST_AUTO_TEST_CASE(validate_refuses_malformed_participants) {
    auto bad_principal = grant({"transfer"});
    bad_principal.account = "Alice";
    BOOST_CHECK(!accepts(bad_principal));

    auto bad_name = grant({"transfer"});
    bad_name.agent_name = "Bot!";
    BOOST_CHECK(!accepts(bad_name));

    auto empty_name = grant({"transfer"});
    empty_name.agent_name = "";
    BOOST_CHECK(!accepts(empty_name));

    // A grant without a key could never sign anything.
    auto no_key = grant({"transfer"});
    no_key.agent_key = public_key_type();
    BOOST_CHECK(!accepts(no_key));

    // ...but a revoke needs only the name.
    auto revoke = grant({});
    revoke.agent_key = public_key_type();
    BOOST_CHECK(accepts(revoke));
}

BOOST_AUTO_TEST_CASE(ordinary_regular_discovery_reports_used_keys) {
    const public_key_type regular = fc::ecc::private_key::regenerate(
        fc::sha256::hash(std::string("regular-discovery"))).get_public_key();
    const public_key_type extra = fc::ecc::private_key::regenerate(
        fc::sha256::hash(std::string("extra-discovery"))).get_public_key();
    authority auth;
    auth.weight_threshold = 1;
    auth.key_auths[regular] = 1;
    const auto getter = [&](const account_name_type&) { return auth; };
    custom_operation op;
    op.required_regular_auths.insert("alice");
    op.id = "regular-discovery";
    op.json = "{}";
    fc::flat_set<public_key_type> used;
    verify_authority({operation(op)}, {regular}, getter, getter, getter,
        CHAIN_MAX_SIG_CHECK_DEPTH, false, {}, {}, {}, {}, true, &used);
    BOOST_CHECK_EQUAL(used.size(), 1u);
    BOOST_CHECK(used.count(regular));
    BOOST_CHECK_NO_THROW(verify_authority({operation(op)}, {regular, extra},
        getter, getter, getter, CHAIN_MAX_SIG_CHECK_DEPTH,
        false, {}, {}, {}, {}, true, &used));
    BOOST_CHECK_EQUAL(used.size(), 1u);
    BOOST_CHECK(!used.count(extra));
    // Broadcast still rejects unrelated co-signatures; only discovery permits
    // unused candidate keys.
    BOOST_CHECK_THROW(verify_authority({operation(op)}, {regular, extra},
        getter, getter, getter), tx_irrelevant_sig);
}

BOOST_AUTO_TEST_CASE(ordinary_partial_active_then_master_keeps_legacy_acceptance) {
    const auto key = [](const char* seed) {
        return public_key_type(fc::ecc::private_key::regenerate(
            fc::sha256::hash(std::string(seed))).get_public_key());
    };
    const public_key_type partial = key("legacy-partial-active");
    const public_key_type unavailable = key("legacy-missing-active");
    const public_key_type master_key = key("legacy-master");
    authority active;
    active.weight_threshold = 2;
    active.key_auths[partial] = 1;
    active.key_auths[unavailable] = 1;
    authority master;
    master.weight_threshold = 1;
    master.key_auths[master_key] = 1;
    const auto get_active = [&](const account_name_type&) { return active; };
    const auto get_master = [&](const account_name_type&) { return master; };
    transfer_operation op;
    op.from = "alice";
    op.to = "bob";
    op.amount = asset(1, TOKEN_SYMBOL);
    // Existing consensus marks the partial active signature used before the
    // successful master fallback. Agent isolation must not tighten that path.
    BOOST_CHECK_NO_THROW(verify_authority({operation(op)}, {partial, master_key},
        get_active, get_master, get_master));
    BOOST_CHECK_NO_THROW(verify_authority({operation(op)}, {partial, master_key},
        get_active, get_master, get_master, CHAIN_MAX_SIG_CHECK_DEPTH,
        false, {}, {}, {}, [](const account_name_type&, sign_state&) { return false; }));
    const public_key_type agent = key("direct-agent-extra-partial");
    BOOST_CHECK_THROW(verify_authority({operation(op)}, {partial, agent},
        get_active, get_master, get_master, CHAIN_MAX_SIG_CHECK_DEPTH,
        false, {}, {}, {}, [&](const account_name_type&, sign_state& state) {
            return state.signed_by(agent);
        }), tx_irrelevant_sig);
}

BOOST_AUTO_TEST_CASE(direct_regular_discards_failed_ordinary_keys) {
    const auto key = [](const char* seed) {
        return public_key_type(fc::ecc::private_key::regenerate(fc::sha256::hash(std::string(seed))).get_public_key());
    };
    const auto partial = key("regular-partial");
    const auto absent = key("regular-absent");
    const auto agent = key("regular-agent");
    authority regular;
    regular.weight_threshold = 2;
    regular.key_auths[partial] = 1;
    regular.key_auths[absent] = 1;
    authority ordinary;
    ordinary.weight_threshold = 1;
    ordinary.key_auths[absent] = 1;
    const auto get_regular = [&](const account_name_type&) { return regular; };
    const auto get_ordinary = [&](const account_name_type&) { return ordinary; };
    custom_operation op;
    op.required_regular_auths.insert("alice");
    op.id = "regular-isolation";
    op.json = "{}";
    const auto direct = [&](const operation& operation, const account_name_type& principal, bool is_regular, sign_state& state) {
        return operation_wire_name(operation) == "custom" && principal == "alice" && is_regular && state.signed_by(agent);
    };
    BOOST_CHECK_NO_THROW(verify_authority({operation(op)}, {agent}, get_ordinary, get_ordinary,
        get_regular, CHAIN_MAX_SIG_CHECK_DEPTH, false, {}, {}, {}, {}, false, nullptr, direct));
    BOOST_CHECK_THROW(verify_authority({operation(op)}, {partial, agent}, get_ordinary, get_ordinary,
        get_regular, CHAIN_MAX_SIG_CHECK_DEPTH, false, {}, {}, {}, {}, false, nullptr, direct), tx_irrelevant_sig);
}

BOOST_AUTO_TEST_SUITE_END()
