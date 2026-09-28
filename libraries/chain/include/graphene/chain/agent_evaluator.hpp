#pragma once
#include <graphene/chain/evaluator.hpp>
#include <graphene/protocol/transaction.hpp>

namespace graphene { namespace chain {
class set_agent_permission_evaluator : public evaluator_impl<set_agent_permission_evaluator> {
public:
    typedef graphene::protocol::set_agent_permission_operation operation_type;
    set_agent_permission_evaluator(database& db) : evaluator_impl<set_agent_permission_evaluator>(db) {}
    void do_apply(const operation_type& o);
};

// Authorize direct active/regular requirements per operation. Other, master, and nested
// account authorities always use the unmodified chain authority getters.
void verify_agent_transaction(const database& db, const graphene::protocol::signed_transaction& trx,
                              const fc::flat_set<graphene::protocol::public_key_type>& keys,
                              bool allow_unused = false,
                              fc::flat_set<graphene::protocol::public_key_type>* used = nullptr);
} } // graphene::chain
