// Contract of the bounded file appender: the file never grows past the cap by more
// than one compaction window, the tail it keeps is byte-identical to what was written,
// and the trim itself is safe on files that are already huge (tail read, not full scan).
//
// These checks pin the two properties that matter in production:
//   1. trim_to_last_lines keeps exactly the last N lines, whatever the file length;
//   2. the appender compacts on its own while logging, without losing the newest lines.

#define BOOST_TEST_MODULE capped_file_appender
#include <boost/test/unit_test.hpp>

#include <graphene/utilities/capped_file_appender.hpp>

#include <fc/log/logger.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/variant.hpp>
#include <fc/variant_object.hpp>

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <process.h>
#define test_pid _getpid
#else
#include <unistd.h>
#define test_pid getpid
#endif
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string temp_path(const std::string &name) {
    const char *dir = std::getenv("TMPDIR");
    return std::string(dir && *dir ? dir : "/tmp") + "/capped_appender_test_" + name + "_" +
           std::to_string(static_cast<long>(test_pid()));
}

void write_file(const std::string &path, const std::string &body) {
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    out.write(body.data(), static_cast<std::streamsize>(body.size()));
}

std::string read_file(const std::string &path) {
    std::ifstream in(path.c_str(), std::ios::binary);
    std::ostringstream os;
    os << in.rdbuf();
    return os.str();
}

std::vector<std::string> split_lines(const std::string &text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < text.size()) {
        const std::size_t end = text.find('\n', start);
        if (end == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

std::string numbered_lines(std::size_t from, std::size_t to) {
    std::string out;
    for (std::size_t i = from; i <= to; ++i) out += "line-" + std::to_string(i) + "\n";
    return out;
}

} // namespace

BOOST_AUTO_TEST_CASE(trim_keeps_exactly_the_last_lines) {
    const std::string path = temp_path("trim25k");
    write_file(path, numbered_lines(1, 25000));

    std::string error;
    BOOST_REQUIRE(graphene::utilities::capped_file_appender::trim_to_last_lines(path, 1000, error));
    BOOST_TEST(error == "");

    const std::string body = read_file(path);
    const std::vector<std::string> lines = split_lines(body);
    BOOST_REQUIRE_EQUAL(lines.size(), 1000u);
    BOOST_TEST(lines.front() == "line-24001");
    BOOST_TEST(lines.back() == "line-25000");
    BOOST_TEST(body.back() == '\n');
    std::remove(path.c_str());
}

BOOST_AUTO_TEST_CASE(a_short_file_is_left_alone) {
    const std::string path = temp_path("short");
    const std::string original = numbered_lines(1, 10);
    write_file(path, original);

    std::string error;
    BOOST_REQUIRE(graphene::utilities::capped_file_appender::trim_to_last_lines(path, 1000, error));
    BOOST_TEST(read_file(path) == original);
    std::remove(path.c_str());
}

BOOST_AUTO_TEST_CASE(exactly_cap_lines_are_kept_intact) {
    const std::string path = temp_path("exact");
    const std::string original = numbered_lines(1, 50);
    write_file(path, original);

    std::string error;
    BOOST_REQUIRE(graphene::utilities::capped_file_appender::trim_to_last_lines(path, 50, error));
    BOOST_TEST(read_file(path) == original);
    std::remove(path.c_str());
}

BOOST_AUTO_TEST_CASE(no_trailing_newline_still_counts_as_a_line) {
    const std::string path = temp_path("nonewline");
    write_file(path, "a\nb\nc");

    std::string error;
    BOOST_REQUIRE(graphene::utilities::capped_file_appender::trim_to_last_lines(path, 2, error));
    BOOST_TEST(read_file(path) == "b\nc");
    std::remove(path.c_str());
}

BOOST_AUTO_TEST_CASE(missing_or_empty_file_is_not_an_error) {
    const std::string missing = temp_path("missing");
    std::remove(missing.c_str());
    std::string error;
    BOOST_REQUIRE(graphene::utilities::capped_file_appender::trim_to_last_lines(missing, 10, error));
    BOOST_TEST(!std::ifstream(missing.c_str()).good());

    const std::string empty = temp_path("empty");
    write_file(empty, "");
    BOOST_REQUIRE(graphene::utilities::capped_file_appender::trim_to_last_lines(empty, 10, error));
    BOOST_TEST(read_file(empty) == "");
    std::remove(empty.c_str());
}

// The scan walks the file backwards in 64 KiB chunks; a boundary may sit in an older
// chunk than the one that satisfied the newline count.
BOOST_AUTO_TEST_CASE(boundary_is_found_across_chunks) {
    const std::string path = temp_path("chunks");
    write_file(path, std::string(200 * 1024, 'x') + "\na\nb\nc\n");

    std::string error;
    BOOST_REQUIRE(graphene::utilities::capped_file_appender::trim_to_last_lines(path, 1, error));
    BOOST_TEST(read_file(path) == "c\n");
    std::remove(path.c_str());
}

BOOST_AUTO_TEST_CASE(appender_compacts_while_logging) {
    const std::string path = temp_path("live");
    std::remove(path.c_str());

    graphene::utilities::capped_file_appender::config cfg;
    cfg.filename = path;
    cfg.max_lines = 50;
    cfg.flush = true;

    graphene::utilities::capped_file_appender appender{fc::variant(cfg)};
    for (int i = 0; i < 200; ++i) {
        fc::log_message m(fc::log_context(fc::log_level::info, "test.cpp", 1, "test"), "msg ${n}",
                          fc::mutable_variant_object("n", i));
        appender.log(m);
    }

    const std::vector<std::string> lines = split_lines(read_file(path));
    // Trigger is 50 + max(16, 5) = 66; every compaction leaves the last 50 lines.
    BOOST_TEST(lines.size() <= 66u);
    BOOST_TEST(lines.size() >= 50u);
    BOOST_REQUIRE(!lines.empty());
    BOOST_TEST(lines.back().find("msg 199") != std::string::npos);
    // The retained window is contiguous and ends at the newest message: nothing in the
    // middle was dropped, and the head is exactly as old as the window is long.
    const std::string first = lines.front();
    const std::size_t at = first.find("msg ");
    BOOST_REQUIRE(at != std::string::npos);
    const int first_number = std::atoi(first.c_str() + at + 4);
    BOOST_TEST(first_number == static_cast<int>(200 - lines.size()));
    std::remove(path.c_str());
}

BOOST_AUTO_TEST_CASE(zero_means_unlimited) {
    const std::string path = temp_path("unlimited");
    std::remove(path.c_str());

    graphene::utilities::capped_file_appender::config cfg;
    cfg.filename = path;
    cfg.max_lines = 0;
    cfg.flush = true;

    graphene::utilities::capped_file_appender appender{fc::variant(cfg)};
    for (int i = 0; i < 300; ++i) {
        fc::log_message m(fc::log_context(fc::log_level::info, "test.cpp", 1, "test"), "msg ${n}",
                          fc::mutable_variant_object("n", i));
        appender.log(m);
    }

    BOOST_TEST(split_lines(read_file(path)).size() == 300u);
    std::remove(path.c_str());
}
