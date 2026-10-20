// Verdict branches of admin.objects.check.
//
// The judge decides whether an operator is invited to wipe the node's state, so every arm is
// driven here rather than only observed live: a false "corrupted" destroys a healthy node, and
// a false "ok" hides damage that the panel exists to surface. These are pure state -> verdict
// checks; the collector that fills `state` from chainbase is exercised by the acceptance suite.

#define BOOST_TEST_MODULE node_admin_objects
#include <boost/test/unit_test.hpp>

#include <graphene/plugins/node_admin/objects_check.hpp>

#include <string>

using namespace graphene::plugins::node_admin::objects_check;

namespace {

const uint32_t kSchema = 15;

/// A node in the shape a healthy running process reports: files present, head and revision
/// agree, all core indexes have their expected cardinality, plenty of room.
state healthy() {
    state s;
    s.state_dir = "/var/lib/vizd/state";
    s.state_dir_exists = true;
    s.shm_exists = true;
    s.shm_bytes = 1024ull * 1024 * 1024;
    s.mapped_bytes = 1024ull * 1024 * 1024;
    s.free_bytes = 700ull * 1024 * 1024;
    s.revision = 1000;
    s.head_block_num = 1000;
    s.schema_file_present = true;
    s.stored_schema = kSchema;
    s.block_log_present = true;
    s.block_log_bytes = 512ull * 1024 * 1024;
    s.dgp_objects = 1;
    s.schedule_objects = 1;
    s.account_objects = 100;
    return s;
}

int count_code(const judgement &j, const std::string &code) {
    int n = 0;
    for (const finding &f : j.findings) {
        if (f.code == code) ++n;
    }
    return n;
}

bool has_code(const judgement &j, const std::string &code) { return count_code(j, code) > 0; }

} // namespace

BOOST_AUTO_TEST_SUITE(node_admin_objects)

BOOST_AUTO_TEST_CASE(healthy_state_is_ok_and_offers_no_repair) {
    const judgement j = judge(healthy(), kSchema);
    BOOST_TEST(j.verdict == "ok");
    BOOST_TEST(j.critical == 0);
    BOOST_TEST(j.warnings == 0);
    BOOST_TEST(j.findings.empty());
    BOOST_TEST(j.recommended_repair.empty());
    BOOST_TEST(j.replay_available);
}

BOOST_AUTO_TEST_CASE(revision_gap_after_crash_is_warn_not_corruption) {
    state s = healthy();
    s.revision = 998; // head advanced, index revision lagged: normal after a crash
    const judgement j = judge(s, kSchema);
    BOOST_TEST(j.verdict == "warn");
    BOOST_TEST(j.critical == 0);
    BOOST_TEST(j.warnings == 1);
    BOOST_TEST(has_code(j, "revision_behind_head"));
    BOOST_TEST(j.recommended_repair.empty());
}

BOOST_AUTO_TEST_CASE(truncated_image_is_corrupted_and_replay_is_recommended) {
    state s = healthy();
    s.shm_bytes = s.mapped_bytes - 4096;
    const judgement j = judge(s, kSchema);
    BOOST_TEST(j.verdict == "corrupted");
    BOOST_TEST(has_code(j, "shared_memory_truncated"));
    BOOST_TEST(j.recommended_repair == "replay");
}

BOOST_AUTO_TEST_CASE(truncation_without_block_log_recommends_resync) {
    state s = healthy();
    s.shm_bytes = s.mapped_bytes - 4096;
    s.block_log_present = false;
    s.block_log_bytes = 0;
    const judgement j = judge(s, kSchema);
    BOOST_TEST(j.verdict == "corrupted");
    BOOST_TEST(!j.replay_available);
    BOOST_TEST(j.recommended_repair == "resync");
}

BOOST_AUTO_TEST_CASE(exhausted_memory_alone_is_not_repairable_by_a_wipe) {
    state s = healthy();
    s.free_bytes = 4ull * 1024 * 1024;
    const judgement j = judge(s, kSchema);
    BOOST_TEST(j.verdict == "corrupted");
    BOOST_TEST(j.critical == 1);
    BOOST_TEST(has_code(j, "shared_memory_exhausted"));
    // The same size would fill again, so wiping is not the fix.
    BOOST_TEST(j.recommended_repair.empty());
}

BOOST_AUTO_TEST_CASE(exhaustion_next_to_another_critical_still_recommends_replay) {
    state s = healthy();
    s.free_bytes = 4ull * 1024 * 1024;
    s.shm_bytes = s.mapped_bytes - 4096;
    const judgement j = judge(s, kSchema);
    BOOST_TEST(j.verdict == "corrupted");
    BOOST_TEST(j.critical == 2);
    BOOST_TEST(j.recommended_repair == "replay");
}

BOOST_AUTO_TEST_CASE(stale_marker_is_critical_and_named) {
    state s = healthy();
    s.markers.push_back("undo_all_in_progress");
    s.markers.push_back("force_replay");
    const judgement j = judge(s, kSchema);
    BOOST_TEST(j.verdict == "corrupted");
    BOOST_TEST(has_code(j, "stale_marker"));
    BOOST_TEST(j.recommended_repair == "replay");
    // The text names what is lying around, not just the count.
    std::string text;
    for (const finding &f : j.findings) {
        if (f.code == "stale_marker") text = f.text;
    }
    BOOST_TEST(text.find("undo_all_in_progress") != std::string::npos);
    BOOST_TEST(text.find("force_replay") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(schema_rules_cover_missing_and_different_versions) {
    state missing = healthy();
    missing.schema_file_present = false;
    missing.stored_schema = 0;
    const judgement jm = judge(missing, kSchema);
    BOOST_TEST(jm.verdict == "corrupted");
    BOOST_TEST(has_code(jm, "schema_mismatch"));

    state older = healthy();
    older.stored_schema = kSchema - 1;
    BOOST_TEST(has_code(judge(older, kSchema), "schema_mismatch"));

    state same = healthy();
    BOOST_TEST(!has_code(judge(same, kSchema), "schema_mismatch"));
}

BOOST_AUTO_TEST_CASE(core_index_counts_are_checked_only_above_genesis) {
    state bad = healthy();
    bad.dgp_objects = 0;
    bad.schedule_objects = 0;
    bad.account_objects = 0;
    const judgement above = judge(bad, kSchema);
    BOOST_TEST(above.verdict == "corrupted");
    BOOST_TEST(has_code(above, "global_properties_count"));
    BOOST_TEST(has_code(above, "validator_schedule_count"));
    BOOST_TEST(has_code(above, "account_index_empty"));

    state genesis = bad;
    genesis.head_block_num = 0;
    genesis.revision = 0;
    genesis.dgp_objects = -1;
    genesis.schedule_objects = -1;
    genesis.account_objects = -1;
    const judgement at_zero = judge(genesis, kSchema);
    BOOST_TEST(!has_code(at_zero, "global_properties_count"));
    BOOST_TEST(!has_code(at_zero, "validator_schedule_count"));
    BOOST_TEST(!has_code(at_zero, "account_index_empty"));
    BOOST_TEST(at_zero.verdict == "ok");
}

BOOST_AUTO_TEST_CASE(free_space_boundary_is_five_percent_strictly) {
    // The file must be large enough that 5% stays above the 8 MiB "exhausted" floor, otherwise
    // the absolute rule fires first and this stops testing the percentage one. 200,000,000 / 20
    // is 10 MB and divides exactly, so the edge case is exact rather than a truncation artefact.
    const uint64_t mapped = 200000000;

    state low = healthy();
    low.mapped_bytes = mapped;
    low.shm_bytes = mapped;
    low.free_bytes = mapped / 20 - 1; // just under 5%
    const judgement jl = judge(low, kSchema);
    BOOST_TEST(jl.verdict == "warn");
    BOOST_TEST(has_code(jl, "shared_memory_low"));
    BOOST_TEST(!has_code(jl, "shared_memory_exhausted"));

    state edge = healthy();
    edge.mapped_bytes = mapped;
    edge.shm_bytes = mapped;
    edge.free_bytes = mapped / 20; // exactly 5%: not "less than"
    const judgement je = judge(edge, kSchema);
    BOOST_TEST(!has_code(je, "shared_memory_low"));
    BOOST_TEST(je.verdict == "ok");
}

BOOST_AUTO_TEST_CASE(file_larger_than_the_mapping_is_only_a_warning) {
    state s = healthy();
    s.shm_bytes = s.mapped_bytes + 4096;
    const judgement j = judge(s, kSchema);
    BOOST_TEST(j.verdict == "warn");
    BOOST_TEST(has_code(j, "shared_memory_larger_than_mapping"));
    BOOST_TEST(j.recommended_repair.empty());
}

BOOST_AUTO_TEST_CASE(missing_state_is_corrupted_without_inventing_file_findings) {
    state dir_gone = healthy();
    dir_gone.state_dir_exists = false;
    const judgement j1 = judge(dir_gone, kSchema);
    BOOST_TEST(j1.verdict == "corrupted");
    BOOST_TEST(has_code(j1, "state_dir_missing"));
    BOOST_TEST(!has_code(j1, "shared_memory_truncated"));
    BOOST_TEST(!has_code(j1, "schema_mismatch"));

    state shm_gone = healthy();
    shm_gone.shm_exists = false;
    const judgement j2 = judge(shm_gone, kSchema);
    BOOST_TEST(j2.verdict == "corrupted");
    BOOST_TEST(has_code(j2, "shared_memory_missing"));
    BOOST_TEST(!has_code(j2, "shared_memory_truncated"));
    BOOST_TEST(j2.recommended_repair == "replay");
}

BOOST_AUTO_TEST_CASE(lock_timeout_is_unreadable_not_corrupted) {
    // A wedged process is a different operational problem than a damaged file: the panel must
    // not invite a wipe on the strength of a lock that was never answered.
    state s = healthy();
    s.lock_error = "read lock timed out";
    s.mapped_bytes = 0;
    s.free_bytes = 0;
    s.revision = 0;
    s.head_block_num = 0;
    const judgement j = judge(s, kSchema);
    BOOST_TEST(j.verdict == "unreadable");
    BOOST_TEST(has_code(j, "read_lock_timeout"));
    BOOST_TEST(!has_code(j, "shared_memory_truncated"));
    BOOST_TEST(j.recommended_repair.empty());
}

BOOST_AUTO_TEST_CASE(recovery_and_missing_block_log_are_warnings_only) {
    state recovering = healthy();
    recovering.recovering = true;
    const judgement j1 = judge(recovering, kSchema);
    BOOST_TEST(j1.verdict == "warn");
    BOOST_TEST(has_code(j1, "recovery_in_progress"));
    BOOST_TEST(j1.recommended_repair.empty());

    state no_log = healthy();
    no_log.block_log_present = false;
    no_log.block_log_bytes = 0;
    const judgement j2 = judge(no_log, kSchema);
    BOOST_TEST(j2.verdict == "warn");
    BOOST_TEST(has_code(j2, "block_log_missing"));
    BOOST_TEST(!j2.replay_available);
}

BOOST_AUTO_TEST_CASE(empty_block_log_is_not_replayable) {
    state s = healthy();
    s.block_log_bytes = 0; // present but empty: replay would have nothing to index
    const judgement j = judge(s, kSchema);
    BOOST_TEST(!j.replay_available);
    s.shm_bytes = s.mapped_bytes - 1;
    const judgement bad = judge(s, kSchema);
    BOOST_TEST(bad.recommended_repair == "resync");
}

BOOST_AUTO_TEST_CASE(repair_effect_names_snapshot_replay_where_it_applies) {
    // Plain node: the marker means what its name says.
    BOOST_TEST(repair_effect("replay", false, false) ==
               "next startup wipes shared memory and reindexes the existing block log from block 1");
    BOOST_TEST(repair_effect("resync", false, false) ==
               "next startup wipes shared memory and the block log, then resyncs from peers");

    // --replay-from-snapshot supersedes the block-log replay; the text must say so.
    const std::string taken = repair_effect("replay", true, true);
    BOOST_TEST(taken.find("snapshot") != std::string::npos);
    BOOST_TEST(taken.find("block 1") == std::string::npos);
    BOOST_TEST(taken != repair_effect("replay", false, false));

    // A snapshot without --replay-from-snapshot still changes where a resync starts from:
    // state comes back from the snapshot, not from genesis.
    const std::string from_snapshot = repair_effect("resync", false, true);
    BOOST_TEST(from_snapshot != repair_effect("resync", false, false));
    BOOST_TEST(from_snapshot.find("not from genesis") != std::string::npos);

    // replay on such a node is unaffected: it keeps its own wording.
    BOOST_TEST(repair_effect("replay", false, true) == repair_effect("replay", false, false));
}

BOOST_AUTO_TEST_SUITE_END()
