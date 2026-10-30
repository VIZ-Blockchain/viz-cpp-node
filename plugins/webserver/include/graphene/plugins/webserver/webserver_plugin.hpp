#pragma once

#include <appbase/application.hpp>

#include <graphene/plugins/json_rpc/plugin.hpp>

#include <boost/thread.hpp>
#include <boost/container/vector.hpp>

#include <cstdint>
#include <string>
#include <vector>


#define WEBSERVER_PLUGIN_NAME "webserver"

namespace graphene {
    namespace plugins {
        namespace webserver {


            /// Per-IP request accounting, filled only while `webserver-stats-enabled` is on.
            struct webserver_ip_stats {
                std::string ip;
                uint64_t    total = 0;
                uint64_t    minute = 0;     // requests in the current minute window
                uint64_t    hour = 0;
                uint64_t    day = 0;
                uint64_t    blocked = 0;    // rejected by the blacklist
                uint64_t    limited = 0;    // rejected by a rate limit
                uint32_t    last_seen = 0;  // unixtime
                bool        whitelisted = false;
                bool        blacklisted = false;
            };

            /// Admission limits. `enabled` follows `webserver-stats-enabled` at startup and
            /// can be changed at runtime through node_admin.
            struct webserver_limits {
                bool     enabled = false;
                uint32_t per_minute = 1000;
                uint32_t per_hour = 10000;
                uint32_t per_day = 100000;
            };

            /// Everything an operator (or an MCP tool through node_admin) can read at once.
            struct webserver_admin_state {
                bool             stats_enabled = false;
                webserver_limits limits;
                uint64_t         total_requests = 0;
                uint64_t         blocked_requests = 0;
                uint64_t         limited_requests = 0;
                size_t           tracked_ips = 0;
                std::vector<std::string> whitelist;
                std::vector<std::string> blacklist;
                std::vector<webserver_ip_stats> ips;
            };

            /**
              * This plugin starts an HTTP/ws webserver and dispatches queries to
              * registered handles based on payload. The payload must be conform
              * to the JSONRPC 2.0 spec.
              *
              * The handler will be called from the appbase application io_service
              * thread.  The callback can be called from any thread and will
              * automatically propagate the call to the http thread.
              *
              * The HTTP service will run in its own thread with its own io_service to
              * make sure that HTTP request processing does not interfer with other
              * plugins.
              */
            class webserver_plugin final : public appbase::plugin<webserver_plugin> {
            public:
                webserver_plugin();

                ~webserver_plugin();

                APPBASE_PLUGIN_REQUIRES((json_rpc::plugin));

                static const std::string &name() {
                    static std::string name = WEBSERVER_PLUGIN_NAME;
                    return name;
                }

                void set_program_options(boost::program_options::options_description &, boost::program_options::options_description &cfg) override;

                /// Live state: limits, IP lists and per-IP counters. Safe from any thread.
                /// `max_ips == 0` skips the per-IP rows (summary only); otherwise at most
                /// `max_ips` rows are returned, highest request count first.
                webserver_admin_state admin_state(size_t max_ips = 200) const;

                /// Turn stats logging on or off at runtime. Enabled limits keep counting
                /// independently. Persisted to the admin state file.
                void set_stats_enabled(bool enabled);

                /// Replace limits. `enabled=false` keeps counting (if stats are on) but stops
                /// rejecting requests. Every value takes effect on the next request.
                bool set_limits(bool enabled, uint32_t per_minute, uint32_t per_hour,
                                uint32_t per_day, std::string &error);

                bool whitelist_add(const std::string &ip);
                bool whitelist_remove(const std::string &ip);
                bool blacklist_add(const std::string &ip);
                bool blacklist_remove(const std::string &ip);

                /// Zero the counters; the IP lists and limits are kept.
                void reset_stats();

                /// Re-read the admin state file (limits and lists) written by earlier
                /// changes. Returns false and fills `error` if the file is unreadable.
                bool reload_admin_state(std::string &error);

            protected:
                void plugin_initialize(const boost::program_options::variables_map &options) override;

                void plugin_startup() override;

                void plugin_shutdown() override;

            private:
                struct webserver_plugin_impl;
                std::unique_ptr<webserver_plugin_impl> my;
            };

        }
    }
} // graphene::plugins::webserver
