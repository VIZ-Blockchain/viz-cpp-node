// Frozen receiver from master 52fab7b8 (node 4.3.1). Do not update with the sender.
void dlt_p2p_node::on_dlt_fork_status(peer_id peer, const dlt_fork_status_message& msg) {
    auto it = _peer_states.find(peer);
    if (it == _peer_states.end()) return;
    auto& state = it->second;

    // Track what the peer's node_status was before this update, so we can
    // detect a SYNC→FORWARD transition and react to it.
    uint8_t prev_node_status = state.peer_node_status;

    state.peer_fork_status = msg.fork_status;
    state.peer_head_id = msg.head_block_id;
    state.peer_head_num = msg.head_block_num;
    state.peer_lib_id = msg.lib_block_id;
    state.peer_lib_num = msg.lib_block_num;
    state.peer_dlt_earliest = msg.dlt_earliest_block;
    state.peer_dlt_latest = msg.dlt_latest_block;
    state.peer_node_status = msg.node_status;
    refresh_peer_lib_cache();  // the peer just moved its claim; the master gate reads this

    // When a peer transitions from SYNC to FORWARD (or is already FORWARD)
    // and we haven't enabled exchange with it yet, re-evaluate. The peer
    // just finished syncing — its head block may now be within our known
    // chain, meaning we should enable block/transaction exchange.
    if (!state.exchange_enabled &&
        msg.node_status == DLT_NODE_STATUS_FORWARD &&
        prev_node_status == DLT_NODE_STATUS_SYNC &&
        state.lifecycle_state == DLT_PEER_LIFECYCLE_ACTIVE &&
        msg.head_block_num > 0 && _delegate) {
        if (_delegate->is_block_known(msg.head_block_id)) {
            ilog(DLT_LOG_GREEN "Peer ${ep} transitioned to FORWARD, re-enabling exchange (head #${hn} now recognized)" DLT_LOG_RESET,
                 ("ep", state.endpoint)("hn", msg.head_block_num));
            state.exchange_enabled = true;
            state.fork_alignment = true;
        }
    }

    // Also update _highest_seen_block_num for gap detection (P37)
    if (msg.head_block_num > _highest_seen_block_num) {
        _highest_seen_block_num = msg.head_block_num;
    }

    record_packet_result(peer, true);
}
