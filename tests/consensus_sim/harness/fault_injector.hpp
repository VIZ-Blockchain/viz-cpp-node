#pragma once

#include "scenario_driver.hpp"
#include "simulated_node.hpp"

#include <graphene/protocol/types.hpp>

#include <fc/time.hpp>

#include <memory>
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
    ~fault_injector();

    // --- Network faults (forwarded to the message bus) ---

    void partition(std::set<std::string> a, std::set<std::string> b);
    void heal();
    void delay_link(const std::string& from, const std::string& to,
                    fc::microseconds extra);
    void drop_next(const std::string& from, const std::string& to);

    // --- Producer-side faults ---

    /// On the next slot whose canonical producer is `witness`, produce
    /// TWO blocks A and B for the same (witness, slot), both validly
    /// signed by `witness` but differing in their transaction set.
    ///
    /// Implementation: a fresh shadow simulated_node is caught up to
    /// the canonical chain at height N-1 (where N is the equivocation
    /// slot's height), then a no-op transaction is pushed into the
    /// shadow's pending pool to force its block_b's merkle root to
    /// differ from prod's block_a. The shadow is then asked to produce
    /// a block at the same `when` and witness. Both blocks are signed
    /// by params.genesis_witness_key (the only on-chain signing key
    /// with single-witness genesis).
    ///
    /// Delivery: the bus is partitioned into {prod} vs {everyone else}
    /// for this slot. block_a is broadcast from prod's label (reaches
    /// nobody — the partition leaves prod alone). block_b is broadcast
    /// from the first other-side node's label (reaches all of side B).
    /// The partition is NOT healed — chains_consistent will detect the
    /// divergence at the next invariant check.
    ///
    /// Fires once and then reverts to honest production.
    void instruct_equivocation(const graphene::protocol::account_name_type& witness);

private:
    scenario_driver& driver_;
    std::optional<graphene::protocol::account_name_type> equivocate_witness_;
    bool equivocation_consumed_ = false;
    std::unique_ptr<simulated_node> shadow_;
};

} // namespace consensus_sim
