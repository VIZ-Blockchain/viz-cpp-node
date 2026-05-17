#pragma once

#include <graphene/chain/witness_objects.hpp>
#include <graphene/chain/chain_object_types.hpp>
#include <graphene/api/chain_api_properties.hpp>

namespace graphene { namespace api {
    using namespace graphene::chain;

    struct witness_api_object {
        witness_api_object(const witness_object &w, const database& db);

        witness_api_object() = default;

        witness_object::id_type id;
        account_name_type owner;
        time_point_sec created;
        std::string url;
        uint32_t total_missed;
        uint64_t last_aslot;
        uint64_t last_confirmed_block_num;
        public_key_type signing_key;
        api::chain_api_properties props;
        share_type votes;
        uint32_t penalty_percent;
        share_type counted_votes;
        fc::uint128_t virtual_last_update;
        fc::uint128_t virtual_position;
        fc::uint128_t virtual_scheduled_time;
        digest_type last_work;
        version running_version;
        hardfork_version hardfork_version_vote;
        time_point_sec hardfork_time_vote;
        // HF13: validator reward sharing
        uint16_t sharing_rate = 0;
        share_type pending_stakeholder_reward = 0;
    };

} } // graphene::api


FC_REFLECT(
    (graphene::api::witness_api_object),
    (id)(owner)(created)(url)(votes)(penalty_percent)(counted_votes)(virtual_last_update)(virtual_position)(virtual_scheduled_time)
    (total_missed)(last_aslot)(last_confirmed_block_num)(signing_key)(props)
    (last_work)(running_version)(hardfork_version_vote)(hardfork_time_vote)
    (sharing_rate)(pending_stakeholder_reward))
