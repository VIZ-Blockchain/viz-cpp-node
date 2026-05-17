#include <graphene/plugins/follow/follow_operations.hpp>
#include <graphene/protocol/operation_util_impl.hpp>

namespace graphene {
    namespace plugins {
        namespace follow {

            void follow_operation::validate() const {
                FC_ASSERT(follower != following, "You cannot follow yourself");
            }

            void reblog_operation::validate() const {
                FC_ASSERT(account != author, "You cannot reblog your own content");
            }

        }
    }
} //graphene::follow

DEFINE_OPERATION_TYPE(graphene::plugins::follow::follow_plugin_operation)