#include <graphene/protocol/agent_operations.hpp>
#include <graphene/protocol/operations.hpp>
#include <graphene/protocol/operation_util_impl.hpp>

namespace graphene { namespace protocol {

        /// Longest wire name of an existing operation is well below this; the cap is anti-spam, not
        /// a semantic limit (the list itself is what a delegation is).
        static const size_t AGENT_MAX_OPERATION_NAME_LEN = 64;
        static const size_t AGENT_MAX_ADDONS = 10;          // owner decision 2026-09-28
        static const size_t AGENT_MAX_ADDON_LEN = 63;       // "shorter than 64"

        const flat_set<string>& never_delegable_operation_names() {
            static const flat_set<string> names = []() {
                flat_set<string> s;
                // Proposal approvals can execute arbitrary proposed operations later, bypassing
                // the agent's scope. Account updates can rotate the active authority outright.
                s.insert("proposal_create");
                s.insert("proposal_update");
                s.insert("proposal_delete");
                s.insert("account_update");
                s.insert("recover_account");
                s.insert("change_recovery_account");
                s.insert("set_account_price");
                s.insert("set_subaccount_price");
                s.insert("target_account_sale");
                // HF4 retired these broadcastable wire operations; a grant could never use them.
                s.insert("vote");
                s.insert("content");
                s.insert("delete_content");
                return s;
            }();
            return names;
        }

        void set_agent_permission_operation::validate() const {
            FC_ASSERT(is_valid_account_name(account), "Account name ${n} is invalid", ("n", account));
            const string label = agent_name;
            FC_ASSERT(!label.empty(), "agent_name is empty");
            FC_ASSERT(label.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") == string::npos,
                      "agent_name ${n} must be lower-case ascii, digits, '_' or '-'", ("n", label));
            if (!operations.empty() || !addons.empty())
                FC_ASSERT(agent_key != public_key_type(), "agent_key is required when granting");

            // Addons are opaque to the node; only bounded. ',' is the storage separator.
            FC_ASSERT(addons.size() <= AGENT_MAX_ADDONS, "at most ${c} addons", ("c", AGENT_MAX_ADDONS));
            for (const string& a : addons) {
                FC_ASSERT(!a.empty(), "empty addon");
                FC_ASSERT(a.size() <= AGENT_MAX_ADDON_LEN, "addon ${a} is longer than ${c} bytes",
                          ("a", a)("c", AGENT_MAX_ADDON_LEN));
                FC_ASSERT(a.find(',') == string::npos, "addon ${a} must not contain ','", ("a", a));
            }

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
