#pragma once

#include <graphene/plugins/chain/plugin.hpp>

#include <appbase/application.hpp>

#define P2P_PLUGIN_NAME "p2p"

namespace graphene {
    namespace plugins {
        namespace p2p {
            namespace bpo = boost::program_options;

            namespace detail {
                class p2p_plugin_impl;
            }

            class p2p_plugin final : public appbase::plugin<p2p_plugin> {
            public:
                APPBASE_PLUGIN_REQUIRES((chain::plugin))

                p2p_plugin();

                ~p2p_plugin();

                void set_program_options(boost::program_options::options_description &,
                                         boost::program_options::options_description &config_file_options) override;

                static const std::string &name() {
                    static std::string name = P2P_PLUGIN_NAME;
                    return name;
                }

                void plugin_initialize(const boost::program_options::variables_map &options) override;

                void plugin_startup() override;

                void plugin_shutdown() override;

                void broadcast_block(const graphene::protocol::signed_block &block);

                void broadcast_block_post_validation(const graphene::protocol::block_id_type block_id,
                    const std::string &witness_account,
                    const graphene::protocol::signature_type &witness_signature);

                void broadcast_transaction(const graphene::protocol::signed_transaction &tx);

                void set_block_production(bool producing_blocks);

                /**
                 * Reset sync from the last irreversible block.
                 * Pops all reversible blocks back to LIB, resets fork_db,
                 * and re-initiates P2P sync. Used for minority fork recovery.
                 */
                void resync_from_lib();

                /**
                 * Re-initiate P2P sync from the current head block.
                 * Does NOT pop any blocks — just tells the P2P layer that
                 * our chain state has changed and peers should be re-synced.
                 * Used after snapshot hot-reload to resume block fetching.
                 */
                void trigger_resync();

                /**
                 * Get the number of currently active P2P connections.
                 */
                uint32_t get_connections_count() const;

                /**
                 * Force-reconnect all configured seed nodes.
                 * Bypasses exponential backoff by resetting connection attempt timers.
                 * Used when the witness plugin detects it has few/no peers after producing a block.
                 */
                void reconnect_seeds();

                /**
                 * Pause block processing. While paused, incoming blocks are
                 * rejected with a transient exception so the P2P layer re-queues
                 * them without penalising the peer. Used during snapshot hot-reload
                 * to prevent concurrent database modifications.
                 */
                void pause_block_processing();

                /**
                 * Resume block processing after a pause.
                 */
                void resume_block_processing();

            private:
                std::unique_ptr<detail::p2p_plugin_impl> my;
            };

        }
    }
} // graphene::plugins::p2p
