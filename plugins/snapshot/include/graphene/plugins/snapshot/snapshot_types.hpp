#pragma once

#include <fc/crypto/sha256.hpp>
#include <fc/time.hpp>
#include <graphene/protocol/types.hpp>
#include <graphene/protocol/block.hpp>

#include <map>
#include <string>

namespace graphene { namespace plugins { namespace snapshot {

    using graphene::protocol::chain_id_type;
    using graphene::protocol::block_id_type;

    /// Snapshot format version
    static const uint32_t SNAPSHOT_FORMAT_VERSION = 1;

    /// Magic bytes at start of snapshot file: "VIZ\x01"
    static const uint32_t SNAPSHOT_MAGIC = 0x015A4956;

    /// Section type identifiers for binary sections
    enum snapshot_section_type : uint32_t {
        section_header         = 0,
        section_objects        = 1,
        section_fork_db_block  = 2,
        section_checksum       = 3,
        section_end            = 0xFFFFFFFF
    };

    /// Snapshot file header (serialized as JSON)
    struct snapshot_header {
        uint32_t        version = SNAPSHOT_FORMAT_VERSION;
        chain_id_type   chain_id;
        uint32_t        snapshot_block_num = 0;
        block_id_type   snapshot_block_id;
        fc::time_point_sec snapshot_block_time;
        uint32_t        last_irreversible_block_num = 0;
        block_id_type   last_irreversible_block_id;
        fc::time_point_sec snapshot_creation_time;
        fc::sha256      payload_checksum;
        std::map<std::string, uint32_t> object_counts;
    };

} } } // graphene::plugins::snapshot

FC_REFLECT((graphene::plugins::snapshot::snapshot_header),
    (version)(chain_id)
    (snapshot_block_num)(snapshot_block_id)(snapshot_block_time)
    (last_irreversible_block_num)(last_irreversible_block_id)
    (snapshot_creation_time)(payload_checksum)(object_counts))
