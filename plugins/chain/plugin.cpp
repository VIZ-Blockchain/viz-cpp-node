#include <graphene/chain/database_exceptions.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/plugins/chain/plugin.hpp>

#include <fc/io/json.hpp>
#include <fc/string.hpp>

#include <iostream>
#include <graphene/protocol/protocol.hpp>
#include <graphene/protocol/types.hpp>
#include <future>
#include <atomic>

namespace graphene {
namespace plugins {
namespace chain {

    namespace bfs = boost::filesystem;
    using fc::flat_map;
    using protocol::block_id_type;

    class plugin::plugin_impl {
    public:

        uint64_t shared_memory_size = 0;
        boost::filesystem::path shared_memory_dir;
        bool replay = false;
        bool replay_if_corrupted = true;
        bool force_replay = false;
        bool resync = false;
        bool readonly = false;
        bool check_locks = false;
        bool validate_invariants = false;
        uint32_t flush_interval = 0;
        flat_map<uint32_t, protocol::block_id_type> loaded_checkpoints;

        uint32_t allow_future_time = 5;

        uint64_t read_wait_micro;
        uint32_t max_read_wait_retries;

        uint64_t write_wait_micro;
        uint32_t max_write_wait_retries;

        size_t inc_shared_memory_size;
        size_t min_free_shared_memory_size;

        bool enable_plugins_on_push_transaction;

        uint32_t block_num_check_free_size = 0;

        bool skip_virtual_ops = false;

        std::string snapshot_path; // --snapshot: load state from snapshot file
        bool replay_from_snapshot = false; // --replay-from-snapshot: snapshot + dlt_block_log replay
        bool auto_recover_from_snapshot = false; // --auto-recover-from-snapshot: auto-recover on corruption
        std::string snapshot_dir; // resolved snapshot directory for auto-discovery

        graphene::chain::database db;

        bool single_write_thread = false;

        bool sync_start_logged = false; // guard to log sync start only once

        std::atomic<bool> currently_syncing{false}; // true while processing P2P sync blocks

        bool pending_snapshot_load = false; // set when snapshot args present but callback not yet registered

        plugin_impl() {
            // get default settings
            read_wait_micro = db.read_wait_micro();
            max_read_wait_retries = db.max_read_wait_retries();

            write_wait_micro = db.write_wait_micro();
            max_write_wait_retries = db.max_write_wait_retries();
        }

        // HELPERS
        graphene::chain::database &database() {
            return db;
        }

        boost::asio::io_service& io_service() {
            return appbase::app().get_io_service();
        }

        constexpr const static char *plugin_name = "chain_api";
        static const std::string &name() {
            static std::string name = plugin_name;
            return name;
        }

        void check_time_in_block(const protocol::signed_block &block);
        bool accept_block(const protocol::signed_block &block, bool currently_syncing, uint32_t skip);
        void accept_transaction(const protocol::signed_transaction &trx);
        void wipe_db(const bfs::path &data_dir, bool wipe_block_log);
        void replay_db(const bfs::path &data_dir, bool force_replay);
        fc::path find_latest_snapshot();
    };

    void plugin::plugin_impl::check_time_in_block(const protocol::signed_block &block) {
        time_point_sec now = fc::time_point::now();

        uint64_t max_accept_time = now.sec_since_epoch();
        max_accept_time += allow_future_time;
        FC_ASSERT(block.timestamp.sec_since_epoch() <= max_accept_time);
    }

    bool plugin::plugin_impl::accept_block(const protocol::signed_block &block, bool currently_syncing_flag, uint32_t skip) {
        currently_syncing.store(currently_syncing_flag, std::memory_order_relaxed);
        if (currently_syncing_flag) {
            if (!sync_start_logged) {
                ilog("\033[92m>>> Syncing Blockchain started from block #${n} (head: ${head})\033[0m",
                     ("n", block.block_num())("head", db.head_block_num()));
                sync_start_logged = true;
            }

            if (block.block_num() % 500 == 0) {
                ilog("\033[93mSyncing Blockchain --- Got block: #${n} time: ${t} producer: ${p}\033[0m",
                     ("t", block.timestamp)("n", block.block_num())("p", block.witness));
            }
        } else {
            if (sync_start_logged) {
                ilog("\033[92mSync mode ended: received normal block #${n} (head: ${head}), sync_start_logged reset\033[0m",
                     ("n", block.block_num())("head", db.head_block_num()));
            }
            sync_start_logged = false; // reset guard when not syncing
        }

        check_time_in_block(block);

        skip = db.validate_block(block, skip);

        if (single_write_thread) {
            std::promise<bool> promise;
            auto result = promise.get_future();

            io_service().post([&]{
                try {
                    promise.set_value(db.push_block(block, skip));
                } catch(...) {
                    promise.set_exception(std::current_exception());
                }
            });
            return result.get(); // if an exception was, it will be thrown
        } else {
            return db.push_block(block, skip);
        }
    }

    void plugin::plugin_impl::wipe_db(const bfs::path &data_dir, bool wipe_block_log) {
        if (wipe_block_log) {
            ilog("Wiping blockchain with block log.");
        } else {
            ilog("Wiping blockchain.");
        }

        db.wipe(data_dir, shared_memory_dir, wipe_block_log);
        db.open(data_dir, shared_memory_dir, CHAIN_INIT_SUPPLY, shared_memory_size, chainbase::database::read_write/*, validate_invariants*/ );
    };

    void plugin::plugin_impl::replay_db(const bfs::path &data_dir, bool force_replay) {
        if (!force_replay) {
            auto head_block_log = db.get_block_log().head();
            force_replay |= head_block_log && db.revision() >= head_block_log->block_num();
        }

        if (force_replay) {
            wipe_db(data_dir, false);
        }

        auto from_block_num = force_replay ? 1 : db.head_block_num() + 1;

        ilog("Replaying blockchain from block num ${from}.", ("from", from_block_num));
        db.reindex(data_dir, shared_memory_dir, from_block_num, shared_memory_size);
    };

    fc::path plugin::plugin_impl::find_latest_snapshot() {
        if (snapshot_dir.empty()) {
            return fc::path();
        }
        fc::path dir_path(snapshot_dir);
        if (!fc::exists(dir_path) || !fc::is_directory(dir_path)) {
            return fc::path();
        }

        fc::path best_path;
        uint32_t best_block = 0;
        boost::filesystem::directory_iterator end_itr;
        for (boost::filesystem::directory_iterator itr(dir_path); itr != end_itr; ++itr) {
            if (boost::filesystem::is_regular_file(itr->status())) {
                std::string filename = itr->path().filename().string();
                std::string ext = itr->path().extension().string();
                if (ext == ".vizjson" || ext == ".json") {
                    auto pos = filename.find("snapshot-block-");
                    if (pos != std::string::npos) {
                        try {
                            std::string num_str = filename.substr(pos + 15);
                            auto dot_pos = num_str.find('.');
                            if (dot_pos != std::string::npos) num_str = num_str.substr(0, dot_pos);
                            uint32_t block_num = static_cast<uint32_t>(std::stoul(num_str));
                            if (block_num > best_block) {
                                best_block = block_num;
                                best_path = fc::path(itr->path().string());
                            }
                        } catch (...) {}
                    }
                }
            }
        }
        return best_path;
    }

    void plugin::plugin_impl::accept_transaction(const protocol::signed_transaction &trx) {
        uint32_t skip = db.validate_transaction(trx, db.skip_apply_transaction);

        if (single_write_thread) {
            std::promise<bool> promise;
            auto wait = promise.get_future();

            io_service().post([&]{
                try {
                    db.push_transaction(trx, skip);
                    promise.set_value(true);
                } catch(...) {
                    promise.set_exception(std::current_exception());
                }
            });
            wait.get(); // if an exception was, it will be thrown
        } else {
            db.push_transaction(trx, skip);
        }
    }

    plugin::plugin() {
    }

    plugin::~plugin() {
    }

    graphene::chain::database &plugin::db() {
        return my->db;
    }

    const graphene::chain::database &plugin::db() const {
        return my->db;
    }

    bool plugin::is_syncing() const {
        return my->currently_syncing.load(std::memory_order_relaxed);
    }

    void plugin::clear_syncing() {
        if (my->currently_syncing.exchange(false, std::memory_order_relaxed)) {
            ilog("Sync complete: cleared currently_syncing flag (witness block production may resume)");
            my->sync_start_logged = false;
        }
    }

    void plugin::set_program_options(boost::program_options::options_description &cli,
                                     boost::program_options::options_description &cfg) {
        cfg.add_options()
            (
                "shared-file-dir", boost::program_options::value<boost::filesystem::path>()->default_value("state"),
                "the location of the shared memory files (absolute path or relative to application data dir)"
            ) (
                "shared-file-size", boost::program_options::value<std::string>()->default_value("2G"),
                "Start size of the shared memory file. Default: 2G"
            ) (
                "inc-shared-file-size", boost::program_options::value<std::string>()->default_value("2G"),
                "Increasing size on reaching limit of free space in shared memory file (see min-free-shared-file-size). Default: 2G"
            ) (
                "min-free-shared-file-size", boost::program_options::value<std::string>()->default_value("500M"),
                "Minimum free space in shared memory file (see inc-shared-file-size). Default: 500M"
            ) (
                "block-num-check-free-size", boost::program_options::value<uint32_t>()->default_value(1000),
                "Check free space in shared memory each N blocks. Default: 1000 (each 3000 seconds)."
            ) (
                "checkpoint", boost::program_options::value<std::vector<std::string>>()->composing(),
                "Pairs of [BLOCK_NUM,BLOCK_ID] that should be enforced as checkpoints."
            ) (
                "flush-state-interval", boost::program_options::value<uint32_t>(),
                "flush shared memory changes to disk every N blocks"
            ) (
                "read-wait-micro", boost::program_options::value<uint64_t>(),
                "maximum microseconds for trying to get read lock"
            ) (
                "max-read-wait-retries", boost::program_options::value<uint32_t>(),
                "maximum number of retries to get read lock"
            ) (
                "write-wait-micro", boost::program_options::value<uint64_t>(),
                "maximum microseconds for trying to get write lock"
            ) (
                "max-write-wait-retries", boost::program_options::value<uint32_t>(),
                "maximum number of retries to get write lock"
            ) (
                "single-write-thread", boost::program_options::value<bool>()->default_value(false),
                "push blocks and transactions from one thread"
            ) (
                "clear-votes-before-block", boost::program_options::value<uint32_t>()->default_value(0),
                "remove votes before defined block, should speedup initial synchronization"
            ) (
                "skip-virtual-ops", boost::program_options::value<bool>()->default_value(false),
                "virtual operations will not be passed to the plugins, helps to save some memory"
            ) (
                "enable-plugins-on-push-transaction", boost::program_options::value<bool>()->default_value(false),
                "enable calling of plugins for operations on push_transaction"
            ) (
                "dlt-block-log-max-blocks", boost::program_options::value<uint32_t>()->default_value(100000),
                "Number of recent blocks to keep in the DLT rolling block_log (0 = disabled)"
            );
        cli.add_options()
            (
                "replay-blockchain", boost::program_options::bool_switch()->default_value(false),
                "clear chain database and replay all blocks"
            ) (
                "replay-if-corrupted", boost::program_options::bool_switch()->default_value(true),
                "replay all blocks if shared memory is corrupted"
            ) (
                "force-replay-blockchain", boost::program_options::bool_switch()->default_value(false),
                "force clear chain database and replay all blocks"
            ) (
                "replay-from-snapshot", boost::program_options::bool_switch()->default_value(false),
                "recover from corruption: import latest snapshot and replay dlt_block_log"
            ) (
                "auto-recover-from-snapshot", boost::program_options::bool_switch()->default_value(true),
                "automatically recover from shared memory corruption by importing latest snapshot and replaying dlt_block_log (enabled by default)"
            ) (
                "resync-blockchain", boost::program_options::bool_switch()->default_value(false),
                "clear chain database and block log"
            ) (
                "check-locks", boost::program_options::bool_switch()->default_value(false),
                "Check correctness of chainbase locking"
            ) (
                "validate-database-invariants", boost::program_options::bool_switch()->default_value(false),
                "Validate all supply invariants check out"
            );
    }

    void plugin::plugin_initialize(const boost::program_options::variables_map &options) {

        my.reset(new plugin_impl());

        auto sfd = options.at("shared-file-dir").as<boost::filesystem::path>();
        if (sfd.is_relative()) {
            my->shared_memory_dir = appbase::app().data_dir() / sfd;
        } else {
            my->shared_memory_dir = sfd;
        }

        if (options.count("read-wait-micro")) {
            my->read_wait_micro = options.at("read-wait-micro").as<uint64_t>();
        }

        if (options.count("max-read-wait-retries")) {
            my->max_read_wait_retries = options.at("max-read-wait-retries").as<uint32_t>();
        }

        if (options.count("write-wait-micro")) {
            my->write_wait_micro = options.at("write-wait-micro").as<uint64_t>();
        }

        if (options.count("max-write-wait-retries")) {
            my->max_write_wait_retries = options.at("max-write-wait-retries").as<uint32_t>();
        }

        my->single_write_thread = options.at("single-write-thread").as<bool>();

        my->enable_plugins_on_push_transaction = options.at("enable-plugins-on-push-transaction").as<bool>();

        my->shared_memory_size = fc::parse_size(options.at("shared-file-size").as<std::string>());
        my->inc_shared_memory_size = fc::parse_size(options.at("inc-shared-file-size").as<std::string>());
        my->min_free_shared_memory_size = fc::parse_size(options.at("min-free-shared-file-size").as<std::string>());
        my->skip_virtual_ops = options.at("skip-virtual-ops").as<bool>();

        if (options.count("block-num-check-free-size")) {
            my->block_num_check_free_size = options.at("block-num-check-free-size").as<uint32_t>();
        }

        my->replay = options.at("replay-blockchain").as<bool>();
        my->replay_if_corrupted = options.at("replay-if-corrupted").as<bool>();
        my->force_replay = options.at("force-replay-blockchain").as<bool>();
        my->resync = options.at("resync-blockchain").as<bool>();
        my->replay_from_snapshot = options.at("replay-from-snapshot").as<bool>();
        my->auto_recover_from_snapshot = options.at("auto-recover-from-snapshot").as<bool>();
        my->check_locks = options.at("check-locks").as<bool>();
        my->validate_invariants = options.at("validate-database-invariants").as<bool>();
        if (options.count("flush-state-interval")) {
            my->flush_interval = options.at("flush-state-interval").as<uint32_t>();
        } else {
            my->flush_interval = 10000;
        }

        if (options.count("checkpoint")) {
            auto cps = options.at("checkpoint").as<std::vector<std::string>>();
            my->loaded_checkpoints.reserve(cps.size());
            for (const auto &cp : cps) {
                auto item = fc::json::from_string(cp).as<std::pair<uint32_t, protocol::block_id_type>>();
                my->loaded_checkpoints[item.first] = item.second;
            }
        }

        // Check if snapshot plugin has a load path configured
        if (options.count("snapshot")) {
            my->snapshot_path = options.at("snapshot").as<std::string>();
            ilog("Chain plugin: will load state from snapshot: ${p}", ("p", my->snapshot_path));
        }

        // Auto-discover latest snapshot if --snapshot-auto-latest is set and --snapshot is not
        if (my->snapshot_path.empty() && options.count("snapshot-auto-latest") && options.at("snapshot-auto-latest").as<bool>()) {
            std::string snap_dir = options.count("snapshot-dir") ? options.at("snapshot-dir").as<std::string>() : "";
            // If snapshot-dir is not set, default to <data_dir>/snapshots
            if (snap_dir.empty()) {
                snap_dir = (appbase::app().data_dir() / "snapshots").string();
            }
            my->snapshot_dir = snap_dir;
            fc::path dir_path(snap_dir);
            if (fc::exists(dir_path) && fc::is_directory(dir_path)) {
                fc::path best_path;
                uint32_t best_block = 0;
                boost::filesystem::directory_iterator end_itr;
                for (boost::filesystem::directory_iterator itr(dir_path); itr != end_itr; ++itr) {
                    if (boost::filesystem::is_regular_file(itr->status())) {
                        std::string filename = itr->path().filename().string();
                        std::string ext = itr->path().extension().string();
                        if (ext == ".vizjson" || ext == ".json") {
                            auto pos = filename.find("snapshot-block-");
                            if (pos != std::string::npos) {
                                try {
                                    std::string num_str = filename.substr(pos + 15);
                                    auto dot_pos = num_str.find('.');
                                    if (dot_pos != std::string::npos) num_str = num_str.substr(0, dot_pos);
                                    uint32_t block_num = static_cast<uint32_t>(std::stoul(num_str));
                                    if (block_num > best_block) {
                                        best_block = block_num;
                                        best_path = fc::path(itr->path().string());
                                    }
                                } catch (...) {}
                            }
                        }
                    }
                }
                if (!best_path.string().empty()) {
                    my->snapshot_path = best_path.string();
                    ilog("Chain plugin: auto-discovered latest snapshot: ${p}", ("p", my->snapshot_path));
                } else {
                    wlog("Chain plugin: --snapshot-auto-latest but no snapshots found in ${d}", ("d", snap_dir));
                }
            }
        }

        // DLT rolling block_log config
        if (options.count("dlt-block-log-max-blocks")) {
            my->db._dlt_block_log_max_blocks = options.at("dlt-block-log-max-blocks").as<uint32_t>();
        }

        // Ensure snapshot_dir is always resolved for auto-recovery even if --snapshot-auto-latest not set
        if (my->snapshot_dir.empty()) {
            std::string sd = options.count("snapshot-dir") ? options.at("snapshot-dir").as<std::string>() : "";
            if (sd.empty()) {
                sd = (appbase::app().data_dir() / "snapshots").string();
            }
            my->snapshot_dir = sd;
        }
    }

    void plugin::plugin_startup() {
        ilog("Starting chain with shared_file_size: ${n} bytes", ("n", my->shared_memory_size));

        auto data_dir = appbase::app().data_dir() / "state";

        if (my->resync) {
            wlog("resync requested: deleting block log and shared memory");
            my->db.wipe(data_dir, my->shared_memory_dir, true);
        }

        my->db.set_flush_interval(my->flush_interval);
        my->db.add_checkpoints(my->loaded_checkpoints);
        my->db.set_require_locking(my->check_locks);

        my->db.set_read_wait_micro(my->read_wait_micro);
        my->db.set_max_read_wait_retries(my->max_read_wait_retries);
        my->db.set_write_wait_micro(my->write_wait_micro);
        my->db.set_max_write_wait_retries(my->max_write_wait_retries);

        my->db.set_inc_shared_memory_size(my->inc_shared_memory_size);
        my->db.set_min_free_shared_memory_size(my->min_free_shared_memory_size);

        if(my->skip_virtual_ops) {
            my->db.set_skip_virtual_ops();
        }

        if (my->block_num_check_free_size) {
            my->db.set_block_num_check_free_size(my->block_num_check_free_size);
        }

        my->db.enable_plugins_on_push_transaction(my->enable_plugins_on_push_transaction);

        // ========== Snapshot loading path ==========
        if (!my->snapshot_path.empty() && !my->replay_from_snapshot) {
            // Check if shared_memory already has state from a previous run.
            // If so, skip snapshot import and use normal open path.
            // This prevents re-importing an old snapshot on container restart
            // when --snapshot is still in the command line (e.g. VIZD_EXTRA_OPTS).
            auto shm_path = my->shared_memory_dir / "shared_memory.bin";
            if (boost::filesystem::exists(shm_path) && boost::filesystem::file_size(shm_path) > 0) {
                wlog("Shared memory already exists at ${p}. Skipping snapshot import, using normal startup.",
                     ("p", shm_path.string()));
                wlog("To force re-import, delete shared_memory first (--resync-blockchain) or remove the shared_memory file.");
            } else if (!boost::filesystem::exists(my->snapshot_path)) {
                // Snapshot file not found -- maybe it was already consumed (.used) or path is wrong.
                // Fall through to normal startup instead of wiping state and failing.
                wlog("Snapshot file not found: ${p}. Skipping snapshot import, using normal startup.",
                     ("p", my->snapshot_path));
            } else if (snapshot_load_callback) {
                // Callback is already registered (snapshot plugin initialized before us) — proceed now.
                do_snapshot_load(data_dir, false);
                return;
            } else {
                // Snapshot plugin hasn't registered its callback yet — defer until
                // snapshot plugin calls trigger_snapshot_load().
                ilog("Snapshot path configured (${p}) but snapshot plugin callback not yet registered. "
                     "Deferring snapshot load until snapshot plugin is ready.", ("p", my->snapshot_path));
                my->pending_snapshot_load = true;
                return;
            }
        }

        // ========== Replay from snapshot + dlt_block_log path ==========
        if (my->replay_from_snapshot) {
            if (my->snapshot_path.empty()) {
                elog("--replay-from-snapshot requires a snapshot path (--snapshot or --snapshot-auto-latest)");
                throw std::runtime_error("--replay-from-snapshot requires --snapshot or --snapshot-auto-latest");
            }

            if (!boost::filesystem::exists(my->snapshot_path)) {
                elog("Snapshot file not found: ${p}", ("p", my->snapshot_path));
                throw std::runtime_error("Snapshot file not found for --replay-from-snapshot");
            }

            if (snapshot_load_callback) {
                // Callback is already registered — proceed now.
                do_snapshot_load(data_dir, true);
                return;
            } else {
                // Snapshot plugin hasn't registered its callback yet — defer.
                ilog("Replay-from-snapshot configured but snapshot plugin callback not yet registered. "
                     "Deferring until snapshot plugin is ready.");
                my->pending_snapshot_load = true;
                return;
            }
        }

        // ========== Normal startup path ==========
        try {
            ilog("Opening shared memory from ${path}", ("path", my->shared_memory_dir.generic_string()));
            my->db.open(data_dir, my->shared_memory_dir, CHAIN_INIT_SUPPLY, my->shared_memory_size, chainbase::database::read_write/*, my->validate_invariants*/ );
            auto head_block_log = my->db.get_block_log().head();
            my->replay |= head_block_log && my->db.revision() != head_block_log->block_num();

            if (my->replay) {
                my->replay_db(data_dir, my->force_replay);
            }
        } catch (const graphene::chain::database_revision_exception &) {
            if (my->auto_recover_from_snapshot && snapshot_load_callback) {
                wlog("Shared memory corrupted (revision mismatch). Attempting automatic recovery from snapshot...");
                fc::path snap = my->find_latest_snapshot();
                if (!snap.string().empty()) {
                    wlog("Auto-recovery: found snapshot ${p}. Wiping shared memory and importing...", ("p", snap.string()));
                    my->snapshot_path = snap.string();
                    do_snapshot_load(data_dir, true);
                    return;
                } else {
                    wlog("Auto-recovery: no snapshots found in ${d}. Falling back to replay.", ("d", my->snapshot_dir));
                }
            }
            if (my->replay_if_corrupted) {
                wlog("Error opening database, attempting to replay blockchain.");
                my->force_replay |= my->db.revision() >= my->db.head_block_num();
                try {
                    my->replay_db(data_dir, my->force_replay);
                } catch (const graphene::chain::block_log_exception &) {
                    wlog("Error opening block log. Having to resync from network...");
                    my->wipe_db(data_dir, true);
                }
            } else {
                wlog("Error opening database, quiting. If should replay, set replay-if-corrupted in config.ini to true.");
                std::exit(0); // TODO Migrate to appbase::app().quit()
                return;
            }
        } catch (...) {
            if (my->auto_recover_from_snapshot && snapshot_load_callback) {
                wlog("Shared memory corrupted (open failed). Attempting automatic recovery from snapshot...");
                fc::path snap = my->find_latest_snapshot();
                if (!snap.string().empty()) {
                    wlog("Auto-recovery: found snapshot ${p}. Wiping shared memory and importing...", ("p", snap.string()));
                    my->snapshot_path = snap.string();
                    do_snapshot_load(data_dir, true);
                    return;
                } else {
                    wlog("Auto-recovery: no snapshots found in ${d}. Falling back to replay.", ("d", my->snapshot_dir));
                }
            }
            if (my->replay_if_corrupted) {
                wlog("Error opening database, attempting to replay blockchain.");
                try {
                    my->replay_db(data_dir, true);
                } catch (const graphene::chain::block_log_exception &) {
                    wlog("Error opening block log. Having to resync from network...");
                    my->wipe_db(data_dir, true);
                }
            } else {
                wlog("Error opening database, quiting. If should replay, set replay-if-corrupted in config.ini to true.");
                std::exit(0); // TODO Migrate to appbase::app().quit()
                return;
            }
        }

        ilog("Started on blockchain with ${n} blocks", ("n", my->db.head_block_num()));

        // If --create-snapshot callback is registered, create snapshot and quit
        // BEFORE on_sync() — so P2P/witness never start.
        if (snapshot_create_callback) {
            snapshot_create_callback();
            return;
        }

        // If state is empty and P2P snapshot sync callback is registered,
        // download and load snapshot from trusted peers before on_sync().
        if (my->db.head_block_num() == 0 && snapshot_p2p_sync_callback) {
            std::cerr << "   Node has no state (0 blocks). Requesting snapshot from trusted peers...\n";
            ilog("Node has no state. Triggering P2P snapshot sync from trusted peers...");
            try {
                snapshot_p2p_sync_callback();
            } catch (const fc::exception& e) {
                elog("FATAL: P2P snapshot sync failed: ${e}", ("e", e.to_detail_string()));
                std::cerr << "   FATAL: P2P snapshot sync failed: " << e.what() << "\n";
                my->wipe_db(data_dir, false);
                appbase::app().quit();
                return;
            } catch (const std::exception& e) {
                elog("FATAL: P2P snapshot sync failed: ${e}", ("e", e.what()));
                std::cerr << "   FATAL: P2P snapshot sync failed: " << e.what() << "\n";
                my->wipe_db(data_dir, false);
                appbase::app().quit();
                return;
            } catch (...) {
                elog("FATAL: P2P snapshot sync failed: unknown exception");
                std::cerr << "   FATAL: P2P snapshot sync failed: unknown exception\n";
                my->wipe_db(data_dir, false);
                appbase::app().quit();
                return;
            }
            std::cerr << "   P2P snapshot sync complete. Started on blockchain with "
                      << my->db.head_block_num() << " blocks\n";
            ilog("Started on blockchain with ${n} blocks (from P2P snapshot sync)", ("n", my->db.head_block_num()));
        } else if (my->db.head_block_num() == 0 && !snapshot_p2p_sync_callback) {
            wlog("Node has no state (0 blocks) and no P2P snapshot sync configured.");
            wlog("Will sync from genesis via P2P (this will be very slow for mature chains).");
            wlog("To bootstrap from a trusted peer, configure 'trusted-snapshot-peer' in config.ini.");
            std::cerr << "   WARNING: Node has 0 blocks and no snapshot sync configured.\n";
            std::cerr << "   Will sync from genesis via P2P (very slow for mature chains).\n";
            std::cerr << "   Add 'trusted-snapshot-peer = <ip>:<port>' to config.ini for fast bootstrap.\n";
        }

        on_sync();
    }

    void plugin::plugin_shutdown() {
        ilog("closing chain database");
        my->db.close();
        ilog("database closed successfully");
    }

    void plugin::do_snapshot_load(const bfs::path& data_dir, bool is_recovery) {
        if (is_recovery) {
            ilog("RECOVERY MODE: replaying from snapshot + dlt_block_log...");
        } else {
            ilog("Opening database in snapshot mode...");
        }

        // Always wipe and re-import for recovery; for first-time load, open fresh.
        try {
            my->db.open_from_snapshot(
                data_dir,
                my->shared_memory_dir,
                CHAIN_INIT_SUPPLY,
                my->shared_memory_size,
                chainbase::database::read_write);

            ilog("Database opened for snapshot import. Loading snapshot state...");
        } catch (const fc::exception& e) {
            elog("Failed to open database for snapshot: ${e}", ("e", e.to_detail_string()));
            throw;
        }

        // Load snapshot state via callback (set by snapshot plugin during initialize)
        // This MUST happen before on_sync() so that P2P starts syncing from the
        // snapshot head block, not from genesis.
        if (snapshot_load_callback) {
            try {
                snapshot_load_callback();
            } catch (const fc::exception& e) {
                elog("FATAL: Failed to load snapshot: ${e}", ("e", e.to_detail_string()));
                if (!is_recovery) {
                    elog("The snapshot file may be corrupted or incompatible. "
                         "Check the file path and try again.");
                }
                my->wipe_db(data_dir, false);
                appbase::app().quit();
                return;
            } catch (const std::exception& e) {
                elog("FATAL: Failed to load snapshot: ${e}", ("e", e.what()));
                my->wipe_db(data_dir, false);
                appbase::app().quit();
                return;
            } catch (...) {
                elog("FATAL: Failed to load snapshot: unknown exception");
                my->wipe_db(data_dir, false);
                appbase::app().quit();
                return;
            }
        } else {
            elog("Snapshot load callback not registered. "
                 "Add 'plugin = snapshot' to config.ini or pass --plugin snapshot on the command line.");
            throw std::runtime_error("Snapshot plugin not configured");
        }

        // Recovery mode: replay dlt_block_log on top of snapshot
        if (is_recovery) {
            uint32_t snapshot_head = my->db.head_block_num();
            ilog("Snapshot loaded at block ${n}. Initializing hardforks...", ("n", snapshot_head));
            my->db.initialize_hardforks();

            // Replay blocks from dlt_block_log if available
            const auto& dlt_head = my->db.get_dlt_block_log().head();
            if (dlt_head && dlt_head->block_num() > snapshot_head) {
                ilog("Replaying dlt_block_log from block ${from} to ${to}...",
                     ("from", snapshot_head + 1)("to", dlt_head->block_num()));
                try {
                    my->db.reindex_from_dlt(snapshot_head + 1);
                } catch (const fc::exception& e) {
                    elog("Failed to replay dlt_block_log: ${e}", ("e", e.to_detail_string()));
                    elog("Node will continue with snapshot state only. P2P sync will fill the gap.");
                }
            } else {
                wlog("No dlt_block_log blocks beyond snapshot head. P2P sync will fill the gap.");
            }

            ilog("Recovery complete. Started on blockchain with ${n} blocks", ("n", my->db.head_block_num()));
        } else {
            // Normal snapshot load: rename file to .used
            try {
                std::string used_path = my->snapshot_path + ".used";
                boost::filesystem::rename(my->snapshot_path, used_path);
                ilog("Snapshot file renamed to ${p}", ("p", used_path));
            } catch (const std::exception& e) {
                wlog("Could not rename snapshot file to .used: ${e}", ("e", e.what()));
            }

            ilog("Started on blockchain with ${n} blocks (from snapshot)", ("n", my->db.head_block_num()));
        }

        // During auto-recovery, on_sync() must NOT fire again —
        // webserver/P2P plugins are already running and calling
        // start_webserver() twice destroys joinable threads (std::terminate).
        if (!is_recovery) {
            on_sync();
        }
    }

    void plugin::trigger_snapshot_load() {
        if (!my->pending_snapshot_load) {
            return; // Nothing deferred — either already loaded or not needed
        }

        ilog("Snapshot plugin is ready. Resuming deferred snapshot load...");
        my->pending_snapshot_load = false;

        auto data_dir = appbase::app().data_dir() / "state";

        if (my->replay_from_snapshot) {
            do_snapshot_load(data_dir, true);
        } else {
            do_snapshot_load(data_dir, false);
        }
    }

    bool plugin::accept_block(const protocol::signed_block &block, bool currently_syncing, uint32_t skip) {
        try {
            return my->accept_block(block, currently_syncing, skip);
        } catch (const graphene::chain::shared_memory_corruption_exception& e) {
            elog("Shared memory corruption detected during block processing: ${e}", ("e", e.to_detail_string()));
            if (my->auto_recover_from_snapshot) {
                attempt_auto_recovery();
            } else {
                elog("Auto-recovery disabled. Restart with --replay-from-snapshot --snapshot-auto-latest");
                appbase::app().quit();
            }
            return false;
        }
    }

    void plugin::attempt_auto_recovery() {
        wlog("=== IMMEDIATE AUTO-RECOVERY: shared memory corruption detected ===");

        // 1. Find latest snapshot
        fc::path snap = my->find_latest_snapshot();
        if (snap.string().empty()) {
            elog("Auto-recovery FAILED: no snapshots found in ${d}. "
                 "Restart manually with --replay-from-snapshot --snapshot-auto-latest",
                 ("d", my->snapshot_dir));
            appbase::app().quit();
            return;
        }

        if (!snapshot_load_callback) {
            elog("Auto-recovery FAILED: snapshot plugin not configured. "
                 "Add 'plugin = snapshot' to config.ini");
            appbase::app().quit();
            return;
        }

        wlog("Auto-recovery: closing database and recovering from snapshot ${p}...", ("p", snap.string()));

        // 2. Close current (corrupted) database
        try {
            my->db.close(false); // close without rewind — state is corrupted anyway
        } catch (...) {
            wlog("Auto-recovery: ignoring error during database close (state is corrupted)");
        }

        // 3. Set snapshot path and trigger full recovery (wipe + import + dlt replay)
        my->snapshot_path = snap.string();
        auto data_dir = appbase::app().data_dir() / "state";

        try {
            do_snapshot_load(data_dir, true);
            wlog("=== AUTO-RECOVERY COMPLETE: node resumed at block ${n} ===",
                 ("n", my->db.head_block_num()));
        } catch (const fc::exception& e) {
            elog("Auto-recovery FAILED during snapshot load: ${e}", ("e", e.to_detail_string()));
            appbase::app().quit();
        } catch (const std::exception& e) {
            elog("Auto-recovery FAILED during snapshot load: ${e}", ("e", e.what()));
            appbase::app().quit();
        }
    }

    void plugin::wipe_state() {
        auto data_dir = appbase::app().data_dir() / "state";
        my->wipe_db(data_dir, false);
    }

    void plugin::accept_transaction(const protocol::signed_transaction &trx) {
        my->accept_transaction(trx);
    }

    bool plugin::block_is_on_preferred_chain(const protocol::block_id_type &block_id) {
        // If it's not known, it's not preferred.
        if (!db().is_known_block(block_id)) {
            return false;
        }

        // Extract the block number from block_id, and fetch that block number's ID from the database.
        // If the database's block ID matches block_id, then block_id is on the preferred chain. Otherwise, it's on a fork.
        return db().get_block_id_for_num(protocol::block_header::num_from_id(block_id)) == block_id;
    }

    void plugin::check_time_in_block(const protocol::signed_block &block) {
        my->check_time_in_block(block);
    }

}
}
} // namespace graphene::plugis::chain::chain_apis
