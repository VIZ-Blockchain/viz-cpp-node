#include "scenario_driver.hpp"

#include <graphene/protocol/config.hpp>

#include <sstream>

namespace consensus_sim {

scenario_driver::scenario_driver(scenario_config cfg)
    : cfg_(std::move(cfg)),
      params_(make_genesis_params(cfg_.seed, cfg_.num_witnesses)),
      clk_(cfg_.start_time) {
    for (uint32_t i = 0; i < cfg_.num_witnesses; ++i) {
        auto label = "node-" + std::to_string(i);
        auto n = std::make_unique<simulated_node>(label, params_, clk_);
        node_ptrs_.push_back(n.get());
        owned_nodes_.push_back(std::move(n));
    }
    produce_slot_ = [this](scenario_driver&, uint32_t slot, fc::time_point_sec when) {
        default_slot_producer_(slot, when);
    };
}

scenario_driver::~scenario_driver() = default;

void scenario_driver::add_invariant(invariant_fn fn) {
    invariants_.push_back(std::move(fn));
}

const std::vector<simulated_node*>& scenario_driver::nodes() const noexcept {
    return node_ptrs_;
}

simulated_node* scenario_driver::witness_to_node_(
    const graphene::protocol::account_name_type& w) {
    for (size_t i = 0; i < params_.witness_keys.size(); ++i) {
        if (params_.witness_keys[i].first == w) return node_ptrs_[i];
    }
    return nullptr;
}

void scenario_driver::default_slot_producer_(uint32_t slot, fc::time_point_sec when) {
    // Milestone 2 producer: round-robin which node generates the block, but
    // every block is signed by the genesis witness (CHAIN_COMMITTEE_ACCOUNT
    // is the only on-chain witness with init_genesis + CHAIN_NUM_INITIATORS=0).
    // This exercises bus delivery + convergence + invariants without needing
    // multi-witness genesis. Milestone 3+ will replace this with a producer
    // that uses the per-index witness identities from params_.witness_keys
    // once register_witness_keys_ actually rotates keys via witness_update.
    uint32_t idx = slot % cfg_.num_witnesses;
    auto* producer = node_ptrs_[idx];

    auto block = producer->produce_block(params_.genesis_witness_name,
                                         params_.genesis_witness_key, when);
    auto ptr = std::make_shared<graphene::protocol::signed_block>(block);

    events_.push_back("slot=" + std::to_string(slot)
                      + " producer=" + producer->label()
                      + " block_num=" + std::to_string(block.block_num()));

    for (auto* peer : node_ptrs_) {
        if (peer == producer) continue;
        bus_.enqueue(producer->label(), peer->label(),
                     std::static_pointer_cast<void>(ptr), when);
    }
}

std::optional<violation_report> scenario_driver::run() {
    fc::time_point_sec t = cfg_.start_time;
    for (uint32_t slot = 1; slot <= cfg_.max_slots; ++slot) {
        t += static_cast<uint32_t>(CHAIN_BLOCK_INTERVAL);
        clk_.advance_to(t);

        produce_slot_(*this, slot, t);

        auto deliveries = bus_.pump_until(t);
        for (auto& d : deliveries) {
            simulated_node* dest = nullptr;
            for (auto* n : node_ptrs_) if (n->label() == d.to) { dest = n; break; }
            if (!dest) continue;
            auto block = std::static_pointer_cast<graphene::protocol::signed_block>(d.payload);
            auto outcome = dest->receive_block(*block);
            std::ostringstream oss;
            oss << "deliver " << d.from << "->" << d.to
                << " block_num=" << block->block_num()
                << " outcome=" << static_cast<int>(outcome);
            events_.push_back(oss.str());
            if (outcome == block_outcome::unexpected_exception) {
                violation_ = violation_report{
                    "block_outcome",
                    "unexpected_exception delivering block "
                        + std::to_string(block->block_num()),
                    dest->label(),
                    block->block_num()
                };
                return violation_;
            }
        }

        for (auto& inv : invariants_) {
            auto v = inv(node_ptrs_);
            if (v) { violation_ = v; return violation_; }
        }
    }
    return std::nullopt;
}

} // namespace consensus_sim
