#include <graphene/chain/agent_evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/agent_objects.hpp>
#include <graphene/protocol/agent_operations.hpp>
#include <graphene/protocol/operation_util_impl.hpp>
#include <graphene/protocol/config.hpp>

namespace graphene { namespace chain {

using namespace graphene::protocol;

// ─── set_agent_permission ────────────────────────────────────────────────────
// Grant, re-grant or revoke an agent's right to broadcast listed operations for a principal.
// The authority itself is checked by the single hook in database.cpp (the op requires the
// PRINCIPAL's active authority, and an agent acting under a delegation signs with its own active
// key, which the hook recognizes). Here we only maintain the object and re-check the list.
//
// The list is re-validated here, not only in the protocol's validate(): the hook consults these
// rows on every transaction, so a row that somehow holds a name it must not hold would be a live
// escalation, not a cosmetic problem. Cheap insurance on a consensus path.
void set_agent_permission_evaluator::do_apply(const set_agent_permission_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_15), "Agent access is not enabled yet");

    const auto& aidx = db.get_index<account_index>().indices().get<by_name>();
    FC_ASSERT(aidx.find(o.account) != aidx.end(), "Principal account ${a} does not exist", ("a", o.account));
    FC_ASSERT(aidx.find(o.agent) != aidx.end(), "Agent account ${a} does not exist", ("a", o.agent));

    auto& pidx = db.get_index<agent_permission_index>().indices().get<by_permission_account>();
    auto existing = pidx.find(boost::make_tuple(o.account, o.agent));
    const auto now = db.head_block_time();

    // Empty list = revoke.
    if (o.operations.empty()) {
        if (existing != pidx.end())
            db.remove(*existing);
        return;
    }

    // Perpetual = epoch; `time_point_sec(0)`. An expiration in the past revokes the row: that is a
    // legitimate way to say "now, and not longer", not an error.
    const bool dead_on_arrival = o.expiration != time_point_sec() && o.expiration <= now;

    const flat_set<string>& denied = never_delegable_operation_names();
    for (const string& raw : o.operations) {
        const string name = fc::resolve_operation_name(raw);
        FC_ASSERT(!denied.count(name), "Operation ${n} is not delegable", ("n", name));
        FC_ASSERT(is_broadcastable_operation_wire_name(name),
                  "Unknown or non-broadcastable operation ${n}", ("n", name));
    }

    if (dead_on_arrival) {
        if (existing != pidx.end())
            db.remove(*existing);
        return;
    }

    const string packed = join_operation_names(o.operations);
    if (existing != pidx.end()) {
        db.modify(*existing, [&](agent_permission_object& p) {
            from_string(p.operations, packed);
            p.expiration = o.expiration;
        });
    } else {
        db.create<agent_permission_object>([&](agent_permission_object& p) {
            p.account  = o.account;
            p.agent    = o.agent;
            from_string(p.operations, packed);
            p.expiration = o.expiration;
        });
    }
}

} } // graphene::chain
