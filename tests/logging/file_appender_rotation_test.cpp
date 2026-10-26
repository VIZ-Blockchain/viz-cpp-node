// Contract of the rotating file_appender — the two ways the log used to be lost.
//
// 1. fc::ofstream::mode is fc's OWN enum, not std::ios_base::openmode. The numbers collide
//    (std::ios_base::app == 1 == fc::ofstream::binary), so an ofstream that passed the mode
//    straight into std::ofstream turned every default open (m = binary) into an APPEND:
//    json::save_to_file, aes_save, mmap_struct and the wallet all wrote a second document
//    behind the first. These checks pin the translation: default/out truncate, only the
//    explicit `append` flag appends.
// 2. Rotation reopens the hourly file, so a restart inside the same hour must APPEND to it,
//    not truncate it; the rotation boundary itself must start the next file and keep
//    node.log pointing at the file that is being written; a stale EMPTY file must be
//    removed (it used to survive because its size was read through a relative path).
//
// The last case deliberately runs a 1-second rotation interval, so it takes ~2.5 s.

#define BOOST_TEST_MODULE file_appender_rotation
#include <boost/test/unit_test.hpp>

#include <fc/filesystem.hpp>
#include <fc/io/fstream.hpp>
#include <fc/log/file_appender.hpp>
#include <fc/log/logger.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/time.hpp>
#include <fc/variant.hpp>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define test_pid _getpid
#else
#include <unistd.h>
#define test_pid getpid
#endif

namespace {

std::string temp_dir(const std::string& name) {
   const char* dir = std::getenv("TMPDIR");
   const std::string base = std::string(dir && *dir ? dir : "/tmp") + "/fa_rotation_test_" + name + "_" +
                            std::to_string(static_cast<long>(test_pid()));
   fc::remove_all(base);
   fc::create_directories(base);
   return base;
}

std::string read_file(const std::string& path) {
   std::ifstream in(path.c_str(), std::ios::binary);
   std::ostringstream os;
   os << in.rdbuf();
   return os.str();
}

void write_file(const std::string& path, const std::string& body) {
   std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
   out.write(body.data(), static_cast<std::streamsize>(body.size()));
}

std::size_t count_of(const std::string& text, const std::string& needle) {
   std::size_t n = 0;
   for(std::size_t p = text.find(needle); p != std::string::npos; p = text.find(needle, p + needle.size()))
      ++n;
   return n;
}

// Every existing file named <prefix><something> that has content. Empty hourly files are
// legal in the middle of a rotation sequence, so they are not counted as "the log".
std::vector<std::string> non_empty_logs(const std::string& dir, const std::string& prefix) {
   std::vector<std::string> out;
   for(fc::directory_iterator itr(dir); itr != fc::directory_iterator(); ++itr)
   {
      const std::string name = itr->filename().string();
      if(name.compare(0, prefix.size(), prefix) != 0 || name == prefix)
         continue;
      if(!fc::is_regular_file(*itr))
         continue;
      if(fc::file_size(*itr) > 0)
         out.push_back((fc::path(dir) / name).string());
   }
   return out;
}

fc::time_point_sec floor_to(const fc::time_point_sec& t, uint32_t interval_seconds) {
   return fc::time_point_sec((uint32_t)((t.sec_since_epoch() / interval_seconds) * interval_seconds));
}

// The rotation interval is aligned to the epoch, so a test that expects ONE hourly file must
// not start in the last second before a boundary.
void wait_away_from_boundary(uint32_t interval_seconds) {
   const fc::time_point_sec now(fc::time_point::now());
   const uint32_t next = floor_to(now, interval_seconds).sec_since_epoch() + interval_seconds;
   if(next - now.sec_since_epoch() <= 3)
      std::this_thread::sleep_for(std::chrono::seconds(next - now.sec_since_epoch() + 1));
}

fc::file_appender::config make_config(const std::string& filename, bool rotate,
                                      uint32_t interval_seconds, uint32_t limit_seconds) {
   fc::file_appender::config cfg(filename);
   cfg.flush = true;
   cfg.rotate = rotate;
   cfg.rotation_interval = fc::seconds(interval_seconds);
   cfg.rotation_limit = fc::seconds(limit_seconds);
   return cfg;
}

void log_line(fc::file_appender& appender, const std::string& text) {
   appender.log(fc::log_message(fc::log_context(fc::log_level::info, "test.cpp", 1, "test"), "${m}",
                                fc::mutable_variant_object("m", text)));
}

} // namespace

// --- fc::ofstream mode translation -------------------------------------------------------

BOOST_AUTO_TEST_CASE(default_mode_truncates) {
   const std::string path = temp_dir("default") + "/state.json";
   write_file(path, "first-document");

   {
      fc::ofstream out(path);
      out.write("second", 6);
      out.close();
   }

   BOOST_CHECK_EQUAL(read_file(path), "second");
}

BOOST_AUTO_TEST_CASE(out_binary_truncates) {
   const std::string path = temp_dir("out_binary") + "/state.json";
   write_file(path, "first-document");

   {
      fc::ofstream out(path, fc::ofstream::out | fc::ofstream::binary);
      out.write("second", 6);
      out.close();
   }

   BOOST_CHECK_EQUAL(read_file(path), "second");
}

BOOST_AUTO_TEST_CASE(append_mode_appends) {
   const std::string path = temp_dir("append") + "/node.log";
   write_file(path, "first\n");

   {
      fc::ofstream out(path, fc::ofstream::out | fc::ofstream::append);
      out.write("second\n", 7);
      out.close();
   }

   BOOST_CHECK_EQUAL(read_file(path), "first\nsecond\n");
}

BOOST_AUTO_TEST_CASE(open_reports_failure_instead_of_throwing) {
   const std::string path = temp_dir("no_such_dir") + "/missing/state.json";
   fc::ofstream out;
   out.open(path, fc::ofstream::out | fc::ofstream::binary);
   BOOST_CHECK(!out.is_open());
}

// --- appenders ---------------------------------------------------------------------------

BOOST_AUTO_TEST_CASE(non_rotating_appender_keeps_lines_across_restart) {
   const std::string dir = temp_dir("plain");
   const std::string path = dir + "/plain.log";

   {
      fc::file_appender first(fc::variant(make_config(path, false, 3600, 86400)));
      log_line(first, "before-restart");
   }
   {
      fc::file_appender second(fc::variant(make_config(path, false, 3600, 86400)));
      log_line(second, "after-restart");
   }

   const std::string body = read_file(path);
   BOOST_CHECK_EQUAL(count_of(body, "before-restart"), 1u);
   BOOST_CHECK_EQUAL(count_of(body, "after-restart"), 1u);
}

BOOST_AUTO_TEST_CASE(rotating_appender_keeps_the_hour_across_restart) {
   wait_away_from_boundary(3600);
   const std::string dir = temp_dir("restart");
   const std::string path = dir + "/node.log";

   {
      fc::file_appender first(fc::variant(make_config(path, true, 3600, 86400)));
      log_line(first, "before-restart");
   }
   {
      fc::file_appender second(fc::variant(make_config(path, true, 3600, 86400)));
      log_line(second, "after-restart");
   }

   const std::vector<std::string> files = non_empty_logs(dir, "node.log.");
   BOOST_REQUIRE_EQUAL(files.size(), 1u);
   const std::string body = read_file(files[0]);
   BOOST_CHECK_EQUAL(count_of(body, "before-restart"), 1u);
   BOOST_CHECK_EQUAL(count_of(body, "after-restart"), 1u);
}

BOOST_AUTO_TEST_CASE(rotation_starts_next_file_and_relinks_node_log) {
   const std::string dir = temp_dir("boundary");
   const std::string path = dir + "/node.log";

   fc::file_appender appender(fc::variant(make_config(path, true, 1, 60)));
   log_line(appender, "tick-a");
   std::this_thread::sleep_for(std::chrono::milliseconds(2500));
   log_line(appender, "tick-b");

   const std::vector<std::string> files = non_empty_logs(dir, "node.log.");
   BOOST_REQUIRE_EQUAL(files.size(), 2u);

   std::string current;
   for(const std::string& file : files)
   {
      const std::string body = read_file(file);
      BOOST_CHECK_EQUAL(count_of(body, "tick-a") + count_of(body, "tick-b"), 1u);
      if(count_of(body, "tick-b") == 1)
         current = body;
   }
   BOOST_CHECK_EQUAL(count_of(current, "tick-b"), 1u);

   // The link must follow the file that is being written, otherwise `tail -f node.log` goes
   // silent while the log itself keeps growing.
   BOOST_REQUIRE(fc::exists(path));
   BOOST_CHECK_EQUAL(read_file(path), current);
}

BOOST_AUTO_TEST_CASE(failed_rotation_keeps_logging_into_previous_file) {
   // Start in the first half of a second: the appender, the log line and the blockers must
   // all land BEFORE the next boundary, otherwise which file gets created is a coin toss.
   {
      const int64_t frac = fc::time_point::now().time_since_epoch().count() % 1000000;
      if(frac > 400000)
         std::this_thread::sleep_for(std::chrono::microseconds(1100000 - frac));
   }
   const std::string dir = temp_dir("failopen");
   const std::string path = dir + "/node.log";
   const fc::time_point_sec t0 = floor_to(fc::time_point_sec(fc::time_point::now()), 1);

   // Occupy the next three hourly names with DIRECTORIES: opening a log file over a directory
   // fails, which is the 25.10 production case (full disk / taken path). Rotation must not
   // lose the lines written while it cannot open the next file.
   std::vector<std::string> blockers;
   for(uint32_t i = 1; i <= 3; ++i)
   {
      const std::string name =
          (fc::path(dir) / ("node.log." + fc::time_point_sec(t0.sec_since_epoch() + i).to_non_delimited_iso_string())).string();
      fc::create_directories(name);
      blockers.push_back(name);
   }

   fc::file_appender appender(fc::variant(make_config(path, true, 1, 60)));
   log_line(appender, "before-fail");

   std::this_thread::sleep_for(std::chrono::milliseconds(1500));
   log_line(appender, "during-fail");

   const std::vector<std::string> files = non_empty_logs(dir, "node.log.");
   BOOST_REQUIRE_EQUAL(files.size(), 1u);
   const std::string body = read_file(files[0]);
   BOOST_CHECK_EQUAL(count_of(body, "before-fail"), 1u);
   BOOST_CHECK_EQUAL(count_of(body, "during-fail"), 1u);
   // tail -f must still see the file that is actually being written.
   BOOST_REQUIRE(fc::exists(path));
   BOOST_CHECK_EQUAL(read_file(path), body);

   // Free the names: the timer retries every second and must come back on its own.
   for(const std::string& blocker : blockers)
      fc::remove_all(blocker);
   std::this_thread::sleep_for(std::chrono::milliseconds(2000));
   log_line(appender, "after-recover");

   bool recovered = false;
   for(const std::string& file : non_empty_logs(dir, "node.log."))
   {
      if(count_of(read_file(file), "after-recover") == 1)
         recovered = true;
   }
   BOOST_CHECK(recovered);
   BOOST_CHECK_EQUAL(count_of(read_file(path), "after-recover"), 1u);
}

BOOST_AUTO_TEST_CASE(rotation_removes_stale_empty_file) {
   wait_away_from_boundary(3600);
   const std::string dir = temp_dir("stale_empty");
   const std::string path = dir + "/hygiene.log";

   // A file of the PREVIOUS hour: still inside rotation_limit, so only its size decides.
   // Before the fix its size was read through a name relative to the process cwd, which threw
   // and was swallowed — the empty file lived until rotation_limit expired.
   const fc::time_point_sec hour_start =
       floor_to(fc::time_point_sec(fc::time_point::now()), 3600);
   const fc::time_point_sec stale(hour_start.sec_since_epoch() - 3600);
   const std::string stale_path =
       (fc::path(dir) / ("hygiene.log." + stale.to_non_delimited_iso_string())).string();
   write_file(stale_path, "");
   BOOST_REQUIRE(fc::exists(stale_path));

   fc::file_appender appender(fc::variant(make_config(path, true, 3600, 86400)));
   log_line(appender, "after-cleanup");

   BOOST_CHECK(!fc::exists(stale_path));
   BOOST_CHECK(fc::exists(path));
   BOOST_CHECK_EQUAL(count_of(read_file(path), "after-cleanup"), 1u);
}
