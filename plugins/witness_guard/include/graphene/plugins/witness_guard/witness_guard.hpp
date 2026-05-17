#pragma once

#include <appbase/application.hpp>
#include <graphene/plugins/chain/plugin.hpp>
#include <graphene/plugins/p2p/p2p_plugin.hpp>

namespace graphene {
namespace plugins {
namespace witness_guard {

class witness_guard_plugin final
        : public appbase::plugin<witness_guard_plugin> {
public:
    APPBASE_PLUGIN_REQUIRES(
        (graphene::plugins::chain::plugin)
        (graphene::plugins::p2p::p2p_plugin)
    )

    constexpr static const char *plugin_name = "witness_guard";

    static const std::string &name() {
        static std::string name = plugin_name;
        return name;
    }

    witness_guard_plugin();
    ~witness_guard_plugin();

    void set_program_options(
        boost::program_options::options_description &command_line_options,
        boost::program_options::options_description &config_file_options
    ) override;

    void plugin_initialize(
        const boost::program_options::variables_map &options
    ) override;

    void plugin_startup()  override;
    void plugin_shutdown() override;

private:
    struct impl;
    std::unique_ptr<impl> pimpl;
};

} // witness_guard
} // plugins
} // graphene