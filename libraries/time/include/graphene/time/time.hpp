#pragma once

#include <fc/optional.hpp>
#include <fc/signals.hpp>
#include <fc/time.hpp>

#include <string>
#include <vector>
#include <utility>

namespace graphene {
    namespace time {

        typedef fc::signal<void()> time_discontinuity_signal_type;
        extern time_discontinuity_signal_type time_discontinuity_signal;

        /** Configuration applied to the NTP service at startup. */
        struct ntp_config {
            /** Custom server list as "host" or "host:port" strings.
             *  Leave empty to use the built-in defaults. */
            std::vector<std::string> servers;

            /** How often to request a time update, in seconds (default: 900 = 15 min). */
            uint32_t request_interval_sec = 900;

            /** Retry interval when NTP has not replied, in seconds (default: 300 = 5 min). */
            uint32_t retry_interval_sec = 300;

            /** Round-trip delay threshold in milliseconds; slower replies are discarded (default: 150). */
            uint32_t round_trip_threshold_ms = 150;

            /** Moving-average history window size (default: 5). */
            uint32_t history_size = 5;

            /** Rejection threshold as a percentage of |moving_avg| (default: 50). */
            uint32_t rejection_threshold_pct = 50;

            /** Minimum rejection threshold in milliseconds (default: 5). */
            uint32_t rejection_min_threshold_ms = 5;
        };

        /** Store NTP configuration to be applied the next time the service is initialized. */
        void configure_ntp(const ntp_config& config);

        fc::optional<fc::time_point> ntp_time();

        fc::time_point now();

        fc::time_point nonblocking_now(); // identical to now() but guaranteed not to block
        void update_ntp_time();

        fc::microseconds ntp_error();

        void shutdown_ntp_time();

        void start_simulated_time(const fc::time_point sim_time);

        void advance_simulated_time_to(const fc::time_point sim_time);

        void advance_time(int32_t delta_seconds);

    }
} // graphene::time
