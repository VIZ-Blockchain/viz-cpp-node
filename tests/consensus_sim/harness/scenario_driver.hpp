#pragma once

#include "genesis_factory.hpp"
#include "invariants.hpp"
#include "message_bus.hpp"
#include "simulated_node.hpp"
#include "virtual_clock.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace consensus_sim {

struct scenario_config {
    uint64_t seed = 0;
    uint32_t num_witnesses = 7;
    /// Defaults to epoch (utc_seconds=0); scenarios should set an explicit
    /// time. The plan's literal-string default doesn't compile —
    /// fc::time_point_sec's ctor takes uint32_t.
    fc::time_point_sec start_time = fc::time_point_sec();
    uint32_t max_slots = 100;
    fc::microseconds default_link_delay = fc::microseconds(0);
};

using invariant_fn = std::function<
    std::optional<violation_report>(const std::vector<simulated_node*>&)>;

class scenario_driver {
public:
    explicit scenario_driver(scenario_config cfg);
    ~scenario_driver();

    scenario_driver(const scenario_driver&) = delete;
    scenario_driver& operator=(const scenario_driver&) = delete;

    void add_invariant(invariant_fn fn);

    /// Runs the scheduled slot count. Returns nullopt on full success;
    /// the first violation otherwise. On violation the driver stops; the
    /// violation can be retrieved via violation() alongside the event log.
    std::optional<violation_report> run();

    const std::vector<simulated_node*>& nodes() const noexcept;
    message_bus& bus() noexcept { return bus_; }
    virtual_clock& clock() noexcept { return clk_; }
    const std::vector<std::string>& event_log() const noexcept { return events_; }
    const std::optional<violation_report>& violation() const noexcept { return violation_; }
    const scenario_config& config() const noexcept { return cfg_; }
    const genesis_params& params() const noexcept { return params_; }

    /// Per-slot producer hook. Default round-robins through
    /// params.witness_keys; fault_injector overrides this to inject
    /// equivocation in a later task.
    using slot_producer_fn = std::function<
        void(scenario_driver&, uint32_t slot, fc::time_point_sec when)>;
    void set_slot_producer(slot_producer_fn fn) { produce_slot_ = std::move(fn); }

private:
    void default_slot_producer_(uint32_t slot, fc::time_point_sec when);
    simulated_node* witness_to_node_(const graphene::protocol::account_name_type& w);

    scenario_config cfg_;
    genesis_params params_;
    virtual_clock clk_;
    message_bus bus_;
    std::vector<std::unique_ptr<simulated_node>> owned_nodes_;
    std::vector<simulated_node*> node_ptrs_;
    std::vector<invariant_fn> invariants_;
    std::vector<std::string> events_;
    std::optional<violation_report> violation_;
    slot_producer_fn produce_slot_;
};

} // namespace consensus_sim
