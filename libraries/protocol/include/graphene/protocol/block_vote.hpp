#pragma once

#include <graphene/protocol/types.hpp>
#include <fc/io/raw.hpp>

namespace graphene { namespace protocol {

    /// Domain tag of a block vote. Keeps a vote signature from ever being valid
    /// as a signature over anything else (transaction, block header, the
    /// pre-HF16 post-validation digest of chain_id+block_id).
    static const char *const CHAIN_BLOCK_VOTE_DOMAIN = "viz-block-vote-v1";

    /// Digest a validator signs to vote for block_id at (block_num, round).
    /// A quorum of these votes for one (block_num, round, block_id) is a quorum
    /// certificate (QC). One signature per (validator, block_num, round) is the
    /// safety rule the validator plugin enforces before signing.
    inline digest_type block_vote_digest(const chain_id_type &chain_id, uint32_t block_num,
                                         const block_id_type &block_id, uint32_t round) {
        digest_type::encoder enc;
        fc::raw::pack(enc, std::string(CHAIN_BLOCK_VOTE_DOMAIN));
        fc::raw::pack(enc, chain_id);
        fc::raw::pack(enc, block_num);
        fc::raw::pack(enc, round);
        fc::raw::pack(enc, block_id);
        return enc.result();
    }

} } // graphene::protocol
