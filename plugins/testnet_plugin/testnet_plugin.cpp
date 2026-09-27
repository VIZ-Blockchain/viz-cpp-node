#include <graphene/plugins/testnet_plugin/testnet_plugin.hpp>

#include <graphene/chain/database.hpp>
#include <graphene/protocol/version.hpp>

#include <fc/exception/exception.hpp>
#include <fc/log/logger.hpp>

#include <appbase/application.hpp>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <cctype>
#include <string>
#include <vector>

namespace graphene {
namespace plugins {
namespace testnet_plugin {

namespace bpo = boost::program_options;

namespace {

/// "15" (a hardfork number) or "4.1.0"/"4.1" (a hardfork version) -> the number to apply.
uint32_t resolve_hardfork_number(graphene::chain::database &db, const std::string &requested) {
    bool numeric = !requested.empty();
    for (char c : requested) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            numeric = false;
            break;
        }
    }

    if (numeric) {
        const unsigned long n = std::stoul(requested);
        FC_ASSERT(n <= CHAIN_NUM_HARDFORKS,
                  "testnet-hardfork: hardfork number ${n} is beyond this binary (max ${max})",
                  ("n", requested)("max", CHAIN_NUM_HARDFORKS));
        return static_cast<uint32_t>(n);
    }

    std::vector<std::string> parts;
    boost::split(parts, requested, boost::is_any_of("."));
    FC_ASSERT(parts.size() == 2 || parts.size() == 3,
              "testnet-hardfork: '${v}' is neither a hardfork number nor a version "
              "(expected MAJOR.MINOR or MAJOR.MINOR.RELEASE)",
              ("v", requested));
    for (const std::string &part : parts) {
        FC_ASSERT(!part.empty(), "testnet-hardfork: cannot parse version '${v}'", ("v", requested));
        for (char c : part) {
            FC_ASSERT(std::isdigit(static_cast<unsigned char>(c)),
                      "testnet-hardfork: cannot parse version '${v}'", ("v", requested));
        }
    }

    const uint32_t major = std::stoul(parts[0]);
    const uint32_t minor = std::stoul(parts[1]);
    FC_ASSERT(major <= 255 && minor <= 255,
              "testnet-hardfork: version '${v}' is out of range", ("v", requested));

    const protocol::hardfork_version wanted(static_cast<uint8_t>(major), static_cast<uint8_t>(minor));
    const fc::optional<uint32_t> number = db.get_hardfork_number(wanted);
    FC_ASSERT(number.valid(),
              "testnet-hardfork: this binary knows no hardfork with version ${v}",
              ("v", std::string(wanted)));
    return *number;
}

} // namespace

struct testnet_plugin::impl {
    impl()
        : chain(appbase::app().get_plugin<graphene::plugins::chain::plugin>()) {}

    graphene::chain::database &db() { return chain.db(); }

    graphene::plugins::chain::plugin &chain;
    std::string requested; // raw --testnet-hardfork value; empty means "do nothing"
};

testnet_plugin::testnet_plugin() : pimpl(new impl()) {}

testnet_plugin::~testnet_plugin() {}

void testnet_plugin::set_program_options(
    bpo::options_description &command_line_options,
    bpo::options_description &config_file_options
) {
    config_file_options.add_options()
        ("testnet-hardfork",
         bpo::value<std::string>()->default_value(""),
         "TESTNET ONLY: apply all hardforks up to this one at startup, bypassing the validator "
         "vote tally (so it also works on a chain stuck in emergency consensus mode). Accepts a "
         "hardfork number (15) or a version (4.1.0). Empty (the default) does nothing. A forced "
         "hardfork cannot be rolled back — never use this on the production network.");
    command_line_options.add(config_file_options);
}

void testnet_plugin::plugin_initialize(const bpo::variables_map &options) {
    if (options.count("testnet-hardfork")) {
        pimpl->requested = options.at("testnet-hardfork").as<std::string>();
    }
}

void testnet_plugin::plugin_startup() {
    if (pimpl->requested.empty()) {
        ilog("testnet_plugin: loaded, no hardfork forced (set --testnet-hardfork to activate one)");
        return;
    }

    graphene::chain::database &db = pimpl->db();
    const uint32_t head = db.head_block_num();
    const uint32_t number = resolve_hardfork_number(db, pimpl->requested);
    const uint32_t applied = db.get_hardfork_property_object().last_hardfork;

    ilog("testnet_plugin: head=#${h}, last_hardfork=${a}, requested '${r}' -> hardfork ${n}",
         ("h", head)("a", applied)("r", pimpl->requested)("n", number));

    if (number <= applied) {
        ilog("testnet_plugin: hardfork ${n} is already applied, nothing to do", ("n", number));
        return;
    }

    elog("*** testnet_plugin: FORCING HARDFORK ${n} (requested '${r}') at head=#${h} ON A "
         "TESTNET-ONLY BASIS — this rewrites hardfork state without consensus and cannot be "
         "rolled back ***",
         ("n", number)("r", pimpl->requested)("h", head));

    db.set_hardfork(number, true);

    const auto &hfp = db.get_hardfork_property_object();
    elog("*** testnet_plugin: hardfork ${n} applied at head=#${h}: last_hardfork=${a}, "
         "current_hardfork_version=${v}, processed_hardforks=${c} ***",
         ("n", number)("h", db.head_block_num())("a", hfp.last_hardfork)
         ("v", std::string(hfp.current_hardfork_version))
         ("c", hfp.processed_hardforks.size()));

    ilog("testnet_plugin: hardfork ${n} is now live from the next block on; remove "
         "--testnet-hardfork once the chain carries the fork on its own",
         ("n", number));
}

void testnet_plugin::plugin_shutdown() {}

} // testnet_plugin
} // plugins
} // graphene
