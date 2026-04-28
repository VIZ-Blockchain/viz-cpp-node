#include <algorithm>
#include <cstring>
#include <fstream>
#include <graphene/chain/dlt_block_log.hpp>
#include <boost/iostreams/device/mapped_file.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread/shared_mutex.hpp>

namespace graphene { namespace chain {
    namespace detail {
        using read_write_mutex = boost::shared_mutex;
        using read_lock = boost::shared_lock<read_write_mutex>;
        using write_lock = boost::unique_lock<read_write_mutex>;
        static constexpr boost::iostreams::stream_offset dlt_min_valid_file_size = sizeof(uint64_t);

        // Index header size: 8 bytes for start_block_num
        static constexpr std::size_t INDEX_HEADER_SIZE = sizeof(uint64_t);

        class dlt_block_log_impl {
        public:
            optional<signed_block> head;
            block_id_type head_id;
            uint32_t _start_block_num = 0; // cached from index header

            std::string block_path;
            std::string index_path;
            boost::iostreams::mapped_file block_mapped_file;
            boost::iostreams::mapped_file index_mapped_file;
            read_write_mutex mutex;

            bool has_block_records() const {
                auto size = block_mapped_file.size();
                return (size > dlt_min_valid_file_size);
            }

            bool has_index_records() const {
                // Index must have at least the header + one entry
                auto size = index_mapped_file.size();
                return (size > INDEX_HEADER_SIZE);
            }

            std::size_t get_mapped_size(const boost::iostreams::mapped_file& mapped_file) const {
                auto size = mapped_file.size();
                if (size < dlt_min_valid_file_size) {
                    return 0;
                }
                return size;
            }

            uint64_t get_uint64(const boost::iostreams::mapped_file& mapped_file, std::size_t pos) const {
                uint64_t value;
                FC_ASSERT(get_mapped_size(mapped_file) >= pos + sizeof(value));
                auto* ptr = mapped_file.data() + pos;
                std::memcpy(&value, ptr, sizeof(value));
                return value;
            }

            uint64_t get_last_uint64(const boost::iostreams::mapped_file& mapped_file) const {
                uint64_t value;
                auto size = get_mapped_size(mapped_file);
                FC_ASSERT(size >= sizeof(value));
                auto* ptr = mapped_file.data() + size - sizeof(value);
                std::memcpy(&value, ptr, sizeof(value));
                return value;
            }

            uint64_t get_block_pos(uint32_t block_num) const {
                if (!head.valid() || _start_block_num == 0) {
                    return dlt_block_log::npos;
                }
                uint32_t head_num = protocol::block_header::num_from_id(head_id);
                if (block_num < _start_block_num || block_num > head_num) {
                    return dlt_block_log::npos;
                }
                // Index entry is at: header(8) + 8 * (block_num - start_block_num)
                std::size_t idx_offset = INDEX_HEADER_SIZE + sizeof(uint64_t) * (block_num - _start_block_num);
                auto idx_size = get_mapped_size(index_mapped_file);
                if (idx_offset + sizeof(uint64_t) > idx_size) {
                    return dlt_block_log::npos;
                }
                return get_uint64(index_mapped_file, idx_offset);
            }

            uint64_t read_block(uint64_t pos, signed_block& block) const {
                const auto file_size = get_mapped_size(block_mapped_file);
                FC_ASSERT(file_size > pos);

                const auto* ptr = block_mapped_file.data() + pos;
                const auto available_size = file_size - pos;
                const auto max_block_size = std::min<std::size_t>(available_size, CHAIN_BLOCK_SIZE);

                fc::datastream<const char*> ds(ptr, max_block_size);
                fc::raw::unpack(ds, block);

                const auto end_pos = pos + ds.tellp();
                FC_ASSERT(get_uint64(block_mapped_file, end_pos) == pos);

                return end_pos + sizeof(uint64_t);
            }

            signed_block read_head() const {
                auto pos = get_last_uint64(block_mapped_file);
                signed_block block;
                read_block(pos, block);
                return block;
            }

            void create_nonexist_file(const std::string& path) const {
                if (!boost::filesystem::is_regular_file(path) || boost::filesystem::file_size(path) == 0) {
                    std::ofstream stream(path, std::ios::out|std::ios::binary);
                    stream << '\0';
                    stream.close();
                }
            }

            void open_block_mapped_file() {
                create_nonexist_file(block_path);
                block_mapped_file.open(block_path, boost::iostreams::mapped_file::readwrite);
            }

            void open_index_mapped_file() {
                create_nonexist_file(index_path);
                index_mapped_file.open(index_path, boost::iostreams::mapped_file::readwrite);
            }

            void construct_index() {
                ilog("DLT block log: reconstructing index...");
                index_mapped_file.close();
                boost::filesystem::remove_all(index_path);
                open_index_mapped_file();

                // Walk the data file to find the first block number
                uint64_t pos = 0;
                uint64_t end_pos = get_last_uint64(block_mapped_file);
                signed_block first_block;
                read_block(pos, first_block);
                uint32_t first_num = first_block.block_num();

                // Calculate number of entries needed
                uint32_t head_num = head->block_num();
                uint32_t count = head_num - first_num + 1;

                // Allocate header + entries
                index_mapped_file.resize(INDEX_HEADER_SIZE + count * sizeof(uint64_t));

                // Write header: start_block_num
                { uint64_t tmp = first_num; std::memcpy(index_mapped_file.data(), &tmp, sizeof(tmp)); }
                _start_block_num = first_num;

                // Write entries
                pos = 0;
                auto* idx_ptr = index_mapped_file.data() + INDEX_HEADER_SIZE;
                signed_block tmp_block;

                while (pos <= end_pos) {
                    std::memcpy(idx_ptr, &pos, sizeof(pos));
                    pos = read_block(pos, tmp_block);
                    idx_ptr += sizeof(uint64_t);
                }
            }

            void open(const fc::path& file) { try {
                block_mapped_file.close();
                index_mapped_file.close();
                head.reset();
                head_id = block_id_type();
                _start_block_num = 0;

                block_path = file.string();
                index_path = boost::filesystem::path(file.string() + ".index").string();

                // Recover from a crash during truncate_before():
                // If .bak files exist but originals are missing/empty, restore from backup.
                std::string bak_block = block_path + ".bak";
                std::string bak_index = index_path + ".bak";
                if (boost::filesystem::exists(bak_block)) {
                    if (!boost::filesystem::is_regular_file(block_path) || boost::filesystem::file_size(block_path) <= 1) {
                        ilog("DLT block log: restoring data from .bak after interrupted truncation");
                        boost::filesystem::remove_all(block_path);
                        boost::filesystem::rename(bak_block, block_path);
                    } else {
                        boost::filesystem::remove_all(bak_block);
                    }
                }
                if (boost::filesystem::exists(bak_index)) {
                    if (!boost::filesystem::is_regular_file(index_path) || boost::filesystem::file_size(index_path) <= 1) {
                        ilog("DLT block log: restoring index from .bak after interrupted truncation");
                        boost::filesystem::remove_all(index_path);
                        boost::filesystem::rename(bak_index, index_path);
                    } else {
                        boost::filesystem::remove_all(bak_index);
                    }
                }
                // Also clean up stale .tmp files from interrupted truncation
                std::string tmp_block = block_path + ".tmp";
                std::string tmp_index = index_path + ".tmp";
                if (boost::filesystem::exists(tmp_block)) {
                    boost::filesystem::remove_all(tmp_block);
                }
                if (boost::filesystem::exists(tmp_index)) {
                    boost::filesystem::remove_all(tmp_index);
                }

                open_block_mapped_file();
                open_index_mapped_file();

                if (has_block_records()) {
                    ilog("DLT block log: data file is nonempty");
                    head = read_head();
                    head_id = head->id();

                    if (has_index_records()) {
                        ilog("DLT block log: index file is nonempty");
                        // Read start_block_num from header
                        _start_block_num = static_cast<uint32_t>(get_uint64(index_mapped_file, 0));

                        // Validate: last index entry should match last block_log position
                        auto block_pos = get_last_uint64(block_mapped_file);
                        auto index_pos = get_last_uint64(index_mapped_file);

                        if (block_pos != index_pos) {
                            ilog("DLT block log: index mismatch, reconstructing");
                            construct_index();
                        }
                    } else {
                        ilog("DLT block log: index is empty, constructing");
                        construct_index();
                    }

                    ilog("DLT block log: opened with blocks ${s}-${h}",
                         ("s", _start_block_num)("h", head->block_num()));
                } else if (has_index_records()) {
                    // Data empty but index exists -- wipe index
                    ilog("DLT block log: data empty but index exists, wiping");
                    index_mapped_file.close();
                    block_mapped_file.close();
                    boost::filesystem::remove_all(block_path);
                    boost::filesystem::remove_all(index_path);
                    open_block_mapped_file();
                    open_index_mapped_file();
                }
            } FC_LOG_AND_RETHROW() }

            uint64_t append(const signed_block& b, const std::vector<char>& data) { try {
                const auto idx_size = get_mapped_size(index_mapped_file);
                const uint32_t block_num = b.block_num();

                if (_start_block_num == 0) {
                    // First block ever written -- initialize the index header
                    _start_block_num = block_num;

                    // Write header + first entry
                    index_mapped_file.resize(INDEX_HEADER_SIZE + sizeof(uint64_t));
                    { uint64_t tmp = _start_block_num; std::memcpy(index_mapped_file.data(), &tmp, sizeof(tmp)); }

                    uint64_t block_pos = get_mapped_size(block_mapped_file);

                    // Write block data + trailing position
                    block_mapped_file.resize(block_pos + data.size() + sizeof(block_pos));
                    auto* ptr = block_mapped_file.data() + block_pos;
                    std::memcpy(ptr, data.data(), data.size());
                    ptr += data.size();
                    std::memcpy(ptr, &block_pos, sizeof(block_pos));

                    // Write index entry
                    auto* idx_ptr = index_mapped_file.data() + INDEX_HEADER_SIZE;
                    std::memcpy(idx_ptr, &block_pos, sizeof(block_pos));

                    head = b;
                    head_id = b.id();
                    return block_pos;
                }

                // Subsequent blocks -- validate position
                const std::size_t expected_idx_size = INDEX_HEADER_SIZE + sizeof(uint64_t) * (block_num - _start_block_num);
                FC_ASSERT(
                    idx_size == expected_idx_size,
                    "DLT block log: append to index at wrong position.",
                    ("position", idx_size)
                    ("expected", expected_idx_size)
                    ("block_num", block_num)
                    ("start_block_num", _start_block_num));

                uint64_t block_pos = get_mapped_size(block_mapped_file);

                // Write block data + trailing position
                block_mapped_file.resize(block_pos + data.size() + sizeof(block_pos));
                auto* ptr = block_mapped_file.data() + block_pos;
                std::memcpy(ptr, data.data(), data.size());
                ptr += data.size();
                std::memcpy(ptr, &block_pos, sizeof(block_pos));

                // Write index entry
                index_mapped_file.resize(idx_size + sizeof(uint64_t));
                ptr = index_mapped_file.data() + idx_size;
                std::memcpy(ptr, &block_pos, sizeof(block_pos));

                head = b;
                head_id = b.id();
                return block_pos;
            } FC_LOG_AND_RETHROW() }

            void close() {
                block_mapped_file.close();
                index_mapped_file.close();
                head.reset();
                head_id = block_id_type();
                _start_block_num = 0;
            }
        };
    }

    dlt_block_log::dlt_block_log()
            : my(std::make_unique<detail::dlt_block_log_impl>()) {
    }

    dlt_block_log::~dlt_block_log() {
        flush();
    }

    void dlt_block_log::open(const fc::path& file) {
        detail::write_lock lock(my->mutex);
        my->open(file);
    }

    void dlt_block_log::close() {
        detail::write_lock lock(my->mutex);
        my->close();
    }

    bool dlt_block_log::is_open() const {
        detail::read_lock lock(my->mutex);
        return my->block_mapped_file.is_open();
    }

    uint64_t dlt_block_log::append(const signed_block& block) { try {
        auto data = fc::raw::pack(block);
        detail::write_lock lock(my->mutex);
        return my->append(block, data);
    } FC_LOG_AND_RETHROW() }

    void dlt_block_log::flush() {
        // Not needed -- data is in page cache (memory-mapped)
    }

    optional<signed_block> dlt_block_log::read_block_by_num(uint32_t block_num) const { try {
        detail::read_lock lock(my->mutex);
        optional<signed_block> result;
        uint64_t pos = my->get_block_pos(block_num);
        if (pos != npos) {
            signed_block block;
            my->read_block(pos, block);
            FC_ASSERT(
                block.block_num() == block_num,
                "DLT block log: wrong block read (${returned} != ${expected}).",
                ("returned", block.block_num())
                ("expected", block_num));
            result = std::move(block);
        }
        return result;
    } FC_LOG_AND_RETHROW() }

    const optional<signed_block>& dlt_block_log::head() const {
        detail::read_lock lock(my->mutex);
        return my->head;
    }

    uint32_t dlt_block_log::start_block_num() const {
        detail::read_lock lock(my->mutex);
        return my->_start_block_num;
    }

    uint32_t dlt_block_log::head_block_num() const {
        detail::read_lock lock(my->mutex);
        if (my->head.valid()) {
            return protocol::block_header::num_from_id(my->head_id);
        }
        return 0;
    }

    uint32_t dlt_block_log::num_blocks() const {
        detail::read_lock lock(my->mutex);
        if (!my->head.valid() || my->_start_block_num == 0) {
            return 0;
        }
        return protocol::block_header::num_from_id(my->head_id) - my->_start_block_num + 1;
    }

    void dlt_block_log::truncate_before(uint32_t new_start) { try {
        detail::write_lock lock(my->mutex);

        if (!my->head.valid() || my->_start_block_num == 0) return;

        uint32_t head_num = protocol::block_header::num_from_id(my->head_id);
        if (new_start <= my->_start_block_num) return;  // nothing to truncate
        if (new_start > head_num) return;                // would delete everything

        ilog("DLT block log: truncating, keeping blocks ${s}-${h}",
             ("s", new_start)("h", head_num));

        std::string temp_block_path = my->block_path + ".tmp";
        std::string temp_index_path = my->index_path + ".tmp";

        // Build temporary files with only the retained blocks
        {
            detail::dlt_block_log_impl temp;
            temp.block_path = temp_block_path;
            temp.index_path = temp_index_path;

            // Remove any stale temp files
            boost::filesystem::remove_all(temp_block_path);
            boost::filesystem::remove_all(temp_index_path);

            temp.open_block_mapped_file();
            temp.open_index_mapped_file();

            for (uint32_t n = new_start; n <= head_num; ++n) {
                uint64_t pos = my->get_block_pos(n);
                if (pos == dlt_block_log::npos) continue;

                signed_block blk;
                my->read_block(pos, blk);
                auto data = fc::raw::pack(blk);
                temp.append(blk, data);
            }

            temp.close();
        }

        // Close originals
        my->close();

        // Swap files safely: rename originals to .bak first, then rename temps,
        // then remove backups. This way a crash at any point leaves recoverable files.
        std::string bak_block_path = my->block_path + ".bak";
        std::string bak_index_path = my->index_path + ".bak";
        boost::filesystem::remove_all(bak_block_path);
        boost::filesystem::remove_all(bak_index_path);
        boost::filesystem::rename(my->block_path, bak_block_path);
        boost::filesystem::rename(my->index_path, bak_index_path);
        boost::filesystem::rename(temp_block_path, my->block_path);
        boost::filesystem::rename(temp_index_path, my->index_path);
        boost::filesystem::remove_all(bak_block_path);
        boost::filesystem::remove_all(bak_index_path);

        // Reopen
        my->open(fc::path(my->block_path));

        ilog("DLT block log: truncation complete, now blocks ${s}-${h}",
             ("s", my->_start_block_num)("h", (my->head.valid() ? my->head->block_num() : 0)));
    } FC_CAPTURE_AND_RETHROW((new_start)) }

    void dlt_block_log::reset() { try {
        detail::write_lock lock(my->mutex);

        uint32_t old_start = my->_start_block_num;
        uint32_t old_end = my->head.valid() ? my->head->block_num() : 0;

        my->close();

        boost::filesystem::remove_all(my->block_path);
        boost::filesystem::remove_all(my->index_path);
        // Also remove stale temp/backup files
        boost::filesystem::remove_all(my->block_path + ".tmp");
        boost::filesystem::remove_all(my->index_path + ".tmp");
        boost::filesystem::remove_all(my->block_path + ".bak");
        boost::filesystem::remove_all(my->index_path + ".bak");

        my->open(fc::path(my->block_path));

        ilog("DLT block log: reset complete (was blocks ${s}-${h}, now empty)",
             ("s", old_start)("h", old_end));
    } FC_CAPTURE_AND_RETHROW() }

} } // graphene::chain
