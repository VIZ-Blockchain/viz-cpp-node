#pragma once

#include <fc/fixed_string.hpp>

#include <graphene/protocol/authority.hpp>
#include <graphene/protocol/chain_operations.hpp>

#include <graphene/chain/chain_object_types.hpp>
#include <graphene/chain/validator_objects.hpp>
#include <graphene/chain/shared_authority.hpp>

#include <boost/multi_index/composite_key.hpp>

#include <numeric>

namespace graphene { namespace chain {

using graphene::protocol::authority;

class account_object
        : public object<account_object_type, account_object> {
public:
    account_object() = delete;

    template<typename Constructor, typename Allocator>
    account_object(Constructor&& c, allocator<Allocator> a) {
        c(*this);
    };

    id_type id;

    account_name_type name;
    public_key_type memo_key;
    account_name_type proxy;
    account_name_type referrer;

    time_point_sec last_account_update;

    time_point_sec created;
    account_name_type recovery_account;
    time_point_sec last_account_recovery;
    uint32_t subcontent_count = 0;
    uint32_t vote_count = 0;
    uint32_t content_count = 0;
    uint64_t awarded_rshares = 0;
    uint64_t custom_sequence = 0;
    uint64_t custom_sequence_block_num = 0;

    int16_t energy = CHAIN_100_PERCENT;   ///< current voting power of this account, it falls after every vote
    time_point_sec last_vote_time; ///< used to increase the voting power of this account the longer it goes without voting.

    asset balance = asset(0, TOKEN_SYMBOL);  ///< total liquid shares held by this account

    share_type curation_rewards = 0;
    share_type posting_rewards = 0;
    share_type receiver_awards = 0;
    share_type benefactor_awards = 0;

    asset vesting_shares = asset(0, SHARES_SYMBOL); ///< total vesting shares held by this account, controls its voting power
    asset delegated_vesting_shares = asset(0, SHARES_SYMBOL); ///<
    asset received_vesting_shares = asset(0, SHARES_SYMBOL); ///<

    asset vesting_withdraw_rate = asset(0, SHARES_SYMBOL); ///< at the time this is updated it can be at most vesting_shares/CHAIN_VESTING_WITHDRAW_INTERVALS
    time_point_sec next_vesting_withdrawal = fc::time_point_sec::maximum(); ///< after every withdrawal this is incremented by 1 week
    share_type withdrawn = 0; /// Track how many shares have been withdrawn
    share_type to_withdraw = 0; /// Might be able to look this up with operation history.
    uint16_t withdraw_routes = 0;

    fc::array<share_type, CHAIN_MAX_PROXY_RECURSION_DEPTH> proxied_vsf_votes;// = std::vector<share_type>( CHAIN_MAX_PROXY_RECURSION_DEPTH, 0 ); ///< the total VFS votes proxied to this account

    uint16_t witnesses_voted_for = 0;
    share_type witnesses_vote_weight = 0;

    time_point_sec last_root_post;
    time_point_sec last_post;

    share_type average_bandwidth;
    share_type lifetime_bandwidth;
    time_point_sec last_bandwidth_update;

    /// This function should be used only when the account votes for a witness directly
    share_type witness_vote_weight() const {
        return std::accumulate(proxied_vsf_votes.begin(),
                proxied_vsf_votes.end(),
                vesting_shares.amount);
    }
    share_type witness_vote_fair_weight_prehf5() const {
        share_type weight=0;
        if(0<witnesses_voted_for){
            weight = (fc::uint128_t(vesting_shares.amount.value) / witnesses_voted_for).to_uint64();
        }
        return std::accumulate(proxied_vsf_votes.begin(),
                proxied_vsf_votes.end(),
                weight);
    }

    share_type witness_vote_fair_weight() const {
        share_type weight=std::accumulate(proxied_vsf_votes.begin(),proxied_vsf_votes.end(),vesting_shares.amount);
        share_type fair_weight=0;
        if(0<witnesses_voted_for){
            fair_weight = (fc::uint128_t(weight) / witnesses_voted_for).to_uint64();
        }
        return fair_weight;
    }

    share_type proxied_vsf_votes_total() const {
        return std::accumulate(proxied_vsf_votes.begin(),
                proxied_vsf_votes.end(),
                share_type());
    }

    /// vesting shares used in voting and bandwidth calculation
    asset effective_vesting_shares() const {
        return vesting_shares - delegated_vesting_shares + received_vesting_shares;
    }

    /// vesting shares, which can be used for delegation (incl. create account) and withdraw operations
    asset available_vesting_shares(bool consider_withdrawal = false) const {
        auto have = vesting_shares - delegated_vesting_shares;
        return consider_withdrawal ? have - asset(to_withdraw - withdrawn, SHARES_SYMBOL) : have;
    }

    bool valid = true;

    account_name_type account_seller;
    asset account_offer_price = asset(0, TOKEN_SYMBOL);
    bool account_on_sale = false;
    time_point_sec account_on_sale_start_time = fc::time_point_sec::min();//extend on new bid to max(now+5min,account_on_sale_start_time)

    asset reserved_balance = asset(0, TOKEN_SYMBOL);//locked by operations with expiration

    account_name_type target_buyer;//set target buyer, without auction

    bool account_on_auction = false;
    asset current_bid = asset(0, TOKEN_SYMBOL);//next bid must be >= max(account_offer_price/10,current_bid-last_bid)
    account_name_type current_bidder;
    public_key_type current_bidder_key;
    asset last_bid = asset(0, TOKEN_SYMBOL);

    account_name_type subaccount_seller;
    asset subaccount_offer_price = asset(0, TOKEN_SYMBOL);
    bool subaccount_on_sale = false;
};

class account_authority_object
        : public object<account_authority_object_type, account_authority_object> {
public:
    account_authority_object() = delete;

    template<typename Constructor, typename Allocator>
    account_authority_object(Constructor &&c, allocator<Allocator> a)
            : master(a), active(a), regular(a) {
        c(*this);
    }

    id_type id;

    account_name_type account;

    shared_authority master;   ///< used for backup control, can set master or active
    shared_authority active;  ///< used for all monetary operations, can set active or regular
    shared_authority regular; ///< used for voting and regular

    time_point_sec last_master_update;
};

class account_metadata_object : public object<account_metadata_object_type, account_metadata_object> {
public:
    account_metadata_object() = delete;

    template<typename Constructor, typename Allocator>
    account_metadata_object(Constructor&& c, allocator<Allocator> a) : json_metadata(a) {
        c(*this);
    }

    id_type id;
    account_name_type account;
    shared_string json_metadata;
};

class vesting_delegation_object: public object<vesting_delegation_object_type, vesting_delegation_object> {
public:
    template<typename Constructor, typename Allocator>
    vesting_delegation_object(Constructor&& c, allocator<Allocator> a) {
        c(*this);
    }

    vesting_delegation_object() {
    }

    id_type id;
    account_name_type delegator;
    account_name_type delegatee;
    asset vesting_shares;
    time_point_sec min_delegation_time;
};

class fix_vesting_delegation_object: public object<fix_vesting_delegation_object_type, fix_vesting_delegation_object> {
public:
    template<typename Constructor, typename Allocator>
    fix_vesting_delegation_object(Constructor&& c, allocator<Allocator> a) {
        c(*this);
    }

    fix_vesting_delegation_object() {
    }

    id_type id;
    account_name_type delegator;
    account_name_type delegatee;
    asset vesting_shares;
};

class vesting_delegation_expiration_object: public object<vesting_delegation_expiration_object_type, vesting_delegation_expiration_object> {
public:
    template<typename Constructor, typename Allocator>
    vesting_delegation_expiration_object(Constructor&& c, allocator<Allocator> a) {
        c(*this);
    }

    vesting_delegation_expiration_object() {
    }

    id_type id;
    account_name_type delegator;
    asset vesting_shares;
    time_point_sec expiration;
};

class master_authority_history_object
        : public object<master_authority_history_object_type, master_authority_history_object> {
public:
    master_authority_history_object() = delete;

    template<typename Constructor, typename Allocator>
    master_authority_history_object(Constructor &&c, allocator<Allocator> a)
            :previous_master_authority(shared_authority::allocator_type(a.get_segment_manager())) {
        c(*this);
    }

    id_type id;

    account_name_type account;
    shared_authority previous_master_authority;
    time_point_sec last_valid_time;
};

class account_recovery_request_object
        : public object<account_recovery_request_object_type, account_recovery_request_object> {
public:
    account_recovery_request_object() = delete;

    template<typename Constructor, typename Allocator>
    account_recovery_request_object(Constructor &&c, allocator<Allocator> a)
            :new_master_authority(shared_authority::allocator_type(a.get_segment_manager())) {
        c(*this);
    }

    id_type id;

    account_name_type account_to_recover;
    shared_authority new_master_authority;
    time_point_sec expires;
};

class change_recovery_account_request_object
        : public object<change_recovery_account_request_object_type, change_recovery_account_request_object> {
public:
    template<typename Constructor, typename Allocator>
    change_recovery_account_request_object(Constructor &&c, allocator<Allocator> a) {
        c(*this);
    }

    id_type id;

    account_name_type account_to_recover;
    account_name_type recovery_account;
    time_point_sec effective_on;
};

struct by_name;
struct by_next_vesting_withdrawal;
struct by_account_on_sale;
struct by_account_on_auction;
struct by_account_on_sale_start_time;
struct by_subaccount_on_sale;

/**
 * @ingroup object_index
 */
typedef multi_index_container<
    account_object,
        indexed_by<
                ordered_unique<tag<by_id>,
                        member<account_object, account_id_type, &account_object::id> >,
                ordered_non_unique<tag<by_account_on_sale>,
                        member<account_object, bool, &account_object::account_on_sale> >,
                ordered_non_unique<tag<by_account_on_auction>,
                        member<account_object, bool, &account_object::account_on_auction> >,
                ordered_non_unique<tag<by_account_on_sale_start_time>,
                        member<account_object, time_point_sec, &account_object::account_on_sale_start_time> >,
                ordered_non_unique<tag<by_subaccount_on_sale>,
                        member<account_object, bool, &account_object::subaccount_on_sale> >,
                ordered_unique<tag<by_name>,
                        member<account_object, account_name_type, &account_object::name>,
                        protocol::string_less>,
                ordered_unique<tag<by_next_vesting_withdrawal>,

                composite_key < account_object,
                    member<account_object, time_point_sec, &account_object::next_vesting_withdrawal>,
                    member<account_object, account_id_type, &account_object::id>
                > > >,
    allocator<account_object>
>
account_index;

struct by_account;
struct by_last_valid;

typedef multi_index_container<
        master_authority_history_object,
        indexed_by<
                ordered_unique<tag<by_id>,
                        member<master_authority_history_object, master_authority_history_id_type, &master_authority_history_object::id>>,
                ordered_unique<tag<by_account>,
                        composite_key < master_authority_history_object,
                        member<master_authority_history_object, account_name_type, &master_authority_history_object::account>,
                        member<master_authority_history_object, time_point_sec, &master_authority_history_object::last_valid_time>,
                        member<master_authority_history_object, master_authority_history_id_type, &master_authority_history_object::id>
                >,
                composite_key_compare <
                std::less<account_name_type>, std::less<time_point_sec>, std::less<master_authority_history_id_type>>
>
>,
allocator<master_authority_history_object>
>
master_authority_history_index;


using account_metadata_index = multi_index_container<
    account_metadata_object,
    indexed_by<
        ordered_unique<
            tag<by_id>, member<account_metadata_object, account_metadata_id_type, &account_metadata_object::id>
        >,
        ordered_unique<
            tag<by_account>, member<account_metadata_object, account_name_type, &account_metadata_object::account>
        >
    >,
    allocator<account_metadata_object>
>;

struct by_last_master_update;

typedef multi_index_container<
        account_authority_object,
        indexed_by<
                ordered_unique<tag<by_id>,
                        member<account_authority_object, account_authority_id_type, &account_authority_object::id>>,
                ordered_unique<tag<by_account>,
                        composite_key < account_authority_object,
                        member<account_authority_object, account_name_type, &account_authority_object::account>,
                        member<account_authority_object, account_authority_id_type, &account_authority_object::id>
                >,
                composite_key_compare <
                std::less<account_name_type>, std::less<account_authority_id_type>>
>,
ordered_unique<tag<by_last_master_update>,
        composite_key < account_authority_object,
        member<account_authority_object, time_point_sec, &account_authority_object::last_master_update>,
        member<account_authority_object, account_authority_id_type, &account_authority_object::id>
>,
composite_key_compare <std::greater<time_point_sec>, std::less<account_authority_id_type>>
>
>,
allocator<account_authority_object>
>
account_authority_index;

struct by_delegation;
struct by_received;

using vesting_delegation_index = multi_index_container<
    vesting_delegation_object,
    indexed_by<
        ordered_unique<
            tag<by_id>,
            member<vesting_delegation_object, vesting_delegation_id_type, &vesting_delegation_object::id>>,
        ordered_unique<
            tag<by_delegation>,
            composite_key<
                vesting_delegation_object,
                member<vesting_delegation_object, account_name_type, &vesting_delegation_object::delegator>,
                member<vesting_delegation_object, account_name_type, &vesting_delegation_object::delegatee>
            >,
            composite_key_compare<protocol::string_less, protocol::string_less>
        >,
        ordered_unique<
            tag<by_received>,
            composite_key<
                vesting_delegation_object,
                member<vesting_delegation_object, account_name_type, &vesting_delegation_object::delegatee>,
                member<vesting_delegation_object, account_name_type, &vesting_delegation_object::delegator>
            >,
            composite_key_compare<protocol::string_less, protocol::string_less>
        >
    >,
    allocator<vesting_delegation_object>
>;

using fix_vesting_delegation_index = multi_index_container<
    fix_vesting_delegation_object,
    indexed_by<
        ordered_unique<
            tag<by_id>,
            member<fix_vesting_delegation_object, fix_vesting_delegation_id_type, &fix_vesting_delegation_object::id>
        >
    >,
    allocator<fix_vesting_delegation_object>
>;

struct by_expiration;
struct by_account_expiration;

using vesting_delegation_expiration_index = multi_index_container<
    vesting_delegation_expiration_object,
    indexed_by<
        ordered_unique<
            tag<by_id>,
            member<vesting_delegation_expiration_object, vesting_delegation_expiration_id_type, &vesting_delegation_expiration_object::id>>,
        ordered_unique<
            tag<by_expiration>,
            composite_key<
                vesting_delegation_expiration_object,
                member<vesting_delegation_expiration_object, time_point_sec, &vesting_delegation_expiration_object::expiration>,
                member<vesting_delegation_expiration_object, vesting_delegation_expiration_id_type, &vesting_delegation_expiration_object::id>
            >,
            composite_key_compare<std::less<time_point_sec>, std::less<vesting_delegation_expiration_id_type>>
        >,
        ordered_unique<
            tag<by_account_expiration>,
            composite_key<
                vesting_delegation_expiration_object,
                member<vesting_delegation_expiration_object, account_name_type, &vesting_delegation_expiration_object::delegator>,
                member<vesting_delegation_expiration_object, time_point_sec, &vesting_delegation_expiration_object::expiration>,
                member<vesting_delegation_expiration_object, vesting_delegation_expiration_id_type, &vesting_delegation_expiration_object::id>
            >,
            composite_key_compare<std::less<account_name_type>, std::less<time_point_sec>, std::less<vesting_delegation_expiration_id_type>>
        >
    >,
    allocator<vesting_delegation_expiration_object>
>;

struct by_expiration;

typedef multi_index_container<
        account_recovery_request_object,
        indexed_by<
                ordered_unique<tag<by_id>,
                        member<account_recovery_request_object, account_recovery_request_id_type, &account_recovery_request_object::id>>,
                ordered_unique<tag<by_account>,
                        composite_key < account_recovery_request_object,
                        member<account_recovery_request_object, account_name_type, &account_recovery_request_object::account_to_recover>,
                        member<account_recovery_request_object, account_recovery_request_id_type, &account_recovery_request_object::id>
                >,
                composite_key_compare <
                std::less<account_name_type>, std::less<account_recovery_request_id_type>>
>,
ordered_unique<tag<by_expiration>,
        composite_key < account_recovery_request_object,
        member<account_recovery_request_object, time_point_sec, &account_recovery_request_object::expires>,
        member<account_recovery_request_object, account_recovery_request_id_type, &account_recovery_request_object::id>
>,
composite_key_compare <std::less<time_point_sec>, std::less<account_recovery_request_id_type>>
>
>,
allocator<account_recovery_request_object>
>
account_recovery_request_index;

struct by_effective_date;

typedef multi_index_container<
        change_recovery_account_request_object,
        indexed_by<
                ordered_unique<tag<by_id>,
                        member<change_recovery_account_request_object, change_recovery_account_request_id_type, &change_recovery_account_request_object::id>>,
                ordered_unique<tag<by_account>,
                        composite_key <
                        change_recovery_account_request_object,
                        member<change_recovery_account_request_object, account_name_type, &change_recovery_account_request_object::account_to_recover>,
                        member<change_recovery_account_request_object, change_recovery_account_request_id_type, &change_recovery_account_request_object::id>
                >,
                composite_key_compare <
                std::less<account_name_type>, std::less<change_recovery_account_request_id_type>>
>,
ordered_unique<tag<by_effective_date>,
        composite_key < change_recovery_account_request_object,
        member<change_recovery_account_request_object, time_point_sec, &change_recovery_account_request_object::effective_on>,
        member<change_recovery_account_request_object, change_recovery_account_request_id_type, &change_recovery_account_request_object::id>
>,
composite_key_compare <std::less<time_point_sec>, std::less<change_recovery_account_request_id_type>>
>
>,
allocator<change_recovery_account_request_object>
>
change_recovery_account_request_index;

} } // graphene::chain


FC_REFLECT((graphene::chain::account_object),
        (id)(name)(memo_key)(proxy)(referrer)(last_account_update)
                (created)
                (recovery_account)(last_account_recovery)
                (subcontent_count)(vote_count)(content_count)(awarded_rshares)
                (custom_sequence)(custom_sequence_block_num)
                (energy)(last_vote_time)
                (balance)(vesting_shares)(delegated_vesting_shares)(received_vesting_shares)
                (vesting_withdraw_rate)(next_vesting_withdrawal)(withdrawn)(to_withdraw)(withdraw_routes)
                (curation_rewards)
                (posting_rewards)
                (proxied_vsf_votes)(witnesses_voted_for)
                (last_root_post)(last_post)
                (average_bandwidth)(lifetime_bandwidth)(last_bandwidth_update)
                (valid)
                (account_seller)(account_offer_price)(account_on_sale)(account_on_sale_start_time)
                (reserved_balance)
                (target_buyer)(account_on_auction)(current_bid)(current_bidder)(current_bidder_key)(last_bid)
                (subaccount_seller)(subaccount_offer_price)(subaccount_on_sale)
)
CHAINBASE_SET_INDEX_TYPE(graphene::chain::account_object, graphene::chain::account_index)

FC_REFLECT((graphene::chain::account_authority_object),
        (id)(account)(master)(active)(regular)(last_master_update)
)
CHAINBASE_SET_INDEX_TYPE(graphene::chain::account_authority_object, graphene::chain::account_authority_index)

FC_REFLECT((graphene::chain::account_metadata_object), (id)(account)(json_metadata))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::account_metadata_object, graphene::chain::account_metadata_index)

FC_REFLECT((graphene::chain::vesting_delegation_object), (id)(delegator)(delegatee)(vesting_shares)(min_delegation_time))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::vesting_delegation_object, graphene::chain::vesting_delegation_index)

FC_REFLECT((graphene::chain::fix_vesting_delegation_object), (id)(delegator)(delegatee)(vesting_shares))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::fix_vesting_delegation_object, graphene::chain::fix_vesting_delegation_index)

FC_REFLECT((graphene::chain::vesting_delegation_expiration_object), (id)(delegator)(vesting_shares)(expiration))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::vesting_delegation_expiration_object, graphene::chain::vesting_delegation_expiration_index)

FC_REFLECT((graphene::chain::master_authority_history_object),
        (id)(account)(previous_master_authority)(last_valid_time)
)
CHAINBASE_SET_INDEX_TYPE(graphene::chain::master_authority_history_object, graphene::chain::master_authority_history_index)

FC_REFLECT((graphene::chain::account_recovery_request_object),
        (id)(account_to_recover)(new_master_authority)(expires)
)
CHAINBASE_SET_INDEX_TYPE(graphene::chain::account_recovery_request_object, graphene::chain::account_recovery_request_index)

FC_REFLECT((graphene::chain::change_recovery_account_request_object),
        (id)(account_to_recover)(recovery_account)(effective_on)
)
CHAINBASE_SET_INDEX_TYPE(graphene::chain::change_recovery_account_request_object, graphene::chain::change_recovery_account_request_index)
