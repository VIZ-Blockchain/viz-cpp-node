#include "message_bus.hpp"

#include <algorithm>

namespace consensus_sim {

void message_bus::enqueue(std::string from, std::string to,
                          std::shared_ptr<void> payload,
                          fc::time_point_sec deliver_at) {
    auto key = std::make_pair(from, to);
    auto it = link_delay_.find(key);
    if (it != link_delay_.end()) deliver_at += it->second;
    queue_.push_back({std::move(from), std::move(to), std::move(payload), deliver_at});
}

std::vector<delivery> message_bus::pump_until(fc::time_point_sec now) {
    std::sort(queue_.begin(), queue_.end(),
              [](const queued& a, const queued& b) { return a.at < b.at; });

    std::vector<delivery> out;
    auto end_it = std::remove_if(queue_.begin(), queue_.end(),
        [&](queued& q) {
            if (q.at > now) return false;
            if (is_blocked_(q.from, q.to)) return true;
            if (consume_drop_next_(q.from, q.to)) return true;
            out.push_back({q.from, q.to, q.payload, q.at});
            return true;
        });
    queue_.erase(end_it, queue_.end());
    return out;
}

void message_bus::partition(std::set<std::string> a, std::set<std::string> b) {
    partition_a_ = std::move(a);
    partition_b_ = std::move(b);
    partitioned_ = true;
}

void message_bus::heal() {
    partitioned_ = false;
    partition_a_.clear();
    partition_b_.clear();
}

void message_bus::drop_next(const std::string& from, const std::string& to) {
    ++drop_next_count_[{from, to}];
}

void message_bus::delay_link(const std::string& from, const std::string& to,
                             fc::microseconds extra) {
    link_delay_[{from, to}] = extra;
}

bool message_bus::is_blocked_(const std::string& from, const std::string& to) const {
    if (!partitioned_) return false;
    bool from_a = partition_a_.count(from) > 0;
    bool to_a   = partition_a_.count(to)   > 0;
    bool from_b = partition_b_.count(from) > 0;
    bool to_b   = partition_b_.count(to)   > 0;
    return (from_a && to_b) || (from_b && to_a);
}

bool message_bus::consume_drop_next_(const std::string& from, const std::string& to) {
    auto it = drop_next_count_.find({from, to});
    if (it == drop_next_count_.end() || it->second <= 0) return false;
    if (--it->second == 0) drop_next_count_.erase(it);
    return true;
}

} // namespace consensus_sim
