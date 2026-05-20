#pragma once

#include "scenario_driver.hpp"

#include <graphene/protocol/types.hpp>

#include <fc/time.hpp>

#include <optional>
#include <set>
#include <string>

namespace consensus_sim {

/// Thin facade over scenario_driver's bus + slot_producer hooks. Lets a
/// scenario declaratively express network and producer-side faults without
/// reaching into the driver internals.
///
/// Lifetime: must outlive the driver's run().
class fault_injector {
public:
    explicit fault_injector(scenario_driver& driver) : driver_(driver) {}

    // --- Network faults (forwarded to the message bus) ---

    void partition(std::set<std::string> a, std::set<std::string> b);
    void heal();
    void delay_link(const std::string& from, const std::string& to,
                    fc::microseconds extra);
    void drop_next(const std::string& from, const std::string& to);

    // --- Producer-side faults ---

    /// On the next slot whose canonical producer is `witness`, instead
    /// of producing one block, produce TWO blocks A and B for the same
    /// slot, both validly signed by `witness`, differing in their
    /// transaction set (or, if the harness can't yet construct two
    /// distinct valid blocks for the same slot, ship A only and record
    /// the gap — Task 14 surfaces the concrete behavior).
    ///
    /// Fires once and then reverts to honest production.
    void instruct_equivocation(const graphene::protocol::account_name_type& witness);

private:
    scenario_driver& driver_;
    std::optional<graphene::protocol::account_name_type> equivocate_witness_;
    bool equivocation_consumed_ = false;
};

} // namespace consensus_sim
