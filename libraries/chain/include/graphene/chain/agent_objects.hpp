#pragma once

#include <graphene/chain/chain_object_types.hpp>

#include <boost/multi_index/composite_key.hpp>

// HF15 agent access (Onix) — consensus object behind set_agent_permission_operation.
//
// One row per (principal, agent name): a key the principal issued may sign the listed operations on
// the principal's behalf until `expiration` (epoch = perpetual). The agent is NOT an account — it is a
// label plus a public key stored on the principal's side. The list is what a delegation IS, so it is
// stored explicitly — never as a role, a level or a prefix mask: a mask would silently widen the
// grant the day a new operation is appended to the chain.

namespace graphene { namespace chain {

        using protocol::string_less;

        /// Operation names are `[a-z0-9_]` only (enforced by the op's validate()), so a `,`-joined
        /// string is unambiguous. Storing the list as one allocator-aware string keeps the object a
        /// flat POD: no container allocator plumbing, no layout surprises on snapshot import, and a
        /// client reading the object sees the names directly. Canonical form: sorted, unique.
        /// `shared_string` is allocator-aware and cannot be default-constructed off the heap, so the
        /// packers work on plain strings and the caller (which owns a live object) converts.
        inline string join_operation_names(const flat_set<string>& names) {
            string joined;
            for (const string& n : names) {
                if (!joined.empty()) joined += ',';
                joined += n;
            }
            return joined;
        }

        inline flat_set<string> unpack_operation_names(const shared_string& packed) {
            flat_set<string> names;
            const string joined = to_string(packed);
            if (joined.empty()) return names;
            size_t pos = 0;
            while (pos <= joined.size()) {
                const size_t comma = joined.find(',', pos);
                const size_t end = (comma == string::npos) ? joined.size() : comma;
                if (end > pos) names.insert(joined.substr(pos, end - pos));
                if (comma == string::npos) break;
                pos = comma + 1;
            }
            return names;
        }

        // ───────────────────────── 1.14 agent_permission_object ─────────────────────────
        class agent_permission_object
                : public object<agent_permission_object_type, agent_permission_object> {
        public:
            agent_permission_object() = delete;
            template<typename Constructor, typename Allocator>
            agent_permission_object(Constructor&& c, allocator<Allocator> a) : operations(a), addons(a) { c(*this); }

            id_type           id;
            account_name_type account;      ///< principal that granted the access
            account_name_type agent_name;   ///< label, unique per principal (not an account)
            public_key_type   agent_key;    ///< key that signs for the principal; unique per principal
            shared_string     operations;   ///< canonical `,`-joined wire names; empty = addon-only agent
            time_point_sec    expiration;   ///< epoch = perpetual; past = no longer valid
            shared_string     addons;       ///< `,`-joined off-chain scopes; opaque to consensus
        };

        struct by_permission_account;
        typedef multi_index_container<
            agent_permission_object,
            indexed_by<
                ordered_unique<tag<by_id>,
                    member<agent_permission_object, agent_permission_id_type, &agent_permission_object::id>>,
                // (principal, agent name): one row per name, so re-granting replaces instead of piling
                // up, and the authority hook walks exactly one principal's rows by prefix.
                ordered_unique<tag<by_permission_account>,
                    composite_key<agent_permission_object,
                        member<agent_permission_object, account_name_type, &agent_permission_object::account>,
                        member<agent_permission_object, account_name_type, &agent_permission_object::agent_name>
                    >,
                    composite_key_compare<string_less, string_less>
                >
            >,
            allocator<agent_permission_object>
        > agent_permission_index;

} } // graphene::chain

FC_REFLECT((graphene::chain::agent_permission_object), (id)(account)(agent_name)(agent_key)(operations)(expiration)(addons))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::agent_permission_object, graphene::chain::agent_permission_index)
