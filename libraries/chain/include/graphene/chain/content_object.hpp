#pragma once

#include <graphene/protocol/authority.hpp>
#include <graphene/protocol/chain_operations.hpp>

#include <graphene/chain/chain_object_types.hpp>
#include <graphene/chain/witness_objects.hpp>

#include <boost/multi_index/composite_key.hpp>


namespace graphene {
    namespace chain {

        namespace bip = boost::interprocess;

        struct strcmp_less {
            bool operator()(const shared_string &a, const shared_string &b) const {
                return less(a.c_str(), b.c_str());
            }

            bool operator()(const shared_string &a, const string &b) const {
                return less(a.c_str(), b.c_str());
            }

            bool operator()(const string &a, const shared_string &b) const {
                return less(a.c_str(), b.c_str());
            }

        private:
            inline bool less(const char *a, const char *b) const {
                return std::strcmp(a, b) < 0;
            }
        };

        class content_type_object
                : public object<content_type_object_type, content_type_object> {
        public:
            content_type_object() = delete;

            template<typename Constructor, typename Allocator>
            content_type_object(Constructor &&c, allocator <Allocator> a)
                    :title(a), body(a), json_metadata(a) {
                c(*this);
            }

            id_type id;

            content_id_type   content;

            shared_string title;
            shared_string body;
            shared_string json_metadata;
        };

        class content_object
                : public object<content_object_type, content_object> {
        public:
            content_object() = delete;

            template<typename Constructor, typename Allocator>
            content_object(Constructor &&c, allocator <Allocator> a)
                    :parent_permlink(a), permlink(a), beneficiaries(a) {
                c(*this);
            }

            id_type id;

            account_name_type parent_author;
            shared_string parent_permlink;
            account_name_type author;
            shared_string permlink;

            time_point_sec last_update;
            time_point_sec created;
            time_point_sec active; ///< the last time this post was "touched" by voting or reply
            time_point_sec last_payout;

            uint16_t depth = 0; ///< used to track max nested depth
            uint32_t children = 0; ///< used to track the total number of children, grandchildren, etc...

            /**
             *  Used to track the total rshares^2 of all children, this is used for indexing purposes. A discussion
             *  that has a nested content of high value should promote the entire discussion so that the content can
             *  be reviewed.
             */
            fc::uint128_t children_rshares;

            /// index on pending_payout for "things happning now... needs moderation"
            /// TRENDING = UNCLAIMED + PENDING
            share_type net_rshares; // reward is proportional to rshares^2, this is the sum of all votes (positive and negative)
            share_type abs_rshares; /// this is used to track the total abs(weight) of votes for the purpose of calculating cashout_time
            share_type vote_rshares; /// Total positive rshares from all votes. Used to calculate delta weights. Needed to handle vote changing and removal.

            time_point_sec cashout_time; /// 24 hours from the weighted average of vote time
            uint64_t total_vote_weight = 0; /// the total weight of voting rewards, used to calculate pro-rata share of curation payouts

            int16_t curation_percent = 0;
            int16_t consensus_curation_percent = 0;

            /** tracks the total payout this content has received over time, measured in VIZ */
            asset payout_value = asset(0, TOKEN_SYMBOL);
            asset shares_payout_value = asset(0, SHARES_SYMBOL);
            asset curator_payout_value = asset(0, SHARES_SYMBOL);
            asset beneficiary_payout_value = asset(0, SHARES_SYMBOL);

            share_type author_rewards = 0;

            int32_t net_votes = 0;

            id_type root_content;

            bip::vector <protocol::beneficiary_route_type, allocator<protocol::beneficiary_route_type>> beneficiaries;
        };


        /**
         * This index maintains the set of voter/content pairs that have been used, voters cannot
         * vote on the same content more than once per payout period.
         */
        class content_vote_object
                : public object<content_vote_object_type, content_vote_object> {
        public:
            template<typename Constructor, typename Allocator>
            content_vote_object(Constructor &&c, allocator <Allocator> a) {
                c(*this);
            }

            id_type id;

            account_id_type voter;
            content_id_type content;
            uint64_t weight = 0; ///< defines the score this vote receives, used by vote payout calc. 0 if a negative vote or changed votes.
            int64_t rshares = 0; ///< The number of rshares this vote is responsible for
            int16_t vote_percent = 0; ///< The percent weight of the vote
            time_point_sec last_update; ///< The time of the last update of the vote
            int8_t num_changes = 0;
        };

        struct by_content_voter;
        struct by_voter_content;
        struct by_content_weight_voter;
        struct by_voter_last_update;
        typedef multi_index_container <
        content_vote_object,
        indexed_by<
                ordered_unique < tag <
                by_id>, member<content_vote_object, content_vote_id_type, &content_vote_object::id>>,
        ordered_unique <tag<by_content_voter>,
        composite_key<content_vote_object,
                member <
                content_vote_object, content_id_type, &content_vote_object::content>,
        member<content_vote_object, account_id_type, &content_vote_object::voter>
        >
        >,
        ordered_unique <tag<by_voter_content>,
        composite_key<content_vote_object,
                member <
                content_vote_object, account_id_type, &content_vote_object::voter>,
        member<content_vote_object, content_id_type, &content_vote_object::content>
        >
        >,
        ordered_unique <tag<by_voter_last_update>,
        composite_key<content_vote_object,
                member <
                content_vote_object, account_id_type, &content_vote_object::voter>,
        member<content_vote_object, time_point_sec, &content_vote_object::last_update>,
        member<content_vote_object, content_id_type, &content_vote_object::content>
        >,
        composite_key_compare <std::less<account_id_type>, std::greater<time_point_sec>, std::less<content_id_type>>
        >,
        ordered_unique <tag<by_content_weight_voter>,
        composite_key<content_vote_object,
                member <
                content_vote_object, content_id_type, &content_vote_object::content>,
        member<content_vote_object, uint64_t, &content_vote_object::weight>,
        member<content_vote_object, account_id_type, &content_vote_object::voter>
        >,
        composite_key_compare <std::less<content_id_type>, std::greater<uint64_t>, std::less<account_id_type>>
        >
        >,
        allocator <content_vote_object>
        >
        content_vote_index;


        struct by_cashout_time; /// cashout_time
        struct by_permlink; /// author, perm
        struct by_root;
        struct by_parent;
        struct by_last_update; /// parent_auth, last_update
        struct by_author_last_update;

        /**
         * @ingroup object_index
         */
        typedef multi_index_container <

            content_object,
            indexed_by<
                /// CONSENUSS INDICIES - used by evaluators
                ordered_unique <
                    tag <by_id>, member<content_object, content_id_type, &content_object::id>>,
                ordered_unique <
                    tag<by_cashout_time>,
                        composite_key<content_object,
                        member <content_object, time_point_sec, &content_object::cashout_time>,
                        member<content_object, content_id_type, &content_object::id>>>,
                ordered_unique <
                    tag<by_permlink>, /// used by consensus to find posts referenced in ops
                        composite_key<content_object,
                        member <content_object, account_name_type, &content_object::author>,
                        member<content_object, shared_string, &content_object::permlink>>,
                    composite_key_compare <std::less<account_name_type>, strcmp_less>>,
                ordered_unique <
                    tag<by_root>,
                        composite_key<content_object,
                        member <content_object, content_id_type, &content_object::root_content>,
                        member<content_object, content_id_type, &content_object::id>>>,
                ordered_unique <
                    tag<by_parent>, /// used by consensus to find posts referenced in ops
                        composite_key<content_object,
                        member <content_object, account_name_type, &content_object::parent_author>,
                        member<content_object, shared_string, &content_object::parent_permlink>,
                        member<content_object, content_id_type, &content_object::id>>,
            composite_key_compare <std::less<account_name_type>, strcmp_less, std::less<content_id_type>> >
        /// NON_CONSENSUS INDICIES - used by APIs
#ifndef IS_LOW_MEM
                ,
                ordered_unique <
                    tag<by_last_update>,
                        composite_key<content_object,
                        member <content_object, account_name_type, &content_object::parent_author>,
                        member<content_object, time_point_sec, &content_object::last_update>,
                        member<content_object, content_id_type, &content_object::id>>,
                    composite_key_compare <std::less<account_name_type>, std::greater<time_point_sec>, std::less<content_id_type>>>,
                ordered_unique <
                    tag<by_author_last_update>,
                        composite_key<content_object,
                        member <content_object, account_name_type, &content_object::author>,
                        member<content_object, time_point_sec, &content_object::last_update>,
                        member<content_object, content_id_type, &content_object::id>>,
                    composite_key_compare <std::less<account_name_type>, std::greater<time_point_sec>, std::less<content_id_type>>>
#endif
            >,
            allocator <content_object>
        >
        content_index;


    struct by_content;

    typedef multi_index_container<
          content_type_object,
          indexed_by<
             ordered_unique< tag< by_id >, member< content_type_object, content_type_id_type, &content_type_object::id > >,
             ordered_unique< tag< by_content >, member< content_type_object, content_id_type, &content_type_object::content > > >,
        allocator< content_type_object >
    > content_type_index;

    }
} // graphene::chain

FC_REFLECT((graphene::chain::content_object),
    (id)(parent_author)(parent_permlink)(author)(permlink)
    (last_update)(created)(active)(last_payout)
    (depth)(children)(children_rshares)
    (net_rshares)(abs_rshares)(vote_rshares)
    (cashout_time)(total_vote_weight)
    (curation_percent)(consensus_curation_percent)
    (payout_value)(shares_payout_value)(curator_payout_value)(beneficiary_payout_value)
    (author_rewards)(net_votes)(root_content)(beneficiaries))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::content_object, graphene::chain::content_index)

FC_REFLECT((graphene::chain::content_type_object),
    (id)(content)(title)(body)(json_metadata))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::content_type_object, graphene::chain::content_type_index)

FC_REFLECT((graphene::chain::content_vote_object),
    (id)(voter)(content)(weight)(rshares)(vote_percent)(last_update)(num_changes))
CHAINBASE_SET_INDEX_TYPE(graphene::chain::content_vote_object, graphene::chain::content_vote_index)
