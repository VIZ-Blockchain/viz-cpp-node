#include <graphene/protocol/agent_operations.hpp>
#include <graphene/protocol/operations.hpp>
#include <graphene/protocol/operation_util_impl.hpp>

namespace graphene { namespace protocol {

        /// Longest wire name of an existing operation is well below this; the cap is anti-spam, not
        /// a semantic limit (the list itself is what a delegation is).
        static const size_t AGENT_MAX_OPERATION_NAME_LEN = 64;

        /// Names that must never appear in a delegation list.
        ///  - `set_agent_permission`: an agent must not be able to mint itself further rights — the
        ///    grant is signed with the principal's active authority, and an agent holding active
        ///    could otherwise re-delegate. Escalation chains are refused structurally.
        ///  - the proposal wrappers: a proposal carries arbitrary operations whose authorities are
        ///    collected at EXECUTION time from the wrapped ops, so delegating `proposal_create`
        ///    would not mean "may create a proposal" but "may execute anything the principal can".
        ///    That silently defeats the explicit list, so the wrappers are refused outright.
        static const flat_set<string>& never_delegable_operation_names() {
            static const flat_set<string> names = []() {
                flat_set<string> s;
                s.insert("set_agent_permission");
                s.insert("proposal_create");
                s.insert("proposal_update");
                s.insert("proposal_delete");
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
