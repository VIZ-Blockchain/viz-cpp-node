#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <fc/crypto/sha256.hpp>
#include <fc/time.hpp>
#include <fc/io/raw.hpp>
#include <chainbase/chainbase.hpp>

// fc::raw pack/unpack for chainbase::object_id (not reflected, needs explicit support)
namespace fc { namespace raw {

    template<typename Stream, typename T>
    inline void pack(Stream& s, const chainbase::object_id<T>& id) {
        fc::raw::pack(s, id._id);
    }

    template<typename Stream, typename T>
    inline void unpack(Stream& s, chainbase::object_id<T>& id, uint32_t depth = 0) {
        fc::raw::unpack(s, id._id, depth);
    }

}} // fc::raw

namespace graphene {
namespace plugins {
namespace snapshot {

constexpr const char SNAPSHOT_MAGIC[8] = {'V','I','Z','S','N','A','P','\0'};
constexpr uint32_t SNAPSHOT_FORMAT_VERSION = 1;

struct snapshot_section_info {
    std::string type;
    uint64_t count = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
};

struct snapshot_header {
    std::string chain_id;
    uint32_t head_block_num = 0;
    std::string head_block_id;
    std::string timestamp;
    uint32_t snapshot_version = SNAPSHOT_FORMAT_VERSION;
    std::vector<snapshot_section_info> sections;
    std::string payload_sha256;
};

struct snapshot_export_r {
    bool success = false;
    std::string path;
    uint32_t head_block_num = 0;
    uint64_t total_objects = 0;
    uint32_t sections = 0;
    std::string sha256;
    std::string error;
};

struct snapshot_info_r {
    bool valid = false;
    snapshot_header header;
    std::string error;
};

struct snapshot_verify_r {
    bool success = false;
    uint64_t objects_checked = 0;
    uint64_t objects_matched = 0;
    uint64_t objects_mismatched = 0;
    uint64_t objects_missing = 0;
    std::vector<std::string> mismatches;
    std::string error;
};

} } } // graphene::plugins::snapshot

FC_REFLECT((graphene::plugins::snapshot::snapshot_section_info),
    (type)(count)(offset)(size))

FC_REFLECT((graphene::plugins::snapshot::snapshot_header),
    (chain_id)(head_block_num)(head_block_id)(timestamp)
    (snapshot_version)(sections)(payload_sha256))

FC_REFLECT((graphene::plugins::snapshot::snapshot_export_r),
    (success)(path)(head_block_num)(total_objects)(sections)(sha256)(error))

FC_REFLECT((graphene::plugins::snapshot::snapshot_info_r),
    (valid)(header)(error))

FC_REFLECT((graphene::plugins::snapshot::snapshot_verify_r),
    (success)(objects_checked)(objects_matched)(objects_mismatched)(objects_missing)(mismatches)(error))
