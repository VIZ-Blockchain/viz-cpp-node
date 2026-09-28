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

/// Wire names of the operations of `trx` that require SOME authority. An operation that requires
/// nothing grants nothing, so it puts no coverage demand on a delegation.
flat_set<string> authority_requiring_operation_names(const signed_transaction& trx) {
    flat_set<string> names;
    for (const auto& op : trx.operations) {
        flat_set<account_name_type> active, master, regular;
        std::vector<authority> other;
        operation_get_required_authorities(op, active, master, regular, other);
        if (active.empty() && master.empty() && regular.empty() && other.empty()) continue;
        names.insert(fc::resolve_operation_name(operation_wire_name(op)));
    }
    return names;
}

/// The plain ACTIVE getter: what the chain has always used. Delegation is layered on top of it.
authority_getter plain_active_authority_getter(const database& db) {
    return [&db](const account_name_type& name) {
        return authority(db.get<account_authority_object, by_account>(name).active);
    };
}

} // anonymous namespace

fc::flat_map<account_name_type, account_name_type>
delegated_active_authorities(const database& db, const signed_transaction& trx,
                             const chain_id_type& chain_id) {
    fc::flat_map<account_name_type, account_name_type> delegated;

    if (!db.has_hardfork(CHAIN_HARDFORK_15))
        return delegated;

    flat_set<account_name_type> required_active, required_master, required_regular;
    std::vector<authority> other;
    trx.get_required_authorities(required_active, required_master, required_regular, other);

    if (required_active.empty())
        return delegated;

    // Master and regular are out of scope for a delegation, and rather than argue about the nested
    // paths that reach them (sign_state resolves nested account authorities through ACTIVE), we
    // simply do not delegate in such a transaction.
    if (!required_master.empty() || !required_regular.empty())
        return delegated;

    flat_set<public_key_type> sigs;
    bool sigs_ready = false;
    // Signature recovery is the expensive part of validation, and verify_authority performs it
    // again right after us. So it is deferred until a delegation row actually exists for some
    // principal: with no rows the hook must add no work at all to the ordinary path.
    auto ensure_signatures = [&]() -> bool {
        if (!sigs_ready) {
            sigs_ready = true;
            try {
                sigs = trx.get_signature_keys(chain_id);
            } catch (...) {
                // Unsigned or malformed: leave the verdict to verify_authority, which reports it.
                sigs.clear();
                return false;
            }
        }
        return true;
    };

    const flat_set<string> tx_ops = authority_requiring_operation_names(trx);
    const authority_getter get_active = plain_active_authority_getter(db);
    const flat_set<public_key_type> no_extra_keys;   // a validating node can produce no extra keys

    const auto& pidx = db.get_index<agent_permission_index>().indices().get<by_permission_account>();
    const time_point_sec now = db.head_block_time();
    const flat_set<string>& denied = never_delegable_operation_names();

    for (const account_name_type& principal : required_active) {
        // Rows of this principal only — prefix scan of the (principal, agent) index, agent-ordered,
        // so the outcome does not depend on the order grants were made. No row, no work.
        auto it = pidx.lower_bound(boost::make_tuple(principal));
        if (it == pidx.end() || it->account != principal)
            continue;

        if (!ensure_signatures())
            return delegated;

        // The principal's own authority always wins: substituting unconditionally would break valid
        // transactions the moment a delegation row exists for the account.
        {
            sign_state principal_signs(sigs, get_active, no_extra_keys);
            if (principal_signs.check_authority(principal))
                continue;
        }

        for (; it != pidx.end() && it->account == principal; ++it) {
            const agent_permission_object& row = *it;

            // Expiration: epoch means perpetual; a past date means the row is already dead. An
            // expired row is an ordinary refusal, not an error.
            if (row.expiration != time_point_sec() && row.expiration <= now)
                continue;

            const flat_set<string> granted = unpack_operation_names(row.operations);
            if (granted.empty())
                continue;

            // A row holding a non-delegable name is an invariant breach (the evaluator refuses
            // those), and the hook trusts these rows — so fail closed instead of trusting it.
            bool usable = true;
            for (const string& name : granted) {
                if (denied.count(name)) { usable = false; break; }
            }
            if (!usable)
                continue;

            // Full coverage of the transaction, per the header's contract.
            for (const string& name : tx_ops) {
                if (!granted.count(name)) { usable = false; break; }
            }
            if (!usable)
                continue;

            // The agent signs with its OWN active key. The check consumes that signature in the real
            // sign_state below, so it does not show up as an unused signature.
            sign_state agent_signs(sigs, get_active, no_extra_keys);
            if (!agent_signs.check_authority(row.agent))
                continue;

            delegated[principal] = row.agent;
            break;
        }
    }

    return delegated;
}

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

    // Touch-time cleanup: every grant sweeps the principal's expired rows, so a dead row lives at
    // most until the principal's next grant. Bounded by the per-principal cap below.
    uint32_t live_others = 0;
    for (auto it = pidx.lower_bound(boost::make_tuple(o.account)); it != pidx.end() && it->account == o.account;) {
        const auto& row = *it++;   // advance before a possible remove
        if (row.expiration != time_point_sec() && row.expiration <= now)
            db.remove(row);
        else if (row.agent != o.agent)
            ++live_others;
    }
    existing = pidx.find(boost::make_tuple(o.account, o.agent));   // the sweep may have removed it
    const string packed = join_operation_names(o.operations);
    if (existing != pidx.end()) {
        db.modify(*existing, [&](agent_permission_object& p) {
            from_string(p.operations, packed);
            p.expiration = o.expiration;
        });
    } else {
        // The hook walks all of a principal's rows for every transaction the principal did not sign
        // itself, and a rejected transaction pays no bandwidth — so the row count must be bounded.
        FC_ASSERT(live_others < CHAIN_AGENT_MAX_PER_ACCOUNT,
                  "Account ${a} already has ${n} agents, the limit is ${m}",
                  ("a", o.account)("n", live_others)("m", CHAIN_AGENT_MAX_PER_ACCOUNT));
        db.create<agent_permission_object>([&](agent_permission_object& p) {
            p.account  = o.account;
            p.agent    = o.agent;
            from_string(p.operations, packed);
            p.expiration = o.expiration;
        });
    }
}

} } // graphene::chain
