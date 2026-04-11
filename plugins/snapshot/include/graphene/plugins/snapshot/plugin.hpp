#pragma once

#include <string>
#include <boost/program_options.hpp>
#include <appbase/application.hpp>
#include <graphene/plugins/chain/plugin.hpp>
#include <graphene/plugins/json_rpc/utility.hpp>
#include <graphene/plugins/json_rpc/plugin.hpp>
#include <graphene/plugins/snapshot/snapshot_format.hpp>

namespace graphene {
namespace plugins {
namespace snapshot {

using graphene::plugins::json_rpc::msg_pack;

DEFINE_API_ARGS(snapshot_export, msg_pack, snapshot_export_r)
DEFINE_API_ARGS(snapshot_info, msg_pack, snapshot_info_r)
DEFINE_API_ARGS(snapshot_verify, msg_pack, snapshot_verify_r)

class plugin final : public appbase::plugin<plugin> {
public:
    APPBASE_PLUGIN_REQUIRES(
        (chain::plugin)
        (json_rpc::plugin)
    )

    constexpr const static char *plugin_name = "snapshot";

    static const std::string &name() {
        static std::string name = plugin_name;
        return name;
    }

    plugin();

    ~plugin();

    void set_program_options(
        boost::program_options::options_description &cli,
        boost::program_options::options_description &cfg) override;

    void plugin_initialize(const boost::program_options::variables_map &options) override;

    void plugin_startup() override;

    void plugin_shutdown() override;

    DECLARE_API(
        (snapshot_export)
        (snapshot_info)
        (snapshot_verify)
    )

private:
    struct plugin_impl;
    std::unique_ptr<plugin_impl> my;
};

} } } // graphene::plugins::snapshot
