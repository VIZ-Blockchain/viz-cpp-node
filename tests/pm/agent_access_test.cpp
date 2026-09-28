// HF15 agent access — wire contract of set_agent_permission_operation.
//
// Two things are pinned here, and both are cheap to get silently wrong:
//  1. the op is APPENDED to the operation static_variant: its index (the consensus op-id, 105)
//     and the indices of its neighbours must not move, or old transactions re-interpret as new ops;
//  2. validate() is the only gate against a delegation list that looks fine and does nothing
//     (typo / virtual name) or does far more than it says (a proposal wrapper, which carries
//     arbitrary operations whose authorities are collected at execution time).
//
// Not a consensus test: it links the protocol library only, no chain.

#define BOOST_TEST_MODULE pm_agent_access
#include <boost/test/unit_test.hpp>

#include <graphene/protocol/operations.hpp>
#include <graphene/protocol/agent_operations.hpp>

using namespace graphene::protocol;

namespace {

set_agent_permission_operation grant(std::initializer_list<const char*> ops) {
    set_agent_permission_operation g;
    g.account = "alice";
    g.agent = "bob";
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

BOOST_AUTO_TEST_CASE(validate_refuses_escalation_and_wrappers) {
    BOOST_CHECK(!accepts(grant({"set_agent_permission"})));  // would let an agent re-delegate
    BOOST_CHECK(!accepts(grant({"proposal_create"})));       // wraps arbitrary ops: bypasses the list
    BOOST_CHECK(!accepts(grant({"proposal_update"})));
    BOOST_CHECK(!accepts(grant({"proposal_delete"})));
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
    auto self = grant({"transfer"});
    self.agent = "alice";
    BOOST_CHECK(!accepts(self));

    auto bad_principal = grant({"transfer"});
    bad_principal.account = "Alice";
    BOOST_CHECK(!accepts(bad_principal));

    auto bad_agent = grant({"transfer"});
    bad_agent.agent = "bob!";
    BOOST_CHECK(!accepts(bad_agent));
}

BOOST_AUTO_TEST_SUITE_END()
