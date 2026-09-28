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

        /// Grant (or revoke) the right for `agent` to perform the listed operations on behalf of
        /// `account`, signing with the agent's OWN active key.
        ///
        /// Rules enforced here and in the evaluator:
        ///  - the grant itself is signed by the principal's ACTIVE authority (`account`);
        ///  - nothing that requires master authority is reachable through a delegation: the hook
        ///    checks every permitted operation against the principal's required authorities, and
        ///    master is never delegable;
        ///  - an agent cannot mint itself new rights: `set_agent_permission`, the proposal
        ///    wrappers and authority-rotating operations are not delegable names
        ///    (see never_delegable_operation_names());
        ///  - a name that does not exist, or is virtual (hence never broadcast), is refused — a
        ///    typo must not become a silently dead permission;
        ///  - an empty `operations` = revoke: the row is deleted;
        ///  - `expiration` == fc::time_point_sec() (the epoch) = perpetual;
        ///  - changing the principal's master/active authority (account_update / recover_account)
        ///    or selling the account wipes every row of that account.
        ///
        /// Names are the wire names of operations, exactly as they appear as the first element of
        /// a broadcast operation array (`transfer`, `pm_place_bet`, ...): a client can copy them
        /// verbatim from a transaction it already builds. Stored normalized through
        /// fc::resolve_operation_name, so a legacy alias cannot produce a second row for the same
        /// operation.
        struct set_agent_permission_operation : public base_operation {
            account_name_type account;     ///< principal granting the access
            account_name_type agent;       ///< account receiving it
            flat_set<string>  operations;  ///< wire names; empty = revoke
            time_point_sec    expiration;  ///< epoch (default) = perpetual

            extensions_type extensions;

            void validate() const;
            void get_required_active_authorities(flat_set<account_name_type>& a) const { a.insert(account); }
        };

} } // graphene::protocol

FC_REFLECT((graphene::protocol::set_agent_permission_operation),
    (account)(agent)(operations)(expiration)(extensions))
