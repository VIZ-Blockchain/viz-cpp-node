#pragma once

#include <cstdint>
#include <iterator>
#include <list>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace graphene { namespace plugins { namespace webserver { namespace detail {

struct ip_counters {
    uint64_t total = 0, minute = 0, hour = 0, day = 0;
    uint64_t blocked = 0, limited = 0;
    uint32_t minute_bucket = 0, hour_bucket = 0, day_bucket = 0, last_seen = 0;
    uint64_t logged = 0;
    std::list<std::string>::iterator lru_position;
};

// Caller holds stats_mutex. A new source always gets a row: reaching the memory
// bound must not deny every new client until a wall-clock GC deadline expires.
class ip_counter_cache {
    using rows_type = std::unordered_map<std::string, ip_counters>;
public:
    explicit ip_counter_cache(std::size_t capacity = 200000) : capacity_(capacity) {
        if (!capacity_) throw std::invalid_argument("IP counter capacity must be positive");
    }
    ip_counter_cache(const ip_counter_cache &) = delete;
    ip_counter_cache &operator=(const ip_counter_cache &) = delete;

    ip_counters &touch(const std::string &ip) {
        auto it = rows_.find(ip);
        if (it != rows_.end()) {
            order_.splice(order_.end(), order_, it->second.lru_position);
            return it->second;
        }
        if (rows_.size() >= capacity_) {
            rows_.erase(order_.front());
            order_.pop_front();
        }
        order_.push_back(ip);
        try {
            auto inserted = rows_.emplace(ip, ip_counters{});
            inserted.first->second.lru_position = std::prev(order_.end());
            return inserted.first->second;
        } catch (...) {
            order_.pop_back();
            throw;
        }
    }
    rows_type::iterator find(const std::string &ip) { return rows_.find(ip); }
    rows_type::iterator begin() { return rows_.begin(); }
    rows_type::iterator end() { return rows_.end(); }
    rows_type::const_iterator begin() const { return rows_.begin(); }
    rows_type::const_iterator end() const { return rows_.end(); }
    std::size_t size() const { return rows_.size(); }
    void clear() { rows_.clear(); order_.clear(); }

private:
    std::size_t capacity_;
    std::list<std::string> order_; // oldest request first; handles equal timestamps deterministically
    rows_type rows_;
};

}}}}
