#pragma once

#include <graphene/protocol/block.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>


namespace graphene {
    namespace chain {
        using boost::multi_index_container;
        using namespace boost::multi_index;

        using graphene::protocol::signed_block;
        using graphene::protocol::block_id_type;

        struct fork_item {
            fork_item(signed_block d)
                    : num(d.block_num()), id(d.id()), data(std::move(d)) {
            }

            block_id_type previous_id() const {
                return data.previous;
            }

            weak_ptr<fork_item> prev;
            uint32_t num;    // initialized in ctor
            /**
             * Used to flag a block as invalid and prevent other blocks from
             * building on top of it.
             */
            bool invalid = false;
            block_id_type id;
            signed_block data;
        };

        typedef shared_ptr<fork_item> item_ptr;


        /**
         *  As long as blocks are pushed in order the fork
         *  database will maintain a linked tree of all blocks
         *  that branch from the start_block.  The tree will
         *  have a maximum depth of 1024 blocks after which
         *  the database will start lopping off forks.
         *
         *  Every time a block is pushed into the fork DB the
         *  block with the highest block_num will be returned.
         */
        class fork_database {
        public:
            typedef vector<item_ptr> branch_type;
            /// The maximum number of blocks that may be skipped in an out-of-order push
            const static int MAX_BLOCK_REORDERING = 2400;

            fork_database();

            void reset();

            void start_block(signed_block b);

            void remove(block_id_type b);

            /**
             * Remove all blocks at the given height from the fork database.
             * Used to clear stale competing blocks from dead forks.
             */
            void remove_blocks_by_number(uint32_t num);

            void set_head(shared_ptr<fork_item> h);

            bool is_known_block(const block_id_type &id) const;

            shared_ptr<fork_item> fetch_block(const block_id_type &id) const;

            vector<item_ptr> fetch_block_by_number(uint32_t n) const;

            /**
             *  @return the new head block ( the longest fork )
             */
            shared_ptr<fork_item> push_block(const signed_block &b);

            shared_ptr<fork_item> head() const {
                return _head;
            }

            void pop_block();

            /**
             *  Given two head blocks, return two branches of the fork graph that
             *  end with a common ancestor (same prior block)
             */
            pair<branch_type, branch_type> fetch_branch_from(block_id_type first,
                    block_id_type second) const;

            shared_ptr<fork_item> walk_main_branch_to_num(uint32_t block_num) const;

            shared_ptr<fork_item> fetch_block_on_main_branch_by_number(uint32_t block_num) const;

            struct block_id;
            struct block_num;
            struct by_previous;
            typedef multi_index_container<
                    item_ptr,
                    indexed_by<
                            hashed_unique<tag<block_id>, member<fork_item, block_id_type, &fork_item::id>, std::hash<fc::ripemd160>>,
                            hashed_non_unique<tag<by_previous>, const_mem_fun<fork_item, block_id_type, &fork_item::previous_id>, std::hash<fc::ripemd160>>,
                            ordered_non_unique<tag<block_num>, member<fork_item, uint32_t, &fork_item::num>>
                    >
            > fork_multi_index_type;

            void set_max_size(uint32_t s);

            /**
             * Set emergency consensus mode flag.
             * During emergency mode, deterministic hash-based tie-breaking
             * is used when two blocks compete at the same height.
             */
            void set_emergency_mode(bool active);

            bool is_emergency_mode() const {
                return _emergency_consensus_active;
            }

            /// Diagnostic accessors for block storage stats
            size_t linked_size() const { return _index.size(); }
            size_t unlinked_size() const { return _unlinked_index.size(); }

            /// Returns min/max block numbers in the linked index (0 if empty)
            uint32_t linked_min_block_num() const {
                auto& by_num = _index.get<block_num>();
                return by_num.empty() ? 0 : (*by_num.begin())->num;
            }
            uint32_t linked_max_block_num() const {
                auto& by_num = _index.get<block_num>();
                return by_num.empty() ? 0 : (*by_num.rbegin())->num;
            }

            /// Returns min/max block numbers in the unlinked index (0 if empty)
            uint32_t unlinked_min_block_num() const {
                auto& by_num = _unlinked_index.get<block_num>();
                return by_num.empty() ? 0 : (*by_num.begin())->num;
            }
            uint32_t unlinked_max_block_num() const {
                auto& by_num = _unlinked_index.get<block_num>();
                return by_num.empty() ? 0 : (*by_num.rbegin())->num;
            }

        private:
            /** @return a pointer to the newly pushed item */
            void _push_block(const item_ptr &b);

            void _push_next(const item_ptr &newly_inserted);

            uint32_t _max_size = 2400;

            bool _emergency_consensus_active = false;

            fork_multi_index_type _unlinked_index;
            fork_multi_index_type _index;
            shared_ptr<fork_item> _head;
        };
    }
} // graphene::chain
