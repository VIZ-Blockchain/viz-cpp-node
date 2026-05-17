#pragma once

#include <graphene/protocol/exceptions.hpp>

#define CHAIN_DECLARE_OP_BASE_EXCEPTIONS(op_name)                \
   FC_DECLARE_DERIVED_EXCEPTION(                                      \
      op_name ## _validate_exception,                                 \
      graphene::chain::operation_validate_exception,                  \
      4040000 + 100 * protocol::operation::tag< protocol::op_name ## _operation >::value, \
      #op_name "_operation validation exception"                      \
      )                                                               \
   FC_DECLARE_DERIVED_EXCEPTION(                                      \
      op_name ## _evaluate_exception,                                 \
      graphene::chain::operation_evaluate_exception,                  \
      4050000 + 100 * protocol::operation::tag< protocol::op_name ## _operation >::value, \
      #op_name "_operation evaluation exception"                      \
      )

#define CHAIN_DECLARE_OP_VALIDATE_EXCEPTION(exc_name, op_name, seqnum, msg) \
   FC_DECLARE_DERIVED_EXCEPTION(                                      \
      op_name ## _ ## exc_name,                                       \
      graphene::chain::op_name ## _validate_exception,                \
      4040000 + 100 * protocol::operation::tag< protocol::op_name ## _operation >::value  \
         + seqnum,                                                    \
      msg                                                             \
      )

#define CHAIN_DECLARE_OP_EVALUATE_EXCEPTION(exc_name, op_name, seqnum, msg) \
   FC_DECLARE_DERIVED_EXCEPTION(                                      \
      op_name ## _ ## exc_name,                                       \
      graphene::chain::op_name ## _evaluate_exception,                \
      4050000 + 100 * protocol::operation::tag< protocol::op_name ## _operation >::value  \
         + seqnum,                                                    \
      msg                                                             \
      )

#define CHAIN_DECLARE_INTERNAL_EXCEPTION(exc_name, seqnum, msg)  \
   FC_DECLARE_DERIVED_EXCEPTION(                                      \
      internal_ ## exc_name,                                          \
      graphene::chain::internal_exception,                            \
      4990000 + seqnum,                                               \
      msg                                                             \
      )

#define CHAIN_TRY_NOTIFY(signal, ...)                                     \
   try                                                                        \
   {                                                                          \
      signal( __VA_ARGS__ );                                                  \
   }                                                                          \
   catch( const graphene::chain::plugin_exception& e )                         \
   {                                                                          \
      elog( "Caught plugin exception: ${e}", ("e", e.to_detail_string() ) );  \
      throw;                                                                  \
   }                                                                          \
   catch( const fc::exception& e )                                            \
   {                                                                          \
      elog( "Caught exception in plugin: ${e}", ("e", e.to_detail_string() ) ); \
   }                                                                          \
   catch( ... )                                                               \
   {                                                                          \
      wlog( "Caught unexpected exception in plugin" );                        \
   }

namespace graphene {
    namespace chain {

        FC_DECLARE_EXCEPTION(chain_exception, 4000000, "blockchain exception")

        FC_DECLARE_DERIVED_EXCEPTION(database_query_exception, graphene::chain::chain_exception, 4010000, "database query exception")

        FC_DECLARE_DERIVED_EXCEPTION(block_validate_exception, graphene::chain::chain_exception, 4020000, "block validation exception")

        FC_DECLARE_DERIVED_EXCEPTION(transaction_exception, graphene::chain::chain_exception, 4030000, "transaction validation exception")

        FC_DECLARE_DERIVED_EXCEPTION(operation_validate_exception, graphene::chain::chain_exception, 4040000, "operation validation exception")

        FC_DECLARE_DERIVED_EXCEPTION(operation_evaluate_exception, graphene::chain::chain_exception, 4050000, "operation evaluation exception")

        FC_DECLARE_DERIVED_EXCEPTION(utility_exception, graphene::chain::chain_exception, 4060000, "utility method exception")

        FC_DECLARE_DERIVED_EXCEPTION(undo_database_exception, graphene::chain::chain_exception, 4070000, "undo database exception")

        FC_DECLARE_DERIVED_EXCEPTION(unlinkable_block_exception, graphene::chain::chain_exception, 4080000, "unlinkable block")

        FC_DECLARE_DERIVED_EXCEPTION(block_too_old_exception, graphene::chain::chain_exception, 4080100, "block is too old for fork database")

        FC_DECLARE_DERIVED_EXCEPTION(deferred_resize_exception, graphene::chain::chain_exception, 4080200, "shared memory resize deferred, retry block")

        FC_DECLARE_DERIVED_EXCEPTION(unknown_hardfork_exception, graphene::chain::chain_exception, 4090000, "chain attempted to apply unknown hardfork")

        FC_DECLARE_DERIVED_EXCEPTION(plugin_exception, graphene::chain::chain_exception, 4100000, "plugin exception")

        FC_DECLARE_DERIVED_EXCEPTION(block_log_exception, graphene::chain::chain_exception, 4110000, "block log exception")

        FC_DECLARE_DERIVED_EXCEPTION(pop_empty_chain, graphene::chain::undo_database_exception, 4070001, "there are no blocks to pop")

        CHAIN_DECLARE_OP_BASE_EXCEPTIONS(transfer);
//   CHAIN_DECLARE_OP_EVALUATE_EXCEPTION( from_account_not_whitelisted, transfer, 1, "owner mismatch" )

        CHAIN_DECLARE_OP_BASE_EXCEPTIONS(account_create);

        CHAIN_DECLARE_OP_EVALUATE_EXCEPTION(max_auth_exceeded, account_create, 1, "Exceeds max authority fan-out")

        CHAIN_DECLARE_OP_EVALUATE_EXCEPTION(auth_account_not_found, account_create, 2, "Auth account not found")

        CHAIN_DECLARE_OP_BASE_EXCEPTIONS(account_update);

        CHAIN_DECLARE_OP_EVALUATE_EXCEPTION(max_auth_exceeded, account_update, 1, "Exceeds max authority fan-out")

        CHAIN_DECLARE_OP_EVALUATE_EXCEPTION(auth_account_not_found, account_update, 2, "Auth account not found")

        FC_DECLARE_DERIVED_EXCEPTION(internal_exception, graphene::chain::chain_exception, 4990000, "internal exception")

        CHAIN_DECLARE_INTERNAL_EXCEPTION(verify_auth_max_auth_exceeded, 1, "Exceeds max authority fan-out")

        CHAIN_DECLARE_INTERNAL_EXCEPTION(verify_auth_account_not_found, 2, "Auth account not found")

        FC_DECLARE_DERIVED_EXCEPTION(database_revision_exception, graphene::chain::chain_exception, 4120000, "database revision exception")

        FC_DECLARE_DERIVED_EXCEPTION(database_signal_exception, graphene::chain::chain_exception, 4130000, "database signal exception")

        FC_DECLARE_DERIVED_EXCEPTION(shared_memory_corruption_exception, graphene::chain::chain_exception, 4140000, "shared memory corruption detected")
    }
} // graphene::chain


#pragma once

#include <fc/exception/exception.hpp>
#include <graphene/protocol/exceptions.hpp>

namespace graphene {
    namespace chain {


    }
} // graphene::chain
