#pragma once

#include <fc/time.hpp>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace consensus_sim {

struct delivery {
    std::string from;
    std::string to;
    std::shared_ptr<void> payload;
    fc::time_point_sec at;
};

/// Opaque in-process message bus for the consensus harness. Carries
/// std::shared_ptr<void> payloads without introspecting them, applies
/// partition / drop_next / per-link delay rules on pump.
class message_bus {
public:
    void enqueue(std::string from, std::string to,
                 std::shared_ptr<void> payload,
                 fc::time_point_sec deliver_at);

    /// Returns deliveries due at or before `now`, sorted by scheduled
    /// delivery time. Removes them from the queue. Applies the active
    /// partition + drop_next rules.
    std::vector<delivery> pump_until(fc::time_point_sec now);

    void partition(std::set<std::string> side_a, std::set<std::string> side_b);
    void heal();
    void drop_next(const std::string& from, const std::string& to);

    /// Optional: per-link delay added on top of the deliver_at the caller
    /// passed to enqueue. Stored in microseconds; rounded down to seconds
    /// because fc::time_point_sec has 1-second granularity.
    void delay_link(const std::string& from, const std::string& to, fc::microseconds extra);

private:
    bool is_blocked_(const std::string& from, const std::string& to) const;
    bool consume_drop_next_(const std::string& from, const std::string& to);

    struct queued {
        std::string from;
        std::string to;
        std::shared_ptr<void> payload;
        fc::time_point_sec at;
    };

    std::vector<queued> queue_;
    std::set<std::string> partition_a_;
    std::set<std::string> partition_b_;
    bool partitioned_ = false;
    std::map<std::pair<std::string, std::string>, int> drop_next_count_;
    std::map<std::pair<std::string, std::string>, fc::microseconds> link_delay_;
};

} // namespace consensus_sim
