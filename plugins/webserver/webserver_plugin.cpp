#include <graphene/plugins/webserver/webserver_plugin.hpp>
#include <graphene/plugins/webserver/ip_counter_cache.hpp>

#include <graphene/plugins/chain/plugin.hpp>
#include <graphene/plugins/snapshot/plugin.hpp>
#include <graphene/protocol/block.hpp>

#include <fc/network/ip.hpp>
#include <fc/log/logger_config.hpp>
#include <fc/io/json.hpp>
#include <fc/network/resolve.hpp>
#include <fc/crypto/sha256.hpp>

#include <fc/compat/asio_compat.hpp>
#include <boost/asio.hpp>
#include <boost/filesystem.hpp>
#include <boost/preprocessor/stringize.hpp>

#include <functional>

#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/logger/stub.hpp>
#ifndef _WIN32
#include <websocketpp/logger/syslog.hpp>
#endif

#include <thread>
#include <memory>
#include <iostream>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <atomic>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <cctype>
#include <ctime>
#include <chrono>
#include <algorithm>
#include <graphene/plugins/json_rpc/plugin.hpp>

namespace graphene {
    namespace plugins {
        namespace webserver {

            namespace asio = boost::asio;

            using std::map;
            using std::string;
            using boost::asio::ip::tcp;
            using std::shared_ptr;
            using websocketpp::connection_hdl;

            typedef uint32_t thread_pool_size_t;

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
                //typedef websocketpp::log::stub elog_type;
                //typedef websocketpp::log::stub alog_type;

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

            using websocket_server_type = websocketpp::server<asio_with_stub_log>;

            // API prefixes whose responses must never be cached because
            // the calls are mutating (broadcast, locks, etc.).
            // Uses proper JSON parsing to reliably detect both
            //   "method":"network_broadcast_api.xxx"  and  "method":"call","params":["network_broadcast_api",...]
            static bool is_cacheable_request(const fc::variant& request) {
                try {
                    if (!request.is_object()) return true;
                    const auto& obj = request.get_object();

                    // Check top-level method
                    if (obj.contains("method")) {
                        std::string method = obj["method"].as_string();
                        if (method.find("network_broadcast_api") != std::string::npos) return false;
                        if (method.find("debug_node") != std::string::npos) return false;
                    }

                    // Check params for call-style requests: params[0] is the API name
                    if (obj.contains("params") && obj["params"].is_array()) {
                        const auto& params = obj["params"].get_array();
                        if (!params.empty() && params[0].is_string()) {
                            std::string api_name = params[0].as_string();
                            if (api_name.find("network_broadcast_api") != std::string::npos) return false;
                            if (api_name.find("debug_node") != std::string::npos) return false;
                        }
                    }
                } catch (...) {
                    // Malformed request — don't cache
                    return false;
                }
                return true;
            }

            // Extract the "id" value from a parsed JSON-RPC request as a raw string
            // suitable for injecting into a response (preserves quotes for string ids).
            // Returns "" if no id field is found.
            static std::string extract_request_id(const fc::variant& request) {
                try {
                    if (!request.is_object()) return "";
                    const auto& obj = request.get_object();
                    if (!obj.contains("id")) return "";
                    const auto& id = obj["id"];
                    // Return the JSON-serialized form of the id value
                    // so it can be directly spliced into a response.
                    return fc::json::to_string(id);
                } catch (...) {
                    return "";
                }
            }

            // Generate a cache key from a parsed JSON-RPC request that is independent
            // of the "id" field. Uses only method + params so that identical calls with
            // different IDs share the same cache entry, preventing cache bypass via
            // id rotation (spam attack pattern).
            static std::string make_cache_key(const fc::variant& request) {
                // For batch requests (array), use full body hash
                if (request.is_array()) {
                    return fc::sha256::hash(fc::json::to_string(request)).str();
                }

                std::string key_material;
                try {
                    if (!request.is_object()) return fc::sha256::hash(key_material).str();
                    const auto& obj = request.get_object();

                    if (obj.contains("method")) {
                        key_material += fc::json::to_string(obj["method"]);
                    }
                    if (obj.contains("params")) {
                        key_material += fc::json::to_string(obj["params"]);
                    }
                } catch (...) {
                    // On any parse error, use empty key material
                }

                return fc::sha256::hash(key_material).str();
            }

            // Replace the "id" value in a JSON-RPC response string with a new one,
            // ensuring the response id matches the request id per JSON-RPC 2.0 spec.
            static std::string patch_response_id(const std::string& response, const std::string& new_id) {
                if (new_id.empty()) return response;

                // Parse response properly to find and replace id
                try {
                    auto resp = fc::json::from_string(response);
                    if (!resp.is_object()) return response;
                    auto& obj = resp.get_object();
                    if (!obj.contains("id")) return response;

                    // Replace id value and re-serialize
                    auto id_var = fc::json::from_string(new_id);
                    // We need to mutate the object — convert to mutable_variant_object
                    fc::mutable_variant_object mobj(obj);
                    mobj["id"] = id_var;
                    return fc::json::to_string(mobj);
                } catch (...) {
                    // Fallback: if parsing fails, return response as-is
                    return response;
                }
            }

            // Cache entry for JSON-RPC responses
            struct cache_entry {
                std::string response;
                uint32_t block_num;
            };

            // ── request accounting, IP lists and admission limits ───────────────────────
            // All of it is driven by `webserver-stats-enabled` (off by default: the node
            // then behaves exactly as before). The blacklist works even with stats off —
            // it is a security control, not a metric. Enabled limits keep their counters
            // even while stats logging is disabled.

            enum class admission {
                allow,
                blacklisted,
                rate_limited
            };

            static uint32_t now_seconds() {
                return static_cast<uint32_t>(::time(nullptr));
            }

            // ::ffff:1.2.3.4 (IPv4-mapped IPv6, what an IPv6 listening socket reports for
            // IPv4 clients) is folded back to 1.2.3.4 so a single client is one identity
            // regardless of how the socket was opened.
            static std::string normalize_ip(std::string ip) {
                static const std::string mapped = "::ffff:";
                if (ip.size() > mapped.size() && ip.compare(0, mapped.size(), mapped) == 0) {
                    ip.erase(0, mapped.size());
                }
                return ip;
            }

            static bool valid_ip_text(const std::string &ip) {
                if (ip.empty() || ip.size() > 45) return false;
                for (char c : ip) {
                    if (!(std::isdigit(static_cast<unsigned char>(c)) || c == '.' || c == ':' ||
                          (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
                        return false;
                    }
                }
                return true;
            }

            static std::string iso_utc(uint32_t t) {
                std::time_t tt = static_cast<std::time_t>(t);
                std::tm tm{};
#ifdef _WIN32
                gmtime_s(&tm, &tt);
#else
                gmtime_r(&tt, &tm);
#endif
                char buf[32];
                std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
                return std::string(buf);
            }

            static std::string peer_ip(const websocket_server_type::connection_ptr &con) {
                boost::system::error_code ec;
                const auto ep = con->get_socket().remote_endpoint(ec);
                if (ec) return "unknown";
                return normalize_ip(ep.address().to_string());
            }

            struct webserver_plugin::webserver_plugin_impl final {
            public:
                boost::thread_group& thread_pool = appbase::app().scheduler();
                webserver_plugin_impl(thread_pool_size_t thread_pool_size) : thread_pool_work(asio::make_work_guard(this->thread_pool_ios)) {
                    for (uint32_t i = 0; i < thread_pool_size; ++i) {
                        thread_pool.create_thread(std::bind(static_cast<std::size_t(asio::io_context::*)()>(&asio::io_context::run), &thread_pool_ios));
                    }
                }

                void start_webserver();

                void stop_webserver();

                void handle_ws_message(websocket_server_type *, connection_hdl, websocket_server_type::message_ptr);

                void handle_http_message(websocket_server_type *, connection_hdl);

                // Cache methods
                fc::optional<std::string> get_cached_response(const std::string& request_hash);
                void cache_response(const std::string& request_hash, const std::string& response, uint32_t block_num);
                void clear_cache();

                shared_ptr<std::thread> http_thread;
                asio::io_context http_ios;
                optional<tcp::endpoint> http_endpoint;
                websocket_server_type http_server;

                shared_ptr<std::thread> ws_thread;
                asio::io_context ws_ios;
                optional<tcp::endpoint> ws_endpoint;
                websocket_server_type ws_server;
                asio::io_context thread_pool_ios;
                asio::executor_work_guard<asio::io_context::executor_type> thread_pool_work;

                plugins::json_rpc::plugin *api;
                chain::plugin *chain_plugin = nullptr;
                snapshot::snapshot_plugin *snap_plugin = nullptr;
                boost::signals2::connection chain_sync_con;
                boost::signals2::connection applied_block_conn;

                // Cache for JSON-RPC responses
                std::unordered_map<std::string, cache_entry> response_cache;
                std::mutex cache_mutex;
                uint32_t current_block_num = 0;
                bool cache_enabled = true;
                size_t max_cache_size = 10000;

                // Request accounting and admission. `stats_mutex` guards everything below
                // up to `state_path`; request handlers take it once per request.
                mutable std::mutex stats_mutex;
                bool stats_enabled = false;
                std::string config_policy;
                std::unordered_set<std::string> trusted_proxies;
                std::string client_ip(const websocket_server_type::connection_ptr &con) const {
                    std::string ip = peer_ip(con);
                    if (!trusted_proxies.count(ip)) return ip;
                    const std::string header = con->get_request().get_header("X-Forwarded-For");
                    if (header.size() > 4096) return ip;
                    std::vector<std::string> hops;
                    size_t start = 0;
                    while (start < header.size()) {
                        const size_t end = header.find(',', start);
                        std::string hop = header.substr(start, end == std::string::npos ? end : end - start);
                        const auto first = hop.find_first_not_of(" \t");
                        const auto last = hop.find_last_not_of(" \t");
                        hop = first == std::string::npos ? "" : hop.substr(first, last - first + 1);
                        hop = normalize_ip(hop);
                        if (!valid_ip_text(hop)) return ip;
                        hops.push_back(hop);
                        if (end == std::string::npos) break;
                        start = end + 1;
                    }
                    // Walk from the authenticated TCP peer towards the client. Never trust
                    // the leftmost address supplied by an untrusted hop.
                    for (auto it = hops.rbegin(); it != hops.rend() && trusted_proxies.count(ip); ++it)
                        ip = *it;
                    return ip;
                }
                webserver_limits limits;
                detail::ip_counter_cache ip_stats;
                std::unordered_set<std::string> whitelist;
                std::unordered_set<std::string> blacklist;
                uint64_t total_requests = 0;
                uint64_t blocked_requests = 0;
                uint64_t limited_requests = 0;
                std::string stats_log_path;
                uint64_t stats_log_max_bytes = 10ULL * 1024 * 1024;
                uint32_t stats_log_keep = 5;
                uint32_t stats_interval_sec = 60;
                std::string state_path;
                std::atomic<bool> stats_stop{false};
                std::thread stats_thread;
                std::chrono::steady_clock::time_point last_stats_flush;

                // Called by both HTTP and WS handlers before any work is done. Returns
                // `allow`, `blacklisted` or `rate_limited`; counters are updated here.
                admission admit(const std::string &raw_ip);

                void rotate_stats_log_locked();
                void flush_stats_locked(bool force);
                void stats_loop();
                bool save_admin_state_locked();
                bool load_admin_state_locked(std::string &error);
            };

            void webserver_plugin::webserver_plugin_impl::start_webserver() {
                if (ws_endpoint) {
                    ws_thread = std::make_shared<std::thread>([&]() {
                        ilog("start processing ws thread");
                        try {
                            ws_server.clear_access_channels(websocketpp::log::alevel::all);
                            ws_server.clear_error_channels(websocketpp::log::elevel::all);
                            ws_server.init_asio(&ws_ios);
                            ws_server.set_reuse_addr(true);

                            ws_server.set_message_handler(std::bind(&webserver_plugin_impl::handle_ws_message, this, &ws_server, std::placeholders::_1, std::placeholders::_2));

                            if (http_endpoint && http_endpoint == ws_endpoint) {
                                ws_server.set_http_handler(std::bind(&webserver_plugin_impl::handle_http_message, this, &ws_server, std::placeholders::_1));
                                ilog("start listending for http requests");
                            }

                            ilog("start listening for ws requests");
                            ws_server.listen(*ws_endpoint);
                            ws_server.start_accept();

                            ws_ios.run();
                            ilog("ws io service exit");
                        } catch (...) {
                            elog("error thrown from http io service");
                        }
                    });
                }

                if (http_endpoint && ((ws_endpoint && ws_endpoint != http_endpoint) || !ws_endpoint)) {
                    http_thread = std::make_shared<std::thread>( [&]() {
                        ilog("start processing http thread");
                        try {
                            http_server.clear_access_channels(websocketpp::log::alevel::all);
                            http_server.clear_error_channels(websocketpp::log::elevel::all);
                            http_server.init_asio(&http_ios);
                            http_server.set_reuse_addr(true);

                            http_server.set_http_handler([this](connection_hdl hdl) {
                                this->handle_http_message(&this->http_server, hdl);
                            });

                            ilog("start listening for http requests");
                            http_server.listen(*http_endpoint);
                            http_server.start_accept();

                            http_ios.run();
                            ilog("http io service exit");
                        } catch (...) {
                            elog("error thrown from http io service");
                        }
                    });
                }
            }

            void webserver_plugin::webserver_plugin_impl::stop_webserver() {
                if (ws_server.is_listening()) {
                    ws_server.stop_listening();
                }

                if (http_server.is_listening()) {
                    http_server.stop_listening();
                }

                thread_pool_ios.stop();
                thread_pool.join_all();

                if (ws_thread) {
                    ws_ios.stop();
                    ws_thread->join();
                    ws_thread.reset();
                }

                if (http_thread) {
                    http_ios.stop();
                    http_thread->join();
                    http_thread.reset();
                }
            }

            fc::optional<std::string> webserver_plugin::webserver_plugin_impl::get_cached_response(const std::string& request_hash) {
                if (!cache_enabled) {
                    return fc::optional<std::string>();
                }

                std::lock_guard<std::mutex> lock(cache_mutex);
                auto it = response_cache.find(request_hash);
                if (it != response_cache.end()) {
                    // Check if the cached response is still valid for current block
                    if (it->second.block_num == current_block_num) {
                        return it->second.response;
                    }
                }
                return fc::optional<std::string>();
            }

            void webserver_plugin::webserver_plugin_impl::cache_response(const std::string& request_hash, const std::string& response, uint32_t block_num) {
                if (!cache_enabled) {
                    return;
                }

                std::lock_guard<std::mutex> lock(cache_mutex);

                // Simple eviction: clear if cache is too large
                if (response_cache.size() >= max_cache_size) {
                    response_cache.clear();
                }

                response_cache[request_hash] = cache_entry{response, block_num};
            }

            void webserver_plugin::webserver_plugin_impl::clear_cache() {
                std::lock_guard<std::mutex> lock(cache_mutex);
                response_cache.clear();
            }

            // ── stats/limits implementation ─────────────────────────────────────────────

            admission webserver_plugin::webserver_plugin_impl::admit(const std::string &raw_ip) {
                const std::string ip = normalize_ip(raw_ip);
                const uint32_t now = now_seconds();

                std::lock_guard<std::mutex> lock(stats_mutex);

                // The blacklist is enforced even when stats are off: it is an access
                // control, not a metric.
                if (blacklist.count(ip)) {
                    ++blocked_requests;
                    auto it = ip_stats.find(ip);
                    if (it != ip_stats.end()) { auto &c = ip_stats.touch(ip); ++c.blocked; c.last_seen = now; }
                    return admission::blacklisted;
                }
                if (!stats_enabled && !limits.enabled) {
                    return admission::allow;
                }

                // Bounded LRU admission evicts only the oldest inactive counter.
                // A source-address spray must not lock every new client out for a day.
                auto &c = ip_stats.touch(ip);
                c.last_seen = now;
                ++c.total;
                ++total_requests;

                const uint32_t minute_bucket = now / 60;
                const uint32_t hour_bucket = now / 3600;
                const uint32_t day_bucket = now / 86400;
                if (c.minute_bucket != minute_bucket) { c.minute_bucket = minute_bucket; c.minute = 0; }
                if (c.hour_bucket != hour_bucket)     { c.hour_bucket = hour_bucket; c.hour = 0; }
                if (c.day_bucket != day_bucket)       { c.day_bucket = day_bucket; c.day = 0; }
                ++c.minute;
                ++c.hour;
                ++c.day;

                if (limits.enabled && !whitelist.count(ip)) {
                    if (c.minute > limits.per_minute || c.hour > limits.per_hour ||
                        c.day > limits.per_day) {
                        ++c.limited;
                        ++limited_requests;
                        return admission::rate_limited;
                    }
                }
                return admission::allow;
            }

            void webserver_plugin::webserver_plugin_impl::rotate_stats_log_locked() {
                if (stats_log_path.empty() || stats_log_max_bytes == 0) return;
                boost::system::error_code ec;
                const uintmax_t size = boost::filesystem::file_size(stats_log_path, ec);
                if (ec || size < stats_log_max_bytes) return;

                const std::string base = stats_log_path;
                try {
                    if (stats_log_keep == 0) {
                        boost::filesystem::remove(base, ec);
                    } else {
                        // base.N is the oldest; shift down and drop the last one.
                        boost::system::error_code ignore;
                        boost::filesystem::remove(base + "." + std::to_string(stats_log_keep), ignore);
                        for (uint32_t i = stats_log_keep; i > 1; --i) {
                            const std::string from = base + "." + std::to_string(i - 1);
                            const std::string to = base + "." + std::to_string(i);
                            if (boost::filesystem::exists(from, ignore)) {
                                boost::filesystem::rename(from, to, ignore);
                            }
                        }
                        boost::filesystem::rename(base, base + ".1", ignore);
                    }
                    ilog("webserver stats log rotated (${s} bytes, keeping ${k})",
                         ("s", size)("k", stats_log_keep));
                } catch (const std::exception &e) {
                    wlog("webserver stats log rotation failed: ${e}", ("e", e.what()));
                }
            }

            void webserver_plugin::webserver_plugin_impl::flush_stats_locked(bool force) {
                std::unique_lock<std::mutex> lock(stats_mutex);
                if (!stats_enabled || stats_log_path.empty()) return;

                struct row { std::string ip; detail::ip_counters c; bool white; bool black; };
                std::vector<row> changed;
                for (auto &kv : ip_stats) {
                    auto &c = kv.second;
                    if (!force && c.total + c.blocked == c.logged) continue;
                    c.logged = c.total + c.blocked;
                    changed.push_back({kv.first, c, whitelist.count(kv.first) != 0, blacklist.count(kv.first) != 0});
                }
                if (changed.empty() && !force) return;
                const uint64_t total = total_requests, blocked = blocked_requests, limited = limited_requests;
                const size_t ips = ip_stats.size();
                const auto policy = limits;
                lock.unlock(); // Formatting and filesystem I/O do not block admission.
                const uint32_t now = now_seconds();
                const auto ts = iso_utc(now);
                std::ostringstream out;
                for (const auto &r : changed) {
                    const auto &c = r.c;
                    out << ts << " ip=" << r.ip << " total=" << c.total
                        << " minute=" << c.minute << " hour=" << c.hour << " day=" << c.day
                        << " blocked=" << c.blocked << " limited=" << c.limited
                        << " whitelisted=" << r.white << " blacklisted=" << r.black << "\n";
                }
                out << ts << " summary total=" << total << " ips=" << ips
                    << " blocked=" << blocked << " limited=" << limited
                    << " limit_m=" << policy.per_minute << " limit_h=" << policy.per_hour
                    << " limit_d=" << policy.per_day << " limits_enabled=" << policy.enabled << "\n";

                try {
                    rotate_stats_log_locked();
                    std::ofstream f(stats_log_path, std::ios::app | std::ios::binary);
                    if (!f.is_open()) {
                        // The directory may not exist yet (custom path).
                        const auto parent = boost::filesystem::path(stats_log_path).parent_path();
                        if (!parent.empty()) {
                            boost::system::error_code ec;
                            boost::filesystem::create_directories(parent, ec);
                        }
                        f.open(stats_log_path, std::ios::app | std::ios::binary);
                    }
                    if (!f.is_open()) {
                        wlog("webserver stats log is not writable: ${p}", ("p", stats_log_path));
                        return;
                    }
                    f << out.str();
                    f.flush();
                } catch (const std::exception &e) {
                    wlog("webserver stats log write failed: ${e}", ("e", e.what()));
                }
            }

            void webserver_plugin::webserver_plugin_impl::stats_loop() {
                const uint32_t step_ms = 1000;
                uint32_t waited_ms = 0;
                while (!stats_stop.load(std::memory_order_acquire)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(step_ms));
                    waited_ms += step_ms;
                    if (waited_ms < stats_interval_sec * 1000) continue;
                    waited_ms = 0;
                    flush_stats_locked(false);
                }
            }

            bool webserver_plugin::webserver_plugin_impl::save_admin_state_locked() {
                if (state_path.empty()) return false;
                fc::mutable_variant_object v;
                v("config_policy", config_policy);
                v("stats_enabled", stats_enabled);
                v("limits_enabled", limits.enabled);
                v("per_minute", limits.per_minute);
                v("per_hour", limits.per_hour);
                v("per_day", limits.per_day);
                fc::variants wl, bl;
                for (const auto &ip : whitelist) wl.push_back(fc::variant(ip));
                for (const auto &ip : blacklist) bl.push_back(fc::variant(ip));
                v("whitelist", fc::variant(wl));
                v("blacklist", fc::variant(bl));

                try {
                    const std::string tmp = state_path + ".tmp";
                    {
                        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                        if (!f.is_open()) return false;
                        f << fc::json::to_string(fc::variant(v));
                        f.flush();
                        if (!f.good()) return false;
                    }
                    boost::system::error_code ec;
                    boost::filesystem::rename(tmp, state_path, ec);
                    if (ec) {
                        boost::filesystem::remove(state_path, ec);
                        boost::filesystem::rename(tmp, state_path, ec);
                    }
                    return !ec;
                } catch (const std::exception &e) {
                    wlog("webserver admin state save failed: ${e}", ("e", e.what()));
                    return false;
                }
            }

            bool webserver_plugin::webserver_plugin_impl::load_admin_state_locked(std::string &error) {
                if (state_path.empty()) {
                    error = "no state file configured";
                    return false;
                }
                if (!boost::filesystem::exists(state_path)) {
                    error = "state file not found: " + state_path;
                    return false;
                }
                try {
                    std::ifstream f(state_path, std::ios::binary);
                    if (!f.is_open()) {
                        error = "cannot open state file: " + state_path;
                        return false;
                    }
                    std::stringstream ss;
                    ss << f.rdbuf();
                    const fc::variant v = fc::json::from_string(ss.str());
                    const auto &o = v.get_object();

                    auto read_u32 = [&](const char *key, uint32_t fallback) -> uint32_t {
                        if (!o.contains(key)) return fallback;
                        try {
                            const int64_t x = o[key].as_int64();
                            return x < 0 ? 0 : static_cast<uint32_t>(x);
                        } catch (...) {
                            return fallback;
                        }
                    };
                    // Parse into locals and commit only when the whole file is valid:
                    // a malformed field must not leave the live policy half-updated.
                    const bool same_config = o.contains("config_policy") &&
                        o["config_policy"].as_string() == config_policy;
                    bool new_stats = stats_enabled;
                    bool new_limits_enabled = limits.enabled;
                    if (same_config && o.contains("stats_enabled")) new_stats = o["stats_enabled"].as_bool();
                    if (same_config && o.contains("limits_enabled")) new_limits_enabled = o["limits_enabled"].as_bool();
                    const uint32_t new_m = std::max<uint32_t>(1, (same_config ? read_u32("per_minute", limits.per_minute) : limits.per_minute));
                    const uint32_t new_h = std::max<uint32_t>(1, (same_config ? read_u32("per_hour", limits.per_hour) : limits.per_hour));
                    const uint32_t new_d = std::max<uint32_t>(1, (same_config ? read_u32("per_day", limits.per_day) : limits.per_day));

                    auto read_list = [&](const char *key) {
                        std::unordered_set<std::string> dst;
                        if (!o.contains(key)) return dst;
                        const auto &arr = o[key].get_array();
                        for (const auto &item : arr) {
                            const std::string ip = normalize_ip(item.as_string());
                            if (valid_ip_text(ip)) dst.insert(ip);
                        }
                        return dst;
                    };
                    const bool has_wl = o.contains("whitelist");
                    const bool has_bl = o.contains("blacklist");
                    std::unordered_set<std::string> new_wl = has_wl ? read_list("whitelist")
                                                                    : std::unordered_set<std::string>();
                    std::unordered_set<std::string> new_bl = has_bl ? read_list("blacklist")
                                                                    : std::unordered_set<std::string>();

                    stats_enabled = new_stats;
                    limits.enabled = new_limits_enabled;
                    limits.per_minute = new_m;
                    limits.per_hour = new_h;
                    limits.per_day = new_d;
                    if (has_wl) whitelist = std::move(new_wl);
                    if (has_bl) blacklist = std::move(new_bl);
                    return true;
                } catch (const std::exception &e) {
                    error = std::string("cannot parse state file: ") + e.what();
                    return false;
                }
            }

            void webserver_plugin::webserver_plugin_impl::handle_ws_message(
                websocket_server_type *server,
                connection_hdl hdl,
                websocket_server_type::message_ptr msg
            ) {
                auto con = server->get_con_from_hdl(hdl);
                const admission verdict = admit(client_ip(con));
                if (verdict != admission::allow) {
                    const char *msg = verdict == admission::blacklisted
                        ? "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32005,\"message\":\"forbidden\"},\"id\":null}"
                        : "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32006,\"message\":\"rate limit exceeded\"},\"id\":null}";
                    try {
                        con->send(msg);
                    } catch (...) {
                    }
                    return;
                }
                boost::asio::post(thread_pool_ios, [con, msg, this]() {
                    try {
                        if ((chain_plugin && chain_plugin->is_recovering()) ||
                        (snap_plugin && snap_plugin->is_snapshot_reloading())) {
                            auto ec = con->send("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32003,\"message\":\"Node is recovering from shared memory corruption, please retry later\"},\"id\":null}");
                            if (ec) throw websocketpp::exception(ec);
                            return;
                        }

                        if (msg->get_opcode() == websocketpp::frame::opcode::text) {
                            auto body = msg->get_payload();

                            // Parse JSON once for all cache operations
                            fc::variant parsed;
                            try {
                                parsed = fc::json::from_string(body);
                            } catch (...) {
                                // Invalid JSON — skip cache, let json_rpc handle the error
                                api->call(body, [con](const std::string &data){
                                    auto ec = con->send(data);
                                    if (ec) throw websocketpp::exception(ec);
                                });
                                return;
                            }

                            bool cacheable = is_cacheable_request(parsed);

                            // Generate id-independent cache key from request body
                            std::string request_hash;
                            std::string request_id;
                            if (cacheable) {
                                request_hash = make_cache_key(parsed);
                                request_id = extract_request_id(parsed);

                                // Check cache first
                                auto cached_response = get_cached_response(request_hash);
                                if (cached_response.valid()) {
                                    // Patch the id in cached response to match request
                                    std::string patched = patch_response_id(*cached_response, request_id);
                                    auto ec = con->send(patched);
                                    if (ec) {
                                        throw websocketpp::exception(ec);
                                    }
                                    return;
                                }
                            }

                            api->call(body, [con, this, request_hash, cacheable](const std::string &data){
                                auto ec = con->send(data);
                                if (ec) {
                                    throw websocketpp::exception(ec);
                                }

                                // Cache the response only for read-only methods
                                if (cacheable) {
                                    cache_response(request_hash, data, current_block_num);
                                }
                            });
                        } else {
                            con->send("error: string payload expected");
                        }
                    } catch (const fc::exception &e) {
                        con->send("error calling API " + e.to_string());
                    }
                });
            }

            void webserver_plugin::webserver_plugin_impl::handle_http_message(websocket_server_type *server, connection_hdl hdl) {
                auto con = server->get_con_from_hdl(hdl);

                switch (admit(client_ip(con))) {
                    case admission::blacklisted:
                        con->append_header("Access-Control-Allow-Origin", "*");
                        con->set_body("forbidden");
                        con->set_status(websocketpp::http::status_code::forbidden);
                        try { con->send_http_response(); } catch (...) {}
                        return;
                    case admission::rate_limited:
                        con->append_header("Access-Control-Allow-Origin", "*");
                        con->append_header("Retry-After", "60");
                        con->set_body("rate limit exceeded");
                        con->set_status(websocketpp::http::status_code::too_many_requests);
                        try { con->send_http_response(); } catch (...) {}
                        return;
                    case admission::allow:
                        break;
                }

                con->defer_http_response();

                // CORS preflight
                if (con->get_request().get_method() == "OPTIONS") {
                    con->append_header("Access-Control-Allow-Origin", "*");
                    con->append_header("Access-Control-Allow-Methods", "POST, GET, OPTIONS");
                    // Reflect exactly the headers the browser asked to send. A fixed
                    //   allow-list blocks any request carrying a header outside it
                    //   (custom X-* headers, x-api-key, etc.), which the browser reports
                    //   as a CORS preflight failure. Falls back to the common defaults
                    //   when the browser did not advertise any headers.
                    const std::string &requested_headers = con->get_request().get_header("Access-Control-Request-Headers");
                    con->append_header("Access-Control-Allow-Headers",
                        requested_headers.empty() ? std::string("Content-Type, Authorization") : requested_headers);
                    con->append_header("Access-Control-Max-Age", "86400");
                    con->set_status(websocketpp::http::status_code::ok);
                    try { con->send_http_response(); } catch (...) {}
                    return;
                }

                boost::asio::post(thread_pool_ios, [con, this]() {
                    if ((chain_plugin && chain_plugin->is_recovering()) ||
                        (snap_plugin && snap_plugin->is_snapshot_reloading())) {
                        con->append_header("Access-Control-Allow-Origin", "*");
                        con->set_body("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32003,\"message\":\"Node is recovering from shared memory corruption, please retry later\"},\"id\":null}");
                        con->set_status(websocketpp::http::status_code::service_unavailable);
                        try { con->send_http_response(); } catch (...) {}
                        return;
                    }

                    auto body = con->get_request_body();

                    if (body.empty()) {
                        con->append_header("Access-Control-Allow-Origin", "*");
                        con->set_body("empty request body");
                        con->set_status(websocketpp::http::status_code::bad_request);
                        try { con->send_http_response(); } catch (...) {}
                        return;
                    }

                    // Parse JSON once for all cache operations
                    fc::variant parsed;
                    try {
                        parsed = fc::json::from_string(body);
                    } catch (...) {
                        // Invalid JSON — skip cache, let json_rpc handle the error
                        try {
                            api->call(body, [con](const std::string &data){
                                con->append_header("Access-Control-Allow-Origin", "*");
                                con->set_body(data);
                                con->set_status(websocketpp::http::status_code::ok);
                                con->send_http_response();
                            });
                        } catch (fc::exception &e) {
                            edump((e));
                            con->append_header("Access-Control-Allow-Origin", "*");
                            con->set_body("Could not call API");
                            con->set_status(websocketpp::http::status_code::not_found);
                            try { con->send_http_response(); } catch (...) {}
                        }
                        return;
                    }

                    bool cacheable = is_cacheable_request(parsed);

                    // Generate id-independent cache key from request body
                    std::string request_hash;
                    std::string request_id;
                    if (cacheable) {
                        request_hash = make_cache_key(parsed);
                        request_id = extract_request_id(parsed);

                        // Check cache first
                        auto cached_response = get_cached_response(request_hash);
                        if (cached_response.valid()) {
                            // Patch the id in cached response to match request
                            std::string patched = patch_response_id(*cached_response, request_id);
                            con->append_header("Access-Control-Allow-Origin", "*");
                            con->set_body(patched);
                            con->set_status(websocketpp::http::status_code::ok);
                            con->send_http_response();
                            return;
                        }
                    }

                    try {
                        api->call(body, [con, this, request_hash, cacheable](const std::string &data){
                            // this lambda can be called from any thread in application
                            //   for example, when task was delegated ( see msg_pack(msg_pack&&) )
                            con->append_header("Access-Control-Allow-Origin", "*");
                            con->set_body(data);
                            con->set_status(websocketpp::http::status_code::ok);
                            con->send_http_response();

                            // Cache the response only for read-only methods
                            if (cacheable) {
                                cache_response(request_hash, data, current_block_num);
                            }
                        });
                    } catch (fc::exception &e) {
                        // this case happens if exception was thrown on parsing request
                        edump((e));
                        con->append_header("Access-Control-Allow-Origin", "*");
                        con->set_body("Could not call API");
                        con->set_status(websocketpp::http::status_code::not_found);
                        // this sending response can't be merged with sending response from try-block
                        //   because try-block can work from other thread,
                        //   when catch-block happens in current thread on parsing request
                        try {
                            con->send_http_response();
                        } catch (...) {
                            // disable segfault
                        }
                    }
                });
            }

            webserver_plugin::webserver_plugin() {
            }

            webserver_admin_state webserver_plugin::admin_state(size_t max_ips) const {
                webserver_admin_state st;
                if (!my) return st;

                std::lock_guard<std::mutex> lock(my->stats_mutex);
                st.stats_enabled = my->stats_enabled;
                st.limits = my->limits;
                st.total_requests = my->total_requests;
                st.blocked_requests = my->blocked_requests;
                st.limited_requests = my->limited_requests;
                st.whitelist.assign(my->whitelist.begin(), my->whitelist.end());
                st.blacklist.assign(my->blacklist.begin(), my->blacklist.end());
                std::sort(st.whitelist.begin(), st.whitelist.end());
                std::sort(st.blacklist.begin(), st.blacklist.end());

                st.ips.reserve(my->ip_stats.size());
                st.tracked_ips = my->ip_stats.size();
                if (max_ips == 0) return st;
                for (const auto &kv : my->ip_stats) {
                    webserver_ip_stats s;
                    s.ip = kv.first;
                    s.total = kv.second.total;
                    s.minute = kv.second.minute;
                    s.hour = kv.second.hour;
                    s.day = kv.second.day;
                    s.blocked = kv.second.blocked;
                    s.limited = kv.second.limited;
                    s.last_seen = kv.second.last_seen;
                    s.whitelisted = my->whitelist.count(kv.first) > 0;
                    s.blacklisted = my->blacklist.count(kv.first) > 0;
                    if (s.total == 0 && s.blocked == 0) continue;
                    st.ips.push_back(std::move(s));
                }
                std::sort(st.ips.begin(), st.ips.end(),
                          [](const webserver_ip_stats &a, const webserver_ip_stats &b) {
                              if (a.total != b.total) return a.total > b.total;
                              return a.ip < b.ip;
                          });
                if (max_ips > 0 && st.ips.size() > max_ips) st.ips.resize(max_ips);
                return st;
            }

            void webserver_plugin::set_stats_enabled(bool enabled) {
                if (!my) return;
                std::lock_guard<std::mutex> lock(my->stats_mutex);
                my->stats_enabled = enabled;
                my->save_admin_state_locked();
            }

            bool webserver_plugin::set_limits(bool enabled, uint32_t per_minute, uint32_t per_hour,
                                              uint32_t per_day, std::string &error) {
                if (!my) {
                    error = "webserver plugin not initialized";
                    return false;
                }
                if (per_minute == 0 || per_hour == 0 || per_day == 0) {
                    error = "limits must be greater than zero";
                    return false;
                }
                {
                    std::lock_guard<std::mutex> lock(my->stats_mutex);
                    my->limits.enabled = enabled;
                    my->limits.per_minute = per_minute;
                    my->limits.per_hour = per_hour;
                    my->limits.per_day = per_day;
                    my->save_admin_state_locked();
                }
                ilog("webserver limits updated: enabled=${e} m=${m} h=${h} d=${d}",
                     ("e", enabled)("m", per_minute)("h", per_hour)("d", per_day));
                return true;
            }

            bool webserver_plugin::whitelist_add(const std::string &raw_ip) {
                if (!my) return false;
                const std::string ip = normalize_ip(raw_ip);
                if (!valid_ip_text(ip)) return false;
                std::lock_guard<std::mutex> lock(my->stats_mutex);
                my->blacklist.erase(ip);
                my->whitelist.insert(ip);
                my->save_admin_state_locked();
                return true;
            }

            bool webserver_plugin::whitelist_remove(const std::string &raw_ip) {
                if (!my) return false;
                const std::string ip = normalize_ip(raw_ip);
                std::lock_guard<std::mutex> lock(my->stats_mutex);
                const bool erased = my->whitelist.erase(ip) > 0;
                if (erased) my->save_admin_state_locked();
                return erased;
            }

            bool webserver_plugin::blacklist_add(const std::string &raw_ip) {
                if (!my) return false;
                const std::string ip = normalize_ip(raw_ip);
                if (!valid_ip_text(ip)) return false;
                std::lock_guard<std::mutex> lock(my->stats_mutex);
                my->whitelist.erase(ip);
                my->blacklist.insert(ip);
                my->save_admin_state_locked();
                return true;
            }

            bool webserver_plugin::blacklist_remove(const std::string &raw_ip) {
                if (!my) return false;
                const std::string ip = normalize_ip(raw_ip);
                std::lock_guard<std::mutex> lock(my->stats_mutex);
                const bool erased = my->blacklist.erase(ip) > 0;
                if (erased) my->save_admin_state_locked();
                return erased;
            }

            void webserver_plugin::reset_stats() {
                if (!my) return;
                std::lock_guard<std::mutex> lock(my->stats_mutex);
                my->ip_stats.clear();
                my->total_requests = 0;
                my->blocked_requests = 0;
                my->limited_requests = 0;
            }

            bool webserver_plugin::reload_admin_state(std::string &error) {
                if (!my) {
                    error = "webserver plugin not initialized";
                    return false;
                }
                std::lock_guard<std::mutex> lock(my->stats_mutex);
                return my->load_admin_state_locked(error);
            }

            webserver_plugin::~webserver_plugin() {
            }

            void webserver_plugin::set_program_options(boost::program_options::options_description &, boost::program_options::options_description &cfg) {
                cfg.add_options()
                    ("webserver-http-endpoint", boost::program_options::value<string>(),
                        "Local http endpoint for webserver requests.")
                    ("webserver-ws-endpoint", boost::program_options::value<string>(),
                        "Local websocket endpoint for webserver requests.")
                    ("rpc-endpoint", boost::program_options::value<string>(),
                        "Local http and websocket endpoint for webserver requests. Deprectaed in favor of webserver-http-endpoint and webserver-ws-endpoint")
                    ("webserver-thread-pool-size", boost::program_options::value<thread_pool_size_t>()->default_value(256),
                        "Number of threads used to handle queries. Default: 256.")
                    ("webserver-cache-enabled", boost::program_options::value<bool>()->default_value(true),
                        "Enable caching of JSON-RPC responses. Cache is cleared on each new block. Default: true.")
                    ("webserver-cache-size", boost::program_options::value<size_t>()->default_value(10000),
                        "Maximum number of cached JSON-RPC responses. Default: 10000.")
                    ("webserver-stats-enabled", boost::program_options::value<bool>()->default_value(false),
                        "Enable per-IP request accounting, the stats log and rate limits. Default: false.")
                    ("webserver-limits-enabled", boost::program_options::value<bool>(),
                        "Enforce request rate limits. Defaults to the value of webserver-stats-enabled.")
                    ("webserver-trusted-proxy", boost::program_options::value<std::vector<std::string>>()->composing(),
                        "Trusted reverse-proxy IP (repeatable). Only these peers may supply X-Forwarded-For.")
                    ("webserver-limit-m", boost::program_options::value<uint32_t>()->default_value(1000),
                        "Maximum requests per IP per minute when limits are enabled. Default: 1000.")
                    ("webserver-limit-h", boost::program_options::value<uint32_t>()->default_value(10000),
                        "Maximum requests per IP per hour when limits are enabled. Default: 10000.")
                    ("webserver-limit-d", boost::program_options::value<uint32_t>()->default_value(100000),
                        "Maximum requests per IP per day when limits are enabled. Default: 100000.")
                    ("webserver-stats-log", boost::program_options::value<string>(),
                        "Stats log file, rotated by size. Default: <data-dir>/webserver-stats.log.")
                    ("webserver-stats-log-max-size", boost::program_options::value<uint64_t>()->default_value(10ULL * 1024 * 1024),
                        "Rotate the stats log once it grows beyond this many bytes. Default: 10485760.")
                    ("webserver-stats-log-keep", boost::program_options::value<uint32_t>()->default_value(5),
                        "How many rotated stats logs to keep. Default: 5.")
                    ("webserver-stats-interval", boost::program_options::value<uint32_t>()->default_value(60),
                        "Seconds between stats log flushes. Default: 60.")
                    ("webserver-admin-state", boost::program_options::value<string>(),
                        "Live state file (limits and IP lists) written by node_admin. Default: <data-dir>/webserver-admin.json.");
            }

            void webserver_plugin::plugin_initialize(const boost::program_options::variables_map &options) {
                auto thread_pool_size = options.at("webserver-thread-pool-size").as<thread_pool_size_t>();
                FC_ASSERT(thread_pool_size > 0, "webserver-thread-pool-size must be greater than 0");
                ilog("configured with ${tps} thread pool size", ("tps", thread_pool_size));
                my.reset(new webserver_plugin_impl(thread_pool_size));

                // Read cache configuration
                my->cache_enabled = options.at("webserver-cache-enabled").as<bool>();
                my->max_cache_size = options.at("webserver-cache-size").as<size_t>();
                ilog("webserver cache enabled: ${enabled}, max size: ${size}",
                     ("enabled", my->cache_enabled)("size", my->max_cache_size));

                // Request accounting. The flag also enables the limits, because limits are
                // computed from the same per-IP counters; webserver-limits-enabled can
                // switch enforcement off while keeping the statistics.
                my->stats_enabled = options.at("webserver-stats-enabled").as<bool>();
                my->limits.enabled = options.count("webserver-limits-enabled")
                    ? options.at("webserver-limits-enabled").as<bool>()
                    : my->stats_enabled;
                // A zero limit would reject every request, which is never what an operator
                // means; clamp to at least one. The admin API applies the same floor.
                my->limits.per_minute = std::max<uint32_t>(1, options.at("webserver-limit-m").as<uint32_t>());
                my->limits.per_hour = std::max<uint32_t>(1, options.at("webserver-limit-h").as<uint32_t>());
                my->limits.per_day = std::max<uint32_t>(1, options.at("webserver-limit-d").as<uint32_t>());
                my->config_policy = std::to_string(my->stats_enabled) + ":" +
                    std::to_string(my->limits.enabled) + ":" + std::to_string(my->limits.per_minute) + ":" +
                    std::to_string(my->limits.per_hour) + ":" + std::to_string(my->limits.per_day);
                if (options.count("webserver-trusted-proxy"))
                    for (const auto &proxy : options.at("webserver-trusted-proxy").as<std::vector<std::string>>()) {
                        const auto ip = normalize_ip(proxy);
                        FC_ASSERT(valid_ip_text(ip), "Invalid webserver-trusted-proxy IP");
                        my->trusted_proxies.insert(ip);
                    }
                my->stats_log_max_bytes = options.at("webserver-stats-log-max-size").as<uint64_t>();
                my->stats_log_keep = options.at("webserver-stats-log-keep").as<uint32_t>();
                // Cap the interval so `stats_interval_sec * 1000` cannot overflow uint32.
                my->stats_interval_sec = std::min<uint32_t>(86400,
                    std::max<uint32_t>(1, options.at("webserver-stats-interval").as<uint32_t>()));

                const std::string data_dir = appbase::app().data_dir().string();
                my->stats_log_path = options.count("webserver-stats-log")
                    ? options.at("webserver-stats-log").as<string>()
                    : (boost::filesystem::path(data_dir) / "webserver-stats.log").string();
                my->state_path = options.count("webserver-admin-state")
                    ? options.at("webserver-admin-state").as<string>()
                    : (boost::filesystem::path(data_dir) / "webserver-admin.json").string();

                {
                    std::string state_error;
                    if (my->load_admin_state_locked(state_error)) {
                        ilog("webserver: live admin state loaded from ${p}", ("p", my->state_path));
                    } else if (boost::filesystem::exists(my->state_path)) {
                        wlog("webserver: admin state not loaded: ${e}", ("e", state_error));
                    }
                }
                ilog("webserver stats enabled: ${s}, limits: ${l} (${m}/${h}/${d} per IP per m/h/d), log: ${p}",
                     ("s", my->stats_enabled)("l", my->limits.enabled)
                     ("m", my->limits.per_minute)("h", my->limits.per_hour)
                     ("d", my->limits.per_day)("p", my->stats_log_path));

                // New-style endpoints are primary: webserver-http-endpoint and
                // webserver-ws-endpoint. rpc-endpoint is ONLY used as a fallback
                // when NEITHER new endpoint is configured at all.
                bool has_http = options.count("webserver-http-endpoint") > 0;
                bool has_ws   = options.count("webserver-ws-endpoint") > 0;

                if (has_http || has_ws) {
                    // New-style: use specific endpoints, ignore rpc-endpoint entirely.
                    if (has_http) {
                        auto http_ep = options.at("webserver-http-endpoint").as<string>();
                        auto endpoints = appbase::app().resolve_string_to_ip_endpoints(http_ep);
                        FC_ASSERT(endpoints.size(), "webserver-http-endpoint ${hostname} did not resolve",
                                  ("hostname", http_ep));
                        my->http_endpoint = endpoints[0];
                        auto tcp_endpoint = endpoints[0];
                        auto ip_port = tcp_endpoint.address().to_string() + ":" + std::to_string(tcp_endpoint.port());
                        ilog("configured http to listen on ${ep}", ("ep", ip_port));
                    }

                    if (has_ws) {
                        auto ws_ep = options.at("webserver-ws-endpoint").as<string>();
                        auto endpoints = appbase::app().resolve_string_to_ip_endpoints(ws_ep);
                        FC_ASSERT(endpoints.size(), "webserver-ws-endpoint ${hostname} did not resolve",
                                  ("hostname", ws_ep));
                        my->ws_endpoint = endpoints[0];
                        auto tcp_endpoint = endpoints[0];
                        auto ip_port = tcp_endpoint.address().to_string() + ":" + std::to_string(tcp_endpoint.port());
                        ilog("configured ws to listen on ${ep}", ("ep", ip_port));
                    }

                    if (options.count("rpc-endpoint")) {
                        wlog("rpc-endpoint is ignored because webserver-http-endpoint and/or webserver-ws-endpoint are set");
                    }
                } else if (options.count("rpc-endpoint")) {
                    // Legacy fallback: no new endpoints configured, use rpc-endpoint for both.
                    auto endpoint = options.at("rpc-endpoint").as<string>();
                    auto endpoints = appbase::app().resolve_string_to_ip_endpoints(endpoint);
                    FC_ASSERT(endpoints.size(), "rpc-endpoint ${hostname} did not resolve", ("hostname", endpoint));

                    auto tcp_endpoint = endpoints[0];
                    auto ip_port = tcp_endpoint.address().to_string() + ":" + std::to_string(tcp_endpoint.port());

                    my->http_endpoint = tcp_endpoint;
                    my->ws_endpoint = tcp_endpoint;
                    ilog("configured http to listen on ${ep} (from rpc-endpoint)", ("ep", ip_port));
                    ilog("configured ws to listen on ${ep} (from rpc-endpoint)", ("ep", ip_port));
                    wlog("rpc-endpoint is deprecated in favor of webserver-http-endpoint and webserver-ws-endpoint");
                }
            }

            void webserver_plugin::plugin_startup() {
                my->api = appbase::app().find_plugin<plugins::json_rpc::plugin>();
                FC_ASSERT(my->api != nullptr, "Could not find API Register Plugin");

                my->chain_plugin = appbase::app().find_plugin<chain::plugin>();
                my->snap_plugin = appbase::app().find_plugin<snapshot::snapshot_plugin>();

                chain::plugin *chain = my->chain_plugin;
                if (chain != nullptr && chain->get_state() != appbase::abstract_plugin::started) {
                    ilog("Waiting for chain plugin to start");
                    my->chain_sync_con = chain->on_sync.connect([this]() {
                        my->start_webserver();
                    });
                } else {
                    my->start_webserver();
                }

                // Connect to applied_block signal to update block number and clear cache
                if (chain != nullptr) {
                    my->applied_block_conn = chain->db().applied_block.connect([this](const protocol::signed_block &b) {
                        std::lock_guard<std::mutex> lock(my->cache_mutex);
                        my->current_block_num = b.block_num();
                        // Clear cache on new block since state may have changed
                        my->response_cache.clear();
                    });
                }

                // The stats thread exists even when stats are disabled: it makes the
                // admin switch cheap, and an idle 1 s tick costs nothing.
                my->stats_stop.store(false, std::memory_order_release);
                my->stats_thread = std::thread([this]() { my->stats_loop(); });
            }

            void webserver_plugin::plugin_shutdown() {
                my->chain_sync_con.disconnect();
                my->applied_block_conn.disconnect();

                my->stats_stop.store(true, std::memory_order_release);
                if (my->stats_thread.joinable()) my->stats_thread.join();

                my->flush_stats_locked(false);

                my->stop_webserver();
            }

        }
    }
} // graphene::plugins::webserver
