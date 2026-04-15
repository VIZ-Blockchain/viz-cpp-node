#include <graphene/plugins/webserver/webserver_plugin.hpp>

#include <graphene/plugins/chain/plugin.hpp>
#include <graphene/protocol/block.hpp>

#include <fc/network/ip.hpp>
#include <fc/log/logger_config.hpp>
#include <fc/io/json.hpp>
#include <fc/network/resolve.hpp>
#include <fc/crypto/sha256.hpp>

#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <boost/bind.hpp>
#include <boost/preprocessor/stringize.hpp>

#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/logger/stub.hpp>
#include <websocketpp/logger/syslog.hpp>

#include <thread>
#include <memory>
#include <iostream>
#include <map>
#include <unordered_map>
#include <mutex>
#include <graphene/plugins/json_rpc/plugin.hpp>

namespace graphene {
    namespace plugins {
        namespace webserver {

            namespace asio = boost::asio;

            using std::map;
            using std::string;
            using boost::optional;
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

            struct webserver_plugin::webserver_plugin_impl final {
            public:
                boost::thread_group& thread_pool = appbase::app().scheduler();
                webserver_plugin_impl(thread_pool_size_t thread_pool_size) : thread_pool_work(this->thread_pool_ios) {
                    for (uint32_t i = 0; i < thread_pool_size; ++i) {
                        thread_pool.create_thread(boost::bind(&asio::io_service::run, &thread_pool_ios));
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
                asio::io_service http_ios;
                optional<tcp::endpoint> http_endpoint;
                websocket_server_type http_server;

                shared_ptr<std::thread> ws_thread;
                asio::io_service ws_ios;
                optional<tcp::endpoint> ws_endpoint;
                websocket_server_type ws_server;
                asio::io_service thread_pool_ios;
                asio::io_service::work thread_pool_work;

                plugins::json_rpc::plugin *api;
                boost::signals2::connection chain_sync_con;
                boost::signals2::connection applied_block_conn;

                // Cache for JSON-RPC responses
                std::unordered_map<std::string, cache_entry> response_cache;
                std::mutex cache_mutex;
                uint32_t current_block_num = 0;
                bool cache_enabled = true;
                size_t max_cache_size = 10000;
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

                            ws_server.set_message_handler(boost::bind(&webserver_plugin_impl::handle_ws_message, this, &ws_server, _1, _2));

                            if (http_endpoint && http_endpoint == ws_endpoint) {
                                ws_server.set_http_handler(boost::bind(&webserver_plugin_impl::handle_http_message, this, &ws_server, _1));
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

            void webserver_plugin::webserver_plugin_impl::handle_ws_message(
                websocket_server_type *server,
                connection_hdl hdl,
                websocket_server_type::message_ptr msg
            ) {
                auto con = server->get_con_from_hdl(hdl);
                thread_pool_ios.post([con, msg, this]() {
                    try {
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
                con->defer_http_response();

                thread_pool_ios.post([con, this]() {
                    auto body = con->get_request_body();

                    // Parse JSON once for all cache operations
                    fc::variant parsed;
                    try {
                        parsed = fc::json::from_string(body);
                    } catch (...) {
                        // Invalid JSON — skip cache, let json_rpc handle the error
                        try {
                            api->call(body, [con](const std::string &data){
                                con->set_body(data);
                                con->set_status(websocketpp::http::status_code::ok);
                                con->send_http_response();
                            });
                        } catch (fc::exception &e) {
                            edump((e));
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
                        "Maximum number of cached JSON-RPC responses. Default: 10000.");
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

                // Process rpc-endpoint FIRST as a fallback value.
                // This ensures that when the config file has both rpc-endpoint and
                // webserver-http-endpoint/webserver-ws-endpoint, the specific endpoints
                // can override the rpc-endpoint, but rpc-endpoint is not silently ignored.
                if (options.count("rpc-endpoint")) {
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

                if (options.count("webserver-http-endpoint")) {
                    auto http_endpoint = options.at("webserver-http-endpoint").as<string>();
                    auto endpoints = appbase::app().resolve_string_to_ip_endpoints(http_endpoint);
                    FC_ASSERT(endpoints.size(), "webserver-http-endpoint ${hostname} did not resolve",
                              ("hostname", http_endpoint));
                    my->http_endpoint = endpoints[0];
                    auto tcp_endpoint = endpoints[0];
                    auto ip_port = tcp_endpoint.address().to_string() + ":" + std::to_string(tcp_endpoint.port());
                    ilog("configured http to listen on ${ep}", ("ep", ip_port));
                }

                if (options.count("webserver-ws-endpoint")) {
                    auto ws_endpoint = options.at("webserver-ws-endpoint").as<string>();
                    auto endpoints = appbase::app().resolve_string_to_ip_endpoints(ws_endpoint);
                    FC_ASSERT(endpoints.size(), "ws-server-endpoint ${hostname} did not resolve",
                              ("hostname", ws_endpoint));
                    my->ws_endpoint = endpoints[0];
                    auto tcp_endpoint = endpoints[0];
                    auto ip_port = tcp_endpoint.address().to_string() + ":" + std::to_string(tcp_endpoint.port());
                    ilog("configured ws to listen on ${ep}", ("ep", ip_port));
                }
            }

            void webserver_plugin::plugin_startup() {
                my->api = appbase::app().find_plugin<plugins::json_rpc::plugin>();
                FC_ASSERT(my->api != nullptr, "Could not find API Register Plugin");

                chain::plugin *chain = appbase::app().find_plugin<chain::plugin>();
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
            }

            void webserver_plugin::plugin_shutdown() {
                my->stop_webserver();
            }

        }
    }
} // graphene::plugins::webserver
