
#include <graphene/plugins/witness/witness.hpp>
#include <graphene/plugins/snapshot/plugin.hpp>

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

using namespace graphene::chain;
using namespace graphene::protocol;

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
                    snapshot_(appbase::app().get_plugin<graphene::plugins::snapshot::snapshot_plugin>()),
                    production_timer_(production_io_service_) {
                    // Keep production_io_service_ alive while the thread runs,
                    // then start the dedicated thread.  This io_service is intentionally
                    // separate from appbase's shared io_service so that P2P network
                    // activity (peer disconnects, send-queue drains) cannot delay the
                    // production timer callback and cause missed-slot lag.
                    production_io_work_ = std::make_unique<asio::io_service::work>(production_io_service_);
                    production_io_thread_ = std::thread([this]() { production_io_service_.run(); });
                }

                ~impl() {
                    production_io_service_.stop();
                    if (production_io_thread_.joinable())
                        production_io_thread_.join();
                }

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

                graphene::plugins::snapshot::snapshot_plugin& snapshot() {
                    return snapshot_;
                }

                graphene::plugins::snapshot::snapshot_plugin& snapshot() const {
                    return snapshot_;
                }

                graphene::plugins::p2p::p2p_plugin& p2p_;

                graphene::plugins::chain::plugin& chain_;

                graphene::plugins::snapshot::snapshot_plugin& snapshot_;

                void schedule_production_loop();

                block_validation_condition::block_validation_condition_enum block_validation_loop();

                block_validation_condition::block_validation_condition_enum maybe_validate_block(fc::mutable_variant_object &capture);

                boost::program_options::variables_map _options;
                uint32_t _required_witness_participation = 33 * CHAIN_1_PERCENT;

                std::atomic<uint64_t> head_block_num_;
                block_id_type head_block_id_ = block_id_type();
                std::atomic<uint64_t> total_hashes_;
                fc::time_point hash_start_time_;

                uint32_t _production_skip_flags = graphene::chain::database::skip_nothing;

                // Dedicated io_service for the production timer — must be declared
                // before production_timer_ so it is initialized first.
                asio::io_service production_io_service_;
                std::unique_ptr<asio::io_service::work> production_io_work_;
                std::thread production_io_thread_;
                asio::deadline_timer production_timer_;

                std::map<public_key_type, fc::ecc::private_key> _private_keys;
                std::set<string> _witnesses;

                fc::time_point last_block_post_validation_time;

                // Fork collision resolution state
                uint32_t fork_collision_defer_count_ = 0;
                uint32_t _fork_collision_timeout_blocks = 21;  // one full witness round (21 blocks = 63s)

                // Minority fork recovery state: tracks when we rolled back to
                // LIB and are waiting for P2P sync to catch up before
                // re-enabling block production.
                bool _minority_fork_recovering = false;
                fc::time_point _minority_fork_recovery_start;

                // P18: slot=0 stall detection — tracks consecutive
                // not_time_yet returns to detect NTP/clock issues.
                uint32_t _slot_zero_streak = 0;
                fc::time_point _slot_zero_streak_start;

                // Track consecutive not_my_turn results to detect when our witness
                // is in the schedule but slots keep belonging to other witnesses.
                // Helps diagnose "silent miss" issues where the witness should have
                // produced but didn't (schedule misalignment, timing issues, etc).
                uint32_t _not_my_turn_streak = 0;
                fc::time_point _not_my_turn_streak_start;
                std::string _last_scheduled_witness; // last witness that got the slot

                // Production watchdog: tracks when we last produced a block
                // so the watchdog can fire if the emergency master goes silent.
                bool _ever_produced = false;
                fc::time_point _last_production_time;
                // Last result from a slot > 0 iteration (not_time_yet filtered out so
                // the watchdog shows a meaningful failure code, not between-slot noise).
                int _last_slot_result = -1;

                // Tracks the scheduled_time of the last lag condition.  Used to skip
                // ahead past a missed slot and avoid spinning in a tight lag loop
                // where every 250ms tick rechecks the same missed slot.
                fc::time_point_sec _last_lag_slot_time;

                // Watchdog debug mode: set to true when the watchdog first fires,
                // enabling verbose DEBUG_CRASH logs automatically to help diagnose
                // why production stopped. Reset to false when production resumes
                // so verbose logging does not persist indefinitely.
                bool _watchdog_debug_enabled = false;

                // Remember the user's --debug-block-production config so the
                // watchdog can restore the original value after auto-enabling
                // debug logging for diagnosis.
                bool _debug_block_production_config = false;

                // Slot hijack detection: counts consecutive blocks where committee
                // filled a slot that was assigned to our witness in the shuffled
                // schedule.  In DLT emergency mode the emergency master may blank
                // our signing key and produce committee blocks in our slots; this
                // counter makes the condition visible in watchdog diagnostics.
                uint32_t _slot_hijack_count = 0;
                uint32_t _slot_hijack_height = 0; // last block height where hijack was detected

                // Track last applied block number to detect missed blocks.
                // Updated in the applied_block signal handler.
                uint64_t _last_applied_block_num = 0;

                // applied_block signal handler: detects when incoming blocks
                // reveal missed slots, and if our witness was scheduled for
                // any of them, dumps full plugin state for diagnosis.
                void on_block_applied(const graphene::chain::signed_block &block);
            };

            void witness_plugin::set_program_options(
                    boost::program_options::options_description &command_line_options,
                    boost::program_options::options_description &config_file_options) {
                    string witness_id_example = "initwitness";

                command_line_options.add_options()
                        ("enable-stale-production", bpo::value<bool>()->implicit_value(true) , "Enable block production, even if the chain is stale.")
                        ("required-participation", bpo::value<uint32_t>()->default_value(33 * CHAIN_1_PERCENT), "Percent of validators (0-99) that must be participating in order to produce blocks")
                        ("validator,v", bpo::value<vector<string>>()->composing()->multitoken(), ("name of validator controlled by this node (e.g. " + witness_id_example + " )").c_str())
                        ("witness,w", bpo::value<vector<string>>()->composing()->multitoken(), "[DEPRECATED] Use --validator. Name of validator controlled by this node.")
                        ("private-key", bpo::value<vector<string>>()->composing()->multitoken(), "WIF PRIVATE KEY to be used by one or more validators")
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
                        ("debug-block-production", bpo::value<bool>()->default_value(false),
                         "Enable verbose debug logging for block production and chain internals. Default: false.")
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
                    LOAD_VALUE_SET(options, "validator", pimpl->_witnesses, string)
                    if (options.count("witness")) {
                        // Deprecated: --witness is kept for config.ini backward compatibility.
                        wlog("Config option 'witness' is deprecated, use 'validator' instead.");
                        LOAD_VALUE_SET(options, "witness", pimpl->_witnesses, string)
                    }
                    edump((pimpl->_witnesses));

                    if(options.count("enable-stale-production")){
                        if (options["enable-stale-production"].as<bool>()) {
                            pimpl->_production_skip_flags |= graphene::chain::database::skip_undo_history_check;
                        }
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

                    if (options.count("debug-block-production")) {
                        pimpl->_debug_block_production_config = options["debug-block-production"].as<bool>();
                        pimpl->chain().db()._debug_block_production = pimpl->_debug_block_production_config;
                        if (pimpl->chain().db()._debug_block_production) {
                            ilog("Debug block production logging ENABLED");
                        }
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

                    // Force NTP sync before first production tick to minimize the
                    // window where get_slot_at_time() returns 0 due to unsynchronized
                    // NTP on restart.
                    graphene::time::update_ntp_time();

                    // Log witness configuration for post-crash diagnostics
                    ilog("Witness config: ${n} witnesses, ${k} private keys",
                         ("n", pimpl->_witnesses.size())("k", pimpl->_private_keys.size()));
                    for (const auto& w : pimpl->_witnesses) {
                        ilog("  configured witness: ${w}", ("w", w));
                    }

                    if (!pimpl->_witnesses.empty()) {
                        ilog("Launching block production for ${n} witnesses.", ("n", pimpl->_witnesses.size()));
                        pimpl->p2p().set_block_production(true);

                        // Connect to applied_block signal to detect missed slots
                        // that belong to our witnesses and log diagnostic state.
                        pimpl->_last_applied_block_num = d.head_block_num();
                        d.applied_block.connect([this](const graphene::chain::signed_block &block) {
                            pimpl->on_block_applied(block);
                        });

                        if (pimpl->_production_skip_flags & graphene::chain::database::skip_undo_history_check) {
                            if (d.head_block_num() == 0) {
                                new_chain_banner(d);
                            }
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
                    // Stop the dedicated io_service so the production thread exits.
                    // io_service::stop() is thread-safe; it causes run() to return
                    // after the currently executing handler (if any) completes.
                    pimpl->production_io_service_.stop();
                    if (pimpl->production_io_thread_.joinable())
                        pimpl->production_io_thread_.join();
                }
            }

            void witness_plugin::plugin_for_each_dependency(plugin_processor&& l) {
                l(appbase::app().register_plugin<graphene::plugins::chain::plugin>());
                l(appbase::app().register_plugin<graphene::plugins::p2p::p2p_plugin>());
                l(appbase::app().register_plugin<graphene::plugins::snapshot::snapshot_plugin>());
            }

            witness_plugin::witness_plugin() {}

            witness_plugin::~witness_plugin() {}

            bool witness_plugin::is_validator_scheduled_soon() const {
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
                    wlog("is_validator_scheduled_soon check failed: ${e}", ("e", e.to_detail_string()));
                } catch (...) {
                    wlog("is_validator_scheduled_soon check failed with unknown exception");
                }
                return false;
            }

            bool witness_plugin::is_emergency_master() const {
                try {
                    if (!pimpl || pimpl->_witnesses.empty()) {
                        return false;
                    }

                    // Condition 1: we hold the emergency-private-key.
                    // CHAIN_EMERGENCY_WITNESS_ACCOUNT is added to _witnesses only
                    // when --emergency-private-key is configured (see plugin_initialize).
                    if (pimpl->_witnesses.find(CHAIN_EMERGENCY_WITNESS_ACCOUNT) == pimpl->_witnesses.end()) {
                        return false;
                    }

                    // Condition 2: the committee account is in the current witness schedule.
                    auto& db = pimpl->database();
                    return db.with_weak_read_lock([&]() -> bool {
                        const witness_schedule_object& wso = db.get_witness_schedule_object();
                        for (int i = 0; i < wso.num_scheduled_witnesses; i += CHAIN_BLOCK_WITNESS_REPEAT) {
                            if (wso.current_shuffled_witnesses[i] == CHAIN_EMERGENCY_WITNESS_ACCOUNT) {
                                return true;
                            }
                        }
                        return false;
                    });
                } catch (const fc::exception& e) {
                    wlog("is_emergency_master check failed: ${e}", ("e", e.to_detail_string()));
                } catch (...) {
                    wlog("is_emergency_master check failed with unknown exception");
                }
                return false;
            }

            bool witness_plugin::is_emergency_key_configured() const {
                try {
                    if (!pimpl || pimpl->_witnesses.empty()) {
                        return false;
                    }
                    // CHAIN_EMERGENCY_WITNESS_ACCOUNT is added to _witnesses only
                    // when --emergency-private-key is configured (see plugin_initialize).
                    return pimpl->_witnesses.find(CHAIN_EMERGENCY_WITNESS_ACCOUNT) != pimpl->_witnesses.end();
                } catch (...) {
                    return false;
                }
            }

            std::string witness_plugin::get_production_diagnostics() const {
                try {
                    if (!pimpl) return "witness=no_pimpl";
                    std::string s = "prod_skip_flags=";
                    s += std::to_string(pimpl->_production_skip_flags);
                    s += " catching_up=";
                    try { s += pimpl->p2p().is_catching_up_after_pause() ? "1" : "0"; } catch (...) { s += "?"; }
                    try { s += " head=#" + std::to_string(pimpl->database().head_block_num()); } catch (...) {}
                    if (pimpl->_ever_produced) {
                        auto ago = (fc::time_point::now() - pimpl->_last_production_time).count() / 1000000;
                        s += " last_prod=" + std::to_string(ago) + "s_ago";
                    } else {
                        s += " last_prod=never";
                    }
                    s += " minority_rcv=";
                    s += pimpl->_minority_fork_recovering ? "1" : "0";
                    s += " slot_hijacks=";
                    s += std::to_string(pimpl->_slot_hijack_count);
                    return "witness[" + s + "]";
                } catch (...) {
                    return "witness=err";
                }
            }

            void witness_plugin::impl::on_block_applied(const graphene::chain::signed_block &block) {
                try {
                    uint64_t block_num = block.block_num();
                    uint64_t prev_num = _last_applied_block_num;
                    _last_applied_block_num = block_num;

                    // === SLOT-HIJACK DETECTION (runs for every block) ===
                    // In DLT emergency mode, the emergency master may blank our witness's
                    // signing_key and fill our scheduled slots with committee blocks.  This
                    // detection runs for every incoming block and checks whether the slot
                    // that was just filled was assigned to our witness but produced by someone
                    // else (committee / emergency account).  The counter is included in
                    // watchdog diagnostics so the operator can see the hijack pattern.
                    const auto& dgp_hijack = database().get_dynamic_global_properties();
                    if (database()._dlt_mode && !_witnesses.empty()
                        && prev_num > 0 && block_num == prev_num + 1
                        && dgp_hijack.emergency_consensus_active) {
                        const auto& wso_sj = database().get_witness_schedule_object();
                        uint32_t nsw_sj = wso_sj.num_scheduled_witnesses;
                        if (nsw_sj > 0) {
                            // apply_block increments current_aslot to the applied block's slot
                            // before this callback fires, so current_aslot IS the applied slot.
                            uint64_t slot_idx = dgp_hijack.current_aslot % nsw_sj;
                            const std::string& expected_witness =
                                wso_sj.current_shuffled_witnesses[slot_idx];
                            bool was_our_slot = _witnesses.count(expected_witness) > 0;
                            // True hijack only if the actual producer is NOT also one of our witnesses.
                            bool producer_is_ours = _witnesses.count(block.witness) > 0;

                            if (was_our_slot && !producer_is_ours && block.witness != expected_witness) {
                                // External witness (committee / emergency) produced at our slot.
                                _slot_hijack_count++;
                                _slot_hijack_height = static_cast<uint32_t>(block_num);
                                // Log the first 3 hijacks, then once per minute.
                                static fc::time_point _last_hijack_log;
                                auto _now_sj = fc::time_point::now();
                                if (_slot_hijack_count <= 3 ||
                                    (_now_sj - _last_hijack_log).count() > 60000000) {
                                    _last_hijack_log = _now_sj;
                                    elog("SLOT-HIJACK: block #${bn} by '${wit}' but slot was assigned "
                                         "to our witness '${exp}' (hijack #${cnt}). "
                                         "head=#${head} aslot=${aslot} num_sched=${nsched}",
                                         ("bn", block_num)("wit", block.witness)("exp", expected_witness)
                                         ("cnt", _slot_hijack_count)
                                         ("head", dgp_hijack.head_block_number)
                                         ("aslot", (uint64_t)dgp_hijack.current_aslot)
                                         ("nsched", nsw_sj));
                                }
                            } else if (was_our_slot && (block.witness == expected_witness || producer_is_ours)) {
                                // Our witness (expected or another of ours) produced — reset hijack counter.
                                if (_slot_hijack_count > 0) {
                                    wlog("SLOT-HIJACK-RESOLVED: our witness '${wit}' produced "
                                         "block #${bn} after ${cnt} hijacked slot(s).",
                                         ("wit", block.witness)("bn", block_num)("cnt", _slot_hijack_count));
                                }
                                _slot_hijack_count = 0;
                            }
                        }
                    }

                    // No gap, first block, or emergency mode not active — nothing to check
                    if (prev_num == 0 || block_num <= prev_num + 1) {
                        return;
                    }

                    uint32_t missed_count = static_cast<uint32_t>(block_num - prev_num - 1);

                    // By the time applied_block fires, current_aslot has already been
                    // updated: new_aslot = old_aslot + missed_count + 1.
                    // The missed slots had absolute slot indices:
                    //   old_aslot + 1, old_aslot + 2, ..., old_aslot + missed_count
                    // So: old_aslot = current_aslot - missed_count - 1
                    // Missed slot i (0-based): abs_slot = current_aslot - missed_count + i
                    const auto &dgp = database().get_dynamic_global_properties();
                    const auto &wso = database().get_witness_schedule_object();
                    uint64_t cur_aslot = dgp.current_aslot;
                    uint32_t num_witnesses = wso.num_scheduled_witnesses;
                    if (num_witnesses == 0) return;

                    // Check each missed slot to see if our witness was scheduled
                    bool our_witness_missed = false;
                    std::string missed_witnesses_list;
                    for (uint32_t i = 0; i < missed_count && i < 100; ++i) {
                        uint64_t abs_slot = cur_aslot - missed_count + i;
                        const std::string &wname = wso.current_shuffled_witnesses[abs_slot % num_witnesses];
                        if (!missed_witnesses_list.empty()) missed_witnesses_list += ",";
                        missed_witnesses_list += wname;
                        if (_witnesses.count(wname) > 0) {
                            our_witness_missed = true;
                        }
                    }

                    if (!our_witness_missed) {
                        // Not our problem — other witnesses missed their slots
                        return;
                    }

                    // Our witness missed a slot! Dump full diagnostic state.
                    fc::time_point_sec now_ts = graphene::time::now();
                    int64_t ntp_us = 0;
                    try { ntp_us = graphene::time::ntp_error().count(); } catch (...) {}

                    bool catching_up = false;
                    try { catching_up = p2p().is_catching_up_after_pause(); } catch (...) {}
                    bool dlt_syncing = false;
                    try { dlt_syncing = chain().is_syncing(); } catch (...) {}

                    std::string witness_names;
                    for (const auto &w : _witnesses) {
                        if (!witness_names.empty()) witness_names += ",";
                        witness_names += w;
                    }

                    fc::time_point_sec next_slot = database().get_slot_time(1);
                    std::string next_scheduled = database().get_scheduled_witness(1);

                    // Check on-chain signing key status for our witnesses
                    std::string key_status;
                    const auto &wit_idx = database().get_index<graphene::chain::witness_index>()
                        .indices().get<graphene::chain::by_name>();
                    for (const auto &wname : _witnesses) {
                        auto witr = wit_idx.find(wname);
                        if (!key_status.empty()) key_status += " ";
                        if (witr != wit_idx.end()) {
                            bool key_blank = (witr->signing_key == graphene::protocol::public_key_type());
                            key_status += wname + ":key=" + (key_blank ? "BLANK" : "ok")
                                + ":last_conf=" + std::to_string(witr->last_confirmed_block_num);
                        } else {
                            key_status += wname + ":NOT_FOUND";
                        }
                    }

                    elog("MISSED-SLOT-OUR-WITNESS: block #${bn} arrived, ${missed} slot(s) missed between #${prev} and #${bn}. "
                         "Missed witnesses: [${mw}]. OUR witness was scheduled! "
                         "State: ever_produced=${ep} minority_recovering=${mr} "
                         "last_slot_result=${sr} not_my_turn_streak=${nmts} slot0_streak=${sz} "
                         "dlt_syncing=${ds} catching_up=${cu} head=#${h} aslot=${a} num_sched=${ns} "
                         "ntp_offset=${ntp}us now=${now} next_slot_time=${nst} next_scheduled=${nsw} "
                         "witnesses=[${wn}] keys=[${ks}]",
                         ("bn", block_num)("missed", missed_count)("prev", prev_num)
                         ("mw", missed_witnesses_list)
                         ("ep", _ever_produced)
                         ("mr", _minority_fork_recovering)
                         ("sr", _last_slot_result)("nmts", _not_my_turn_streak)
                         ("sz", _slot_zero_streak)
                         ("ds", dlt_syncing)("cu", catching_up)
                         ("h", dgp.head_block_number)("a", cur_aslot)
                         ("ns", num_witnesses)
                         ("ntp", ntp_us)("now", now_ts)
                         ("nst", next_slot)("nsw", next_scheduled)
                         ("wn", witness_names)("ks", key_status));
                } catch (...) {
                    // Non-critical diagnostic — never disrupt block processing
                }
            }

            void witness_plugin::impl::schedule_production_loop() {
                //Schedule for the next 250ms tick regardless of chain state
                // With +250ms look-ahead in maybe_validate_block(), the tick at
                // T_slot - 250ms aligns now exactly to the slot boundary for zero-lag production.
                // If we would wait less than 50ms, wait for the whole 250ms period.
                int64_t ntp_microseconds = graphene::time::now().time_since_epoch().count();
                int64_t next_microseconds = 250000 - ( ntp_microseconds % 250000 );
                if (next_microseconds < 50000) { // we must sleep for at least 50ms
                    next_microseconds += 250000 ;
                }

                // Sanity check: in normal operation next_microseconds is always ≤500ms.
                // A larger value means NTP time jumped backward, which delays the loop
                // and can cause missed slots.
                if (next_microseconds > 500000) {
                    int64_t ntp_us = 0;
                    try { ntp_us = graphene::time::ntp_error().count(); } catch (...) {}
                    wlog("SCHEDULE WARNING: production loop sleeping ${d}ms (expected ≤500ms). "
                         "NTP may have jumped backward. ntp_offset=${n}us",
                         ("d", next_microseconds / 1000)("n", ntp_us));
                }
                production_timer_.expires_from_now( posix_time::microseconds(next_microseconds) );
                production_timer_.async_wait( [this](const system::error_code &) { block_validation_loop(); } );
            }

            block_validation_condition::block_validation_condition_enum witness_plugin::impl::block_validation_loop() {
                block_validation_condition::block_validation_condition_enum result;
                fc::mutable_variant_object capture;
                if (database()._debug_block_production) ilog("DEBUG_CRASH: block_validation_loop ENTER");
                try {
                    result = maybe_validate_block(capture);
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
                    result = block_validation_condition::exception_validating_block;
                }

                if (database()._debug_block_production) ilog("DEBUG_CRASH: maybe_validate_block returned ${r}", ("r", (int)result));
                if (result != block_validation_condition::not_time_yet)
                    _last_slot_result = (int)result;
                switch (result) {
                    case block_validation_condition::produced:
                        ilog("\033[92mGenerated block #${n} with timestamp ${t} at time ${c} by ${w} with ${tx} transactions\033[0m", (capture));
                        fork_collision_defer_count_ = 0;
                        _slot_zero_streak = 0;  // P18: reset stall counter on success
                        _not_my_turn_streak = 0; // reset not_my_turn tracking
                        _slot_hijack_count = 0;  // reset hijack counter on successful production
                        _ever_produced = true;
                        _last_production_time = fc::time_point::now();
                        // Reset watchdog auto-enabled debug logging after successful
                        // production. The watchdog enabled verbose logging to diagnose
                        // the silence; now that production is confirmed working, restore
                        // the user's original config to prevent indefinite DEBUG_CRASH spam.
                        if (_watchdog_debug_enabled) {
                            _watchdog_debug_enabled = false;
                            database()._debug_block_production = _debug_block_production_config;
                        }
                        if (_minority_fork_recovering) {
                            auto elapsed = fc::time_point::now() - _minority_fork_recovery_start;
                            ilog("MINORITY FORK RECOVERY COMPLETE: production resumed after ${e}s",
                                 ("e", elapsed.count() / 1000000));
                            _minority_fork_recovering = false;
                        }
                        break;
                    case block_validation_condition::not_synced:
                        if (_minority_fork_recovering) {
                            auto elapsed = fc::time_point::now() - _minority_fork_recovery_start;
                            if (elapsed.count() % 5000000 < 300000) { // log every ~5 seconds
                                auto &rdb = database();
                                ilog("MINORITY FORK RECOVERY: waiting for P2P sync (head=#${h}, "
                                     "slot_time=${st}, now=${now}, elapsed=${e}s)",
                                     ("h", rdb.head_block_num())
                                     ("st", rdb.get_slot_time(1))
                                     ("now", graphene::time::now())
                                     ("e", elapsed.count() / 1000000));
                            }
                        } else {
                            static fc::time_point _last_not_synced_log;
                            auto _now_ns = fc::time_point::now();
                            if ((_now_ns - _last_not_synced_log).count() > 10000000) {
                                _last_not_synced_log = _now_ns;
                                wlog("Block production deferred: not_synced (head=#${h}, catching_up=${c})",
                                     ("h", database().head_block_num())
                                     ("c", p2p().is_catching_up_after_pause()));
                            }
                        }
                        fork_collision_defer_count_ = 0;
                        _slot_zero_streak = 0;  // P18: reset on valid non-stall result
                        _not_my_turn_streak = 0; // reset not_my_turn tracking
                        break;
                    case block_validation_condition::not_my_turn:
                        // This log-record is commented, because it outputs very often
                        // ilog("Not producing block because it isn't my turn");
                        fork_collision_defer_count_ = 0;
                        _slot_zero_streak = 0;  // P18: reset on valid non-stall result
                        // Emergency master: the EMRG-DIAG log in maybe_validate_block fires
                        // per-slot details; nothing extra needed here.

                        // Track consecutive not_my_turn to detect schedule misalignment
                        _not_my_turn_streak++;
                        if (_not_my_turn_streak == 1) {
                            _not_my_turn_streak_start = fc::time_point::now();
                        }
                        if (_not_my_turn_streak == 500) {
                            // ~125s with slot>0 but not our witness — investigate
                            auto elapsed = fc::time_point::now() - _not_my_turn_streak_start;
                            wlog("NOT_MY_TURN STREAK: ${n} consecutive slots (${s}s) went to other witnesses. "
                                 "Last scheduled: ${sw}. Our witnesses: ${ow}. "
                                 "Check: is our witness still in shuffled schedule?",
                                 ("n", _not_my_turn_streak)("s", elapsed.count() / 1000000)
                                 ("sw", _last_scheduled_witness)
                                 ("ow", [_witnesses = _witnesses]() {
                                     std::string s;
                                     for (const auto& w : _witnesses) { if (!s.empty()) s += ","; s += w; }
                                     return s;
                                 }()));
                        }
                        break;
                    case block_validation_condition::not_time_yet:
                        // This log-record is commented, because it outputs very often
                        // ilog("Not producing block because slot has not yet arrived");
                        // P18 fix: Detect slot=0 stall caused by NTP drift.
                        // Only count as a stall when now <= head_block_time (NTP time
                        // has fallen behind chain time).  When now > head_block_time
                        // we are simply between slots — this is normal and should NOT
                        // increment the streak counter, otherwise every 2.5s of normal
                        // waiting triggers a spurious NTP resync.
                        {
                            auto _now = graphene::time::now();
                            auto _hbt = database().head_block_time();
                            if (_now <= fc::time_point(_hbt)) {
                                // Real stall: NTP time is behind chain time
                                _slot_zero_streak++;
                            } else {
                                // Normal: just waiting for next slot
                                _slot_zero_streak = 0;
                            }
                        }
                        if (_slot_zero_streak == 1) {
                            _slot_zero_streak_start = fc::time_point::now();
                        }
                        // Threshold 3 (~750ms): first logged warning. Threshold 1 is too
                        // noisy — blocks naturally arrive with timestamps slightly ahead
                        // of our NTP clock, triggering a false streak-start every ~3s.
                        // At threshold 3, the gap is large enough to be worth investigating.
                        if (_slot_zero_streak == 3) {
                            auto _now_init = graphene::time::now();
                            auto _hbt_init = database().head_block_time();
                            auto _nst_init = database().get_slot_time(1);
                            int64_t _drift_us = 0;
                            try { _drift_us = graphene::time::ntp_error().count(); } catch (...) {}
                            int64_t _gap_ms = (_nst_init - _now_init).count() / 1000;
                            std::string _next_w3 = database().get_scheduled_witness(1);
                            bool _ours3 = _witnesses.count(_next_w3) > 0;
                            wlog("SLOT=0 STREAK: ${n} consecutive not_time_yet. "
                                 "now=${now} head_block_time=${hbt} (drift=${d}us) "
                                 "next_slot=${nst} (gap=${g}ms) next_witness=${nw} is_ours=${o} head=#${h}.",
                                 ("n", _slot_zero_streak)("now", _now_init)("hbt", _hbt_init)
                                 ("d", _drift_us)("nst", _nst_init)("g", _gap_ms)
                                 ("nw", _next_w3)("o", _ours3)
                                 ("h", database().head_block_num()));
                        }
                        if (_slot_zero_streak == 10) {
                            // ~2.5s at 250ms schedule interval — NTP drift detected
                            auto _now10 = graphene::time::now();
                            auto _hbt10 = database().head_block_time();
                            auto _nst10 = database().get_slot_time(1);
                            int64_t _drift10 = 0;
                            try { _drift10 = graphene::time::ntp_error().count(); } catch (...) {}
                            int64_t _gap_ms = (_nst10 - _now10).count() / 1000;
                            std::string _next_w10 = database().get_scheduled_witness(1);
                            bool _ours10 = _witnesses.count(_next_w10) > 0;
                            wlog("slot=0 streak: ${n} consecutive not_time_yet (${s}s elapsed). "
                                 "now=${now}, head_block_time=${hbt}, next_slot_time=${nst}. "
                                 "Time to next slot: ${g}ms. NTP drift: ${d}us. "
                                 "head_age=${ha}ms. next_witness=${nw} is_ours=${o}. Forcing NTP resync.",
                                 ("n", _slot_zero_streak)("s", (_now10 - _slot_zero_streak_start).count() / 1000000)
                                 ("now", _now10)("hbt", _hbt10)("nst", _nst10)("g", _gap_ms)
                                 ("d", _drift10)("ha", (_now10 - fc::time_point(_hbt10)).count() / 1000)
                                 ("nw", _next_w10)("o", _ours10));
                            graphene::time::update_ntp_time();
                        }
                        if (_slot_zero_streak == 60) {
                            // ~15s — prolonged stall, investigate root cause
                            auto _now60 = graphene::time::now();
                            auto _hbt60 = database().head_block_time();
                            auto _nst60 = database().get_slot_time(1);
                            int64_t _drift60 = 0;
                            try { _drift60 = graphene::time::ntp_error().count(); } catch (...) {}
                            int64_t _future_ms = (fc::time_point(_hbt60) - _now60).count() / 1000;
                            bool catching_up = false;
                            try { catching_up = p2p().is_catching_up_after_pause(); } catch (...) {}
                            bool dlt_syncing = false;
                            try { dlt_syncing = chain().is_syncing(); } catch (...) {}
                            std::string _next_w60 = database().get_scheduled_witness(1);
                            bool _ours60 = _witnesses.count(_next_w60) > 0;
                            elog("SLOT=0 PROLONGED STALL: ${n} consecutive not_time_yet (${s}s). "
                                 "head_block_time=${hbt} is ${f}ms AHEAD of now=${now}! "
                                 "next_slot_time=${nst} next_witness=${nw} is_ours=${o}, NTP drift=${d}us. "
                                 "catching_up=${c}, dlt_syncing=${ds}, head=#${h}. "
                                 "Check: was a block with future timestamp applied?",
                                 ("n", _slot_zero_streak)("s", (_now60 - _slot_zero_streak_start).count() / 1000000)
                                 ("hbt", _hbt60)("f", _future_ms)("now", _now60)("nst", _nst60)
                                 ("nw", _next_w60)("o", _ours60)
                                 ("d", _drift60)("c", catching_up)("ds", dlt_syncing)
                                 ("h", database().head_block_num()));
                        }
                        if (_slot_zero_streak == 120) {
                            // ~30s — serious stall, head_block_time may be in the future
                            auto _now120 = graphene::time::now();
                            auto _hbt120 = database().head_block_time();
                            auto _nst120 = database().get_slot_time(1);
                            int64_t _drift120 = 0;
                            try { _drift120 = graphene::time::ntp_error().count(); } catch (...) {}
                            int64_t _future120_ms = (fc::time_point(_hbt120) - _now120).count() / 1000;
                            bool catching_up120 = false;
                            try { catching_up120 = p2p().is_catching_up_after_pause(); } catch (...) {}
                            bool dlt_syncing120 = false;
                            try { dlt_syncing120 = chain().is_syncing(); } catch (...) {}
                            const auto &wso120 = database().get_witness_schedule_object();
                            std::string shuffled_top3;
                            for (int i = 0; i < std::min<int>(3, wso120.num_scheduled_witnesses); i++) {
                                if (!shuffled_top3.empty()) shuffled_top3 += ",";
                                shuffled_top3 += wso120.current_shuffled_witnesses[i];
                            }
                            std::string _next_w120 = database().get_scheduled_witness(1);
                            bool _ours120 = _witnesses.count(_next_w120) > 0;
                            elog("CRITICAL: slot=0 stall for ${s}s! head_block_time=${hbt} is ${f}ms in the future "
                                 "relative to NTP time (now=${now}). next_slot_time=${nst} next_witness=${nw} is_ours=${o}, NTP drift=${d}us. "
                                 "Network is stalled. catching_up=${c}, dlt_syncing=${ds}, head=#${h}. "
                                 "shuffled_witnesses[0..2]=[${sw}]. "
                                 "ACTION REQUIRED: Check NTP sync, system clock, or restart node.",
                                 ("s", (_now120 - _slot_zero_streak_start).count() / 1000000)
                                 ("hbt", _hbt120)("f", _future120_ms)("now", _now120)("nst", _nst120)
                                 ("nw", _next_w120)("o", _ours120)
                                 ("d", _drift120)("c", catching_up120)("ds", dlt_syncing120)
                                 ("h", database().head_block_num())("sw", shuffled_top3));
                        }
                        break;
                    case block_validation_condition::no_private_key:
                        ilog("Not producing block for ${scheduled_witness} because I don't have the private key for ${scheduled_key}",
                             (capture));
                        break;
                    case block_validation_condition::low_participation:
                        elog("Not producing block because node appears to be on a minority fork with only ${pct}% witness participation",
                             (capture));
                        break;
                    case block_validation_condition::lag:
                        elog("Not producing block because node didn't wake up within 500ms of the slot time.");
                        graphene::time::update_ntp_time();  // Force NTP sync on timing issues
                        // After a lag, the current slot is already missed.  Skip ahead past this
                        // slot to avoid spinning in a tight loop where every 250ms tick rechecks
                        // the same missed slot and returns lag again.  By advancing _last_slot_time_
                        // to the missed slot's time, the next production loop will compute a slot
                        // that is actually in the future (or get slot=0 and wait normally).
                        _last_lag_slot_time = capture["scheduled_time"].as<fc::time_point_sec>();
                        break;
                    case block_validation_condition::consecutive:
                        elog("Not producing block because the last block was generated by the same witness.\nThis node is probably disconnected from the network so block production has been disabled.\nDisable this check with --allow-consecutive option.");
                        break;
                    case block_validation_condition::exception_validating_block:
                        elog("Failure when producing block with no transactions");
                        break;
                    case block_validation_condition::fork_collision:
                        wlog("Deferred block production due to fork collision; will retry next slot");
                        graphene::time::update_ntp_time();  // Force NTP sync on fork issues
                        break;
                    case block_validation_condition::minority_fork:
                        elog("Not producing block: minority fork detected, resyncing from P2P network");
                        break;
                }

                // Production watchdog: elog if we've produced before but have gone
                // silent for too long while production is still enabled.
                // Emergency master threshold: 60s (before 315s blanking at 105 missed blocks).
                // Regular witness threshold: 180s (before 600s blanking at 200 missed blocks).
                // Fires every 30s once triggered so the operator has multiple chances to react.
                //
                // When returning from a lag condition, skip ahead past the missed slot
                // so the production loop doesn't immediately recheck the same slot and
                // spin in a tight lag loop.
                if (_last_lag_slot_time != fc::time_point_sec()) {
                    // Compute elapsed time in milliseconds (count() is microseconds).
                    int64_t time_since_lag_ms = (fc::time_point::now() - fc::time_point(_last_lag_slot_time)).count() / 1000;
                    // If less than one full slot interval has passed since the lag, skip ahead
                    // to avoid rechecking the same missed slot.
                    if (time_since_lag_ms < (int64_t)CHAIN_BLOCK_INTERVAL * 1000) {
                        int64_t skip_ms = (CHAIN_BLOCK_INTERVAL * 1000) - time_since_lag_ms;
                        if (skip_ms > 0) {
                            if (database()._debug_block_production) {
                                ilog("DEBUG_CRASH: skipping ${ms}ms to avoid rechecking lagged slot", ("ms", skip_ms));
                            }
                            production_timer_.expires_from_now(posix_time::milliseconds(skip_ms));
                            production_timer_.async_wait([this](const system::error_code &) { block_validation_loop(); });
                            _last_lag_slot_time = fc::time_point_sec();
                            return result;
                        }
                    }
                    _last_lag_slot_time = fc::time_point_sec();
                }

                if (_ever_produced) {
                    // Check if production should be active by querying actual state
                    bool should_be_producing = false;
                    try {
                        const auto& dgp_watch = database().get_dynamic_global_properties();
                        // Production should be active if:
                        // - Not in minority fork recovery
                        // - Witnesses are configured
                        // - Either emergency master OR network is healthy (participation >= 33%)
                        if (!_minority_fork_recovering && !_witnesses.empty()) {
                            if (dgp_watch.emergency_consensus_active) {
                                // Emergency mode: should produce if we have emergency key
                                should_be_producing = (_witnesses.count(CHAIN_EMERGENCY_WITNESS_ACCOUNT) > 0);
                            } else {
                                // Normal mode: should produce if participation is healthy
                                uint32_t prate_watch = database().witness_participation_rate();
                                should_be_producing = (prate_watch >= 33 * CHAIN_1_PERCENT);
                            }
                        }
                    } catch (...) {}

                    if (should_be_producing) {
                        auto silent_for = fc::time_point::now() - _last_production_time;
                        bool is_emrg_master = _witnesses.count(CHAIN_EMERGENCY_WITNESS_ACCOUNT) > 0;
                        int64_t threshold_us = is_emrg_master ? 60000000 : 180000000;
                        if (silent_for.count() > threshold_us) {
                            // === AUTO-ENABLE DEBUG LOGGING ON FIRST WATCHDOG FIRE ===
                            // When the watchdog detects that production has stopped unexpectedly,
                            // automatically enable verbose DEBUG_CRASH logging to capture
                            // detailed execution flow on every subsequent production tick.
                            // This helps diagnose why blocks are not being produced.
                            if (!_watchdog_debug_enabled) {
                                _watchdog_debug_enabled = true;
                                database()._debug_block_production = true;
                                elog("WATCHDOG: Auto-enabled _debug_block_production for detailed diagnostic logging");
                            }

                            static fc::time_point _last_watchdog_log;
                            auto _now_wdog = fc::time_point::now();
                            if ((_now_wdog - _last_watchdog_log).count() > 30000000) {
                                _last_watchdog_log = _now_wdog;
                                auto& db_wd = database();
                                bool catching_up = false;
                                try { catching_up = p2p().is_catching_up_after_pause(); } catch (...) {}
                                bool dlt_syncing = false;
                                try { dlt_syncing = chain().is_syncing(); } catch (...) {}
                                std::string witness_names;
                                for (const auto& w : _witnesses) { if (!witness_names.empty()) witness_names += ","; witness_names += w; }
                                int64_t ntp_us = 0;
                                try { ntp_us = graphene::time::ntp_error().count(); } catch (...) {}

                                // Who does the chain expect to produce right now?
                                std::string scheduled_now = "?";
                                bool we_are_scheduled = false;
                                // How many of our witnesses appear anywhere in the full shuffled schedule?
                                uint32_t our_slots_in_schedule = 0;
                                // Which of our witnesses have zero on-chain signing key (blanked by emergency consensus)?
                                std::string blanked_keys;
                                try {
                                    fc::time_point_sec now_sec = graphene::time::now() + fc::microseconds(250000);
                                    uint32_t cur_slot = db_wd.get_slot_at_time(now_sec);
                                    if (cur_slot > 0) {
                                        scheduled_now = db_wd.get_scheduled_witness(cur_slot);
                                        we_are_scheduled = _witnesses.count(scheduled_now) > 0;
                                    } else {
                                        // Between slots: show who gets the NEXT slot
                                        scheduled_now = "between_slots/" + db_wd.get_scheduled_witness(1);
                                    }

                                    // Scan full shuffled schedule for our witnesses
                                    const auto &wso_wd = db_wd.get_witness_schedule_object();
                                    for (int i = 0; i < wso_wd.num_scheduled_witnesses; i++) {
                                        if (_witnesses.count(wso_wd.current_shuffled_witnesses[i]) > 0)
                                            our_slots_in_schedule++;
                                    }

                                    // Check on-chain signing keys for our witnesses
                                    const auto &wit_idx = db_wd.get_index<graphene::chain::witness_index>().indices().get<graphene::chain::by_name>();
                                    for (const auto& w_name : _witnesses) {
                                        auto w_itr = wit_idx.find(w_name);
                                        if (w_itr != wit_idx.end() &&
                                            w_itr->signing_key == graphene::protocol::public_key_type()) {
                                            if (!blanked_keys.empty()) blanked_keys += ",";
                                            blanked_keys += w_name;
                                        }
                                    }
                                } catch (...) {}

                                int64_t head_age_s = (fc::time_point::now() - fc::time_point(db_wd.head_block_time())).count() / 1000000;

                                elog("WITNESS-WATCHDOG: ${t} silent for ${s}s! "
                                    "witnesses=${w} keys=${k} skip_flags=${sf} minority_recovering=${mr} "
                                    "slot_result=${sr} dlt_syncing=${ds} catching_up=${c} "
                                    "head=#${h} head_age=${ha}s scheduled_now=${sw} we_are_scheduled=${ws} "
                                    "in_schedule=${is}/${total} blanked_keys=[${bk}] "
                                    "slot0_streak=${sz} not_my_turn_streak=${nmt} last_scheduled=${nmtw} "
                                    "ntp_offset=${n}us slot_hijacks=${shj} debug_logging=${dl}",
                                    ("t", is_emrg_master ? "emergency master" : "witness")
                                    ("s", silent_for.count() / 1000000)
                                    ("w", witness_names)
                                    ("k", _private_keys.size())
                                    ("sf", _production_skip_flags)
                                    ("mr", _minority_fork_recovering)
                                    ("sr", _last_slot_result)
                                    ("ds", dlt_syncing)
                                    ("c", catching_up)
                                    ("h", db_wd.head_block_num())
                                    ("ha", head_age_s)
                                    ("sw", scheduled_now)
                                    ("ws", we_are_scheduled)
                                    ("is", our_slots_in_schedule)
                                    ("total", _witnesses.size())
                                    ("bk", blanked_keys)
                                    ("sz", _slot_zero_streak)
                                    ("nmt", _not_my_turn_streak)
                                    ("nmtw", _last_scheduled_witness)
                                    ("n", ntp_us)
                                    ("shj", _slot_hijack_count)
                                    ("dl", _watchdog_debug_enabled));

                                // === WATCHDOG PRODUCTION RECOVERY ===
                                // Brute-force recovery: if production is silent but
                                // the node is clearly operational (head advancing,
                                // FORWARD mode, peers connected), force-reset every
                                // flag that could silently block production.  This
                                // covers any safety gate that may have gotten stuck
                                // due to race conditions, stale state, or edge
                                // cases we haven't diagnosed.
                                //
                                // Conditions for recovery:
                                //   - Head is recent (external blocks arriving)
                                //   - Not in active P2P sync
                                //   - At least some peers connected
                                //   - We have witnesses with valid keys in schedule
                                bool head_advancing = (head_age_s >= 0 && head_age_s < 30);
                                bool has_peers = false;
                                try { has_peers = p2p().get_connections_count() > 0; } catch (...) {}
                                bool has_active_keys = (our_slots_in_schedule > 0 && blanked_keys.size() < witness_names.size());

                                if (head_advancing && !dlt_syncing && !catching_up && has_peers && has_active_keys) {
                                    bool did_recover = false;

                                    // Clear minority fork recovery state
                                    if (_minority_fork_recovering) {
                                        _minority_fork_recovering = false;
                                        did_recover = true;
                                        elog("WATCHDOG-RECOVERY: cleared _minority_fork_recovering");
                                    }

                                    // Force-clear P2P catchup flag
                                    try {
                                        if (p2p().is_catching_up_after_pause()) {
                                            p2p().clear_catchup_flag();
                                            did_recover = true;
                                            elog("WATCHDOG-RECOVERY: force-cleared P2P catching_up_after_pause flag");
                                        }
                                    } catch (...) {}

                                    // Force-clear chain syncing flag
                                    try {
                                        if (chain().is_syncing()) {
                                            chain().clear_syncing();
                                            did_recover = true;
                                            elog("WATCHDOG-RECOVERY: force-cleared chain syncing flag");
                                        }
                                    } catch (...) {}

                                    // Reset streak counters that may affect logic
                                    _not_my_turn_streak = 0;
                                    _slot_zero_streak = 0;

                                    if (did_recover) {
                                        elog("WATCHDOG-RECOVERY: production forcibly restored after ${s}s silence "
                                            "(head=#${h}, head_age=${ha}s, peers=${p}, in_schedule=${is})",
                                            ("s", silent_for.count() / 1000000)
                                            ("h", db_wd.head_block_num())
                                            ("ha", head_age_s)
                                            ("p", has_peers)
                                            ("is", our_slots_in_schedule));
                                    }
                                } else {
                                    elog("WATCHDOG-RECOVERY: skipped — conditions not met "
                                        "(head_advancing=${ha} dlt_syncing=${ds} catching_up=${cu} "
                                        "has_peers=${hp} has_active_keys=${hk})",
                                        ("ha", head_advancing)("ds", dlt_syncing)("cu", catching_up)
                                        ("hp", has_peers)("hk", has_active_keys));
                                }
                            }
                        }
                    }
                }

                if (database()._debug_block_production) ilog("DEBUG_CRASH: scheduling next production loop");
                schedule_production_loop();
                if (database()._debug_block_production) ilog("DEBUG_CRASH: block_validation_loop EXIT");
                return result;
            }

            block_validation_condition::block_validation_condition_enum witness_plugin::impl::maybe_validate_block(fc::mutable_variant_object &capture) {
                auto &db = database();
                if (db._debug_block_production) ilog("DEBUG_CRASH: maybe_validate_block ENTER");
                fc::time_point now_fine = graphene::time::now();
                fc::time_point_sec now = now_fine + fc::microseconds( 250000 );

                // Read DGP early so the DLT sync guard can check emergency consensus state.
                // In emergency mode the master MUST produce blocks regardless of sync state;
                // blocking production here creates a permanent deadlock because:
                //   - The master is the sole block producer
                //   - No blocks arrive to clear the syncing flag
                //   - The production loop is the only path to advance the chain
                if (db._debug_block_production) ilog("DEBUG_CRASH: getting dgp");
                const auto &dgp = db.get_dynamic_global_properties();
                if (db._debug_block_production) ilog("DEBUG_CRASH: dgp ok, head=${h} emergency=${e}", ("h", dgp.head_block_number)("e", dgp.emergency_consensus_active));

                // === DLT MODE: DEFER PRODUCTION DURING ACTIVE SYNC ===
                // In DLT mode, the witness must not produce blocks while the
                // chain is actively receiving sync blocks from P2P.  Producing
                // during sync creates blocks on a stale head that conflict
                // with incoming blocks, causing "failed to link" errors and
                // re-triggering sync — the oscillation bug described in
                // problem6.log.
                //
                // EMERGENCY MASTER EXCEPTION: When emergency consensus is active
                // AND this node holds the emergency-private-key (i.e. it IS the
                // master), production MUST proceed regardless of sync state.
                // The master is the sole block producer — waiting for sync to
                // complete would deadlock because no blocks arrive to clear
                // the syncing flag (p18.log).
                //
                // EMERGENCY SLAVE: Must still respect the syncing flag.  A slave
                // node that produces on a stale head creates double-production
                // collisions and minority forks (p32.log).
                //
                // Outside DLT mode this check is NOT applied because normal
                // witnesses must produce on the canonical chain head even
                // while the network is catching up.
                if (db._dlt_mode && chain().is_syncing()) {
                    bool we_are_emergency_master =
                        dgp.emergency_consensus_active &&
                        _witnesses.find(CHAIN_EMERGENCY_WITNESS_ACCOUNT) != _witnesses.end();
                    if (!we_are_emergency_master) {
                        return block_validation_condition::not_synced;
                    }
                    // Emergency master: bypass sync check to avoid deadlock.
                }

                // === SNAPSHOT PAUSE / POST-PAUSE CATCHUP GATE ===
                // Defer block production when P2P block processing is paused
                // (snapshot creation holding DB read lock) or while catching
                // up after the pause (draining queued blocks).  Producing
                // during pause deadlocks on the write lock; producing after
                // pause but before drain creates a fork on a stale head.
                //
                // This gate applies to ALL witness types (emergency and normal).
                // The flag is cleared when: pause ends + drain completes +
                // no peer is ahead (see drain_paused_block_queue / periodic_task).
                //
                // Check snapshot plugin directly for snapshot_in_progress flag.
                try {
                    if (snapshot().is_snapshot_in_progress()) {
                        wlog("Deferring block production: snapshot creation in progress "
                             "(head=#${h}). Waiting for snapshot to complete.",
                             ("h", db.head_block_num()));
                        return block_validation_condition::not_synced;
                    }
                } catch (...) {
                    // snapshot plugin may not be available
                }

                try {
                    if (p2p().is_catching_up_after_pause()) {
                        wlog("Deferring block production: P2P is catching up after "
                             "snapshot pause (head=#${h}). Waiting for gap fill.",
                             ("h", db.head_block_num()));
                        return block_validation_condition::not_synced;
                    }
                } catch (...) {
                    // p2p plugin may not be available during startup
                }

                // === HARDFORK 12: THREE-STATE SAFETY ENFORCEMENT ===
                if (db._debug_block_production) ilog("DEBUG_CRASH: checking hardfork12 and emergency path");

                if (db.has_hardfork(CHAIN_HARDFORK_12)) {
                    if (dgp.emergency_consensus_active) {
                        // EMERGENCY MODE: auto-bypass both stale and participation checks
                        // for the emergency master only.  The master holds the
                        // emergency-private-key and MUST produce to avoid deadlock.
                        //
                        // Slave nodes (no emergency key) must still sync first —
                        // producing on a stale head creates double-production
                        // collisions and minority forks (p32.log).
                        bool we_are_emergency_master =
                            _witnesses.find(CHAIN_EMERGENCY_WITNESS_ACCOUNT) != _witnesses.end();
                        if (!we_are_emergency_master && _witnesses.empty()) {
                            elog("EMERGENCY MODE ACTIVE but no witnesses configured! "
                                 "Block production impossible. Add --emergency-private-key to config.");
                        }
                    } else {
                        uint32_t prate = db.witness_participation_rate();
                        if (prate >= 33 * CHAIN_1_PERCENT) {
                            // HEALTHY NETWORK: enforce safe defaults automatically.
                            // Even if operator has enable-stale-production=true in config,
                            // it's overridden because the network doesn't need it.
                            // Clear the stale-production skip flag so that minority fork
                            // detection is re-enabled now that the network is healthy.
                            _production_skip_flags &= ~graphene::chain::database::skip_undo_history_check;
                            // Participation is already >= 33%, no need to check again
                        } else {
                            // DISTRESSED NETWORK (participation < 33%, not yet emergency):
                            // Honor manual config overrides -- operator may be trying to
                            // accelerate recovery before the 1-hour timeout.
                            if (prate < _required_witness_participation) {
                                if (_production_skip_flags & graphene::chain::database::skip_undo_history_check) {
                                    // enable-stale-production=true: operator override, produce anyway
                                    // to bootstrap/recover a fully stalled network where all nodes
                                    // see low participation and would otherwise deadlock.
                                    dlog("Witness participation is ${p}% but stale-production is enabled, "
                                         "producing anyway to recover stalled network",
                                         ("p", uint32_t(prate / CHAIN_1_PERCENT)));
                                } else {
                                    // Network partition guard: if participation is below 33%
                                    // this node is likely in a minority network segment.
                                    // Producing here risks two partitions simultaneously
                                    // building chains — each seeing only the other segment's
                                    // witnesses as absent, neither triggering minority_fork
                                    // detection below (which requires ALL recent fork_db
                                    // blocks to be ours).  Stopping production is the safe
                                    // choice; use enable-stale-production=true to override
                                    // when you know the low participation is caused by
                                    // offline witnesses rather than a partition.
                                    capture("pct", uint32_t(prate / CHAIN_1_PERCENT));
                                    return block_validation_condition::low_participation;
                                }
                            }
                        }
                    }
                } else {
                    // Pre-hardfork 12: no participation check here (done later)
                }

                //try get block post validation list for each witness
                //if witness can validate it, sign chain_id and block_id for message
                //broadcast validation message by p2p plugin
                if (db._debug_block_production) ilog("DEBUG_CRASH: emergency/participation check done, entering block_post_validation");
                if(last_block_post_validation_time < now_fine ){
                    last_block_post_validation_time = now;
                    if (db._debug_block_production) ilog("DEBUG_CRASH: block_post_validation tick, iterating ${n} witnesses", ("n", _witnesses.size()));

                    // Pre-compute the current scheduled witnesses set so we can skip
                    // configured witnesses that are not actually scheduled.  A witness
                    // that is not in the current schedule cannot contribute to LIB
                    // advancement and broadcasting their post-validation is wasted
                    // bandwidth and CPU.
                    const witness_schedule_object &wso = db.get_witness_schedule_object();
                    std::set<string> scheduled_witnesses_set;
                    for (int i = 0; i < wso.num_scheduled_witnesses; i += CHAIN_BLOCK_WITNESS_REPEAT) {
                        if (wso.current_shuffled_witnesses[i] != account_name_type()) {
                            scheduled_witnesses_set.insert(wso.current_shuffled_witnesses[i]);
                        }
                    }

                    //get block post validation for each witness we have
                    for (auto &witness_account : _witnesses) {
                        // Skip witnesses not in the current schedule — they cannot
                        // contribute to block post validation and broadcasting their
                        // signatures is pointless network spam.
                        if (scheduled_witnesses_set.find(witness_account) == scheduled_witnesses_set.end()) {
                            continue;
                        }

                        bool ignore_witness = false;
                        if (db._debug_block_production) ilog("DEBUG_CRASH: get_block_post_validations for ${w}", ("w", witness_account));
                        auto block_post_validations = db.get_block_post_validations(witness_account);
                        if (db._debug_block_production) ilog("DEBUG_CRASH: got ${n} post_validations for ${w}", ("n", block_post_validations.size())("w", witness_account));
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
                                if (db._debug_block_production) ilog("DEBUG_CRASH: signing post_validations for ${w}", ("w", witness_account));
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
                                            p2p().post_broadcast_block_post_validation(block_post_validations[i].block_id, witness_account, bpv_signature);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                if (db._debug_block_production) ilog("DEBUG_CRASH: block_post_validation done, entering minority fork detection");
                // === MINORITY FORK DETECTION ===
                // If the last CHAIN_MAX_WITNESSES (21) blocks in fork_db were ALL
                // produced by our own configured witnesses, we are likely stuck on
                // a minority fork where no external witnesses are participating.
                //
                // SKIP during emergency consensus: in emergency mode all blocks are
                // produced by the committee account (which is in _witnesses), so the
                // check would always falsely trigger and kill recovery.
                //
                // EXCEPTION: In DLT mode, even during emergency consensus, we apply
                // a higher-threshold minority fork check.  See the DLT-specific block
                // below.
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
                                _minority_fork_recovering = true;
                                _minority_fork_recovery_start = fc::time_point::now();
                                return block_validation_condition::minority_fork;
                            }
                        }
                    }
                }

                // === DLT-SPECIFIC MINORITY FORK DETECTION IN EMERGENCY MODE ===
                // In emergency + DLT mode, the standard minority fork check above is
                // skipped because committee blocks are produced by an account that
                // may be in _witnesses.  However, a DLT emergency witness that has
                // lost its P2P connection to the master will produce blocks for its
                // own witness slots AND the committee slots (because the emergency
                // key covers committee).  After a few rounds with NO external blocks
                // at all, the node is on a minority fork.
                //
                // Detect this by checking whether the last full round (21 blocks)
                // in fork_db contain ONLY blocks from our witnesses.  In a healthy
                // emergency hybrid schedule, committee slots are filled by the master
                // node's blocks — so we should see non-our-witness blocks regularly.
                // If we don't, we're isolated.
                //
                // We use 1 round (21 blocks) because in a healthy emergency hybrid
                // schedule the committee (master) produces at least 1 block per
                // round, so we should never see 21 consecutive blocks from only
                // our witnesses unless we're isolated from the master.  This matches
                // the standard non-emergency minority fork threshold.
                //
                // IMPORTANT: If committee (CHAIN_EMERGENCY_WITNESS_ACCOUNT) is in the
                // current witness schedule AND we have its key (emergency-private-key
                // configured), this node IS the emergency master.  All blocks being
                // "ours" is expected — other nodes sync from us.  Skip minority fork
                // detection entirely to avoid false positives and the production
                // deadlock that would otherwise occur.
                if (dgp.emergency_consensus_active && db._dlt_mode) {
                    // If committee is in the schedule and we have its key, WE are the
                    // emergency master.  All blocks being "ours" is expected -- other
                    // nodes sync from us.  Skip minority fork detection to prevent
                    // false positives and the production deadlock.
                    // Check both conditions: (a) committee is in the schedule, AND
                    // (b) we have its key (committee is in _witnesses only when
                    // emergency-private-key was configured — see plugin_initialize).
                    bool we_are_master = false;
                    if (_witnesses.find(CHAIN_EMERGENCY_WITNESS_ACCOUNT) != _witnesses.end()) {
                        const witness_schedule_object &wso = db.get_witness_schedule_object();
                        for (int i = 0; i < wso.num_scheduled_witnesses; i += CHAIN_BLOCK_WITNESS_REPEAT) {
                            if (wso.current_shuffled_witnesses[i] == CHAIN_EMERGENCY_WITNESS_ACCOUNT) {
                                we_are_master = true;
                                break;
                            }
                        }
                    }

                    if (!we_are_master) {
                        // Slave DLT node: committee not in schedule or we don't have
                        // the key.  Run the existing fork_db isolation scan.
                        auto fork_head = db.get_fork_db().head();
                        if (fork_head) {
                            const uint32_t dlt_minority_threshold = CHAIN_MAX_WITNESSES; // 21 blocks = 1 full round
                            bool all_ours = true;
                            uint32_t blocks_checked = 0;
                            auto current = fork_head;

                            while (current && blocks_checked < dlt_minority_threshold) {
                                if (_witnesses.find(current->data.witness) == _witnesses.end()) {
                                    all_ours = false;
                                    break;
                                }
                                blocks_checked++;
                                current = current->prev.lock();
                            }

                            if (all_ours && blocks_checked >= dlt_minority_threshold) {
                                elog("DLT EMERGENCY MINORITY FORK DETECTED: last ${n} blocks all from our "
                                     "witnesses (1+ full rounds). Node is isolated from master. "
                                     "Resetting to LIB and resyncing from P2P network.",
                                     ("n", blocks_checked));
                                p2p().resync_from_lib(true /*force_emergency*/);
                                _minority_fork_recovering = true;
                                _minority_fork_recovery_start = fc::time_point::now();
                                return block_validation_condition::minority_fork;
                            }
                        }
                    } else {
                        if (db._debug_block_production) {
                            ilog("DEBUG_CRASH: DLT minority fork check SKIPPED - we are emergency master");
                        }
                    }
                }

                // Guard lockless reads into shared memory with the resize barrier.
                // This prevents a concurrent shared memory resize from invalidating
                // pointers while we read witness schedule, slot time, etc.
                // The guard is released before generate_block() which has its own.
                if (db._debug_block_production) ilog("DEBUG_CRASH: creating op_guard");
                fc::time_point _guard_enter = fc::time_point::now();
                auto op_guard = db.make_operation_guard();
                if (db._debug_block_production) ilog("DEBUG_CRASH: op_guard ok");

                // Re-capture 'now' after acquiring op_guard: if make_operation_guard()
                // blocked on a DB resize, the original 'now' (captured at function entry)
                // is stale and get_slot_at_time() would return 0, causing the production
                // loop to silently miss all blocks until the watchdog fires.
                now_fine = graphene::time::now();
                now = now_fine + fc::microseconds(250000);

                // Detect op_guard stall crossing a slot boundary.
                // A stall of 3+ seconds shifts 'now' into the next witness's slot,
                // causing not_my_turn even when our slot just passed — silent miss.
                {
                    int64_t _guard_ms = (fc::time_point::now() - _guard_enter).count() / 1000;
                    if (_guard_ms > 100) {
                        uint32_t _slot_before = db.get_slot_at_time(now_fine + fc::microseconds(250000) - fc::microseconds(_guard_ms * 1000));
                        std::string _wit_before = _slot_before > 0 ? db.get_scheduled_witness(_slot_before) : "none";
                        bool _our_slot_lost = _slot_before > 0 && _witnesses.count(_wit_before) > 0;
                        if (_our_slot_lost) {
                            elog("WITNESS-SLOT-LOST: op_guard stall ${d}ms crossed slot boundary! "
                                 "missed slot for ${w} — now points to next slot after refresh. head=#${h}",
                                 ("d", _guard_ms)("w", _wit_before)("h", db.head_block_num()));
                        } else {
                            wlog("WITNESS-GUARD-STALL: op_guard blocked ${d}ms (slot before=${sb} witness=${w}). head=#${h}",
                                 ("d", _guard_ms)("sb", _slot_before)("w", _wit_before)("h", db.head_block_num()));
                        }
                    }
                }

                // is anyone scheduled to produce now or one second in the future?
                if (db._debug_block_production) ilog("DEBUG_CRASH: get_slot_at_time");
                uint32_t slot = db.get_slot_at_time(now);
                if (db._debug_block_production) ilog("DEBUG_CRASH: slot=${s}", ("s", slot));
                if (slot == 0) {
                    capture("next_time", db.get_slot_time(1));
                    // Emergency master diagnostic: log when we are stuck at slot=0 and
                    // real time is well past the expected next slot (i.e. we should have
                    // a slot available but get_slot_at_time says 0 — NTP or head-time anomaly)
                    if (_witnesses.find(CHAIN_EMERGENCY_WITNESS_ACCOUNT) != _witnesses.end()) {
                        const auto &_dgp2 = db.get_dynamic_global_properties();
                        if (_dgp2.emergency_consensus_active) {
                            static fc::time_point _last_slot0_log;
                            auto _now2 = fc::time_point::now();
                            if ((_now2 - _last_slot0_log).count() > 10000000) { // log every 10s max
                                _last_slot0_log = _now2;
                                dlog("EMRG-DIAG slot=0: head=#${h} head_time=${ht} now=${now} next_slot=${ns} aslot=${a} num_sched=${ns2}",
                                    ("h", _dgp2.head_block_number)
                                    ("ht", db.head_block_time())
                                    ("now", now_fine)
                                    ("ns", db.get_slot_time(1))
                                    ("a", _dgp2.current_aslot)
                                    ("ns2", db.get_witness_schedule_object().num_scheduled_witnesses));
                            }
                        }
                    }
                    // NTP drift check: warn if local clock is >250ms behind NTP time.
                    // A slow local clock causes get_slot_at_time() to return slot=0 even
                    // when the network is expecting our block, making us miss slots silently.
                    {
                        int64_t ntp_us = 0;
                        try { ntp_us = graphene::time::ntp_error().count(); } catch (...) {}
                        if (ntp_us > 250000) { // local clock >250ms behind NTP
                            static fc::time_point _last_ntp_drift_log;
                            auto _now_nd = fc::time_point::now();
                            if ((_now_nd - _last_ntp_drift_log).count() > 10000000) {
                                _last_ntp_drift_log = _now_nd;
                                auto next_slot_time = db.get_slot_time(1);
                                wlog("NTP DRIFT: local clock is ${n}ms behind NTP — may miss slots! "
                                     "(now=${now} next_slot=${ns} head=#${h})",
                                     ("n", ntp_us / 1000)("now", now_fine)
                                     ("ns", next_slot_time)("h", db.head_block_num()));
                            }
                        }
                    }
                    return block_validation_condition::not_time_yet;
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

                if (db._debug_block_production) ilog("DEBUG_CRASH: get_scheduled_witness(${s})", ("s", slot));
                string scheduled_witness = db.get_scheduled_witness(slot);
                if (db._debug_block_production) ilog("DEBUG_CRASH: scheduled_witness=${w}", ("w", scheduled_witness));
                // we must control the witness scheduled to produce the next block.
                if (_witnesses.find(scheduled_witness) == _witnesses.end()) {
                    capture("scheduled_witness", scheduled_witness);
                    _last_scheduled_witness = scheduled_witness; // track for diagnostic
                    // Emergency master diagnostic: log when committee is configured but
                    // get_scheduled_witness returned a different name — reveals schedule misalignment
                    if (_witnesses.find(CHAIN_EMERGENCY_WITNESS_ACCOUNT) != _witnesses.end()) {
                        const auto &_dgp3 = db.get_dynamic_global_properties();
                        if (_dgp3.emergency_consensus_active) {
                            static fc::time_point _last_nmt_log;
                            auto _now3 = fc::time_point::now();
                            if ((_now3 - _last_nmt_log).count() > 3000000) { // log every 3s max (once per slot)
                                _last_nmt_log = _now3;
                                const auto &_wso3 = db.get_witness_schedule_object();
                                dlog("EMRG-DIAG not_my_turn: slot=${s} scheduled=${sw} head=#${h} aslot=${a} num_sched=${ns} aslot_mod=${am}",
                                    ("s", slot)
                                    ("sw", scheduled_witness)
                                    ("h", _dgp3.head_block_number)
                                    ("a", _dgp3.current_aslot)
                                    ("ns", _wso3.num_scheduled_witnesses)
                                    ("am", _dgp3.current_aslot % _wso3.num_scheduled_witnesses));
                            }
                        }
                    }
                    return block_validation_condition::not_my_turn;
                }

                if (db._debug_block_production) ilog("DEBUG_CRASH: looking up witness in index");
                const auto &witness_by_name = db.get_index<graphene::chain::witness_index>().indices().get<graphene::chain::by_name>();
                auto itr = witness_by_name.find(scheduled_witness);
                if (db._debug_block_production) ilog("DEBUG_CRASH: witness found=${f}", ("f", itr != witness_by_name.end()));

                fc::time_point_sec scheduled_time = db.get_slot_time(slot);
                graphene::protocol::public_key_type scheduled_key = itr->signing_key;
                if (db._debug_block_production) ilog("DEBUG_CRASH: scheduled_key=${k}", ("k", scheduled_key));

                // Skip production if the scheduled slot time is at or before
                // the current head block time. This means the slot was already filled
                // by another block (e.g. received from P2P during/after a snapshot pause).
                // Without this guard, the witness produces a competing block at the same
                // height, creating a micro-fork that propagates to all peers.
                //
                // This can happen when:
                //   1. Snapshot pauses P2P processing for several seconds
                //   2. A block from another witness fills the slot during/after pause
                //   3. Our witness production loop fires for a slot that's now occupied
                if (scheduled_time <= db.head_block_time()) {
                    wlog("Skipping block production: scheduled slot ${st} is at or before "
                         "head_block_time ${hbt} (head=#${hn}). Slot was already filled.",
                         ("st", scheduled_time)("hbt", db.head_block_time())
                         ("hn", db.head_block_num()));
                    return block_validation_condition::not_time_yet;
                }

                // Check if witness has zero/null signing key (intentionally disabled for block production)
                if (scheduled_key == graphene::protocol::public_key_type()) {
                    if (scheduled_witness == CHAIN_EMERGENCY_WITNESS_ACCOUNT &&
                        _witnesses.find(CHAIN_EMERGENCY_WITNESS_ACCOUNT) != _witnesses.end()) {
                        static fc::time_point _last_zerokey_log;
                        auto _now_zk = fc::time_point::now();
                        if ((_now_zk - _last_zerokey_log).count() > 3000000) {
                            _last_zerokey_log = _now_zk;
                            dlog("EMRG-DIAG zero-key: committee scheduled at slot=${s} but signing_key is ZERO on chain! "
                                 "head=#${h} aslot=${a}",
                                 ("s", slot)("h", db.head_block_num())
                                 ("a", db.get_dynamic_global_properties().current_aslot));
                        }
                    } else if (_witnesses.count(scheduled_witness)) {
                        // Our configured witness is scheduled but its on-chain signing_key is zero.
                        // This means the chain blanked the key due to too many missed blocks
                        // (database.cpp update_global_dynamic_data).  Production is permanently
                        // blocked until the operator sends an update_witness transaction.
                        static fc::time_point _last_zerokey_regular_log;
                        auto _now_zkr = fc::time_point::now();
                        if ((_now_zkr - _last_zerokey_regular_log).count() > 60000000) {
                            _last_zerokey_regular_log = _now_zkr;
                            elog("Witness ${w} scheduled at slot=${s} but signing_key is ZERO on chain! "
                                 "Key was blanked due to too many missed blocks. "
                                 "Send update_witness transaction to re-enable. head=#${h}",
                                 ("w", scheduled_witness)("s", slot)("h", db.head_block_num()));
                        }
                    }
                    return block_validation_condition::not_my_turn;
                }

                auto private_key_itr = _private_keys.find(scheduled_key);

                if (private_key_itr == _private_keys.end()) {
                    capture("scheduled_witness", scheduled_witness);
                    capture("scheduled_key", scheduled_key);
                    return block_validation_condition::no_private_key;
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
                            return block_validation_condition::low_participation;
                        }
                    }
                }

                if (llabs((scheduled_time - now).count()) > fc::milliseconds(500).count()) {
                    capture("scheduled_time", scheduled_time)("now", now);
                    {
                        static fc::time_point _last_lag_log;
                        auto _now_lag = fc::time_point::now();
                        if ((_now_lag - _last_lag_log).count() > 60000000) {
                            _last_lag_log = _now_lag;
                            wlog("Block production LAG: our slot for ${w} at ${st} but now=${now} "
                                 "(delta=${d}ms). Production loop fired too late for this slot. "
                                 "head=#${h}",
                                 ("w", scheduled_witness)("st", scheduled_time)("now", now)
                                 ("d", (scheduled_time - now).count() / 1000)("h", db.head_block_num()));
                        }
                    }
                    return block_validation_condition::lag;
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
                                    return block_validation_condition::fork_collision;
                                } else {
                                    // Tied or comparison impossible (one tip not in fork_db)
                                    // Defer briefly, timeout will kick in
                                    capture("height", db.head_block_num() + 1)("scheduled_witness", scheduled_witness);
                                    wlog("Fork collision at height ${h} with tied/unknown vote weight. "
                                         "Deferring (attempt ${n}/${max}).",
                                         ("h", db.head_block_num() + 1)
                                         ("n", fork_collision_defer_count_)
                                         ("max", _fork_collision_timeout_blocks));
                                    return block_validation_condition::fork_collision;
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
                                return block_validation_condition::fork_collision;
                            }
                        }
                    }
                }

                // Release the operation guard before generate_block(), which
                // acquires its own guard internally via apply_pending_resize()
                // and with_strong_write_lock().
                op_guard.release();

                // Re-check snapshot pause: the gate at ~line 1133 passed before the
                // snapshot could have started (race window ~1 block interval).
                // If the snapshot began since then, _block_processing_paused is now
                // true and generate_block would immediately contend on the read lock
                // held by the snapshot thread, causing 2-11s write-lock starvation
                // (p67 incident).  Returning not_time_yet here costs one missed slot
                // (3 s) — far cheaper than the full snapshot read hold time.
                //
                // Check snapshot plugin directly for snapshot_in_progress flag.
                try {
                    if (snapshot().is_snapshot_in_progress()) {
                        dlog("Snapshot started between production checks for slot ${s}, skipping produce",
                             ("s", slot));
                        return block_validation_condition::not_time_yet;
                    }
                } catch (...) {}

                try {
                    if (p2p().is_catching_up_after_pause()) {
                        dlog("Snapshot started between production checks for slot ${s}, skipping produce",
                             ("s", slot));
                        return block_validation_condition::not_time_yet;
                    }
                } catch (...) {}

                if (db._debug_block_production) ilog("DEBUG_CRASH: calling generate_block for ${w}", ("w", scheduled_witness));
                if (scheduled_witness == CHAIN_EMERGENCY_WITNESS_ACCOUNT) {
                    dlog("EMRG-DIAG producing: slot=${s} scheduled_time=${st} head=#${h} aslot=${a}",
                         ("s", slot)("st", scheduled_time)("h", db.head_block_num())
                         ("a", db.get_dynamic_global_properties().current_aslot));
                }
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
                        capture("n", block.block_num())("t", block.timestamp)("c", now)("w", scheduled_witness)("tx", block.transactions.size());
                        p2p().broadcast_block(block);

                        // If we produced a block but have few/no peers,
                        // force-reconnect seeds so the block can propagate.
                        // Skip in isolated-peers mode — seed reconnects are intentionally suppressed there.
                        auto peer_count = p2p().get_connections_count();
                        if (peer_count < 2 && !p2p().is_isolated_peers()) {
                            wlog("Produced block #${n} but only ${p} peer(s) connected — force-reconnecting seeds",
                                 ("n", block.block_num())("p", peer_count));
                            p2p().reconnect_seeds();
                        }

                        return block_validation_condition::produced;
                    }
                    catch (const graphene::chain::shared_memory_corruption_exception& e) {
                        elog("Shared memory corruption detected during block generation: ${e}", ("e", e.to_detail_string()));
                        chain().attempt_auto_recovery();
                        return block_validation_condition::exception_validating_block;
                    }
                    catch (const graphene::chain::unlinkable_block_exception& e) {
                        // Fork DB broken prev chain — retrying won't help.
                        // Roll back to LIB and resync from P2P network.
                        elog("unlinkable_block_exception during block generation: fork_db broken. "
                             "Rolling back to LIB and resyncing from P2P network.");
                        p2p().resync_from_lib(dgp.emergency_consensus_active /*force_emergency*/);
                        _minority_fork_recovering = true;
                        _minority_fork_recovery_start = fc::time_point::now();
                        return block_validation_condition::minority_fork;
                    }
                    catch (fc::exception &e) {
                        elog("${e}", ("e", e.to_detail_string()));
                        elog("Clearing pending transactions and attempting again");
                        db.clear_pending();
                        retry++;
                    }
                } while (retry < 2);

                return block_validation_condition::exception_validating_block;
            }
        }
    }
}
