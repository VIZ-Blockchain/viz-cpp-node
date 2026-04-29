#pragma once

#include <fc/filesystem.hpp>
#include <graphene/protocol/block.hpp>

namespace graphene {
    namespace chain {

        using namespace graphene::protocol;

        namespace detail { class dlt_block_log_impl; }

        /**
         * DLT rolling block log -- a separate append-only block store with an
         * offset-aware index that can start from an arbitrary block number.
         *
         * Used by DLT (snapshot-based) nodes to keep a sliding window of recent
         * irreversible blocks so they can be served to P2P peers.
         *
         * Data file layout (same as regular block_log):
         * +---------+----------------+---------+----------------+-----+
         * | Block N | Pos of Block N | Block.. | Pos of Block.. | ... |
         * +---------+----------------+---------+----------------+-----+
         *
         * Index file layout (offset-aware):
         * +-------------------+------------------+------------------+-----+
         * | start_block_num   | Pos of block S   | Pos of block S+1 | ... |
         * | (8 bytes header)  | (8 bytes)        | (8 bytes)        |     |
         * +-------------------+------------------+------------------+-----+
         *
         * To look up block N: read start_block_num from header, then read
         * offset at byte  8 + 8 * (N - start_block_num).
         */

        class dlt_block_log {
        public:
            dlt_block_log();

            ~dlt_block_log();

            void open(const fc::path& file);

            void close();

            bool is_open() const;

            uint64_t append(const signed_block& b);

            void flush();

            optional<signed_block> read_block_by_num(uint32_t block_num) const;

            optional<signed_block> head() const;

            /// First block number stored in the log (0 if empty).
            uint32_t start_block_num() const;

            /// Last block number stored in the log (0 if empty).
            uint32_t head_block_num() const;

            /// Number of blocks currently stored.
            uint32_t num_blocks() const;

            /// Truncate old blocks: keep only blocks from new_start onward.
            /// Creates temp files, copies the retained blocks, swaps, reopens.
            void truncate_before(uint32_t new_start);

            /// Reset the log: close, delete files, reopen empty.
            /// The next append() will start a fresh log from whatever block is written.
            void reset();

            /// Check mapping consistency and self-heal if stale.
            /// Returns true if a stale mapping was detected and healed.
            /// Call this periodically (e.g. from stats task) to detect
            /// Windows mapped_file.size() drift after many resize() cycles.
            bool verify_mapping();

            /// Number of resize() calls since open (for diagnostics).
            uint64_t resize_count() const;

            static const uint64_t npos = std::numeric_limits<uint64_t>::max();

        private:
            std::unique_ptr<detail::dlt_block_log_impl> my;
        };

    }
}
