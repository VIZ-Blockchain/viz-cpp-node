#include <graphene/plugins/node_admin/node_admin_plugin.hpp>

#include <graphene/plugins/p2p/p2p_plugin.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/protocol/version.hpp>

#include <appbase/application.hpp>

#include <fc/crypto/sha256.hpp>
#include <fc/io/json.hpp>
#include <fc/log/logger.hpp>
#include <fc/time.hpp>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#include <websocketpp/config/asio.hpp>
#include <websocketpp/server.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

namespace graphene {
namespace plugins {
namespace node_admin {

namespace bpo = boost::program_options;
namespace bfs = boost::filesystem;
namespace asio = boost::asio;
using boost::asio::ip::tcp;
using websocketpp::connection_hdl;
// CHAIN_VERSION expands to an unqualified `version(4, 2, 0)`; same import as vizd's main.cpp.
using graphene::protocol::version;

namespace {

// appbase exposes only a typed find_plugin<T>(); the name-based lookup is protected and the
// plugin registry has no public enumeration. So the config list is the visible population, and
// the state below is genuinely observed only for the plugins whose type this TU links.
const appbase::abstract_plugin *observe_plugin(const std::string &name) {
    if (name == "chain") return appbase::app().find_plugin<graphene::plugins::chain::plugin>();
    if (name == "p2p") return appbase::app().find_plugin<graphene::plugins::p2p::p2p_plugin>();
    if (name == node_admin_plugin::name()) return appbase::app().find_plugin<node_admin_plugin>();
    return nullptr;
}

// ── limits ──────────────────────────────────────────────────────────────────────────────────
const std::size_t kMaxBodyBytes = 64 * 1024;
const std::size_t kMaxLogBytes = 64 * 1024;
const std::size_t kMaxLines = 2000;
const std::size_t kAuditRotateBytes = 16 * 1024 * 1024;
const uint32_t kRatePerSecond = 20;

// ── small string helpers ────────────────────────────────────────────────────────────────────
std::string trim(const std::string &s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string to_lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (true) {
        const std::size_t next = s.find(sep, pos);
        if (next == std::string::npos) {
            out.push_back(s.substr(pos));
            return out;
        }
        out.push_back(s.substr(pos, next - pos));
        pos = next + 1;
    }
}

/// Constant-time comparison for equal-length strings. Token hashes are always 64 hex chars,
/// so an early size check leaks nothing beyond "this is not a sha256 hex digest".
bool ct_equal(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

std::string iso_utc(const fc::time_point_sec &t) {
    return t.to_iso_string() + "Z";
}

std::string now_iso() {
    return iso_utc(fc::time_point_sec(static_cast<uint32_t>(fc::time_point::now().sec_since_epoch())));
}

// ── secret masking ──────────────────────────────────────────────────────────────────────────
bool sensitive_key(const std::string &key) {
    const std::string k = to_lower(key);
    static const char *needles[] = {
        "private-key", "private_key", "password", "secret", "token", "wif", "emergency"
    };
    for (const char *n : needles) {
        if (k.find(n) != std::string::npos) return true;
    }
    return false;
}

/// Redact key material from free text (log tail). Deliberately coarse: a false positive
/// (masking an innocent word) is harmless; a false negative is a leaked WIF.
std::string mask_text(const std::string &text) {
    static const std::regex wif(
        "(^|[^A-Za-z0-9])(5[HJK][1-9A-HJ-NP-Za-km-z]{49}|[KL][1-9A-HJ-NP-Za-km-z]{51})([^A-Za-z0-9]|$)",
        std::regex::ECMAScript);
    static const std::regex inline_secret(
        "((private-key|emergency-private-key|admin-token|password|secret)[[:space:]]*[=:][[:space:]]*)([^[:space:]\\\"]+)",
        std::regex::ECMAScript | std::regex::icase);
    std::string out = std::regex_replace(text, wif, "$1***$3");
    out = std::regex_replace(out, inline_secret, "$1***");
    return out;
}

// ── config file ────────────────────────────────────────────────────────────────────────────
struct cfg_entry {
    std::string section;
    std::string key;
    std::string value;
};

std::vector<cfg_entry> parse_config_file(const std::string &path, bool *exists = nullptr) {
    std::vector<cfg_entry> entries;
    std::ifstream in(path.c_str());
    if (!in) {
        if (exists) *exists = false;
        return entries;
    }
    if (exists) *exists = true;

    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t[0] == '[' && t.back() == ']') {
            section = trim(t.substr(1, t.size() - 2));
            continue;
        }
        const std::size_t eq = t.find('=');
        if (eq == std::string::npos) {
            entries.push_back(cfg_entry{section, trim(t), ""});
        } else {
            entries.push_back(cfg_entry{section, trim(t.substr(0, eq)), trim(t.substr(eq + 1))});
        }
    }
    return entries;
}

// ── tail a file ────────────────────────────────────────────────────────────────────────────
struct tail_result {
    bool ok = false;
    std::string error;
    uint64_t size = 0;
    uint64_t bytes_returned = 0;
    std::vector<std::string> lines;
};

int64_t elapsed_ms(const fc::time_point &started) {
    return (fc::time_point::now() - started).count() / 1000;
}

// The admin API is machine-facing: emit real JSON numbers. fc's default formatting quotes every
// int64 above 2^32-1 and every double, which turns byte counts and load averages into strings
// and forces the client to parse them twice.
std::string to_json(const fc::variant &v) {
    return fc::json::to_string(v, fc::json::legacy_generator);
}

int64_t param_int(const fc::variant_object &o, const std::string &key, int64_t def) {
    auto it = o.find(key);
    if (it == o.end()) return def;
    try {
        return it->value().as_int64();
    } catch (...) {
        return def;
    }
}

std::string param_str(const fc::variant_object &o, const std::string &key, const std::string &def) {
    auto it = o.find(key);
    if (it == o.end() || !it->value().is_string()) return def;
    return it->value().as_string();
}

tail_result tail_file(const std::string &path, uint64_t bytes, std::size_t max_lines) {
    tail_result r;
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        r.error = "cannot open file";
        return r;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end < 0) {
        r.error = "cannot stat file";
        return r;
    }
    r.size = static_cast<uint64_t>(end);
    const uint64_t take = std::min<uint64_t>(bytes, r.size);
    in.seekg(static_cast<std::streamoff>(r.size - take), std::ios::beg);
    std::string buf(static_cast<std::size_t>(take), '\0');
    in.read(&buf[0], static_cast<std::streamsize>(take));
    buf.resize(static_cast<std::size_t>(in.gcount()));
    r.bytes_returned = buf.size();

    std::vector<std::string> all = split(buf, '\n');
    // A tail that starts mid-line produces a truncated first line; drop it unless we read
    // the whole file (then the first line is genuine).
    if (take < r.size && !all.empty()) all.erase(all.begin());
    // Files end with a newline, so the final split element is an empty string — not a line.
    if (!all.empty() && all.back().empty()) all.pop_back();
    if (all.size() > max_lines) {
        all.erase(all.begin(), all.end() - static_cast<std::ptrdiff_t>(max_lines));
    }
    r.lines = std::move(all);
    r.ok = true;
    return r;
}

// ── websocketpp server config (same stub-log shape as webserver_plugin) ─────────────────────
struct asio_with_stub_log : public websocketpp::config::asio {
    typedef asio_with_stub_log type;
    typedef asio base;

    typedef base::concurrency_type concurrency_type;
    typedef base::request_type request_type;
    typedef base::response_type response_type;
    typedef base::message_type message_type;
    typedef base::con_msg_manager_type con_msg_manager_type;
    typedef base::endpoint_msg_manager_type endpoint_msg_manager_type;
    typedef base::alog_type alog_type;
    typedef base::elog_type elog_type;
    typedef base::rng_type rng_type;

    struct transport_config : public base::transport_config {
        typedef type::concurrency_type concurrency_type;
        typedef type::alog_type alog_type;
        typedef type::elog_type elog_type;
        typedef type::request_type request_type;
        typedef type::response_type response_type;
        typedef websocketpp::transport::asio::basic_socket::endpoint socket_type;
    };

    typedef websocketpp::transport::asio::endpoint<transport_config> transport_type;

    static const long timeout_open_handshake = 0;
};

typedef websocketpp::server<asio_with_stub_log> websocket_server_type;

// ── JSON-RPC error ─────────────────────────────────────────────────────────────────────────
struct rpc_error {
    int code;
    std::string message;
    std::string reason;
    std::string hint;
};

// ── tokens ─────────────────────────────────────────────────────────────────────────────────
struct token_info {
    std::string label;
    std::set<std::string> scopes;
    std::string hash_hex;
    uint64_t expires = 0;
};

token_info parse_token_spec(const std::string &spec) {
    // label:scope[,scope]:sha256hex[:expires-unixtime]
    const std::vector<std::string> parts = split(spec, ':');
    FC_ASSERT(parts.size() >= 3 && parts.size() <= 4,
              "admin-token: expected 'label:scope[,scope]:sha256hex[:expires]', got '${s}'",
              ("s", spec));
    token_info t;
    t.label = trim(parts[0]);
    FC_ASSERT(!t.label.empty(), "admin-token: empty label");

    for (const std::string &raw : split(parts[1], ',')) {
        const std::string scope = trim(raw);
        FC_ASSERT(scope == "read" || scope == "plugins" || scope == "config" ||
                  scope == "process" || scope == "chain",
                  "admin-token: unknown scope '${s}' (allowed: read, plugins, config, process, chain)",
                  ("s", scope));
        t.scopes.insert(scope);
    }
    FC_ASSERT(!t.scopes.empty(), "admin-token: token '${l}' has no scopes", ("l", t.label));

    t.hash_hex = to_lower(trim(parts[2]));
    FC_ASSERT(t.hash_hex.size() == 64 &&
              t.hash_hex.find_first_not_of("0123456789abcdef") == std::string::npos,
              "admin-token: token '${l}' hash is not 64 hex chars", ("l", t.label));

    if (parts.size() == 4) {
        const std::string exp = trim(parts[3]);
        FC_ASSERT(!exp.empty() && exp.find_first_not_of("0123456789") == std::string::npos,
                  "admin-token: token '${l}' has a non-numeric expiry", ("l", t.label));
        t.expires = std::stoull(exp);
    }
    return t;
}

} // namespace

// ── impl ───────────────────────────────────────────────────────────────────────────────────
struct node_admin_plugin::impl {
    explicit impl(chain::plugin &c) : chain(c) {}

    chain::plugin &chain;

    // listener
    asio::io_context ios;
    websocket_server_type server;
    std::thread thread;
    std::unique_ptr<tcp::endpoint> endpoint;
    bool listening = false;
    fc::time_point started_at = fc::time_point::now();

    // auth / policy
    std::vector<token_info> tokens;
    std::string audit_path;
    std::map<std::string, std::string> log_files; // name -> path
    bool allow_remote = false;

    // rate limit: label -> (window start sec, count in window)
    std::mutex rate_mutex;
    std::map<std::string, std::pair<uint32_t, uint32_t>> rate;

    std::atomic<uint64_t> request_seq{0};
    std::mutex audit_mutex;

    // ── listener ───────────────────────────────────────────────────────────────────────────
    void start();
    void stop();
    void handle_http(connection_hdl hdl);

    // ── auth ───────────────────────────────────────────────────────────────────────────────
    const token_info *authenticate(const std::string &header, std::string &why) const;
    bool rate_ok(const std::string &label);

    // ── dispatch ───────────────────────────────────────────────────────────────────────────
    fc::variant dispatch(const std::string &method, const fc::variant &params,
                         const token_info &token);
    fc::variant rpc_ok(const fc::variant &id, const fc::variant &result) const;
    fc::variant rpc_err(const fc::variant &id, const rpc_error &e) const;

    // ── methods ────────────────────────────────────────────────────────────────────────────
    fc::mutable_variant_object make_node_info() const;
    fc::variant h_ping(const fc::variant &params);
    fc::variant h_node_info(const fc::variant &params);
    fc::variant h_diagnostics(const fc::variant &params);
    fc::variant h_plugins_list(const fc::variant &params);
    fc::variant h_config_get(const fc::variant &params);
    fc::variant h_logs_tail(const fc::variant &params);
    fc::variant h_audit_tail(const fc::variant &params);

    // ── MCP (Streamable HTTP) ──────────────────────────────────────────────────────────────
    fc::variant mcp_tools() const;
    fc::variant h_mcp_initialize(const fc::variant &params);
    fc::variant h_mcp_ping(const fc::variant &params);
    fc::variant h_mcp_tools_list(const fc::variant &params);
    fc::variant h_mcp_tools_call(const fc::variant &params, const token_info &token);

    // ── audit ──────────────────────────────────────────────────────────────────────────────
    void audit(const std::string &rid, const std::string &label, const std::string &method,
               const fc::variant &params, const std::string &outcome, int code,
               int64_t duration_ms, const std::string &remote);
    void audit_line(const std::string &line);

    std::string data_dir() const { return appbase::app().data_dir().string(); }
    std::string config_path() const { return appbase::app().config_path().string(); }

    void respond(websocket_server_type::connection_ptr con,
                 websocketpp::http::status_code::value status, const std::string &body) {
        try {
            con->append_header("Content-Type", "application/json");
            con->append_header("Cache-Control", "no-store");
            con->set_body(body);
            con->set_status(status);
            con->send_http_response();
        } catch (const std::exception &e) {
            wlog("node_admin: failed to send response: ${e}", ("e", e.what()));
        } catch (...) {
        }
    }
};

// ── option registration ────────────────────────────────────────────────────────────────────
void node_admin_plugin::set_program_options(
    bpo::options_description &command_line_options,
    bpo::options_description &config_file_options
) {
    // Config-file description only, like testnet_plugin: appbase also accepts these on the
    // command line, and adding the same long name to both descriptions makes boost refuse
    // every start with "option is ambiguous".
    bpo::options_description opts("Node administration API");
    opts.add_options()
        ("admin-endpoint", bpo::value<std::string>(),
         "HTTP endpoint of the admin API, e.g. 127.0.0.1:8099. Empty (default) keeps the "
         "plugin inert.")
        ("admin-token", bpo::value<std::vector<std::string>>()->composing()->multitoken(),
         "Access token: label:scope[,scope]:sha256hex[:expires-unixtime]. Repeatable.")
        ("admin-allow-remote", bpo::value<bool>()->default_value(false),
         "Allow admin-endpoint on a non-loopback address. Off by default; a remote admin "
         "port is a security decision, not a convenience.")
        ("admin-audit-log", bpo::value<std::string>()->default_value(""),
         "Append-only audit log path. Default: node-admin-audit.log next to config.ini.")
        ("admin-log-file", bpo::value<std::vector<std::string>>()->composing()->multitoken(),
         "File the admin API may tail, as name=path or path. Repeatable. Nothing outside "
         "this allow-list can be read through admin.logs.tail.");
    config_file_options.add(opts);
    (void) command_line_options;
}

node_admin_plugin::node_admin_plugin() {}
node_admin_plugin::~node_admin_plugin() = default;

// ── lifecycle ─────────────────────────────────────────────────────────────────────────────
void node_admin_plugin::plugin_initialize(const bpo::variables_map &options) {
    pimpl.reset(new impl(appbase::app().get_plugin<chain::plugin>()));

    if (!options.count("admin-endpoint")) {
        ilog("node_admin: admin-endpoint is not set; the admin API is disabled");
        return;
    }

    const std::string ep = options.at("admin-endpoint").as<std::string>();
    if (trim(ep).empty()) {
        ilog("node_admin: admin-endpoint is empty; the admin API is disabled");
        return;
    }

    pimpl->allow_remote = options.at("admin-allow-remote").as<bool>();

    // Tokens are mandatory: an admin port without authentication is never a valid state.
    FC_ASSERT(options.count("admin-token"), "node_admin: admin-endpoint is set but no admin-token is configured");
    for (const std::string &spec : options.at("admin-token").as<std::vector<std::string>>()) {
        pimpl->tokens.push_back(parse_token_spec(spec));
    }
    FC_ASSERT(!pimpl->tokens.empty(), "node_admin: admin-endpoint is set but no admin-token is configured");

    const std::string audit = trim(options.at("admin-audit-log").as<std::string>());
    if (audit.empty()) {
        bfs::path p = appbase::app().config_path();
        p = p.parent_path() / "node-admin-audit.log";
        pimpl->audit_path = p.string();
    } else {
        pimpl->audit_path = audit;
    }

    if (options.count("admin-log-file")) {
        for (const std::string &spec : options.at("admin-log-file").as<std::vector<std::string>>()) {
            const std::size_t eq = spec.find('=');
            std::string name;
            std::string path;
            if (eq == std::string::npos) {
                path = trim(spec);
                name = bfs::path(path).filename().string();
            } else {
                name = trim(spec.substr(0, eq));
                path = trim(spec.substr(eq + 1));
            }
            FC_ASSERT(!name.empty() && !path.empty(),
                      "admin-log-file: expected 'name=path' or 'path', got '${s}'", ("s", spec));
            pimpl->log_files[name] = path;
        }
    }

    // Resolve and validate the endpoint. The listener is loopback-only unless the operator
    // says otherwise: a bearer token on an open port is a different threat model.
    std::vector<tcp::endpoint> eps = appbase::app().resolve_string_to_ip_endpoints(ep);
    FC_ASSERT(!eps.empty(), "admin-endpoint '${e}' did not resolve", ("e", ep));
    const tcp::endpoint chosen = eps.front();
    if (!chosen.address().is_loopback() && !pimpl->allow_remote) {
        FC_ASSERT(false,
                  "admin-endpoint '${e}' is not a loopback address; set admin-allow-remote = true "
                  "if this is deliberate", ("e", ep));
    }
    pimpl->endpoint.reset(new tcp::endpoint(chosen));

    ilog("node_admin: configured on ${host}:${port}, tokens=${n}, audit=${audit}",
         ("host", chosen.address().to_string())("port", chosen.port())
         ("n", pimpl->tokens.size())("audit", pimpl->audit_path));
    if (!chosen.address().is_loopback()) {
        wlog("node_admin: listening on a NON-loopback address ${host} — the admin API is exposed to the network",
             ("host", chosen.address().to_string()));
    }
}

void node_admin_plugin::plugin_startup() {
    if (!pimpl || !pimpl->endpoint) return;
    pimpl->start();
}

void node_admin_plugin::plugin_shutdown() {
    if (pimpl) pimpl->stop();
}

void node_admin_plugin::impl::start() {
    server.clear_access_channels(websocketpp::log::alevel::all);
    server.clear_error_channels(websocketpp::log::elevel::all);
    server.init_asio(&ios);
    server.set_reuse_addr(true);
    server.set_max_http_body_size(kMaxBodyBytes);

    server.set_http_handler([this](connection_hdl hdl) { this->handle_http(hdl); });

    server.listen(*endpoint);
    server.start_accept();
    listening = true;
    started_at = fc::time_point::now();

    thread = std::thread([this]() {
        try {
            ios.run();
        } catch (const std::exception &e) {
            elog("node_admin: listener stopped with an error: ${e}", ("e", e.what()));
        } catch (...) {
            elog("node_admin: listener stopped with an unknown error");
        }
    });
    ilog("node_admin: listening on ${host}:${port}",
         ("host", endpoint->address().to_string())("port", endpoint->port()));
}

void node_admin_plugin::impl::stop() {
    if (!listening) return;
    listening = false;
    try {
        server.stop_listening();
    } catch (...) {
    }
    ios.stop();
    if (thread.joinable()) thread.join();
    ilog("node_admin: listener stopped");
}

// ── auth / rate limit ─────────────────────────────────────────────────────────────────────
const token_info *node_admin_plugin::impl::authenticate(const std::string &header,
                                                        std::string &why) const {
    static const std::string prefix = "bearer ";
    const std::string h = trim(header);
    if (h.size() <= prefix.size() || to_lower(h.substr(0, prefix.size())) != prefix) {
        why = "missing or malformed Authorization header (expected 'Bearer <token>')";
        return nullptr;
    }
    const std::string token = trim(h.substr(prefix.size()));
    if (token.empty()) {
        why = "empty bearer token";
        return nullptr;
    }
    const std::string hash = fc::sha256::hash(token).str();
    const uint64_t now = static_cast<uint64_t>(fc::time_point::now().sec_since_epoch());
    for (const token_info &t : tokens) {
        if (!ct_equal(hash, t.hash_hex)) continue;
        if (t.expires != 0 && now > t.expires) {
            why = "token expired";
            return nullptr;
        }
        return &t;
    }
    why = "unknown token";
    return nullptr;
}

bool node_admin_plugin::impl::rate_ok(const std::string &label) {
    const uint32_t now = static_cast<uint32_t>(fc::time_point::now().sec_since_epoch());
    std::lock_guard<std::mutex> lock(rate_mutex);
    auto &slot = rate[label];
    if (slot.first != now) {
        slot.first = now;
        slot.second = 0;
    }
    if (++slot.second > kRatePerSecond) return false;
    return true;
}

// ── HTTP entry ────────────────────────────────────────────────────────────────────────────
void node_admin_plugin::impl::handle_http(connection_hdl hdl) {
    websocket_server_type::connection_ptr con;
    try {
        con = server.get_con_from_hdl(hdl);
    } catch (...) {
        return;
    }
    if (!con) return;
    con->defer_http_response();

    const fc::time_point started = fc::time_point::now();
    const std::string rid = std::to_string(static_cast<unsigned long long>(++request_seq));
    // The real peer, not X-Forwarded-For: this listener is never behind a proxy by design, so
    // that header would only let a client choose its own audit attribution.
    std::string remote;
    try {
        remote = con->get_remote_endpoint();
    } catch (...) {
    }

    auto finish = [&](websocketpp::http::status_code::value status, const std::string &body) {
        respond(con, status, body);
    };

    if (con->get_request().get_method() != "POST") {
        const fc::variant id;
        finish(websocketpp::http::status_code::method_not_allowed,
               to_json(rpc_err(id, rpc_error{-32600, "only POST is supported", "", ""})));
        audit(rid, "-", con->get_request().get_method(), fc::variant(), "method_not_allowed",
              -32600, elapsed_ms(started), remote);
        return;
    }

    const std::string body = con->get_request_body();
    if (body.empty()) {
        finish(websocketpp::http::status_code::bad_request,
               to_json(rpc_err(fc::variant(), rpc_error{-32700, "empty request body", "", ""})));
        audit(rid, "-", "-", fc::variant(), "empty_body", -32700, elapsed_ms(started), remote);
        return;
    }
    if (body.size() > kMaxBodyBytes) {
        finish(websocketpp::http::status_code::request_entity_too_large,
               to_json(rpc_err(fc::variant(), rpc_error{-32600, "request body too large", "", ""})));
        audit(rid, "-", "-", fc::variant(), "body_too_large", -32600, elapsed_ms(started), remote);
        return;
    }

    // ── auth first: parsing untrusted JSON before authentication is needless attack surface
    std::string why;
    const token_info *token = authenticate(con->get_request().get_header("Authorization"), why);
    if (!token) {
        finish(websocketpp::http::status_code::unauthorized,
               to_json(rpc_err(fc::variant(),
                                           rpc_error{-32001, "unauthorized", why, ""})));
        audit(rid, "-", "-", fc::variant(), "unauthorized", -32001, elapsed_ms(started), remote);
        return;
    }
    if (!rate_ok(token->label)) {
        finish(websocketpp::http::status_code::too_many_requests,
               to_json(rpc_err(fc::variant(),
                                           rpc_error{-32005, "rate limit exceeded", "", ""})));
        audit(rid, token->label, "-", fc::variant(), "rate_limited", -32005, elapsed_ms(started), remote);
        return;
    }

    // ── parse
    fc::variant request;
    try {
        request = fc::json::from_string(body);
    } catch (const fc::exception &e) {
        finish(websocketpp::http::status_code::bad_request,
               to_json(rpc_err(fc::variant(),
                                           rpc_error{-32700, "parse error", e.to_string(), ""})));
        audit(rid, token->label, "-", fc::variant(), "parse_error", -32700, elapsed_ms(started), remote);
        return;
    }
    if (!request.is_object()) {
        finish(websocketpp::http::status_code::bad_request,
               to_json(rpc_err(fc::variant(),
                                           rpc_error{-32600, "invalid request", "expected a JSON object", ""})));
        audit(rid, token->label, "-", fc::variant(), "invalid_request", -32600, elapsed_ms(started), remote);
        return;
    }

    const fc::variant_object &obj = request.get_object();
    fc::variant id;
    auto id_it = obj.find("id");
    if (id_it != obj.end()) id = id_it->value();

    if (!obj.contains("method") || !obj["method"].is_string()) {
        finish(websocketpp::http::status_code::bad_request,
               to_json(rpc_err(id, rpc_error{-32600, "invalid request", "missing 'method'", ""})));
        audit(rid, token->label, "-", fc::variant(), "invalid_request", -32600, elapsed_ms(started), remote);
        return;
    }
    const std::string method = obj["method"].as_string();
    fc::variant params;
    auto p_it = obj.find("params");
    if (p_it != obj.end()) params = p_it->value();

    // JSON-RPC notification (no id): MCP clients send `notifications/initialized` this way and
    // expect 202 with no body. Answering a notification with a response is a protocol error.
    if (method.compare(0, 14, "notifications/") == 0) {
        finish(websocketpp::http::status_code::accepted, "");
        audit(rid, token->label, method, params, "notification", 0, elapsed_ms(started), remote);
        return;
    }

    try {
        const fc::variant result = dispatch(method, params, *token);
        finish(websocketpp::http::status_code::ok, to_json(rpc_ok(id, result)));
        audit(rid, token->label, method, params, "ok", 0, elapsed_ms(started), remote);
    } catch (const rpc_error &e) {
        finish(websocketpp::http::status_code::ok, to_json(rpc_err(id, e)));
        audit(rid, token->label, method, params, "error", e.code, elapsed_ms(started), remote);
    } catch (const fc::exception &e) {
        finish(websocketpp::http::status_code::ok,
               to_json(rpc_err(id, rpc_error{-32099, "internal error", e.to_string(), ""})));
        audit(rid, token->label, method, params, "internal_error", -32099, elapsed_ms(started), remote);
    } catch (const std::exception &e) {
        finish(websocketpp::http::status_code::ok,
               to_json(rpc_err(id, rpc_error{-32099, "internal error", e.what(), ""})));
        audit(rid, token->label, method, params, "internal_error", -32099, elapsed_ms(started), remote);
    }
}

// ── dispatch ─────────────────────────────────────────────────────────────────────────────
fc::variant node_admin_plugin::impl::rpc_ok(const fc::variant &id, const fc::variant &result) const {
    fc::mutable_variant_object r;
    r("jsonrpc", "2.0");
    r("id", id);
    r("result", result);
    return fc::variant(r);
}

fc::variant node_admin_plugin::impl::rpc_err(const fc::variant &id, const rpc_error &e) const {
    fc::mutable_variant_object err;
    err("code", static_cast<int64_t>(e.code));
    err("message", e.message);
    if (!e.reason.empty() || !e.hint.empty()) {
        fc::mutable_variant_object data;
        if (!e.reason.empty()) data("reason", e.reason);
        if (!e.hint.empty()) data("hint", e.hint);
        err("data", data);
    }
    fc::mutable_variant_object r;
    r("jsonrpc", "2.0");
    r("id", id);
    r("error", err);
    return fc::variant(r);
}

fc::variant node_admin_plugin::impl::dispatch(const std::string &method, const fc::variant &params,
                                             const token_info &token) {
    // v1 is read-only; every method needs the `read` scope. Later stages add their own
    // scopes (plugins/config/process/chain) on the same check.
    if (token.scopes.find("read") == token.scopes.end()) {
        throw rpc_error{-32002, "forbidden", "token '" + token.label + "' has no 'read' scope", ""};
    }

    if (method == "admin.ping") return h_ping(params);
    if (method == "admin.node_info") return h_node_info(params);
    if (method == "admin.diagnostics") return h_diagnostics(params);
    if (method == "admin.plugins.list") return h_plugins_list(params);
    if (method == "admin.config.get") return h_config_get(params);
    if (method == "admin.logs.tail") return h_logs_tail(params);
    if (method == "admin.audit.tail") return h_audit_tail(params);

    // MCP Streamable HTTP on the same authenticated listener (decision q#3052=B: the MCP
    // endpoint lives inside the plugin, not in a sidecar). Tools are 1:1 with the admin.*
    // methods above, so the audit trail and scopes are shared.
    if (method == "initialize") return h_mcp_initialize(params);
    if (method == "ping") return h_mcp_ping(params);
    if (method == "tools/list") return h_mcp_tools_list(params);
    if (method == "tools/call") return h_mcp_tools_call(params, token);

    throw rpc_error{-32601, "method not found", method, ""};
}

// ── methods ──────────────────────────────────────────────────────────────────────────────
fc::mutable_variant_object node_admin_plugin::impl::make_node_info() const {
    const graphene::chain::database &db = chain.db();
    const auto head_time = db.head_block_time();
    const auto now = fc::time_point::now().sec_since_epoch();
    const int64_t head_age = static_cast<int64_t>(now) - static_cast<int64_t>(head_time.sec_since_epoch());

    fc::mutable_variant_object m;
    m("server_time", now_iso());
    m("version", std::string(CHAIN_VERSION));
    m("hardfork_count", static_cast<int64_t>(CHAIN_NUM_HARDFORKS));
    m("chain_id", db.get_chain_id().str());
    m("head_block_num", static_cast<int64_t>(db.head_block_num()));
    m("head_block_id", db.head_block_id().str());
    m("head_block_time", iso_utc(head_time));
    // An empty chain reports head time 1970, so the "age" would be 56 years and mean nothing.
    m("head_age_sec", db.head_block_num() == 0 ? fc::variant() : fc::variant(head_age < 0 ? 0 : head_age));
    m("last_irreversible_block_num",
      static_cast<int64_t>(db.get_dynamic_global_properties().last_irreversible_block_num));
    m("recovering", chain.is_recovering());
    m("uptime_sec", static_cast<int64_t>((fc::time_point::now() - started_at).count() / 1000000));
    return m;
}

fc::variant node_admin_plugin::impl::h_ping(const fc::variant &) {
    fc::mutable_variant_object m;
    m("ok", true);
    m("server_time", now_iso());
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_node_info(const fc::variant &) {
    return fc::variant(make_node_info());
}

fc::variant node_admin_plugin::impl::h_diagnostics(const fc::variant &) {
    fc::mutable_variant_object m = make_node_info();

    m("data_dir", data_dir());
    m("config_path", config_path());

    // peers: read through the p2p thread, because the peer map is not thread-safe
    bool p2p_loaded = false;
    uint32_t peers = 0;
    if (auto *p2p = appbase::app().find_plugin<graphene::plugins::p2p::p2p_plugin>()) {
        p2p_loaded = true;
        if (p2p->get_state() == appbase::abstract_plugin::started) {
            peers = p2p->get_connections_count_sync();
        }
    }
    m("p2p_loaded", p2p_loaded);
    m("peers", static_cast<int64_t>(peers));

    // configured plugins: appbase keeps its registry private, so config.ini is what we can see.
    // `plugins_observed` counts the names whose lifecycle state is read through a typed
    // find_plugin<T>(); the rest are known to be loaded by construction (appbase aborts startup
    // on an unknown plugin name) but their state cannot be read from here.
    bool cfg_exists = false;
    const std::vector<cfg_entry> cfg = parse_config_file(config_path(), &cfg_exists);
    std::set<std::string> configured;
    for (const auto &e : cfg) {
        if (e.key == "plugin" && !e.value.empty()) configured.insert(e.value);
    }
    std::size_t observed_count = 0;
    for (const auto &name : configured) {
        if (observe_plugin(name) != nullptr) ++observed_count;
    }
    m("plugins_configured", static_cast<int64_t>(configured.size()));
    m("plugins_observed", static_cast<int64_t>(observed_count));

    // shared memory file
    boost::system::error_code ec;
    const bfs::path shm = bfs::path(data_dir()) / "shared_memory.bin";
    const uintmax_t shm_size = bfs::file_size(shm, ec);
    if (!ec) m("shared_memory_bytes", static_cast<int64_t>(shm_size));

#ifndef _WIN32
    struct statvfs vfs;
    if (::statvfs(data_dir().c_str(), &vfs) == 0) {
        m("disk_total_bytes",
          static_cast<int64_t>(static_cast<uint64_t>(vfs.f_blocks) * vfs.f_frsize));
        m("disk_free_bytes",
          static_cast<int64_t>(static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize));
    }
    struct rusage usage;
    if (::getrusage(RUSAGE_SELF, &usage) == 0) {
        m("rss_peak_kb", static_cast<int64_t>(usage.ru_maxrss));
    }
    double load[3] = {0, 0, 0};
    if (::getloadavg(load, 3) == 3) {
        m("loadavg", fc::variants{fc::variant(load[0]), fc::variant(load[1]), fc::variant(load[2])});
    }
#endif

    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_plugins_list(const fc::variant &) {
    bool exists = false;
    const std::vector<cfg_entry> cfg = parse_config_file(config_path(), &exists);

    fc::variants plugins;
    std::set<std::string> seen;
    for (const auto &e : cfg) {
        if (e.key != "plugin" || e.value.empty()) continue;
        if (!seen.insert(e.value).second) continue;

        const appbase::abstract_plugin *p = observe_plugin(e.value);
        const char *state = "unknown";
        const char *source = "inferred";
        if (p) {
            source = "observed";
            switch (p->get_state()) {
                case appbase::abstract_plugin::registered: state = "registered"; break;
                case appbase::abstract_plugin::initialized: state = "initialized"; break;
                case appbase::abstract_plugin::started: state = "started"; break;
                case appbase::abstract_plugin::stopped: state = "stopped"; break;
                default: state = "unknown"; break;
            }
        }
        fc::mutable_variant_object item;
        item("name", e.value);
        // A configured plugin is always loaded: appbase calls get_plugin(name) for every
        // `plugin =` line and aborts startup when the name is unknown.
        item("loaded", p ? p->get_state() != appbase::abstract_plugin::registered : true);
        item("state", p ? fc::variant(std::string(state)) : fc::variant());
        item("state_source", std::string(source));
        plugins.push_back(fc::variant(item));
    }

    fc::mutable_variant_object m;
    m("config_path", config_path());
    m("config_exists", exists);
    m("note", "Entries come from config.ini. Appbase keeps its plugin registry private, so "
              "lifecycle state is observed only for chain, p2p and node_admin; for the others "
              "loaded=true is implied by startup (an unknown plugin name aborts the node).");
    m("plugins", plugins);
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_config_get(const fc::variant &) {
    bool exists = false;
    const std::vector<cfg_entry> cfg = parse_config_file(config_path(), &exists);

    // Preserve order and duplicate keys (config.ini repeats `plugin = ...`), so this is an
    // array of sections rather than a map.
    fc::variants sections;
    std::string current;
    fc::variants current_entries;
    bool have_section = false;
    auto flush = [&]() {
        fc::mutable_variant_object s;
        s("name", current);
        s("entries", current_entries);
        sections.push_back(fc::variant(s));
        current_entries.clear();
    };
    for (const auto &e : cfg) {
        if (!have_section || e.section != current) {
            if (have_section) flush();
            current = e.section;
            have_section = true;
        }
        fc::mutable_variant_object item;
        item("key", e.key);
        item("value", sensitive_key(e.key) ? std::string("***") : e.value);
        current_entries.push_back(fc::variant(item));
    }
    if (have_section) flush();

    fc::mutable_variant_object m;
    m("path", config_path());
    m("exists", exists);
    m("sections", sections);
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_logs_tail(const fc::variant &params) {
    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected an object {name, bytes?, lines?}", ""};
    }
    const fc::variant_object &o = params.get_object();
    const std::string name = param_str(o, "name", "");
    if (name.empty()) {
        throw rpc_error{-32602, "invalid params", "missing 'name'", "known names: the admin-log-file entries"};
    }
    auto it = log_files.find(name);
    if (it == log_files.end()) {
        std::string known;
        for (const auto &kv : log_files) {
            if (!known.empty()) known += ", ";
            known += kv.first;
        }
        throw rpc_error{-32004, "unknown log", "no admin-log-file entry named '" + name + "'",
                        known.empty() ? "no admin-log-file entries are configured" : "known: " + known};
    }

    const int64_t bytes = std::max<int64_t>(1, std::min<int64_t>(
        param_int(o, "bytes", 16 * 1024), static_cast<int64_t>(kMaxLogBytes)));
    const int64_t lines = std::max<int64_t>(1, std::min<int64_t>(
        param_int(o, "lines", 200), static_cast<int64_t>(kMaxLines)));

    tail_result t = tail_file(it->second, static_cast<uint64_t>(bytes), static_cast<std::size_t>(lines));
    if (!t.ok) {
        throw rpc_error{-32004, "cannot read log", t.error, it->second};
    }

    fc::variants out;
    for (const std::string &line : t.lines) out.push_back(fc::variant(mask_text(line)));

    fc::mutable_variant_object m;
    m("name", name);
    m("path", it->second);
    m("size", static_cast<int64_t>(t.size));
    m("bytes_returned", static_cast<int64_t>(t.bytes_returned));
    m("lines", out);
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_audit_tail(const fc::variant &params) {
    const fc::variant_object empty;
    const fc::variant_object &o = params.is_object() ? params.get_object() : empty;
    const int64_t lines = std::max<int64_t>(1, std::min<int64_t>(
        param_int(o, "lines", 100), static_cast<int64_t>(kMaxLines)));

    tail_result t = tail_file(audit_path, kMaxLogBytes, static_cast<std::size_t>(lines));
    if (!t.ok) {
        throw rpc_error{-32004, "cannot read audit log", t.error, audit_path};
    }
    fc::variants out;
    for (const std::string &line : t.lines) out.push_back(fc::variant(line));

    fc::mutable_variant_object m;
    m("path", audit_path);
    m("size", static_cast<int64_t>(t.size));
    m("lines", out);
    return fc::variant(m);
}

// ── MCP (Streamable HTTP) ────────────────────────────────────────────────────────────────
// The MCP endpoint is the same loopback listener and the same bearer tokens as admin.*.
// Tools call the admin methods 1:1, so every tool invocation lands in the audit log with
// its own method name (`tools/call` is the JSON-RPC method; the tool name is in params).
fc::variant node_admin_plugin::impl::mcp_tools() const {
    auto schema = [](fc::variants props, fc::variants required) {
        fc::mutable_variant_object s;
        s("type", "object");
        s("properties", props);
        s("additionalProperties", false);
        if (!required.empty()) s("required", required);
        return fc::variant(s);
    };
    auto prop = [](const char *type, const char *desc) {
        fc::mutable_variant_object p;
        p("type", type);
        p("description", desc);
        return fc::variant(p);
    };
    auto tool = [&](const char *name, const char *desc, const fc::variant &input) {
        fc::mutable_variant_object t;
        t("name", name);
        t("description", desc);
        t("inputSchema", input);
        return fc::variant(t);
    };
    const fc::variant no_args = schema(fc::variants{}, fc::variants{});

    fc::variants logs_props;
    logs_props.push_back(prop("string", "Name from admin-log-file entries"));
    logs_props.push_back(prop("integer", "Maximum bytes to read from the tail (default 16384)"));
    logs_props.push_back(prop("integer", "Maximum lines to return (default 200)"));
    fc::variants logs_required;
    logs_required.push_back(fc::variant(std::string("name")));

    fc::variants audit_props;
    audit_props.push_back(prop("integer", "Maximum audit lines to return (default 100)"));

    fc::variants tools;
    tools.push_back(tool("admin.ping", "Liveness check: server time and ok=true.", no_args));
    tools.push_back(tool("admin.node_info",
                         "Chain id, head block, hardfork, uptime and head age.", no_args));
    tools.push_back(tool("admin.diagnostics",
                         "Node info plus data dir, peer count, disk/RSS/loadavg and plugin counters.",
                         no_args));
    tools.push_back(tool("admin.plugins.list",
                         "Plugins configured in config.ini with observed lifecycle state.", no_args));
    tools.push_back(tool("admin.config.get",
                         "Effective config.ini by sections with secret values masked.", no_args));
    tools.push_back(tool("admin.logs.tail",
                         "Tail an allow-listed log file; secrets and WIFs are masked.",
                         schema(logs_props, logs_required)));
    tools.push_back(tool("admin.audit.tail",
                         "Tail the node_admin audit log (one JSON line per request outcome).",
                         schema(audit_props, fc::variants{})));
    return fc::variant(tools);
}

fc::variant node_admin_plugin::impl::h_mcp_initialize(const fc::variant &params) {
    std::string requested;
    if (params.is_object()) {
        requested = param_str(params.get_object(), "protocolVersion", "");
    }
    // Echo only versions we actually implement; otherwise answer with our own, as the spec
    // requires ("the server MUST respond with a protocol version it supports").
    const std::set<std::string> supported{"2024-11-05", "2025-03-26", "2025-06-18"};
    const std::string proto =
        supported.count(requested) ? requested : std::string("2025-06-18");

    fc::mutable_variant_object tools_cap;
    tools_cap("listChanged", false);
    fc::mutable_variant_object caps;
    caps("tools", tools_cap);

    fc::mutable_variant_object server;
    server("name", "viz-node-admin");
    server("version", "0.1.0");

    fc::mutable_variant_object m;
    m("protocolVersion", proto);
    m("capabilities", caps);
    m("serverInfo", server);
    m("instructions",
      "Read-only administration of this VIZ node. Tools map 1:1 to admin.* methods; "
      "mutating tools arrive with node_admin v2 and use the same bearer token and audit log.");
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_mcp_ping(const fc::variant &) {
    return fc::variant(fc::mutable_variant_object());
}

fc::variant node_admin_plugin::impl::h_mcp_tools_list(const fc::variant &) {
    fc::mutable_variant_object m;
    m("tools", mcp_tools());
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_mcp_tools_call(const fc::variant &params,
                                                      const token_info &token) {
    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected an object {name, arguments}", ""};
    }
    const fc::variant_object &o = params.get_object();
    const std::string name = param_str(o, "name", "");
    if (name.empty()) {
        throw rpc_error{-32602, "invalid params", "missing tool name", "see tools/list"};
    }
    const fc::variant tools = mcp_tools();
    bool known = false;
    for (const fc::variant &t : tools.get_array()) {
        if (t.get_object()["name"].as_string() == name) {
            known = true;
            break;
        }
    }
    if (!known) {
        throw rpc_error{-32602, "unknown tool", name, "see tools/list"};
    }

    fc::variant args;
    auto a_it = o.find("arguments");
    if (a_it != o.end() && !a_it->value().is_null()) {
        if (!a_it->value().is_object()) {
            throw rpc_error{-32602, "invalid params", "'arguments' must be an object", ""};
        }
        args = a_it->value();
    }

    auto content = [](const std::string &text, bool is_error) {
        fc::mutable_variant_object item;
        item("type", "text");
        item("text", text);
        fc::mutable_variant_object m;
        m("content", fc::variants{fc::variant(item)});
        m("isError", is_error);
        return fc::variant(m);
    };

    try {
        return content(to_json(dispatch(name, args, token)), false);
    } catch (const rpc_error &e) {
        // Tool execution errors are reported inside the result (isError), not as a
        // protocol-level JSON-RPC error: the model must see what the tool said.
        std::string text = "error " + std::to_string(e.code) + ": " + e.message;
        if (!e.reason.empty()) text += ": " + e.reason;
        if (!e.hint.empty()) text += " (" + e.hint + ")";
        return content(text, true);
    } catch (const fc::exception &e) {
        return content("internal error: " + e.to_string(), true);
    } catch (const std::exception &e) {
        return content(std::string("internal error: ") + e.what(), true);
    }
}


// ── audit ────────────────────────────────────────────────────────────────────────────────
void node_admin_plugin::impl::audit(const std::string &rid, const std::string &label,
                                    const std::string &method, const fc::variant &params,
                                    const std::string &outcome, int code, int64_t duration_ms,
                                    const std::string &remote) {
    try {
        fc::mutable_variant_object m;
        m("ts", now_iso());
        m("rid", rid);
        m("token", label.empty() ? "-" : label);
        m("method", method);
        if (!params.is_null()) m("params", params);
        m("outcome", outcome);
        m("code", static_cast<int64_t>(code));
        m("ms", duration_ms);
        m("remote", remote.empty() ? "-" : remote);
        audit_line(mask_text(to_json(fc::variant(m))));
    } catch (...) {
        // Auditing must never break the request path.
    }
}

void node_admin_plugin::impl::audit_line(const std::string &line) {
    std::lock_guard<std::mutex> lock(audit_mutex);
#ifndef _WIN32
    // rotate at a fixed size instead of growing without bound
    struct stat st;
    if (::stat(audit_path.c_str(), &st) == 0 && st.st_size > static_cast<off_t>(kAuditRotateBytes)) {
        ::rename(audit_path.c_str(), (audit_path + ".1").c_str());
    }
    const int fd = ::open(audit_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;
    const std::string data = line + "\n";
    ssize_t ignored = ::write(fd, data.data(), data.size());
    (void) ignored;
    ::close(fd);
#else
    std::ofstream out(audit_path.c_str(), std::ios::app);
    if (out) out << line << "\n";
#endif
}

} // namespace node_admin
} // namespace plugins
} // namespace graphene
