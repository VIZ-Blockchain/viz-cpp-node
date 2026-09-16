#include <graphene/chain/fork_database.hpp>

#include <graphene/chain/database_exceptions.hpp>

namespace graphene {
    namespace chain {

        fork_database::fork_database() {
        }

        void fork_database::reset() {
            _head.reset();
            _index.clear();
            _unlinked_index.clear();
        }

        void fork_database::pop_block() {
            FC_ASSERT(_head, "cannot pop an empty fork database");
            auto prev = _head->prev.lock();
            FC_ASSERT(prev, "popping head block would leave fork DB empty");
            _head = prev;
        }

        void fork_database::start_block(signed_block b) {
            auto item = std::make_shared<fork_item>(std::move(b));
            _index.insert(item);
            _head = item;
        }

        void fork_database::insert_as_base(signed_block b) {
            auto &id_index = _index.get<block_id>();
            auto existing = id_index.find(b.id());
            if (existing != id_index.end()) {
                // Already in linked index — still repair any child blocks whose
                // prev pointer is null (inserted via start_block before we knew
                // this ancestor).
                _repair_child_prev_links(*existing);
                return;
            }
            auto item = std::make_shared<fork_item>(std::move(b));
            // Insert without parent-chain check — this block is confirmed on our
            // main chain, we just don't have its parent in fork_db (e.g., the
            // snapshot LIB block whose data is absent from the DLT block log).
            _index.insert(item);
            // Link any blocks in _unlinked_index that were waiting for this parent.
            _push_next(item);
            // Repair null prev pointers on child blocks already in _index
            // (e.g., the block inserted via start_block that should follow this one).
            _repair_child_prev_links(item);
        }

        void fork_database::insert_anchor_block(const block_id_type &id, uint32_t block_num) {
            auto &id_index = _index.get<block_id>();
            auto existing = id_index.find(id);
            if (existing != id_index.end()) {
                return;  // already present (e.g., from start_block or a real block push)
            }
            // Construct a minimal fork_item with the correct id and num but
            // default-constructed (empty) signed_block data.  The fork_item
            // constructor takes a signed_block and derives num/id from it,
            // but we need to override those fields since our data is empty.
            signed_block empty_block;
            auto item = std::make_shared<fork_item>(std::move(empty_block));
            item->id = id;
            item->num = block_num;
            // data.previous remains default (block_id_type()) — the anchor has
            // no parent in fork_db.  This is intentional: the anchor represents
            // the snapshot head whose parent data is not available.
            //
            // Mark the entry so no reader mistakes the empty data for block
            // data: see fork_item::anchor_only and the guards in
            // fetch_block_by_number() / fetch_branch_from().
            item->anchor_only = true;
            _index.insert(item);
            if (!_head || block_num >= _head->num) {
                _head = item;
            }
        }

        void fork_database::_repair_child_prev_links(const item_ptr &parent) {
            auto &num_idx = _index.get<block_num>();
            auto it = num_idx.lower_bound(parent->num + 1);
            auto end = num_idx.upper_bound(parent->num + 1);
            for (; it != end; ++it) {
                const item_ptr &candidate = *it;
                if (candidate->data.previous == parent->id && !candidate->prev.lock()) {
                    candidate->prev = parent;
                }
            }
        }

/**
 * Pushes the block into the fork database and caches it if it doesn't link
 *
 */
        shared_ptr<fork_item> fork_database::push_block(const signed_block &b) {
            auto item = std::make_shared<fork_item>(b);
            try {
                _push_block(item);
            }
            catch (const unlinkable_block_exception &e) {
                wlog("Pushing block to fork database that failed to link: ${id}, ${num}", ("id", b.id())("num", b.block_num()));
                // Report the head's own fields: _head may be an anchor, whose
                // data is empty and does not describe the block it stands for.
                wlog("Head: ${num}, ${id}", ("num", _head->num)("id", _head->id));
                _unlinked_index.insert(item);
                throw;
            }
            return _head;
        }

        void fork_database::_push_block(const item_ptr &item) {
            // Skip if this block already exists in the index (e.g., after snapshot
            // import the head block was seeded via start_block, and P2P may re-send it)
            auto &id_index = _index.get<block_id>();
            auto existing_itr = id_index.find(item->id);
            if (existing_itr != id_index.end()) {
                return;
            }

            if (_head) // make sure the block is within the range that we are caching
            {
                if (item->num <= std::max<int64_t>(0, int64_t(_head->num) - (_max_size))) {
                    FC_THROW_EXCEPTION(block_too_old_exception,
                        "attempting to push a block that is too old",
                        ("item_num", item->num)("head", _head->num)("max_size", _max_size));
                }
            }

            if (_head && item->previous_id() != block_id_type()) {
                auto &index = _index.get<block_id>();
                auto itr = index.find(item->previous_id());
                CHAIN_ASSERT(
                    itr != index.end(),
                    unlinkable_block_exception,
                    "block does not link to known chain");
                FC_ASSERT(!(*itr)->invalid);
                item->prev = *itr;
            }

            _index.insert(item);
            if (!_head || item->num > _head->num) {
                _head = item;
            }
            // During emergency mode, deterministic hash tie-breaking:
            // When two blocks compete at the same height (multiple emergency
            // producers), prefer the lower block_id hash. This ensures all
            // nodes converge regardless of P2P arrival order.
            else if (item->num == _head->num && item->id < _head->id &&
                     _emergency_consensus_active) {
                _head = item;
            }

            // After inserting a new block, check if any previously-unlinkable
            // blocks in the _unlinked_index can now be linked to this one.
            _push_next(item);
        }

/**
 *  Iterate through the unlinked cache and insert anything that
 *  links to the newly inserted item.  This will start a recursive
 *  set of calls performing a depth-first insertion of pending blocks as
 *  _push_next(..) calls _push_block(...) which will in turn call _push_next
 */
        void fork_database::_push_next(const item_ptr &new_item) {
            auto &prev_idx = _unlinked_index.get<by_previous>();

            auto itr = prev_idx.find(new_item->id);
            while (itr != prev_idx.end()) {
                auto tmp = *itr;
                prev_idx.erase(itr);
                _push_block(tmp);

                itr = prev_idx.find(new_item->id);
            }
        }

        void fork_database::set_max_size(uint32_t s) {
            _max_size = s;
            if (!_head) {
                return;
            }

            { /// index
                auto &by_num_idx = _index.get<block_num>();
                auto itr = by_num_idx.begin();
                while (itr != by_num_idx.end()) {
                    if ((*itr)->num <
                        std::max(int64_t(0), int64_t(_head->num) - _max_size)) {
                        by_num_idx.erase(itr);
                    } else {
                        break;
                    }
                    itr = by_num_idx.begin();
                }
            }
            { /// unlinked_index
                auto &by_num_idx = _unlinked_index.get<block_num>();
                auto itr = by_num_idx.begin();
                while (itr != by_num_idx.end()) {
                    if ((*itr)->num <
                        std::max(int64_t(0), int64_t(_head->num) - _max_size)) {
                        by_num_idx.erase(itr);
                    } else {
                        break;
                    }
                    itr = by_num_idx.begin();
                }
            }
        }

        bool fork_database::is_known_block(const block_id_type &id) const {
            auto &index = _index.get<block_id>();
            auto itr = index.find(id);
            if (itr != index.end()) {
                return true;
            }
            auto &unlinked_index = _unlinked_index.get<block_id>();
            auto unlinked_itr = unlinked_index.find(id);
            return unlinked_itr != unlinked_index.end();
        }

        item_ptr fork_database::fetch_block(const block_id_type &id) const {
            auto &index = _index.get<block_id>();
            auto itr = index.find(id);
            if (itr != index.end()) {
                return *itr;
            }
            auto &unlinked_index = _unlinked_index.get<block_id>();
            auto unlinked_itr = unlinked_index.find(id);
            if (unlinked_itr != unlinked_index.end()) {
                return *unlinked_itr;
            }
            return item_ptr();
        }

        vector<item_ptr> fork_database::fetch_block_by_number(uint32_t num) const {
            try {
                vector<item_ptr> result;
                auto itr = _index.get<block_num>().find(num);
                while (itr != _index.get<block_num>().end()) {
                    if ((*itr)->num == num) {
                        // Anchor entries carry no block data (insert_anchor_block):
                        // callers of this method ask for real blocks (competing
                        // blocks at a height, block data lookups), so keep them out.
                        if (!(*itr)->anchor_only) {
                            result.push_back(*itr);
                        }
                    } else {
                        break;
                    }
                    ++itr;
                }
                return result;
            }
            FC_LOG_AND_RETHROW()
        }

        pair<fork_database::branch_type, fork_database::branch_type>
        fork_database::fetch_branch_from(block_id_type first, block_id_type second) const {
            try {
                // This function gets a branch (i.e. vector<fork_item>) leading
                // back to the most recent common ancestor.
                pair<branch_type, branch_type> result;

                // An anchor entry (see fork_item::anchor_only) has no block data:
                // its data.block_num()/data.id()/data.previous do not describe it.
                // Callers apply the branches returned here via apply_block() and
                // use the common ancestor's data.previous as a pop target, so an
                // anchor must never take part in a branch — refuse loudly instead
                // of letting an empty block or a bogus common ancestor through.
                auto no_anchor = [](const item_ptr &i, const char *where) {
                    FC_ASSERT(i && !i->anchor_only,
                        "fetch_branch_from: fork_db anchor entry reached ${w} "
                        "(anchor carries no block data)",
                        ("w", where));
                };
                auto first_branch_itr = _index.get<block_id>().find(first);
                if (first_branch_itr == _index.get<block_id>().end()) {
                    wlog("fetch_branch_from: first block not in fork_db index");
                    FC_THROW_EXCEPTION(fc::assert_exception,
                        "fetch_branch_from: first block ${id} not found in fork_db",
                        ("id", first));
                }
                auto first_branch = *first_branch_itr;

                auto second_branch_itr = _index.get<block_id>().find(second);
                if (second_branch_itr == _index.get<block_id>().end()) {
                    wlog("fetch_branch_from: second block not in fork_db index");
                    FC_THROW_EXCEPTION(fc::assert_exception,
                        "fetch_branch_from: second block ${id} not found in fork_db",
                        ("id", second));
                }
                auto second_branch = *second_branch_itr;


                while (first_branch->data.block_num() >
                       second_branch->data.block_num()) {
                    no_anchor(first_branch, "first branch");
                    result.first.push_back(first_branch);
                    first_branch = first_branch->prev.lock();
                    if (!first_branch) {
                        wlog("fetch_branch_from: broken prev chain on first branch");
                        FC_THROW_EXCEPTION(fc::assert_exception,
                            "fetch_branch_from: broken prev chain on first branch (block ${id})",
                            ("id", first));
                    }
                }
                while (second_branch->data.block_num() >
                       first_branch->data.block_num()) {
                    no_anchor(second_branch, "second branch");
                    result.second.push_back(second_branch);
                    second_branch = second_branch->prev.lock();
                    if (!second_branch) {
                        wlog("fetch_branch_from: broken prev chain on second branch");
                        FC_THROW_EXCEPTION(fc::assert_exception,
                            "fetch_branch_from: broken prev chain on second branch (block ${id})",
                            ("id", second));
                    }
                }
                while (first_branch->data.previous !=
                       second_branch->data.previous) {
                    no_anchor(first_branch, "first branch ancestor search");
                    no_anchor(second_branch, "second branch ancestor search");
                    result.first.push_back(first_branch);
                    result.second.push_back(second_branch);
                    first_branch = first_branch->prev.lock();
                    if (!first_branch || !second_branch) {
                        wlog("fetch_branch_from: broken prev chain during common ancestor search");
                        FC_THROW_EXCEPTION(fc::assert_exception,
                            "fetch_branch_from: broken prev chain during common ancestor search");
                    }
                    second_branch = second_branch->prev.lock();
                    if (!second_branch) {
                        wlog("fetch_branch_from: broken prev chain on second branch during ancestor search");
                        FC_THROW_EXCEPTION(fc::assert_exception,
                            "fetch_branch_from: broken prev chain on second branch during ancestor search");
                    }
                }
                if (first_branch && second_branch) {
                    // The last pushed items are the common ancestor.  Its
                    // data.previous is used by the caller as the pop target, so
                    // an anchor here would make the caller pop to a bogus id.
                    no_anchor(first_branch, "common ancestor");
                    no_anchor(second_branch, "common ancestor");
                    result.first.push_back(first_branch);
                    result.second.push_back(second_branch);
                }
                return result;
            } FC_CAPTURE_AND_RETHROW((first)(second))
        }

        shared_ptr<fork_item> fork_database::walk_main_branch_to_num(uint32_t block_num) const {
            shared_ptr<fork_item> next = head();
            if (block_num > next->num) {
                return shared_ptr<fork_item>();
            }

            while (next.get() != nullptr && next->num > block_num) {
                next = next->prev.lock();
            }
            return next;
        }

        shared_ptr<fork_item> fork_database::fetch_block_on_main_branch_by_number(uint32_t block_num) const {
            vector<item_ptr> blocks = fetch_block_by_number(block_num);
            if (blocks.size() == 1) {
                return blocks[0];
            }
            if (blocks.size() == 0) {
                return shared_ptr<fork_item>();
            }
            return walk_main_branch_to_num(block_num);
        }

        void fork_database::set_head(shared_ptr<fork_item> h) {
            _head = h;
        }

        void fork_database::set_emergency_mode(bool active) {
            _emergency_consensus_active = active;
        }

        void fork_database::remove(block_id_type id) {
            _index.get<block_id>().erase(id);
        }

        void fork_database::remove_blocks_by_number(uint32_t num) {
            auto blocks = fetch_block_by_number(num);
            for (const auto& b : blocks) {
                _index.get<block_id>().erase(b->id);
            }
        }

    }
} // graphene::chain
