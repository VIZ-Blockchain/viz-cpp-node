#include <graphene/plugins/validator_api/plugin.hpp>
#include <graphene/chain/operation_notification.hpp>


#define CHECK_ARG_SIZE(s) \
   FC_ASSERT( args.args->size() == s, "Expected #s argument(s), was ${n}", ("n", args.args->size()) );

namespace graphene { namespace plugins { namespace validator_api {

using namespace graphene::protocol;
using namespace graphene::chain;

struct plugin::validator_plugin_impl {
public:
    validator_plugin_impl() : database(appbase::app().get_plugin<chain::plugin>().db()) {
    }

    ~validator_plugin_impl() = default;

    std::vector<optional<validator_api_object>> get_witnesses(const std::vector<validator_object::id_type> &witness_ids) const;
    fc::optional<validator_api_object> get_witness_by_account(std::string account_name) const;
    std::vector<validator_api_object> get_witnesses_by_vote(std::string from, uint32_t limit) const;
    std::vector<validator_api_object> get_witnesses_by_counted_vote(std::string from, uint32_t limit) const;
    uint64_t get_witness_count() const;
    std::set<account_name_type> lookup_witness_accounts(const std::string &lower_bound_name, uint32_t limit) const;

    graphene::chain::database& database;
};

DEFINE_API(plugin, get_active_witnesses) {
    return my->database.with_weak_read_lock([&]() {
        const auto &wso = my->database.get_validator_schedule_object();
        size_t n = wso.current_shuffled_validators.size();
        vector<account_name_type> result;
        result.reserve(n);
        for (size_t i = 0; i < n; i++) {
            if (wso.current_shuffled_validators[i] != "") {
                result.push_back(wso.current_shuffled_validators[i]);
            }
        }
        return result;
    });
}

DEFINE_API(plugin, get_witness_schedule) {
    return my->database.with_weak_read_lock([&]() {
        return my->database.get(validator_schedule_object::id_type());
    });
}

std::vector<optional<validator_api_object>> plugin::validator_plugin_impl::get_witnesses(
    const std::vector<validator_object::id_type> &witness_ids
) const {
    std::vector<optional<validator_api_object>> result;
    result.reserve(witness_ids.size());
    std::transform(
        witness_ids.begin(), witness_ids.end(), std::back_inserter(result),
        [&](validator_object::id_type id) -> optional<validator_api_object> {
           if (auto o = database.find(id)) {
               return validator_api_object(*o, database);
           }
           return {};
        });
    return result;
}

DEFINE_API(plugin, get_witnesses) {
    CHECK_ARG_SIZE(1)
    auto witness_ids = args.args->at(0).as<vector<validator_object::id_type> >();
    return my->database.with_weak_read_lock([&]() {
        return my->get_witnesses(witness_ids);
    });
}

DEFINE_API(plugin, get_witness_by_account) {
    CHECK_ARG_SIZE(1)
    auto account_name = args.args->at(0).as<std::string>();
    return my->database.with_weak_read_lock([&]() {
        return my->get_witness_by_account(account_name);
    });
}


fc::optional<validator_api_object> plugin::validator_plugin_impl::get_witness_by_account(std::string account_name) const {
    const auto& idx = database.get_index<validator_index>().indices().get<by_name>();
    auto itr = idx.find(account_name);
    if (itr != idx.end()) {
        return validator_api_object(*itr, database);
    }
    return {};
}

DEFINE_API(plugin, get_witnesses_by_vote) {
    CHECK_ARG_SIZE(2)
    auto from = args.args->at(0).as<std::string>();
    auto limit = args.args->at(1).as<uint32_t>();
    return my->database.with_weak_read_lock([&]() {
        return my->get_witnesses_by_vote(from, limit);
    });
}

std::vector<validator_api_object> plugin::validator_plugin_impl::get_witnesses_by_vote(
        std::string from, uint32_t limit
) const {
    FC_ASSERT(limit <= 100);

    std::vector<validator_api_object> result;
    result.reserve(limit);

    const auto &name_idx = database.get_index<validator_index>().indices().get<by_name>();
    const auto &vote_idx = database.get_index<validator_index>().indices().get<by_vote_name>();

    auto itr = vote_idx.begin();
    if (from.size()) {
        auto nameitr = name_idx.find(from);
        FC_ASSERT(nameitr != name_idx.end(), "invalid validator name ${n}", ("n", from));
        itr = vote_idx.iterator_to(*nameitr);
    }

    while (itr != vote_idx.end() && result.size() < limit && itr->votes > 0) {
        result.emplace_back(*itr, database);
        ++itr;
    }
    return result;
}

DEFINE_API(plugin, get_witnesses_by_counted_vote) {
    CHECK_ARG_SIZE(2)
    auto from = args.args->at(0).as<std::string>();
    auto limit = args.args->at(1).as<uint32_t>();
    return my->database.with_weak_read_lock([&]() {
        return my->get_witnesses_by_counted_vote(from, limit);
    });
}

std::vector<validator_api_object> plugin::validator_plugin_impl::get_witnesses_by_counted_vote(
        std::string from, uint32_t limit
) const {
    FC_ASSERT(limit <= 100);

    std::vector<validator_api_object> result;
    result.reserve(limit);

    const auto &name_idx = database.get_index<validator_index>().indices().get<by_name>();
    const auto &vote_idx = database.get_index<validator_index>().indices().get<by_counted_vote_name>();

    auto itr = vote_idx.begin();
    if (from.size()) {
        auto nameitr = name_idx.find(from);
        FC_ASSERT(nameitr != name_idx.end(), "invalid validator name ${n}", ("n", from));
        itr = vote_idx.iterator_to(*nameitr);
    }

    while (itr != vote_idx.end() && result.size() < limit && itr->counted_votes > 0) {
        result.emplace_back(*itr, database);
        ++itr;
    }
    return result;
}

DEFINE_API(plugin, get_witness_count) {
    return my->database.with_weak_read_lock([&]() {
        return my->get_witness_count();
    });
}

uint64_t plugin::validator_plugin_impl::get_witness_count() const {
    return database.get_index<validator_index>().indices().size();
}

DEFINE_API(plugin, lookup_witness_accounts) {
    CHECK_ARG_SIZE(2)
    auto lower_bound_name = args.args->at(0).as<std::string>();
    auto limit = args.args->at(1).as<uint32_t>();
    return my->database.with_weak_read_lock([&]() {
        return my->lookup_witness_accounts(lower_bound_name, limit);
    });
}

std::set<account_name_type> plugin::validator_plugin_impl::lookup_witness_accounts(
    const std::string &lower_bound_name,
    uint32_t limit
) const {
    FC_ASSERT(limit <= 1000);
    const auto &witnesses_by_id = database.get_index<validator_index>().indices().get<by_id>();

    // get all the names and look them all up, sort them, then figure out what
    // records to return.  This could be optimized, but we expect the
    // number of witnesses to be few and the frequency of calls to be rare
    std::set<account_name_type> witnesses_by_account_name;
    for (const auto& witness : witnesses_by_id) {
        if (witness.owner >= lower_bound_name) { // we can ignore anything below lower_bound_name
            witnesses_by_account_name.insert(witness.owner);
        }
    }

    auto end_iter = witnesses_by_account_name.begin();
    while (end_iter != witnesses_by_account_name.end() && limit--) {
        ++end_iter;
    }
    witnesses_by_account_name.erase(end_iter, witnesses_by_account_name.end());
    return witnesses_by_account_name;
}

// Preferred-name aliases — forward to the legacy implementations above
DEFINE_API(plugin, get_active_validators)      { return get_active_witnesses(args); }
DEFINE_API(plugin, get_validator_schedule)     { return get_witness_schedule(args); }
DEFINE_API(plugin, get_validators)             { return get_witnesses(args); }
DEFINE_API(plugin, get_validator_by_account)   { return get_witness_by_account(args); }
DEFINE_API(plugin, get_validators_by_vote)     { return get_witnesses_by_vote(args); }
DEFINE_API(plugin, get_validators_by_counted_vote) { return get_witnesses_by_counted_vote(args); }
DEFINE_API(plugin, get_validator_count)        { return get_witness_count(args); }
DEFINE_API(plugin, lookup_validator_accounts)  { return lookup_witness_accounts(args); }

void plugin::set_program_options(
    boost::program_options::options_description &cli,
    boost::program_options::options_description &cfg
) {
}

void plugin::plugin_initialize(const boost::program_options::variables_map &options) {
    ilog("validator_api plugin: plugin_initialize() begin");

    try {
        my = std::make_unique<validator_plugin_impl>();

        JSON_RPC_REGISTER_API(name());
    } FC_CAPTURE_AND_RETHROW()

    ilog("validator_api plugin: plugin_initialize() end");
}

plugin::plugin() = default;

plugin::~plugin() = default;

void plugin::plugin_startup() {
    ilog("validator_api plugin: plugin_startup() begin");

    ilog("validator_api plugin: plugin_startup() end");
}


} } } // graphene::plugins::validator_api
