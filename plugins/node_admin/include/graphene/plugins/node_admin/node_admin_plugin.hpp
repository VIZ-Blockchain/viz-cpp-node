#pragma once

#include <appbase/application.hpp>
#include <graphene/plugins/chain/plugin.hpp>

#include <boost/program_options.hpp>

#include <memory>

namespace graphene {
namespace plugins {
namespace node_admin {

/**
 * Administrative API for the node itself.
 *
 * Listens on a SEPARATE HTTP endpoint (loopback by default) and speaks JSON-RPC 2.0 with
 * bearer-token authentication. It is deliberately not part of the public webserver API:
 * that endpoint is proxied by nginx, so every remote request arrives inside the node
 * looking like 127.0.0.1 — a source-IP check there protects nothing.
 *
 * The plugin is inert unless `admin-endpoint` is configured. With an endpoint but without
 * tokens it refuses to start: an unauthenticated admin port is never a valid configuration.
 *
 * v1 is READ-ONLY: node_info, diagnostics, plugins.list, config.get, logs.tail, audit.tail.
 * Mutating methods (config/plugin edits, restart, validator updates) are later stages and
 * will be built on the same listener with plan/apply and an audit trail.
 */
class node_admin_plugin final : public appbase::plugin<node_admin_plugin> {
public:
    APPBASE_PLUGIN_REQUIRES((graphene::plugins::chain::plugin))

    constexpr static const char *plugin_name = "node_admin";

    static const std::string &name() {
        static std::string name = plugin_name;
        return name;
    }

    node_admin_plugin();
    ~node_admin_plugin();

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

} // namespace node_admin
} // namespace plugins
} // namespace graphene
