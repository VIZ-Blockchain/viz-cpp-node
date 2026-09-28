#include <graphene/chain/agent_evaluator.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/agent_objects.hpp>
#include <graphene/protocol/agent_operations.hpp>
#include <graphene/protocol/operation_util_impl.hpp>
#include <graphene/protocol/operations.hpp>
#include <graphene/protocol/sign_state.hpp>
#include <graphene/protocol/transaction.hpp>

namespace graphene { namespace chain {
using namespace graphene::protocol;

bool agent_requirement_allowed(const database& db, const operation& op,
                               const account_name_type& name, const flat_set<public_key_type>& keys) {
    if (!db.has_hardfork(CHAIN_HARDFORK_15)) return false;
    const string wire = fc::resolve_operation_name(operation_wire_name(op));
    if (never_delegable_operation_names().count(wire)) return false;
    const auto& idx = db.get_index<agent_permission_index>().indices().get<by_permission_account>();
    const auto now = db.head_block_time();
    for (auto it = idx.lower_bound(boost::make_tuple(name));
         it != idx.end() && it->account == name; ++it) {
        if (it->expiration != time_point_sec() && it->expiration <= now) continue;
        if (unpack_operation_names(it->operations).count(wire) && keys.count(it->agent_key)) return true;
    }
    return false;
}

void verify_agent_transaction(const database& db, const signed_transaction& trx,
                              const flat_set<public_key_type>& keys, bool allow_unused,
                              flat_set<public_key_type>* used) {
    const auto get_active = [&db](const account_name_type& name) {
        return authority(db.get<account_authority_object, by_account>(name).active);
    };
    const auto get_master = [&db](const account_name_type& name) {
        return authority(db.get<account_authority_object, by_account>(name).master);
    };
    const auto get_regular = [&db](const account_name_type& name) {
        return authority(db.get<account_authority_object, by_account>(name).regular);
    };
    if (!db.has_hardfork(CHAIN_HARDFORK_15)) {
        graphene::protocol::verify_authority(trx.operations, keys, get_active, get_master,
                                             get_regular, CHAIN_MAX_SIG_CHECK_DEPTH);
        return;
    }
    const auto& idx = db.get_index<agent_permission_index>().indices().get<by_permission_account>();
    const auto now = db.head_block_time();
    const agent_authority_checker agent = [&](const operation& op, const account_name_type& name,
                                              bool /* regular */, sign_state& s) {
        const string wire = fc::resolve_operation_name(operation_wire_name(op));
        if (never_delegable_operation_names().count(wire)) return false;
        for (auto it = idx.lower_bound(boost::make_tuple(name));
             it != idx.end() && it->account == name; ++it) {
            if (it->expiration != time_point_sec() && it->expiration <= now) continue;
            if (!unpack_operation_names(it->operations).count(wire)) continue;
            if (s.signed_by(it->agent_key)) return true;
        }
        return false;
    };
    graphene::protocol::verify_authority_with_agents(trx.operations, keys, get_active, get_master,
                                                      get_regular, agent, CHAIN_MAX_SIG_CHECK_DEPTH,
                                                      allow_unused, used);
}

void set_agent_permission_evaluator::do_apply(const set_agent_permission_operation& o) {
    auto& db = _db;
    FC_ASSERT(db.has_hardfork(CHAIN_HARDFORK_15), "Agent access is not enabled yet");
    const auto& aidx = db.get_index<account_index>().indices().get<by_name>();
    FC_ASSERT(aidx.find(o.account) != aidx.end(), "Principal account ${a} does not exist", ("a", o.account));
    auto& pidx = db.get_index<agent_permission_index>().indices().get<by_permission_account>();
    auto existing = pidx.find(boost::make_tuple(o.account, o.agent_name));
    const auto now = db.head_block_time();
    if (o.operations.empty() && o.addons.empty()) {
        if (existing != pidx.end()) db.remove(*existing);
        return;
    }
    for (const string& raw : o.operations) {
        const string name = fc::resolve_operation_name(raw);
        FC_ASSERT(!never_delegable_operation_names().count(name),
                  "Operation ${n} is not delegable", ("n", name));
        FC_ASSERT(is_broadcastable_operation_wire_name(name),
                  "Unknown or non-broadcastable operation ${n}", ("n", name));
    }
    if (o.expiration != time_point_sec() && o.expiration <= now) {
        if (existing != pidx.end()) db.remove(*existing);
        return;
    }
    uint32_t others = 0;
    for (auto it = pidx.lower_bound(boost::make_tuple(o.account));
         it != pidx.end() && it->account == o.account;) {
        const auto& row = *it++;
        if (row.expiration != time_point_sec() && row.expiration <= now) db.remove(row);
        else if (row.agent_name != o.agent_name) ++others;
    }
    existing = pidx.find(boost::make_tuple(o.account, o.agent_name));
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
        FC_ASSERT(others < CHAIN_AGENT_MAX_PER_ACCOUNT,
                  "Account ${a} already has ${n} agents, the limit is ${m}",
                  ("a", o.account)("n", others)("m", CHAIN_AGENT_MAX_PER_ACCOUNT));
        db.create<agent_permission_object>([&](agent_permission_object& p) {
            p.account = o.account;
            p.agent_name = o.agent_name;
            p.agent_key = o.agent_key;
            from_string(p.operations, packed);
            p.expiration = o.expiration;
            from_string(p.addons, packed_addons);
        });
    }
}
} } // graphene::chain
