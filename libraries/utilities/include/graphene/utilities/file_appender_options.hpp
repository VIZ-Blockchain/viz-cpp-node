#pragma once

#include <fc/log/file_appender.hpp>

#include <map>
#include <string>
#include <vector>

namespace graphene {
namespace utilities {

/**
 * Options of one [log.file_appender.<name>] ini section.
 *
 * The defaults are the values programs/vizd/main.cpp hard-coded before 2026-10-25, so a
 * config that does not mention these keys keeps the old behaviour exactly.
 */
struct file_appender_options {
   bool    flush = true;
   bool    rotate = true;
   int64_t rotation_interval_seconds = 60 * 60;
   int64_t rotation_limit_seconds    = 24 * 60 * 60;
};

/**
 * Reads flush/rotate/rotation_interval/rotation_limit out of an ini section.
 *
 * Never throws. A value that cannot be parsed keeps its default and pushes one line into
 * `warnings` — a typo must not cost the operator the whole logging config (the same rule
 * the capped appender applies to max_lines). Keys that are not part of the section are
 * reported too: the config parser accepts any key, so an unknown one would otherwise be a
 * phantom option — an operator edits it and nothing happens.
 *
 * fc::file_appender asserts rotation_interval >= 1 s and rotation_limit >= rotation_interval
 * when rotate is on. A violating pair keeps the defaults and is reported instead, because
 * the assert would take the appender (and with it the log file) down at startup.
 */
file_appender_options parse_file_appender_options(const std::string& section_name,
                                                  const std::map<std::string, std::string>& options,
                                                  std::vector<std::string>* warnings = nullptr);

/**
 * Builds the fc::file_appender::config that vizd hands to the "file" appender.
 *
 * Kept next to the parser so the wiring from ini to the appender's config is covered by a
 * test instead of by reading main.cpp. `section_name` is used only in warnings.
 */
fc::file_appender::config make_file_appender_config(const std::string& section_name,
                                                    const fc::path& filename,
                                                    const std::map<std::string, std::string>& options,
                                                    std::vector<std::string>* warnings = nullptr);

} // namespace utilities
} // namespace graphene
