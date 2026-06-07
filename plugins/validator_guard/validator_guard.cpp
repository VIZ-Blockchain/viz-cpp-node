#include <graphene/plugins/validator_guard/validator_guard.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/account_object.hpp>
#include <graphene/chain/validator_objects.hpp>
#include <graphene/chain/chain_object_types.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include <graphene/time/time.hpp>
#include <fc/string.hpp>
#include <fc/io/json.hpp>
#include <sstream>
#include <thread>
#include <chrono>

#include <boost/signals2/connection.hpp>
#include <appbase/application.hpp>

#include <fc/smart_ref_impl.hpp>

namespace graphene {
namespace plugins {
namespace validator_guard {

namespace bpo = boost::program_options;

// ─── impl ────────────────────────────────────────────────────────────────────

struct validator_guard_plugin::impl {
    impl()
        : chain_(appbase::app().get_plugin<graphene::plugins::chain::plugin>())
        , p2p_(appbase::app().get_plugin<graphene::plugins::p2p::p2p_plugin>())
    {}

    graphene::chain::database& db() { return chain_.db(); }
    graphene::chain::database& db() const { return chain_.db(); }

    // ── config ────────────────────────────────────────────────────────────────
    bool                        _enabled        = true;
    uint32_t                    _check_interval = 20;   // blocks between periodic checks
    uint32_t                    _disable_threshold = 5; // consecutive blocks by same validator before auto-disable
    bool                        _disable_on_shutdown = true; // on graceful shutdown, null the signing key of enabled validators
    uint32_t                    _shutdown_grace_sec = 10;    // seconds to keep re-broadcasting disable txs so peers receive them
    bool                        _initial_check_done = false; // true once the node is confirmed in sync at startup
    bool                        _stale_production_config = false; // mirrors enable-stale-production from validator config
    boost::signals2::connection _applied_block_connection;   // applied_block signal connection

    // Per-validator consecutive block counter: validator_name -> count of consecutive blocks produced
    std::map<std::string, uint32_t> _consecutive_blocks;

    // Validators that have been auto-disabled and are awaiting manual intervention
    // (we should NOT auto-restore them — only a restart or explicit action should re-enable)
    std::set<std::string> _auto_disabled_validators;

    // Per-validator key pair used for signing and for broadcasting the update
    struct validator_info {
        fc::ecc::private_key signing_key;
        fc::ecc::private_key active_key;
    };

    // Configured validators: validator_name -> key pair
    std::map<std::string, validator_info> _validator_configs;

    // validators with an in-flight restore: validator_name -> tx expiration time
    std::map<std::string, fc::time_point_sec> _restore_pending;

    // Transaction IDs awaiting block inclusion: tx_id -> (validator_name, expiration)
    std::map<graphene::chain::transaction_id_type, std::pair<std::string, fc::time_point_sec>> _pending_confirmations;

    // ── core ──────────────────────────────────────────────────────────────────

    bool check_and_restore_internal();
    void send_validator_update(const std::string& validator_name,
                             const graphene::chain::validator_object& obj,
                             const validator_info& config);
    void send_validator_disable(const std::string& validator_name,
                              const graphene::chain::validator_object& obj,
                              const validator_info& config);

    // Graceful-shutdown handler: nulls the signing key of every configured
    // validator that is still enabled on-chain and keeps re-broadcasting the
    // transactions until peers have had a chance to receive them.
    void disable_on_shutdown();

    graphene::plugins::chain::plugin&   chain_;
    graphene::plugins::p2p::p2p_plugin& p2p_;
};

// ─── check_and_restore ───────────────────────────────────────────────────────
// Checks all monitored validators and restores signing keys when needed.
// Returns true if the node is in sync and the full check was performed.
bool validator_guard_plugin::impl::check_and_restore_internal() {
    auto& database = db();

    // Skip auto-restore when stale production is enabled AND the network is
    // not yet healthy. Once participation reaches >= 33% the stale-production
    // override is no longer needed — auto-clear it (same logic as validator plugin)
    // to remove operator human error and re-enable key restoration.
    if (_stale_production_config) {
        const auto& dgp = database.get_dynamic_global_properties();
        if (!dgp.emergency_consensus_active) {
            uint32_t prate = database.validator_participation_rate();
            if (prate >= 33 * CHAIN_1_PERCENT) {
                ilog("validator_guard: network is healthy (participation ${p}%), "
                     "auto-clearing stale production override",
                     ("p", prate / CHAIN_1_PERCENT));
                _stale_production_config = false;
            } else {
                dlog("validator_guard: stale production enabled and network not yet healthy, "
                     "auto-restore disabled");
                return false;
            }
        }
        // In emergency mode we do NOT skip — emergency consensus handles
        // its own recovery and key restoration may still be needed.
    }

    // Require the node to be synchronized: head block must be recent
    // (within the last 2 * CHAIN_BLOCK_INTERVAL seconds)
    const auto head_time = database.head_block_time();
    const auto now       = fc::time_point_sec(graphene::time::now());
    if (head_time < now - fc::seconds(CHAIN_BLOCK_INTERVAL * 2)) {
        dlog("validator_guard: node not in sync, skipping check");
        return false;
    }

    // Safety check: if the Last Irreversible Block is too old the node may be
    // stuck on a long fork or the network may be stalled — skip restoration.
    const auto& dgp = database.get_dynamic_global_properties();
    const uint32_t lib_num = dgp.last_irreversible_block_num;
    auto lib_block = database.fetch_block_by_number(lib_num);
    if (lib_block) {
        const auto lib_time = lib_block->timestamp;
        if (now - lib_time > fc::seconds(200)) {
            wlog("validator_guard: POTENTIAL LONG FORK DETECTED! LIB #${n} is ${sec}s old. Skipping restoration.",
                 ("n", lib_num)("sec", (now - lib_time).to_seconds()));
            return false;
        }
    }

    // Expire stale pending-confirmation trackers so that failed broadcasts
    // are retried on the next check cycle instead of staying stuck.
    for (auto it = _pending_confirmations.begin(); it != _pending_confirmations.end(); ) {
        if (now > it->second.second) {
            _restore_pending.erase(it->second.first);
            it = _pending_confirmations.erase(it);
        } else {
            ++it;
        }
    }

    const auto& idx = database
        .get_index<graphene::chain::validator_index>()
        .indices()
        .get<graphene::chain::by_name>();

    static const graphene::protocol::public_key_type null_key;

    // Iterate over configured validators and check their on-chain signing key.
    // If a key is null, initiate a validator_update to restore it.
    for (const auto& entry : _validator_configs) {
        const std::string& name = entry.first;
        const validator_info& config = entry.second;

        auto itr = idx.find(name);
        if (itr == idx.end()) {
            wlog("validator_guard: validator '${w}' not found in database", ("w", name));
            continue;
        }

        // Signing key is present on-chain — nothing to do
        if (itr->signing_key != null_key) {
            _restore_pending.erase(name);
            // If this validator was auto-disabled and the key is now present on-chain,
            // clear the auto-disabled flag (operator must have manually restored)
            _auto_disabled_validators.erase(name);
            continue;
        }

        // If this validator was auto-disabled by the consecutive-block guard,
        // do NOT auto-restore it — the operator must investigate and restart
        if (_auto_disabled_validators.count(name)) {
            dlog("validator_guard: '${w}' was auto-disabled (consecutive block limit), "
                 "skipping auto-restore", ("w", name));
            continue;
        }

        // A restore transaction is already in flight; wait for it to land or expire
        if (_restore_pending.count(name)) {
            if (now <= _restore_pending[name]) continue;
            ilog("validator_guard: previous restore for '${w}' expired, retrying", ("w", name));
        }

        ilog("validator_guard: '${w}' has null signing key on-chain — initiating restore",
             ("w", name));
        send_validator_update(name, *itr, config);
    }

    return true; // node was in sync, full check performed
}

// ─── send_validator_update ─────────────────────────────────────────────────────
// Builds, signs, and broadcasts a validator_update transaction that restores
// the on-chain signing key for the given validator.

void validator_guard_plugin::impl::send_validator_update(
        const std::string& validator_name,
        const graphene::chain::validator_object& obj,
        const validator_info& config)
{
    try {
        const auto signing_pub = config.signing_key.get_public_key();
        const auto& active_priv = config.active_key;

        // Build the validator_update operation with the correct signing key
        graphene::protocol::validator_update_operation op;
        op.owner            = validator_name;
        op.url              = std::string(obj.url.begin(), obj.url.end());
        op.block_signing_key = signing_pub;

        // 30-second expiration window for the transaction
        fc::time_point_sec expiration(graphene::time::now() + fc::seconds(30));

        // Assemble and sign the transaction with the validator's active authority key
        graphene::chain::signed_transaction tx;
        tx.operations.push_back(op);
        tx.set_expiration(expiration);
        tx.set_reference_block(db().head_block_id());

        tx.sign(active_priv, db().get_chain_id());
        const auto tx_id = tx.id();

        // Prevent unbounded growth of the confirmation tracker
        if (_pending_confirmations.size() > 1000) {
            wlog("validator_guard: _pending_confirmations limit reached, clearing old entries");
            _pending_confirmations.clear();
        }

        ilog("validator_guard: broadcasting validator_update [ID: ${id}] for '${w}' — restoring key to ${k}",
             ("id", tx_id)("w", validator_name)("k", signing_pub));

        p2p_.broadcast_transaction(tx);

        // Track so we can confirm inclusion in a future block
        _restore_pending[validator_name] = expiration;
        _pending_confirmations[tx_id] = { validator_name, expiration };

        ilog("validator_guard: validator_update for '${w}' sent successfully", ("w", validator_name));

    } catch (const fc::exception& e) {
        elog("validator_guard: validator_update FAILED for '${w}': ${e}",
             ("w", validator_name)("e", e.to_detail_string()));
        // Do not mark as pending — retry will happen on the next check cycle
    }
}

// ─── send_validator_disable ─────────────────────────────────────────────────────
// Builds, signs, and broadcasts a validator_update transaction that sets the
// on-chain signing key to null, effectively disabling block production.

void validator_guard_plugin::impl::send_validator_disable(
        const std::string& validator_name,
        const graphene::chain::validator_object& obj,
        const validator_info& config)
{
    try {
        const auto& active_priv = config.active_key;

        static const graphene::protocol::public_key_type null_key;

        // Build the validator_update operation with null signing key to disable
        graphene::protocol::validator_update_operation op;
        op.owner             = validator_name;
        op.url               = std::string(obj.url.begin(), obj.url.end());
        op.block_signing_key = null_key;

        // 30-second expiration window for the transaction
        fc::time_point_sec expiration(graphene::time::now() + fc::seconds(30));

        // Assemble and sign the transaction with the validator's active authority key
        graphene::chain::signed_transaction tx;
        tx.operations.push_back(op);
        tx.set_expiration(expiration);
        tx.set_reference_block(db().head_block_id());

        tx.sign(active_priv, db().get_chain_id());
        const auto tx_id = tx.id();

        ilog("validator_guard: broadcasting validator_update [ID: ${id}] for '${w}' — DISABLING (setting key to null)",
             ("id", tx_id)("w", validator_name));

        p2p_.broadcast_transaction(tx);

        // Mark this validator as auto-disabled so we don't auto-restore it
        _auto_disabled_validators.insert(validator_name);

        ilog("validator_guard: validator_disable for '${w}' sent successfully", ("w", validator_name));

    } catch (const fc::exception& e) {
        elog("validator_guard: validator_disable FAILED for '${w}': ${e}",
             ("w", validator_name)("e", e.to_detail_string()));
    }
}

// ─── disable_on_shutdown ───────────────────────────────────────────────────────
// Called once during plugin_shutdown(). For every configured validator that is
// still ENABLED on-chain (non-null signing key) we broadcast a validator_update
// that nulls the signing key, so the network stops scheduling this node while it
// is offline (avoiding missed blocks). We cannot get a peer-level ACK, so we make
// a best effort to guarantee delivery: broadcast_transaction() blocks until the
// bytes are written into every peer socket, and we keep re-broadcasting for a
// short grace window so the txs survive a transient relay miss and have time to
// spread before this node disappears.
void validator_guard_plugin::impl::disable_on_shutdown() {
    if (!_enabled || _validator_configs.empty()) return;
    if (!_disable_on_shutdown) {
        ilog("validator_guard: disable-on-shutdown is OFF, skipping");
        return;
    }

    static const graphene::protocol::public_key_type null_key;

    // Build and sign every disable transaction under a single read lock, then
    // release the lock BEFORE broadcasting. broadcast_transaction() dispatches to
    // the P2P thread and waits, and that thread may need the write lock to apply
    // an incoming block — holding a read lock across the broadcast could deadlock.
    std::vector<std::pair<std::string, graphene::chain::signed_transaction>> txs;
    try {
        db().with_weak_read_lock([&]() {
            const auto& idx = db()
                .get_index<graphene::chain::validator_index>()
                .indices()
                .get<graphene::chain::by_name>();

            // Expiration must outlive the whole grace window plus a margin so the
            // tx stays valid while peers relay and include it after we are gone.
            fc::time_point_sec expiration(
                graphene::time::now() + fc::seconds(_shutdown_grace_sec + 120));

            for (const auto& entry : _validator_configs) {
                const std::string& name   = entry.first;
                const validator_info& cfg  = entry.second;

                auto itr = idx.find(name);
                if (itr == idx.end()) continue;
                if (itr->signing_key == null_key) continue;   // already disabled

                graphene::protocol::validator_update_operation op;
                op.owner             = name;
                op.url               = std::string(itr->url.begin(), itr->url.end());
                op.block_signing_key = null_key;

                graphene::chain::signed_transaction tx;
                tx.operations.push_back(op);
                tx.set_expiration(expiration);
                tx.set_reference_block(db().head_block_id());
                tx.sign(cfg.active_key, db().get_chain_id());

                txs.emplace_back(name, std::move(tx));
            }
        });
    } catch (const fc::exception& e) {
        elog("validator_guard: failed to build shutdown disable transactions: ${e}",
             ("e", e.to_detail_string()));
        return;
    }

    if (txs.empty()) {
        ilog("validator_guard: no enabled validators to disable on shutdown");
        return;
    }

    const uint32_t peers = p2p_.get_connections_count();
    if (peers == 0) {
        wlog("validator_guard: shutting down with NO connected peers — disable "
             "transactions for ${n} validator(s) cannot be propagated", ("n", txs.size()));
        return;
    }

    ilog("validator_guard: graceful shutdown — disabling ${n} enabled validator(s) "
         "across ${p} peer(s)", ("n", txs.size())("p", peers));

    // Initial broadcast. Each call blocks until the tx bytes are written into
    // every active peer's socket.
    for (const auto& nt : txs) {
        try {
            ilog("validator_guard: broadcasting shutdown disable for '${w}' [ID: ${id}]",
                 ("w", nt.first)("id", nt.second.id()));
            p2p_.broadcast_transaction(nt.second);
        } catch (const fc::exception& e) {
            elog("validator_guard: shutdown disable broadcast FAILED for '${w}': ${e}",
                 ("w", nt.first)("e", e.to_detail_string()));
        }
    }

    // Grace window: keep the disable txs alive in the network by re-broadcasting
    // them periodically. Stop early if every peer goes away (nothing left to send
    // to). Re-broadcast every 3 seconds — the txs are idempotent (same id), so the
    // peer mempools simply ignore duplicates.
    for (uint32_t elapsed = 0; elapsed < _shutdown_grace_sec; ++elapsed) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        if (p2p_.get_connections_count() == 0) {
            wlog("validator_guard: lost all peers ${e}s into shutdown grace, "
                 "stopping re-broadcast", ("e", elapsed + 1));
            return;
        }

        if ((elapsed + 1) % 3 == 0) {
            for (const auto& nt : txs) {
                try { p2p_.broadcast_transaction(nt.second); }
                catch (const fc::exception&) { /* peer churn during shutdown — ignore */ }
            }
        }
    }

    ilog("validator_guard: shutdown disable complete for ${n} validator(s)",
         ("n", txs.size()));
}

// ─── plugin lifecycle ────────────────────────────────────────────────────────

validator_guard_plugin::validator_guard_plugin()  = default;
validator_guard_plugin::~validator_guard_plugin() = default;

void validator_guard_plugin::set_program_options(
        bpo::options_description& cli,
        bpo::options_description& cfg)
{
    cfg.add_options()
        ("validator-guard-enabled",
         bpo::value<bool>()->default_value(true),
         "Enable validator key auto-restore. "
         "When true, the plugin monitors configured validators and sends "
         "validator_update if the on-chain signing key is reset to null.")
        ("witness-guard-enabled",  // DEPRECATED alias
         bpo::value<bool>(),
         "[DEPRECATED] Use validator-guard-enabled.")

        ("validator-guard-validator",
         bpo::value<std::vector<std::string>>()->composing()->multitoken(),
         "Validator to monitor: name signing_wif active_wif (triplets). Can be specified multiple times.")
        ("witness-guard-witness",  // DEPRECATED alias
         bpo::value<std::vector<std::string>>()->composing()->multitoken(),
         "[DEPRECATED] Use validator-guard-validator.")

        ("validator-guard-interval",
         bpo::value<uint32_t>()->default_value(20),
         "How often to check validator signing keys, in blocks (default: 20 ≈ 60s).")
        ("witness-guard-interval",  // DEPRECATED alias
         bpo::value<uint32_t>(),
         "[DEPRECATED] Use validator-guard-interval.")

        ("validator-guard-disable",
         bpo::value<uint32_t>()->default_value(5),
         "Number of consecutive blocks produced by the same validator from this node "
         "before automatically disabling that validator (setting signing key to null). "
         "Set to 0 to disable this feature. (default: 5)")
        ("witness-guard-disable",  // DEPRECATED alias
         bpo::value<uint32_t>(),
         "[DEPRECATED] Use validator-guard-disable.")

        ("validator-guard-disable-on-shutdown",
         bpo::value<bool>()->default_value(true),
         "On graceful shutdown, broadcast a validator_update that nulls the signing key "
         "of every configured validator still enabled on-chain, so the network stops "
         "scheduling this node while it is offline. (default: true)")

        ("validator-guard-shutdown-grace",
         bpo::value<uint32_t>()->default_value(10),
         "Seconds to keep re-broadcasting the shutdown disable transactions so peers "
         "have time to receive and relay them. Keep below the container/process kill "
         "timeout. (default: 10)")
        ;

    cli.add(cfg);
}

void validator_guard_plugin::plugin_initialize(
        const bpo::variables_map& options)
{
    try {
        ilog("validator_guard: plugin_initialize() begin");
        pimpl = std::make_unique<impl>();

        // enabled flag — prefer validator-guard-enabled, fall back to deprecated witness-guard-enabled
        if (options.count("validator-guard-enabled")) {
            pimpl->_enabled = options["validator-guard-enabled"].as<bool>();
        } else if (options.count("witness-guard-enabled")) {
            wlog("Config option 'witness-guard-enabled' is deprecated, use 'validator-guard-enabled'.");
            pimpl->_enabled = options["witness-guard-enabled"].as<bool>();
        }
        if (!pimpl->_enabled) {
            ilog("validator_guard: disabled via config, skipping initialization");
            return;
        }

        // Detect whether enable-stale-production is active.
        // When stale production is on the operator intentionally produces on a
        // minority fork, so auto-restoring signing keys would be dangerous.
        // This flag is auto-cleared once the network becomes healthy (>= 33%).
        if (options.count("enable-stale-production")) {
            pimpl->_stale_production_config = options["enable-stale-production"].as<bool>();
        }
        if (pimpl->_stale_production_config) {
            wlog("validator_guard: enable-stale-production detected — "
                 "auto-restore is DISABLED until network participation >= 33%%");
        }

        // check interval (in blocks) — prefer validator-guard-interval
        if (options.count("witness-guard-interval") && !options.count("validator-guard-interval")) {
            wlog("Config option 'witness-guard-interval' is deprecated, use 'validator-guard-interval'.");
            pimpl->_check_interval = options["witness-guard-interval"].as<uint32_t>();
        } else {
            pimpl->_check_interval = options["validator-guard-interval"].as<uint32_t>();
        }
        if (pimpl->_check_interval == 0) pimpl->_check_interval = 1;

        // disable threshold — prefer validator-guard-disable
        if (options.count("witness-guard-disable") && !options.count("validator-guard-disable")) {
            wlog("Config option 'witness-guard-disable' is deprecated, use 'validator-guard-disable'.");
            pimpl->_disable_threshold = options["witness-guard-disable"].as<uint32_t>();
        } else {
            pimpl->_disable_threshold = options["validator-guard-disable"].as<uint32_t>();
        }
        if (pimpl->_disable_threshold > 0) {
            ilog("validator_guard: auto-disable enabled — will disable validator after ${n} consecutive blocks",
                 ("n", pimpl->_disable_threshold));
        } else {
            ilog("validator_guard: auto-disable feature is OFF (validator-guard-disable = 0)");
        }

        // graceful-shutdown disable + propagation grace window
        if (options.count("validator-guard-disable-on-shutdown")) {
            pimpl->_disable_on_shutdown = options["validator-guard-disable-on-shutdown"].as<bool>();
        }
        if (options.count("validator-guard-shutdown-grace")) {
            pimpl->_shutdown_grace_sec = options["validator-guard-shutdown-grace"].as<uint32_t>();
        }
        if (pimpl->_disable_on_shutdown) {
            ilog("validator_guard: disable-on-shutdown enabled — will null signing keys "
                 "on shutdown and re-broadcast for ${s}s", ("s", pimpl->_shutdown_grace_sec));
        }

        // validator configs — each entry is a JSON triplet: ["name", "signing_wif", "active_wif"]
        // Accept both validator-guard-validator (new) and witness-guard-witness (deprecated).
        std::vector<std::string> guard_entries;
        if (options.count("validator-guard-validator"))
            for (auto& e : options["validator-guard-validator"].as<std::vector<std::string>>())
                guard_entries.push_back(e);
        if (options.count("witness-guard-witness")) {
            wlog("Config option 'witness-guard-witness' is deprecated, use 'validator-guard-validator'.");
            for (auto& e : options["witness-guard-witness"].as<std::vector<std::string>>())
                guard_entries.push_back(e);
        }
        if (!guard_entries.empty()) {
            const auto& entries = guard_entries;
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

                    pimpl->_validator_configs[name] = { *sign_priv, *active_priv };

                    ilog("validator_guard: monitoring validator '${w}' (signing key: ${k})",
                         ("w", name)("k", sign_priv->get_public_key()));

                } catch (const fc::exception& e) {
                    elog("validator_guard: failed to parse validator entry '${entry}': ${e}",
                         ("entry", entry)("e", e.to_detail_string()));
                }
            }
        }

        if (pimpl->_validator_configs.empty()) {
            wlog("validator_guard: no validators configured for monitoring");
        }

        ilog("validator_guard: plugin_initialize() end — "
             "monitoring ${n} validator(s), interval=${i} blocks",
             ("n", pimpl->_validator_configs.size())("i", pimpl->_check_interval));

    } FC_LOG_AND_RETHROW()
}

void validator_guard_plugin::plugin_startup() {
    ilog("validator_guard: plugin_startup() begin");

    if (!pimpl->_enabled || pimpl->_validator_configs.empty()) {
        ilog("validator_guard: nothing to monitor, plugin inactive");
        return;
    }
    // Verify on-chain authority for every configured validator
    // The chain database is open at this point so we can query account objects.
    for (auto it = pimpl->_validator_configs.begin(); it != pimpl->_validator_configs.end(); ) {
        const std::string& name = it->first;
        const impl::validator_info& config = it->second;

        try {
           // VIZ stores authorities in account_authority_object (not account_object)
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
                elog("validator_guard: WARNING: Configured active key for validator '${w}' "
                     "does NOT have authority on-chain. Restoration will fail.", ("w", name));
            }
            ++it;
        } catch (const fc::exception& e) {
            elog("validator_guard: account '${w}' not found on chain, removing from monitor list", ("w", name));
            it = pimpl->_validator_configs.erase(it);
        }
    }

    if (pimpl->_validator_configs.empty()) return;

    // Run an initial check at startup; mark done if the node is already in sync
    // (check_and_restore_internal returns true when the node is synchronized).
    if (pimpl->check_and_restore_internal()) {
        pimpl->_initial_check_done = true;
    }

    // Subscribe to every new applied block for ongoing monitoring
    pimpl->_applied_block_connection = pimpl->db().applied_block.connect(
    [this](const graphene::chain::signed_block& b) {
        if (!pimpl->_enabled) return;

        // 0. Consecutive-block auto-disable check:
        //    If one of our validators produces N consecutive blocks, disable it.
        if (pimpl->_disable_threshold > 0 && pimpl->_validator_configs.count(b.validator)) {
            const std::string& producer = b.validator;
            // Reset counters for all OTHER validators — only the current producer's streak continues
            for (auto& entry : pimpl->_consecutive_blocks) {
                if (entry.first != producer) entry.second = 0;
            }
            // Increment the consecutive counter for this validator
            pimpl->_consecutive_blocks[producer]++;
            const uint32_t count = pimpl->_consecutive_blocks[producer];

            if (count >= pimpl->_disable_threshold) {
                // Already auto-disabled? Skip repeated broadcasts
                if (!pimpl->_auto_disabled_validators.count(producer)) {
                    wlog("validator_guard: validator '${w}' produced ${c} consecutive blocks — "
                         "auto-disabling (threshold=${t})",
                         ("w", producer)("c", count)("t", pimpl->_disable_threshold));

                    // Look up the validator object and send disable transaction
                    const auto& idx = pimpl->db()
                        .get_index<graphene::chain::validator_index>()
                        .indices()
                        .get<graphene::chain::by_name>();
                    auto itr = idx.find(producer);
                    if (itr != idx.end()) {
                        const auto& config = pimpl->_validator_configs.at(producer);
                        pimpl->send_validator_disable(producer, *itr, config);
                    }
                }
            }
        }
        else {
            // Block produced by a different validator — reset ALL our consecutive counters
            // because the streak is broken
            if (!pimpl->_consecutive_blocks.empty()) {
                for (auto& entry : pimpl->_consecutive_blocks) {
                    entry.second = 0;
                }
            }
        }

        // 1. Scan the block for any of our pending restore transactions
        if (!pimpl->_pending_confirmations.empty()) {
            std::vector<graphene::chain::transaction_id_type> confirmed_ids;
            for (const auto& tx : b.transactions) {
                if (pimpl->_pending_confirmations.count(tx.id()))
                    confirmed_ids.push_back(tx.id());
            }

            for (const auto& id : confirmed_ids) {
                auto it = pimpl->_pending_confirmations.find(id);
                if (it != pimpl->_pending_confirmations.end()) {
                    const auto w_name = it->second.first;
                    pimpl->_restore_pending.erase(w_name);
                    pimpl->_pending_confirmations.erase(it);
                    ilog("validator_guard: CONFIRMED restoration for '${w}' in block #${n} [TX: ${id}]",
                         ("w", w_name)("n", b.block_num())("id", id));
                }
            }
        }

        // 2. Look-ahead: if one of our validators is scheduled within the next 3 slots
        //    run an immediate check so the key is restored before the slot arrives
        bool scheduled_soon = false;
        if (pimpl->_initial_check_done) {
            for (uint32_t i = 1; i <= 3; ++i) {
                if (pimpl->_validator_configs.count(pimpl->db().get_scheduled_validator(i))) {
                    scheduled_soon = true;
                    break;
                }
            }
        }

        // 3. Decide whether to run the periodic check:
        //    - scheduled_soon: one of our validators produces very soon
        //    - !_initial_check_done: keep probing every 10 blocks until the node syncs
        //    - regular interval: every _check_interval blocks
        if (scheduled_soon) {
            pimpl->check_and_restore_internal();
        }
        else if (!pimpl->_initial_check_done && (b.block_num() % 10 == 0)) {
            if (pimpl->check_and_restore_internal()) {
                pimpl->_initial_check_done = true;
            }
        }
        else if (b.block_num() % pimpl->_check_interval == 0) {
            pimpl->check_and_restore_internal();
        }
    }
);

    ilog("validator_guard: plugin_startup() end — active");
}

void validator_guard_plugin::plugin_shutdown() {
    // Disconnect from applied_block first so the consecutive-block guard does not
    // fire (and double-broadcast) while we run the graceful-shutdown disable below.
    if (pimpl && pimpl->_applied_block_connection.connected()) {
        pimpl->_applied_block_connection.disconnect();
    }

    // Gracefully disable our validators and make a best effort to ensure the
    // disable transactions reach peers before we exit.
    if (pimpl) {
        try {
            pimpl->disable_on_shutdown();
        } catch (const fc::exception& e) {
            elog("validator_guard: disable_on_shutdown failed: ${e}", ("e", e.to_detail_string()));
        } catch (...) {
            elog("validator_guard: disable_on_shutdown failed with an unknown exception");
        }
    }

    ilog("validator_guard: plugin_shutdown()");
}

} // validator_guard
} // plugins
} // graphene