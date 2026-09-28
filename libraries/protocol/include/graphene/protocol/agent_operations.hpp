#pragma once

#include <graphene/protocol/base.hpp>
#include <graphene/protocol/block_header.hpp>

#include <fc/utf8.hpp>

namespace graphene { namespace protocol {

        // HF15 agent access (Onix). Account-level delegation with an explicit, frozen list of
        // operations — no roles, no levels, no wildcard masks. A mask (`pm_*`) would silently
        // widen the grant the day a new operation is appended to the chain, which is exactly the
        // failure mode this design exists to avoid.
        //
        // Op-id NOTE: appended to the single `operation` static_variant (see operations.hpp); the
        // variant index IS the consensus op-id.

        /// Names that must never appear in a delegation list. Single source of truth: the grant
        /// operation refuses them in validate(), and the chain-side authority hook refuses them
        /// again at execution time — the rule must not live in one place only.
        const flat_set<string>& never_delegable_operation_names();

        /// Issue, replace or revoke an agent of `account`. An agent is not an account: it is a
        /// label (`agent_name`, unique per principal) and a public key (`agent_key`) that may sign the
        /// listed operations on the principal's behalf. The principal may hold up to
        /// CHAIN_AGENT_MAX_PER_ACCOUNT live agents.
        ///
        /// Rules enforced here and in the evaluator:
        ///  - signed by the principal's ACTIVE authority;
        ///  - a transaction signed by an agent key passes only if every authority-requiring operation
        ///    in it is on that agent's list and nothing in it needs master or regular authority;
        ///  - never-delegable names are refused (see never_delegable_operation_names()); unknown
        ///    or virtual names are refused too — a typo must not become a dead permission;
        ///  - one key per agent, and a key may belong to one agent of the principal only;
        ///  - empty `operations` = revoke the named agent; `expiration` in the past = revoke;
        ///    epoch (default) = perpetual;
        ///  - any change of the principal's master or active authority, recovery or sale wipes all
        ///    of the principal's agents.
        ///
        /// Operation names are the wire names (`transfer`, `pm_place_bet`, ...), stored normalized
        /// through fc::resolve_operation_name.
        struct set_agent_permission_operation : public base_operation {
            account_name_type account;     ///< principal granting the access
            account_name_type agent_name;  ///< label, unique per principal; [a-z0-9_-], 1..32
            public_key_type   agent_key;   ///< key the agent signs with; ignored on revoke
            flat_set<string>  operations;  ///< wire names; empty = revoke
            time_point_sec    expiration;  ///< epoch (default) = perpetual

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(account); }
        };

} } // graphene::protocol

FC_REFLECT((graphene::protocol::set_agent_permission_operation),
    (account)(agent_name)(agent_key)(operations)(expiration)(extensions))
