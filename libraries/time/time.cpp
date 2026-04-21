#include <graphene/time/time.hpp>

#include <fc/exception/exception.hpp>
#include <fc/log/logger.hpp>
#include <fc/network/ntp.hpp>
#include <fc/thread/mutex.hpp>
#include <fc/thread/scoped_lock.hpp>

#include <atomic>
#include <sstream>

namespace graphene {
    namespace time {

        static int32_t simulated_time = 0;
        static int32_t adjusted_time_sec = 0;

        time_discontinuity_signal_type time_discontinuity_signal;

        namespace detail {
            std::atomic<fc::ntp *> ntp_service(nullptr);
            fc::mutex ntp_service_initialization_mutex;

            // NTP configuration stored by configure_ntp(), applied when the service is first created.
            ntp_config pending_ntp_config;
            fc::mutex ntp_config_mutex;

            /** Apply all non-default fields from cfg to an already-created fc::ntp instance. */
            static void apply_ntp_config(fc::ntp* svc, const ntp_config& cfg) {
                // Servers: only replace defaults when the user provided at least one entry.
                if (!cfg.servers.empty()) {
                    std::vector<std::pair<std::string, uint16_t>> parsed;
                    for (const auto& s : cfg.servers) {
                        auto colon = s.rfind(':');
                        if (colon != std::string::npos) {
                            std::string host = s.substr(0, colon);
                            uint16_t port = uint16_t(123);
                            try {
                                port = static_cast<uint16_t>(std::stoul(s.substr(colon + 1)));
                            } catch (const std::exception& ex) {
                                wlog("NTP: invalid port in server entry '${s}', using 123: ${e}",
                                     ("s", s)("e", ex.what()));
                            }
                            parsed.emplace_back(host, port);
                        } else {
                            parsed.emplace_back(s, uint16_t(123));
                        }
                    }
                    svc->set_servers(parsed);
                }
                svc->set_request_interval(cfg.request_interval_sec);
                svc->set_retry_interval(cfg.retry_interval_sec);
                svc->set_round_trip_threshold_ms(cfg.round_trip_threshold_ms);
                svc->set_delta_history_size(cfg.history_size);
                svc->set_rejection_threshold_pct(cfg.rejection_threshold_pct);
                svc->set_rejection_min_threshold_ms(cfg.rejection_min_threshold_ms);
            }
        }

        void configure_ntp(const ntp_config& config) {
            fc::scoped_lock<fc::mutex> lock(detail::ntp_config_mutex);
            detail::pending_ntp_config = config;
        }

        fc::optional<fc::time_point> ntp_time() {
            fc::ntp *actual_ntp_service = detail::ntp_service.load();
            if (!actual_ntp_service) {
                fc::scoped_lock<fc::mutex> lock(detail::ntp_service_initialization_mutex);
                actual_ntp_service = detail::ntp_service.load();
                if (!actual_ntp_service) {
                    actual_ntp_service = new fc::ntp;
                    // Apply any previously stored configuration.
                    {
                        fc::scoped_lock<fc::mutex> cfg_lock(detail::ntp_config_mutex);
                        detail::apply_ntp_config(actual_ntp_service, detail::pending_ntp_config);
                    }
                    detail::ntp_service.store(actual_ntp_service);
                }
            }
            return actual_ntp_service->get_time();
        }

        void shutdown_ntp_time() {
            fc::ntp *actual_ntp_service = detail::ntp_service.exchange(nullptr);
            delete actual_ntp_service;
        }

        fc::time_point now() {
            if (simulated_time) {
                return fc::time_point() +
                       fc::seconds(simulated_time + adjusted_time_sec);
            }

            fc::optional<fc::time_point> current_ntp_time = ntp_time();
            if (current_ntp_time.valid()) {
                return *current_ntp_time + fc::seconds(adjusted_time_sec);
            } else {
                return fc::time_point::now() + fc::seconds(adjusted_time_sec);
            }
        }

        fc::time_point nonblocking_now() {
            if (simulated_time) {
                return fc::time_point() +
                       fc::seconds(simulated_time + adjusted_time_sec);
            }

            fc::ntp *actual_ntp_service = detail::ntp_service.load();
            fc::optional<fc::time_point> current_ntp_time;
            if (actual_ntp_service) {
                current_ntp_time = actual_ntp_service->get_time();
            }

            if (current_ntp_time) {
                return *current_ntp_time + fc::seconds(adjusted_time_sec);
            } else {
                return fc::time_point::now() + fc::seconds(adjusted_time_sec);
            }
        }

        void update_ntp_time() {
            detail::ntp_service.load()->request_now();
        }

        fc::microseconds ntp_error() {
            fc::optional<fc::time_point> current_ntp_time = ntp_time();
            FC_ASSERT(current_ntp_time, "We don't have NTP time!");
            return *current_ntp_time - fc::time_point::now();
        }

        void start_simulated_time(const fc::time_point sim_time) {
            simulated_time = sim_time.sec_since_epoch();
            adjusted_time_sec = 0;
        }

        void advance_simulated_time_to(const fc::time_point sim_time) {
            simulated_time = sim_time.sec_since_epoch();
            adjusted_time_sec = 0;
        }

        void advance_time(int32_t delta_seconds) {
            adjusted_time_sec += delta_seconds;
            time_discontinuity_signal();
        }

    }
} // graphene::time
