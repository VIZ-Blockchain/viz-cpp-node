#pragma once

#include <fc/crypto/sha256.hpp>
#include <fc/time.hpp>
#include <list>
#include <map>
#include <utility>

namespace graphene { namespace network {

// Relay deduplication only; chain validation still decides whether a vote counts.
// Keep insertion order so expiry never scans every retained hash per packet.
class dlt_vote_cache {
    using entry = std::pair<fc::sha256, fc::time_point>;
    std::list<entry> _order;
    std::map<fc::sha256, std::list<entry>::iterator> _seen;
    static constexpr size_t capacity = 8192;

    void erase_oldest() {
        _seen.erase(_order.front().first);
        _order.pop_front();
    }

public:
    bool contains(const fc::sha256& key, fc::time_point now) {
        // Bound even the first packet after a long idle interval.
        for (size_t n = 0; n < 64 && !_order.empty(); ++n) {
            if (!(now - _order.front().second > fc::seconds(120))) break;
            erase_oldest();
        }
        auto it = _seen.find(key);
        if (it == _seen.end()) return false;
        if (now - it->second->second > fc::seconds(120)) {
            _order.erase(it->second);
            _seen.erase(it);
            return false;
        }
        return true;
    }

    void insert(const fc::sha256& key, fc::time_point now) {
        // A local resend must not renew retention indefinitely.
        if (_seen.count(key)) return;
        if (_seen.size() >= capacity) erase_oldest();
        _order.emplace_back(key, now);
        _seen.emplace(key, std::prev(_order.end()));
    }

    size_t size() const { return _seen.size(); }
};

} } // graphene::network
