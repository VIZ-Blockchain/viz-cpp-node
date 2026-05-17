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

                // Fire-and-forget variant: posts the broadcast to the P2P
                // thread without blocking the caller.  Safe to call from
                // the production timer thread — the caller never waits
                // for slow peer I/O to complete.
                void post_broadcast_block_post_validation(const graphene::protocol::block_id_type block_id,
                    const std::string &witness_account,
                    const graphene::protocol::signature_type &witness_signature);

                void broadcast_transaction(const graphene::protocol::signed_transaction &tx);

                /**
                 * Broadcast chain status (DLT mode, emergency consensus) to all connected
                 * peers.  Called on connection establishment and when chain state changes.
                 */
                void broadcast_chain_status();

                void set_block_production(bool producing_blocks);

                /**
                 * Reset sync from the last irreversible block.
                 * Pops all reversible blocks back to LIB, resets fork_db,
                 * and re-initiates P2P sync. Used for minority fork recovery.
                 *
                 * @param force_emergency If true, bypass the emergency consensus
                 * guard that normally prevents popping blocks during emergency
                 * mode. Should only be set when the caller has already confirmed
                 * the node is on a minority/isolated fork (e.g. DLT emergency
                 * minority fork detector).
                 */
                void resync_from_lib(bool force_emergency = false);

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
                bool is_isolated_peers() const;

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

                /**
                 * Get the timestamp of the last block received from the P2P network
                 * that was successfully applied to the chain. Unlike the generic
                 * "last block received" timer, this is NOT updated for self-produced
                 * blocks (broadcast_block), allowing callers to distinguish between
                 * real network progress and isolated-fork production.
                 * Returns fc::time_point() (epoch) if no network block has been received.
                 */
                fc::time_point get_last_network_block_time() const;

                /**
                 * Returns true when block production should be deferred:
                 * either during a block-processing pause (snapshot creation
                 * holding DB read lock) or while catching up after the
                 * pause ends (draining queued blocks / gap fill).
                 * The witness plugin checks this to avoid write-lock
                 * deadlock or producing on a stale head.
                 */
                bool is_catching_up_after_pause() const;

                /**
                 * Force-clear the catchup-after-pause flag.
                 * Used by the witness watchdog recovery to unblock
                 * production when the flag is stuck due to race
                 * conditions or edge cases.
                 */
                void clear_catchup_flag();

            private:
                std::unique_ptr<detail::p2p_plugin_impl> my;
            };

        }
    }
} // graphene::plugins::p2p
