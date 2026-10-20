#pragma once

// Judging of the chainbase state view shown by the Objects tab.
//
// The plugin collects facts about the running node (file sizes, index counts, markers) and
// this header turns them into a verdict. It is deliberately free of fc/chainbase/appbase:
// the whole point of separating it is that every branch can be driven by a unit test with a
// hand-built state, including the ones an operator acts on (wipe + replay / resync), where a
// false "corrupted" would be expensive and a false "ok" would hide real damage.

#include <cstdint>
#include <string>
#include <vector>

namespace graphene {
namespace plugins {
namespace node_admin {
namespace objects_check {

/// One row per chainbase index (one per object type).
struct index_row {
    std::string type;
    int64_t type_id = 0;
    int64_t objects = 0;
    int64_t revision = 0;
};

/// Everything the admin plugin can see about the state of a running node. Defaults describe
/// a node whose state directory is missing; the collector fills what it can read.
struct state {
    std::string state_dir;
    bool state_dir_exists = false;
    bool shm_exists = false;
    uint64_t shm_bytes = 0;
    uint64_t mapped_bytes = 0;
    uint64_t free_bytes = 0;
    uint64_t reserved_bytes = 0;
    int64_t revision = 0;
    uint32_t head_block_num = 0;
    uint32_t stored_schema = 0;
    bool schema_file_present = false;
    bool block_log_present = false;
    uint64_t block_log_bytes = 0;
    bool recovering = false;
    std::vector<std::string> markers;
    std::vector<index_row> indexes;
    int64_t total_objects = 0;
    int64_t dgp_objects = -1;
    int64_t schedule_objects = -1;
    int64_t account_objects = -1;
    std::string lock_error;
};

struct finding {
    std::string severity; // "critical" | "warn"
    std::string code;
    std::string text;
};

struct judgement {
    std::string verdict; // "ok" | "warn" | "corrupted" | "unreadable"
    int critical = 0;
    int warnings = 0;
    std::vector<finding> findings;
    std::string recommended_repair; // "" | "replay" | "resync"
    bool replay_available = false;
};

/// Facts only: the node's schema version differs from the binary that is running it.
inline bool schema_mismatch(const state &s, uint32_t compiled_schema) {
    return s.shm_exists && (!s.schema_file_present || s.stored_schema != compiled_schema);
}

/// What the next startup will really do with a repair marker. The chain plugin takes the
/// snapshot path before it looks at force_replay, so on a --replay-from-snapshot node neither
/// marker does what its name suggests: the state is rebuilt from the snapshot either way.
inline std::string repair_effect(const std::string &mode, bool snapshot_replay_from,
                                 bool snapshot_configured) {
    if (mode == "replay" && snapshot_replay_from) {
        return "next startup rebuilds state from the configured snapshot and replays the "
               "block log on top of it: this node runs with --replay-from-snapshot, which the "
               "chain plugin takes before the marker's replay-from-block-1 path";
    }
    if (mode == "replay") {
        return "next startup wipes shared memory and reindexes the existing block log from block 1";
    }
    if (snapshot_configured) {
        return "next startup wipes shared memory and the block log, then rebuilds state from the "
               "configured snapshot and continues from the network (not from genesis)";
    }
    return "next startup wipes shared memory and the block log, then resyncs from peers";
}

/// A block log that only carries chainbase's one-byte placeholder has nothing to replay.
/// block_log::open() writes that byte when it creates the files, so on a fresh node (and after
/// a resync wipe) "the file exists" is true while it holds zero blocks. Treating it as a
/// replay source would promise a rebuild that would reindex nothing.
inline bool has_replayable_blocks(const state &s) {
    return s.block_log_present && s.block_log_bytes > 1;
}

inline judgement judge(const state &s, uint32_t compiled_schema) {
    judgement j;
    auto add = [&](const char *severity, const char *code, const std::string &text) {
        finding f;
        f.severity = severity;
        f.code = code;
        f.text = text;
        j.findings.push_back(f);
        if (std::string(severity) == "critical") ++j.critical;
        else if (std::string(severity) == "warn") ++j.warnings;
    };
    auto num = [](uint64_t v) { return std::to_string(v); };

    if (!s.lock_error.empty()) {
        add("critical", "read_lock_timeout",
            "shared memory did not answer a read lock: " + s.lock_error);
    }
    if (!s.state_dir_exists) {
        add("critical", "state_dir_missing", "shared-memory directory does not exist: " + s.state_dir);
    } else if (!s.shm_exists) {
        add("critical", "shared_memory_missing",
            "shared_memory.bin is absent in " + s.state_dir + "; the node has no state");
    } else {
        if (s.mapped_bytes > 0 && s.shm_bytes < s.mapped_bytes) {
            add("critical", "shared_memory_truncated",
                "shared_memory.bin is " + num(s.shm_bytes) + " bytes but the node maps " +
                num(s.mapped_bytes) + "; the file was truncated or replaced under a running process");
        } else if (s.mapped_bytes > 0 && s.shm_bytes > s.mapped_bytes) {
            add("warn", "shared_memory_larger_than_mapping",
                "shared_memory.bin is " + num(s.shm_bytes) + " bytes, larger than the mapping this "
                "process opened (" + num(s.mapped_bytes) + "); something else grew the file under "
                "the running node, usually a second process pointed at the same state directory");
        }
    }
    if (!s.markers.empty()) {
        std::string list;
        for (const std::string &x : s.markers) {
            if (!list.empty()) list += ", ";
            list += x;
        }
        add("critical", "stale_marker",
            "marker file(s) present from an interrupted operation: " + list +
            "; the next startup will act on them, and until then the state is mid-operation");
    }
    if (schema_mismatch(s, compiled_schema)) {
        add("critical", "schema_mismatch",
            "schema_version " + std::string(s.schema_file_present ? num(s.stored_schema) : "missing") +
            " does not match this binary (" + num(compiled_schema) +
            "); the next startup wipes shared memory and rebuilds it from a snapshot or block log");
    }
    if (s.shm_exists && s.revision != static_cast<int64_t>(s.head_block_num)) {
        add("warn", "revision_behind_head",
            "index revision " + std::to_string(s.revision) + " differs from head block " +
            num(s.head_block_num) + "; the next startup replays the gap from the block log. "
            "Normal after a crash, not corruption by itself");
    }
    if (s.mapped_bytes > 0 && s.free_bytes < 8ull * 1024 * 1024) {
        add("critical", "shared_memory_exhausted",
            "only " + num(s.free_bytes) + " bytes are free in the mapped file; the node cannot "
            "write another block. Repair does not help: raise shared-file-size or the resize "
            "limits and restart");
    } else if (s.mapped_bytes > 0 && s.free_bytes * 20 < s.mapped_bytes) {
        add("warn", "shared_memory_low",
            "less than 5% of the mapped file is free (" + num(s.free_bytes) + " of " +
            num(s.mapped_bytes) + " bytes)");
    }
    if (s.head_block_num > 0) {
        if (s.dgp_objects != 1) {
            add("critical", "global_properties_count",
                "dynamic_global_property_object rows = " + std::to_string(s.dgp_objects) +
                ", expected exactly 1");
        }
        if (s.schedule_objects != 1) {
            add("critical", "validator_schedule_count",
                "validator_schedule_object rows = " + std::to_string(s.schedule_objects) +
                ", expected exactly 1");
        }
        if (s.account_objects == 0) {
            add("critical", "account_index_empty",
                "account_object rows = 0 while head is " + num(s.head_block_num));
        }
    }
    if (!s.block_log_present && s.head_block_num > 0) {
        add("warn", "block_log_missing",
            "block_log is absent while head is " + num(s.head_block_num) +
            "; a replay cannot rebuild state, only a full resync can");
    }
    if (s.recovering) {
        add("warn", "recovery_in_progress",
            "the chain plugin reports an automatic recovery in progress");
    }

    if (!s.lock_error.empty()) j.verdict = "unreadable";
    else if (j.critical > 0) j.verdict = "corrupted";
    else if (j.warnings > 0) j.verdict = "warn";
    else j.verdict = "ok";

    // A repair rewrites state, so it is recommended only when rewriting can fix the finding.
    // Exhausted shared memory is the exception: the same size would fill up again, so wiping
    // alone is not a fix unless something else is also wrong.
    const bool repair_helps =
        j.verdict == "corrupted" &&
        !(j.critical == 1 && s.free_bytes < 8ull * 1024 * 1024 && s.mapped_bytes > 0);
    if (repair_helps) j.recommended_repair = has_replayable_blocks(s) ? "replay" : "resync";
    j.replay_available = has_replayable_blocks(s);
    return j;
}

} // namespace objects_check
} // namespace node_admin
} // namespace plugins
} // namespace graphene
