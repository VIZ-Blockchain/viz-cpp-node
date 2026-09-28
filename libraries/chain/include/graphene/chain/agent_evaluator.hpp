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

} } // graphene::chain
