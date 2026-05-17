#include <graphene/api/chain_api_properties.hpp>

namespace graphene { namespace api {

    chain_api_properties::chain_api_properties(
        const chain_properties& src,
        const database& db
    ) : account_creation_fee(src.account_creation_fee),
        maximum_block_size(src.maximum_block_size),
        create_account_delegation_ratio(src.create_account_delegation_ratio),
        create_account_delegation_time(src.create_account_delegation_time),
        min_delegation(src.min_delegation),
        min_curation_percent(src.min_curation_percent),
        max_curation_percent(src.max_curation_percent),
        bandwidth_reserve_percent(src.bandwidth_reserve_percent),
        bandwidth_reserve_below(src.bandwidth_reserve_below),
        flag_energy_additional_cost(src.flag_energy_additional_cost),
        vote_accounting_min_rshares(src.vote_accounting_min_rshares),
        committee_request_approve_min_percent(src.committee_request_approve_min_percent)
    {
        if (db.has_hardfork(CHAIN_HARDFORK_4)) {
            inflation_witness_percent=src.inflation_witness_percent;
            inflation_ratio_committee_vs_reward_fund=src.inflation_ratio_committee_vs_reward_fund;
            inflation_recalc_period=src.inflation_recalc_period;
        }
        if (db.has_hardfork(CHAIN_HARDFORK_6)) {
            data_operations_cost_additional_bandwidth=src.data_operations_cost_additional_bandwidth;
            witness_miss_penalty_percent=src.witness_miss_penalty_percent;
            witness_miss_penalty_duration=src.witness_miss_penalty_duration;
        }
        if (db.has_hardfork(CHAIN_HARDFORK_9)) {
            create_invite_min_balance=src.create_invite_min_balance;
            committee_create_request_fee=src.committee_create_request_fee;
            create_paid_subscription_fee=src.create_paid_subscription_fee;
            account_on_sale_fee=src.account_on_sale_fee;
            subaccount_on_sale_fee=src.subaccount_on_sale_fee;
            witness_declaration_fee=src.witness_declaration_fee;
            withdraw_intervals=src.withdraw_intervals;
        }
        if (db.has_hardfork(CHAIN_HARDFORK_13)) {
            distribution_epoch_length=src.distribution_epoch_length;
        }
    }

} } // graphene::api
