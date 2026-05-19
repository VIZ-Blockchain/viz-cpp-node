#include <string>
#include <unordered_map>

namespace fc {

    std::string name_from_type(const std::string &type_name) {
        auto start = type_name.find_last_of(':') + 1;
        auto end = type_name.find_last_of('_');
        return type_name.substr(start, end - start);
    }

    std::string resolve_operation_name(const std::string &name) {
        static const std::unordered_map<std::string, std::string> aliases = {
            {"witness_update",         "validator_update"},
            {"account_witness_vote",   "account_validator_vote"},
            {"account_witness_proxy",  "account_validator_proxy"},
            {"shutdown_witness",       "shutdown_validator"},
            {"witness_reward",         "validator_reward"},
        };
        auto it = aliases.find(name);
        if (it != aliases.end()) return it->second;
        return name;
    }

} // fc
