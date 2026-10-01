#include <graphene/chain/agent_evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/agent_objects.hpp>
#include <graphene/protocol/agent_operations.hpp>
#include <graphene/protocol/operation_util_impl.hpp>
#include <graphene/protocol/operations.hpp>
#include <graphene/protocol/sign_state.hpp>
#include <graphene/protocol/transaction.hpp>
#include <graphene/protocol/config.hpp>

namespace graphene { namespace chain {

using namespace graphene::protocol;

namespace {

bool usable_agent_row(const database& db, const agent_permission_object& row, const string& wire) {
    if (row.expiration != time_point_sec() && row.expiration <= db.head_block_time()) return false;
    const auto grants = unpack_operation_names(row.operations);
    if (!grants.count(wire)) return false;
    for (const auto& name : grants)
        if (never_delegable_operation_names().count(name)) return false;
    return true;
}

} // anonymous namespace

bool agent_operation_allowed(const database& db, const operation& op,
                             const account_name_type& principal, const public_key_type& key) {
    if (!db.has_hardfork(CHAIN_HARDFORK_15)) return false;
    const string wire = operation_wire_name(op);
    if (never_delegable_operation_names().count(wire)) return false;
    const auto& idx = db.get_index<agent_permission_index>().indices().get<by_permission_account>();
    for (auto it = idx.lower_bound(boost::make_tuple(principal));
         it != idx.end() && it->account == principal; ++it)
        if (it->agent_key == key && usable_agent_row(db, *it, wire)) return true;
    return false;
}

void verify_agent_transaction(const database& db, const signed_transaction& trx,
                              const flat_set<public_key_type>& keys, bool allow_unused,
                              flat_set<public_key_type>* used, agent_proofs* proofs) {
    const auto get_active = [&](const account_name_type& n) {
        return authority(db.get<account_authority_object, by_account>(n).active);
    };
    const auto get_master = [&](const account_name_type& n) {
        return authority(db.get<account_authority_object, by_account>(n).master);
    };
    const auto get_regular = [&](const account_name_type& n) {
        return authority(db.get<account_authority_object, by_account>(n).regular);
    };
    if (proofs) proofs->operations.assign(trx.operations.size(), {});
    const auto direct = [&](const operation& op, const account_name_type& principal,
                            bool /* regular */, sign_state& state) {
        if (!db.has_hardfork(CHAIN_HARDFORK_15)) return false;
        const string wire = operation_wire_name(op);
        if (never_delegable_operation_names().count(wire)) return false;
        const auto& idx = db.get_index<agent_permission_index>().indices().get<by_permission_account>();
        for (auto it = idx.lower_bound(boost::make_tuple(principal));
             it != idx.end() && it->account == principal; ++it) {
            if (!usable_agent_row(db, *it, wire) || !state.signed_by(it->agent_key)) continue;
            if (proofs) proofs->operations[&op - trx.operations.data()][principal] = it->agent_key;
            return true;
        }
        return false;
    };
    protocol::verify_authority(trx.operations, keys, get_active, get_master, get_regular,
        CHAIN_MAX_SIG_CHECK_DEPTH, false, {}, {}, {}, {}, allow_unused, used, direct);
}

// ─── set_agent_permission ────────────────────────────────────────────────────
// Issue, replace or revoke an agent (label + key) of a principal. The op requires the principal's
// active authority; the single hook in database.cpp recognizes agent keys on later transactions.
// Here we only maintain the object and re-check the list.
//
// The list is re-validated here, not only in the protocol's validate(): the hook consults these
// rows on every transaction, so a row that somehow holds a name it must not hold would be a live
// escalation, not a cosmetic problem. Cheap insurance on a consensus path.
void set_agent_permission_evaluator::do_apply(const set_agent_permission_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_15), "Agent access is not enabled yet");

    const auto& aidx = db.get_index<account_index>().indices().get<by_name>();
    FC_ASSERT(aidx.find(o.account) != aidx.end(), "Principal account ${a} does not exist", ("a", o.account));

    auto& pidx = db.get_index<agent_permission_index>().indices().get<by_permission_account>();
    auto existing = pidx.find(boost::make_tuple(o.account, o.agent_name));
    const auto now = db.head_block_time();

    // Both lists empty = revoke. Addons alone make a valid agent (a key for off-chain services only,
    // q1718=A); such a row grants nothing on chain — the hook skips an empty operation list.
    if (o.operations.empty() && o.addons.empty()) {
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

    // Touch-time cleanup: every grant sweeps the principal's expired rows, so a dead row lives at
    // most until the principal's next grant. Bounded by the per-principal cap below.
    uint32_t live_others = 0;
    for (auto it = pidx.lower_bound(boost::make_tuple(o.account)); it != pidx.end() && it->account == o.account;) {
        const auto& row = *it++;   // advance before a possible remove
        if (row.expiration != time_point_sec() && row.expiration <= now)
            db.remove(row);
        else if (row.agent_name != o.agent_name) {
            ++live_others;
            // One key, one agent: otherwise a signature could not be attributed to a single list,
            // and revoking one agent would leave its key alive under another name.
            FC_ASSERT(row.agent_key != o.agent_key,
                      "Key ${k} already belongs to agent ${n} of ${a}",
                      ("k", o.agent_key)("n", row.agent_name)("a", o.account));
        }
    }
    existing = pidx.find(boost::make_tuple(o.account, o.agent_name));   // the sweep may have removed it
    const string packed = join_operation_names(o.operations);
    const string packed_addons = join_operation_names(o.addons);
    if (existing != pidx.end()) {
        db.modify(*existing, [&](agent_permission_object& p) {
            p.agent_key = o.agent_key;
            from_string(p.operations, packed);
            p.expiration = o.expiration;
            from_string(p.addons, packed_addons);
        });
    } else {
        // The hook walks all of a principal's rows for every transaction the principal did not sign
        // itself, and a rejected transaction pays no bandwidth — so the row count must be bounded.
        FC_ASSERT(live_others < CHAIN_AGENT_MAX_PER_ACCOUNT,
                  "Account ${a} already has ${n} agents, the limit is ${m}",
                  ("a", o.account)("n", live_others)("m", CHAIN_AGENT_MAX_PER_ACCOUNT));
        db.create<agent_permission_object>([&](agent_permission_object& p) {
            p.account  = o.account;
            p.agent_name = o.agent_name;
            p.agent_key  = o.agent_key;
            from_string(p.operations, packed);
            p.expiration = o.expiration;
            from_string(p.addons, packed_addons);
        });
    }
}

} } // graphene::chain
