#pragma once

// The pm_market_meta_object / pm_market_meta_index TYPE was moved to libraries/chain
// (graphene/chain/pm_meta_object.hpp) so shared consumers — notably the snapshot plugin — can
// serialize it into the snapshot without a plugin-to-plugin dependency. Runtime registration
// (add_plugin_index) and all the build/prune logic still live in this plugin. This shim re-exposes
// the names in the prediction_market_api namespace so existing plugin code compiles unchanged.

#include <graphene/chain/pm_meta_object.hpp>

namespace graphene { namespace plugins { namespace prediction_market_api {
    using namespace graphene::chain;
} } } // graphene::plugins::prediction_market_api
