#include <graphene/plugins/p2p/p2p_plugin.hpp>
#include <graphene/plugins/snapshot/plugin.hpp>

#include <graphene/network/node.hpp>
#include <graphene/network/exceptions.hpp>

#include <graphene/chain/database_exceptions.hpp>

#include <fc/network/resolve.hpp>

#include <boost/range/algorithm/reverse.hpp>
#include <boost/range/adaptor/reversed.hpp>

#include <map>

// ANSI color codes for P2P stats console log messages
#define CLOG_CYAN   "\033[96m"
#define CLOG_WHITE  "\033[97m"
#define CLOG_GRAY   "\033[90m"
#define CLOG_ORANGE "\033[33m"
#define CLOG_RED    "\033[91m"
#define CLOG_RESET  "\033[0m"

using std::string;
using std::vector;

namespace graphene {
    namespace plugins {
        namespace p2p {

            using appbase::app;

            using graphene::network::item_hash_t;
            using graphene::network::item_id;
            using graphene::network::message;
            using graphene::network::core_message_type_enum;
            using graphene::network::block_message;
            using graphene::network::block_post_validation_message;
            using graphene::network::trx_message;

            using graphene::protocol::block_header;
            using graphene::protocol::signed_block_header;
            using graphene::protocol::signed_block;
            using graphene::protocol::block_id_type;
            using graphene::chain::database;
            using graphene::chain::chain_id_type;

            using graphene::protocol::signature_type;

            namespace detail {

                class p2p_plugin_impl : public graphene::network::node_delegate {
                public:

                    p2p_plugin_impl(chain::plugin &c) : chain(c) {
                    }

                    virtual ~p2p_plugin_impl() {
                    }

                    bool is_included_block(const block_id_type &block_id);

                    chain_id_type get_chain_id() const;

                    // node_delegate interface
                    virtual bool has_item(const item_id &) override;

                    virtual bool handle_block(const block_message &, bool, std::vector<fc::uint160_t> &, fc::optional<fc::ip::endpoint> = fc::optional<fc::ip::endpoint>()) override;

                    virtual void handle_transaction(const trx_message &) override;

                    virtual void handle_message(const message &) override;

                    virtual std::vector<item_hash_t> get_block_ids(const std::vector<item_hash_t> &, uint32_t &,
                                                                   uint32_t) override;

                    virtual message get_item(const item_id &) override;

                    virtual std::vector<item_hash_t> get_blockchain_synopsis(const item_hash_t &, uint32_t) override;

                    virtual void sync_status(uint32_t, uint32_t) override;

                    virtual void connection_count_changed(uint32_t) override;

                    virtual uint32_t get_block_number(const item_hash_t &) override;

                    virtual fc::time_point_sec get_block_time(const item_hash_t &) override;

                    virtual fc::time_point_sec get_blockchain_now() override;

                    virtual item_hash_t get_head_block_id() const override;

                    virtual uint32_t estimate_last_known_fork_from_git_revision_timestamp(uint32_t) const override;

                    virtual void error_encountered(const std::string &message, const fc::oexception &error) override;

                    //virtual uint8_t get_current_block_interval_in_seconds() const override {
                    //    return CHAIN_BLOCK_INTERVAL;
                    //}


                    fc::optional<fc::ip::endpoint> endpoint;
                    vector<fc::ip::endpoint> seeds;
                    string user_agent;
                    uint32_t max_connections = 0;
                    bool force_validate = false;
                    bool block_producer = false;

                    bool stats_enabled = true;
                    uint32_t stats_interval_seconds = 300;
                    fc::future<void> _stats_task_done;
                    std::map<std::string, uint64_t> _stats_bytes_received_last;

                    void p2p_stats_task();

                    // Stale sync detection
                    bool _stale_sync_enabled = false;
                    uint32_t _stale_sync_timeout_seconds = 120;
                    fc::time_point _last_block_received_time;
                    fc::future<void> _stale_sync_task_done;

                    void stale_sync_check_task();

                    std::unique_ptr<graphene::network::node> node;

                    chain::plugin &chain;

                    fc::thread p2p_thread;
                };

                ////////////////////////////// Begin node_delegate Implementation //////////////////////////////
                bool p2p_plugin_impl::has_item(const item_id &id) {
                    return chain.db().with_weak_read_lock([&]() {
                        try {
                            if (id.item_type == network::block_message_type) {
                                return chain.db().is_known_block(id.item_hash);
                            } else {
                                return chain.db().is_known_transaction(id.item_hash);
                            }
                        } FC_CAPTURE_LOG_AND_RETHROW((id))
                    });
                }

                bool p2p_plugin_impl::handle_block(const block_message &blk_msg, bool sync_mode, std::vector<fc::uint160_t> &, fc::optional<fc::ip::endpoint> originating_peer_endpoint) {
                    try {
                        // Track last block received time for stale sync detection
                        _last_block_received_time = fc::time_point::now();

                        uint32_t head_block_num;
                        chain.db().with_weak_read_lock([&]() {
                            head_block_num = chain.db().head_block_num();
                        });
                        int32_t gap = (int32_t)blk_msg.block.block_num() - (int32_t)head_block_num - 1;
                        if (sync_mode)
                            dlog("Chain pushing sync block #${block_num} (head: ${head}, gap: ${gap})",
                                 ("block_num", blk_msg.block.block_num())("head", head_block_num)("gap", gap));
                        else
                            dlog("Chain pushing normal block #${block_num} (head: ${head}, gap: ${gap})",
                                 ("block_num", blk_msg.block.block_num())("head", head_block_num)("gap", gap));

                        try {
                            // When a block is too old for our fork database (e.g. a peer
                            // sending stale blocks from a dead fork), convert to
                            // block_older_than_undo_history so the network layer will
                            // inhibit/soft-ban the peer instead of restarting sync.
                            bool result = chain.accept_block(blk_msg.block, sync_mode, (block_producer | force_validate)
                                                                                       ? database::skip_nothing
                                                                                       : database::skip_transaction_signatures);

                            if (!sync_mode) {
                                fc::microseconds latency = fc::time_point::now() - blk_msg.block.timestamp;
                                std::string peer_str = originating_peer_endpoint ? (std::string)*originating_peer_endpoint : "unknown";
                                ilog(CLOG_WHITE "Got ${t} transactions on block ${b} by ${w} - latency: ${l} ms - from ${p}" CLOG_RESET,
                                     ("t", blk_msg.block.transactions.size())("b", blk_msg.block.block_num())("w", blk_msg.block.witness)("l", latency.count() / 1000)("p", peer_str));
                            }

                            return result;
                        } catch (const graphene::chain::block_too_old_exception &e) {
                            wlog("Block ${n} is too old for fork database (head=${head}): ${e}",
                                 ("n", blk_msg.block.block_num())("head", head_block_num)("e", e.to_detail_string()));
                            FC_THROW_EXCEPTION(graphene::network::block_older_than_undo_history,
                                "Block is too old for fork database: ${e}", ("e", e.to_detail_string()));
                        } catch (const graphene::chain::deferred_resize_exception &e) {
                            // Shared memory resize is deferred. Re-throw as network exception
                            // so the P2P layer knows this is transient and should not
                            // penalise the peer or mark the block as accepted.
                            wlog("Block ${n} deferred due to shared memory resize (head=${head}): ${e}",
                                 ("n", blk_msg.block.block_num())("head", head_block_num)("e", e.to_detail_string()));
                            FC_THROW_EXCEPTION(graphene::network::deferred_resize_exception,
                                "Shared memory resize deferred: ${e}", ("e", e.to_detail_string()));
                        } catch (const graphene::chain::unlinkable_block_exception &e) {
                            // Chain rejected block from a dead fork whose parent is not
                            // in fork_db.  Convert to network exception so the P2P layer
                            // can soft-ban the peer (block at/below head) or resync
                            // (block ahead of head).  Micro-fork blocks are NOT caught
                            // here — they have parents in fork_db and return false normally.
                            wlog("Block ${n} is from a dead fork (parent not in fork_db, head=${head}): ${e}",
                                 ("n", blk_msg.block.block_num())("head", head_block_num)("e", e.to_detail_string()));
                            FC_THROW_EXCEPTION(graphene::network::unlinkable_block_exception,
                                "Block from a dead fork: ${e}", ("e", e.to_detail_string()));
                        } catch (const graphene::network::unlinkable_block_exception &e) {
                            // translate to a graphene::network exception
                            elog("Error when pushing block, current head block is ${head}: ${e}", ("e", e.to_detail_string())("head", head_block_num));
                            FC_THROW_EXCEPTION(graphene::network::unlinkable_block_exception, "Error when pushing block: ${e}", ("e", e.to_detail_string()));
                        } catch (const fc::exception &e) {
                            elog("Error when pushing block, current head block is ${head}: ${e}", ("e", e.to_detail_string())("head", head_block_num));
                            throw;
                        }

                        return false;
                    } FC_CAPTURE_AND_RETHROW((blk_msg)(sync_mode))
                }

                void p2p_plugin_impl::handle_transaction(const trx_message &trx_msg) {
                    try {
                        chain.accept_transaction(trx_msg.trx);
                    } FC_CAPTURE_AND_RETHROW((trx_msg))
                }

                void p2p_plugin_impl::handle_message(const message &message_to_process) {
                    // not a transaction, not a block
                    //ilog("handle_message ${m}", ("m", message_to_process));
                    if(message_to_process.msg_type == core_message_type_enum::block_post_validation_message_type){
                        //get message_to_process as block_post_validation_message type
                        block_post_validation_message bpvl=block_post_validation_message(message_to_process.as<block_post_validation_message>());
                        //ilog("handle_message as bpvl ${m}", ("m", bpvl));

                        //get block_id from block_post_validation_message
                        block_id_type validate_block_id=block_id_type(bpvl.block_id);

                        graphene::protocol::digest_type::encoder enc;
                        fc::raw::pack(enc, chain.db().get_chain_id().str().append(validate_block_id.str()));

                        //recover public key from signature
                        fc::ecc::public_key recovered_public_key(bpvl.witness_signature, enc.result(), true);
                        //get signing_key from witness_account (guard against concurrent resize)
                        auto op_guard = chain.db().make_operation_guard();
                        fc::ecc::public_key w_signing_key=chain.db().get_witness_key(bpvl.witness_account);
                        if(chain.db().get_witness_key(bpvl.witness_account) == recovered_public_key){
                            op_guard.release();
                            //trigger db block validation
                            //ilog("recovered_public_key EQUAL to witness ${w}", ("w", bpvl.witness_account));
                            chain.db().apply_block_post_validation(validate_block_id,bpvl.witness_account);
                        }
                        else{//ignore
                            //ilog("recovered_public_key NOT EQUAL to witness ${w}", ("w", bpvl.witness_account));
                        }
                        return;
                    }
                    FC_THROW("Invalid Message Type");
                }

                std::vector<item_hash_t> p2p_plugin_impl::get_block_ids(
                        const std::vector<item_hash_t> &blockchain_synopsis, uint32_t &remaining_item_count,
                        uint32_t limit) {
                    try {
                        return chain.db().with_weak_read_lock([&]() {
                            vector<block_id_type> result;
                            remaining_item_count = 0;
                            if (chain.db().head_block_num() == 0) {
                                return result;
                            }

                            result.reserve(limit);
                            block_id_type last_known_block_id;

                            if (blockchain_synopsis.empty() ||
                                (blockchain_synopsis.size() == 1 && blockchain_synopsis[0] == block_id_type())) {
                                // peer has sent us an empty synopsis meaning they have no blocks.
                                // A bug in old versions would cause them to send a synopsis containing block 000000000
                                // when they had an empty blockchain, so pretend they sent the right thing here.
                                // do nothing, leave last_known_block_id set to zero
                            } else {
                                bool found_a_block_in_synopsis = false;

                                for (const item_hash_t &block_id_in_synopsis : boost::adaptors::reverse(
                                        blockchain_synopsis)) {
                                    if (block_id_in_synopsis == block_id_type()) {
                                        last_known_block_id = block_id_in_synopsis;
                                        found_a_block_in_synopsis = true;
                                        break;
                                    }
                                    bool known = chain.db().is_known_block(block_id_in_synopsis);
                                    bool included = known ? is_included_block(block_id_in_synopsis) : false;
                                    if (known && included) {
                                        last_known_block_id = block_id_in_synopsis;
                                        found_a_block_in_synopsis = true;
                                        break;
                                    }
                                    if (chain.db()._dlt_mode) {
                                        uint32_t syn_num = block_header::num_from_id(block_id_in_synopsis);
                                        block_id_type our_id = chain.db().find_block_id_for_num(syn_num);
                                        wlog("DLT mode: get_block_ids() synopsis block #${num} (${id}): "
                                             "is_known=${known}, is_included=${included}, "
                                             "our_id_for_num=${our_id}, match=${match}, "
                                             "head=${head}, dlt_range=[${dlt_s}..${dlt_e}]",
                                             ("num", syn_num)("id", block_id_in_synopsis)
                                             ("known", known)("included", included)
                                             ("our_id", our_id)("match", our_id == block_id_in_synopsis)
                                             ("head", chain.db().head_block_num())
                                             ("dlt_s", chain.db().get_dlt_block_log().start_block_num())
                                             ("dlt_e", chain.db().get_dlt_block_log().head_block_num()));
                                    }
                                }

                                if (!found_a_block_in_synopsis) {
                                    wlog("DLT mode: peer_is_on_an_unreachable_fork — could not match any of "
                                         "${n} synopsis entries. Synopsis: ${syn}",
                                         ("n", blockchain_synopsis.size())("syn", blockchain_synopsis));
                                    FC_THROW_EXCEPTION(graphene::network::peer_is_on_an_unreachable_fork, "Unable to provide a list of blocks starting at any of the blocks in peer's synopsis");
                                }
                            }

                            // Determine the starting block number for the response.
                            uint32_t start_num = block_header::num_from_id(last_known_block_id);

                            // In DLT mode, we can only serve blocks from our available range
                            // (dlt_block_log + fork_db). Don't advertise blocks we can't serve,
                            // otherwise the peer will request them, get item_not_available, and
                            // disconnect us with "You are missing a sync item you claim to have".
                            uint32_t effective_head = chain.db().head_block_num();
                            if (chain.db()._dlt_mode) {
                                uint32_t earliest = chain.db().earliest_available_block_num();
                                if (start_num < earliest) {
                                    dlog(CLOG_GRAY "DLT mode: get_block_ids() clamping start from ${old} to ${new} "
                                         "(earliest available block), head=${head}" CLOG_RESET,
                                         ("old", start_num)("new", earliest)("head", chain.db().head_block_num()));
                                    start_num = earliest;
                                }

                                // Also check for upper-bound gaps: in DLT mode there can be a gap
                                // between dlt_block_log end and fork_db start (e.g. after snapshot
                                // import when fork_db is empty).  Only advertise blocks we can
                                // actually serve from contiguous storage.
                                uint32_t dlt_end = chain.db().get_dlt_block_log().head_block_num();
                                uint32_t blog_end = 0;
                                auto blog_head = chain.db().get_block_log().head();
                                if (blog_head) {
                                    blog_end = blog_head->block_num();
                                }
                                uint32_t storage_end = std::max(dlt_end, blog_end);

                                if (start_num > storage_end) {
                                    // start_num is beyond all log storage — check if it's in fork_db
                                    auto test_block = chain.db().fetch_block_by_number(start_num);
                                    if (!test_block) {
                                        dlog(CLOG_GRAY "DLT mode: get_block_ids() cannot serve blocks from #${num} "
                                             "(not in dlt_block_log [${dlt_start}-${dlt_end}], block_log, or fork_db). "
                                             "Returning empty." CLOG_RESET,
                                             ("num", start_num)
                                             ("dlt_start", chain.db().get_dlt_block_log().start_block_num())
                                             ("dlt_end", dlt_end));
                                        return result;
                                    }
                                } else if (storage_end < effective_head) {
                                    // start_num is in log storage, but check if there's a gap
                                    // between storage end and fork_db
                                    auto gap_block = chain.db().fetch_block_by_number(storage_end + 1);
                                    if (!gap_block) {
                                        // Gap exists — only serve up to storage_end
                                        effective_head = storage_end;
                                        dlog(CLOG_GRAY "DLT mode: get_block_ids() clamping end to #${end} "
                                             "(gap between storage and fork_db), head=${head}" CLOG_RESET,
                                             ("end", effective_head)("head", chain.db().head_block_num()));
                                    }
                                }
                            }

                            for (uint32_t num = start_num;
                                 num <= effective_head && result.size() < limit; ++num) {
                                if (num > 0) {
                                    result.push_back(chain.db().get_block_id_for_num(num));
                                }
                            }

                            // Use effective_head (not head_block_num) for the
                            // remaining-item count.  In DLT mode, effective_head
                            // may be clamped to storage_end when there is a gap
                            // between the block log and fork_db.  Advertising
                            // blocks beyond effective_head causes an infinite
                            // sync loop: the peer keeps asking for more IDs, we
                            // return only the anchor block the peer already has,
                            // remaining>0, peer re-requests, etc.
                            if (!result.empty() &&
                                block_header::num_from_id(result.back()) < effective_head) {
                                remaining_item_count =
                                        effective_head - block_header::num_from_id(result.back());
                            }

                            if (chain.db()._dlt_mode) {
                                dlog(CLOG_GRAY "DLT mode: get_block_ids() returning ${n} block IDs "
                                     "(start=${start}, effective_head=${ehead}, head=${head}, "
                                     "earliest_available=${earliest}, dlt_end=${dlt_end})" CLOG_RESET,
                                     ("n", result.size())("start", start_num)
                                     ("ehead", effective_head)
                                     ("head", chain.db().head_block_num())
                                     ("earliest", chain.db().earliest_available_block_num())
                                     ("dlt_end", chain.db().get_dlt_block_log().head_block_num()));
                            }

                            return result;
                        });
                    } FC_CAPTURE_AND_RETHROW((blockchain_synopsis)(remaining_item_count)(limit))
                }

                message p2p_plugin_impl::get_item(const item_id &id) {
                    try {
                        if (id.item_type == network::block_message_type) {
                            return chain.db().with_weak_read_lock([&]() {
                                auto opt_block = chain.db().fetch_block_by_id(id.item_hash);
                                if (!opt_block) {
                                    if (chain.db()._dlt_mode) {
                                        uint32_t block_num = block_header::num_from_id(id.item_hash);
                                        uint32_t earliest = chain.db().earliest_available_block_num();
                                        uint32_t head = chain.db().head_block_num();
                                        // In DLT mode, block data may not be available for blocks
                                        // outside the dlt_block_log range. Log with full context.
                                        wlog("DLT mode: cannot serve block #${num} (${id}) — "
                                             "available block range: [${earliest}..${head}], "
                                             "dlt_block_log: [${dlt_start}..${dlt_end}]",
                                            ("num", block_num)("id", id.item_hash)
                                            ("earliest", earliest)("head", head)
                                            ("dlt_start", chain.db().get_dlt_block_log().start_block_num())
                                            ("dlt_end", chain.db().get_dlt_block_log().head_block_num()));
                                        FC_THROW_EXCEPTION(fc::key_not_found_exception, "");
                                    }
                                    elog("Couldn't find block ${id} -- corresponding ID in our chain is ${id2}",
                                         ("id", id.item_hash)("id2", chain.db().get_block_id_for_num(
                                                 block_header::num_from_id(id.item_hash))));
                                }
                                FC_ASSERT(opt_block.valid());
                                // ilog("Serving up block #${num}", ("num", opt_block->block_num()));
                                return block_message(std::move(*opt_block));
                            });
                        }
                        return chain.db().with_weak_read_lock([&]() {
                            return trx_message(chain.db().get_recent_transaction(id.item_hash));
                        });
                    } FC_CAPTURE_AND_RETHROW((id))
                }

                chain_id_type p2p_plugin_impl::get_chain_id() const {
                    return CHAIN_ID;
                }

                std::vector<item_hash_t> p2p_plugin_impl::get_blockchain_synopsis(const item_hash_t &reference_point,
                                                                                  uint32_t number_of_blocks_after_reference_point) {
                    try {
                        std::vector<item_hash_t> synopsis;
                        chain.db().with_weak_read_lock([&]() {
                            synopsis.reserve(30);
                            uint32_t high_block_num;
                            uint32_t non_fork_high_block_num;
                            uint32_t low_block_num = chain.db().last_non_undoable_block_num();
                            std::vector<block_id_type> fork_history;

                            if (reference_point != item_hash_t()) {
                                // the node is asking for a summary of the block chain up to a specified
                                // block, which may or may not be on a fork
                                // for now, assume it's not on a fork
                                if (is_included_block(reference_point)) {
                                    // reference_point is a block we know about and is on the main chain
                                    uint32_t reference_point_block_num = block_header::num_from_id(reference_point);
                                    assert(reference_point_block_num > 0);
                                    high_block_num = reference_point_block_num;
                                    non_fork_high_block_num = high_block_num;

                                    if (reference_point_block_num < low_block_num) {
                                        // we're on the same fork (at least as far as reference_point) but we've passed
                                        // reference point and could no longer undo that far if we diverged after that
                                        // block.  This should probably only happen due to a race condition where
                                        // the network thread calls this function, and then immediately pushes a bunch of blocks,
                                        // then the main thread finally processes this function.
                                        // with the current framework, there's not much we can do to tell the network
                                        // thread what our current head block is, so we'll just pretend that
                                        // our head is actually the reference point.
                                        // this *may* enable us to fetch blocks that we're unable to push, but that should
                                        // be a rare case (and correctly handled)
                                        low_block_num = reference_point_block_num;
                                    }
                                } else {
                                    // block is a block we know about, but it is on a fork
                                    try {
                                        fork_history = chain.db().get_block_ids_on_fork(reference_point);
                                        // returns a vector where the last element is the common ancestor with the preferred chain,
                                        // and the first element is the reference point you passed in
                                        assert(fork_history.size() >= 2);

                                        if (fork_history.front() != reference_point) {
                                            edump((fork_history)(reference_point));
                                            assert(fork_history.front() == reference_point);
                                        }
                                        block_id_type last_non_fork_block = fork_history.back();
                                        fork_history.pop_back();  // remove the common ancestor
                                        boost::reverse(fork_history);

                                        if (last_non_fork_block ==
                                            block_id_type()) { // if the fork goes all the way back to genesis (does viz's fork db allow this?)
                                            non_fork_high_block_num = 0;
                                        } else {
                                            non_fork_high_block_num = block_header::num_from_id(last_non_fork_block);
                                        }

                                        high_block_num = non_fork_high_block_num + fork_history.size();
                                        assert(high_block_num == block_header::num_from_id(fork_history.back()));
                                    } catch (const fc::exception &e) {
                                        // unable to get fork history for some reason.  maybe not linked?
                                        // we can't return a synopsis of its chain
                                        elog("Unable to construct a blockchain synopsis for reference hash ${hash}: ${exception}",
                                             ("hash", reference_point)("exception", e));
                                        throw;
                                    }
                                    if (non_fork_high_block_num < low_block_num) {
                                        wlog("Unable to generate a usable synopsis because the peer we're generating it for forked too long ago "
                                                     "(our chains diverge after block #${non_fork_high_block_num} but only undoable to block #${low_block_num})",
                                             ("low_block_num", low_block_num)("non_fork_high_block_num",
                                                                              non_fork_high_block_num));
                                        FC_THROW_EXCEPTION(graphene::network::block_older_than_undo_history, "Peer is are on a fork I'm unable to switch to");
                                    }
                                }
                            } else {
                                // no reference point specified, summarize the whole block chain
                                high_block_num = chain.db().head_block_num();
                                non_fork_high_block_num = high_block_num;
                                if (high_block_num == 0) {
                                    return;
                                } // we have no blocks
                            }

                            if (low_block_num == 0) {
                                low_block_num = 1;
                            }

                            // at this point:
                            // low_block_num is the block before the first block we can undo,
                            // non_fork_high_block_num is the block before the fork (if the peer is on a fork, or otherwise it is the same as high_block_num)
                            // high_block_num is the block number of the reference block, or the end of the chain if no reference provided

                            // true_high_block_num is the ending block number after the network code appends any item ids it
                            // knows about that we don't
                            uint32_t true_high_block_num = high_block_num + number_of_blocks_after_reference_point;
                            do {
                                // for each block in the synopsis, figure out where to pull the block id from.
                                // if it's <= non_fork_high_block_num, we grab it from the main blockchain;
                                // if it's not, we pull it from the fork history
                                if (low_block_num <= non_fork_high_block_num) {
                                    synopsis.push_back(chain.db().get_block_id_for_num(low_block_num));
                                } else {
                                    synopsis.push_back(fork_history[low_block_num - non_fork_high_block_num - 1]);
                                }
                                low_block_num += (true_high_block_num - low_block_num + 2) / 2;
                            } while (low_block_num <= high_block_num);

                            //idump((synopsis));
                            if (chain.db()._dlt_mode) {
                                dlog(CLOG_GRAY "DLT mode: get_blockchain_synopsis() returning ${n} entries, "
                                     "low=${low}, high=${high}, head=${head}, LIB=${lib}, "
                                     "earliest_available=${earliest}" CLOG_RESET,
                                     ("n", synopsis.size())("low", chain.db().last_non_undoable_block_num())
                                     ("high", high_block_num)("head", chain.db().head_block_num())
                                     ("lib", chain.db().last_non_undoable_block_num())
                                     ("earliest", chain.db().earliest_available_block_num()));
                            }
                            return;
                        });

                        return synopsis;
                    } FC_LOG_AND_RETHROW()
                }

                void p2p_plugin_impl::sync_status(uint32_t item_type, uint32_t item_count) {
                    // any status reports to GUI go here
                }

                void p2p_plugin_impl::connection_count_changed(uint32_t c) {
                    // any status reports to GUI go here
                }

                uint32_t p2p_plugin_impl::get_block_number(const item_hash_t &block_id) {
                    try {
                        return block_header::num_from_id(block_id);
                    } FC_CAPTURE_AND_RETHROW((block_id))
                }

                fc::time_point_sec p2p_plugin_impl::get_block_time(const item_hash_t &block_id) {
                    try {
                        return chain.db().with_weak_read_lock([&]() {
                            auto opt_block = chain.db().fetch_block_by_id(block_id);
                            if (opt_block.valid()) {
                                return opt_block->timestamp;
                            }
                            return fc::time_point_sec::min();
                        });
                    } FC_CAPTURE_AND_RETHROW((block_id))
                }

                item_hash_t p2p_plugin_impl::get_head_block_id() const {
                    try {
                        return chain.db().with_weak_read_lock([&]() {
                            return chain.db().head_block_id();
                        });
                    } FC_CAPTURE_AND_RETHROW()
                }

                uint32_t p2p_plugin_impl::estimate_last_known_fork_from_git_revision_timestamp(uint32_t) const {
                    return 0; // there are no forks in viz
                }

                void p2p_plugin_impl::error_encountered(const string &message, const fc::oexception &error) {
                    // notify GUI or something cool
                }

                fc::time_point_sec p2p_plugin_impl::get_blockchain_now() {
                    try {
                        return fc::time_point::now();
                    } FC_CAPTURE_AND_RETHROW()
                }

                bool p2p_plugin_impl::is_included_block(const block_id_type &block_id) {
                    try {
                        return chain.db().with_weak_read_lock([&]() {
                            uint32_t block_num = block_header::num_from_id(block_id);
                            block_id_type block_id_in_preferred_chain = chain.db().get_block_id_for_num(block_num);
                            return block_id == block_id_in_preferred_chain;
                        });
                    } FC_CAPTURE_AND_RETHROW()
                }

                ////////////////////////////// End node_delegate Implementation //////////////////////////////

                void p2p_plugin_impl::p2p_stats_task() {
                    if (!stats_enabled || !node) {
                        return;
                    }
                    try {
                        auto peers = node->get_connected_peers();
                        std::map<std::string, uint64_t> new_bytes_map;
                        if (peers.empty()) {
                            ilog(CLOG_CYAN "P2P stats: no connected peers" CLOG_RESET);
                        } else {
                            ilog(CLOG_CYAN "P2P stats: ${n} connected peer(s)" CLOG_RESET, ("n", peers.size()));
                            for (const auto &peer_info : peers) {
                                std::string ip;
                                uint16_t port = 0;
                                try {
                                    ip = static_cast<std::string>(peer_info.host.get_address());
                                    port = peer_info.host.port();
                                } catch (...) {
                                    ip = "(unknown)";
                                }

                                int64_t latency_ms = -1;
                                uint64_t bytes_recv = 0;
                                bool is_blocked = false;
                                std::string blocked_reason;

                                auto it_lat = peer_info.info.find("latency_ms");
                                if (it_lat != peer_info.info.end()) {
                                    latency_ms = it_lat->value().as_int64();
                                }
                                auto it_byt = peer_info.info.find("bytesrecv");
                                if (it_byt != peer_info.info.end()) {
                                    bytes_recv = it_byt->value().as_uint64();
                                }
                                auto it_blk = peer_info.info.find("is_blocked");
                                if (it_blk != peer_info.info.end()) {
                                    is_blocked = it_blk->value().as_bool();
                                }
                                auto it_rsn = peer_info.info.find("blocked_reason");
                                if (it_rsn != peer_info.info.end()) {
                                    blocked_reason = it_rsn->value().as_string();
                                }

                                std::string addr_key = ip + ":" + std::to_string(port);
                                uint64_t bytes_delta = bytes_recv;
                                auto prev_it = _stats_bytes_received_last.find(addr_key);
                                if (prev_it != _stats_bytes_received_last.end()) {
                                    bytes_delta = (bytes_recv >= prev_it->second)
                                        ? bytes_recv - prev_it->second
                                        : bytes_recv;
                                }
                                new_bytes_map[addr_key] = bytes_recv;

                                ilog(CLOG_CYAN "P2P peer | ip: ${ip} | port: ${port} | latency: ${lat}ms | bytes_in: ${bin} | blocked: ${bl} | reason: ${r}" CLOG_RESET,
                                    ("ip", ip)("port", (int)port)("lat", latency_ms)("bin", bytes_delta)("bl", is_blocked)("r", blocked_reason));
                            }
                        }
                        _stats_bytes_received_last = std::move(new_bytes_map);

                        // Dump potential peer database: shows all known peers including
                        // failed, rejected, and banned ones that are no longer connected.
                        // This is critical for debugging post-snapshot sync failures.
                        auto potential_peers = node->get_potential_peers();
                        uint32_t failed_count = 0;
                        for (const auto &pp : potential_peers) {
                            if (pp.last_connection_disposition != graphene::network::last_connection_succeeded &&
                                pp.last_connection_disposition != graphene::network::never_attempted_to_connect) {
                                ++failed_count;
                                std::string disposition;
                                switch (pp.last_connection_disposition) {
                                    case graphene::network::last_connection_failed: disposition = "failed"; break;
                                    case graphene::network::last_connection_rejected: disposition = "rejected"; break;
                                    case graphene::network::last_connection_handshaking_failed: disposition = "handshake_failed"; break;
                                    default: disposition = "unknown"; break;
                                }
                                std::string error_str;
                                if (pp.last_error) {
                                    error_str = pp.last_error->to_string();
                                }
                                dlog(CLOG_CYAN "P2P peer_db | ${ep} | status: ${disp} | last_attempt: ${time} | fails: ${f} | error: ${err}" CLOG_RESET,
                                    ("ep", pp.endpoint)("disp", disposition)
                                    ("time", pp.last_connection_attempt_time.to_iso_string())
                                    ("f", pp.number_of_failed_connection_attempts)
                                    ("err", error_str));
                            }
                        }
                        if (failed_count > 0) {
                            dlog(CLOG_CYAN "P2P peer_db: ${n} peers with failed/rejected status (of ${total} total)" CLOG_RESET,
                                ("n", failed_count)("total", potential_peers.size()));
                        }

                        // Block storage diagnostics: show what ranges are available for serving peers
                        chain.db().with_weak_read_lock([&]() {
                            uint32_t head = chain.db().head_block_num();
                            uint32_t lib = chain.db().get_dynamic_global_properties().last_irreversible_block_num;
                            uint32_t earliest = chain.db().earliest_available_block_num();

                            // DLT block log range
                            uint32_t dlt_start = chain.db().get_dlt_block_log().start_block_num();
                            uint32_t dlt_end = chain.db().get_dlt_block_log().head_block_num();

                            // Regular block log
                            uint32_t blog_end = 0;
                            auto blog_head = chain.db().get_block_log().head();
                            if (blog_head) {
                                blog_end = blog_head->block_num();
                            }

                            // Fork database
                            const auto& fork_db = chain.db().get_fork_db();
                            uint32_t fork_head = fork_db.head() ? fork_db.head()->num : 0;
                            size_t fork_linked = fork_db.linked_size();
                            size_t fork_unlinked = fork_db.unlinked_size();
                            uint32_t fork_linked_min = fork_db.linked_min_block_num();
                            uint32_t fork_linked_max = fork_db.linked_max_block_num();
                            uint32_t fork_unlinked_min = fork_db.unlinked_min_block_num();
                            uint32_t fork_unlinked_max = fork_db.unlinked_max_block_num();

                            ilog(CLOG_CYAN "Block storage | head: ${head} | LIB: ${lib} | earliest: ${earliest} | "
                                 "dlt_log: [${dlt_s}..${dlt_e}] | block_log_end: ${blog} | "
                                 "fork_db: head=${fh}, linked=${fl} [${fl_min}..${fl_max}], "
                                 "unlinked=${fu} [${fu_min}..${fu_max}] | "
                                 "dlt_mode: ${dlt} | dlt_resizes: ${resizes}" CLOG_RESET,
                                 ("head", head)("lib", lib)("earliest", earliest)
                                 ("dlt_s", dlt_start)("dlt_e", dlt_end)("blog", blog_end)
                                 ("fh", fork_head)
                                 ("fl", fork_linked)("fl_min", fork_linked_min)("fl_max", fork_linked_max)
                                 ("fu", fork_unlinked)("fu_min", fork_unlinked_min)("fu_max", fork_unlinked_max)
                                 ("dlt", chain.db()._dlt_mode)
                                 ("resizes", chain.db().get_dlt_block_log().resize_count()));

                            // Detect gap between dlt_block_log end and fork_db start
                            if (chain.db()._dlt_mode && dlt_end > 0 && fork_linked_min > 0
                                && fork_linked_min > dlt_end + 1) {
                                ilog(CLOG_ORANGE "DLT COVERAGE GAP: dlt_block_log ends at #${dlt_end}, "
                                     "fork_db starts at #${fork_min}. Blocks ${gap_start}..${gap_end} "
                                     "are NOT available for P2P serving!" CLOG_RESET,
                                     ("dlt_end", dlt_end)("fork_min", fork_linked_min)
                                     ("gap_start", dlt_end + 1)("gap_end", fork_linked_min - 1));
                            }
                        });

                        // Periodically verify dlt_block_log mapping consistency.
                        // Detects & heals stale mapped_file.size().
                        if (chain.db()._dlt_mode) {
                            chain.db().get_dlt_block_log().verify_mapping();

                            // Full integrity scan: walk all blocks, report gaps
                            auto gaps = chain.db().get_dlt_block_log().verify_continuity();
                            if (!gaps.empty()) {
                                std::string gap_str;
                                size_t shown = 0;
                                for (auto g : gaps) {
                                    if (shown > 0) gap_str += ", ";
                                    gap_str += std::to_string(g);
                                    if (++shown >= 20) {
                                        gap_str += "... (" + std::to_string(gaps.size()) + " total)";
                                        break;
                                    }
                                }
                                ilog(CLOG_ORANGE "DLT INTEGRITY WARNING: ${count} gaps found in dlt_block_log! "
                                     "Missing blocks: ${gaps}" CLOG_RESET,
                                     ("count", gaps.size())("gaps", gap_str));
                            }
                        }
                    } catch (const fc::exception &e) {
                        wlog("Exception in P2P stats task: ${e}", ("e", e.to_detail_string()));
                    } catch (...) {
                        wlog("Unknown exception in P2P stats task");
                    }

                    if (stats_enabled) {
                        _stats_task_done = fc::schedule(
                            [this]() { p2p_stats_task(); },
                            fc::time_point::now() + fc::seconds(stats_interval_seconds),
                            "p2p_stats_task"
                        );
                    }
                }

                void p2p_plugin_impl::stale_sync_check_task() {
                    if (!_stale_sync_enabled || !node) {
                        return;
                    }
                    try {
                        auto now = fc::time_point::now();
                        auto elapsed = now - _last_block_received_time;
                        auto timeout = fc::seconds(_stale_sync_timeout_seconds);

                        if (elapsed > timeout) {
                            uint32_t head_block = 0;
                            uint32_t lib_num = 0;
                            chain.db().with_weak_read_lock([&]() {
                                head_block = chain.db().head_block_num();
                                lib_num = chain.db().get_dynamic_global_properties().last_irreversible_block_num;
                            });

                            wlog("Stale sync detected: no blocks received for ${s}s (head: ${h}, LIB: ${lib}). "
                                 "Resetting sync from last irreversible block and reconnecting seed peers.",
                                 ("s", _stale_sync_timeout_seconds)("h", head_block)("lib", lib_num));

                            // Reset sync from last irreversible block
                            if (lib_num > 0 && node) {
                                block_id_type lib_block_id;
                                chain.db().with_weak_read_lock([&]() {
                                    lib_block_id = chain.db().get_block_id_for_num(lib_num);
                                });
                                node->sync_from(item_id(graphene::network::block_message_type, lib_block_id),
                                                std::vector<uint32_t>());
                                ilog("Reset P2P sync from LIB block #${n}", ("n", lib_num));

                                // Force resync with all currently connected peers
                                node->resync();
                            }

                            // Reconnect all seed nodes (add_node resets the retry timer,
                            // connect_to_endpoint initiates connection if not already connected)
                            for (const auto &seed : seeds) {
                                try {
                                    ilog("Reconnecting seed node ${s}", ("s", seed));
                                    node->add_node(seed);
                                    node->connect_to_endpoint(seed);
                                } catch (const fc::exception &e) {
                                    wlog("Failed to reconnect seed node ${s}: ${e}",
                                         ("s", seed)("e", e.to_detail_string()));
                                }
                            }

                            // Reset timer to avoid immediate retry
                            _last_block_received_time = fc::time_point::now();
                        }
                    } catch (const fc::exception &e) {
                        wlog("Exception in stale sync check task: ${e}", ("e", e.to_detail_string()));
                    } catch (...) {
                        wlog("Unknown exception in stale sync check task");
                    }

                    if (_stale_sync_enabled) {
                        _stale_sync_task_done = fc::schedule(
                            [this]() { stale_sync_check_task(); },
                            fc::time_point::now() + fc::seconds(30),
                            "stale_sync_check_task"
                        );
                    }
                }

            } // detail

            p2p_plugin::p2p_plugin() {
            }

            p2p_plugin::~p2p_plugin() {
            }

            void p2p_plugin::set_program_options(boost::program_options::options_description &cli, boost::program_options::options_description &cfg) {
                cfg.add_options()
                    ("p2p-endpoint", boost::program_options::value<string>()->implicit_value("127.0.0.1:9876"),
                        "The local IP address and port to listen for incoming connections.")
                    ("p2p-max-connections", boost::program_options::value<uint32_t>(),
                        "Maxmimum number of incoming connections on P2P endpoint.")
                    ("seed-node", boost::program_options::value<vector<string>>()->composing(),
                        "The IP address and port of a remote peer to sync with. Deprecated in favor of p2p-seed-node.")
                    ("p2p-seed-node", boost::program_options::value<vector<string>>()->composing(),
                        "The IP address and port of a remote peer to sync with.")
                    ("p2p-stats-enabled", boost::program_options::value<bool>()->default_value(true),
                        "Enable periodic logging of P2P peer statistics (ip, port, latency, bytes in, blocked status).")
                    ("p2p-stats-interval", boost::program_options::value<uint32_t>()->default_value(300),
                        "Interval in seconds between P2P peer statistics dumps (default: 300 = 5 minutes).")
                    ("p2p-stale-sync-detection", boost::program_options::value<bool>()->default_value(false),
                        "Enable stale sync detection: when no blocks are received for the configured timeout, "
                        "reset sync from last irreversible block and reconnect seed peers (default: false).")
                    ("p2p-stale-sync-timeout-seconds", boost::program_options::value<uint32_t>()->default_value(120),
                        "Timeout in seconds after which stale sync detection triggers recovery action (default: 120 = 2 minutes).");
                cli.add_options()
                    ("force-validate", boost::program_options::bool_switch()->default_value(false),
                        "Force validation of all transactions. Deprecated in favor of p2p-force-validate")
                    ("p2p-force-validate", boost::program_options::bool_switch()->default_value(false),
                        "Force validation of all transactions.");
            }

            void p2p_plugin::plugin_initialize(const boost::program_options::variables_map &options) {
                my.reset(new detail::p2p_plugin_impl(appbase::app().get_plugin<chain::plugin>()));

                if (options.count("p2p-endpoint")) {
                    my->endpoint = fc::ip::endpoint::from_string(options.at("p2p-endpoint").as<string>());
                }

                my->user_agent = "Graphene Reference Implementation";

                if (options.count("p2p-max-connections")) {
                    my->max_connections = options.at("p2p-max-connections").as<uint32_t>();
                }

                if (options.count("seed-node") || options.count("p2p-seed-node")) {
                    vector<string> seeds;
                    if (options.count("seed-node")) {
                        wlog("Option seed-node is deprecated in favor of p2p-seed-node");
                        auto s = options.at("seed-node").as<vector<string>>();
                        seeds.insert(seeds.end(), s.begin(), s.end());
                    }

                    if (options.count("p2p-seed-node")) {
                        auto s = options.at("p2p-seed-node").as<vector<string>>();
                        seeds.insert(seeds.end(), s.begin(), s.end());
                    }

                    for (const string &endpoint_string : seeds) {
                        try {
                            auto eps = appbase::app().resolve_string_to_ip_endpoints(endpoint_string);
                            for (auto& ep: eps) {
                                my->seeds.push_back(fc::ip::endpoint(ep.address().to_string(), ep.port()));
                            }
                        } catch (const fc::exception &e) {
                            wlog("caught exception ${e} while adding seed node ${endpoint}",
                                 ("e", e.to_detail_string())("endpoint", endpoint_string));
                        }
                    }
                }

                my->force_validate = options.at("p2p-force-validate").as<bool>();

                if (!my->force_validate && options.at("force-validate").as<bool>()) {
                    wlog("Option force-validate is deprecated in favor of p2p-force-validate");
                    my->force_validate = true;
                }

                if (options.count("p2p-stats-enabled")) {
                    my->stats_enabled = options.at("p2p-stats-enabled").as<bool>();
                }

                if (options.count("p2p-stats-interval")) {
                    uint32_t interval = options.at("p2p-stats-interval").as<uint32_t>();
                    if (interval > 0) {
                        my->stats_interval_seconds = interval;
                    } else {
                        wlog("p2p-stats-interval must be > 0, using default of 300 seconds");
                    }
                }

                if (options.count("p2p-stale-sync-detection")) {
                    my->_stale_sync_enabled = options.at("p2p-stale-sync-detection").as<bool>();
                }

                if (options.count("p2p-stale-sync-timeout-seconds")) {
                    uint32_t stale_timeout = options.at("p2p-stale-sync-timeout-seconds").as<uint32_t>();
                    if (stale_timeout > 0) {
                        my->_stale_sync_timeout_seconds = stale_timeout;
                    } else {
                        wlog("p2p-stale-sync-timeout-seconds must be > 0, using default of 120 seconds");
                    }
                }
            }

            void p2p_plugin::plugin_startup() {
                my->p2p_thread.async([this] {
                    my->node.reset(new graphene::network::node(my->user_agent));
                    my->node->load_configuration(app().data_dir() / "p2p");
                    my->node->set_node_delegate(&(*my));

                    if (my->endpoint) {
                        ilog("Configuring P2P to listen at ${ep}", ("ep", my->endpoint));
                        my->node->listen_on_endpoint(*my->endpoint, true);
                    }

                    for (const auto &seed : my->seeds) {
                        ilog("P2P adding seed node ${s}", ("s", seed));
                        my->node->add_node(seed);
                        my->node->connect_to_endpoint(seed);
                    }

                    if (my->max_connections) {
                        ilog("Setting p2p max connections to ${n}", ("n", my->max_connections));
                        fc::variant_object node_param = fc::variant_object("maximum_number_of_connections",
                                                                           fc::variant(my->max_connections));
                        my->node->set_advanced_node_parameters(node_param);
                    }

                    my->node->listen_to_p2p_network();
                    my->node->connect_to_p2p_network();

                    // Register trusted snapshot peer IPs for reduced soft-ban (5 min vs 1 hour)
                    auto* snap_plug = appbase::app().find_plugin<graphene::plugins::snapshot::snapshot_plugin>();
                    if (snap_plug) {
                        auto trusted_eps = snap_plug->get_trusted_snapshot_peers();
                        if (!trusted_eps.empty()) {
                            ilog("Registering ${n} trusted snapshot peer(s) for reduced P2P soft-ban", ("n", trusted_eps.size()));
                            my->node->set_trusted_peer_endpoints(trusted_eps);
                        }
                    }

                    // === Startup block storage diagnostics (before sync) ===
                    my->chain.db().with_weak_read_lock([&]() {
                        auto& db = my->chain.db();
                        uint32_t head = db.head_block_num();
                        uint32_t lib = db.get_dynamic_global_properties().last_irreversible_block_num;
                        uint32_t earliest = db.earliest_available_block_num();

                        uint32_t dlt_start = db.get_dlt_block_log().start_block_num();
                        uint32_t dlt_end = db.get_dlt_block_log().head_block_num();

                        uint32_t blog_end = 0;
                        auto blog_head = db.get_block_log().head();
                        if (blog_head) {
                            blog_end = blog_head->block_num();
                        }

                        const auto& fork_db = db.get_fork_db();
                        uint32_t fork_head = fork_db.head() ? fork_db.head()->num : 0;
                        size_t fork_linked = fork_db.linked_size();
                        uint32_t fork_linked_min = fork_db.linked_min_block_num();
                        uint32_t fork_linked_max = fork_db.linked_max_block_num();
                        size_t fork_unlinked = fork_db.unlinked_size();
                        uint32_t fork_unlinked_min = fork_db.unlinked_min_block_num();
                        uint32_t fork_unlinked_max = fork_db.unlinked_max_block_num();

                        ilog(CLOG_CYAN "=== STARTUP block storage state (before sync) ===" CLOG_RESET);
                        ilog(CLOG_CYAN "  head: ${head} | LIB: ${lib} | earliest: ${earliest}" CLOG_RESET,
                             ("head", head)("lib", lib)("earliest", earliest));
                        ilog(CLOG_CYAN "  dlt_block_log: [${s}..${e}] (${n} blocks) | block_log_end: ${blog}" CLOG_RESET,
                             ("s", dlt_start)("e", dlt_end)
                             ("n", db.get_dlt_block_log().num_blocks())("blog", blog_end));
                        ilog(CLOG_CYAN "  fork_db: head=${fh}, linked=${fl} [${fl_min}..${fl_max}], "
                             "unlinked=${fu} [${fu_min}..${fu_max}]" CLOG_RESET,
                             ("fh", fork_head)
                             ("fl", fork_linked)("fl_min", fork_linked_min)("fl_max", fork_linked_max)
                             ("fu", fork_unlinked)("fu_min", fork_unlinked_min)("fu_max", fork_unlinked_max));
                        ilog(CLOG_CYAN "  dlt_mode: ${dlt} | dlt_resizes: ${r}" CLOG_RESET,
                             ("dlt", db._dlt_mode)("r", db.get_dlt_block_log().resize_count()));

                        // Detect gap between dlt_block_log and fork_db at startup
                        if (db._dlt_mode && dlt_end > 0 && fork_linked_min > 0
                            && fork_linked_min > dlt_end + 1) {
                            ilog(CLOG_ORANGE "  STARTUP GAP: dlt_block_log ends at #${dlt_end}, "
                                 "fork_db starts at #${fork_min}. "
                                 "Blocks ${gap_s}..${gap_e} are missing!" CLOG_RESET,
                                 ("dlt_end", dlt_end)("fork_min", fork_linked_min)
                                 ("gap_s", dlt_end + 1)("gap_e", fork_linked_min - 1));
                        }

                        // Full integrity scan at startup
                        if (db._dlt_mode && dlt_end > 0) {
                            auto gaps = db.get_dlt_block_log().verify_continuity();
                            if (!gaps.empty()) {
                                std::string gap_str;
                                size_t shown = 0;
                                for (auto g : gaps) {
                                    if (shown > 0) gap_str += ", ";
                                    gap_str += std::to_string(g);
                                    if (++shown >= 20) {
                                        gap_str += "... (" + std::to_string(gaps.size()) + " total)";
                                        break;
                                    }
                                }
                                ilog(CLOG_ORANGE "  STARTUP INTEGRITY: ${count} gaps in dlt_block_log! "
                                     "Missing: ${gaps}" CLOG_RESET,
                                     ("count", gaps.size())("gaps", gap_str));
                            } else {
                                ilog(CLOG_CYAN "  dlt_block_log integrity: OK (all blocks readable)" CLOG_RESET);
                            }
                        }
                        ilog(CLOG_CYAN "=== END startup diagnostics ===" CLOG_RESET);
                    });

                    block_id_type block_id;
                    my->chain.db().with_weak_read_lock([&]() {
                        block_id = my->chain.db().head_block_id();
                    });
                    my->node->sync_from(item_id(graphene::network::block_message_type, block_id),
                                        std::vector<uint32_t>());
                    ilog("P2P node listening at ${ep}", ("ep", my->node->get_actual_listening_endpoint()));

                    if (my->stats_enabled) {
                        ilog("P2P stats logging enabled, interval: ${s} seconds", ("s", my->stats_interval_seconds));
                        my->_stats_task_done = fc::schedule(
                            [this]() { my->p2p_stats_task(); },
                            fc::time_point::now() + fc::seconds(my->stats_interval_seconds),
                            "p2p_stats_task"
                        );
                    }

                    if (my->_stale_sync_enabled) {
                        my->_last_block_received_time = fc::time_point::now();
                        ilog("P2P stale sync detection enabled, timeout: ${s}s", ("s", my->_stale_sync_timeout_seconds));
                        my->_stale_sync_task_done = fc::schedule(
                            [this]() { my->stale_sync_check_task(); },
                            fc::time_point::now() + fc::seconds(30),
                            "stale_sync_check_task"
                        );
                    }
                }).wait();
                ilog("P2P Plugin started");
            }

            void p2p_plugin::plugin_shutdown() {
                ilog("Shutting down P2P Plugin");
                if (my->stats_enabled && my->_stats_task_done.valid()) {
                    try {
                        my->_stats_task_done.cancel_and_wait("p2p_plugin::plugin_shutdown() stats");
                    } catch (const fc::exception &e) {
                        wlog("Exception canceling P2P stats task: ${e}", ("e", e.to_detail_string()));
                    } catch (...) {
                        wlog("Unknown exception canceling P2P stats task");
                    }
                }
                if (my->_stale_sync_enabled && my->_stale_sync_task_done.valid()) {
                    try {
                        my->_stale_sync_task_done.cancel_and_wait("p2p_plugin::plugin_shutdown() stale_sync");
                    } catch (const fc::exception &e) {
                        wlog("Exception canceling stale sync check task: ${e}", ("e", e.to_detail_string()));
                    } catch (...) {
                        wlog("Unknown exception canceling stale sync check task");
                    }
                }
                my->node->close();
                my->p2p_thread.quit();
                my->node.reset();
            }

            void p2p_plugin::broadcast_block(const protocol::signed_block &block) {
                ulog("Broadcasting block #${n}", ("n", block.block_num()));
                my->node->broadcast(block_message(block));
            }

            void p2p_plugin::broadcast_block_post_validation(const network::block_id_type block_id,
                    const std::string &witness_account,
                    const protocol::signature_type &witness_signature) {
                if(!my->chain.db().has_hardfork(CHAIN_HARDFORK_11)){
                    return;
                }
                //ilog("Broadcasting block post validation ${n} ${w} ${s}", ("n", block_id)("w", witness_account)("s", witness_signature));
                my->node->broadcast(block_post_validation_message(block_id,witness_account,witness_signature));
                //apply block post validation after broadcast
                my->chain.db().apply_block_post_validation(block_id,witness_account);
            }

            void p2p_plugin::broadcast_transaction(const protocol::signed_transaction &tx) {
                ulog("Broadcasting tx #${n}", ("id", tx.id()));
                my->node->broadcast(trx_message(tx));
            }

            void p2p_plugin::set_block_production(bool producing_blocks) {
                my->block_producer = producing_blocks;
            }

            void p2p_plugin::resync_from_lib() {
                try {
                    auto& db = my->chain.db();
                    uint32_t head_num = 0;
                    uint32_t lib_num = 0;

                    db.with_weak_read_lock([&]() {
                        head_num = db.head_block_num();
                        lib_num = db.get_dynamic_global_properties().last_irreversible_block_num;
                    });

                    if (lib_num == 0 || head_num <= lib_num) {
                        wlog("resync_from_lib: nothing to pop (head=${h}, LIB=${lib})",
                             ("h", head_num)("lib", lib_num));
                        return;
                    }

                    wlog("MINORITY FORK RECOVERY: popping ${n} blocks from head=${h} back to LIB=${lib}",
                         ("n", head_num - lib_num)("h", head_num)("lib", lib_num));

                    // Pop all reversible blocks back to LIB and reset fork_db.
                    // This replicates what undo_all() does on node restart.
                    db.with_strong_write_lock([&]() {
                        while (db.head_block_num() > lib_num) {
                            db.pop_block();
                        }
                        db.clear_pending();
                        db.get_fork_db().reset();

                        // Re-seed fork_db with LIB block so P2P sync can link new blocks
                        auto lib_block = db.fetch_block_by_number(lib_num);
                        if (lib_block.valid()) {
                            db.get_fork_db().start_block(*lib_block);
                        }
                    });

                    ilog("MINORITY FORK RECOVERY: state rolled back to LIB=${lib}. Re-initiating P2P sync.",
                         ("lib", lib_num));

                    // Re-trigger P2P sync from LIB
                    if (my->node) {
                        block_id_type lib_block_id;
                        db.with_weak_read_lock([&]() {
                            lib_block_id = db.head_block_id();
                        });
                        my->node->sync_from(item_id(graphene::network::block_message_type, lib_block_id),
                                            std::vector<uint32_t>());
                        my->node->resync();

                        // Reconnect seed nodes to ensure we have peers to sync from
                        for (const auto &seed : my->seeds) {
                            try {
                                my->node->add_node(seed);
                                my->node->connect_to_endpoint(seed);
                            } catch (const fc::exception &e) {
                                wlog("Failed to reconnect seed ${s}: ${e}",
                                     ("s", seed)("e", e.to_detail_string()));
                            }
                        }
                    }

                    // Reset stale sync timer
                    my->_last_block_received_time = fc::time_point::now();

                } catch (const fc::exception &e) {
                    elog("resync_from_lib failed: ${e}", ("e", e.to_detail_string()));
                } catch (...) {
                    elog("resync_from_lib failed with unknown exception");
                }
            }

        }
    }
} // namespace graphene::plugins::p2p
