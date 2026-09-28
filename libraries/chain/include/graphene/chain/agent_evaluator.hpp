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

        /// HF15 agent access: principals of `trx` whose required ACTIVE authority is satisfied by a
        /// delegate, mapped to the delegate. The authority hook in database.cpp answers
        /// `get_active(principal)` with the AGENT's active authority for these, so the ordinary
        /// sign_state path performs the actual signature check — this function only decides whether
        /// a delegation applies at all, and never grants anything by itself.
        ///
        /// Empty unless HF15 is active. Returns nothing for a transaction that asks for MASTER or
        /// REGULAR authority: nested account authorities are resolved through ACTIVE, so a
        /// substitution could otherwise satisfy a master requirement of another account. Refusing
        /// the whole transaction keeps "master is never delegable" structural rather than reasoned.
        ///
        /// A delegation must cover EVERY operation of the transaction that requires an authority —
        /// not just the ones naming the principal. The hook cannot tell a top-level requirement from
        /// a nested one (both arrive through the same getter), so the only statement it can stand
        /// behind is "the agent acted within its grant for every operation here".
        fc::flat_map<graphene::protocol::account_name_type, graphene::protocol::account_name_type>
        delegated_active_authorities(const database& db,
                                     const graphene::protocol::signed_transaction& trx,
                                     const graphene::protocol::chain_id_type& chain_id);

} } // graphene::chain
