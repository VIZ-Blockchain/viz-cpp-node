#pragma once

#include "genesis_factory.hpp"
#include "virtual_clock.hpp"

#include <graphene/chain/database.hpp>
#include <graphene/protocol/block.hpp>
#include <graphene/protocol/transaction.hpp>
#include <graphene/protocol/types.hpp>

#include <fc/filesystem.hpp>

#include <memory>
#include <string>
#include <vector>

namespace consensus_sim {

struct chain_block_info {
    uint32_t block_num;
    graphene::protocol::block_id_type id;
    std::string witness;
    fc::time_point_sec timestamp;
};

enum class block_outcome {
    accepted_extends_head,
    accepted_into_fork_db,
    rejected_invalid_signature,
    rejected_invalid_state,
    rejected_witness_not_scheduled,
    rejected_duplicate,
    unexpected_exception
};

class simulated_node {
public:
    simulated_node(std::string label,
                   const genesis_params& params,
                   virtual_clock& clk);
    ~simulated_node();

    simulated_node(const simulated_node&) = delete;
    simulated_node& operator=(const simulated_node&) = delete;

    /// Drive the wrapped database to produce a block at `when`,
    /// signed by `witness` with `key`. Returns the produced block.
    /// Throws std::runtime_error if the database refuses (caller's bug).
    graphene::protocol::signed_block produce_block(
        const graphene::protocol::account_name_type& witness,
        const fc::ecc::private_key& key,
        fc::time_point_sec when);

    /// Push a block produced elsewhere. Maps internal exceptions to
    /// a typed outcome. Never throws.
    block_outcome receive_block(const graphene::protocol::signed_block& block) noexcept;

    uint32_t head_block_num() const;
    graphene::protocol::block_id_type head_block_id() const;
    fc::time_point_sec head_block_time() const;
    uint32_t last_irreversible_block_num() const;

    /// Walks the canonical chain backward from head, returns up to `count`
    /// entries (newest first). Exposes witness + timestamp so equivocation
    /// invariants can detect repeated (witness, slot) pairs.
    std::vector<chain_block_info> recent_blocks(uint32_t count) const;

    /// Returns full signed_block bodies on the canonical chain at heights
    /// [from_height, head_block_num()] in ascending order. Empty if
    /// from_height > head. Used by the equivocation fault to catch a
    /// shadow node up to canonical state before forking it.
    std::vector<graphene::protocol::signed_block> canonical_blocks_from(
        uint32_t from_height) const;

    /// Push a transaction into the pending pool. Wraps database::push_transaction
    /// and maps fc::exception into std::runtime_error so the caller doesn't
    /// have to know about the chain's exception hierarchy. Used by the fault
    /// injector to force shadow/canonical divergence before producing
    /// equivocating blocks.
    void push_pending_transaction(const graphene::protocol::signed_transaction& tx);

    graphene::protocol::chain_id_type chain_id() const;

    const std::string& label() const noexcept { return label_; }

private:
    void register_witness_keys_(const genesis_params& params);

    std::string label_;
    fc::path data_dir_;
    fc::path shared_mem_dir_;
    std::unique_ptr<graphene::chain::database> db_;
    virtual_clock& clk_;
};

} // namespace consensus_sim
