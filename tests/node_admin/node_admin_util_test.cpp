// Pure helpers of the node_admin plugin: secret masking, admin-token parsing and config.ini
// parsing. The live acceptance suite drives a real listener; these checks pin the contracts
// that the listener relies on, including the shapes that must be REJECTED.

#define BOOST_TEST_MODULE node_admin_util
#include <boost/test/unit_test.hpp>

#include <graphene/plugins/node_admin/node_admin_util.hpp>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

using namespace graphene::plugins::node_admin::util;

namespace {

const std::string kHash64 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

bool parse_throws(const std::string &spec) {
    try {
        parse_token_spec(spec);
    } catch (const fc::exception &) {
        return true;
    }
    return false;
}

std::string write_temp_config(const std::string &body) {
    const char *dir = std::getenv("TMPDIR");
    const std::string path = std::string(dir && *dir ? dir : "/tmp") + "/node_admin_util_test.ini";
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
    out.close();
    return path;
}

} // namespace

BOOST_AUTO_TEST_SUITE(node_admin_util)

// ─── mask_text ─────────────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(mask_wif_anywhere_in_text) {
    const std::string wif = "5Hw9YPABaFxa2LooiANLrhUK5TPryy8f7v9Y1rk923PuYqbYdfC";
    const std::string in = "boot ok, key " + wif + " loaded";
    const std::string out = mask_text(in);
    BOOST_CHECK(out.find(wif) == std::string::npos);
    BOOST_CHECK(out.find("***") != std::string::npos);
    BOOST_CHECK(out.find("boot ok, key ") == 0);
}

BOOST_AUTO_TEST_CASE(mask_wif_at_start_and_end) {
    const std::string wif = "5Hw9YPABaFxa2LooiANLrhUK5TPryy8f7v9Y1rk923PuYqbYdfC";
    BOOST_CHECK(mask_text(wif).find(wif) == std::string::npos);
    BOOST_CHECK(mask_text("x " + wif).find(wif) == std::string::npos);
    BOOST_CHECK(mask_text(wif + " x").find(wif) == std::string::npos);
}

BOOST_AUTO_TEST_CASE(mask_kl_wif_form) {
    // Second accepted form: K/L + 51 base58 chars.
    const std::string wif = "L" + std::string(51, '1');
    BOOST_CHECK(mask_text("k=" + wif).find(wif) == std::string::npos);
}

BOOST_AUTO_TEST_CASE(mask_inline_config_secrets) {
    BOOST_CHECK(mask_text("admin-token = deadbeef") == "admin-token = ***");
    BOOST_CHECK(mask_text("private-key=abc123") == "private-key=***");
    BOOST_CHECK(mask_text("emergency-private-key:zzz") == "emergency-private-key:***");
    BOOST_CHECK(mask_text("PASSWORD: hunter2") == "PASSWORD: ***");
    BOOST_CHECK(mask_text("secret = q") == "secret = ***");
}

BOOST_AUTO_TEST_CASE(mask_leaves_innocent_text_alone) {
    BOOST_CHECK_EQUAL(mask_text("monkey=banana"), "monkey=banana");
    BOOST_CHECK_EQUAL(mask_text("plugin = chain"), "plugin = chain");
    BOOST_CHECK_EQUAL(mask_text(""), "");
}

BOOST_AUTO_TEST_CASE(sensitive_key_names) {
    BOOST_CHECK(sensitive_key("private-key"));
    BOOST_CHECK(sensitive_key("PRIVATE_KEY"));
    BOOST_CHECK(sensitive_key("admin-token"));
    BOOST_CHECK(sensitive_key("p2p-private-key"));
    BOOST_CHECK(sensitive_key("password"));
    BOOST_CHECK(!sensitive_key("plugin"));
    BOOST_CHECK(!sensitive_key("shared-file-size"));
}

// ─── parse_token_spec ──────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(token_valid_with_scope_list_and_expiry) {
    const token_info t = parse_token_spec("agent:read,webserver:" + kHash64 + ":1760000000");
    BOOST_CHECK_EQUAL(t.label, "agent");
    BOOST_CHECK_EQUAL(t.scopes.size(), std::size_t(2));
    BOOST_CHECK(t.scopes.count("read") == 1);
    BOOST_CHECK(t.scopes.count("webserver") == 1);
    BOOST_CHECK_EQUAL(t.hash_hex, kHash64);
    BOOST_CHECK_EQUAL(t.expires, uint64_t(1760000000));
}

BOOST_AUTO_TEST_CASE(token_without_expiry_never_expires) {
    const token_info t = parse_token_spec(" snap : read : " + kHash64 + " ");
    BOOST_CHECK_EQUAL(t.label, "snap");
    BOOST_CHECK_EQUAL(t.expires, uint64_t(0));
}

BOOST_AUTO_TEST_CASE(token_hash_is_lowercased) {
    std::string upper = kHash64;
    for (char &c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    BOOST_CHECK_EQUAL(parse_token_spec("a:read:" + upper).hash_hex, kHash64);
}

BOOST_AUTO_TEST_CASE(token_rejects_malformed_specs) {
    BOOST_CHECK(parse_throws("agent:read"));                       // no hash
    BOOST_CHECK(parse_throws(":read:" + kHash64));                 // empty label
    BOOST_CHECK(parse_throws("agent:bogus:" + kHash64));           // unknown scope
    BOOST_CHECK(parse_throws("agent:read:" + kHash64.substr(1)));  // 63 hex chars
    BOOST_CHECK(parse_throws("agent:read:" + kHash64.substr(0, 63) + "z")); // non-hex
    BOOST_CHECK(parse_throws("agent:read:" + kHash64 + ":soon"));  // non-numeric expiry
    BOOST_CHECK(parse_throws("a:b:c:d:e"));                        // too many fields
}

// ─── parse_config_file ────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(config_parses_entries_sections_and_comments) {
    const std::string path = write_temp_config(
        "# comment\r\n"
        "; another\r\n"
        "plugin = chain\r\n"
        "plugin=webserver\r\n"
        "\r\n"
        "[section]\r\n"
        "key = value with spaces\r\n"
        "bare\r\n");
    bool exists = false;
    const std::vector<cfg_entry> e = parse_config_file(path, &exists);
    std::remove(path.c_str());

    BOOST_CHECK(exists);
    BOOST_CHECK_EQUAL(e.size(), std::size_t(4));
    BOOST_CHECK_EQUAL(e[0].key, "plugin");
    BOOST_CHECK_EQUAL(e[0].value, "chain");
    BOOST_CHECK_EQUAL(e[0].section, "");
    BOOST_CHECK_EQUAL(e[1].value, "webserver");
    BOOST_CHECK_EQUAL(e[2].section, "section");
    BOOST_CHECK_EQUAL(e[2].key, "key");
    BOOST_CHECK_EQUAL(e[2].value, "value with spaces");
    BOOST_CHECK_EQUAL(e[3].key, "bare");
    BOOST_CHECK_EQUAL(e[3].value, "");
}

BOOST_AUTO_TEST_CASE(config_values_end_where_the_loader_ends_them) {
    // appbase loads config.ini through boost's config parser, which treats '#' as a comment to
    // the end of the line. A value that keeps the comment splits into names the loader never
    // sees (`plugin = chain # why` -> "#", "why").
    const std::string path = write_temp_config(
        "plugin = chain p2p # two plugins\r\n"
        "p2p-max-connections = 50   # tuned\r\n");
    bool exists = false;
    const std::vector<cfg_entry> e = parse_config_file(path, &exists);
    std::remove(path.c_str());

    BOOST_CHECK(exists);
    BOOST_REQUIRE_EQUAL(e.size(), std::size_t(2));
    BOOST_CHECK_EQUAL(e[0].value, "chain p2p");
    BOOST_CHECK_EQUAL(e[1].value, "50");
}

BOOST_AUTO_TEST_CASE(config_missing_file_reports_absent) {
    bool exists = true;
    const std::vector<cfg_entry> e =
        parse_config_file("/tmp/node_admin_util_test.definitely-missing", &exists);
    BOOST_CHECK(!exists);
    BOOST_CHECK(e.empty());
}

// ─── config allow-list ────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(config_allowlist_excludes_identity_and_endpoints) {
    BOOST_CHECK(config_spec_for("p2p-max-connections") != nullptr);
    BOOST_CHECK(config_spec_for("webserver-limit-m") != nullptr);
    BOOST_CHECK(!config_spec_for("private-key"));
    BOOST_CHECK(!config_spec_for("emergency-private-key"));
    BOOST_CHECK(!config_spec_for("admin-token"));
    BOOST_CHECK(!config_spec_for("data-dir"));
    BOOST_CHECK(!config_spec_for("webserver-http-endpoint"));
    BOOST_CHECK(!config_spec_for("plugin"));
    BOOST_CHECK(!config_spec_for("shared-file-size"));
}

BOOST_AUTO_TEST_CASE(config_values_are_normalized_and_bounded) {
    std::string v;
    std::string why;
    BOOST_CHECK(normalize_config_value("webserver-stats-enabled", " YES ", v, why));
    BOOST_CHECK_EQUAL(v, "true");
    BOOST_CHECK(normalize_config_value("p2p-isolated-peers", "0", v, why));
    BOOST_CHECK_EQUAL(v, "false");
    BOOST_CHECK(normalize_config_value("p2p-max-connections", "007", v, why));
    BOOST_CHECK_EQUAL(v, "7");

    BOOST_CHECK(!normalize_config_value("webserver-stats-enabled", "maybe", v, why));
    BOOST_CHECK(why.find("boolean") != std::string::npos);
    BOOST_CHECK(!normalize_config_value("p2p-max-connections", "-1", v, why));
    BOOST_CHECK(!normalize_config_value("p2p-max-connections", "10k", v, why));
    BOOST_CHECK(!normalize_config_value("p2p-max-connections", "0", v, why));
    BOOST_CHECK(why.find("between") != std::string::npos);
    BOOST_CHECK(!normalize_config_value("private-key", "5Hx", v, why));
    BOOST_CHECK(why.find("allow-list") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(plugin_names_and_protection) {
    BOOST_CHECK(known_plugins().count("node_admin"));
    BOOST_CHECK(known_plugins().count("json_rpc"));
    BOOST_CHECK(known_plugins().count("snapshot"));
    BOOST_CHECK(!known_plugins().count("not_a_plugin"));
    BOOST_CHECK(plugin_removal_forbidden("node_admin"));
    BOOST_CHECK(plugin_removal_forbidden("chain"));
    BOOST_CHECK(!plugin_removal_forbidden("snapshot"));
}

// ─── raw config line editing ──────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(set_value_keeps_spacing_and_comments) {
    const std::vector<std::string> lines = {
        "# node config",
        "p2p-max-connections = 10",
        "webserver-cache-enabled=true",
        "[logger]",
        "level = debug"
    };
    const line_edit e = set_config_value(lines, "p2p-max-connections", "20");
    BOOST_CHECK(e.error.empty());
    BOOST_CHECK_EQUAL(e.changed, std::size_t(1));
    BOOST_CHECK_EQUAL(e.lines[0], "# node config");
    BOOST_CHECK_EQUAL(e.lines[1], "p2p-max-connections = 20");
    BOOST_CHECK_EQUAL(e.lines[2], "webserver-cache-enabled=true");

    const line_edit f = set_config_value(lines, "webserver-cache-enabled", "false");
    BOOST_CHECK_EQUAL(f.lines[2], "webserver-cache-enabled=false");
}

BOOST_AUTO_TEST_CASE(set_value_inserts_before_first_section) {
    const std::vector<std::string> lines = {"plugin = chain", "", "[logger]", "level = info"};
    const line_edit e = set_config_value(lines, "webserver-limit-m", "42");
    BOOST_CHECK_EQUAL(e.changed, std::size_t(1));
    BOOST_CHECK_EQUAL(e.lines[0], "plugin = chain");
    BOOST_CHECK_EQUAL(e.lines[1], "");
    BOOST_CHECK_EQUAL(e.lines[2], "webserver-limit-m = 42");
    BOOST_CHECK_EQUAL(e.lines[3], "[logger]");
    BOOST_CHECK_EQUAL(e.lines[4], "level = info");
}

BOOST_AUTO_TEST_CASE(set_value_appends_without_sections) {
    const line_edit e = set_config_value({"plugin = chain"}, "dlt-stats-interval-sec", "60");
    BOOST_CHECK_EQUAL(e.lines.size(), std::size_t(2));
    BOOST_CHECK_EQUAL(e.lines[1], "dlt-stats-interval-sec = 60");
}

BOOST_AUTO_TEST_CASE(set_value_ignores_same_key_inside_a_section) {
    const std::vector<std::string> lines = {"[logger]", "webserver-limit-m = 1"};
    const line_edit e = set_config_value(lines, "webserver-limit-m", "5");
    // The section entry is not ours; the top-level key is inserted before the section.
    BOOST_CHECK_EQUAL(e.lines[0], "webserver-limit-m = 5");
    BOOST_CHECK_EQUAL(e.lines[1], "[logger]");
    BOOST_CHECK_EQUAL(e.lines[2], "webserver-limit-m = 1");
}

BOOST_AUTO_TEST_CASE(set_value_preserves_crlf) {
    const std::vector<std::string> lines = {"p2p-max-connections = 1\r", "[logger]\r"};
    const line_edit e = set_config_value(lines, "p2p-max-connections", "2");
    BOOST_CHECK_EQUAL(e.lines[0], "p2p-max-connections = 2\r");
    const line_edit f = set_config_value(lines, "dlt-stats-interval-sec", "60");
    BOOST_CHECK_EQUAL(f.lines[0], "p2p-max-connections = 1\r");
    BOOST_CHECK_EQUAL(f.lines[1], "dlt-stats-interval-sec = 60\r");
    BOOST_CHECK_EQUAL(f.lines[2], "[logger]\r");
}

BOOST_AUTO_TEST_CASE(split_and_join_roundtrip) {
    const std::string text = "a = 1\nb = 2\n";
    BOOST_CHECK_EQUAL(join_lines(split_lines(text)), text);
    BOOST_CHECK_EQUAL(join_lines(split_lines("a = 1\nb = 2")), "a = 1\nb = 2\n");
}

// ─── plugin line editing ──────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(plugin_lines_add_and_remove) {
    const std::vector<std::string> lines = {
        "plugin = chain",
        "plugin = p2p",
        "plugin = node_admin",
        "[logger]",
        "level = info"
    };
    const line_edit rm = edit_plugin_lines(lines, {}, {"p2p"});
    BOOST_CHECK(rm.error.empty());
    BOOST_CHECK_EQUAL(rm.changed, std::size_t(1));
    BOOST_CHECK_EQUAL(rm.lines[0], "plugin = chain");
    BOOST_CHECK_EQUAL(rm.lines[1], "plugin = node_admin");

    const line_edit add = edit_plugin_lines(lines, {"snapshot"}, {});
    BOOST_CHECK(add.error.empty());
    BOOST_CHECK_EQUAL(add.changed, std::size_t(1));
    // After the last plugin line, before the [section].
    BOOST_CHECK_EQUAL(add.lines[3], "plugin = snapshot");
    BOOST_CHECK_EQUAL(add.lines[4], "[logger]");
}

BOOST_AUTO_TEST_CASE(plugin_lines_insert_before_section_when_none) {
    const line_edit e = edit_plugin_lines({"p2p-max-connections = 10", "[logger]", "level = info"},
                                          {"snapshot"}, {});
    BOOST_CHECK(e.error.empty());
    BOOST_CHECK_EQUAL(e.lines[0], "p2p-max-connections = 10");
    BOOST_CHECK_EQUAL(e.lines[1], "plugin = snapshot");
    BOOST_CHECK_EQUAL(e.lines[2], "[logger]");
}

BOOST_AUTO_TEST_CASE(plugin_names_split_like_the_loader) {
    BOOST_CHECK_EQUAL(join_lines(plugin_names_in_value("chain p2p")), "chain\np2p\n");
    const std::vector<std::string> padded = plugin_names_in_value("  chain\t p2p  ");
    BOOST_REQUIRE_EQUAL(padded.size(), std::size_t(2));
    BOOST_CHECK_EQUAL(padded[0], "chain");
    BOOST_CHECK_EQUAL(padded[1], "p2p");
    BOOST_CHECK(plugin_names_in_value("   ").empty());
}

BOOST_AUTO_TEST_CASE(plugin_lines_see_names_inside_a_multi_name_line) {
    const std::vector<std::string> lines = {
        "plugin = chain p2p",
        "plugin = json_rpc webserver",
        "[logger]"
    };
    // Configured means "appbase would load it", not "the line equals this name".
    BOOST_CHECK(!edit_plugin_lines(lines, {"p2p"}, {}).error.empty());   // already configured

    // Removing one name rewrites the line; the neighbour on the same line stays.
    const line_edit rm = edit_plugin_lines(lines, {}, {"p2p"});
    BOOST_CHECK(rm.error.empty());
    BOOST_CHECK_EQUAL(rm.changed, std::size_t(1));
    BOOST_CHECK_EQUAL(rm.lines[0], "plugin = chain");
    BOOST_CHECK_EQUAL(rm.lines[1], "plugin = json_rpc webserver");

    // Removing the only name drops the line; removing from a two-name line keeps both others.
    const line_edit none = edit_plugin_lines(lines, {}, {"json_rpc", "webserver"});
    BOOST_CHECK(none.error.empty());
    BOOST_CHECK_EQUAL(none.changed, std::size_t(1));
    BOOST_REQUIRE_EQUAL(none.lines.size(), std::size_t(2));
    BOOST_CHECK_EQUAL(none.lines[0], "plugin = chain p2p");
    BOOST_CHECK_EQUAL(none.lines[1], "[logger]");

    // CRLF survives a rewrite. A line as split_lines() produces it carries '\n' nowhere and
    // keeps only the '\r' (join_lines() adds the newline back), so that is what the edit sees.
    const line_edit crlf = edit_plugin_lines({"plugin = chain p2p\r", "[logger]"}, {}, {"p2p"});
    BOOST_CHECK(crlf.error.empty());
    BOOST_CHECK_EQUAL(crlf.lines[0], "plugin = chain\r");
    BOOST_CHECK_EQUAL(join_lines(crlf.lines, false), "plugin = chain\r\n[logger]");
}

BOOST_AUTO_TEST_CASE(inline_comments_do_not_become_plugin_names) {
    // The loader cuts a value at '#'; a comment word must never count as a configured plugin,
    // and a rewrite must not swallow the operator's note.
    const std::vector<std::string> lines = {"plugin = chain p2p # two plugins", "[logger]"};
    BOOST_CHECK(!edit_plugin_lines(lines, {}, {"two"}).error.empty());  // comment is not a name
    const line_edit rm = edit_plugin_lines(lines, {}, {"p2p"});
    BOOST_CHECK(rm.error.empty());
    BOOST_CHECK_EQUAL(rm.changed, std::size_t(1));
    BOOST_CHECK_EQUAL(rm.lines[0], "plugin = chain # two plugins");
}

BOOST_AUTO_TEST_CASE(config_set_keeps_an_inline_comment) {
    // Same line contract as above: split_lines() keeps the '\r', join_lines() restores the '\n'.
    const line_edit e =
        set_config_value({"p2p-max-connections = 50   # tuned\r"}, "p2p-max-connections", "60");
    BOOST_CHECK(e.error.empty());
    BOOST_CHECK_EQUAL(e.lines[0], "p2p-max-connections = 60 # tuned\r");

    const line_edit s = set_config_value_in_section({"[logger.p2p]", "level = info # quiet"},
                                                    "logger.p2p", "level", "debug");
    BOOST_CHECK(s.error.empty());
    BOOST_CHECK_EQUAL(s.lines[1], "level = debug # quiet");
}

BOOST_AUTO_TEST_CASE(plugin_lines_refuse_bad_edits) {
    const std::vector<std::string> lines = {"plugin = chain", "plugin = node_admin"};
    BOOST_CHECK(!edit_plugin_lines(lines, {"bogus"}, {}).error.empty());
    BOOST_CHECK(!edit_plugin_lines(lines, {"chain"}, {}).error.empty());      // already configured
    BOOST_CHECK(!edit_plugin_lines(lines, {}, {"chain"}).error.empty());     // protected
    BOOST_CHECK(!edit_plugin_lines(lines, {}, {"node_admin"}).error.empty()); // protected
    BOOST_CHECK(!edit_plugin_lines(lines, {}, {"p2p"}).error.empty());       // not configured
    BOOST_CHECK(!edit_plugin_lines(lines, {}, {}).error.empty());            // nothing to do
    BOOST_CHECK(!edit_plugin_lines(lines, {"snapshot"}, {"snapshot"}).error.empty());
}

// ─── sectioned config editor (logging settings) ───────────────────────────────────────────

BOOST_AUTO_TEST_CASE(log_level_accepts_only_known_levels) {
    std::string norm;
    std::string why;
    BOOST_CHECK(normalize_config_value("logger.default.level", " INFO ", norm, why));
    BOOST_CHECK_EQUAL(norm, "info");
    BOOST_CHECK(normalize_config_value("logger.p2p.level", "off", norm, why));
    BOOST_CHECK_EQUAL(norm, "off");
    BOOST_CHECK(!normalize_config_value("logger.default.level", "verbose", norm, why));
    BOOST_CHECK(why.find("all, debug, info, warn, error, off") != std::string::npos);
    // A level is not a number: the unsigned-int branch must not swallow it.
    BOOST_CHECK(!normalize_config_value("logger.default.level", "3", norm, why));
}

BOOST_AUTO_TEST_CASE(allowlist_names_split_into_section_and_key) {
    std::string section;
    std::string key;
    const config_spec *top = config_spec_for("p2p-max-connections");
    BOOST_REQUIRE(top);
    config_name_parts(*top, section, key);
    BOOST_CHECK_EQUAL(section, "");
    BOOST_CHECK_EQUAL(key, "p2p-max-connections");

    const config_spec *nested = config_spec_for("logger.default.level");
    BOOST_REQUIRE(nested);
    config_name_parts(*nested, section, key);
    BOOST_CHECK_EQUAL(section, "logger.default");
    BOOST_CHECK_EQUAL(key, "level");
}

BOOST_AUTO_TEST_CASE(section_editor_replaces_value_in_place) {
    const std::vector<std::string> lines = {
        "plugin = chain", "[logger.default]", "level=warn", "appenders=stderr",
        "[logger.p2p]", "level = error"};
    const line_edit e = set_config_value_in_section(lines, "logger.p2p", "level", "info");
    BOOST_CHECK(e.error.empty());
    BOOST_CHECK_EQUAL(e.changed, std::size_t(1));
    BOOST_CHECK_EQUAL(e.lines[5], "level = info");
    // The other logger's level is a different section and must not move.
    BOOST_CHECK_EQUAL(e.lines[2], "level=warn");
}

BOOST_AUTO_TEST_CASE(section_editor_inserts_under_the_header) {
    const std::vector<std::string> lines = {"[logger.default]", "level = warn", "[logger.p2p]"};
    const line_edit e = set_config_value_in_section(lines, "log.capped_file_appender.node",
                                                   "max_lines", "5000");
    // Missing section is refused, not invented: the node never declares it.
    BOOST_CHECK(!e.error.empty());
    BOOST_CHECK(e.lines == lines);

    const line_edit add = set_config_value_in_section(lines, "logger.p2p", "level", "info");
    BOOST_CHECK(add.error.empty());
    BOOST_CHECK_EQUAL(add.lines.size(), std::size_t(4));
    BOOST_CHECK_EQUAL(add.lines[3], "level = info");
}

BOOST_AUTO_TEST_CASE(section_editor_keeps_crlf) {
    const std::vector<std::string> lines = {"[logger.default]\r", "level = warn\r"};
    const line_edit e = set_config_value_in_section(lines, "logger.default", "level", "debug");
    BOOST_CHECK(e.error.empty());
    BOOST_CHECK_EQUAL(e.lines[1], "level = debug\r");
}

BOOST_AUTO_TEST_CASE(section_value_reader_scopes_by_section) {
    const std::vector<std::string> lines = {
        "level = top-level", "[logger.default]", "level = warn", "[logger.p2p]", "level = error"};
    std::string v;
    BOOST_CHECK(config_value_in_section(lines, "logger.p2p", "level", v));
    BOOST_CHECK_EQUAL(v, "error");
    BOOST_CHECK(config_value_in_section(lines, "logger.default", "level", v));
    BOOST_CHECK_EQUAL(v, "warn");
    // A key that only exists at top level is not visible inside a section.
    BOOST_CHECK(!config_value_in_section(lines, "logger.p2p", "appenders", v));
}

BOOST_AUTO_TEST_CASE(config_groups_map_to_plugins) {
    BOOST_CHECK_EQUAL(config_key_group("p2p-max-connections"), "p2p");
    BOOST_CHECK_EQUAL(config_key_group("dlt-stats-interval-sec"), "p2p");
    BOOST_CHECK_EQUAL(config_key_group("auto-resync-on-wedge"), "p2p");
    BOOST_CHECK_EQUAL(config_key_group("webserver-cache-size"), "webserver");
    BOOST_CHECK_EQUAL(config_key_group("validator-guard-interval"), "validator_guard");
    BOOST_CHECK_EQUAL(config_key_group("logger.default.level"), "logging");
    BOOST_CHECK_EQUAL(config_key_group("log.capped_file_appender.node.max_lines"), "logging");
    BOOST_CHECK_EQUAL(config_key_group("something-else"), "other");
}

// ─── snapshot names ───────────────────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(snapshot_names_accept_plain_files_only) {
    BOOST_CHECK(is_plain_snapshot_name("snapshot-block-100.vizjson"));
    BOOST_CHECK(is_plain_snapshot_name("SNAP.JSON"));
    BOOST_CHECK(!is_plain_snapshot_name(""));
    BOOST_CHECK(!is_plain_snapshot_name("."));
    BOOST_CHECK(!is_plain_snapshot_name(".."));
    BOOST_CHECK(!is_plain_snapshot_name("../x.vizjson"));
    BOOST_CHECK(!is_plain_snapshot_name("a/b.vizjson"));
    BOOST_CHECK(!is_plain_snapshot_name("a\\b.vizjson"));
    BOOST_CHECK(!is_plain_snapshot_name("x.txt"));
    BOOST_CHECK(!is_plain_snapshot_name(".json"));
}

BOOST_AUTO_TEST_SUITE_END()
