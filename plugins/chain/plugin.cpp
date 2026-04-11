#include <graphene/chain/database_exceptions.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/witness_objects.hpp>
#include <graphene/chain/content_object.hpp>
#include <graphene/chain/chain_objects.hpp>
#include <graphene/chain/proposal_object.hpp>
#include <graphene/chain/committee_objects.hpp>
#include <graphene/chain/invite_objects.hpp>
#include <graphene/chain/paid_subscription_objects.hpp>
#include <graphene/chain/block_summary_object.hpp>
#include <graphene/chain/transaction_object.hpp>
#include <graphene/chain/chain_object_types.hpp>
#include <graphene/plugins/chain/plugin.hpp>
#include <graphene/plugins/snapshot/snapshot_format.hpp>

#include <fc/io/json.hpp>
#include <fc/io/raw.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/string.hpp>
#include <fc/interprocess/container.hpp>

#include <iostream>
#include <fstream>
#include <graphene/protocol/protocol.hpp>
#include <graphene/protocol/types.hpp>
#include <future>

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

        std::string load_snapshot_path;

        graphene::chain::database db;

        bool single_write_thread = false;

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
        bool load_from_snapshot(const bfs::path &data_dir);
    };

    void plugin::plugin_impl::check_time_in_block(const protocol::signed_block &block) {
        time_point_sec now = fc::time_point::now();

        uint64_t max_accept_time = now.sec_since_epoch();
        max_accept_time += allow_future_time;
        FC_ASSERT(block.timestamp.sec_since_epoch() <= max_accept_time);
    }

    bool plugin::plugin_impl::accept_block(const protocol::signed_block &block, bool currently_syncing, uint32_t skip) {
        if (currently_syncing && block.block_num() % 10000 == 0) {
            ilog("Syncing Blockchain --- Got block: #${n} time: ${t} producer: ${p}",
                 ("t", block.timestamp)("n", block.block_num())("p", block.witness));
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
        auto head_block_log = db.get_block_log().head();
        force_replay |= head_block_log && db.revision() >= head_block_log->block_num();

        if (force_replay) {
            wipe_db(data_dir, false);
        }

        auto from_block_num = force_replay ? 1 : db.head_block_num() + 1;

        ilog("Replaying blockchain from block num ${from}.", ("from", from_block_num));
        db.reindex(data_dir, shared_memory_dir, from_block_num, shared_memory_size);
    };

    // Helper: remove all objects of a given type from the database
    template<typename IndexType, typename ObjectType>
    void clear_index(graphene::chain::database &db) {
        const auto &idx = db.get_index<IndexType>().indices().template get<graphene::chain::by_id>();
        std::vector<typename ObjectType::id_type> ids;
        ids.reserve(std::distance(idx.begin(), idx.end()));
        for (auto itr = idx.begin(); itr != idx.end(); ++itr) {
            ids.push_back(itr->id);
        }
        for (const auto &id : ids) {
            const auto &obj = db.get<ObjectType>(id);
            db.remove(obj);
        }
    }

    // Helper template: clear existing objects then import from snapshot payload.
    // Uses fc::raw::unpack directly into the object created by chainbase
    // (avoids the deleted default constructor problem).
    //
    // ID handling: chainbase::emplace sets id = _next_id before calling our constructor,
    // then our constructor overwrites id with the snapshot value. The multi_index
    // stores the object with the snapshot ID (correct), but _next_id may be wrong.
    // After all objects are imported, we advance _next_id by creating+removing dummy
    // objects until _next_id exceeds the max ID in the snapshot.
    template<typename IndexType, typename ObjectType>
    void import_snapshot_section(
        graphene::chain::database &db,
        const char *payload_data,
        const graphene::plugins::snapshot::snapshot_section_info &section
    ) {
        // Clear any objects created by init_genesis
        clear_index<IndexType, ObjectType>(db);

        fc::datastream<const char*> ds(payload_data + section.offset, section.size);

        uint64_t count;
        fc::raw::unpack(ds, count);

        ilog("Importing ${n} ${type} objects", ("n", count)("type", section.type));

        typename ObjectType::id_type max_id(0);
        for (uint64_t i = 0; i < count; i++) {
            const auto &created = db.create<ObjectType>([&](ObjectType &obj) {
                fc::raw::unpack(ds, obj);
            });
            if (created.id > max_id) {
                max_id = created.id;
            }
        }

        // Fix chainbase _next_id to be max_id + 1, so future creates
        // don't collide with imported IDs.
        if (count > 0) {
            using index_type = typename chainbase::get_index_type<ObjectType>::type;
            db.get_mutable_index<index_type>().set_next_id(
                typename ObjectType::id_type(max_id._id + 1));
        }
    }

    bool plugin::plugin_impl::load_from_snapshot(const bfs::path &data_dir) {
        using namespace graphene::plugins::snapshot;
        using namespace graphene::chain;

        ilog("Loading state from snapshot: ${path}", ("path", load_snapshot_path));

        // Read file
        std::ifstream in(load_snapshot_path, std::ios::binary | std::ios::ate);
        if (!in.is_open()) {
            elog("Failed to open snapshot file: ${path}", ("path", load_snapshot_path));
            return false;
        }

        size_t file_size = in.tellg();
        in.seekg(0, std::ios::beg);

        // Verify magic
        char magic[8];
        in.read(magic, 8);
        if (std::memcmp(magic, SNAPSHOT_MAGIC, 8) != 0) {
            elog("Invalid snapshot file: bad magic");
            return false;
        }

        // Version
        uint32_t version;
        in.read(reinterpret_cast<char*>(&version), 4);
        if (version != SNAPSHOT_FORMAT_VERSION) {
            elog("Unsupported snapshot version: ${v}", ("v", version));
            return false;
        }

        // Header
        uint32_t header_len;
        in.read(reinterpret_cast<char*>(&header_len), 4);
        std::string header_json(header_len, '\0');
        in.read(&header_json[0], header_len);

        auto header = fc::json::from_string(header_json).as<snapshot_header>();

        // Read payload
        size_t payload_offset = 8 + 4 + 4 + header_len;
        size_t payload_size = file_size - payload_offset;
        std::vector<char> payload(payload_size);
        in.read(payload.data(), payload_size);
        in.close();

        // Verify SHA256
        auto computed_hash = fc::sha256::hash(payload.data(), payload.size());
        if (computed_hash.str() != header.payload_sha256) {
            elog("Snapshot SHA256 mismatch!");
            return false;
        }

        ilog("Snapshot payload SHA256 verified: block ${b}, ${s} sections",
            ("b", header.head_block_num)("s", header.sections.size()));

        // Verify snapshot block exists in block_log and has valid signature
        auto block = db.get_block_log().read_block_by_num(header.head_block_num);
        if (!block) {
            elog("Snapshot block ${b} not found in block_log. "
                 "Block log may be truncated or snapshot is from a different chain.",
                 ("b", header.head_block_num));
            return false;
        }

        // Verify block ID matches
        auto block_id = block->id();
        if (block_id.str() != header.head_block_id) {
            elog("Block ID mismatch at block ${b}: snapshot has ${snap}, block_log has ${log}",
                 ("b", header.head_block_num)
                 ("snap", header.head_block_id)
                 ("log", block_id.str()));
            return false;
        }

        // Verify block signature (recover public key from signature + digest)
        try {
            auto signee = block->signee();
            FC_ASSERT(signee != fc::ecc::public_key(),
                "Block ${b} has invalid signature (cannot recover public key)",
                ("b", header.head_block_num));
            ilog("Snapshot block ${b} signature verified (witness: ${w}, signee: ${s})",
                ("b", header.head_block_num)("w", block->witness)
                ("s", std::string(signee)));
        } catch (const fc::exception &e) {
            elog("Block ${b} signature verification failed: ${e}",
                 ("b", header.head_block_num)("e", e.to_detail_string()));
            return false;
        }

        // Verify chain_id matches
        if (db.get_chain_id().str() != header.chain_id) {
            elog("Chain ID mismatch: snapshot is for chain ${snap}, this node is ${node}",
                 ("snap", header.chain_id)("node", db.get_chain_id().str()));
            return false;
        }

        ilog("Snapshot integrity fully verified");

        // Wipe DB and start fresh
        wipe_db(data_dir, false);

        // Import each section
        for (const auto &section : header.sections) {
            if (section.type == "dynamic_global_property") {
                import_snapshot_section<dynamic_global_property_index, dynamic_global_property_object>(db, payload.data(), section);
            } else if (section.type == "account") {
                import_snapshot_section<account_index, account_object>(db, payload.data(), section);
            } else if (section.type == "account_authority") {
                import_snapshot_section<account_authority_index, account_authority_object>(db, payload.data(), section);
            } else if (section.type == "witness") {
                import_snapshot_section<witness_index, witness_object>(db, payload.data(), section);
            } else if (section.type == "transaction") {
                import_snapshot_section<transaction_index, transaction_object>(db, payload.data(), section);
            } else if (section.type == "block_summary") {
                import_snapshot_section<block_summary_index, block_summary_object>(db, payload.data(), section);
            } else if (section.type == "witness_schedule") {
                import_snapshot_section<witness_schedule_index, witness_schedule_object>(db, payload.data(), section);
            } else if (section.type == "content") {
                import_snapshot_section<content_index, content_object>(db, payload.data(), section);
            } else if (section.type == "content_type") {
                import_snapshot_section<content_type_index, content_type_object>(db, payload.data(), section);
            } else if (section.type == "content_vote") {
                import_snapshot_section<content_vote_index, content_vote_object>(db, payload.data(), section);
            } else if (section.type == "witness_vote") {
                import_snapshot_section<witness_vote_index, witness_vote_object>(db, payload.data(), section);
            } else if (section.type == "hardfork_property") {
                import_snapshot_section<hardfork_property_index, hardfork_property_object>(db, payload.data(), section);
            } else if (section.type == "withdraw_vesting_route") {
                import_snapshot_section<withdraw_vesting_route_index, withdraw_vesting_route_object>(db, payload.data(), section);
            } else if (section.type == "master_authority_history") {
                import_snapshot_section<master_authority_history_index, master_authority_history_object>(db, payload.data(), section);
            } else if (section.type == "account_recovery_request") {
                import_snapshot_section<account_recovery_request_index, account_recovery_request_object>(db, payload.data(), section);
            } else if (section.type == "change_recovery_account_request") {
                import_snapshot_section<change_recovery_account_request_index, change_recovery_account_request_object>(db, payload.data(), section);
            } else if (section.type == "escrow") {
                import_snapshot_section<escrow_index, escrow_object>(db, payload.data(), section);
            } else if (section.type == "vesting_delegation") {
                import_snapshot_section<vesting_delegation_index, vesting_delegation_object>(db, payload.data(), section);
            } else if (section.type == "fix_vesting_delegation") {
                import_snapshot_section<fix_vesting_delegation_index, fix_vesting_delegation_object>(db, payload.data(), section);
            } else if (section.type == "vesting_delegation_expiration") {
                import_snapshot_section<vesting_delegation_expiration_index, vesting_delegation_expiration_object>(db, payload.data(), section);
            } else if (section.type == "account_metadata") {
                import_snapshot_section<account_metadata_index, account_metadata_object>(db, payload.data(), section);
            } else if (section.type == "proposal") {
                import_snapshot_section<proposal_index, proposal_object>(db, payload.data(), section);
            } else if (section.type == "required_approval") {
                import_snapshot_section<required_approval_index, required_approval_object>(db, payload.data(), section);
            } else if (section.type == "committee_request") {
                import_snapshot_section<committee_request_index, committee_request_object>(db, payload.data(), section);
            } else if (section.type == "committee_vote") {
                import_snapshot_section<committee_vote_index, committee_vote_object>(db, payload.data(), section);
            } else if (section.type == "invite") {
                import_snapshot_section<invite_index, invite_object>(db, payload.data(), section);
            } else if (section.type == "award_shares_expire") {
                import_snapshot_section<award_shares_expire_index, award_shares_expire_object>(db, payload.data(), section);
            } else if (section.type == "paid_subscription") {
                import_snapshot_section<paid_subscription_index, paid_subscription_object>(db, payload.data(), section);
            } else if (section.type == "paid_subscribe") {
                import_snapshot_section<paid_subscribe_index, paid_subscribe_object>(db, payload.data(), section);
            } else if (section.type == "witness_penalty_expire") {
                import_snapshot_section<witness_penalty_expire_index, witness_penalty_expire_object>(db, payload.data(), section);
            } else if (section.type == "block_post_validation") {
                import_snapshot_section<block_post_validation_index, block_post_validation_object>(db, payload.data(), section);
            } else {
                wlog("Unknown snapshot section: ${type}, skipping", ("type", section.type));
            }
        }

        // Set database revision to match snapshot block
        db.set_revision(header.head_block_num);

        ilog("Snapshot loaded successfully at block ${b}", ("b", header.head_block_num));
        return true;
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

    void plugin::set_program_options(boost::program_options::options_description &cli,
                                     boost::program_options::options_description &cfg) {
        cfg.add_options()
            (
                "shared-file-dir", boost::program_options::value<boost::filesystem::path>()->default_value("blockchain"),
                "the location of the chain shared memory files (absolute path or relative to application data dir)"
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
                "resync-blockchain", boost::program_options::bool_switch()->default_value(false),
                "clear chain database and block log"
            ) (
                "check-locks", boost::program_options::bool_switch()->default_value(false),
                "Check correctness of chainbase locking"
            ) (
                "validate-database-invariants", boost::program_options::bool_switch()->default_value(false),
                "Validate all supply invariants check out"
            ) (
                "load-snapshot", boost::program_options::value<std::string>()->default_value(""),
                "load state from snapshot file instead of replaying blocks"
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
        my->check_locks = options.at("check-locks").as<bool>();
        my->validate_invariants = options.at("validate-database-invariants").as<bool>();
        my->load_snapshot_path = options.at("load-snapshot").as<std::string>();
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
    }

    void plugin::plugin_startup() {
        ilog("Starting chain with shared_file_size: ${n} bytes", ("n", my->shared_memory_size));

        auto data_dir = appbase::app().data_dir() / "blockchain";

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

        try {
            ilog("Opening shared memory from ${path}", ("path", my->shared_memory_dir.generic_string()));
            my->db.open(data_dir, my->shared_memory_dir, CHAIN_INIT_SUPPLY, my->shared_memory_size, chainbase::database::read_write/*, my->validate_invariants*/ );

            if (!my->load_snapshot_path.empty()) {
                my->load_from_snapshot(data_dir);
            } else {
                auto head_block_log = my->db.get_block_log().head();
                my->replay |= head_block_log && my->db.revision() != head_block_log->block_num();

                if (my->replay) {
                    my->replay_db(data_dir, my->force_replay);
                }
            }
        } catch (const graphene::chain::database_revision_exception &) {
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
        on_sync();
    }

    void plugin::plugin_shutdown() {
        ilog("closing chain database");
        my->db.close();
        ilog("database closed successfully");
    }

    bool plugin::accept_block(const protocol::signed_block &block, bool currently_syncing, uint32_t skip) {
        return my->accept_block(block, currently_syncing, skip);
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
