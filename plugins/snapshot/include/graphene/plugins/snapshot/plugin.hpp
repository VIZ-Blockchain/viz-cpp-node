#pragma once

#include <appbase/application.hpp>
#include <graphene/plugins/chain/plugin.hpp>
#include <graphene/plugins/snapshot/snapshot_types.hpp>

namespace graphene { namespace plugins { namespace snapshot {

    namespace bpo = boost::program_options;

    class snapshot_plugin final : public appbase::plugin<snapshot_plugin> {
    public:
        APPBASE_PLUGIN_REQUIRES((chain::plugin))

        snapshot_plugin();
        ~snapshot_plugin();

        constexpr static const char* plugin_name = "snapshot";

        static const std::string& name() {
            static std::string name = plugin_name;
            return name;
        }

        void set_program_options(
            bpo::options_description& cli,
            bpo::options_description& cfg) override;

        void plugin_initialize(const bpo::variables_map& options) override;
        void plugin_startup() override;
        void plugin_shutdown() override;

        /// Returns the snapshot load path if --snapshot was specified, empty string otherwise
        std::string get_snapshot_path() const;

        /// Load state from snapshot file into the database
        void load_snapshot_from(const std::string& path);

        /// Create snapshot at the given path
        void create_snapshot_at(const std::string& path);

    private:
        class plugin_impl;
        std::unique_ptr<plugin_impl> my;
    };

} } } // graphene::plugins::snapshot
