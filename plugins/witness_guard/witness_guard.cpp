#include <graphene/plugins/witness_guard/witness_guard.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/chain/witness_objects.hpp>
#include <graphene/chain/chain_object_types.hpp>
#include <graphene/protocol/chain_operations.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include <graphene/time/time.hpp>

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
    uint32_t _check_interval = 20;   // blocuri între verificări

    // Witness accounts de monitorizat
    std::set<std::string> _witnesses;

    // signing_pub → signing_priv  (cheia cu care semnează blocuri)
    std::map<graphene::protocol::public_key_type,
             fc::ecc::private_key> _signing_keys;

    // active_pub → active_priv   (cheia cu care semnăm witness_update)
    std::map<graphene::protocol::public_key_type,
             fc::ecc::private_key> _active_keys;

    // guard anti-spam: nu trimitem a doua oară până nodul nu repornește
    std::set<std::string> _restore_sent;

    // ── core ──────────────────────────────────────────────────────────────────
    void check_and_restore();
    void send_witness_update(const std::string& witness_name,
                             const graphene::chain::witness_object& obj);

    graphene::plugins::chain::plugin&   chain_;
    graphene::plugins::p2p::p2p_plugin& p2p_;
};

// ─── check_and_restore ───────────────────────────────────────────────────────

void witness_guard_plugin::impl::check_and_restore() {
    auto& database = db();

    // Verificăm doar dacă nodul e sincronizat
    // (head block în ultimele 2 * CHAIN_BLOCK_INTERVAL secunde)
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

    for (const auto& name : _witnesses) {
        // Deja am trimis restore pentru acesta la această sesiune
        if (_restore_sent.count(name)) continue;

        auto itr = idx.find(name);
        if (itr == idx.end()) {
            wlog("witness_guard: witness '${w}' not found in database", ("w", name));
            continue;
        }

        if (itr->signing_key != null_key) continue;   // cheia e ok

        ilog("witness_guard: '${w}' has null signing key on-chain — initiating restore",
             ("w", name));
        send_witness_update(name, *itr);
    }
}

// ─── send_witness_update ─────────────────────────────────────────────────────

void witness_guard_plugin::impl::send_witness_update(
        const std::string& witness_name,
        const graphene::chain::witness_object& obj)
{
    try {
        // 1. Găsim cheia de signing din config
        if (_signing_keys.empty()) {
            elog("witness_guard: no witness-guard-signing-key configured for '${w}'",
                 ("w", witness_name));
            return;
        }
        const auto& [signing_pub, signing_priv] = *_signing_keys.begin();

        // 2. Găsim cheia active din config
        if (_active_keys.empty()) {
            elog("witness_guard: no witness-guard-active-key configured for '${w}'",
                 ("w", witness_name));
            return;
        }
        const auto& [active_pub, active_priv] = *_active_keys.begin();

        // 3. URL curent din blockchain (nu îl suprascriem)
        std::string url = fc::to_string(obj.url);

        // 4. Construim operația
        graphene::protocol::witness_update_operation op;
        op.owner            = witness_name;
        op.url              = url;
        op.block_signing_key = signing_pub;

        // 5. Construim tranzacția
        graphene::chain::signed_transaction tx;
        tx.operations.push_back(op);
        tx.set_expiration(db().head_block_time() + fc::seconds(30));
        tx.set_reference_block(db().head_block_id());

        // 6. Semnăm cu cheia active
        tx.sign(active_priv, db().get_chain_id());

        ilog("witness_guard: broadcasting witness_update for '${w}' "
             "— restoring signing key to ${k}",
             ("w", witness_name)("k", signing_pub));

        // 7. Push local + broadcast în rețea
        db().push_transaction(tx, graphene::chain::database::skip_nothing);
        p2p_.broadcast_transaction(tx);

        // 8. Marcăm ca trimis — nu mai trimitem în această sesiune
        _restore_sent.insert(witness_name);

        ilog("witness_guard: witness_update for '${w}' sent successfully", ("w", witness_name));

    } catch (const fc::exception& e) {
        elog("witness_guard: witness_update FAILED for '${w}': ${e}",
             ("w", witness_name)("e", e.to_detail_string()));
        // Nu adăugăm în _restore_sent — vom reîncerca la următoarea verificare
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

        ("witness-guard-account",
         bpo::value<std::vector<std::string>>()->composing()->multitoken(),
         "Witness account name to monitor (can be specified multiple times).")

        ("witness-guard-signing-key",
         bpo::value<std::vector<std::string>>()->composing()->multitoken(),
         "WIF private key to restore as the signing key on-chain. "
         "This is the block-signing key (same as private-key in witness plugin).")

        ("witness-guard-active-key",
         bpo::value<std::vector<std::string>>()->composing()->multitoken(),
         "WIF private key with active authority on the witness account. "
         "Used to sign the witness_update transaction. "
         "WARNING: stored in plaintext in config.ini — use a dedicated key.")

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

        // witness accounts
        if (options.count("witness-guard-account")) {
            const auto& names =
                options["witness-guard-account"].as<std::vector<std::string>>();
            for (const auto& n : names) {
                pimpl->_witnesses.insert(n);
            }
        }

        // signing keys
        if (options.count("witness-guard-signing-key")) {
            const auto& keys =
                options["witness-guard-signing-key"].as<std::vector<std::string>>();
            for (const auto& wif : keys) {
                auto priv = graphene::utilities::wif_to_key(wif);
                FC_ASSERT(priv.valid(), "witness-guard-signing-key: invalid WIF key");
                pimpl->_signing_keys[priv->get_public_key()] = *priv;
            }
        }

        // active keys
        if (options.count("witness-guard-active-key")) {
            const auto& keys =
                options["witness-guard-active-key"].as<std::vector<std::string>>();
            for (const auto& wif : keys) {
                auto priv = graphene::utilities::wif_to_key(wif);
                FC_ASSERT(priv.valid(), "witness-guard-active-key: invalid WIF key");
                pimpl->_active_keys[priv->get_public_key()] = *priv;
            }
        }

        // Validare minimă
        if (!pimpl->_witnesses.empty() &&
            pimpl->_signing_keys.empty()) {
            wlog("witness_guard: witness-guard-account set but no "
                 "witness-guard-signing-key configured — plugin will not restore keys");
        }
        if (!pimpl->_witnesses.empty() &&
            pimpl->_active_keys.empty()) {
            wlog("witness_guard: witness-guard-account set but no "
                 "witness-guard-active-key configured — plugin will not restore keys");
        }

        ilog("witness_guard: plugin_initialize() end — "
             "monitoring ${n} witness(es), interval=${i} blocks",
             ("n", pimpl->_witnesses.size())("i", pimpl->_check_interval));

    } FC_LOG_AND_RETHROW()
}

void witness_guard_plugin::plugin_startup() {
    ilog("witness_guard: plugin_startup() begin");

    if (!pimpl->_enabled || pimpl->_witnesses.empty()) {
        ilog("witness_guard: nothing to monitor, plugin inactive");
        return;
    }

    // Hook pe fiecare bloc aplicat
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