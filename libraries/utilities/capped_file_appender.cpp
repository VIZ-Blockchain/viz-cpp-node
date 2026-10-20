#include <graphene/utilities/capped_file_appender.hpp>

#include <fc/log/logger.hpp>
#include <fc/reflect/variant.hpp>
#include <fc/time.hpp>
#include <fc/variant.hpp>

#include <boost/thread/mutex.hpp>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#include <process.h>
#define get_pid _getpid
#else
#include <unistd.h>
#define get_pid getpid
#endif

namespace graphene {
namespace utilities {

namespace {

const uint32_t kDefaultMaxLines = 10000;

// The file is compacted only after it grows past the cap by this many lines. Without
// slack every single line past the cap would trigger a full rewrite (O(n) per line);
// with it the rewrite cost is amortised over ~10% of the file. A 10k-line file is a
// couple of megabytes, so the file oscillates between max_lines and 1.1 * max_lines.
uint64_t compaction_trigger(uint32_t max_lines) {
    return static_cast<uint64_t>(max_lines) + std::max<uint64_t>(16, max_lines / 10);
}

// Reading a file backwards costs one pass over the tail only; refuse to walk forever
// through a degenerate file that has no line breaks at all (a single 10 GB "line").
const uint64_t kMaxScanBytes = 64ull * 1024 * 1024;

/**
 * Offset right after the newline that starts the last `keep` lines, or 0 when the
 * whole file is that short. `have_boundary` distinguishes "cut at 0" from "cut at 0
 * because everything fits", which matters for skipping the rewrite.
 */
bool find_tail_offset(const std::string &path, uint32_t keep, uint64_t &cut, bool &found, std::string &error) {
    cut = 0;
    found = false;

    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        error = "cannot open " + path + ": " + std::strerror(errno);
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size <= 0) return true;

    // A trailing newline terminates the last line; it does not open a new one. So a
    // file that ends with '\n' needs keep+1 newline boundaries to cut before line N.
    char last = 0;
    in.seekg(size - 1);
    in.read(&last, 1);
    const uint64_t newlines_wanted = (last == '\n') ? static_cast<uint64_t>(keep) + 1 : keep;

    const std::size_t chunk = 64 * 1024;
    std::streamoff pos = size;
    uint64_t seen = 0;
    uint64_t scanned = 0;

    while (pos > 0) {
        const std::size_t take = static_cast<std::size_t>(std::min<std::streamoff>(chunk, pos));
        pos -= take;
        std::string piece(take, '\0');
        in.seekg(pos);
        in.read(&piece[0], static_cast<std::streamsize>(take));
        if (in.gcount() != static_cast<std::streamsize>(take)) {
            error = "short read from " + path;
            return false;
        }
        for (std::streamoff i = static_cast<std::streamoff>(take) - 1; i >= 0; --i) {
            if (piece[static_cast<std::size_t>(i)] == '\n' && ++seen == newlines_wanted) {
                cut = static_cast<uint64_t>(pos + i + 1);
                found = true;
                return true;
            }
        }
        scanned += take;
        if (scanned >= kMaxScanBytes) {
            error = "no line boundary within " + std::to_string(kMaxScanBytes) + " bytes of " + path;
            return false;
        }
    }
    return true;
}

bool replace_with_tail(const std::string &path, uint64_t cut, std::string &error) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        error = "cannot open " + path + ": " + std::strerror(errno);
        return false;
    }
    in.seekg(static_cast<std::streamoff>(cut));

    const std::string tmp = path + ".trim." + std::to_string(static_cast<long>(get_pid()));
    {
        std::ofstream out(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot create " + tmp + ": " + std::strerror(errno);
            return false;
        }
        out << in.rdbuf();
        out.flush();
        if (!out) {
            error = "cannot write " + tmp + ": " + std::strerror(errno);
            out.close();
            std::remove(tmp.c_str());
            return false;
        }
    }
    in.close();

#ifdef _WIN32
    // std::rename does not replace an existing file on Windows. The appender closes its
    // stream before compaction and reopens afterwards, so deleting first is safe here.
    std::remove(path.c_str());
#endif
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        error = "cannot replace " + path + ": " + std::strerror(errno);
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

std::string format_line(const fc::log_message &m) {
    std::stringstream line;
    {
        int64_t us = m.get_context().get_timestamp().time_since_epoch().count();
        int ms = static_cast<int>((us / 1000) % 1000);
        int64_t s = us / 1000000;
        line << std::setfill('0')
             << std::setw(2) << static_cast<int>((s / 3600) % 24) << ':'
             << std::setw(2) << static_cast<int>((s / 60) % 60) << ':'
             << std::setw(2) << static_cast<int>(s % 60) << '.'
             << std::setw(3) << ms
             << std::setfill(' ') << ' ';
    }
    line << std::setw(21)
         << (m.get_context().get_thread_name().substr(0, 9) + std::string(":") + m.get_context().get_task_name()).c_str()
         << " ";

    std::string method_name = m.get_context().get_method();
    if (!method_name.empty()) {
        uint32_t p = 0;
        for (uint32_t i = 0; i < method_name.size(); ++i) {
            if (method_name[i] == ':') p = i;
        }
        if (method_name[p] == ':') ++p;
        line << std::setw(20) << method_name.substr(p, 20).c_str() << " ";
    }

    line << "] " << fc::format_string(m.get_format(), m.get_data()).c_str();
    return line.str();
}

} // namespace

class capped_file_appender::impl : public fc::retainable {
public:
    config cfg;
    std::ofstream out;
    boost::mutex slock;
    uint64_t lines = 0;
    bool warned = false;

    explicit impl(const config &c) : cfg(c) {
        try {
            if (!cfg.filename.parent_path().string().empty()) fc::create_directories(cfg.filename.parent_path());
        } catch (...) {
            // Reported below by the failed open; a bare filename needs no directory.
        }

        if (cfg.max_lines > 0) {
            std::string error;
            if (!capped_file_appender::trim_to_last_lines(cfg.filename, cfg.max_lines, error)) {
                warn(error);
            }
            lines = count_lines_of_file();
        }
        open_append();
    }

    void open_append() {
        out.open(cfg.filename.string().c_str(), std::ios::out | std::ios::app | std::ios::binary);
        if (!out.is_open()) {
            error("cannot open log file " + cfg.filename.string() + ": " + std::strerror(errno));
        }
    }

    void error(const std::string &text) const {
        std::cerr << "capped_file_appender: " << text << "\n";
    }

    void warn(const std::string &text) {
        if (warned) return;
        warned = true;
        std::cerr << "capped_file_appender: " << text << " (log cap is not enforced)\n";
    }

    uint64_t count_lines_of_file() const {
        uint64_t n = 0;
        std::string error_text;
        if (!capped_file_appender::count_lines_in_file(cfg.filename, n, error_text)) {
            // Counting is best-effort here: a wrong count only delays or advances the next
            // compaction, so report it and keep logging rather than dropping the appender.
            error(error_text);
            return 0;
        }
        return n;
    }

    // Caller holds slock. Rewrites the file to its last max_lines lines and reopens.
    void compact() {
        out.flush();
        out.close();

        std::string error_text;
        if (capped_file_appender::trim_to_last_lines(cfg.filename, cfg.max_lines, error_text)) {
            lines = count_lines_of_file();
        } else {
            // Do not retry on every following line: wait for the next trigger window.
            warn(error_text);
            lines = 0;
        }
        open_append();
    }
};

capped_file_appender::config::config(const fc::path &p) :
    filename(p),
    max_lines(kDefaultMaxLines),
    flush(true) {
}

capped_file_appender::capped_file_appender(const fc::variant &args) :
    my(new impl(args.as<config>())) {
}

capped_file_appender::~capped_file_appender() = default;

bool capped_file_appender::count_lines_in_file(const fc::path &path, uint64_t &lines, std::string &error) {
    lines = 0;
    error.clear();

    const std::string name = path.string();
    std::ifstream in(name.c_str(), std::ios::binary);
    if (!in) {
        if (errno == ENOENT) return true; // nothing written yet
        error = "cannot open " + name + ": " + std::strerror(errno);
        return false;
    }

    std::vector<char> buf(64 * 1024);
    char last = 0;
    bool any = false;
    while (in) {
        in.read(&buf[0], static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = in.gcount();
        if (got <= 0) break;
        lines += static_cast<uint64_t>(
            std::count(buf.begin(), buf.begin() + static_cast<std::size_t>(got), '\n'));
        last = buf[static_cast<std::size_t>(got) - 1];
        any = true;
    }
    if (in.bad()) {
        error = "read error on " + name;
        lines = 0;
        return false;
    }
    // A file that does not end in '\n' still has that last partial line.
    if (any && last != '\n') ++lines;
    return true;
}

bool capped_file_appender::trim_to_last_lines(const fc::path &path, uint32_t keep, std::string &error) {
    error.clear();
    if (keep == 0) return true;

    const std::string name = path.string();
    {
        std::ifstream probe(name.c_str(), std::ios::binary);
        if (!probe) return true; // nothing to trim yet
    }

    uint64_t cut = 0;
    bool found = false;
    if (!find_tail_offset(name, keep, cut, found, error)) return false;
    if (!found || cut == 0) return true; // the whole file fits into the cap

    return replace_with_tail(name, cut, error);
}

void capped_file_appender::log(const fc::log_message &m) {
    const std::string line = format_line(m);

    boost::mutex::scoped_lock lock(my->slock);
    if (!my->out.is_open()) return;

    my->out << line << "\t\t\t" << m.get_context().get_file() << ":" << m.get_context().get_line_number() << "\n";
    if (my->cfg.flush) my->out.flush();
    ++my->lines;

    if (my->cfg.max_lines > 0 && my->lines >= compaction_trigger(my->cfg.max_lines)) {
        my->compact();
    }
}

} // namespace utilities
} // namespace graphene
