#pragma once

#include <graphene/network/dlt_p2p_messages.hpp>
#include <graphene/network/dlt_p2p_peer_state.hpp>
#include <graphene/network/message.hpp>

#include <graphene/protocol/block.hpp>
#include <graphene/protocol/transaction.hpp>

#include <fc/crypto/elliptic.hpp>
#include <fc/network/ip.hpp>
#include <fc/network/tcp_socket.hpp>
#include <fc/time.hpp>
#include <fc/thread/future.hpp>

#include <map>
#include <set>
#include <string>
#include <vector>
#include <functional>

namespace graphene {
namespace network {

/**
 *  @class dlt_p2p_delegate
 *  @brief Callback interface between dlt_p2p_node and the chain/plugin layer.
 *
 *  Implemented by p2p_plugin_impl to provide chain state queries
 *  and block/transaction handling. Similar to the old node_delegate.
 */
class dlt_p2p_delegate {
public:
    virtual ~dlt_p2p_delegate() = default;

    // ── Chain state queries ──────────────────────────────────────
    virtual block_id_type  get_head_block_id() const = 0;
    virtual uint32_t       get_head_block_num() const = 0;
    virtual block_id_type  get_lib_block_id() const = 0;
    virtual uint32_t       get_lib_block_num() const = 0;
    virtual uint32_t       get_dlt_earliest_block() const = 0;
    virtual uint32_t       get_dlt_latest_block() const = 0;
    virtual bool           is_emergency_consensus_active() const = 0;
    virtual bool           has_emergency_private_key() const = 0;
    virtual bool           is_dlt_mode() const = 0;

    // ── Block queries ────────────────────────────────────────────
    virtual fc::optional<signed_block> read_block_by_num(uint32_t block_num) const = 0;
    virtual bool           block_exists_in_log_or_fork_db(uint32_t block_num, block_id_type& id_out) const = 0;
    virtual bool           is_block_known(const block_id_type& id) const = 0;

    // ── Block/transaction handling ───────────────────────────────
    virtual bool           accept_block(const signed_block& block, bool sync_mode) = 0;
    virtual bool           accept_transaction(const signed_transaction& trx) = 0;

    // ── Fork resolution ──────────────────────────────────────────
    virtual int            compare_fork_branches(const block_id_type& a, const block_id_type& b) const = 0;
    virtual std::vector<block_id_type> get_fork_branch_tips() const = 0;
    virtual void           switch_to_fork(const block_id_type& new_head) = 0;
    virtual bool           is_head_on_branch(const block_id_type& tip) const = 0;

    // ── TaPoS / mempool helpers ──────────────────────────────────
    virtual bool           is_tapos_block_known(uint32_t ref_block_num, uint32_t ref_block_prefix) const = 0;
    virtual void           resync_from_lib(bool force_emergency) = 0;
};

/**
 *  @class dlt_p2p_node
 *  @brief DLT-specific P2P node replacing the old Graphene synopsis-based node.
 */
class dlt_p2p_node {
public:
    explicit dlt_p2p_node(const std::string& user_agent);
    ~dlt_p2p_node();

    // ── Configuration ────────────────────────────────────────────
    void set_delegate(dlt_p2p_delegate* del);
    void set_listen_endpoint(const fc::ip::endpoint& ep, bool wait_if_busy = true);
    void add_seed_node(const fc::ip::endpoint& ep);
    void set_max_connections(uint32_t max_conn);
    void set_thread(fc::thread& t);

    void set_dlt_block_log_max_blocks(uint32_t max_blocks);
    void set_peer_max_disconnect_hours(uint32_t hours);
    void set_mempool_limits(uint32_t max_tx, uint32_t max_bytes, uint32_t max_tx_size, uint32_t max_expiration_hours);
    void set_peer_exchange_limits(uint32_t max_per_reply, uint32_t max_per_subnet, uint32_t min_uptime_sec);

    // ── Lifecycle ────────────────────────────────────────────────
    void start();
    void close();

    // ── Broadcast (called by p2p_plugin) ─────────────────────────
    void broadcast_block(const signed_block& block);
    void broadcast_block_post_validation(
        const block_id_type& block_id,
        const std::string& witness_account,
        const signature_type& witness_signature);
    void broadcast_transaction(const signed_transaction& trx);
    void broadcast_chain_status();

    // ── State queries (called by p2p_plugin) ─────────────────────
    uint32_t    get_connection_count() const;
    fc::time_point get_last_network_block_time() const;
    void        set_block_production(bool producing);
    void        resync_from_lib(bool force_emergency = false);
    void        trigger_resync();
    void        reconnect_seeds();
    void        pause_block_processing();
    void        resume_block_processing();

    // ── Our node state ───────────────────────────────────────────
    dlt_node_status get_node_status() const { return _node_status; }
    dlt_fork_status get_fork_status() const { return _fork_status; }
    bool is_on_majority_fork() const;

    // ── Called by plugin when a block is applied to chain ────────
    void on_block_applied(const signed_block& block, bool caused_fork_switch);

private:
    typedef uint64_t peer_id;
    static constexpr peer_id INVALID_PEER_ID = 0;

    // ── Message send ─────────────────────────────────────────────
    void send_message(peer_id peer, const message& msg);
    void send_to_all_our_fork_peers(const message& msg, peer_id exclude = INVALID_PEER_ID);

    // ── Connection management ────────────────────────────────────
    void connect_to_peer(const fc::ip::endpoint& ep);
    void handle_disconnect(peer_id peer, const std::string& reason, bool skip_backoff_increase = false);
    void periodic_reconnect_check();
    void periodic_lifecycle_timeout_check();
    void accept_loop();
    void start_read_loop(peer_id peer);

    // ── Message handlers ─────────────────────────────────────────
    void on_message(peer_id peer, const message& msg);

    void on_dlt_hello(peer_id peer, const dlt_hello_message& hello);
    void on_dlt_hello_reply(peer_id peer, const dlt_hello_reply_message& reply);
    void on_dlt_range_request(peer_id peer, const dlt_range_request_message& req);
    void on_dlt_range_reply(peer_id peer, const dlt_range_reply_message& reply);
    void on_dlt_get_block_range(peer_id peer, const dlt_get_block_range_message& req);
    void on_dlt_block_range_reply(peer_id peer, const dlt_block_range_reply_message& reply);
    void on_dlt_get_block(peer_id peer, const dlt_get_block_message& req);
    void on_dlt_block_reply(peer_id peer, const dlt_block_reply_message& reply);
    void on_dlt_not_available(peer_id peer, const dlt_not_available_message& msg);
    void on_dlt_fork_status(peer_id peer, const dlt_fork_status_message& msg);
    void on_dlt_peer_exchange_request(peer_id peer, const dlt_peer_exchange_request& req);
    void on_dlt_peer_exchange_reply(peer_id peer, const dlt_peer_exchange_reply& reply);
    void on_dlt_peer_exchange_rate_limited(peer_id peer, const dlt_peer_exchange_rate_limited& msg);
    void on_dlt_transaction(peer_id peer, const dlt_transaction_message& msg);

    // ── Hello construction ───────────────────────────────────────
    dlt_hello_message        build_hello_message() const;
    dlt_hello_reply_message  build_hello_reply(peer_id peer, const dlt_hello_message& hello) const;

    // ── Fork alignment check ─────────────────────────────────────
    bool check_fork_alignment(const block_id_type& head_id, const block_id_type& lib_id,
                              block_id_type& recognized_head_out, block_id_type& recognized_lib_out) const;

    // ── Node status transitions ──────────────────────────────────
    void transition_to_forward();
    void transition_to_sync();

    // ── Sync logic ───────────────────────────────────────────────
    void request_blocks_from_peer(peer_id peer);
    void sync_stagnation_check();

    // ── Mempool ──────────────────────────────────────────────────
    bool add_to_mempool(const signed_transaction& trx, bool from_peer, peer_id sender);
    void remove_transactions_in_block(const signed_block& block);
    void prune_mempool_on_fork_switch();
    void periodic_mempool_cleanup();
    bool is_tapos_valid(const signed_transaction& trx) const;
    bool is_mempool_full() const;
    void evict_oldest_expiry_mempool_entries(uint32_t count);

    // ── Fork resolution ──────────────────────────────────────────
    void track_fork_state(const signed_block& block);
    void resolve_fork();
    dlt_fork_branch_info compute_branch_info(const block_id_type& tip) const;

    // ── Anti-spam ────────────────────────────────────────────────
    bool record_packet_result(peer_id peer, bool is_good);
    void soft_ban_peer(peer_id peer);

    // ── DLT block log pruning ────────────────────────────────────
    void periodic_dlt_prune_check();

    // ── Peer exchange ────────────────────────────────────────────
    void periodic_peer_exchange();

    // ── Periodic tasks ───────────────────────────────────────────
    void periodic_task();
    void block_validation_timeout();

    // ── Subnet diversity ─────────────────────────────────────────
    uint32_t count_peers_in_subnet(const fc::ip::address& addr) const;
    bool is_same_subnet(const fc::ip::address& a, const fc::ip::address& b) const;

private:
    dlt_p2p_delegate*               _delegate = nullptr;
    std::string                     _user_agent;
    fc::ip::endpoint                _listen_endpoint;
    bool                            _wait_if_busy = true;
    uint32_t                        _max_connections = 50;

    // ── Our node state ───────────────────────────────────────────
    dlt_node_status                 _node_status = DLT_NODE_STATUS_SYNC;
    dlt_fork_status                 _fork_status = DLT_FORK_STATUS_NORMAL;
    node_id_t                       _node_id;
    bool                            _producing_blocks = false;
    fc::time_point                  _last_network_block_time;
    fc::time_point                  _last_block_received_time;

    // ── Fork tracking ────────────────────────────────────────────
    bool                            _fork_detected = false;
    uint32_t                        _fork_detection_block_num = 0;
    dlt_fork_resolution_state       _fork_resolution_state;

    // ── Sync stagnation ──────────────────────────────────────────
    uint32_t                        _sync_stagnation_retries = 0;
    static constexpr uint32_t       SYNC_STAGNATION_SEC = 30;
    static constexpr uint32_t       SYNC_STAGNATION_MAX_RETRIES = 3;

    // ── Peer state ───────────────────────────────────────────────
    peer_id                         _next_peer_id = 1;
    std::map<peer_id, dlt_peer_state> _peer_states;
    std::set<dlt_known_peer>        _known_peers;
    std::vector<fc::ip::endpoint>   _seed_nodes;

    // ── Active connections ───────────────────────────────────────
    std::map<peer_id, fc::tcp_socket_ptr> _connections;
    fc::tcp_server                  _tcp_server;

    // ── Fiber tracking ────────────────────────────────────────────
    fc::thread*                     _thread = nullptr;
    bool                            _running = false;
    std::map<peer_id, fc::future<void>> _read_fibers;
    fc::future<void>                _accept_fiber;
    fc::future<void>                _periodic_fiber;

    // ── Mempool ──────────────────────────────────────────────────
    std::map<graphene::protocol::transaction_id_type, dlt_mempool_entry> _mempool_by_id;
    std::multimap<fc::time_point_sec, graphene::protocol::transaction_id_type> _mempool_by_expiry;
    uint32_t                        _mempool_total_bytes = 0;

    uint32_t                        _mempool_max_tx = 10000;
    uint32_t                        _mempool_max_bytes = 100 * 1024 * 1024;
    uint32_t                        _mempool_max_tx_size = 64 * 1024;
    uint32_t                        _mempool_max_expiration_hours = 24;

    // ── DLT config ───────────────────────────────────────────────
    uint32_t                        _dlt_block_log_max_blocks = 100000;
    uint32_t                        _peer_max_disconnect_hours = 8;
    uint32_t                        _last_prune_block_num = 0;
    static constexpr uint32_t       DLT_PRUNE_BATCH_SIZE = 10000;

    // ── Peer exchange config ─────────────────────────────────────
    uint32_t                        _peer_exchange_max_per_reply = 10;
    uint32_t                        _peer_exchange_max_per_subnet = 2;
    uint32_t                        _peer_exchange_min_uptime_sec = 600;

    // ── Anti-spam ────────────────────────────────────────────────
    static constexpr uint32_t       SPAM_STRIKE_THRESHOLD = 10;
    static constexpr uint32_t       BAN_DURATION_SEC = 3600;

    // ── Fork resolution ──────────────────────────────────────────
    static constexpr uint32_t       FORK_RESOLUTION_BLOCK_THRESHOLD = 42;

    // ── Block processing pause ───────────────────────────────────
    bool                            _block_processing_paused = false;

    // ── Color-coded logging macros ─────────────────────────────
    // Must be #define (not constexpr) so they concatenate with
    // adjacent string literals in ilog/wlog format arguments.
};

// ── Color-coded logging macros (must be #define for string-literal concatenation) ─
#define DLT_LOG_GREEN   "\033[32m"
#define DLT_LOG_WHITE   "\033[37m"
#define DLT_LOG_RED     "\033[91m"
#define DLT_LOG_DGRAY   "\033[90m"
#define DLT_LOG_ORANGE  "\033[33m"
#define DLT_LOG_RESET   "\033[0m"

} // namespace network
} // namespace graphene
