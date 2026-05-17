#pragma once

#include <appbase/application.hpp>
#include <graphene/plugins/chain/plugin.hpp>
#include <graphene/api/discussion.hpp>
#include <graphene/plugins/follow/plugin.hpp>
#include <graphene/api/account_vote.hpp>
#include <graphene/api/vote_state.hpp>
#include <graphene/api/committee_api_object.hpp>
#include <graphene/api/invite_api_object.hpp>

namespace graphene { namespace plugins { namespace social_network {
    using plugins::json_rpc::msg_pack;
    using graphene::api::discussion;
    using graphene::api::account_vote;
    using graphene::api::vote_state;
    using graphene::api::content_api_object;
    using graphene::api::committee_api_object;
    using graphene::api::committee_vote_state;
    using graphene::api::invite_api_object;
    using namespace graphene::chain;

    DEFINE_API_ARGS(get_content,                msg_pack, discussion)
    DEFINE_API_ARGS(get_content_replies,        msg_pack, std::vector<discussion>)
    DEFINE_API_ARGS(get_all_content_replies,    msg_pack, std::vector<discussion>)
    DEFINE_API_ARGS(get_account_votes,          msg_pack, std::vector<account_vote>)
    DEFINE_API_ARGS(get_active_votes,           msg_pack, std::vector<vote_state>)
    DEFINE_API_ARGS(get_replies_by_last_update, msg_pack, std::vector<discussion>)
    DEFINE_API_ARGS(get_committee_request,      msg_pack, committee_api_object)
    DEFINE_API_ARGS(get_committee_request_votes,msg_pack, std::vector<committee_vote_state>)
    DEFINE_API_ARGS(get_committee_requests_list,msg_pack, std::vector<uint16_t>)
    DEFINE_API_ARGS(get_invites_list,           msg_pack, std::vector<int64_t>)
    DEFINE_API_ARGS(get_invite_by_id,           msg_pack, invite_api_object)
    DEFINE_API_ARGS(get_invite_by_key,          msg_pack, invite_api_object)

    class social_network final: public appbase::plugin<social_network> {
    public:
        APPBASE_PLUGIN_REQUIRES (
            (chain::plugin)
            (json_rpc::plugin)
        )

        DECLARE_API(
            (get_content)
            (get_content_replies)
            (get_all_content_replies)
            (get_account_votes)
            (get_active_votes)
            (get_replies_by_last_update)
            (get_committee_request)
            (get_committee_request_votes)
            (get_committee_requests_list)
            (get_invites_list)
            (get_invite_by_id)
            (get_invite_by_key)
        )

        social_network();
        ~social_network();

        void set_program_options(
            boost::program_options::options_description&,
            boost::program_options::options_description& config_file_options
        ) override;

        static const std::string& name();

        void plugin_initialize(const boost::program_options::variables_map& options) override;

        void plugin_startup() override;
        void plugin_shutdown() override;

    private:
        struct impl;
        std::unique_ptr<impl> pimpl;
    };
} } } // graphene::plugins::social_network