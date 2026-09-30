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

        /// HF15: eligible direct ACTIVE fallback keys for principals required by `trx`.
        /// The ordinary active getter is never replaced, so nested account_auths cannot
        /// inherit an agent's rights. Every authority-requiring operation must be covered.
        /// `candidate_keys` supports unsigned RPC signature discovery; when null the
        /// transaction's actual signatures are recovered and checked.
        fc::flat_map<graphene::protocol::account_name_type, graphene::protocol::public_key_type>
        delegated_active_authorities(const database& db,
                                     const graphene::protocol::signed_transaction& trx,
                                     const graphene::protocol::chain_id_type& chain_id,
                                     const fc::flat_set<graphene::protocol::public_key_type>* candidate_keys = nullptr);

} } // graphene::chain
