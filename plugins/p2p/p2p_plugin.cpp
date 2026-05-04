#include <graphene/plugins/p2p/p2p_plugin.hpp>

#include <graphene/network/dlt_p2p_node.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/dlt_block_log.hpp>
#include <graphene/chain/fork_database.hpp>

#include <fc/network/resolve.hpp>
#include <fc/thread/thread.hpp>

using std::string;
using std::vector;

namespace graphene {
namespace plugins {
namespace p2p {

using appbase::app;

using graphene::network::dlt_p2p_node;
using graphene::network::dlt_p2p_delegate;

using graphene::protocol::block_id_type;
using graphene::protocol::signed_block;
using graphene::protocol::signed_transaction;
using graphene::protocol::signature_type;
using graphene::chain::database;
using graphene::chain::chain_id_type;

namespace detail {

// ── DLT P2P Delegate — bridges chain state to the P2P node ──────────
class dlt_delegate : public dlt_p2p_delegate {
public:
    explicit dlt_delegate(chain::plugin& c) : chain(c) {}

    // ── Chain state queries ──────────────────────────────────────
    block_id_type get_head_block_id() const override {
        return chain.db().head_block_id();
    }

    uint32_t get_head_block_num() const override {
        return chain.db().head_block_num();
    }

    block_id_type get_lib_block_id() const override {
        return chain.db().with_read_lock([&]() {
            return chain.db().get_dynamic_global_properties().last_irreversible_block_id;
        });
    }

    uint32_t get_lib_block_num() const override {
        return chain.db().with_read_lock([&]() {
            return chain.db().get_dynamic_global_properties().last_irreversible_block_num;
        });
    }

    uint32_t get_dlt_earliest_block() const override {
        auto& db = chain.db();
        if (!db._dlt_mode) return 0;
        auto& log = db.get_dlt_block_log();
        return log.is_open() ? log.start_block_num() : 0;
    }

    uint32_t get_dlt_latest_block() const override {
        auto& db = chain.db();
        if (!db._dlt_mode) return db.head_block_num();
        auto& log = db.get_dlt_block_log();
        return log.is_open() ? log.head_block_num() : db.head_block_num();
    }

    bool is_emergency_consensus_active() const override {
        return chain.db()._dlt_mode &&
               chain.db().with_read_lock([&]() {
                   return chain.db().get_dynamic_global_properties().is_emergency_consensus;
               });
    }

    bool has_emergency_private_key() const override {
        // This is checked via the witness plugin — simplified here
        return false; // The witness plugin provides this info separately
    }

    bool is_dlt_mode() const override {
        return chain.db()._dlt_mode;
    }

    // ── Block queries ────────────────────────────────────────────
    fc::optional<signed_block> read_block_by_num(uint32_t block_num) const override {
        auto& db = chain.db();
        // Check dlt_block_log first
        if (db._dlt_mode) {
            auto& log = db.get_dlt_block_log();
            if (log.is_open()) {
                auto block = log.read_block_by_num(block_num);
                if (block.valid()) return block;
            }
        }
        // Then check fork_db
        try {
            auto& fdb = db.get_fork_db();
            auto blocks = fdb.fetch_block_by_number(block_num);
            if (!blocks.empty()) return blocks.front();
        } catch (...) {}
        return {};
    }

    bool block_exists_in_log_or_fork_db(uint32_t block_num, block_id_type& id_out) const override {
        auto& db = chain.db();
        // Check dlt_block_log
        if (db._dlt_mode) {
            auto& log = db.get_dlt_block_log();
            if (log.is_open()) {
                auto block = log.read_block_by_num(block_num);
                if (block.valid()) {
                    id_out = block->id();
                    return true;
                }
            }
        }
        // Check fork_db
        try {
            auto& fdb = db.get_fork_db();
            auto blocks = fdb.fetch_block_by_number(block_num);
            if (!blocks.empty()) {
                id_out = blocks.front()->id();
                return true;
            }
        } catch (...) {}
        return false;
    }

    bool is_block_known(const block_id_type& id) const override {
        return chain.db().is_known_block(id);
    }

    // ── Block/transaction handling ───────────────────────────────
    bool accept_block(const signed_block& block, bool sync_mode) override {
        try {
            auto result = chain.db().push_block(block);
            return false; // push_block returns void; fork detection done via on_block_applied
        } catch (const graphene::chain::unlinkable_block_exception&) {
            wlog("Unlinkable block #${n}, storing in fork_db", ("n", block.block_num()));
            chain.db().get_fork_db().push_block(block);
            return false;
        } catch (const fc::exception& e) {
            wlog("Error accepting block #${n}: ${e}", ("n", block.block_num())("e", e.to_detail_string()));
            return false;
        }
    }

    bool accept_transaction(const signed_transaction& trx) override {
        try {
            chain.db().accept_transaction(trx);
            return true;
        } catch (const fc::exception& e) {
            dlog("Error accepting transaction: ${e}", ("e", e.to_detail_string()));
            return false;
        }
    }

    // ── Fork resolution ──────────────────────────────────────────
    int compare_fork_branches(const block_id_type& a, const block_id_type& b) const override {
        return chain.db().compare_fork_branches(a, b);
    }

    std::vector<block_id_type> get_fork_branch_tips() const override {
        std::vector<block_id_type> tips;
        try {
            auto& fdb = chain.db().get_fork_db();
            // Get head block and any blocks at same height
            auto head_num = chain.db().head_block_num();
            auto blocks = fdb.fetch_block_by_number(head_num);
            for (auto& b : blocks) {
                tips.push_back(b->id());
            }
            // Also check a few blocks ahead for competing forks
            for (uint32_t n = head_num + 1; n <= head_num + 5; ++n) {
                auto more = fdb.fetch_block_by_number(n);
                for (auto& b : more) {
                    tips.push_back(b->id());
                }
            }
        } catch (...) {}
        return tips;
    }

    void switch_to_fork(const block_id_type& new_head) override {
        try {
            auto& fdb = chain.db().get_fork_db();
            auto block = fdb.fetch_block(new_head);
            if (block) {
                chain.db().pop_block(); // pop back to fork point
                // Re-push blocks from the fork
                ilog("Switching to fork with head ${id}", ("id", new_head));
            }
        } catch (const fc::exception& e) {
            wlog("Error switching to fork: ${e}", ("e", e.to_detail_string()));
        }
    }

    bool is_head_on_branch(const block_id_type& tip) const override {
        // Simple check: if tip matches our head, we're on that branch
        return tip == chain.db().head_block_id();
    }

    // ── TaPoS helpers ───────────────────────────────────────────
    bool is_tapos_block_known(uint32_t ref_block_num, uint32_t ref_block_prefix) const override {
        return chain.db().with_read_lock([&]() {
            return chain.db().is_known_block(ref_block_num);
        });
    }

    void resync_from_lib(bool force_emergency) override {
        // This is handled at the plugin level, not delegate
    }

    chain::plugin& chain;
};

// ── New p2p_plugin_impl — wraps dlt_p2p_node ────────────────────────
class p2p_plugin_impl {
public:
    p2p_plugin_impl(chain::plugin& c)
        : chain(c), delegate(std::make_unique<dlt_delegate>(c)) {}

    ~p2p_plugin_impl() = default;

    std::unique_ptr<dlt_p2p_node> node;
    std::unique_ptr<dlt_delegate> delegate;
    fc::optional<fc::ip::endpoint> endpoint;
    vector<fc::ip::endpoint> seeds;
    string user_agent;
    uint32_t max_connections = 50;
    bool block_producer = false;

    // DLT config
    uint32_t dlt_block_log_max_blocks = 100000;
    uint32_t peer_max_disconnect_hours = 8;
    uint32_t mempool_max_tx = 10000;
    uint32_t mempool_max_bytes = 100 * 1024 * 1024;
    uint32_t mempool_max_tx_size = 64 * 1024;
    uint32_t mempool_max_expiration_hours = 24;
    uint32_t peer_exchange_max_per_reply = 10;
    uint32_t peer_exchange_max_per_subnet = 2;
    uint32_t peer_exchange_min_uptime_sec = 600;

    chain::plugin& chain;

    fc::thread p2p_thread;
};

} // namespace detail

// ── p2p_plugin implementation ────────────────────────────────────────

p2p_plugin::p2p_plugin() {}

p2p_plugin::~p2p_plugin() {}

void p2p_plugin::set_program_options(
    boost::program_options::options_description& cli,
    boost::program_options::options_description& cfg) {
    cfg.add_options()
        ("p2p-endpoint", boost::program_options::value<string>()->implicit_value("127.0.0.1:9876"),
            "The local IP address and port to listen for incoming connections.")
        ("p2p-max-connections", boost::program_options::value<uint32_t>(),
            "Maximum number of incoming connections on P2P endpoint.")
        ("seed-node", boost::program_options::value<vector<string>>()->composing(),
            "The IP address and port of a remote peer to sync with. Deprecated in favor of p2p-seed-node.")
        ("p2p-seed-node", boost::program_options::value<vector<string>>()->composing(),
            "The IP address and port of a remote peer to sync with.")
        ("dlt-block-log-max-blocks", boost::program_options::value<uint32_t>()->default_value(100000),
            "Maximum number of blocks to keep in DLT block log (0 = no DLT log).")
        ("dlt-peer-max-disconnect-hours", boost::program_options::value<uint32_t>()->default_value(8),
            "Remove peer from known list after this many hours of non-response.")
        ("dlt-mempool-max-tx", boost::program_options::value<uint32_t>()->default_value(10000),
            "Maximum number of transactions in P2P mempool.")
        ("dlt-mempool-max-bytes", boost::program_options::value<uint32_t>()->default_value(104857600),
            "Maximum total bytes of transactions in P2P mempool (default 100MB).")
        ("dlt-mempool-max-tx-size", boost::program_options::value<uint32_t>()->default_value(65536),
            "Maximum single transaction size in bytes (default 64KB).")
        ("dlt-mempool-max-expiration-hours", boost::program_options::value<uint32_t>()->default_value(24),
            "Reject transactions with expiration too far in the future (hours).")
        ("dlt-peer-exchange-max-per-reply", boost::program_options::value<uint32_t>()->default_value(10),
            "Max peers to include in a peer exchange reply.")
        ("dlt-peer-exchange-max-per-subnet", boost::program_options::value<uint32_t>()->default_value(2),
            "Max peers per /24 subnet in peer exchange replies.")
        ("dlt-peer-exchange-min-uptime-sec", boost::program_options::value<uint32_t>()->default_value(600),
            "Min connection uptime (seconds) before sharing a peer in exchange replies.");
}

void p2p_plugin::plugin_initialize(const boost::program_options::variables_map& options) {
    my = std::make_unique<detail::p2p_plugin_impl>(app().get_plugin<chain::plugin>());

    if (options.count("p2p-endpoint")) {
        my->endpoint = fc::ip::endpoint::from_string(options.at("p2p-endpoint").as<string>());
    }

    if (options.count("p2p-max-connections")) {
        my->max_connections = options.at("p2p-max-connections").as<uint32_t>();
    }

    // Seed nodes (support both old and new config names)
    if (options.count("seed-node")) {
        for (const auto& addr : options.at("seed-node").as<vector<string>>()) {
            try {
                my->seeds.push_back(fc::ip::endpoint::from_string(addr));
            } catch (...) {
                try {
                    auto eps = fc::resolve(addr, 0);
                    if (!eps.empty()) my->seeds.push_back(eps.front());
                } catch (...) {}
            }
        }
    }
    if (options.count("p2p-seed-node")) {
        for (const auto& addr : options.at("p2p-seed-node").as<vector<string>>()) {
            try {
                my->seeds.push_back(fc::ip::endpoint::from_string(addr));
            } catch (...) {
                try {
                    auto eps = fc::resolve(addr, 0);
                    if (!eps.empty()) my->seeds.push_back(eps.front());
                } catch (...) {}
            }
        }
    }

    // DLT config
    if (options.count("dlt-block-log-max-blocks")) {
        my->dlt_block_log_max_blocks = options.at("dlt-block-log-max-blocks").as<uint32_t>();
    }
    if (options.count("dlt-peer-max-disconnect-hours")) {
        my->peer_max_disconnect_hours = options.at("dlt-peer-max-disconnect-hours").as<uint32_t>();
    }
    if (options.count("dlt-mempool-max-tx")) {
        my->mempool_max_tx = options.at("dlt-mempool-max-tx").as<uint32_t>();
    }
    if (options.count("dlt-mempool-max-bytes")) {
        my->mempool_max_bytes = options.at("dlt-mempool-max-bytes").as<uint32_t>();
    }
    if (options.count("dlt-mempool-max-tx-size")) {
        my->mempool_max_tx_size = options.at("dlt-mempool-max-tx-size").as<uint32_t>();
    }
    if (options.count("dlt-mempool-max-expiration-hours")) {
        my->mempool_max_expiration_hours = options.at("dlt-mempool-max-expiration-hours").as<uint32_t>();
    }
    if (options.count("dlt-peer-exchange-max-per-reply")) {
        my->peer_exchange_max_per_reply = options.at("dlt-peer-exchange-max-per-reply").as<uint32_t>();
    }
    if (options.count("dlt-peer-exchange-max-per-subnet")) {
        my->peer_exchange_max_per_subnet = options.at("dlt-peer-exchange-max-per-subnet").as<uint32_t>();
    }
    if (options.count("dlt-peer-exchange-min-uptime-sec")) {
        my->peer_exchange_min_uptime_sec = options.at("dlt-peer-exchange-min-uptime-sec").as<uint32_t>();
    }
}

void p2p_plugin::plugin_startup() {
    my->p2p_thread.async([this]() {
        // Create the DLT P2P node
        my->node = std::make_unique<dlt_p2p_node>("viz-dlt-p2p");

        // Set the p2p thread so the node can schedule fibers
        my->node->set_thread(fc::thread::current());

        // Set delegate
        my->node->set_delegate(my->delegate.get());

        // Configure
        if (my->endpoint) {
            my->node->set_listen_endpoint(*my->endpoint, true);
        }
        for (const auto& seed : my->seeds) {
            my->node->add_seed_node(seed);
        }
        my->node->set_max_connections(my->max_connections);
        my->node->set_dlt_block_log_max_blocks(my->dlt_block_log_max_blocks);
        my->node->set_peer_max_disconnect_hours(my->peer_max_disconnect_hours);
        my->node->set_mempool_limits(my->mempool_max_tx, my->mempool_max_bytes,
                                      my->mempool_max_tx_size, my->mempool_max_expiration_hours);
        my->node->set_peer_exchange_limits(my->peer_exchange_max_per_reply,
                                            my->peer_exchange_max_per_subnet,
                                            my->peer_exchange_min_uptime_sec);

        // Start (accept loop + periodic task run as internal fibers)
        my->node->start();
    }).wait();

    ilog("DLT P2P Plugin started");
}

void p2p_plugin::plugin_shutdown() {
    ilog("Shutting down DLT P2P Plugin");
    if (my->node) {
        my->p2p_thread.async([this]() {
            my->node->close();
        }).wait();
    }
}

void p2p_plugin::broadcast_block(const graphene::protocol::signed_block& block) {
    my->p2p_thread.async([this, block]() {
        my->node->broadcast_block(block);
    }).wait();
}

void p2p_plugin::broadcast_block_post_validation(
    const graphene::protocol::block_id_type block_id,
    const std::string& witness_account,
    const graphene::protocol::signature_type& witness_signature) {
    my->p2p_thread.async([this, block_id, witness_account, witness_signature]() {
        my->node->broadcast_block_post_validation(block_id, witness_account, witness_signature);
    }).wait();
}

void p2p_plugin::broadcast_transaction(const graphene::protocol::signed_transaction& tx) {
    my->p2p_thread.async([this, tx]() {
        my->node->broadcast_transaction(tx);
    }).wait();
}

void p2p_plugin::broadcast_chain_status() {
    my->p2p_thread.async([this]() {
        my->node->broadcast_chain_status();
    }).wait();
}

void p2p_plugin::set_block_production(bool producing_blocks) {
    my->p2p_thread.async([this, producing_blocks]() {
        my->node->set_block_production(producing_blocks);
    }).wait();
}

void p2p_plugin::resync_from_lib(bool force_emergency) {
    my->p2p_thread.async([this, force_emergency]() {
        my->node->resync_from_lib(force_emergency);
    }).wait();
}

void p2p_plugin::trigger_resync() {
    my->p2p_thread.async([this]() {
        my->node->trigger_resync();
    }).wait();
}

uint32_t p2p_plugin::get_connections_count() const {
    return my->node ? my->node->get_connection_count() : 0;
}

void p2p_plugin::reconnect_seeds() {
    my->p2p_thread.async([this]() {
        my->node->reconnect_seeds();
    }).wait();
}

void p2p_plugin::pause_block_processing() {
    my->p2p_thread.async([this]() {
        my->node->pause_block_processing();
    }).wait();
}

void p2p_plugin::resume_block_processing() {
    my->p2p_thread.async([this]() {
        my->node->resume_block_processing();
    }).wait();
}

fc::time_point p2p_plugin::get_last_network_block_time() const {
    return my->node ? my->node->get_last_network_block_time() : fc::time_point();
}

} // namespace p2p
} // namespace plugins
} // namespace graphene
