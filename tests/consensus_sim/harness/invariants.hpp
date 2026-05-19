#pragma once

#include "simulated_node.hpp"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace consensus_sim {

struct violation_report {
    std::string invariant_name;
    std::string detail;
    std::string node_label;   // empty if cross-node
    uint32_t block_num = 0;
};

/// All canonical chains agree on every block they share — one is a prefix
/// of, or equal to, the other. Modulo blocks still in flight, this should
/// hold every tick after pump. Milestone 2 coarse-grains the check to
/// "heads at the same num must have the same id"; finer-grained shared-
/// prefix comparison is deferred until simulated_node exposes a block
/// enumerator.
std::optional<violation_report> chains_consistent(
    const std::vector<simulated_node*>& nodes);

/// LIB never decreases on any node.
class lib_monotone_checker {
public:
    std::optional<violation_report> operator()(
        const std::vector<simulated_node*>& nodes);
private:
    std::map<std::string, uint32_t> last_lib_;
};

/// Total VIZ supply matches genesis (no asset created or destroyed
/// outside the chain's own issuance/burn rules). Stubbed until a
/// scenario needs it; floor check only when implemented.
std::optional<violation_report> supply_conserved(
    const std::vector<simulated_node*>& nodes,
    uint64_t expected_supply_floor);

/// No two distinct blocks with the same (witness, slot) appear in any
/// node's canonical chain. Filled in by Task 13 (equivocation), when the
/// simulated_node block-enumeration helper is in place.
std::optional<violation_report> no_double_signed_in_canonical(
    const std::vector<simulated_node*>& nodes);

} // namespace consensus_sim
