#include <graphene/plugins/private_message/private_message_objects.hpp>
#include <graphene/protocol/operation_util_impl.hpp>

namespace graphene {
    namespace plugins {
        namespace private_message {

            void private_message_operation::validate() const {
                FC_ASSERT(from != to, "You cannot write to yourself");
            }
        }
    }
}

DEFINE_OPERATION_TYPE(graphene::plugins::private_message::private_message_plugin_operation);
