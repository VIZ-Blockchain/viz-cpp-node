#include <graphene/plugins/snapshot/chainbase_raw.hpp>
#include <graphene/plugins/snapshot/plugin.hpp>
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
#include <graphene/protocol/types.hpp>
#include <graphene/plugins/json_rpc/utility.hpp>
#include <graphene/plugins/json_rpc/plugin.hpp>

#include <fc/io/fstream.hpp>
#include <fc/io/raw.hpp>
#include <fc/crypto/sha256.hpp>
#include <fc/interprocess/container.hpp>

#include <boost/asio/signal_set.hpp>

#include <fstream>
#include <csignal>
#include <atomic>

namespace graphene {
namespace plugins {
namespace snapshot {

using namespace graphene::chain;

// Helper: export one index section to a buffer, returning section info
template<typename IndexType>
snapshot_section_info export_section(
    database &db,
    std::vector<char> &payload,
    const std::string &type_name
) {
    snapshot_section_info info;
    info.type = type_name;
    info.offset = payload.size();

    const auto &idx = db.get_index<IndexType>().indices();
    info.count = std::distance(idx.begin(), idx.end());

    // Pack count
    auto count_size = fc::raw::pack_size(uint64_t(info.count));
    size_t old_size = payload.size();
    payload.resize(old_size + count_size);
    fc::datastream<char*> count_ds(payload.data() + old_size, count_size);
    fc::raw::pack(count_ds, uint64_t(info.count));

    // Pack each object
    for (const auto &obj : idx) {
        auto obj_size = fc::raw::pack_size(obj);
        old_size = payload.size();
        payload.resize(old_size + obj_size);
        fc::datastream<char*> obj_ds(payload.data() + old_size, obj_size);
        fc::raw::pack(obj_ds, obj);
    }

    info.size = payload.size() - info.offset;
    return info;
}

struct plugin::plugin_impl {
public:
    plugin_impl()
        : chain_plugin_(appbase::app().get_plugin<plugins::chain::plugin>()),
          db_(chain_plugin_.db()) {
    }

    snapshot_export_r do_export(const std::string &path);
    snapshot_info_r do_info(const std::string &path);
    snapshot_verify_r do_verify(const std::string &path);

    void on_applied_block(const protocol::signed_block &b);
    void trigger_export();
    std::string generate_snapshot_path();
    void register_sigusr1_handler();

    graphene::chain::database &database() {
        return db_;
    }

    plugins::chain::plugin &chain() {
        return chain_plugin_;
    }

    // Config
    uint32_t snapshot_every = 0;        // 0 = disabled
    std::string snapshot_dir;

    // State
    boost::signals2::scoped_connection applied_block_conn_;
    std::shared_ptr<boost::asio::signal_set> sigusr1_set_;
    std::atomic<bool> export_requested_{false};
    std::atomic<bool> export_in_progress_{false};

private:
    plugins::chain::plugin &chain_plugin_;
    graphene::chain::database &db_;
};

std::string plugin::plugin_impl::generate_snapshot_path() {
    auto &db = database();
    const auto &dgpo = db.get_dynamic_global_properties();
    std::string filename = "snapshot-block-" + std::to_string(dgpo.head_block_number) + ".bin";
    if (snapshot_dir.empty()) {
        return (appbase::app().data_dir() / filename).string();
    }
    boost::filesystem::path dir(snapshot_dir);
    if (!boost::filesystem::exists(dir)) {
        boost::filesystem::create_directories(dir);
    }
    return (dir / filename).string();
}

void plugin::plugin_impl::register_sigusr1_handler() {
    sigusr1_set_->async_wait([this](const boost::system::error_code &err, int) {
        if (err) return;
        ilog("snapshot: SIGUSR1 received, scheduling export on next block");
        export_requested_ = true;
        register_sigusr1_handler(); // re-register for next signal
    });
}

void plugin::plugin_impl::trigger_export() {
    if (export_in_progress_.exchange(true)) {
        ilog("snapshot: export already in progress, skipping");
        return;
    }

    try {
        auto path = generate_snapshot_path();
        ilog("snapshot: pausing chain for snapshot export to ${path}", ("path", path));

        // Pause block processing before export
        chain().pause();

        auto result = do_export(path);

        // Resume block processing after export
        chain().resume();

        if (result.success) {
            ilog("snapshot: exported ${n} objects in ${s} sections to ${path} at block ${b}",
                ("n", result.total_objects)("s", result.sections)
                ("path", result.path)("b", result.head_block_num));
        } else {
            elog("snapshot: export failed: ${e}", ("e", result.error));
        }
    } catch (const fc::exception &e) {
        chain().resume();
        elog("snapshot: export exception: ${e}", ("e", e.to_detail_string()));
    } catch (const std::exception &e) {
        chain().resume();
        elog("snapshot: export exception: ${e}", ("e", e.what()));
    } catch (...) {
        chain().resume();
        elog("snapshot: export unknown exception");
    }

    export_in_progress_ = false;
}

void plugin::plugin_impl::on_applied_block(const protocol::signed_block &b) {
    // Check signal-triggered export
    if (export_requested_.exchange(false)) {
        trigger_export();
        return;
    }

    // Check interval-based export
    if (snapshot_every > 0) {
        uint32_t block_num = b.block_num();
        if (block_num % snapshot_every == 0) {
            trigger_export();
        }
    }
}

snapshot_export_r plugin::plugin_impl::do_export(const std::string &path) {
    snapshot_export_r result;
    try {
        auto &db = database();

        // Build payload with all sections
        std::vector<char> payload;
        payload.reserve(64 * 1024 * 1024); // pre-allocate 64MB

        std::vector<snapshot_section_info> sections;

        // Export all core indexes in initialize_indexes() order
        sections.push_back(export_section<dynamic_global_property_index>(db, payload, "dynamic_global_property"));
        sections.push_back(export_section<account_index>(db, payload, "account"));
        sections.push_back(export_section<account_authority_index>(db, payload, "account_authority"));
        sections.push_back(export_section<witness_index>(db, payload, "witness"));
        sections.push_back(export_section<transaction_index>(db, payload, "transaction"));
        sections.push_back(export_section<block_summary_index>(db, payload, "block_summary"));
        sections.push_back(export_section<witness_schedule_index>(db, payload, "witness_schedule"));
        sections.push_back(export_section<content_index>(db, payload, "content"));
        sections.push_back(export_section<content_type_index>(db, payload, "content_type"));
        sections.push_back(export_section<content_vote_index>(db, payload, "content_vote"));
        sections.push_back(export_section<witness_vote_index>(db, payload, "witness_vote"));
        sections.push_back(export_section<hardfork_property_index>(db, payload, "hardfork_property"));
        sections.push_back(export_section<withdraw_vesting_route_index>(db, payload, "withdraw_vesting_route"));
        sections.push_back(export_section<master_authority_history_index>(db, payload, "master_authority_history"));
        sections.push_back(export_section<account_recovery_request_index>(db, payload, "account_recovery_request"));
        sections.push_back(export_section<change_recovery_account_request_index>(db, payload, "change_recovery_account_request"));
        sections.push_back(export_section<escrow_index>(db, payload, "escrow"));
        sections.push_back(export_section<vesting_delegation_index>(db, payload, "vesting_delegation"));
        sections.push_back(export_section<fix_vesting_delegation_index>(db, payload, "fix_vesting_delegation"));
        sections.push_back(export_section<vesting_delegation_expiration_index>(db, payload, "vesting_delegation_expiration"));
        sections.push_back(export_section<account_metadata_index>(db, payload, "account_metadata"));
        sections.push_back(export_section<proposal_index>(db, payload, "proposal"));
        sections.push_back(export_section<required_approval_index>(db, payload, "required_approval"));
        sections.push_back(export_section<committee_request_index>(db, payload, "committee_request"));
        sections.push_back(export_section<committee_vote_index>(db, payload, "committee_vote"));
        sections.push_back(export_section<invite_index>(db, payload, "invite"));
        sections.push_back(export_section<award_shares_expire_index>(db, payload, "award_shares_expire"));
        sections.push_back(export_section<paid_subscription_index>(db, payload, "paid_subscription"));
        sections.push_back(export_section<paid_subscribe_index>(db, payload, "paid_subscribe"));
        sections.push_back(export_section<witness_penalty_expire_index>(db, payload, "witness_penalty_expire"));
        sections.push_back(export_section<block_post_validation_index>(db, payload, "block_post_validation"));

        // Compute SHA256 of payload
        auto payload_hash = fc::sha256::hash(payload.data(), payload.size());

        // Build header
        const auto &dgpo = db.get_dynamic_global_properties();
        snapshot_header header;
        header.chain_id = db.get_chain_id().str();
        header.head_block_num = dgpo.head_block_number;
        header.head_block_id = dgpo.head_block_id.str();
        header.timestamp = dgpo.time.to_iso_string();
        header.snapshot_version = SNAPSHOT_FORMAT_VERSION;
        header.sections = sections;
        header.payload_sha256 = payload_hash.str();

        std::string header_json = fc::json::to_string(header);

        // Write file
        std::ofstream out(path, std::ios::binary);
        if (!out.is_open()) {
            result.error = "Failed to open file: " + path;
            return result;
        }

        // Magic
        out.write(SNAPSHOT_MAGIC, 8);

        // Format version
        uint32_t version = SNAPSHOT_FORMAT_VERSION;
        out.write(reinterpret_cast<const char*>(&version), 4);

        // Header length
        uint32_t header_len = header_json.size();
        out.write(reinterpret_cast<const char*>(&header_len), 4);

        // Header JSON
        out.write(header_json.c_str(), header_json.size());

        // Payload
        out.write(payload.data(), payload.size());

        out.close();

        // Build result
        result.success = true;
        result.path = path;
        result.head_block_num = dgpo.head_block_number;
        result.sha256 = payload_hash.str();
        result.sections = sections.size();
        result.total_objects = 0;
        for (const auto &s : sections) {
            result.total_objects += s.count;
        }
    } catch (const fc::exception &e) {
        result.error = e.to_detail_string();
    } catch (const std::exception &e) {
        result.error = e.what();
    }
    return result;
}

snapshot_info_r plugin::plugin_impl::do_info(const std::string &path) {
    snapshot_info_r result;
    try {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            result.error = "Failed to open file: " + path;
            return result;
        }

        // Read and verify magic
        char magic[8];
        in.read(magic, 8);
        if (std::memcmp(magic, SNAPSHOT_MAGIC, 8) != 0) {
            result.error = "Invalid snapshot file: bad magic";
            return result;
        }

        // Read version
        uint32_t version;
        in.read(reinterpret_cast<char*>(&version), 4);
        if (version != SNAPSHOT_FORMAT_VERSION) {
            result.error = "Unsupported snapshot version: " + std::to_string(version);
            return result;
        }

        // Read header length
        uint32_t header_len;
        in.read(reinterpret_cast<char*>(&header_len), 4);

        // Read header JSON
        std::string header_json(header_len, '\0');
        in.read(&header_json[0], header_len);

        result.header = fc::json::from_string(header_json).as<snapshot_header>();
        result.valid = true;
    } catch (const fc::exception &e) {
        result.error = e.to_detail_string();
    } catch (const std::exception &e) {
        result.error = e.what();
    }
    return result;
}

// Verify a section by re-exporting from DB and comparing bytes.
// This avoids constructing temporary objects (many have deleted default constructors).
template<typename IndexType>
void verify_section(
    database &db,
    const char *payload_data,
    const snapshot_section_info &section,
    snapshot_verify_r &result
) {
    // Re-export this section from DB
    std::vector<char> db_payload;
    auto db_section = export_section<IndexType>(db, db_payload, section.type);

    result.objects_checked += section.count;

    if (db_section.count != section.count) {
        result.objects_mismatched += section.count;
        result.mismatches.push_back(section.type + ": snapshot has " +
            std::to_string(section.count) + " objects, DB has " +
            std::to_string(db_section.count));
        return;
    }

    if (db_section.size != section.size) {
        result.objects_mismatched += section.count;
        result.mismatches.push_back(section.type + ": byte size mismatch (snapshot=" +
            std::to_string(section.size) + ", DB=" + std::to_string(db_section.size) + ")");
        return;
    }

    // Compare raw bytes
    if (std::memcmp(payload_data + section.offset, db_payload.data(), section.size) == 0) {
        result.objects_matched += section.count;
    } else {
        result.objects_mismatched += section.count;
        result.mismatches.push_back(section.type + ": binary data mismatch");
    }
}

snapshot_verify_r plugin::plugin_impl::do_verify(const std::string &path) {
    snapshot_verify_r result;
    try {
        // Read entire file
        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in.is_open()) {
            result.error = "Failed to open file: " + path;
            return result;
        }

        size_t file_size = in.tellg();
        in.seekg(0, std::ios::beg);

        // Verify magic
        char magic[8];
        in.read(magic, 8);
        if (std::memcmp(magic, SNAPSHOT_MAGIC, 8) != 0) {
            result.error = "Invalid snapshot file: bad magic";
            return result;
        }

        // Read version
        uint32_t version;
        in.read(reinterpret_cast<char*>(&version), 4);

        // Read header
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
            result.error = "SHA256 mismatch: expected " + header.payload_sha256 +
                ", got " + computed_hash.str();
            return result;
        }

        auto &db = database();

        // Verify block signature from block_log
        auto block = db.get_block_log().read_block_by_num(header.head_block_num);
        if (!block) {
            result.error = "Block " + std::to_string(header.head_block_num) +
                " not found in block_log";
            return result;
        }

        if (block->id().str() != header.head_block_id) {
            result.error = "Block ID mismatch at block " +
                std::to_string(header.head_block_num) +
                ": snapshot=" + header.head_block_id +
                ", block_log=" + block->id().str();
            return result;
        }

        try {
            auto signee = block->signee();
            if (signee == fc::ecc::public_key()) {
                result.error = "Block " + std::to_string(header.head_block_num) +
                    " has invalid signature";
                return result;
            }
        } catch (...) {
            result.error = "Block signature verification failed for block " +
                std::to_string(header.head_block_num);
            return result;
        }

        if (db.get_chain_id().str() != header.chain_id) {
            result.error = "Chain ID mismatch: snapshot=" + header.chain_id +
                ", node=" + db.get_chain_id().str();
            return result;
        }

        // Verify each section by re-exporting from DB and comparing bytes
        for (const auto &section : header.sections) {
            if (section.type == "dynamic_global_property") {
                verify_section<dynamic_global_property_index>(db, payload.data(), section, result);
            } else if (section.type == "account") {
                verify_section<account_index>(db, payload.data(), section, result);
            } else if (section.type == "account_authority") {
                verify_section<account_authority_index>(db, payload.data(), section, result);
            } else if (section.type == "witness") {
                verify_section<witness_index>(db, payload.data(), section, result);
            } else if (section.type == "transaction") {
                verify_section<transaction_index>(db, payload.data(), section, result);
            } else if (section.type == "block_summary") {
                verify_section<block_summary_index>(db, payload.data(), section, result);
            } else if (section.type == "witness_schedule") {
                verify_section<witness_schedule_index>(db, payload.data(), section, result);
            } else if (section.type == "content") {
                verify_section<content_index>(db, payload.data(), section, result);
            } else if (section.type == "content_type") {
                verify_section<content_type_index>(db, payload.data(), section, result);
            } else if (section.type == "content_vote") {
                verify_section<content_vote_index>(db, payload.data(), section, result);
            } else if (section.type == "witness_vote") {
                verify_section<witness_vote_index>(db, payload.data(), section, result);
            } else if (section.type == "hardfork_property") {
                verify_section<hardfork_property_index>(db, payload.data(), section, result);
            } else if (section.type == "withdraw_vesting_route") {
                verify_section<withdraw_vesting_route_index>(db, payload.data(), section, result);
            } else if (section.type == "master_authority_history") {
                verify_section<master_authority_history_index>(db, payload.data(), section, result);
            } else if (section.type == "account_recovery_request") {
                verify_section<account_recovery_request_index>(db, payload.data(), section, result);
            } else if (section.type == "change_recovery_account_request") {
                verify_section<change_recovery_account_request_index>(db, payload.data(), section, result);
            } else if (section.type == "escrow") {
                verify_section<escrow_index>(db, payload.data(), section, result);
            } else if (section.type == "vesting_delegation") {
                verify_section<vesting_delegation_index>(db, payload.data(), section, result);
            } else if (section.type == "fix_vesting_delegation") {
                verify_section<fix_vesting_delegation_index>(db, payload.data(), section, result);
            } else if (section.type == "vesting_delegation_expiration") {
                verify_section<vesting_delegation_expiration_index>(db, payload.data(), section, result);
            } else if (section.type == "account_metadata") {
                verify_section<account_metadata_index>(db, payload.data(), section, result);
            } else if (section.type == "proposal") {
                verify_section<proposal_index>(db, payload.data(), section, result);
            } else if (section.type == "required_approval") {
                verify_section<required_approval_index>(db, payload.data(), section, result);
            } else if (section.type == "committee_request") {
                verify_section<committee_request_index>(db, payload.data(), section, result);
            } else if (section.type == "committee_vote") {
                verify_section<committee_vote_index>(db, payload.data(), section, result);
            } else if (section.type == "invite") {
                verify_section<invite_index>(db, payload.data(), section, result);
            } else if (section.type == "award_shares_expire") {
                verify_section<award_shares_expire_index>(db, payload.data(), section, result);
            } else if (section.type == "paid_subscription") {
                verify_section<paid_subscription_index>(db, payload.data(), section, result);
            } else if (section.type == "paid_subscribe") {
                verify_section<paid_subscribe_index>(db, payload.data(), section, result);
            } else if (section.type == "witness_penalty_expire") {
                verify_section<witness_penalty_expire_index>(db, payload.data(), section, result);
            } else if (section.type == "block_post_validation") {
                verify_section<block_post_validation_index>(db, payload.data(), section, result);
            } else {
                result.mismatches.push_back("Unknown section type: " + section.type);
            }
        }

        result.success = (result.objects_mismatched == 0 && result.objects_missing == 0);
    } catch (const fc::exception &e) {
        result.error = e.to_detail_string();
    } catch (const std::exception &e) {
        result.error = e.what();
    }
    return result;
}

DEFINE_API(plugin, snapshot_export) {
    auto path = args.args->at(0).as<std::string>();
    auto &db = my->database();

    // Pause chain for consistent snapshot
    my->chain().pause();

    snapshot_export_r result;
    try {
        result = db.with_weak_read_lock([&]() {
            return my->do_export(path);
        });
    } catch (...) {
        my->chain().resume();
        throw;
    }

    my->chain().resume();
    return result;
}

DEFINE_API(plugin, snapshot_info) {
    auto path = args.args->at(0).as<std::string>();
    return my->do_info(path);
}

DEFINE_API(plugin, snapshot_verify) {
    auto path = args.args->at(0).as<std::string>();
    auto &db = my->database();
    return db.with_weak_read_lock([&]() {
        return my->do_verify(path);
    });
}

plugin::plugin() {
}

plugin::~plugin() {
}

void plugin::set_program_options(
    boost::program_options::options_description &cli,
    boost::program_options::options_description &cfg
) {
    cfg.add_options()
        (
            "snapshot-every", boost::program_options::value<uint32_t>()->default_value(0),
            "automatically export snapshot every N blocks (0 = disabled)"
        ) (
            "snapshot-dir", boost::program_options::value<std::string>()->default_value(""),
            "directory for automatic snapshots (default: data_dir)"
        );
}

void plugin::plugin_initialize(const boost::program_options::variables_map &options) {
    my.reset(new plugin_impl);
    JSON_RPC_REGISTER_API(name());

    my->snapshot_every = options.at("snapshot-every").as<uint32_t>();
    my->snapshot_dir = options.at("snapshot-dir").as<std::string>();
}

void plugin::plugin_startup() {
    auto &db = my->database();

    // Subscribe to applied_block for interval-based and signal-triggered export
    if (my->snapshot_every > 0 || true /* always listen for SIGUSR1 */) {
        my->applied_block_conn_ = db.applied_block.connect([this](const protocol::signed_block &b) {
            my->on_applied_block(b);
        });
    }

    // Register SIGUSR1 handler for manual snapshot trigger
    my->sigusr1_set_.reset(new boost::asio::signal_set(appbase::app().get_io_service(), SIGUSR1));
    my->register_sigusr1_handler();

    if (my->snapshot_every > 0) {
        ilog("snapshot plugin: started, auto-export every ${n} blocks to ${d}",
            ("n", my->snapshot_every)
            ("d", my->snapshot_dir.empty() ? "data_dir" : my->snapshot_dir));
    } else {
        ilog("snapshot plugin: started (SIGUSR1 for manual export)");
    }
}

void plugin::plugin_shutdown() {
    if (my->sigusr1_set_) {
        my->sigusr1_set_->cancel();
    }
}

} } } // graphene::plugins::snapshot
