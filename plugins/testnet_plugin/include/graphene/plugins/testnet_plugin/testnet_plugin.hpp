#pragma once

#include <appbase/application.hpp>
#include <graphene/plugins/chain/plugin.hpp>

#include <boost/program_options.hpp>

namespace graphene {
namespace plugins {
namespace testnet_plugin {

/**
 * TESTNET-ONLY helper: force a hardfork at startup.
 *
 * Adds one startup command, `--testnet-hardfork <version|number>` (for example `4.1.0` or
 * `15`). When it is set, the plugin applies every hardfork up to that one as soon as the
 * chain state is loaded — after the snapshot import, before block production starts.
 *
 * It exists because a chain stuck in emergency consensus mode can never activate a hardfork
 * on its own: the committee holds every schedule slot and is deliberately excluded from the
 * hardfork vote tally, so neither `next_hardfork` nor the quorum can ever move (see HF12).
 * Forcing the fork bypasses the tally entirely and lets a testnet verify the activation and
 * the gated behaviour ahead of the production date.
 *
 * The plugin does nothing unless it is both loaded (`plugin = testnet_plugin` in config.ini
 * or `--plugin testnet_plugin`) and given a target, so shipping it in the production image is
 * inert. It must never be given a target on the production network: it rewrites hardfork
 * state without consensus, and a forced fork cannot be rolled back.
 */
class testnet_plugin final : public appbase::plugin<testnet_plugin> {
public:
    APPBASE_PLUGIN_REQUIRES((graphene::plugins::chain::plugin))

    constexpr static const char *plugin_name = "testnet_plugin";

    static const std::string &name() {
        static std::string name = plugin_name;
        return name;
    }

    testnet_plugin();
    ~testnet_plugin();

    void set_program_options(
        boost::program_options::options_description &command_line_options,
        boost::program_options::options_description &config_file_options
    ) override;

    void plugin_initialize(const boost::program_options::variables_map &options) override;
    void plugin_startup() override;
    void plugin_shutdown() override;

private:
    struct impl;
    std::unique_ptr<impl> pimpl;
};

} // testnet_plugin
} // plugins
} // graphene
