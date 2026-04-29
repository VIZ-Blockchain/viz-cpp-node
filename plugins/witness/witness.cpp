
#include <graphene/plugins/witness/witness.hpp>

#include <graphene/chain/database_exceptions.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/chain_object_types.hpp>
#include <graphene/chain/witness_objects.hpp>
#include <graphene/chain/hardfork.hpp>
#include <graphene/time/time.hpp>

#include <graphene/utilities/key_conversion.hpp>

#include <fc/smart_ref_impl.hpp>

#include <memory>
#include <thread>

#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

using std::string;
using std::vector;

namespace bpo = boost::program_options;

void new_chain_banner(const graphene::chain::database &db) {
    std::cerr << "\n"
            "********************************\n"
            "*                              *\n"
            "*   ------- NEW CHAIN ------   *\n"
            "*   -    Welcome to VIZ!   -   *\n"
            "*   ------------------------   *\n"
            "*                              *\n"
            "********************************\n"
            "\n";
    return;
}

template<typename T>
T dejsonify(const string &s) {
    return fc::json::from_string(s).as<T>();
}

#define DEFAULT_VALUE_VECTOR(value) default_value({fc::json::to_string(value)}, fc::json::to_string(value))
#define LOAD_VALUE_SET(options, name, container, type) \
            if( options.count(name) ) { \
                  const std::vector<std::string>& ops = options[name].as<std::vector<std::string>>(); \
                  std::transform(ops.begin(), ops.end(), std::inserter(container, container.end()), &dejsonify<type>); \
            }

namespace graphene {
    namespace plugins {
        namespace witness_plugin {

            namespace asio = boost::asio;
            namespace posix_time = boost::posix_time;
            namespace system = boost::system;

            struct witness_plugin::impl final {
                impl():
                    p2p_(appbase::app().get_plugin<graphene::plugins::p2p::p2p_plugin>()),
                    chain_(appbase::app().get_plugin<graphene::plugins::chain::plugin>()),
                    production_timer_(appbase::app().get_io_service()) {
                }

                ~impl(){}

                graphene::chain::database& database() {
                    return chain_.db();
                }

                graphene::chain::database& database() const {
                    return chain_.db();
                }

                graphene::plugins::chain::plugin& chain() {
                    return chain_;
                }

                graphene::plugins::chain::plugin& chain() const {
                    return chain_;
                }

                graphene::plugins::p2p::p2p_plugin& p2p(){
                    return p2p_;
                };

                graphene::plugins::p2p::p2p_plugin& p2p() const {
                    return p2p_;
                };

                graphene::plugins::p2p::p2p_plugin& p2p_;

                graphene::plugins::chain::plugin& chain_;

                void schedule_production_loop();

                block_production_condition::block_production_condition_enum block_production_loop();

                block_production_condition::block_production_condition_enum maybe_produce_block(fc::mutable_variant_object &capture);

                boost::program_options::variables_map _options;
                uint32_t _required_witness_participation = 33 * CHAIN_1_PERCENT;

                std::atomic<uint64_t> head_block_num_;
                block_id_type head_block_id_ = block_id_type();
                std::atomic<uint64_t> total_hashes_;
                fc::time_point hash_start_time_;

                uint32_t _production_skip_flags = graphene::chain::database::skip_nothing;
                bool _production_enabled = false;
                asio::deadline_timer production_timer_;

                std::map<public_key_type, fc::ecc::private_key> _private_keys;
                std::set<string> _witnesses;

                fc::time_point last_block_post_validation_time;

                // Fork collision resolution state
                uint32_t fork_collision_defer_count_ = 0;
                uint32_t _fork_collision_timeout_blocks = 21;  // one full witness round (21 blocks = 63s)
            };

            void witness_plugin::set_program_options(
                    boost::program_options::options_description &command_line_options,
                    boost::program_options::options_description &config_file_options) {
                    string witness_id_example = "initwitness";

                command_line_options.add_options()
                        ("enable-stale-production", bpo::value<bool>()->implicit_value(true) , "Enable block production, even if the chain is stale.")
                        ("required-participation", bpo::value<uint32_t>()->default_value(33 * CHAIN_1_PERCENT), "Percent of witnesses (0-99) that must be participating in order to produce blocks")
                        ("witness,w", bpo::value<vector<string>>()->composing()->multitoken(), ("name of witness controlled by this node (e.g. " + witness_id_example + " )").c_str())
                        ("private-key", bpo::value<vector<string>>()->composing()->multitoken(), "WIF PRIVATE KEY to be used by one or more witnesses")
                        ("emergency-private-key", bpo::value<vector<string>>()->composing()->multitoken(),
                         "WIF PRIVATE KEY for emergency consensus block production. "
                         "Only used when the network enters emergency consensus mode "
                         "(no blocks for >1 hour since last irreversible block). "
                         "Multiple nodes can safely have this key.")
                        ("ntp-server", bpo::value<vector<string>>()->composing()->multitoken(),
                         "NTP server to use for time synchronization (host or host:port). "
                         "Can be specified multiple times. Leave unset to use the built-in defaults "
                         "(pool.ntp.org, time.google.com, time.cloudflare.com).")
                        ("ntp-request-interval", bpo::value<uint32_t>()->default_value(900),
                         "How often to request a time update from NTP servers, in seconds (default: 900 = 15 min).")
                        ("ntp-retry-interval", bpo::value<uint32_t>()->default_value(300),
                         "Retry interval in seconds when NTP has not replied (default: 300 = 5 min).")
                        ("ntp-round-trip-threshold", bpo::value<uint32_t>()->default_value(150),
                         "Round-trip delay threshold in milliseconds; NTP replies slower than this are discarded (default: 150).")
                        ("ntp-history-size", bpo::value<uint32_t>()->default_value(5),
                         "Moving-average history window size for NTP delta smoothing (default: 5).")
                        ("ntp-rejection-threshold-pct", bpo::value<uint32_t>()->default_value(50),
                         "Rejection threshold as a percentage of the absolute moving average; deltas deviating more are rejected (default: 50).")
                        ("ntp-rejection-min-threshold", bpo::value<uint32_t>()->default_value(5),
                         "Minimum rejection threshold in milliseconds, applied regardless of the percentage rule (default: 5).")
                        ("fork-collision-timeout-blocks", bpo::value<uint32_t>()->default_value(21),
                         "Number of consecutive fork-collision deferrals (block slots) before forcing production. "
                         "One full witness schedule round is 21 blocks (63 seconds). Default: 21.")
                        ;

                config_file_options.add(command_line_options);
            }

            using std::vector;
            using std::pair;
            using std::string;

            void witness_plugin::plugin_initialize(const boost::program_options::variables_map &options) {
                try {
                    ilog("witness plugin:  plugin_initialize() begin");
                    pimpl = std::make_unique<witness_plugin::impl>();

                    pimpl->total_hashes_.store(0, std::memory_order_relaxed);
                    pimpl->_options = &options;
                    LOAD_VALUE_SET(options, "witness", pimpl->_witnesses, string)
                    edump((pimpl->_witnesses));

                    if(options.count("enable-stale-production")){
                        pimpl->_production_enabled = options["enable-stale-production"].as<bool>();
                    }

                    if(options.count("required-participation")){
                        pimpl->_required_witness_participation = options["required-participation"].as<uint32_t>();
                    }

                    if (options.count("private-key")) {
                        const std::vector<std::string> keys = options["private-key"].as<std::vector<std::string>>();
                        for (const std::string &wif_key : keys) {
                            fc::optional<fc::ecc::private_key> private_key = graphene::utilities::wif_to_key(wif_key);
                            FC_ASSERT(private_key.valid(), "unable to parse private key");
                            pimpl->_private_keys[private_key->get_public_key()] = *private_key;
                        }
                    }

                    if (options.count("emergency-private-key")) {
                        const std::vector<std::string> keys = options["emergency-private-key"].as<std::vector<std::string>>();
                        for (const std::string &wif_key : keys) {
                            fc::optional<fc::ecc::private_key> private_key = graphene::utilities::wif_to_key(wif_key);
                            FC_ASSERT(private_key.valid(), "unable to parse emergency private key");
                            pimpl->_private_keys[private_key->get_public_key()] = *private_key;
                        }
                        // Add the committee account to our witness set so we produce blocks
                        // when the schedule assigns committee slots during emergency mode
                        pimpl->_witnesses.insert(CHAIN_EMERGENCY_WITNESS_ACCOUNT);
                        ilog("Emergency private key loaded. Will produce blocks during emergency consensus mode.");
                    }

                    // Build and store NTP configuration so it is applied when the NTP service starts.
                    {
                        graphene::time::ntp_config ntp_cfg;
                        if (options.count("ntp-server"))
                            ntp_cfg.servers = options["ntp-server"].as<std::vector<std::string>>();
                        ntp_cfg.request_interval_sec   = options["ntp-request-interval"].as<uint32_t>();
                        ntp_cfg.retry_interval_sec     = options["ntp-retry-interval"].as<uint32_t>();
                        ntp_cfg.round_trip_threshold_ms = options["ntp-round-trip-threshold"].as<uint32_t>();
                        ntp_cfg.history_size            = options["ntp-history-size"].as<uint32_t>();
                        ntp_cfg.rejection_threshold_pct = options["ntp-rejection-threshold-pct"].as<uint32_t>();
                        ntp_cfg.rejection_min_threshold_ms = options["ntp-rejection-min-threshold"].as<uint32_t>();
                        graphene::time::configure_ntp(ntp_cfg);
                    }

                    if (options.count("fork-collision-timeout-blocks")) {
                        pimpl->_fork_collision_timeout_blocks = options["fork-collision-timeout-blocks"].as<uint32_t>();
                    }

                    ilog("witness plugin:  plugin_initialize() end");
                } FC_LOG_AND_RETHROW()
            }

            void witness_plugin::plugin_startup() {
                try {
                    ilog("witness plugin:  plugin_startup() begin");
                    auto &d = pimpl->database();
                    //Start NTP time client
                    graphene::time::now();

                    if (!pimpl->_witnesses.empty()) {
                        ilog("Launching block production for ${n} witnesses.", ("n", pimpl->_witnesses.size()));
                        pimpl->p2p().set_block_production(true);
                        if (pimpl->_production_enabled) {
                            if (d.head_block_num() == 0) {
                                new_chain_banner(d);
                            }
                            pimpl->_production_skip_flags |= graphene::chain::database::skip_undo_history_check;
                        }
                        pimpl->schedule_production_loop();
                    } else
                        elog("No witnesses configured! Please add witness names and private keys to configuration.");
                    ilog("witness plugin:  plugin_startup() end");
                } FC_CAPTURE_AND_RETHROW()
            }

            void witness_plugin::plugin_shutdown() {
                graphene::time::shutdown_ntp_time();
                if (!pimpl->_witnesses.empty()) {
                    ilog("shutting downing production timer");
                    pimpl->production_timer_.cancel();
                }
            }

            witness_plugin::witness_plugin() {}

            witness_plugin::~witness_plugin() {}

            bool witness_plugin::is_witness_scheduled_soon() const {
                try {
                    if (!pimpl || pimpl->_witnesses.empty() || pimpl->_private_keys.empty()) {
                        return false;
                    }

                    auto& db = pimpl->database();
                    auto op_guard = db.make_operation_guard();
                    fc::time_point now_fine = graphene::time::now();
                    fc::time_point_sec now = now_fine + fc::microseconds(250000);

                    uint32_t slot = db.get_slot_at_time(now);
                    if (slot == 0) {
                        slot = 1;
                    }

                    // Check 4 upcoming slots (~12 seconds) to cover snapshot creation time (~10s) + safety margin
                    for (uint32_t s = slot; s <= slot + 3; ++s) {
                        string scheduled_witness = db.get_scheduled_witness(s);
                        if (pimpl->_witnesses.find(scheduled_witness) == pimpl->_witnesses.end()) {
                            continue;
                        }

                        const auto& witness_by_name = db.get_index<graphene::chain::witness_index>().indices().get<graphene::chain::by_name>();
                        auto itr = witness_by_name.find(scheduled_witness);
                        if (itr == witness_by_name.end()) {
                            continue;
                        }

                        graphene::protocol::public_key_type scheduled_key = itr->signing_key;
                        if (scheduled_key == graphene::protocol::public_key_type()) {
                            continue; // Disabled witness (zero key)
                        }

                        if (pimpl->_private_keys.find(scheduled_key) != pimpl->_private_keys.end()) {
                            op_guard.release();
                            return true; // We have the private key and are scheduled soon
                        }
                    }
                } catch (const fc::exception& e) {
                    wlog("is_witness_scheduled_soon check failed: ${e}", ("e", e.to_detail_string()));
                } catch (...) {
                    wlog("is_witness_scheduled_soon check failed with unknown exception");
                }
                return false;
            }

            void witness_plugin::impl::schedule_production_loop() {
                //Schedule for the next 250ms tick regardless of chain state
                // With +250ms look-ahead in maybe_produce_block(), the tick at
                // T_slot - 250ms aligns now exactly to the slot boundary for zero-lag production.
                // If we would wait less than 50ms, wait for the whole 250ms period.
                int64_t ntp_microseconds = graphene::time::now().time_since_epoch().count();
                int64_t next_microseconds = 250000 - ( ntp_microseconds % 250000 );
                if (next_microseconds < 50000) { // we must sleep for at least 50ms
                    next_microseconds += 250000 ;
                }

                production_timer_.expires_from_now( posix_time::microseconds(next_microseconds) );
                production_timer_.async_wait( [this](const system::error_code &) { block_production_loop(); } );
            }

            block_production_condition::block_production_condition_enum witness_plugin::impl::block_production_loop() {
                block_production_condition::block_production_condition_enum result;
                fc::mutable_variant_object capture;
                try {
                    result = maybe_produce_block(capture);
                }
                catch (const fc::canceled_exception &) {
                    //We're trying to exit. Go ahead and let this one out.
                    throw;
                }
                catch (const graphene::chain::unknown_hardfork_exception &e) {
                    // Hit a hardfork that the current node know nothing about, stop production and inform user
                    elog("${e}\nNode may be out of date...", ("e", e.to_detail_string()));
                    throw;
                }
                catch (const fc::exception &e) {
                    elog("Got exception while generating block:\n${e}", ("e", e.to_detail_string()));
                    result = block_production_condition::exception_producing_block;
                }

                switch (result) {
                    case block_production_condition::produced:
                        ilog("\033[92mGenerated block #${n} with timestamp ${t} at time ${c} by ${w}\033[0m", (capture));
                        fork_collision_defer_count_ = 0;
                        break;
                    case block_production_condition::not_synced:
                        // This log-record is commented, because it outputs very often
                        // ilog("Not producing block because production is disabled until we receive a recent block (see: --enable-stale-production)");
                        fork_collision_defer_count_ = 0;
                        break;
                    case block_production_condition::not_my_turn:
                        // This log-record is commented, because it outputs very often
                        // ilog("Not producing block because it isn't my turn");
                        fork_collision_defer_count_ = 0;
                        break;
                    case block_production_condition::not_time_yet:
                        // This log-record is commented, because it outputs very often
                        // ilog("Not producing block because slot has not yet arrived");
                        break;
                    case block_production_condition::no_private_key:
                        ilog("Not producing block for ${scheduled_witness} because I don't have the private key for ${scheduled_key}",
                             (capture));
                        break;
                    case block_production_condition::low_participation:
                        elog("Not producing block because node appears to be on a minority fork with only ${pct}% witness participation",
                             (capture));
                        break;
                    case block_production_condition::lag:
                        elog("Not producing block because node didn't wake up within 500ms of the slot time.");
                        graphene::time::update_ntp_time();  // Force NTP sync on timing issues
                        break;
                    case block_production_condition::consecutive:
                        elog("Not producing block because the last block was generated by the same witness.\nThis node is probably disconnected from the network so block production has been disabled.\nDisable this check with --allow-consecutive option.");
                        break;
                    case block_production_condition::exception_producing_block:
                        elog("Failure when producing block with no transactions");
                        break;
                    case block_production_condition::fork_collision:
                        wlog("Deferred block production due to fork collision; will retry next slot");
                        graphene::time::update_ntp_time();  // Force NTP sync on fork issues
                        break;
                    case block_production_condition::minority_fork:
                        elog("Not producing block: minority fork detected, resyncing from P2P network");
                        break;
                }

                schedule_production_loop();
                return result;
            }

            block_production_condition::block_production_condition_enum witness_plugin::impl::maybe_produce_block(fc::mutable_variant_object &capture) {
                auto &db = database();
                fc::time_point now_fine = graphene::time::now();
                fc::time_point_sec now = now_fine + fc::microseconds( 250000 );

                // === HARDFORK 12: THREE-STATE SAFETY ENFORCEMENT ===
                const auto &dgp = db.get_dynamic_global_properties();

                if (db.has_hardfork(CHAIN_HARDFORK_12)) {
                    if (dgp.emergency_consensus_active) {
                        // EMERGENCY MODE: auto-bypass both stale and participation checks.
                        // The consensus layer has determined emergency mode is needed.
                        _production_enabled = true;
                    } else {
                        uint32_t prate = db.witness_participation_rate();
                        if (prate >= 33 * CHAIN_1_PERCENT) {
                            // HEALTHY NETWORK: enforce safe defaults automatically.
                            // Even if operator has enable-stale-production=true in config,
                            // it's overridden because the network doesn't need it.
                            // Clear the stale-production skip flag so that minority fork
                            // detection is re-enabled now that the network is healthy.
                            _production_skip_flags &= ~graphene::chain::database::skip_undo_history_check;
                            if (!_production_enabled) {
                                if (db.get_slot_time(1) >= now) {
                                    _production_enabled = true;
                                } else {
                                    return block_production_condition::not_synced;
                                }
                            }
                            // Participation is already >= 33%, no need to check again
                        } else {
                            // DISTRESSED NETWORK (participation < 33%, not yet emergency):
                            // Honor manual config overrides -- operator may be trying to
                            // accelerate recovery before the 1-hour timeout.
                            if (!_production_enabled) {
                                if (_production_skip_flags & graphene::chain::database::skip_undo_history_check) {
                                    // enable-stale-production=true -> skip sync check
                                    _production_enabled = true;
                                } else if (db.get_slot_time(1) >= now) {
                                    _production_enabled = true;
                                } else {
                                    return block_production_condition::not_synced;
                                }
                            }
                            if (prate < _required_witness_participation) {
                                if (_production_skip_flags & graphene::chain::database::skip_undo_history_check) {
                                    // enable-stale-production=true: operator override, produce anyway
                                    // to bootstrap/recover a fully stalled network where all nodes
                                    // see low participation and would otherwise deadlock.
                                    dlog("Witness participation is ${p}% but stale-production is enabled, "
                                         "producing anyway to recover stalled network",
                                         ("p", uint32_t(prate / CHAIN_1_PERCENT)));
                                } else {
                                    capture("pct", uint32_t(prate / CHAIN_1_PERCENT));
                                    return block_production_condition::low_participation;
                                }
                            }
                        }
                    }
                } else {
                    // Pre-hardfork 12: use legacy behavior with config-based overrides
                    if (!_production_enabled) {
                        if (db.get_slot_time(1) >= now) {
                            _production_enabled = true;
                        } else {
                            return block_production_condition::not_synced;
                        }
                    }
                }

                //try get block post validation list for each witness
                //if witness can validate it, sign chain_id and block_id for message
                //broadcast validation message by p2p plugin
                if(last_block_post_validation_time < now_fine ){
                    last_block_post_validation_time = now;
                    //ilog("! tick last_block_post_validation_time");
                    //get block post validation for each witness we have
                    for (auto &witness_account : _witnesses) {
                        bool ignore_witness = false;
                        auto block_post_validations = db.get_block_post_validations(witness_account);
                        if (block_post_validations.size() > 0) {
                            const auto &witness_by_name = db.get_index<graphene::chain::witness_index>().indices().get<graphene::chain::by_name>();
                            auto w_itr = witness_by_name.find(witness_account);
                            if (w_itr == witness_by_name.end()) {
                                wlog("Witness ${w} not found in witness index, skipping block post validation", ("w", witness_account));
                                continue;
                            }
                            graphene::protocol::public_key_type witness_pub_key = w_itr->signing_key;

                            // Skip witnesses with zero/null signing key (intentionally disabled)
                            if (witness_pub_key == graphene::protocol::public_key_type()) {
                                ignore_witness = true;
                            }

                            auto private_key_itr = _private_keys.find(witness_pub_key);

                            if (!ignore_witness && private_key_itr == _private_keys.end()) {
                                ilog("No private key to public ${p} for ${w}", ("p", witness_pub_key)("w", witness_account));
                                ignore_witness = true;
                            }
                            if(!ignore_witness){
                                graphene::protocol::private_key_type witness_priv_key = private_key_itr->second;
                                //we have block post validations for this witness
                                //check if we have a block
                                for(uint8_t i = 0; i < block_post_validations.size(); i++) {
                                    if(0 != block_post_validations[i].block_num){
                                        if(block_post_validations[i].block_id != block_id_type()){
                                            graphene::protocol::digest_type::encoder enc;
                                            fc::raw::pack(enc, db.get_chain_id().str().append(block_post_validations[i].block_id.str()));
                                            //sign the enc by witness_priv_key
                                            graphene::protocol::signature_type bpv_signature = witness_priv_key.sign_compact(enc.result());
                                            //ilog("Witness ${w} signed block post validation #${n} ${b} with signature ${s}", ("w", witness_account)("n", block_post_validations[i].block_num)("b", block_post_validations[i].block_id)("s", bpv_signature));
                                            p2p().broadcast_block_post_validation(block_post_validations[i].block_id, witness_account, bpv_signature);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // === MINORITY FORK DETECTION ===
                // If the last CHAIN_MAX_WITNESSES (21) blocks in fork_db were ALL
                // produced by our own configured witnesses, we are likely stuck on
                // a minority fork where no external witnesses are participating.
                //
                // SKIP during emergency consensus: in emergency mode all blocks are
                // produced by the committee account (which is in _witnesses), so the
                // check would always falsely trigger and kill recovery.
                //
                // With enable-stale-production=true: operator knows what they're doing,
                //   continue producing (bootstrap / testnet / recovery scenario).
                // With enable-stale-production=false (default): we're on the wrong fork,
                //   pop back to LIB and resync from the P2P network.
                if (!dgp.emergency_consensus_active) {
                    auto fork_head = db.get_fork_db().head();
                    if (fork_head) {
                        bool all_ours = true;
                        uint32_t blocks_checked = 0;
                        auto current = fork_head;

                        while (current && blocks_checked < CHAIN_MAX_WITNESSES) {
                            if (_witnesses.find(current->data.witness) == _witnesses.end()) {
                                all_ours = false;
                                break;
                            }
                            blocks_checked++;
                            current = current->prev.lock();
                        }

                        if (all_ours && blocks_checked >= CHAIN_MAX_WITNESSES) {
                            if (_production_skip_flags & graphene::chain::database::skip_undo_history_check) {
                                // enable-stale-production=true: operator override, continue
                                dlog("Minority fork detected (last ${n} blocks from our witnesses) "
                                     "but stale production enabled, continuing",
                                     ("n", blocks_checked));
                            } else {
                                // Wrong fork: trigger recovery
                                elog("MINORITY FORK DETECTED: last ${n} blocks all from our witnesses. "
                                     "Resetting to LIB and resyncing from P2P network.",
                                     ("n", blocks_checked));
                                p2p().resync_from_lib();
                                _production_enabled = false;
                                return block_production_condition::minority_fork;
                            }
                        }
                    }
                }

                // Guard lockless reads into shared memory with the resize barrier.
                // This prevents a concurrent shared memory resize from invalidating
                // pointers while we read witness schedule, slot time, etc.
                // The guard is released before generate_block() which has its own.
                auto op_guard = db.make_operation_guard();

                // is anyone scheduled to produce now or one second in the future?
                uint32_t slot = db.get_slot_at_time(now);
                if (slot == 0) {
                    capture("next_time", db.get_slot_time(1));
                    return block_production_condition::not_time_yet;
                }

                //
                // this assert should not fail, because now <= db.head_block_time()
                // should have resulted in slot == 0.
                //
                // if this assert triggers, there is a serious bug in get_slot_at_time()
                // which would result in allowing a later block to have a timestamp
                // less than or equal to the previous block
                //
                assert(now > db.head_block_time());

                string scheduled_witness = db.get_scheduled_witness(slot);
                // we must control the witness scheduled to produce the next block.
                if (_witnesses.find(scheduled_witness) == _witnesses.end()) {
                    capture("scheduled_witness", scheduled_witness);
                    return block_production_condition::not_my_turn;
                }

                const auto &witness_by_name = db.get_index<graphene::chain::witness_index>().indices().get<graphene::chain::by_name>();
                auto itr = witness_by_name.find(scheduled_witness);

                fc::time_point_sec scheduled_time = db.get_slot_time(slot);
                graphene::protocol::public_key_type scheduled_key = itr->signing_key;

                // Check if witness has zero/null signing key (intentionally disabled for block production)
                if (scheduled_key == graphene::protocol::public_key_type()) {
                    // Don't log - witness is configured but has zero key on chain (monitoring only)
                    return block_production_condition::not_my_turn;
                }

                auto private_key_itr = _private_keys.find(scheduled_key);

                if (private_key_itr == _private_keys.end()) {
                    capture("scheduled_witness", scheduled_witness);
                    capture("scheduled_key", scheduled_key);
                    return block_production_condition::no_private_key;
                }

                // Pre-HF12 participation check (legacy behavior)
                if (!db.has_hardfork(CHAIN_HARDFORK_12)) {
                    uint32_t prate = db.witness_participation_rate();
                    if (prate < _required_witness_participation) {
                        if (_production_skip_flags & graphene::chain::database::skip_undo_history_check) {
                            dlog("Witness participation is ${p}% but stale-production is enabled, "
                                 "producing anyway to recover stalled network",
                                 ("p", uint32_t(prate / CHAIN_1_PERCENT)));
                        } else {
                            capture("pct", uint32_t(prate / CHAIN_1_PERCENT));
                            return block_production_condition::low_participation;
                        }
                    }
                }

                if (llabs((scheduled_time - now).count()) > fc::milliseconds(500).count()) {
                    capture("scheduled_time", scheduled_time)("now", now);
                    return block_production_condition::lag;
                }

                // Check if a competing block already exists in the fork database for this block height.
                // Two-level fork collision resolution:
                //   Level 1: Vote-weighted comparison when both forks are in fork_db
                //   Level 2: Stuck-head timeout after one full witness round (21 blocks = 63s)
                {
                    auto existing_blocks = db.get_fork_db().fetch_block_by_number(db.head_block_num() + 1);
                    if (existing_blocks.size() > 0) {
                        bool has_competing_block = false;
                        graphene::chain::item_ptr competing_block;

                        if (dgp.emergency_consensus_active) {
                            // During emergency mode: ANY block at this height is competing.
                            // Multiple nodes with the emergency key may have produced.
                            // Defer to the deterministic hash-based resolution in fork_db.
                            has_competing_block = true;
                            competing_block = existing_blocks[0];
                        } else {
                            // Normal mode: only count blocks from different witnesses
                            // on a different parent as competing
                            for (const auto &eb : existing_blocks) {
                                if (eb->data.witness != scheduled_witness &&
                                    eb->data.previous != db.head_block_id()) {
                                    has_competing_block = true;
                                    competing_block = eb;
                                    break;
                                }
                            }
                        }

                        if (has_competing_block && competing_block) {
                            fork_collision_defer_count_++;

                            // LEVEL 2: Stuck-head timeout
                            // If we've been deferring and the head hasn't advanced, the competing
                            // block is from a dead fork. The network has moved on without it.
                            // After 21 consecutive deferrals (one full witness round = 63s),
                            // we can be sure the longer chain had all scheduled witnesses
                            // produce on it — confirming it's the canonical chain.
                            // This applies regardless of hardfork version — even pre-HF12
                            // nodes must not defer forever.
                            if (fork_collision_defer_count_ > _fork_collision_timeout_blocks) {
                                wlog("Fork collision timeout exceeded (${n} deferrals, head stuck at ${h}). "
                                     "Removing dead-fork competing block and producing on our chain.",
                                     ("n", fork_collision_defer_count_)("h", db.head_block_num()));
                                db.get_fork_db().remove_blocks_by_number(db.head_block_num() + 1);
                                fork_collision_defer_count_ = 0;
                                // Fall through to produce block
                            }
                            // LEVEL 1: Vote-weighted comparison (when both forks are in fork_db)
                            else if (db.has_hardfork(CHAIN_HARDFORK_12)) {
                                int weight_cmp = db.compare_fork_branches(
                                    competing_block->id, db.head_block_id());

                                if (weight_cmp < 0) {
                                    // Our fork has MORE vote weight -> produce on our fork
                                    wlog("Our fork has more vote weight at height ${h}. "
                                         "Producing despite competing block from weaker fork.",
                                         ("h", db.head_block_num() + 1));
                                    // Remove the losing competing block
                                    db.get_fork_db().remove(competing_block->id);
                                    fork_collision_defer_count_ = 0;
                                    // Fall through to produce block
                                } else if (weight_cmp > 0) {
                                    // Competing fork has MORE vote weight
                                    // Defer to let the fork switch happen naturally via _push_block.
                                    capture("height", db.head_block_num() + 1)("scheduled_witness", scheduled_witness);
                                    wlog("Competing fork at height ${h} has more vote weight. "
                                         "Deferring to allow fork switch to stronger chain.",
                                         ("h", db.head_block_num() + 1));
                                    return block_production_condition::fork_collision;
                                } else {
                                    // Tied or comparison impossible (one tip not in fork_db)
                                    // Defer briefly, timeout will kick in
                                    capture("height", db.head_block_num() + 1)("scheduled_witness", scheduled_witness);
                                    wlog("Fork collision at height ${h} with tied/unknown vote weight. "
                                         "Deferring (attempt ${n}/${max}).",
                                         ("h", db.head_block_num() + 1)
                                         ("n", fork_collision_defer_count_)
                                         ("max", _fork_collision_timeout_blocks));
                                    return block_production_condition::fork_collision;
                                }
                            }
                            // Pre-HF12: defer, but timeout still applies on next iteration
                            else {
                                capture("height", db.head_block_num() + 1)("scheduled_witness", scheduled_witness);
                                wlog("Fork collision at height ${h} (pre-HF12). "
                                     "Deferring (attempt ${n}/${max}).",
                                     ("h", db.head_block_num() + 1)
                                     ("n", fork_collision_defer_count_)
                                     ("max", _fork_collision_timeout_blocks));
                                return block_production_condition::fork_collision;
                            }
                        }
                    }
                }

                // Release the operation guard before generate_block(), which
                // acquires its own guard internally via apply_pending_resize()
                // and with_strong_write_lock().
                op_guard.release();

                int retry = 0;
                do {
                    try {
                        // TODO: the same thread as used in chain-plugin,
                        //       but in the future it should refactored to calling of a chain-plugin function
                        auto block = db.generate_block(
                                scheduled_time,
                                scheduled_witness,
                                private_key_itr->second,
                                _production_skip_flags
                        );
                        capture("n", block.block_num())("t", block.timestamp)("c", now)("w", scheduled_witness);
                        p2p().broadcast_block(block);

                        return block_production_condition::produced;
                    }
                    catch (const graphene::chain::shared_memory_corruption_exception& e) {
                        elog("Shared memory corruption detected during block generation: ${e}", ("e", e.to_detail_string()));
                        chain().attempt_auto_recovery();
                        return block_production_condition::exception_producing_block;
                    }
                    catch (fc::exception &e) {
                        elog("${e}", ("e", e.to_detail_string()));
                        elog("Clearing pending transactions and attempting again");
                        db.clear_pending();
                        retry++;
                    }
                } while (retry < 2);

                return block_production_condition::exception_producing_block;
            }
        }
    }
}
