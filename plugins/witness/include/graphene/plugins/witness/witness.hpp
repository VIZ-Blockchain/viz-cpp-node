#pragma once


#include <graphene/chain/database.hpp>
#include <boost/program_options/variables_map.hpp>
#include <appbase/application.hpp>
#include <graphene/plugins/chain/plugin.hpp>
#include <graphene/plugins/p2p/p2p_plugin.hpp>
#include <memory>

namespace graphene {
    namespace plugins {
        namespace witness_plugin {

            using std::string;
            using protocol::public_key_type;
            using graphene::protocol::block_id_type;
            using graphene::chain::signed_block;

            namespace block_production_condition {
                enum block_production_condition_enum {
                    produced = 0,
                    not_synced = 1,
                    not_my_turn = 2,
                    not_time_yet = 3,
                    no_private_key = 4,
                    low_participation = 5,
                    lag = 6,
                    consecutive = 7,
                    exception_producing_block = 8,
                    fork_collision = 9,
                    minority_fork = 10
                };
            }

            class witness_plugin final : public appbase::plugin<witness_plugin> {
            public:
                APPBASE_PLUGIN_REQUIRES((chain::plugin) (p2p::p2p_plugin))

                constexpr static const char *plugin_name = "witness";

                static const std::string &name() {
                    static std::string name = plugin_name;
                    return name;
                }

                witness_plugin();

                ~witness_plugin();


                void set_program_options(boost::program_options::options_description &command_line_options,
                                         boost::program_options::options_description &config_file_options) override;

                void set_block_production(bool allow);

                void plugin_initialize(const boost::program_options::variables_map &options) override;

                void plugin_startup() override;

                void plugin_shutdown() override;

                /// Returns true if a locally-controlled witness is scheduled to produce in the next slot
                bool is_witness_scheduled_soon() const;

                /// Returns true if this node is the emergency master: holds the
                /// emergency-private-key (committee is in _witnesses) AND committee
                /// appears in the current witness schedule.  Only the master should
                /// produce blocks solo during emergency consensus; all other nodes
                /// are followers that must sync from the network.
                bool is_emergency_master() const;

                /// Returns true if the emergency-private-key is configured,
                /// regardless of whether the committee is in the current schedule.
                bool is_emergency_key_configured() const;

            private:
                struct impl;
                std::unique_ptr<impl> pimpl;

            };

        }
    }
} //graphene::witness_plugin
