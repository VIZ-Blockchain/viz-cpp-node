#include "simulated_node.hpp"

#include <graphene/chain/database_exceptions.hpp>
#include <fc/exception/exception.hpp>

#include <chainbase/chainbase.hpp>

#include <boost/filesystem.hpp>

#include <stdexcept>

namespace consensus_sim {

namespace bfs = boost::filesystem;

static fc::path make_temp_dir(const std::string& prefix) {
    auto p = bfs::temp_directory_path() /
             bfs::unique_path("consensus_sim-" + prefix + "-%%%%%%%%");
    bfs::create_directories(p);
    return fc::path(p.string());
}

simulated_node::simulated_node(std::string label,
                               const genesis_params& params,
                               virtual_clock& clk)
    : label_(std::move(label)),
      data_dir_(make_temp_dir(label_ + "-data")),
      shared_mem_dir_(make_temp_dir(label_ + "-shm")),
      db_(std::make_unique<graphene::chain::database>()),
      clk_(clk) {
    db_->open(data_dir_, shared_mem_dir_,
              params.initial_supply,
              /*shared_file_size=*/64ull * 1024 * 1024,
              /*chainbase_flags=*/chainbase::database::read_write);
    register_witness_keys_(params);
}

simulated_node::~simulated_node() {
    try { if (db_) db_->close(); } catch (...) {}
    try { bfs::remove_all(data_dir_.string()); } catch (...) {}
    try { bfs::remove_all(shared_mem_dir_.string()); } catch (...) {}
}

void simulated_node::register_witness_keys_(const genesis_params& params) {
    // Milestone 1: no-op. VIZ's init_genesis creates a single witness
    // (CHAIN_INITIATOR_NAME, signed by CHAIN_INITIATOR_PUBLIC_KEY_STR).
    // The smoke test produces blocks as that witness using genesis_params'
    // initiator_key, so no rotation is needed.
    //
    // Milestone 3+ (equivocation tests) will need distinct witness identities;
    // at that point this method will push witness_update operations to rotate
    // each genesis witness to params.witness_keys[i].
    (void)params;
}

graphene::protocol::signed_block simulated_node::produce_block(
    const graphene::protocol::account_name_type& witness,
    const fc::ecc::private_key& key,
    fc::time_point_sec when) {
    try {
        return db_->generate_block(when, witness, key,
                                   graphene::chain::database::skip_nothing);
    } catch (const fc::exception& e) {
        throw std::runtime_error(
            "simulated_node::produce_block failed: " + e.to_detail_string());
    }
}

block_outcome simulated_node::receive_block(
    const graphene::protocol::signed_block& block) noexcept {
    try {
        bool accepted = db_->push_block(block, graphene::chain::database::skip_nothing);
        if (accepted) {
            return (db_->head_block_id() == block.id())
                ? block_outcome::accepted_extends_head
                : block_outcome::accepted_into_fork_db;
        }
        return block_outcome::rejected_duplicate;
    } catch (const fc::exception& e) {
        const std::string msg = e.to_detail_string();
        if (msg.find("signature") != std::string::npos)
            return block_outcome::rejected_invalid_signature;
        if (msg.find("witness") != std::string::npos)
            return block_outcome::rejected_witness_not_scheduled;
        if (msg.find("duplicate") != std::string::npos)
            return block_outcome::rejected_duplicate;
        if (msg.find("invalid") != std::string::npos ||
            msg.find("merkle") != std::string::npos ||
            msg.find("balance") != std::string::npos)
            return block_outcome::rejected_invalid_state;
        return block_outcome::unexpected_exception;
    } catch (...) {
        return block_outcome::unexpected_exception;
    }
}

uint32_t simulated_node::head_block_num() const { return db_->head_block_num(); }
graphene::protocol::block_id_type simulated_node::head_block_id() const { return db_->head_block_id(); }
fc::time_point_sec simulated_node::head_block_time() const { return db_->head_block_time(); }
uint32_t simulated_node::last_irreversible_block_num() const {
    return db_->last_non_undoable_block_num();
}

} // namespace consensus_sim
