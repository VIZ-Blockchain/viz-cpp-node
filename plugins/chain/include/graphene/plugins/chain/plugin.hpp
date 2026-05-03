#pragma once

#include <appbase/application.hpp>

#include <boost/signals2.hpp>
#include <graphene/protocol/types.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/protocol/block.hpp>

#include <graphene/plugins/json_rpc/utility.hpp>
#include <graphene/plugins/json_rpc/plugin.hpp>
// for api
#include <fc/optional.hpp>

namespace graphene {
    namespace plugins {
        namespace chain {

            using graphene::plugins::json_rpc::msg_pack;

            class plugin final : public appbase::plugin<plugin> {
            public:
                APPBASE_PLUGIN_REQUIRES((json_rpc::plugin))

                plugin();

                ~plugin();

                constexpr const static char *plugin_name = "chain";

                static const std::string &name() {
                    static std::string name = plugin_name;
                    return name;
                }

                void set_program_options(boost::program_options::options_description &cli, boost::program_options::options_description &cfg) override;

                void plugin_initialize(const boost::program_options::variables_map &options) override;

                void plugin_startup() override;

                void plugin_shutdown() override;

                bool accept_block(const protocol::signed_block &block, bool currently_syncing = false, uint32_t skip = 0);

                void accept_transaction(const protocol::signed_transaction &trx);

                bool block_is_on_preferred_chain(const protocol::block_id_type &block_id);

                void check_time_in_block(const protocol::signed_block &block);

                /// Called by the snapshot plugin after it has registered its callback.
                /// If chain plugin deferred snapshot loading (callback wasn't set yet during
                /// plugin_startup), this triggers the deferred load.
                void trigger_snapshot_load();

                template<typename MultiIndexType>
                bool has_index() const {
                    return db().has_index<MultiIndexType>();
                }

                template<typename MultiIndexType>
                const chainbase::generic_index<MultiIndexType> &get_index() const {
                    return db().get_index<MultiIndexType>();
                }

                template<typename ObjectType, typename IndexedByType, typename CompatibleKey>
                const ObjectType *find(CompatibleKey &&key) const {
                    return db().find<ObjectType, IndexedByType, CompatibleKey>(key);
                }

                template<typename ObjectType>
                const ObjectType *find(chainbase::object_id<ObjectType> key = chainbase::object_id<ObjectType>()) {
                    return db().find<ObjectType>(key);
                }

                template<typename ObjectType, typename IndexedByType, typename CompatibleKey>
                const ObjectType &get(CompatibleKey &&key) const {
                    return db().get<ObjectType, IndexedByType, CompatibleKey>(key);
                }

                template<typename ObjectType>
                const ObjectType &get(
                        const chainbase::object_id<ObjectType> &key = chainbase::object_id<ObjectType>()) {
                    return db().get<ObjectType>(key);
                }

                // Exposed for backwards compatibility. In the future, plugins should manage their own internal database
                graphene::chain::database &db();

                const graphene::chain::database &db() const;

                /// Returns true when the node is processing P2P sync blocks
                /// (i.e. catching up to the network head).  Plugins that perform
                /// heavy background work (e.g. periodic snapshots) should defer
                /// until this returns false.
                bool is_syncing() const;

                /// Explicitly clear the syncing flag.  Called by the P2P
                /// layer when sync completes (all peers report zero
                /// unfetched items) so that the witness plugin can resume
                /// block production.
                void clear_syncing();

                // Emitted when the blockchain is syncing/live.
                // This is to synchronize plugins that have the chain plugin as an optional dependency.
                boost::signals2::signal<void()> on_sync;

                // Callback for snapshot loading. Set by the snapshot plugin during initialize().
                // Called by the chain plugin during startup() BEFORE on_sync(),
                // so that the snapshot state is loaded before P2P starts syncing.
                std::function<void()> snapshot_load_callback;

                // Callback for snapshot creation. Set by the snapshot plugin during initialize().
                // Called by the chain plugin during startup() AFTER full DB load (including replay),
                // but BEFORE on_sync(), so that the snapshot is created before P2P/witness start.
                std::function<void()> snapshot_create_callback;

                // Callback for P2P snapshot sync. Set by the snapshot plugin during initialize()
                // when --sync-snapshot-from-trusted-peer is enabled. Called by the chain plugin
                // during startup() when state is empty (head_block_num == 0), BEFORE on_sync().
                std::function<void()> snapshot_p2p_sync_callback;

                /// Attempt immediate auto-recovery from shared memory corruption.
                /// Closes database, finds latest snapshot, wipes shared memory,
                /// imports snapshot, replays dlt_block_log, and resumes.
                /// Can be called from any plugin that detects corruption at runtime.
                void attempt_auto_recovery();

                /// Wipe shared memory (not block logs) to recover from a failed
                /// snapshot import. Called by the snapshot plugin when import fails.
                void wipe_state();

            private:
                class plugin_impl;

                std::unique_ptr<plugin_impl> my;

                /// Internal: opens database in snapshot mode, loads via callback, calls on_sync().
                /// @param is_recovery  true for --replay-from-snapshot (includes dlt_block_log replay)
                void do_snapshot_load(const boost::filesystem::path& data_dir, bool is_recovery);
            };
        }
    }
} // graphene::plugins::chain
