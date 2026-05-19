#pragma once

#include <graphene/chain/validator_objects.hpp>
#include <graphene/chain/database.hpp>

namespace graphene { namespace api {
    using graphene::protocol::asset;
    using graphene::chain::chain_properties;
    using graphene::chain::database;

    struct chain_api_properties {
        chain_api_properties(const chain_properties&, const database&);
        chain_api_properties() = default;

        asset account_creation_fee;
        uint32_t maximum_block_size;

        uint32_t create_account_delegation_ratio;
        uint32_t create_account_delegation_time;
        asset min_delegation;
        int16_t min_curation_percent;
        int16_t max_curation_percent;
        int16_t bandwidth_reserve_percent;
        asset bandwidth_reserve_below;
        int16_t flag_energy_additional_cost;
        uint32_t vote_accounting_min_rshares;
        int16_t committee_request_approve_min_percent;

        fc::optional<int16_t> inflation_validator_percent;
        fc::optional<int16_t> inflation_ratio_committee_vs_reward_fund;
        fc::optional<uint32_t> inflation_recalc_period;

        fc::optional<uint32_t> data_operations_cost_additional_bandwidth;
        fc::optional<int16_t> validator_miss_penalty_percent;
        fc::optional<uint32_t> validator_miss_penalty_duration;

        fc::optional<asset> create_invite_min_balance;
        fc::optional<asset> committee_create_request_fee;
        fc::optional<asset> create_paid_subscription_fee;
        fc::optional<asset> account_on_sale_fee;
        fc::optional<asset> subaccount_on_sale_fee;
        fc::optional<asset> validator_declaration_fee;
        fc::optional<uint16_t> withdraw_intervals;
        fc::optional<uint32_t> distribution_epoch_length;
    };

} } // graphene::api

FC_REFLECT(
    (graphene::api::chain_api_properties),
    (account_creation_fee)(maximum_block_size)(maximum_block_size)
    (create_account_delegation_ratio)(create_account_delegation_time)(min_delegation)
    (min_curation_percent)(max_curation_percent)(bandwidth_reserve_percent)(bandwidth_reserve_below)
    (flag_energy_additional_cost)(vote_accounting_min_rshares)(committee_request_approve_min_percent)
    (inflation_validator_percent)(inflation_ratio_committee_vs_reward_fund)(inflation_recalc_period)
    (data_operations_cost_additional_bandwidth)(validator_miss_penalty_percent)(validator_miss_penalty_duration)
    (create_invite_min_balance)(committee_create_request_fee)(create_paid_subscription_fee)
    (account_on_sale_fee)(subaccount_on_sale_fee)(validator_declaration_fee)(withdraw_intervals)
    (distribution_epoch_length))