#include <graphene/plugins/block_archive/plugin.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/dlt_block_log.hpp>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include <cstdio>
#include <regex>

namespace graphene {
namespace plugins {
namespace block_archive {

namespace bfs = boost::filesystem;
using graphene::chain::signed_block;

struct plugin::plugin_impl {
    plugin_impl() : db(appbase::app().get_plugin<chain::plugin>().db()) {
    }

    graphene::chain::database &db;
    bfs::path dir;
    uint32_t range = 10000;
    uint32_t archived = 0;              // last block written (partial or sealed)
    graphene::chain::dlt_block_log current;
    uint32_t current_range = UINT32_MAX;
    bool stalled = false;
    boost::signals2::scoped_connection conn;

    static std::string num(uint32_t n) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%010u", n);
        return buf;
    }

    bfs::path partial_file(uint32_t r) const {
        return dir / "partial" / ("blocks-" + num(r * range));
    }

    // Seal: close the partial range and move it to <dir>/blocks-<first>-<last>.
    void seal() {
        if (!current.is_open()) {
            return;
        }
        uint32_t first = current.start_block_num();
        uint32_t last = current.head_block_num();
        current.flush();
        current.close();
        bfs::path src = partial_file(current_range);
        std::string name = "blocks-" + num(first) + "-" + num(last);
        bfs::rename(src.string() + ".index", (dir / (name + ".log.index")).string());
        bfs::rename(src.string(), (dir / (name + ".log")).string());
        ilog("block_archive: sealed ${n}", ("n", name));
        current_range = UINT32_MAX;
    }

    void open_range(uint32_t r) {
        current_range = r;
        current.open(partial_file(r));
    }

    // Restore the cursor from what is on disk.
    void scan() {
        bfs::create_directories(dir / "partial");
        std::regex re("blocks-(\\d{10})-(\\d{10})\\.log");
        for (bfs::directory_iterator it(dir), end; it != end; ++it) {
            std::smatch m;
            std::string f = it->path().filename().string();
            if (std::regex_match(f, m, re)) {
                archived = std::max<uint32_t>(archived, std::stoul(m[2]));
            }
        }
        std::regex pre("blocks-(\\d{10})");
        for (bfs::directory_iterator it(dir / "partial"), end; it != end; ++it) {
            std::smatch m;
            std::string f = it->path().filename().string();
            if (!std::regex_match(f, m, pre)) {
                continue;
            }
            uint32_t r = std::stoul(m[1]) / range;
            if (current.is_open() || (archived && r * range <= archived)) {
                wlog("block_archive: dropping stale partial ${f}", ("f", f));
                bfs::remove(it->path());
                bfs::remove(it->path().string() + ".index");
                continue;
            }
            open_range(r);
            if (current.head_block_num()) {
                archived = current.head_block_num();
            }
        }
        ilog("block_archive: ${d}, range ${r}, last archived block ${a}",
             ("d", dir.string())("r", range)("a", archived));
    }

    void write(const signed_block &b) {
        uint32_t n = b.block_num();
        uint32_t r = n / range;
        if (r != current_range) {
            seal();
            open_range(r);
        }
        current.append(b);
        archived = n;
        if (n % range == range - 1) {
            seal();
        }
    }

    void on_block() {
        if (stalled) {
            return;
        }
        uint32_t lib = db.last_non_undoable_block_num();
        if (lib <= archived) {
            return;
        }
        try {
            uint32_t n = archived ? archived + 1 : lib;
            for (; n <= lib; ++n) {
                auto b = db.fetch_block_by_number(n);
                if (!b) {
                    if (archived) {
                        // A hole would make the archive silently incomplete. Stop and say so; it stays
                        // stopped after a restart too (the cursor is on disk): fill the gap or start a
                        // new archive directory.
                        elog("block_archive: block ${n} is not readable, archiving stopped", ("n", n));
                        stalled = true;
                        return;
                    }
                    continue;
                }
                write(*b);
            }
            current.flush();
        } catch (const fc::exception &e) {
            // The archive is a side copy: never let it break block application.
            elog("block_archive: ${e}, archiving stopped", ("e", e.to_detail_string()));
            stalled = true;
        }
    }
};

plugin::plugin() {
}

plugin::~plugin() {
}

void plugin::set_program_options(
        boost::program_options::options_description &cli,
        boost::program_options::options_description &cfg) {
    cfg.add_options()
        ("block-archive-dir", boost::program_options::value<std::string>()->default_value("block-archive"),
         "Directory for irreversible block range files (relative to data dir)")
        ("block-archive-range", boost::program_options::value<uint32_t>()->default_value(10000),
         "Blocks per archive file");
}

void plugin::plugin_initialize(const boost::program_options::variables_map &options) {
    my.reset(new plugin_impl);
    bfs::path d = options.at("block-archive-dir").as<std::string>();
    if (d.is_relative()) {
        d = appbase::app().data_dir() / d;
    }
    my->dir = d;
    my->range = options.at("block-archive-range").as<uint32_t>();
    FC_ASSERT(my->range >= 100, "block-archive-range must be at least 100");
}

void plugin::plugin_startup() {
    my->scan();
    my->conn = my->db.applied_block.connect([this](const signed_block &) {
        my->on_block();
    });
}

void plugin::plugin_shutdown() {
    my->conn.disconnect();
    if (my->current.is_open()) {
        my->current.flush();
        my->current.close();
    }
}

} } } // graphene::plugins::block_archive
