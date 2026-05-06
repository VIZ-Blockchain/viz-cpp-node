#ifndef CHAIN_TAG_API_OBJ_HPP
#define CHAIN_TAG_API_OBJ_HPP

#include <graphene/plugins/tags/tags_object.hpp>
#include <graphene/protocol/asset.hpp>

namespace graphene { namespace plugins { namespace tags {
    struct tag_api_object {
        tag_api_object(const tags::tag_stats_object& o)
            : name(o.name),
              total_children_rshares(o.total_children_rshares),
              total_payouts(o.total_payout),
              net_votes(o.net_votes), top_posts(o.top_posts),
              contents(o.contents) {
        }

        tag_api_object() {
        }

        std::string name;
        fc::uint128_t total_children_rshares;
        graphene::protocol::asset total_payouts;
        int32_t net_votes = 0;
        uint32_t top_posts = 0;
        uint32_t contents = 0;
    };
} } } // graphene::plugins::tags


FC_REFLECT((graphene::plugins::tags::tag_api_object),
    (name)(total_children_rshares)(total_payouts)(net_votes)(top_posts)(contents)
)
#endif //CHAIN_TAG_API_OBJ_HPP
