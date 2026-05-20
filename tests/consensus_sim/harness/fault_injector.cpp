#include "fault_injector.hpp"

#include <graphene/protocol/config.hpp>

#include <memory>

namespace consensus_sim {

void fault_injector::partition(std::set<std::string> a, std::set<std::string> b) {
    driver_.bus().partition(std::move(a), std::move(b));
}

void fault_injector::heal() { driver_.bus().heal(); }

void fault_injector::delay_link(const std::string& from, const std::string& to,
                                fc::microseconds extra) {
    driver_.bus().delay_link(from, to, extra);
}

void fault_injector::drop_next(const std::string& from, const std::string& to) {
    driver_.bus().drop_next(from, to);
}

void fault_injector::instruct_equivocation(
    const graphene::protocol::account_name_type& witness) {
    equivocate_witness_ = witness;
    equivocation_consumed_ = false;

    driver_.set_slot_producer(
        [this](scenario_driver& d, uint32_t slot, fc::time_point_sec when) {
            const auto& params = d.params();
            const auto& gw_name = params.genesis_witness_name;
            const auto& gw_key = params.genesis_witness_key;

            uint32_t idx = slot % d.config().num_witnesses;
            auto* prod = d.nodes()[idx];

            const bool fire =
                equivocate_witness_ && gw_name == *equivocate_witness_ &&
                !equivocation_consumed_;

            // Honest path (also: every slot before/after the equivocation
            // slot). Matches the default slot producer in scenario_driver.
            if (!fire) {
                auto block = prod->produce_block(gw_name, gw_key, when);
                auto ptr = std::make_shared<graphene::protocol::signed_block>(block);
                for (auto* peer : d.nodes()) {
                    if (peer == prod) continue;
                    d.bus().enqueue(prod->label(), peer->label(),
                                    std::static_pointer_cast<void>(ptr), when);
                }
                return;
            }

            // Equivocation slot: produce two blocks for the same witness/slot.
            //
            // Approach: build a "shadow" simulated_node that catches up to
            // prod's height via receive_block, then call produce_block on
            // both prod and shadow at `when`. With identical genesis, identical
            // witness/key, and the same head, the two blocks differ only in
            // contents the shadow controls independently (transaction pool,
            // future state).
            //
            // GAP (filled by Task 14 once the concrete failure mode is known):
            // catching the shadow up requires full signed_block bodies, but
            // simulated_node::recent_blocks() returns only block metadata.
            // Two viable extensions when needed:
            //   1. Extend recent_blocks() (or add canonical_chain_blocks())
            //      to return full signed_block objects.
            //   2. Take the produced block_a, mutate transaction_merkle_root
            //      or include/exclude one synthetic tx, and re-sign with
            //      gw_key.
            //
            // Until then this path produces and broadcasts block_a only,
            // matching the honest path, and marks the gap.
            auto block_a = prod->produce_block(gw_name, gw_key, when);
            auto ptr_a = std::make_shared<graphene::protocol::signed_block>(block_a);
            for (auto* peer : d.nodes()) {
                if (peer == prod) continue;
                d.bus().enqueue(prod->label(), peer->label(),
                                std::static_pointer_cast<void>(ptr_a), when);
            }

            equivocation_consumed_ = true;
        });
}

} // namespace consensus_sim
