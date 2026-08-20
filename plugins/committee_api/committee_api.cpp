#include <boost/program_options/options_description.hpp>
#include <graphene/plugins/committee_api/committee_api.hpp>
#include <graphene/chain/index.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/committee_objects.hpp>
#include <graphene/api/committee_api_object.hpp>

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

namespace graphene { namespace plugins { namespace committee_api {

    struct committee_api::impl final {
        impl(): database_(appbase::app().get_plugin<chain::plugin>().db()) {
        }

        ~impl() = default;

        graphene::chain::database& database() {
            return database_;
        }

        graphene::chain::database& database() const {
            return database_;
        }

    private:
        graphene::chain::database& database_;
    };

    void committee_api::plugin_startup() {
        wlog("committee_api plugin: plugin_startup()");
    }

    void committee_api::plugin_shutdown() {
        wlog("committee_api plugin: plugin_shutdown()");
    }

    const std::string& committee_api::name() {
        static const std::string name = "committee_api";
        return name;
    }

    committee_api::committee_api() = default;

    void committee_api::set_program_options(
        boost::program_options::options_description&,
        boost::program_options::options_description& config_file_options
    ) {
    }

    void committee_api::plugin_initialize(const boost::program_options::variables_map& options) {
        pimpl = std::make_unique<impl>();
        JSON_RPC_REGISTER_API(name());
    }

    committee_api::~committee_api() = default;

    DEFINE_API(committee_api, get_committee_request) {
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

    DEFINE_API(committee_api, get_committee_request_votes) {
        CHECK_ARG_MIN_SIZE(1, 1)
        auto request_id = args.args->at(0).as<uint32_t>();
        // q#687 note: the per-vote get_account() walk below is bounded — committee_vote_request_evaluator
        // caps ballots at MAX_COMMITTEE_VOTES_PER_REQUEST (HF14), so this API is O(≤100k) per call.
        // No pagination: the projection must tally every ballot.
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

    DEFINE_API(committee_api, get_committee_requests_list) {
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

} } } // graphene::plugins::committee_api
