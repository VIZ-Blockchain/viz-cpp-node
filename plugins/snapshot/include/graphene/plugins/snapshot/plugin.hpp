#pragma once

#include <appbase/application.hpp>
#include <graphene/plugins/chain/plugin.hpp>
#include <graphene/plugins/snapshot/snapshot_types.hpp>

#include <vector>

namespace graphene { namespace plugins { namespace snapshot {

    namespace bpo = boost::program_options;

    // ========== Snapshot P2P sync protocol ==========

    enum snapshot_net_message_type : uint32_t {
        snapshot_info_request  = 1,
        snapshot_info_reply    = 2,
        snapshot_data_request  = 3,
        snapshot_data_reply    = 4,
        snapshot_not_available = 5,
        snapshot_access_denied = 6
    };

    /// Reason codes for snapshot_access_denied messages
    enum snapshot_deny_reason : uint32_t {
        deny_untrusted        = 1,  // IP not in trusted list
        deny_max_connections  = 2,  // Server at max concurrent connections
        deny_session_limit    = 3,  // Too many active sessions from this IP
        deny_rate_limited     = 4   // Too many connections per hour from this IP
    };

    struct snapshot_access_denied_data {
        uint32_t reason = 0;  // snapshot_deny_reason
    };

    struct snapshot_info_reply_data {
        uint32_t        block_num = 0;
        block_id_type   block_id;
        fc::sha256      checksum;
        uint64_t        compressed_size = 0;
    };

    struct snapshot_data_request_data {
        uint32_t block_num = 0;
        uint64_t offset = 0;
        uint32_t chunk_size = 0;  // requested chunk size (max ~1 MB)
    };

    struct snapshot_data_reply_data {
        uint64_t            offset = 0;
        std::vector<char>   data;
        bool                is_last = false;
    };

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

FC_REFLECT((graphene::plugins::snapshot::snapshot_info_reply_data),
    (block_num)(block_id)(checksum)(compressed_size))

FC_REFLECT((graphene::plugins::snapshot::snapshot_data_request_data),
    (block_num)(offset)(chunk_size))

FC_REFLECT((graphene::plugins::snapshot::snapshot_data_reply_data),
    (offset)(data)(is_last))

FC_REFLECT((graphene::plugins::snapshot::snapshot_access_denied_data),
    (reason))
