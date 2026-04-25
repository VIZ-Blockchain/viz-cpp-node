#include <graphene/plugins/witness_guard/witness_guard.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/witness_objects.hpp>
#include <graphene/chain/chain_object_types.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include <graphene/time/time.hpp>
#include <fc/string.hpp> // Adăugat pentru conversia fc::shared_string la std::string
#include <fc/io/json.hpp>
#include <sstream>

#include <appbase/application.hpp>

#include <fc/smart_ref_impl.hpp>

namespace graphene {
namespace plugins {
namespace witness_guard {

namespace bpo = boost::program_options;

// ─── impl ────────────────────────────────────────────────────────────────────

struct witness_guard_plugin::impl {
    impl()
        : chain_(appbase::app().get_plugin<graphene::plugins::chain::plugin>())
        , p2p_(appbase::app().get_plugin<graphene::plugins::p2p::p2p_plugin>())
    {}

    graphene::chain::database& db() { return chain_.db(); }

    // ── config ────────────────────────────────────────────────────────────────
    bool     _enabled        = true;
    uint32_t _check_interval = 20;   // blocks between checks

    struct witness_info {
        fc::ecc::private_key signing_key;
        fc::ecc::private_key active_key;
    };

    // Mapping witness_name -> config (keys)
    std::map<std::string, witness_info> _witness_configs;

    // anti-spam guard: don't send a second time until the node restarts
    std::set<std::string> _restore_sent;

    // ── core ──────────────────────────────────────────────────────────────────
    void check_and_restore();
    void send_witness_update(const std::string& witness_name,
                             const graphene::chain::witness_object& obj,
                             const witness_info& config);

    graphene::plugins::chain::plugin&   chain_;
    graphene::plugins::p2p::p2p_plugin& p2p_;
};

// ─── check_and_restore ───────────────────────────────────────────────────────

void witness_guard_plugin::impl::check_and_restore() {
    auto& database = db();

    // Check only if the node is synchronized
    // (head block within the last 2 * CHAIN_BLOCK_INTERVAL seconds)
    const auto head_time = database.head_block_time();
    const auto now       = fc::time_point_sec(graphene::time::now());
    if (head_time < now - fc::seconds(CHAIN_BLOCK_INTERVAL * 2)) {
        dlog("witness_guard: node not in sync, skipping check");
        return;
    }

    const auto& idx = database
        .get_index<graphene::chain::witness_index>()
        .indices()
        .get<graphene::chain::by_name>();

    static const graphene::protocol::public_key_type null_key;

    for (const auto& [name, config] : _witness_configs) {
        auto itr = idx.find(name);
        if (itr == idx.end()) {
            wlog("witness_guard: witness '${w}' not found in database", ("w", name));
            continue;
        }

        if (itr->signing_key != null_key) {
        // If the key is valid on-chain, reset the guard for this witness.
        // This allows the plugin to intervene again if the key becomes null later.
            _restore_sent.erase(name);
            continue;
        }

    // If restore has already been sent and the transaction is not yet included in a block, wait.
        if (_restore_sent.count(name)) continue;

        ilog("witness_guard: '${w}' has null signing key on-chain — initiating restore",
             ("w", name));
        send_witness_update(name, *itr, config);
    }
}

// ─── send_witness_update ─────────────────────────────────────────────────────

void witness_guard_plugin::impl::send_witness_update(
        const std::string& witness_name,
        const graphene::chain::witness_object& obj,
        const witness_info& config)
{
    try {
        const auto signing_pub = config.signing_key.get_public_key();
        const auto& active_priv = config.active_key;

        // Current URL from blockchain (do not overwrite)
        std::string url = std::string(obj.url.c_str());

        // Construct the operation
        graphene::protocol::witness_update_operation op;
        op.owner            = witness_name;
        op.url              = url;
        op.block_signing_key = signing_pub;

        // Construct the transaction
        graphene::chain::signed_transaction tx;
        tx.operations.push_back(op);
        tx.set_expiration(db().head_block_time() + fc::seconds(30));
        tx.set_reference_block(db().head_block_id());

        // Sign with the active key
        tx.sign(active_priv, db().get_chain_id());

        ilog("witness_guard: broadcasting witness_update for '${w}' "
             "— restoring signing key to ${k}",
             ("w", witness_name)("k", signing_pub));

        // Only network broadcast
            p2p_.broadcast_transaction(tx);

        // Mark as sent — do not send again in this session
        _restore_sent.insert(witness_name);

        ilog("witness_guard: witness_update for '${w}' sent successfully", ("w", witness_name));

    } catch (const fc::exception& e) {
        elog("witness_guard: witness_update FAILED for '${w}': ${e}",
             ("w", witness_name)("e", e.to_detail_string()));
        // Do not add to _restore_sent — we will retry at the next check
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

        // enabled flag
        if (options.count("witness-guard-enabled")) {
            pimpl->_enabled = options["witness-guard-enabled"].as<bool>();
        }
        if (!pimpl->_enabled) {
            ilog("witness_guard: disabled via config, skipping initialization");
            return;
        }

        // interval
        pimpl->_check_interval = options["witness-guard-interval"].as<uint32_t>();
        if (pimpl->_check_interval == 0) pimpl->_check_interval = 1;

        // witness configs (triplets)
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
                    
                    ilog("witness_guard: monitoring witness '${w}' (signing key: ${k})",
                         ("w", name)("k", sign_priv->get_public_key()));

                } catch (const fc::exception& e) {
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

    // Hook on every applied block
    pimpl->db().applied_block.connect(
    [this](const graphene::chain::signed_block& b) {
        if (!pimpl->_enabled) return;
        if (b.block_num() % pimpl->_check_interval == 0) {
            pimpl->check_and_restore();
        }
    }
);

    ilog("witness_guard: plugin_startup() end — active");
}

void witness_guard_plugin::plugin_shutdown() {
    ilog("witness_guard: plugin_shutdown()");
}

} // witness_guard
} // plugins
} // graphene