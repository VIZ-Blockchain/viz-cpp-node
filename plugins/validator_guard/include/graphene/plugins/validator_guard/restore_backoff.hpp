/*
 * Copyright (c) 2026 VIZ contributors
 *
 * Futility tracking for validator_guard's key restoration.
 *
 * In emergency consensus the chain blanks the signing key of a validator whose
 * `head - last_confirmed_block_num` exceeds CHAIN_EMERGENCY_MAX_VALIDATOR_MISSED_BLOCKS
 * (database.cpp, the emergency offline-validator branch). For a validator this node does not
 * produce blocks for, last_confirmed never advances, so every restoration is discarded on the
 * next block and the guard would broadcast a validator_update every round forever.
 *
 * The decision is pure state -> verdict, kept here so it can be driven by tests: this node
 * signs with a hot key, so a wrong "keep restoring" is a stream of real transactions, and a
 * wrong "stop restoring" leaves a validator that a human has to re-key by hand.
 */
#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace graphene {
namespace plugins {
namespace validator_guard {

class restore_backoff {
public:
    struct decision {
        /// Attempt the broadcast for this validator now.
        bool restore = true;
        /// Emit the futility warning (once, at the moment the hold is set).
        bool warn = false;
        /// Futile cycles counted so far, after this decision.
        unsigned cycles = 0;
        /// Seconds between the confirmed restoration and the null key that counted as a cycle.
        int64_t age = 0;
        /// Auto-restore is paused until this time (0 = not paused).
        int64_t held_until = 0;
    };

    /**
     * The key of @p name is null on chain right now.
     *
     * A null key within @p window_sec of a confirmed restoration is not a race: the chain
     * blanked it, so the cycle is counted; the second one sets a hold of @p hold_sec.
     */
    decision on_null_key(const std::string& name, int64_t now, int64_t window_sec,
                         int64_t hold_sec) {
        record& r = _records[name];

        if (r.hold_until > now) {
            decision d;
            d.restore = false;
            d.cycles = r.cycles;
            d.held_until = r.hold_until;
            return d;
        }

        if (r.confirmed_at >= 0) {
            const int64_t age = now - r.confirmed_at;
            if (age <= window_sec) {
                r.confirmed_at = -1;  // this restoration was blanked; it is not a candidate again
                ++r.cycles;
                if (r.cycles >= 2) {
                    decision d;
                    d.restore = false;
                    d.warn = true;
                    d.cycles = r.cycles;
                    d.age = age;
                    r.cycles = 0;
                    r.hold_until = now + hold_sec;
                    d.held_until = r.hold_until;
                    return d;
                }
                decision d;
                d.cycles = r.cycles;
                d.age = age;
                return d;
            }
            // The restoration held past the window: the key going null now is a new event,
            // not evidence that restoring is futile.
            r.confirmed_at = -1;
            r.cycles = 0;
        }

        decision d;
        d.cycles = r.cycles;
        return d;
    }

    /// A restoration for @p name was confirmed in a block at @p now.
    void on_restore_confirmed(const std::string& name, int64_t now) {
        _records[name].confirmed_at = now;
    }

    /**
     * The key of @p name is present on chain.
     *
     * This is the normal state between a restoration and the next blanking, so the cycle
     * counter is kept: dropping it here would reset the count on every attempt. Only an
     * operator's action (or a hold that elapsed) ends the episode, hence the hold is cleared.
     */
    void on_key_present(const std::string& name) {
        auto it = _records.find(name);
        if (it != _records.end()) {
            it->second.hold_until = 0;
        }
    }

    bool is_held(const std::string& name, int64_t now) const {
        auto it = _records.find(name);
        return it != _records.end() && it->second.hold_until > now;
    }

    unsigned cycles(const std::string& name) const {
        auto it = _records.find(name);
        return it == _records.end() ? 0u : it->second.cycles;
    }

private:
    struct record {
        int64_t confirmed_at = -1;
        unsigned cycles = 0;
        int64_t hold_until = 0;
    };
    std::map<std::string, record> _records;
};

}  // namespace validator_guard
}  // namespace plugins
}  // namespace graphene
