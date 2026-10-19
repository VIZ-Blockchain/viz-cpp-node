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

BOOST_AUTO_TEST_CASE(config_missing_file_reports_absent) {
    bool exists = true;
    const std::vector<cfg_entry> e =
        parse_config_file("/tmp/node_admin_util_test.definitely-missing", &exists);
    BOOST_CHECK(!exists);
    BOOST_CHECK(e.empty());
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
