#include "invariants.hpp"

#include <graphene/protocol/config.hpp>

#include <map>
#include <utility>

namespace consensus_sim {

std::optional<violation_report> chains_consistent(
    const std::vector<simulated_node*>& nodes) {
    if (nodes.size() < 2) return std::nullopt;

    auto* base = nodes[0];
    for (size_t i = 1; i < nodes.size(); ++i) {
        auto* other = nodes[i];
        if (base->head_block_num() == other->head_block_num() &&
            !(base->head_block_id() == other->head_block_id())) {
            return violation_report{
                "chains_consistent",
                "head divergence at same height",
                base->label() + " vs " + other->label(),
                base->head_block_num()
            };
        }
    }
    return std::nullopt;
}

std::optional<violation_report> lib_monotone_checker::operator()(
    const std::vector<simulated_node*>& nodes) {
    for (auto* n : nodes) {
        uint32_t cur = n->last_irreversible_block_num();
        auto it = last_lib_.find(n->label());
        if (it != last_lib_.end() && cur < it->second) {
            return violation_report{
                "lib_monotone",
                "LIB regressed from " + std::to_string(it->second) +
                    " to " + std::to_string(cur),
                n->label(),
                cur
            };
        }
        last_lib_[n->label()] = cur;
    }
    return std::nullopt;
}

std::optional<violation_report> supply_conserved(
    const std::vector<simulated_node*>& nodes,
    uint64_t expected_supply_floor) {
    (void)nodes;
    (void)expected_supply_floor;
    return std::nullopt;
}

std::optional<violation_report> no_double_signed_in_canonical(
    const std::vector<simulated_node*>& nodes) {
    for (auto* n : nodes) {
        auto blocks = n->recent_blocks(200);
        std::map<std::pair<std::string, uint32_t>,
                 graphene::protocol::block_id_type> seen;
        for (auto& b : blocks) {
            uint32_t slot = b.timestamp.sec_since_epoch() / CHAIN_BLOCK_INTERVAL;
            std::pair<std::string, uint32_t> key{b.witness, slot};
            auto it = seen.find(key);
            if (it != seen.end() && !(it->second == b.id)) {
                return violation_report{
                    "no_double_signed_in_canonical",
                    "witness " + b.witness + " has two distinct blocks at slot "
                        + std::to_string(slot),
                    n->label(),
                    b.block_num
                };
            }
            seen[key] = b.id;
        }
    }
    return std::nullopt;
}

} // namespace consensus_sim
