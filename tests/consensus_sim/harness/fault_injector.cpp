#include "fault_injector.hpp"

#include "tx_factory.hpp"

#include <graphene/protocol/config.hpp>

#include <memory>
#include <set>
#include <string>

namespace consensus_sim {

fault_injector::~fault_injector() = default;

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

            // slot >= 2 guard: the shadow needs to set ref_block on its no-op
            // tx to the canonical block at height N-1. At slot 1 there's no
            // such block (head==0 / default block_id_type), so we defer the
            // equivocation to the first matching slot at height >= 2.
            const bool fire =
                equivocate_witness_ && gw_name == *equivocate_witness_ &&
                !equivocation_consumed_ && slot >= 2;

            // Honest path: prod produces and the bus delivers to all peers.
            // Matches the default slot producer's behavior in scenario_driver.
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

            // Equivocation slot. We produce two distinct, validly-signed blocks
            // for the same (witness, slot) and arrange asymmetric delivery so
            // the network splits into two disagreeing partitions.
            //
            // (1) prod produces block_a (empty pool -> empty block at height N).
            auto block_a = prod->produce_block(gw_name, gw_key, when);

            // (2) Build a shadow node freshly and catch it up to canonical state
            //     at height N-1 (one block behind prod, which is now at N).
            //     The canonical chain at heights 1..N-1 is whatever prod just
            //     extended from — we pull it from prod and replay it into the
            //     shadow, dropping the final block (block_a itself).
            shadow_ = std::make_unique<simulated_node>("shadow", params, d.clock());
            auto history = prod->canonical_blocks_from(1);
            if (!history.empty()) history.pop_back();  // drop block_a
            for (auto& b : history) {
                shadow_->receive_block(b);
            }

            // (3) Force divergence: push a no-op tx into the shadow's pool.
            //     The shadow's next block_b will include this tx, giving it
            //     a different transaction_merkle_root from block_a (which was
            //     produced from an empty pool). Payload is keyed on slot so
            //     the same scenario seed produces the same tx id determ.
            auto noop = make_noop_metadata_tx(
                params,
                shadow_->head_block_id(),
                shadow_->head_block_time(),
                shadow_->chain_id(),
                std::string("{\"shadow\":") + std::to_string(slot) + "}");
            shadow_->push_pending_transaction(noop);

            // (4) Shadow produces block_b at the same `when` and witness.
            auto block_b = shadow_->produce_block(gw_name, gw_key, when);

            // (5) Partition the network: {prod} vs {everyone else}, no heal.
            //     block_a is broadcast from prod -> cross-partition -> blocked.
            //     block_b is broadcast from a side-B carrier -> same-side ->
            //     delivered to all of side B. Result: prod stays at block_a,
            //     all other nodes accept block_b. chains_consistent fires.
            std::set<std::string> side_a{prod->label()};
            std::set<std::string> side_b;
            std::string carrier;
            for (auto* n : d.nodes()) {
                if (n == prod) continue;
                side_b.insert(n->label());
                if (carrier.empty()) carrier = n->label();
            }
            d.bus().partition(side_a, side_b);

            auto ptr_a = std::make_shared<graphene::protocol::signed_block>(block_a);
            auto ptr_b = std::make_shared<graphene::protocol::signed_block>(block_b);
            for (auto* peer : d.nodes()) {
                if (peer == prod) continue;
                d.bus().enqueue(prod->label(), peer->label(),
                                std::static_pointer_cast<void>(ptr_a), when);
                d.bus().enqueue(carrier, peer->label(),
                                std::static_pointer_cast<void>(ptr_b), when);
            }

            equivocation_consumed_ = true;
        });
}

} // namespace consensus_sim
