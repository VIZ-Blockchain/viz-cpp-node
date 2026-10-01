#pragma once

#include <graphene/chain/evaluator.hpp>
#include <graphene/protocol/transaction.hpp>

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

        // Only direct agent-dependent requirements, indexed by operation. These are
        // ephemeral validation proofs, never persistent or nested account approvals.
        struct agent_proofs {
            std::vector<fc::flat_map<graphene::protocol::account_name_type,
                                    graphene::protocol::public_key_type>> operations;
        };

        bool agent_operation_allowed(const database& db, const graphene::protocol::operation& op,
                                     const graphene::protocol::account_name_type& principal,
                                     const graphene::protocol::public_key_type& key);

        void verify_agent_transaction(const database& db,
                                      const graphene::protocol::signed_transaction& trx,
                                      const fc::flat_set<graphene::protocol::public_key_type>& keys,
                                      bool allow_unused = false,
                                      fc::flat_set<graphene::protocol::public_key_type>* used = nullptr,
                                      agent_proofs* proofs = nullptr);

} } // graphene::chain
