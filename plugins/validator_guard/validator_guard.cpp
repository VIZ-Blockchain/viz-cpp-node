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
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>

#include <cstdio>
#include <unistd.h>

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
    
    std::map<std::string, uint32_t> _auto_disable_count; // Number of times each validator was auto-disabled (due to consecutive blocks)
    uint32_t _auto_disable_max = 3; // Limit after which we stop performing auto-restore entirely
    
    

    // Per-validator consecutive block counter: validator_name -> count of consecutive blocks produced
    std::map<std::string, uint32_t> _consecutive_blocks;

    // Validators that have been auto-disabled and are awaiting manual intervention
    // (we should NOT auto-restore them — only a restart or explicit action should re-enable)
    std::set<std::string> _auto_disabled_validators;

    // Operator-intent latch: validators disabled through node_admin stay disabled.
    // Unlike _auto_disabled_validators this is NOT about consecutive blocks and is not
    // cleared by the periodic check; it survives restarts through a file in the data dir,
    // otherwise a redeploy would silently re-enable a validator the operator turned off.
    std::set<std::string> _operator_hold;
    mutable std::mutex _state_mtx;      // guards _operator_hold
    std::string _hold_path;
    std::atomic<bool> _startup_done{false};

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

    // ── operator actions (node_admin) ─────────────────────────────────────────
    bool chain_actions_available() const {
        return _enabled && _startup_done.load() && !_validator_configs.empty();
    }
    bool is_held(const std::string& name) const;
    void load_operator_holds();
    void save_operator_holds_locked();   // caller holds _state_mtx
    std::vector<validator_status> validators_status() const;
    std::vector<validator_action_result> apply_validator_actions(
        const std::vector<validator_action_request>& requests);

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

        // Operator disabled this validator through node_admin: the hold latch suppresses
        // auto-restore, otherwise the periodic check would re-enable it within a minute.
        if (is_held(name)) continue;

        // Signing key is present on-chain — nothing to do
        if (itr->signing_key != null_key) {
            _restore_pending.erase(name);
            // If this validator was auto-disabled and the key is now present on-chain,
            // clear the auto-disabled flag (operator must have manually restored)
            _auto_disabled_validators.erase(name);
            _auto_disable_count.erase(name); 
            continue;
        }

        // If this validator was auto-disabled by the consecutive-block guard,
        // do NOT auto-restore it — the operator must investigate and restart
        if (_auto_disabled_validators.count(name)) {
            uint32_t count = _auto_disable_count.count(name) ? _auto_disable_count.at(name) : 0;
            if (_auto_disable_max > 0 && count >= _auto_disable_max) {
                dlog("validator_guard: '${w}' reached max auto-disable limit (${c}/${m}), "
                     "blocking auto-restore", ("w", name)("c", count)("m", _auto_disable_max));
                continue;
            }
            // Allow auto-restore because count is below limit. 
            // We clear the flag so the logic below proceeds with restoration.
            _auto_disabled_validators.erase(name); 
            _consecutive_blocks[name] = 0; // reset consecutive block count on auto-restore
            ilog("validator_guard: '${w}' was auto-disabled, attempting automatic restore (${c}/${m})", ("w", name)("c", count)("m", _auto_disable_max));
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
        _auto_disable_count[validator_name]++;

        const uint32_t cnt = _auto_disable_count[validator_name];
        if (_auto_disable_max > 0 && cnt >= _auto_disable_max) {
            wlog("validator_guard: '${w}' was auto-disabled ${c} times — "
                "auto-restore PERMANENTLY BLOCKED until node restart",
                ("w", validator_name)("c", cnt));
        } else {
            ilog("validator_guard: '${w}' disabled (count=${c}/${m})",
                ("w", validator_name)("c", cnt)("m", _auto_disable_max));
        }
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

// ─── operator hold latch ─────────────────────────────────────────────────────
// A hold is "the operator turned this validator off through node_admin". It is persisted so
// a restart/redeploy cannot silently re-enable block production, and it is distinct from
// _auto_disabled_validators (which tracks consecutive blocks and clears itself).

bool validator_guard_plugin::impl::is_held(const std::string& name) const {
    std::lock_guard<std::mutex> lock(_state_mtx);
    return _operator_hold.count(name) > 0;
}

void validator_guard_plugin::impl::load_operator_holds() {
    std::set<std::string> holds;
    if (!_hold_path.empty()) {
        std::ifstream in(_hold_path);
        std::string line;
        while (std::getline(in, line)) {
            // Format: "<unixtime> <name>". Account names cannot contain spaces.
            const std::size_t sp = line.find(' ');
            if (sp == std::string::npos) continue;
            std::string name = line.substr(sp + 1);
            while (!name.empty() && (name.back() == '\r' || name.back() == ' ')) name.pop_back();
            if (!name.empty()) holds.insert(name);
        }
        if (!holds.empty()) {
            ilog("validator_guard: restored ${n} operator hold(s) from ${p}",
                 ("n", holds.size())("p", _hold_path));
        }
    }
    std::lock_guard<std::mutex> lock(_state_mtx);
    _operator_hold = std::move(holds);
}

void validator_guard_plugin::impl::save_operator_holds_locked() {
    if (_hold_path.empty()) return;
    const std::string tmp = _hold_path + ".tmp." + std::to_string(::getpid());
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            elog("validator_guard: cannot write hold file ${p}; holds live in memory only", ("p", _hold_path));
            return;
        }
        const uint32_t now = static_cast<uint32_t>(graphene::time::now().sec_since_epoch());
        for (const auto& name : _operator_hold) out << now << ' ' << name << '\n';
        out.flush();
        if (!out) {
            elog("validator_guard: failed writing hold file ${p}", ("p", _hold_path));
            std::remove(tmp.c_str());
            return;
        }
    }
    if (std::rename(tmp.c_str(), _hold_path.c_str()) != 0) {
        elog("validator_guard: failed replacing hold file ${p} (tmp ${t})", ("p", _hold_path)("t", tmp));
        std::remove(tmp.c_str());
    }
}

// ─── validators_status ───────────────────────────────────────────────────────
// Read-only snapshot for node_admin. Called from the admin (webserver) thread, so the
// chain database is read under a weak read lock; _validator_configs is immutable after
// initialization, and the hold set is copied under its own mutex first (no lock nesting
// with the chain lock on the write side).

std::vector<validator_status> validator_guard_plugin::impl::validators_status() const {
    std::vector<validator_status> out;
    // _validator_configs is still being pruned while startup runs; report nothing until
    // it is final instead of racing with the erase.
    if (!_enabled || !_startup_done.load()) return out;

    std::set<std::string> holds;
    {
        std::lock_guard<std::mutex> lock(_state_mtx);
        holds = _operator_hold;
    }

    db().with_weak_read_lock([&]() {
        static const graphene::protocol::public_key_type null_key;
        const auto& idx = db()
            .get_index<graphene::chain::validator_index>()
            .indices()
            .get<graphene::chain::by_name>();

        // Best effort: the schedule may be empty right after startup.
        std::vector<std::string> next_slots;
        for (uint32_t i = 1; i <= 3; ++i) {
            try { next_slots.push_back(db().get_scheduled_validator(i)); }
            catch (...) { next_slots.push_back(std::string()); }
        }

        for (const auto& entry : _validator_configs) {
            validator_status st;
            st.name = entry.first;
            st.hold = holds.count(st.name) > 0;
            st.configured_key = static_cast<std::string>(
                graphene::protocol::public_key_type(entry.second.signing_key.get_public_key()));

            auto itr = idx.find(st.name);
            if (itr != idx.end()) {
                st.on_chain = true;
                st.enabled = (itr->signing_key != null_key);
                if (st.enabled) st.signing_key = static_cast<std::string>(itr->signing_key);
            }

            try {
                const auto& auth = db().get<graphene::chain::account_authority_object,
                                            graphene::chain::by_account>(st.name);
                const auto pk = entry.second.active_key.get_public_key();
                for (const auto& k : auth.active.key_auths) {
                    if (k.first == pk) { st.active_authority = true; break; }
                }
            } catch (...) {
                // A validator whose account is gone is reported as on_chain=false above.
            }

            for (std::size_t i = 0; i < next_slots.size(); ++i) {
                if (next_slots[i] == st.name) { st.scheduled_in = static_cast<int32_t>(i + 1); break; }
            }
            out.push_back(st);
        }
    });
    return out;
}

// ─── apply_validator_actions ─────────────────────────────────────────────────
// Builds and signs every validator_update under one read lock, then releases the lock and
// broadcasts — broadcast_transaction() dispatches to the P2P thread and waits, and that
// thread may need the write lock to apply an incoming block (same rule as disable_on_shutdown).
// The local chain also gets the transaction through accept_transaction(), so a single-node
// testnet includes it without peers.

std::vector<validator_action_result> validator_guard_plugin::impl::apply_validator_actions(
        const std::vector<validator_action_request>& requests) {
    std::vector<validator_action_result> results(requests.size());
    for (std::size_t i = 0; i < requests.size(); ++i) {
        results[i].name = requests[i].name;
        switch (requests[i].action) {
            case validator_action::enable:  results[i].action = "enable";  break;
            case validator_action::disable: results[i].action = "disable"; break;
            case validator_action::set_key: results[i].action = "set_key"; break;
        }
    }

    auto fail_all = [&](const std::string& why) {
        for (auto& r : results) if (r.error.empty()) r.error = why;
        return results;
    };

    if (!_enabled) return fail_all("validator_guard is disabled in config (validator-guard-enabled = false)");
    if (!_startup_done.load() || _validator_configs.empty())
        return fail_all("validator_guard is not active: no validator is configured with an active key");

    struct pending_action {
        std::size_t index = 0;
        bool has_tx = false;
        graphene::chain::signed_transaction tx;
    };
    std::vector<pending_action> pending;
    static const graphene::protocol::public_key_type null_key;

    try {
        db().with_weak_read_lock([&]() {
            const auto& idx = db()
                .get_index<graphene::chain::validator_index>()
                .indices()
                .get<graphene::chain::by_name>();

            for (std::size_t i = 0; i < requests.size(); ++i) {
                const validator_action_request& req = requests[i];
                validator_action_result& res = results[i];

                auto cfg_it = _validator_configs.find(req.name);
                if (cfg_it == _validator_configs.end()) {
                    res.error = "validator '" + req.name + "' is not configured in validator-guard";
                    continue;
                }
                auto itr = idx.find(req.name);
                if (itr == idx.end()) {
                    res.error = "validator '" + req.name + "' does not exist on chain";
                    continue;
                }

                const bool current_enabled = (itr->signing_key != null_key);
                const std::string current = current_enabled
                    ? static_cast<std::string>(itr->signing_key) : std::string();

                if (req.check_expected && current != req.expected_key) {
                    res.error = "chain state changed: signing key is '" +
                                (current.empty() ? std::string("null") : current) +
                                "', the plan expected '" +
                                (req.expected_key.empty() ? std::string("null") : req.expected_key) + "'";
                    continue;
                }

                graphene::protocol::public_key_type target;
                switch (req.action) {
                    case validator_action::enable:
                        target = graphene::protocol::public_key_type(
                            cfg_it->second.signing_key.get_public_key());
                        break;
                    case validator_action::disable:
                        target = null_key;
                        break;
                    case validator_action::set_key:
                        try {
                            target = graphene::protocol::public_key_type(req.signing_key);
                        } catch (const fc::exception& e) {
                            res.error = "invalid signing_key: " + e.to_string();
                            continue;
                        }
                        break;
                }

                const bool target_enabled = (target != null_key);
                if (current_enabled == target_enabled && (!target_enabled || itr->signing_key == target)) {
                    // Already in the desired state (the chain moved between plan and apply).
                    res.ok = true;
                    res.changed = false;
                    pending_action pa;
                    pa.index = i;
                    pending.push_back(pa);
                    continue;
                }

                graphene::protocol::validator_update_operation op;
                op.owner            = req.name;
                op.url              = std::string(itr->url.begin(), itr->url.end());
                op.block_signing_key = target;

                graphene::chain::signed_transaction tx;
                tx.operations.push_back(op);
                // Operator window: long enough for a couple of blocks of retry, short
                // enough that a stale transaction cannot resurface much later.
                tx.set_expiration(fc::time_point_sec(graphene::time::now() + fc::seconds(60)));
                tx.set_reference_block(db().head_block_id());
                tx.sign(cfg_it->second.active_key, db().get_chain_id());

                pending_action pa;
                pa.index = i;
                pa.has_tx = true;
                pa.tx = tx;
                pending.push_back(pa);
            }
        });
    } catch (const fc::exception& e) {
        return fail_all(std::string("failed to read chain state: ") + e.to_string());
    } catch (const std::exception& e) {
        return fail_all(std::string("failed to read chain state: ") + e.what());
    }

    for (auto& pa : pending) {
        if (!pa.has_tx) continue;
        validator_action_result& res = results[pa.index];
        const std::string tx_id = pa.tx.id().str();

        std::string accept_error;
        try {
            chain_.accept_transaction(pa.tx);
        } catch (const fc::exception& e) {
            accept_error = e.to_detail_string();
        } catch (const std::exception& e) {
            accept_error = e.what();
        }

        try {
            p2p_.broadcast_transaction(pa.tx);
        } catch (const fc::exception& e) {
            res.ok = false;
            res.tx_id = tx_id;
            res.error = "broadcast failed: " + e.to_detail_string();
            if (!accept_error.empty()) res.error += "; local accept: " + accept_error;
            continue;
        } catch (const std::exception& e) {
            res.ok = false;
            res.tx_id = tx_id;
            res.error = std::string("broadcast failed: ") + e.what();
            continue;
        }

        if (!accept_error.empty()) {
            // The tx may still reach the network, but this node refused it locally —
            // calling that a success would be a lie. Name both facts.
            res.ok = false;
            res.tx_id = tx_id;
            res.error = "local accept failed: " + accept_error;
            continue;
        }

        res.ok = true;
        res.changed = true;
        res.tx_id = tx_id;
        ilog("validator_guard: operator ${a} for '${w}' [ID: ${id}]",
             ("a", res.action)("w", res.name)("id", tx_id));
    }

    // The hold follows the outcome, never the intent: a failed disable must not leave a
    // latch that blocks auto-restore (that would disable the validator only in our heads).
    {
        std::lock_guard<std::mutex> lock(_state_mtx);
        bool dirty = false;
        for (std::size_t i = 0; i < results.size(); ++i) {
            if (!results[i].ok) continue;
            const bool want_hold = (requests[i].action == validator_action::disable);
            if (want_hold && !_operator_hold.count(results[i].name)) {
                _operator_hold.insert(results[i].name);
                dirty = true;
            } else if (!want_hold && _operator_hold.erase(results[i].name) > 0) {
                dirty = true;
            }
        }
        if (dirty) save_operator_holds_locked();
    }

    return results;
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

         ("validator-guard-disable-max",
         bpo::value<uint32_t>()->default_value(3),
         "Number of times a validator can be auto-disabled before auto-restore is "
         "permanently suppressed until node restart. Set to 0 to disable this limit. (default: 3)")
        ;

    cli.add(cfg);
}

void validator_guard_plugin::plugin_initialize(
        const bpo::variables_map& options)
{
    try {
        ilog("validator_guard: plugin_initialize() begin");
        pimpl = std::make_unique<impl>();
        pimpl->_hold_path =
            (appbase::app().data_dir() / "validator-guard-holds.txt").string();

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
        if (options.count("validator-guard-disable-max")) {
            pimpl->_auto_disable_max = options["validator-guard-disable-max"].as<uint32_t>();
        }
        ilog("validator_guard: auto-disable-max = ${n}", ("n", pimpl->_auto_disable_max));
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

    // Operator holds are read before the first periodic check: a restart must not
    // re-enable a validator that was disabled on purpose.
    pimpl->load_operator_holds();

    if (!pimpl->_enabled || pimpl->_validator_configs.empty()) {
        ilog("validator_guard: nothing to monitor, plugin inactive");
        pimpl->_startup_done = true;
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

    if (pimpl->_validator_configs.empty()) { pimpl->_startup_done = true; return; }

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
                    wlog("validator_guard: validator '${w}' produced ${c} consecutive blocks (potential local fork) — "
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

    pimpl->_startup_done = true;
    ilog("validator_guard: plugin_startup() end — active");
}

void validator_guard_plugin::plugin_shutdown() {
    if (pimpl) pimpl->_startup_done = false;
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

// ─── public API for node_admin ───────────────────────────────────────────────

bool validator_guard_plugin::chain_actions_available() const {
    return pimpl && pimpl->chain_actions_available();
}

std::vector<validator_status> validator_guard_plugin::validators_status() const {
    if (!pimpl) return {};
    return pimpl->validators_status();
}

std::vector<validator_action_result> validator_guard_plugin::apply_validator_actions(
        const std::vector<validator_action_request>& requests) {
    if (!pimpl) {
        std::vector<validator_action_result> out(requests.size());
        for (std::size_t i = 0; i < requests.size(); ++i) {
            out[i].name = requests[i].name;
            out[i].error = "validator_guard is not initialized";
        }
        return out;
    }
    return pimpl->apply_validator_actions(requests);
}

} // validator_guard
} // plugins
} // graphene