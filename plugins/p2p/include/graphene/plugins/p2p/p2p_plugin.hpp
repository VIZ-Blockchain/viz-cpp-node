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

            private:
                std::unique_ptr<detail::p2p_plugin_impl> my;
            };

        }
    }
} // graphene::plugins::p2p
