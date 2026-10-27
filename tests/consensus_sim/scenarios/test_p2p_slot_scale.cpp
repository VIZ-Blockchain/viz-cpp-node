// Handler-level guard for the HF16 P2P threshold scaling (PR #183).
//
// FORWARD_FALLBEHIND_THRESHOLD (2) and FORK_RESOLUTION_BLOCK_THRESHOLD (42) are
// block counts written for 3 s slots.  After HF16 every use site multiplies them
// by slot_scale() so they keep their wall-clock meaning (6 s, 126 s).  The
// offline test in tests/p2p/test_slot_interval.py pins the arithmetic; this one
// drives the real handlers through a mock delegate, so a use site that drops the
// multiplier fails here.  No sockets: send_message() is a no-op for a peer with
// no connection, and exchange_enabled=false keeps request_blocks_from_peer out.

#include <boost/test/unit_test.hpp>

#include <graphene/network/dlt_p2p_node.hpp>

#include <fc/bitutil.hpp>

namespace graphene { namespace network {

struct dlt_p2p_node_test_access {
    static constexpr uint32_t FALLBEHIND = dlt_p2p_node::FORWARD_FALLBEHIND_THRESHOLD;
    static constexpr uint32_t FORK = dlt_p2p_node::FORK_RESOLUTION_BLOCK_THRESHOLD;
    static void add_peer(dlt_p2p_node& n, uint32_t head) {
        auto& s = n._peer_states[n._peer_states.size() + 1];
        s.lifecycle_state = DLT_PEER_LIFECYCLE_ACTIVE;
        s.peer_head_num = head;
    }
    static void set_forward(dlt_p2p_node& n) {
        n._node_status = DLT_NODE_STATUS_FORWARD;
        n._forward_entered_time = fc::time_point();  // no grace period
    }
    static void check_forward_behind(dlt_p2p_node& n) { n.check_forward_behind(); }
    static void check_sync_catchup(dlt_p2p_node& n) { n.check_sync_catchup(); }
    static void track_fork_state(dlt_p2p_node& n, const signed_block& b) { n.track_fork_state(b); }
    static void set_highest_seen(dlt_p2p_node& n, uint32_t h) { n._highest_seen_block_num = h; }
    static uint32_t resolve_attempts(const dlt_p2p_node& n) {
        return n._fork_resolution_state.consecutive_blocks_as_winner;
    }
};

}} // graphene::network

using namespace graphene::network;
using graphene::protocol::block_id_type;
using graphene::protocol::signed_block;
using graphene::protocol::signed_transaction;
using A = dlt_p2p_node_test_access;

namespace {

constexpr uint32_t HEAD = 1000;

struct mock_delegate : dlt_p2p_delegate {
    uint32_t interval = CHAIN_BLOCK_INTERVAL;
    size_t tips = 1;

    uint32_t block_interval() const override { return interval; }
    block_id_type get_head_block_id() const override { return block_id_type(); }
    uint32_t get_head_block_num() const override { return HEAD; }
    block_id_type get_lib_block_id() const override { return block_id_type(); }
    uint32_t get_lib_block_num() const override { return HEAD - 10; }
    uint32_t get_dlt_earliest_block() const override { return 1; }
    uint32_t get_dlt_latest_block() const override { return HEAD; }
    bool is_emergency_consensus_active() const override { return false; }
    bool has_emergency_private_key() const override { return false; }
    bool is_dlt_mode() const override { return false; }
    fc::optional<signed_block> read_block_by_num(uint32_t) const override { return {}; }
    bool block_exists_in_log_or_fork_db(uint32_t, block_id_type&) const override { return false; }
    bool is_block_known(const block_id_type&) const override { return false; }
    dlt_block_accept_result accept_block(const signed_block&, bool) override { return dlt_block_accept_result::REJECTED; }
    bool accept_transaction(const signed_transaction&) override { return false; }
    int compare_fork_branches(const block_id_type&, const block_id_type&) const override { return 0; }
    std::vector<block_id_type> get_fork_branch_tips() const override {
        std::vector<block_id_type> v(tips);
        for (size_t i = 0; i < v.size(); ++i) v[i]._hash[4] = uint32_t(i + 1);
        return v;
    }
    void switch_to_fork(const block_id_type&) override {}
    bool is_head_on_branch(const block_id_type&) const override { return true; }
    bool is_tapos_block_known(uint32_t, uint32_t) const override { return false; }
    bool check_tapos_block_summary(uint32_t, uint32_t) const override { return false; }
    void resync_from_lib(bool) override {}
    void clear_syncing() override {}
};

signed_block block_at(uint32_t num) {
    signed_block b;
    b.previous._hash[0] = fc::endian_reverse_u32(num - 1);
    return b;
}

// FORWARD with one peer `ahead` blocks past our head: does the node drop to SYNC?
bool falls_behind(uint32_t interval, uint32_t ahead) {
    mock_delegate d; d.interval = interval;
    dlt_p2p_node n("test"); n.set_delegate(&d);
    A::set_forward(n);
    A::add_peer(n, HEAD + ahead);
    A::check_forward_behind(n);
    return n.get_node_status() == DLT_NODE_STATUS_SYNC;
}

// SYNC, peers at our head, unlinked blocks seen `ahead` past it: does the node go FORWARD?
bool catches_up(uint32_t interval, uint32_t ahead) {
    mock_delegate d; d.interval = interval;
    dlt_p2p_node n("test"); n.set_delegate(&d);
    A::add_peer(n, HEAD);
    A::set_highest_seen(n, HEAD + ahead);
    A::check_sync_catchup(n);
    return n.get_node_status() == DLT_NODE_STATUS_FORWARD;
}

// Fork seen at block 500, then a block `after` later: was resolution attempted?
bool resolves_fork(uint32_t interval, uint32_t after) {
    mock_delegate d; d.interval = interval; d.tips = 2;
    dlt_p2p_node n("test"); n.set_delegate(&d);
    A::track_fork_state(n, block_at(500));
    BOOST_REQUIRE(n.get_fork_status() == DLT_FORK_STATUS_LOOKING_RESOLUTION);
    A::track_fork_state(n, block_at(500 + after));
    return A::resolve_attempts(n) > 0;
}

constexpr uint32_t FALLBEHIND = A::FALLBEHIND;
constexpr uint32_t FORK = A::FORK;

} // namespace

BOOST_AUTO_TEST_SUITE(p2p_slot_scale)

BOOST_AUTO_TEST_CASE(forward_behind_keeps_6s_after_hf16) {
    BOOST_CHECK(!falls_behind(3, FALLBEHIND));
    BOOST_CHECK(falls_behind(3, FALLBEHIND + 1));
    BOOST_CHECK(!falls_behind(1, 3 * FALLBEHIND));      // 6 blocks = 6 s: not behind
    BOOST_CHECK(falls_behind(1, 3 * FALLBEHIND + 1));
}

BOOST_AUTO_TEST_CASE(sync_catchup_keeps_6s_after_hf16) {
    BOOST_CHECK(catches_up(3, FALLBEHIND));
    BOOST_CHECK(!catches_up(3, FALLBEHIND + 1));
    BOOST_CHECK(catches_up(1, 3 * FALLBEHIND));
    BOOST_CHECK(!catches_up(1, 3 * FALLBEHIND + 1));
}

BOOST_AUTO_TEST_CASE(fork_resolution_keeps_126s_after_hf16) {
    BOOST_CHECK(!resolves_fork(3, FORK - 1));
    BOOST_CHECK(resolves_fork(3, FORK));
    BOOST_CHECK(!resolves_fork(1, 3 * FORK - 1));       // 125 blocks = 125 s: too early
    BOOST_CHECK(resolves_fork(1, 3 * FORK));
}

BOOST_AUTO_TEST_SUITE_END()
