#pragma once

#include <graphene/plugins/private_message/private_message_objects.hpp>
#include <graphene/plugins/private_message/private_message_plugin.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/evaluator.hpp>

namespace graphene {
    namespace plugins {
        namespace private_message {

            class private_message_evaluator : public graphene::chain::evaluator_impl<private_message_evaluator, private_message_plugin_operation>
            {
            public:
                typedef private_message_operation operation_type;

                private_message_evaluator(database& db, private_message_plugin* plugin)
                        : graphene::chain::evaluator_impl<private_message_evaluator, private_message_plugin_operation>( db )
                        , _plugin( plugin )
                {}

                void do_apply( const private_message_operation& o );

                private_message_plugin* _plugin;
            };

        }
    }
}
