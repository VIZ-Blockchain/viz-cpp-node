#include <appbase/application.hpp>
#include <graphene/protocol/types.hpp>
#include <fc/log/logger_config.hpp>
#include <graphene/utilities/file_appender_options.hpp>
#include <graphene/utilities/key_conversion.hpp>
#include <fc/git_revision.hpp>
#include <graphene/plugins/chain/plugin.hpp>
#include <graphene/plugins/p2p/p2p_plugin.hpp>
#include <graphene/plugins/webserver/webserver_plugin.hpp>
#include <graphene/plugins/network_broadcast_api/network_broadcast_api_plugin.hpp>
#include <graphene/plugins/validator/validator.hpp>
#include <graphene/plugins/database_api/plugin.hpp>
#include <graphene/plugins/account_history/plugin.hpp>
#include <graphene/plugins/account_by_key/account_by_key_plugin.hpp>
#include <graphene/plugins/raw_block/plugin.hpp>
#include <graphene/plugins/block_archive/plugin.hpp>
#include <graphene/plugins/block_info/plugin.hpp>
#include <graphene/plugins/validator_api/plugin.hpp>
#include <graphene/plugins/committee_api/committee_api.hpp>
#include <graphene/plugins/invite_api/invite_api.hpp>
#include <graphene/plugins/paid_subscription_api/paid_subscription_api.hpp>
#include <graphene/plugins/prediction_market_api/prediction_market_api.hpp>
#include <graphene/plugins/custom_protocol_api/custom_protocol_api.hpp>
#include <graphene/plugins/snapshot/plugin.hpp>

#include <graphene/plugins/validator_guard/validator_guard.hpp>
#include <graphene/plugins/testnet_plugin/testnet_plugin.hpp>
#include <graphene/plugins/node_admin/node_admin_plugin.hpp>

#include <fc/interprocess/signals.hpp>
#include <fc/log/console_appender.hpp>
#include <fc/log/json_console_appender.hpp>
#include <fc/log/file_appender.hpp>
#include <graphene/utilities/capped_file_appender.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string.hpp>

#include <graphene/utilities/git_revision.hpp>

#ifdef _WIN32
#include <windows.h>

/// Tray-launcher support: the launcher owns the console window lifecycle.
/// Surviving CTRL_CLOSE_EVENT lets the launcher hide the window when it is
/// closed instead of the process being terminated. All other events
/// (CTRL_C_EVENT in particular) fall through so the appbase SIGINT handler
/// performs a clean shutdown.
static BOOL WINAPI vizd_tray_console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_CLOSE_EVENT)
        return TRUE;
    return FALSE;
}
#endif

using graphene::protocol::version;


std::string& version_string() {
    static std::string v_str =
            "blockchain_version: " + std::string( CHAIN_VERSION ) + "\n" +
            "git_revision:       " + std::string( graphene::utilities::git_revision_sha )       + "\n" +
            "fc_git_revision:          " + std::string( fc::git_revision_sha )       + "\n";
    return v_str;
}

namespace graphene {

    namespace utilities {
        namespace bpo = boost::program_options;
        void set_logging_program_options(bpo::options_description &);
        fc::optional<fc::logging_config> load_logging_config(const boost::filesystem::path&);
        std::map<string, std::map<string, string>> load_config_sections(const boost::filesystem::path &path);
    }

    namespace plugins {
        void register_plugins(){
            ///plugins
            appbase::app().register_plugin<graphene::plugins::chain::plugin>();
            appbase::app().register_plugin<graphene::plugins::p2p::p2p_plugin>();
            appbase::app().register_plugin<graphene::plugins::webserver::webserver_plugin>();
            appbase::app().register_plugin<graphene::plugins::validator_plugin::validator_plugin>();
            appbase::app().register_plugin<graphene::plugins::validator_api::plugin>();
            appbase::app().register_plugin<graphene::plugins::network_broadcast_api::network_broadcast_api_plugin>();
            graphene::plugins::database_api::register_database_api();
            appbase::app().register_plugin<graphene::plugins::account_history::plugin>();
            appbase::app().register_plugin<graphene::plugins::account_by_key::account_by_key_plugin>();
            appbase::app().register_plugin<graphene::plugins::raw_block::plugin>();
            appbase::app().register_plugin<graphene::plugins::block_archive::plugin>();
            appbase::app().register_plugin<graphene::plugins::block_info::plugin>();
            appbase::app().register_plugin<graphene::plugins::committee_api::committee_api>();
            appbase::app().register_plugin<graphene::plugins::invite_api::invite_api>();
            appbase::app().register_plugin<graphene::plugins::paid_subscription_api::paid_subscription_api>();
            appbase::app().register_plugin<graphene::plugins::prediction_market_api::prediction_market_api>();
            appbase::app().register_plugin<graphene::plugins::custom_protocol_api::custom_protocol_api_plugin>();
            appbase::app().register_plugin<graphene::plugins::snapshot::snapshot_plugin>();
            appbase::app().register_plugin<graphene::plugins::validator_guard::validator_guard_plugin>();
            appbase::app().register_plugin<graphene::plugins::testnet_plugin::testnet_plugin>();
            appbase::app().register_plugin<graphene::plugins::node_admin::node_admin_plugin>();
            ///plugins
        };
    }

}

void logo(){
    std::cerr << "------------------------------------------------------\n\n";
    std::cerr << "            STARTING VIZ NETWORK\n\n";
    std::cerr << "------------------------------------------------------\n";
    std::cerr << "initiator public key: " << CHAIN_INITIATOR_PUBLIC_KEY_STR << "\n";
    std::cerr << "committee public key: " << CHAIN_COMMITTEE_PUBLIC_KEY_STR << "\n";
    std::cerr << "chain id: " << std::string( CHAIN_ID ) << "\n";
    std::cerr << "blockchain version: " << std::string( CHAIN_VERSION ) << "\n";
    std::cerr << "------------------------------------------------------\n";
}

int main( int argc, char** argv ) {
    try {

        graphene::plugins::register_plugins();
        appbase::app().set_version_string(version_string());

        // Optional bounded file appender ("log.capped_file_appender.*" in config.ini).
        // Registered before configure_logging; if no config section names it, nothing
        // changes for existing nodes.
        static bool reg_capped_file_appender =
                fc::appender::register_appender<graphene::utilities::capped_file_appender>("capped_file");
        (void)reg_capped_file_appender;

        // Setup logging config
        boost::program_options::options_description options;
        graphene::utilities::set_logging_program_options(options);
        appbase::app().add_program_options(boost::program_options::options_description(), options);

        bool initialized = appbase::app().initialize<
                graphene::plugins::chain::plugin,
                graphene::plugins::p2p::p2p_plugin,
                graphene::plugins::webserver::webserver_plugin
        >
                ( argc, argv );

        if( !initialized )
            return 0;

        logo();

        // auto& args = appbase::app().get_args();

        try {
            auto logging_config = graphene::utilities::load_logging_config(appbase::app().config_path());
            if (logging_config)
                fc::configure_logging(*logging_config);
        } catch (const fc::exception&) {
            wlog("Error parsing logging config");
        }

#ifdef _WIN32
        // Let the tray launcher manage the console window: closing it (X)
        // must hide vizd, not stop it. See vizd_tray_console_ctrl_handler.
        SetConsoleCtrlHandler(vizd_tray_console_ctrl_handler, TRUE);
#endif

        appbase::app().startup();
        appbase::app().exec();
        ilog("exited cleanly");
        return 0;
    }
    catch ( const boost::exception& e ) {
        std::cerr << boost::diagnostic_information(e) << "\n";
    }
    catch ( const fc::exception& e ) {
        std::cerr << e.to_detail_string() << "\n";
    }
    catch ( const std::exception& e ) {
        std::cerr << e.what() << "\n";
    }
    catch ( ... ) {
        std::cerr << "unknown exception\n";
    }

    return -1;
}



namespace graphene {
namespace utilities {
using std::string;
using std::vector;

void set_logging_program_options(bpo::options_description &options) {
    bpo::options_description log_opts("Logging options");
    log_opts.add_options()
        // pass section description with trailing dot (declare before section options)
        ("log.console_appender.stderr.", "declare an appender named \"stderr\" that writes messages to the console")
        ("log.console_appender.stderr.stream", bpo::value<string>()->default_value("std_error"), "console appender stream")
        ("log.file_appender.p2p.", "declare an appender named \"p2p\" that writes messages to p2p.log")
        ("log.file_appender.p2p.filename", bpo::value<string>()->default_value("logs/p2p/p2p.log"), "filename can be absolute or relative to this config file")
        // Besides filename, a file appender section accepts flush, rotate,
        // rotation_interval and rotation_limit — they are read by load_logging_config()
        // (graphene/utilities/file_appender_options.hpp). They are not declared here
        // because the config file is parsed with allow_unregistered: a declaration here
        // would only advertise command-line flags that do not reach the appender.
        // An unknown key in such a section is reported to stderr rather than ignored.

        // Optional bounded file appender: one file, always the last max_lines lines.
        // Nodes that do not configure this section behave exactly as before.
        ("log.capped_file_appender.node.", "declare an appender named \"node\" that keeps only the last max_lines lines")
        ("log.capped_file_appender.node.filename", bpo::value<string>()->default_value("logs/node/node.log"), "filename can be absolute or relative to this config file")
        ("log.capped_file_appender.node.max_lines", bpo::value<uint32_t>()->default_value(10000), "keep at most this many lines in the file (0 = unlimited)")

        ("logger.default.", "route any messages logged to the default logger to the \"stderr\" logger we declared above, if they are info level are higher")
        ("logger.default.level", bpo::value<string>()->default_value("warn"), "log level of \"default\" logger (all|debug|info|warn|error|off)")
        ("logger.default.appenders", bpo::value<string>()->default_value("stderr"), "appender(s) of \"default\" logger")

        ("logger.p2p.", "route messages sent to the \"p2p\" logger to the p2p appender declared above")
        ("logger.p2p.level", bpo::value<string>()->default_value("warn"), "log level of \"p2p\" logger (all|debug|info|warn|error|off)")
        ("logger.p2p.appenders", bpo::value<string>()->default_value("p2p"), "appender(s) of \"p2p\" logger")
        ;

    options.add(log_opts);
}


std::map<string, std::map<string, string>> load_config_sections(const boost::filesystem::path &path) {
    bpo::options_description options;
    auto parsed_options = bpo::parse_config_file<char>(path.string().c_str(), options, true);
    std::map<string, std::map<string, string>> sections;
    for (const auto& o : parsed_options.options) {
        auto key = o.string_key;
        auto pos = key.rfind('.');
        if (pos != std::string::npos) {
            auto s = key.substr(0, pos);
            auto k = key.substr(pos+1);
            auto v = o.value;
            sections[s][k] = v.empty() ? "" : v[0];
        }
    }
    return sections;
}

fc::optional<fc::logging_config> load_logging_config(const boost::filesystem::path &pwd) {
    try {
        fc::logging_config logging_config;
        bool found_logging_config = false;

        const std::string console_appender_section_prefix = "log.console_appender.";
        const std::string json_console_appender_section_prefix = "log.json_console_appender.";
        const std::string file_appender_section_prefix = "log.file_appender.";
        const std::string capped_file_appender_section_prefix = "log.capped_file_appender.";
        const std::string logger_section_prefix = "logger.";

        auto sections = load_config_sections(pwd);
        for (const auto& sect : sections) {
            const std::string &section_name = sect.first;
            auto &options = sect.second;

            if (boost::starts_with(section_name, json_console_appender_section_prefix)) {
                std::string console_appender_name = section_name.substr(json_console_appender_section_prefix.length());
                std::string stream_name = options.at("stream");

                // construct a default json console appender config here
                // stdout/stderr will be taken from ini file, everything else hard-coded here
                fc::json_console_appender::j_config console_appender_config;
                console_appender_config.level_colors.emplace_back(fc::json_console_appender::j_level_color(fc::log_level::debug, fc::json_console_appender::j_color::green));
                console_appender_config.level_colors.emplace_back(fc::json_console_appender::j_level_color(fc::log_level::warn, fc::json_console_appender::j_color::brown));
                console_appender_config.level_colors.emplace_back(fc::json_console_appender::j_level_color(fc::log_level::error, fc::json_console_appender::j_color::cyan));
                console_appender_config.stream = fc::variant(stream_name).as<fc::json_console_appender::j_stream::type>();
                logging_config.appenders.emplace_back(console_appender_name, "json_console", fc::variant(console_appender_config));
                found_logging_config = true;
            } else if (boost::starts_with(section_name, console_appender_section_prefix)) {
                std::string console_appender_name = section_name.substr(console_appender_section_prefix.length());
                std::string stream_name = options.at("stream");

                // construct a default console appender config here
                // stdout/stderr will be taken from ini file, everything else hard-coded here
                fc::console_appender::config console_appender_config;
                console_appender_config.level_colors.emplace_back(fc::console_appender::level_color(fc::log_level::debug, fc::console_appender::color::green));
                console_appender_config.level_colors.emplace_back(fc::console_appender::level_color(fc::log_level::warn, fc::console_appender::color::brown));
                console_appender_config.level_colors.emplace_back(fc::console_appender::level_color(fc::log_level::error, fc::console_appender::color::cyan));
                console_appender_config.stream = fc::variant(stream_name).as<fc::console_appender::stream::type>();
                logging_config.appenders.emplace_back(console_appender_name, "console", fc::variant(console_appender_config));
                found_logging_config = true;
            } else if (boost::starts_with(section_name, file_appender_section_prefix)) {
                std::string file_appender_name = section_name.substr(file_appender_section_prefix.length());
                fc::path file_name = options.at("filename");
                if (file_name.is_relative()) {
                    file_name = fc::absolute(pwd).parent_path() / file_name;
                }

                // filename and the four rotation options come from the ini; keys the
                // section does not set keep the historical defaults. Invalid values and
                // unknown keys are reported to stderr instead of being silently ignored.
                std::vector<std::string> appender_warnings;
                fc::file_appender::config file_appender_config =
                   graphene::utilities::make_file_appender_config(section_name, file_name, options,
                                                                  &appender_warnings);
                for (const std::string& warning : appender_warnings)
                   std::cerr << warning << "\n";
                logging_config.appenders.emplace_back(file_appender_name, "file", fc::variant(file_appender_config));
                found_logging_config = true;
            } else if (boost::starts_with(section_name, capped_file_appender_section_prefix)) {
                std::string capped_appender_name = section_name.substr(capped_file_appender_section_prefix.length());
                auto filename_opt = options.find("filename");
                FC_ASSERT(filename_opt != options.end(),
                          "log.capped_file_appender.${a}.filename is required", ("a", capped_appender_name));

                fc::path file_name = filename_opt->second;
                if (file_name.is_relative()) {
                    file_name = fc::absolute(pwd).parent_path() / file_name;
                }

                graphene::utilities::capped_file_appender::config capped_config;
                capped_config.filename = file_name;
                capped_config.max_lines = 10000;

                auto max_lines_opt = options.find("max_lines");
                if (max_lines_opt != options.end()) {
                    char *end = nullptr;
                    const unsigned long parsed = std::strtoul(max_lines_opt->second.c_str(), &end, 10);
                    if (end == max_lines_opt->second.c_str() || (end && *end != '\0') || parsed > 10000000UL) {
                        // A typo must not cost the operator the whole logging config: keep the
                        // default and say so, instead of clearing the logger map in configure_logging.
                        std::cerr << "log.capped_file_appender." << capped_appender_name
                                  << ".max_lines: invalid value '" << max_lines_opt->second
                                  << "', using 10000\n";
                    } else {
                        capped_config.max_lines = static_cast<uint32_t>(parsed);
                    }
                }

                auto flush_opt = options.find("flush");
                if (flush_opt != options.end()) {
                    capped_config.flush = !(flush_opt->second == "false" || flush_opt->second == "0" ||
                                            flush_opt->second == "no");
                }

                logging_config.appenders.emplace_back(capped_appender_name, "capped_file", fc::variant(capped_config));
                found_logging_config = true;
            } else if (boost::starts_with(section_name, logger_section_prefix)) {
                std::string logger_name = section_name.substr(logger_section_prefix.length());
                std::string level_string = options.at("level");
                std::string appenders_string = options.at("appenders");
                fc::logger_config logger_config(logger_name);
                logger_config.level = fc::variant(level_string).as<fc::log_level>();
                boost::split(logger_config.appenders, appenders_string, boost::is_any_of(" ,"), boost::token_compress_on);
                logging_config.loggers.push_back(logger_config);
                found_logging_config = true;
            }
        }
        if (found_logging_config) {
            return logging_config;
        } else {
            return fc::optional<fc::logging_config>();
        }
    }
    FC_RETHROW_EXCEPTIONS(warn, "")
}
}
}
