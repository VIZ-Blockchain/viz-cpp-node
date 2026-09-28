#pragma once

#include <graphene/chain/evaluator.hpp>

namespace graphene { namespace chain {

        /// HF15 agent access: applies set_agent_permission_operation (grant / re-grant / revoke).
        class set_agent_permission_evaluator
                : public evaluator_impl<set_agent_permission_evaluator> {
        public:
            typedef graphene::protocol::set_agent_permission_operation operation_type;

            set_agent_permission_evaluator(database& db)
                    : evaluator_impl<set_agent_permission_evaluator>(db) {}

            void do_apply(const operation_type& o);
        };

        /// HF15 agent access: principals of `trx` whose required ACTIVE authority is answered by one
        /// of their agent keys, mapped to that key. The authority hook in database.cpp answers
        /// `get_active(principal)` with a single-key authority of that key, so the ordinary
        /// sign_state path still performs the signature check — this function only decides whether
        /// an agent applies at all, and never grants anything by itself.
        ///
        /// Empty unless HF15 is active, and for any transaction that asks for MASTER or REGULAR
        /// authority. An agent must cover EVERY authority-requiring operation of the transaction:
        /// the hook cannot tell a top-level requirement from a nested one, so the only statement it
        /// can stand behind is "the agent acted within its list for every operation here".
        fc::flat_map<graphene::protocol::account_name_type, graphene::protocol::public_key_type>
        delegated_active_authorities(const database& db,
                                     const graphene::protocol::signed_transaction& trx,
                                     const graphene::protocol::chain_id_type& chain_id);

} } // graphene::chain
