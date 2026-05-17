#ifndef CHAIN_FOLLOW_EVALUATORS_HPP
#define CHAIN_FOLLOW_EVALUATORS_HPP

#include <graphene/plugins/follow/follow_operations.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/evaluator.hpp>

namespace graphene {
    namespace plugins {
        namespace follow {
            using graphene::chain::evaluator;
            using graphene::chain::database;

            class follow_evaluator : public graphene::chain::evaluator_impl<follow_evaluator, follow_plugin_operation> {
            public:
                typedef follow_operation operation_type;

                follow_evaluator(database &db, plugin *plugin) : graphene::chain::evaluator_impl<follow_evaluator, follow_plugin_operation>(db), _plugin(plugin) {
                }

                void do_apply(const follow_operation &o);

                plugin *_plugin;
            };

            class reblog_evaluator : public graphene::chain::evaluator_impl<reblog_evaluator, follow_plugin_operation> {
            public:
                typedef reblog_operation operation_type;

                reblog_evaluator(database &db, plugin *plugin) : graphene::chain::evaluator_impl<reblog_evaluator, follow_plugin_operation>(db), _plugin(plugin) {
                }

                void do_apply(const reblog_operation &o);

                plugin *_plugin;
            };
        }
    }
}

#endif //CHAIN_FOLLOW_EVALUATORS_HPP
