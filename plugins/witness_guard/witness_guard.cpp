#include <graphene/plugins/witness_guard/witness_guard.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/witness_objects.hpp>
#include <graphene/chain/chain_object_types.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include <graphene/time/time.hpp>
//#include <fc/string.hpp> // Adăugat pentru conversia fc::shared_string la std::string
#include <fc/io/json.hpp>
//#include <sstream>

#include <boost/signals2/connection.hpp>
#include <appbase/application.hpp>

#include <fc/smart_ref_impl.hpp>

namespace graphene {
namespace plugins {
namespace witness_guard {

namespace bpo = boost::program_options;

// ─── impl ────────────────────────────────────────────────────────────────────

/**
 * @brief Internal implementation details for the Witness Guard plugin.
 * 
 * This struct maintains the internal state of monitored witnesses and manages 
 * the lifecycle of restoration transactions. It uses the PIMPL pattern to 
 * decouple the plugin interface from the chain and database dependencies.
 */
struct witness_guard_plugin::impl {
    impl()
        : chain_(appbase::app().get_plugin<graphene::plugins::chain::plugin>())
        , p2p_(appbase::app().get_plugin<graphene::plugins::p2p::p2p_plugin>())
    {}

    graphene::chain::database& db() { return chain_.db(); }
    graphene::chain::database& db() const { return chain_.db(); }

    bool                        _enabled        = true;
    uint32_t                    _check_interval = 20;        ///< Check signing keys every N blocks
    bool                        _initial_check_done = false; ///< True once the node is confirmed to be in sync
    boost::signals2::connection _applied_block_connection;   ///< Subscription handle for the applied_block signal

    /**
     * @brief Storage for keys required to perform a witness update.
     */
    struct witness_info {
        fc::ecc::private_key signing_key; // The key we want to restore on the witness object
        fc::ecc::private_key active_key;  // The key required to sign the witness_update transaction
    };

    // Configured witnesses to monitor: account_name -> keys
    std::map<std::string, witness_info> _witness_configs;

    /** 
     * Rate-limiting map: Tracks witnesses with an update already in the network.
     * Key: account name, Value: time when the pending request expires.
     */
    std::map<std::string, fc::time_point_sec> _restore_pending;

    /// Confirmation tracking: maps transaction IDs to their associated witness and expiration time.
    /// Used to clean up internal state once a transaction is successfully included in a block.
    std::map<graphene::chain::transaction_id_type, std::pair<std::string, fc::time_point_sec>> _pending_confirmations;

    bool check_and_restore_internal();
    void send_witness_update(const std::string& witness_name,
                             const graphene::chain::witness_object& obj,
                             const witness_info& config);

    graphene::plugins::chain::plugin&   chain_;
    graphene::plugins::p2p::p2p_plugin& p2p_;
};

// ─── check_and_restore ───────────────────────────────────────────────────────

/**
 * @brief Scans all monitored witnesses to check if their on-chain signing key needs restoration.
 * @return true if the node is synchronized and the check proceeded, false if skipped due to sync issues.
 */
bool witness_guard_plugin::impl::check_and_restore_internal() {
    auto& database = db();

    // Safety Check: Ensure the node is synchronized. 
    // Broadcasting updates during replay or deep sync can lead to expired or invalid transactions.
    const auto head_time = database.head_block_time();
    const auto now       = fc::time_point_sec(graphene::time::now());
    if (head_time < now - fc::seconds(CHAIN_BLOCK_INTERVAL * 2)) {
        dlog("witness_guard: node not in sync, skipping check");
        return false; // Node not in sync, full check not performed
    }

    // Detect potential long fork by checking Last Irreversible Block (LIB) age
    const auto& dgp = database.get_dynamic_global_properties();
    const uint32_t lib_num = dgp.last_irreversible_block_num;

    // Get block by number instead of header as some versions of database.hpp may lack fetch_block_header
    auto lib_block = database.fetch_block_by_number(lib_num);
    if (lib_block) {
        const auto lib_time = lib_block->timestamp;
        // Long Fork Protection: If the LIB is stale, the node's view of the chain is unreliable.
        if (now - lib_time > fc::seconds(200)) {
            wlog("witness_guard: POTENTIAL LONG FORK DETECTED! LIB #${n} is ${sec}s old. Skipping restoration.",
                 ("n", lib_num)("sec", (now - lib_time).to_seconds()));
            return false; // View of state is potentially invalid; do not broadcast.
        }
    }

    // Clean up _pending_confirmations once per check, not inside the witness loop.
    // Removes trackers that have expired or are no longer relevant.
    for (auto it = _pending_confirmations.begin(); it != _pending_confirmations.end(); ) {
        // If the transaction ID is expired, remove it from confirmation tracking
        if (now > it->second.second) {
            // If a transaction expires from _pending_confirmations, it means it was not included.
            // We should also remove the corresponding entry from _restore_pending to allow a retry
            // without waiting for _restore_pending's own expiration.
            _restore_pending.erase(it->second.first);
            it = _pending_confirmations.erase(it);
        } else {
            ++it;
        }
    }

    const auto& idx = database
        .get_index<graphene::chain::witness_index>()
        .indices()
        .get<graphene::chain::by_name>();

    static const graphene::protocol::public_key_type null_key;

    for (const auto& entry : _witness_configs) {
        const std::string& name = entry.first;
        const witness_info& config = entry.second;

        auto itr = idx.find(name);
        if (itr == idx.end()) {
            wlog("witness_guard: witness '${w}' not found in database", ("w", name));
            continue;
        }

        if (itr->signing_key != null_key) {
            // Key is healthy on-chain: ensure we are not holding any stale retry or confirmation state.
            _restore_pending.erase(name);
            for (auto it_conf = _pending_confirmations.begin(); it_conf != _pending_confirmations.end(); ) {
                if (it_conf->second.first == name)
                    it_conf = _pending_confirmations.erase(it_conf); // Cleanup any orphan trackers
                else
                    ++it_conf;
            }
            continue;
        }

        // Rate-limiting: Check if we already have a pending transaction in flight for this witness.
        // We only retry if the previous attempt has passed its expiration window.
        if (_restore_pending.count(name)) {
            if (now <= _restore_pending[name]) continue;
            ilog("witness_guard: previous restore for '${w}' expired, retrying", ("w", name));
        }

        ilog("witness_guard: '${w}' has null signing key on-chain — initiating restore",
             ("w", name));
        send_witness_update(name, *itr, config);
    }

    return true; // Node was in sync, full check performed
}

// ─── send_witness_update ─────────────────────────────────────────────────────

/**
 * @brief Constructs, signs, and broadcasts a witness_update transaction to the network.
 * @param obj The current on-chain witness object state.
 */
void witness_guard_plugin::impl::send_witness_update(
        const std::string& witness_name,
        const graphene::chain::witness_object& obj,
        const witness_info& config)
{
    try {
        const auto signing_pub = config.signing_key.get_public_key();
        const auto& active_priv = config.active_key;

        // Prepare the operation
        graphene::protocol::witness_update_operation op;
        op.owner            = witness_name;
        // Chainbase shared_string must be manually copied to std::string for the protocol operation.
        op.url              = std::string(obj.url.begin(), obj.url.end()); 
        op.block_signing_key = signing_pub;

        // 30s expiration is a safe window for an automated bot to either succeed or retry 
        // without clogging the expiration buffers of the network.
        fc::time_point_sec expiration(graphene::time::now() + fc::seconds(30)); 

        graphene::chain::signed_transaction tx;
        tx.operations.push_back(op);
        tx.set_expiration(expiration);
        // Use TaPOS (Transactions as Proof of Stake) to anchor the transaction to the current head block.
        tx.set_reference_block(db().head_block_id());

        tx.sign(active_priv, db().get_chain_id());
        const auto tx_id = tx.id();

        // Memory safety: Prevent map overflow in case of extreme network congestion or logic errors.
        if (_pending_confirmations.size() > 1000) {
            wlog("witness_guard: _pending_confirmations limit reached, purging expired entries");
            const auto now_sec = fc::time_point_sec(graphene::time::now());
            for (auto it = _pending_confirmations.begin(); it != _pending_confirmations.end(); ) {
                if (now_sec > it->second.second)
                    it = _pending_confirmations.erase(it);
                else
                    ++it;
            }
        }

        ilog("witness_guard: broadcasting witness_update [ID: ${id}] for '${w}' — restoring key to ${k}",
             ("id", tx_id)("w", witness_name)("k", signing_pub));

        p2p_.broadcast_transaction(tx);

        // Update internal state to track this flight.
        _restore_pending[witness_name] = expiration;
        _pending_confirmations[tx_id] = { witness_name, expiration }; 

        ilog("witness_guard: witness_update for '${w}' sent successfully", ("w", witness_name));

    } catch (const fc::exception& e) {
        elog("witness_guard: witness_update FAILED for '${w}': ${e}",
             ("w", witness_name)("e", e.to_detail_string()));
    }
}

// ─── plugin lifecycle ────────────────────────────────────────────────────────

witness_guard_plugin::witness_guard_plugin()  = default;
witness_guard_plugin::~witness_guard_plugin() = default;

void witness_guard_plugin::set_program_options(
        bpo::options_description& cli,
        bpo::options_description& cfg)
{
    cfg.add_options()
        ("witness-guard-enabled",
         bpo::value<bool>()->default_value(true),
         "Enable witness key auto-restore. "
         "When true, the plugin monitors configured witnesses and sends "
         "witness_update if the on-chain signing key is reset to null.")

        ("witness-guard-witness",
         bpo::value<std::vector<std::string>>()->composing()->multitoken(),
         "Witness to monitor: name signing_wif active_wif (triplets). Can be specified multiple times.")

        ("witness-guard-interval",
         bpo::value<uint32_t>()->default_value(20),
         "How often to check witness signing keys, in blocks (default: 20 ≈ 60s).")
        ;

    cli.add(cfg);
}

void witness_guard_plugin::plugin_initialize(
        const bpo::variables_map& options)
{
    try {
        ilog("witness_guard: plugin_initialize() begin");
        pimpl = std::make_unique<impl>();

        if (options.count("witness-guard-enabled")) {
            pimpl->_enabled = options["witness-guard-enabled"].as<bool>();
        }
        if (!pimpl->_enabled) {
            ilog("witness_guard: disabled via config, skipping initialization");
            return;
        }

        pimpl->_check_interval = options["witness-guard-interval"].as<uint32_t>();
        if (pimpl->_check_interval == 0) pimpl->_check_interval = 1;

        // Parse witness configurations provided in config.ini or CLI.
        if (options.count("witness-guard-witness")) {
            const auto& entries = options["witness-guard-witness"].as<std::vector<std::string>>();
            for (const auto& entry : entries) {
                try {
                    // Parse each line as a JSON array: ["name", "signing_wif", "active_wif"]
                    auto arr = fc::json::from_string(entry).get_array();
                    FC_ASSERT(arr.size() == 3, "witness-guard-witness expects [name, signing_wif, active_wif]");

                    std::string name = arr[0].as_string();
                    auto sign_priv = graphene::utilities::wif_to_key(arr[1].as_string());
                    auto active_priv = graphene::utilities::wif_to_key(arr[2].as_string());

                    FC_ASSERT(sign_priv.valid(), "witness-guard-witness: invalid signing WIF for ${n}", ("n", name));
                    FC_ASSERT(active_priv.valid(), "witness-guard-witness: invalid active WIF for ${n}", ("n", name));
                    pimpl->_witness_configs[name] = { *sign_priv, *active_priv };
                    
                    ilog("witness_guard: added monitor for witness '${w}' (signing key: ${k})",
                         ("w", name)("k", sign_priv->get_public_key()));

                } catch (const fc::exception& e) {
                    // Individual entry failure doesn't stop the whole plugin.
                    elog("witness_guard: failed to parse witness entry '${entry}': ${e}",
                         ("entry", entry)("e", e.to_detail_string()));
                }
            }
        }

        if (pimpl->_witness_configs.empty()) {
            wlog("witness_guard: no witnesses configured for monitoring");
        }

        ilog("witness_guard: plugin_initialize() end — "
             "monitoring ${n} witness(es), interval=${i} blocks",
             ("n", pimpl->_witness_configs.size())("i", pimpl->_check_interval));

    } FC_LOG_AND_RETHROW()
}

void witness_guard_plugin::plugin_startup() {
    ilog("witness_guard: plugin_startup() begin");

    if (!pimpl->_enabled || pimpl->_witness_configs.empty()) {
        ilog("witness_guard: nothing to monitor, plugin inactive");
        return;
    }

    // Logic: Verify configured active keys against the on-chain account authorities.
    // This prevents the plugin from running silently while lacking the permission to broadcast updates.
    for (auto it = pimpl->_witness_configs.begin(); it != pimpl->_witness_configs.end(); ) {
        const std::string& name = it->first;
        const impl::witness_info& config = it->second;

        try {
            // Cryptographic authorities are stored in account_authority_object, separate from balances.
            const auto& account_auth_obj = pimpl->db().get<graphene::chain::account_authority_object, graphene::chain::by_account>(name);
            
            const auto active_pub_key = config.active_key.get_public_key();
            bool active_key_has_authority = false;
            for (const auto& auth : account_auth_obj.active.key_auths) {
                if (auth.first == active_pub_key) {
                    active_key_has_authority = true;
                    break;
                }
            }
            if (!active_key_has_authority) {
                elog("witness_guard: WARNING: Configured active key for witness '${w}' "
                     "does NOT have authority on-chain. Restoration will fail.", ("w", name));
            }
            ++it;
        } catch (const fc::exception& e) {
            elog("witness_guard: ERROR: Account '${w}' not found on chain. Removing from monitor.", ("w", name));
            it = pimpl->_witness_configs.erase(it);
        }
    }

    if (pimpl->_witness_configs.empty()) return;

    // Run an initial check if the node is already synced. 
    // If not, the check will be deferred to the applied_block handler.
    if (pimpl->check_and_restore_internal()) {
        pimpl->_initial_check_done = true;
    }

    // Main logic hook: execute on every block applied to the database.
    pimpl->_applied_block_connection = pimpl->db().applied_block.connect(
    [this](const graphene::chain::signed_block& b) {
        if (!pimpl->_enabled) return;

        // 1. Check for transaction confirmations in the new block
        // We first collect IDs to avoid iterator invalidation issues if a map modification is triggered.
        if (!pimpl->_pending_confirmations.empty()) {
            std::vector<graphene::chain::transaction_id_type> confirmed_ids;
            for (const auto& tx : b.transactions) {
                // Checking for presence is faster than a full find/iterator extraction here.
                if (pimpl->_pending_confirmations.count(tx.id()))
                    confirmed_ids.push_back(tx.id()); 
            }

            for (const auto& id : confirmed_ids) {
                auto it = pimpl->_pending_confirmations.find(id);
                if (it != pimpl->_pending_confirmations.end()) {
                    const auto w_name = it->second.first;
                    pimpl->_restore_pending.erase(w_name);
                    pimpl->_pending_confirmations.erase(it);
                    ilog("witness_guard: CONFIRMED restoration for '${w}' in block #${n} [TX: ${id}]",
                         ("w", w_name)("n", b.block_num())("id", id));
                }
            }
        }

        // 2. Look-ahead: If any of our witnesses are scheduled in the next 3 slots, check now!
        // We check 3 slots ahead (~9s) to allow enough time for the restoration transaction 
        // to propagate and be included in the block immediately preceding our witness's turn,
        // thus preventing missed blocks.
        bool scheduled_soon = false;
        if (pimpl->_initial_check_done) {
            for (uint32_t i = 1; i <= 3; ++i) {
                if (pimpl->_witness_configs.count(pimpl->db().get_scheduled_witness(i))) {
                    scheduled_soon = true;
                    break;
                }
            }
        }

        // Execution logic based on urgency and node state.
        if (scheduled_soon) {
            // High priority: Witness is scheduled to produce soon.
            pimpl->check_and_restore_internal();
        }
        else if (!pimpl->_initial_check_done && (b.block_num() % 10 == 0)) {
            // Background priority: Catching up with sync.
            if (pimpl->check_and_restore_internal()) {
                pimpl->_initial_check_done = true; // Sync detected.
            }
        }
        else if (b.block_num() % pimpl->_check_interval == 0) {
            pimpl->check_and_restore_internal();
        }
    }
);

    ilog("witness_guard: plugin_startup() end — active");
}

void witness_guard_plugin::plugin_shutdown() {
    if (pimpl && pimpl->_applied_block_connection.connected()) {
        pimpl->_applied_block_connection.disconnect();
    }
    ilog("witness_guard: plugin_shutdown()");
}

} // witness_guard
} // plugins
} // graphene