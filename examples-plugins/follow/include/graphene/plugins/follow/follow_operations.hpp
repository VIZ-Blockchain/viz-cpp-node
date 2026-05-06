#pragma once

#include <graphene/protocol/base.hpp>
#include <graphene/chain/evaluator.hpp>
#include <graphene/plugins/follow/plugin.hpp>

namespace graphene {
    namespace plugins {
        namespace follow {
            using graphene::chain::base_operation;
            using graphene::protocol::account_name_type;
            struct follow_operation : base_operation {
                protocol::account_name_type follower;
                protocol::account_name_type following;
                std::set<std::string> what; /// blog, mute

                void validate() const;
                void get_required_regular_authorities(flat_set<account_name_type>& a) const {
                    a.insert(follower);
                }
            };

            struct reblog_operation : base_operation{
                protocol::account_name_type account;
                protocol::account_name_type author;
                std::string permlink;

                void validate() const;
                void get_required_regular_authorities(flat_set<account_name_type>& a) const {
                    a.insert(account);
                }
            };

            using follow_plugin_operation = fc::static_variant<follow_operation, reblog_operation>;

        }
    }
} // graphene::follow

FC_REFLECT((graphene::plugins::follow::follow_operation), (follower)(following)(what));
FC_REFLECT((graphene::plugins::follow::reblog_operation), (account)(author)(permlink));

FC_REFLECT_TYPENAME((graphene::plugins::follow::follow_plugin_operation));
DECLARE_OPERATION_TYPE(graphene::plugins::follow::follow_plugin_operation)
