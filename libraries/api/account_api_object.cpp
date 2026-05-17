#include <graphene/api/account_api_object.hpp>

namespace graphene { namespace api {

using graphene::chain::by_account;
using graphene::chain::account_authority_object;
using graphene::chain::account_metadata_object;

account_api_object::account_api_object(const account_object& a, const graphene::chain::database& db)
    :   id(a.id), name(a.name), memo_key(a.memo_key), proxy(a.proxy), referrer(a.referrer),
        last_account_update(a.last_account_update), created(a.created),
        recovery_account(a.recovery_account), last_account_recovery(a.last_account_recovery),
        subcontent_count(a.subcontent_count), vote_count(a.vote_count), content_count(a.content_count),
        awarded_rshares(a.awarded_rshares), custom_sequence(a.custom_sequence), custom_sequence_block_num(a.custom_sequence_block_num), energy(a.energy), last_vote_time(a.last_vote_time),
        balance(a.balance), curation_rewards(a.curation_rewards), posting_rewards(a.posting_rewards),
        receiver_awards(a.receiver_awards), benefactor_awards(a.benefactor_awards),
        vesting_shares(a.vesting_shares),
        delegated_vesting_shares(a.delegated_vesting_shares), received_vesting_shares(a.received_vesting_shares),
        vesting_withdraw_rate(a.vesting_withdraw_rate), next_vesting_withdrawal(a.next_vesting_withdrawal),
        withdrawn(a.withdrawn), to_withdraw(a.to_withdraw), withdraw_routes(a.withdraw_routes),
        validators_voted_for(a.validators_voted_for), validators_vote_weight(a.validators_vote_weight), last_root_post(a.last_root_post), last_post(a.last_post),
        average_bandwidth(a.average_bandwidth), lifetime_bandwidth(a.lifetime_bandwidth), last_bandwidth_update(a.last_bandwidth_update),
        valid(a.valid), account_seller(a.account_seller), account_offer_price(a.account_offer_price), account_on_sale(a.account_on_sale), account_on_sale_start_time(a.account_on_sale_start_time),
        subaccount_seller(a.subaccount_seller), subaccount_offer_price(a.subaccount_offer_price), subaccount_on_sale(a.subaccount_on_sale),
        reserved_balance(a.reserved_balance), target_buyer(a.target_buyer), account_on_auction(a.account_on_auction),
        current_bid(a.current_bid), current_bidder(a.current_bidder), current_bidder_key(a.current_bidder_key),
        last_bid(a.last_bid){
    size_t n = a.proxied_vsf_votes.size();
    proxied_vsf_votes.reserve(n);
    for (size_t i = 0; i < n; i++) {
        proxied_vsf_votes.push_back(a.proxied_vsf_votes[i]);
    }

    const auto& auth = db.get<account_authority_object, by_account>(name);
    master_authority = authority(auth.master);
    active_authority = authority(auth.active);
    regular_authority = authority(auth.regular);
    last_master_update = auth.last_master_update;

#ifndef IS_LOW_MEM
    const auto& meta = db.get<account_metadata_object, by_account>(name);
    json_metadata = graphene::chain::to_string(meta.json_metadata);
#endif

}

account_api_object::account_api_object() {}

} } // graphene::api
