#include <graphene/protocol/agent_operations.hpp>
#include <graphene/protocol/operations.hpp>
#include <graphene/protocol/operation_util_impl.hpp>

namespace graphene { namespace protocol {

        /// Longest wire name of an existing operation is well below this; the cap is anti-spam, not
        /// a semantic limit (the list itself is what a delegation is).
        static const size_t AGENT_MAX_OPERATION_NAME_LEN = 64;

        const flat_set<string>& never_delegable_operation_names() {
            static const flat_set<string> names = []() {
                flat_set<string> s;
                // An agent must not mint itself further rights: the grant is signed with the
                // principal's active authority, so an agent holding active could otherwise
                // re-delegate. Escalation chains are refused structurally.
                s.insert("set_agent_permission");
                // Proposal wrappers carry arbitrary operations whose authorities are collected at
                // EXECUTION time from the wrapped ops. Delegating `proposal_create` would therefore
                // not mean "may create a proposal" but "may execute anything the principal can" —
                // the explicit list would be bypassed while looking narrow.
                s.insert("proposal_create");
                s.insert("proposal_update");
                s.insert("proposal_delete");
                // Authority rotation. `account_update` is the sharp one: an op without the `master`
                // field is satisfied by the ACTIVE authority (see account_update_operation::
                // get_required_active_authorities) and may carry a new `active` authority — so an
                // agent granted account_update for "metadata edits" could simply rotate the
                // principal's active key to one it controls and own the account outright.
                s.insert("account_update");
                // Operations that always demand the principal's MASTER authority are structurally
                // out of reach for an agent (the hook never substitutes master). Granting one would
                // create a permission that can never succeed: a silent no-op, refused on principle.
                s.insert("recover_account");
                s.insert("change_recovery_account");
                s.insert("set_account_price");
                s.insert("set_subaccount_price");
                s.insert("target_account_sale");
                return s;
            }();
            return names;
        }

        void set_agent_permission_operation::validate() const {
            FC_ASSERT(is_valid_account_name(account), "Account name ${n} is invalid", ("n", account));
            FC_ASSERT(is_valid_account_name(agent), "Account name ${n} is invalid", ("n", agent));
            FC_ASSERT(account != agent, "account ${n} cannot be its own agent", ("n", account));

            for (const string& raw : operations) {
                FC_ASSERT(!raw.empty(), "empty operation name in the permission list");
                FC_ASSERT(raw.size() <= AGENT_MAX_OPERATION_NAME_LEN,
                          "operation name ${n} is longer than ${c} bytes",
                          ("n", raw)("c", AGENT_MAX_OPERATION_NAME_LEN));
                FC_ASSERT(raw.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") == string::npos,
                          "operation name ${n} must be lower-case ascii, digits or underscore", ("n", raw));
                const string name = fc::resolve_operation_name(raw);
                FC_ASSERT(name == raw, "operation name ${n} is a legacy alias, use ${c}", ("n", raw)("c", name));
                FC_ASSERT(!never_delegable_operation_names().count(name),
                          "operation ${n} is not delegable", ("n", name));
                // A name that no operation answers to (or a virtual one, which is never broadcast)
                // would be a silently dead permission: refuse it at grant time.
                FC_ASSERT(is_broadcastable_operation_wire_name(name),
                          "unknown or non-broadcastable operation ${n}", ("n", name));
            }
        }

} } // graphene::protocol
