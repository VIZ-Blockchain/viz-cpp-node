#pragma once

#include <appbase/application.hpp>
#include <graphene/plugins/chain/plugin.hpp>
#include <graphene/plugins/p2p/p2p_plugin.hpp>

namespace graphene {
namespace plugins {
namespace validator_guard {

// Read-only view of one configured validator, as seen by the guard.
struct validator_status {
    std::string name;
    bool on_chain = false;          // validator object exists in the chain database
    bool enabled = false;           // on-chain signing key is not null
    bool hold = false;              // operator disabled it: auto-restore is suppressed
    bool active_authority = false;  // the guard's active key is in the account's active authority
    int32_t scheduled_in = -1;      // first of the next 3 slots this validator produces, -1 if none
    std::string signing_key;        // on-chain signing key, empty when null/absent
    std::string configured_key;     // public half of the guard's own signing key
};

enum class validator_action { enable, disable, set_key };

struct validator_action_request {
    std::string name;
    validator_action action = validator_action::enable;
    std::string signing_key;    // set_key: explicit public key (base58)
    // apply re-checks the chain state against the snapshot the plan was built from:
    // "" expects a null signing key, otherwise the exact public key string.
    bool check_expected = false;
    std::string expected_key;
};

struct validator_action_result {
    std::string name;
    std::string action;
    bool ok = false;
    bool changed = false;       // false when the chain already was in the desired state
    std::string tx_id;
    std::string error;
};

class validator_guard_plugin final
        : public appbase::plugin<validator_guard_plugin> {
public:
    APPBASE_PLUGIN_REQUIRES(
        (graphene::plugins::chain::plugin)
        (graphene::plugins::p2p::p2p_plugin)
    )

    constexpr static const char *plugin_name = "validator_guard";

    static const std::string &name() {
        static std::string name = plugin_name;
        return name;
    }

    validator_guard_plugin();
    ~validator_guard_plugin();

    void set_program_options(
        boost::program_options::options_description &command_line_options,
        boost::program_options::options_description &config_file_options
    ) override;

    void plugin_initialize(
        const boost::program_options::variables_map &options
    ) override;

    void plugin_startup()  override;
    void plugin_shutdown() override;

    // True when this plugin holds an active key for at least one validator and has
    // finished startup: the precondition node_admin calls "live guard with an active key".
    bool chain_actions_available() const;

    // State of every configured validator. Safe to call from any thread.
    std::vector<validator_status> validators_status() const;

    // Enable/disable validators or point them at another signing key. Transactions are
    // signed with the guard's active keys, which never leave this plugin. Each request
    // carries the expected previous state so a plan applied after the chain moved is
    // refused instead of silently overriding somebody else's update.
    std::vector<validator_action_result> apply_validator_actions(
        const std::vector<validator_action_request> &requests);

private:
    struct impl;
    std::unique_ptr<impl> pimpl;
};

} // validator_guard
} // plugins
} // graphene