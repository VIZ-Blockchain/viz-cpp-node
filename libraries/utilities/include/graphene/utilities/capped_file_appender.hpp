#pragma once

#include <fc/filesystem.hpp>
#include <fc/log/appender.hpp>

#include <cstdint>
#include <string>

namespace graphene {
namespace utilities {

/**
 * File appender that keeps only the last `max_lines` lines of one file.
 *
 * Why it exists: the stock fc::file_appender rotates by TIME (new file every hour,
 * kept for a day). In debug mode an hour of logs can be gigabytes, and 24 hourly
 * files of that size overflow a small disk. This appender keeps a single file and
 * compacts it in place once it grows past the cap, so the file always holds the
 * most recent lines and nothing older.
 *
 * The appender is OPTIONAL by construction:
 *   - it is registered as its own type ("capped_file"), so configs that do not use
 *     it behave exactly as before;
 *   - an older binary ignores the whole [log.capped_file_appender.*] section (its
 *     loader does not know the prefix) and an unknown appender type resolves to a
 *     null appender instead of failing;
 *   - `max_lines = 0` disables compaction explicitly.
 *
 * File format is byte-identical to fc::file_appender's text format, so the Logs tab
 * of the node_admin plugin (admin.logs.list/tail) needs no changes: it reads a path.
 */
class capped_file_appender : public fc::appender {
public:
    struct config {
        config(const fc::path &p = "log.txt");

        fc::path filename;
        /// Keep at most this many lines; 0 = unlimited (no compaction).
        uint32_t max_lines = 10000;
        bool flush = true;
    };

    explicit capped_file_appender(const fc::variant &args);

    ~capped_file_appender() override;

    void log(const fc::log_message &m) override;

    /**
     * Rewrite `path` so that it holds only its last `keep` lines.
     *
     * Reads the file backwards in chunks, so a file that is already far over the cap
     * costs one bounded tail read rather than a full scan of 10 GB. The rewrite goes
     * through a temporary file in the same directory and an atomic rename, so a reader
     * (admin.logs.tail) never sees a half-written file.
     *
     * @return true when the file is already short enough or was trimmed; false with a
     *         reason in `error` when it could not be read or replaced.
     */
    static bool trim_to_last_lines(const fc::path &path, uint32_t keep, std::string &error);

private:
    class impl;

    fc::shared_ptr<impl> my;
};

} // namespace utilities
} // namespace graphene

#include <fc/reflect/reflect.hpp>

FC_REFLECT((graphene::utilities::capped_file_appender::config), (filename)(max_lines)(flush))
