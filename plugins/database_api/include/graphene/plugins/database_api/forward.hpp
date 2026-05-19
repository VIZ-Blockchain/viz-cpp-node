#ifndef CHAIN_FORWARD_HPP
#define CHAIN_FORWARD_HPP

#include <graphene/chain/chain_objects.hpp>

namespace graphene { namespace plugins { namespace database_api {
			using protocol::share_type;
            typedef graphene::chain::change_recovery_account_request_object change_recovery_account_request_api_object;
            typedef graphene::chain::block_summary_object block_summary_api_object;
            typedef graphene::chain::content_vote_object content_vote_api_object;
            typedef graphene::chain::dynamic_global_property_object dynamic_global_property_api_object;
            typedef graphene::chain::escrow_object escrow_api_object;
            typedef graphene::chain::withdraw_vesting_route_object withdraw_vesting_route_api_object;
            typedef graphene::chain::witness_vote_object witness_vote_api_object;
            typedef graphene::chain::validator_schedule_object witness_schedule_api_object;

using vesting_delegation_api_object = graphene::chain::vesting_delegation_object;
using vesting_delegation_expiration_api_object = graphene::chain::vesting_delegation_expiration_object;

} } } // graphene::plugins::database_api

#endif //CHAIN_FORWARD_HPP
