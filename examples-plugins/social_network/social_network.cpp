#include <boost/program_options/options_description.hpp>
#include <graphene/plugins/social_network/social_network.hpp>
#include <graphene/chain/index.hpp>
#include <graphene/api/vote_state.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/api/discussion_helper.hpp>
#include <graphene/chain/committee_objects.hpp>
#include <graphene/api/committee_api_object.hpp>
#include <graphene/chain/invite_objects.hpp>
#include <graphene/api/invite_api_object.hpp>

// These visitors creates additional tables, we don't really need them in LOW_MEM mode
#include <graphene/plugins/tags/plugin.hpp>

#define CHECK_ARG_SIZE(_S)                                 \
   FC_ASSERT(                                              \
       args.args->size() == _S,                            \
       "Expected #_S argument(s), was ${n}",               \
       ("n", args.args->size()) );

#define CHECK_ARG_MIN_SIZE(_S, _M)                         \
   FC_ASSERT(                                              \
       args.args->size() >= _S && args.args->size() <= _M, \
       "Expected #_S (maximum #_M) argument(s), was ${n}", \
       ("n", args.args->size()) );

#define GET_OPTIONAL_ARG(_I, _T, _D)   \
   (args.args->size() > _I) ?          \
   (args.args->at(_I).as<_T>()) :      \
   static_cast<_T>(_D)

#ifndef DEFAULT_VOTE_LIMIT
#  define DEFAULT_VOTE_LIMIT 10000
#endif

namespace graphene { namespace plugins { namespace social_network {
    using graphene::api::discussion_helper;

    struct social_network::impl final {
        impl(): database_(appbase::app().get_plugin<chain::plugin>().db()) {
            helper = std::make_unique<discussion_helper>(database_);
        }

        ~impl() = default;

        graphene::chain::database& database() {
            return database_;
        }

        graphene::chain::database& database() const {
            return database_;
        }

        void select_active_votes (
            std::vector<vote_state>& result, uint32_t& total_count,
            const std::string& author, const std::string& permlink, uint32_t limit
        ) const ;

        void select_content_replies(
            std::vector<discussion>& result, std::string author, std::string permlink, uint32_t limit
        ) const;

        std::vector<discussion> get_content_replies(
            const std::string& author, const std::string& permlink, uint32_t vote_limit
        ) const;

        std::vector<discussion> get_all_content_replies(
            const std::string& author, const std::string& permlink, uint32_t vote_limit
        ) const;

        std::vector<discussion> get_replies_by_last_update(
            account_name_type start_parent_author, std::string start_permlink,
            uint32_t limit, uint32_t vote_limit
        ) const;

        discussion get_content(std::string author, std::string permlink, uint32_t limit) const;

        discussion get_discussion(const content_object& c, uint32_t vote_limit) const ;

    private:
        graphene::chain::database& database_;
        std::unique_ptr<discussion_helper> helper;
    };


    discussion social_network::impl::get_discussion(const content_object& c, uint32_t vote_limit) const {
        return helper->get_discussion(c, vote_limit);
    }

    void social_network::impl::select_active_votes(
        std::vector<vote_state>& result, uint32_t& total_count,
        const std::string& author, const std::string& permlink, uint32_t limit
    ) const {
        helper->select_active_votes(result, total_count, author, permlink, limit);
    }

    void social_network::plugin_startup() {
        wlog("social_network plugin: plugin_startup()");
    }

    void social_network::plugin_shutdown() {
        wlog("social_network plugin: plugin_shutdown()");
    }

    const std::string& social_network::name() {
        static const std::string name = "social_network";
        return name;
    }

    social_network::social_network() = default;

    void social_network::set_program_options(
        boost::program_options::options_description&,
        boost::program_options::options_description& config_file_options
    ) {
    }

    void social_network::plugin_initialize(const boost::program_options::variables_map& options) {
        pimpl = std::make_unique<impl>();
        JSON_RPC_REGISTER_API(name());
    }

    social_network::~social_network() = default;

    void social_network::impl::select_content_replies(
        std::vector<discussion>& result, std::string author, std::string permlink, uint32_t limit
    ) const {
        account_name_type acc_name = account_name_type(author);
        const auto& by_permlink_idx = database().get_index<content_index>().indices().get<by_parent>();
        auto itr = by_permlink_idx.find(std::make_tuple(acc_name, permlink));
        while (
            itr != by_permlink_idx.end() &&
            itr->parent_author == author &&
            to_string(itr->parent_permlink) == permlink
        ) {
            result.emplace_back(get_discussion(*itr, limit));
            ++itr;
        }
    }

    std::vector<discussion> social_network::impl::get_content_replies(
        const std::string& author, const std::string& permlink, uint32_t vote_limit
    ) const {
        std::vector<discussion> result;
        select_content_replies(result, author, permlink, vote_limit);
        return result;
    }

    DEFINE_API(social_network, get_content_replies) {
        CHECK_ARG_MIN_SIZE(2, 3)
        auto author = args.args->at(0).as<string>();
        auto permlink = args.args->at(1).as<string>();
        auto vote_limit = GET_OPTIONAL_ARG(2, uint32_t, DEFAULT_VOTE_LIMIT);
        return pimpl->database().with_weak_read_lock([&]() {
            return pimpl->get_content_replies(author, permlink, vote_limit);
        });
    }

    std::vector<discussion> social_network::impl::get_all_content_replies(
        const std::string& author, const std::string& permlink, uint32_t vote_limit
    ) const {
        std::vector<discussion> result;
        select_content_replies(result, author, permlink, vote_limit);
        for (std::size_t i = 0; i < result.size(); ++i) {
            if (result[i].children > 0) {
                auto j = result.size();
                select_content_replies(result, result[i].author, result[i].permlink, vote_limit);
                for (; j < result.size(); ++j) {
                    result[i].replies.push_back(result[j].author + "/" + result[j].permlink);
                }
            }
        }
        return result;
    }

    DEFINE_API(social_network, get_all_content_replies) {
        CHECK_ARG_MIN_SIZE(2, 3)
        auto author = args.args->at(0).as<string>();
        auto permlink = args.args->at(1).as<string>();
        auto vote_limit = GET_OPTIONAL_ARG(2, uint32_t, DEFAULT_VOTE_LIMIT);
        return pimpl->database().with_weak_read_lock([&]() {
            return pimpl->get_all_content_replies(author, permlink, vote_limit);
        });
    }

    DEFINE_API(social_network, get_account_votes) {
        CHECK_ARG_MIN_SIZE(1, 3)
        account_name_type voter = args.args->at(0).as<account_name_type>();
        auto from = GET_OPTIONAL_ARG(1, uint32_t, 0);
        auto limit = GET_OPTIONAL_ARG(2, uint64_t, DEFAULT_VOTE_LIMIT);

        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            std::vector<account_vote> result;

            const auto& voter_acnt = db.get_account(voter);
            const auto& idx = db.get_index<content_vote_index>().indices().get<by_voter_content>();

            account_object::id_type aid(voter_acnt.id);
            auto itr = idx.lower_bound(aid);
            auto end = idx.upper_bound(aid);

            limit += from;
            for (uint32_t i = 0; itr != end && i < limit; ++itr, ++i) {
                if (i < from) {
                    continue;
                }

                const auto& vo = db.get(itr->content);
                account_vote avote;
                avote.authorperm = vo.author + "/" + to_string(vo.permlink);
                avote.weight = itr->weight;
                avote.rshares = itr->rshares;
                avote.percent = itr->vote_percent;
                avote.time = itr->last_update;
                result.emplace_back(avote);
            }
            return result;
        });
    }

    discussion social_network::impl::get_content(std::string author, std::string permlink, uint32_t limit) const {
        const auto& by_permlink_idx = database().get_index<content_index>().indices().get<by_permlink>();
        auto itr = by_permlink_idx.find(std::make_tuple(author, permlink));
        if (itr != by_permlink_idx.end()) {
            return get_discussion(*itr, limit);
        }
        return discussion();
    }

    DEFINE_API(social_network, get_committee_request) {
        CHECK_ARG_MIN_SIZE(1, 2)
        auto request_id = args.args->at(0).as<uint32_t>();
        auto votes_count = GET_OPTIONAL_ARG(1, int32_t, 0);
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto &idx = db.get_index<committee_request_index>().indices().get<by_request_id>();
            auto itr = idx.find(request_id);
            FC_ASSERT(itr != idx.end(), "Committee request id not found.");
            committee_api_object result = committee_api_object(*itr);
            if(0!=votes_count){
                const auto &vote_idx = db.get_index<committee_vote_index>().indices().get<by_request_id>();
                auto vote_itr = vote_idx.lower_bound(request_id);
                int32_t num = 0;
                while (vote_itr != vote_idx.end() &&
                       vote_itr->request_id == request_id) {
                    const auto &cur_vote = *vote_itr;
                    ++vote_itr;
                    committee_vote_state vote=committee_vote_state(cur_vote);
                    result.votes.emplace_back(vote);
                    if(-1!=votes_count){
                        ++num;
                        if(num>=votes_count){
                            vote_itr=vote_idx.end();
                        }
                    }
                }
            }
            return result;
        });
    }

    DEFINE_API(social_network, get_committee_request_votes) {
        CHECK_ARG_MIN_SIZE(1, 1)
        auto request_id = args.args->at(0).as<uint32_t>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto &vote_idx = db.get_index<committee_vote_index>().indices().get<by_request_id>();
            auto vote_itr = vote_idx.lower_bound(request_id);
            std::vector<committee_vote_state> votes;
            while (vote_itr != vote_idx.end() &&
                   vote_itr->request_id == request_id) {
                const auto &cur_vote = *vote_itr;
                ++vote_itr;
                committee_vote_state vote=committee_vote_state(cur_vote);
                votes.emplace_back(vote);
            }
            return votes;
        });
    }

    DEFINE_API(social_network, get_committee_requests_list) {
        CHECK_ARG_MIN_SIZE(1, 1)
        auto status = args.args->at(0).as<uint16_t>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto &idx = db.get_index<committee_request_index>().indices().get<by_status>();
            std::vector<uint16_t> requests_list;
            auto itr = idx.lower_bound(status);
            while (itr != idx.end() &&
                   itr->status == status) {
                requests_list.emplace_back(itr->request_id);
                ++itr;
            }
            return requests_list;
        });
    }

    DEFINE_API(social_network, get_invites_list) {
        CHECK_ARG_MIN_SIZE(1, 1)
        auto status = args.args->at(0).as<uint16_t>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto &idx = db.get_index<invite_index>().indices().get<by_status>();
            std::vector<int64_t> invites_list;
            auto itr = idx.lower_bound(status);
            while (itr != idx.end() &&
                   itr->status == status) {
                invites_list.emplace_back(itr->id._id);
                ++itr;
            }
            return invites_list;
        });
    }

    DEFINE_API(social_network, get_invite_by_id) {
        CHECK_ARG_MIN_SIZE(1, 1)
        auto search_id = args.args->at(0).as<int64_t>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto &idx = db.get_index<invite_index>().indices().get<by_id>();
            auto itr = idx.find(search_id);
            FC_ASSERT(itr != idx.end(), "Invite id not found.");
            invite_api_object result = invite_api_object(*itr);
            return result;
        });
    }

    DEFINE_API(social_network, get_invite_by_key) {
        CHECK_ARG_MIN_SIZE(1, 1)
        auto search_key = args.args->at(0).as<public_key_type>();
        auto& db = pimpl->database();
        return db.with_weak_read_lock([&]() {
            const auto &idx = db.get_index<invite_index>().indices().get<by_invite_key>();
            auto itr = idx.find(search_key);
            FC_ASSERT(itr != idx.end(), "Invite key not found.");
            invite_api_object result = invite_api_object(*itr);
            return result;
        });
    }

    DEFINE_API(social_network, get_content) {
        CHECK_ARG_MIN_SIZE(2, 3)
        auto author = args.args->at(0).as<account_name_type>();
        auto permlink = args.args->at(1).as<string>();
        auto vote_limit = GET_OPTIONAL_ARG(2, uint32_t, DEFAULT_VOTE_LIMIT);
        return pimpl->database().with_weak_read_lock([&]() {
            return pimpl->get_content(author, permlink, vote_limit);
        });
    }

    DEFINE_API(social_network, get_active_votes) {
        CHECK_ARG_MIN_SIZE(2, 3)
        auto author = args.args->at(0).as<string>();
        auto permlink = args.args->at(1).as<string>();
        auto limit = GET_OPTIONAL_ARG(2, uint32_t, DEFAULT_VOTE_LIMIT);
        return pimpl->database().with_weak_read_lock([&]() {
            std::vector<vote_state> result;
            uint32_t total_count;
            pimpl->select_active_votes(result, total_count, author, permlink, limit);
            return result;
        });
    }

    std::vector<discussion> social_network::impl::get_replies_by_last_update(
        account_name_type start_parent_author,
        std::string start_permlink,
        uint32_t limit,
        uint32_t vote_limit
    ) const {
        std::vector<discussion> result;
#ifndef IS_LOW_MEM
        auto& db = database();
        const auto& last_update_idx = db.get_index<content_index>().indices().get<by_last_update>();
        auto itr = last_update_idx.begin();
        const account_name_type* parent_author = &start_parent_author;

        if (start_permlink.size()) {
            const auto& content = db.get_content(start_parent_author, start_permlink);
            itr = last_update_idx.iterator_to(content);
            parent_author = &content.parent_author;
        } else if (start_parent_author.size()) {
            itr = last_update_idx.lower_bound(start_parent_author);
        }

        result.reserve(limit);

        while (itr != last_update_idx.end() && result.size() < limit && itr->parent_author == *parent_author) {
            result.emplace_back(get_discussion(*itr, vote_limit));
            ++itr;
        }
#endif
        return result;
    }

    /**
     *  This method can be used to fetch replies to an account.
     *
     *  The first call should be (account_to_retrieve replies, "", limit)
     *  Subsequent calls should be (last_author, last_permlink, limit)
     */
    DEFINE_API(social_network, get_replies_by_last_update) {
        CHECK_ARG_MIN_SIZE(3, 4)
        auto start_parent_author = args.args->at(0).as<account_name_type>();
        auto start_permlink = args.args->at(1).as<string>();
        auto limit = args.args->at(2).as<uint32_t>();
        auto vote_limit = GET_OPTIONAL_ARG(3, uint32_t, DEFAULT_VOTE_LIMIT);
        FC_ASSERT(limit <= 100);
        return pimpl->database().with_weak_read_lock([&]() {
            return pimpl->get_replies_by_last_update(start_parent_author, start_permlink, limit, vote_limit);
        });
    }

} } } // graphene::plugins::social_network
