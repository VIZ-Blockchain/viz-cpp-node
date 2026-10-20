#include <graphene/plugins/node_admin/node_admin_plugin.hpp>
#include <graphene/plugins/node_admin/node_admin_util.hpp>
#include <graphene/plugins/node_admin/objects_check.hpp>

#include <graphene/plugins/p2p/p2p_plugin.hpp>
#include <graphene/plugins/snapshot/plugin.hpp>
#include <graphene/plugins/validator_guard/validator_guard.hpp>
#include <graphene/plugins/validator/validator.hpp>
#include <graphene/plugins/webserver/webserver_plugin.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/chain/validator_objects.hpp>
#include <graphene/protocol/version.hpp>

#include <appbase/application.hpp>

#include <fc/crypto/sha256.hpp>
#include <fc/io/json.hpp>
#include <fc/log/logger.hpp>
#include <fc/time.hpp>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

// Must precede any websocketpp header: restores the `io_service` alias that
// Boost >= 1.87 dropped but the vendored websocketpp still spells throughout.
#include <fc/compat/asio_compat.hpp>

#include <websocketpp/config/asio.hpp>
#include <websocketpp/server.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#endif

namespace graphene {
namespace plugins {
namespace node_admin {

namespace bpo = boost::program_options;
namespace bfs = boost::filesystem;
namespace asio = boost::asio;
using boost::asio::ip::tcp;
using websocketpp::connection_hdl;
// CHAIN_VERSION expands to an unqualified `version(4, 2, 0)`; same import as vizd's main.cpp.
using graphene::protocol::version;

namespace {

using util::cfg_entry;
using util::classify_config_line;
using util::edit_plugin_lines;
using util::is_plain_snapshot_name;
using util::join_lines;
using util::line_edit;
using util::mask_text;
using util::normalize_config_value;
using util::parse_config_file;
using util::parse_token_spec;
using util::plugin_removal_forbidden;
using util::sensitive_key;
using util::set_config_value;
using util::split;
using util::split_lines;
using util::to_lower;
using util::token_info;
using util::trim;

// appbase exposes only a typed find_plugin<T>(); the name-based lookup is protected and the
// plugin registry has no public enumeration. So the config list is the visible population, and
// the state below is genuinely observed only for the plugins whose type this TU links.
const appbase::abstract_plugin *observe_plugin(const std::string &name) {
    if (name == "chain") return appbase::app().find_plugin<graphene::plugins::chain::plugin>();
    if (name == "p2p") return appbase::app().find_plugin<graphene::plugins::p2p::p2p_plugin>();
    if (name == "snapshot") return appbase::app().find_plugin<graphene::plugins::snapshot::snapshot_plugin>();
    if (name == "webserver") return appbase::app().find_plugin<graphene::plugins::webserver::webserver_plugin>();
    if (name == "validator_guard") return appbase::app().find_plugin<graphene::plugins::validator_guard::validator_guard_plugin>();
    if (name == "validator") return appbase::app().find_plugin<graphene::plugins::validator_plugin::validator_plugin>();
    if (name == node_admin_plugin::name()) return appbase::app().find_plugin<node_admin_plugin>();
    return nullptr;
}

// ── chainbase introspection ─────────────────────────────────────────────────────────────────
// The shared-memory image is the node's whole state. Its file size and the index list are the
// two facts an operator can see from inside a running process; anything deeper (page checks,
// checksum) needs the file closed, so "repair" is a marker honoured at the next startup.
bool ends_with_text(const std::string &s, const char *suffix) {
    const std::size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

bool read_uint_file(const bfs::path &p, uint32_t &out) {
    std::ifstream f(p.string());
    if (!f) return false;
    uint32_t v = 0;
    f >> v;
    if (f.fail()) return false;
    out = v;
    return true;
}

// The facts live in objects_check.hpp together with the judge, so the verdict branches can be
// driven by unit tests without chainbase: see tests/node_admin/node_admin_objects_test.cpp.
using objects_state = objects_check::state;

// ── limits ──────────────────────────────────────────────────────────────────────────────────
const std::size_t kMaxBodyBytes = 64 * 1024;
const std::size_t kMaxLogBytes = 64 * 1024;
const std::size_t kMaxLines = 2000;
const std::size_t kMaxSnapshotPage = 200;
const std::size_t kAuditRotateBytes = 16 * 1024 * 1024;
const uint32_t kRatePerSecond = 20;
// Snapshot files are served in one HTTP response, so the body has to fit in memory.
// VIZ mainnet snapshots are ~4 MB compressed; the cap is for a pathological file, not
// the normal case. Larger files stay downloadable with scp from the node.
const uint64_t kMaxSnapshotDownloadBytes = 512ULL * 1024 * 1024;

/// Constant-time comparison for equal-length strings. Token hashes are always 64 hex chars,
/// so an early size check leaks nothing beyond "this is not a sha256 hex digest".
bool ct_equal(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return diff == 0;
}

std::string iso_utc(const fc::time_point_sec &t) {
    return t.to_iso_string() + "Z";
}

std::string now_iso() {
    return iso_utc(fc::time_point_sec(static_cast<uint32_t>(fc::time_point::now().sec_since_epoch())));
}

std::string unixtime_iso(uint32_t t) {
    return iso_utc(fc::time_point_sec(t));
}

uint32_t now_unixtime() {
    return static_cast<uint32_t>(fc::time_point::now().sec_since_epoch());
}

// ── config file IO ────────────────────────────────────────────────────────────────────────
bool read_text_file(const std::string &path, std::string &out, std::string &why) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        why = "cannot open " + path;
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

std::string text_sha256(const std::string &text) {
    return fc::sha256::hash(text).str();
}

/// Current value of a top-level key, for the plan diff. Keys inside [sections] are a different
/// namespace and are not touched by the editor.
bool top_level_value(const std::vector<std::string> &lines, const std::string &key,
                     std::string &value) {
    std::string section;
    bool found = false;
    for (const std::string &raw : lines) {
        const util::cfg_line l = util::classify_config_line(raw, section);
        if (l.kind == util::cfg_line::header) {
            section = l.section;
            continue;
        }
        if (l.kind == util::cfg_line::entry && l.section.empty() && l.key == key) {
            value = l.value;
            found = true;
        }
    }
    return found;
}

/// Apply one allow-listed key to the candidate lines, choosing the namespace by the spec: the 29
/// top-level options keep their old behaviour, and the logging keys (`logger.*`, `log.*`) are
/// edited inside their ini section. Both plan and apply go through here, so the diff shown to the
/// operator and the file that is actually written cannot disagree.
line_edit set_allowed_config_value(std::vector<std::string> lines, const std::string &key,
                                   const std::string &value) {
    const util::config_spec *spec = util::config_spec_for(key);
    if (!spec) {
        line_edit out{std::move(lines), 0, "key '" + key + "' is not in the admin allow-list"};
        return out;
    }
    std::string section;
    std::string leaf;
    util::config_name_parts(*spec, section, leaf);
    if (section.empty()) return util::set_config_value(std::move(lines), leaf, value);
    return util::set_config_value_in_section(std::move(lines), section, leaf, value);
}

/// Current value of an allow-listed key in whichever namespace its spec names.
bool allowed_config_value(const std::vector<std::string> &lines, const std::string &key,
                          std::string &value) {
    const util::config_spec *spec = util::config_spec_for(key);
    if (!spec) return false;
    std::string section;
    std::string leaf;
    util::config_name_parts(*spec, section, leaf);
    if (section.empty()) return top_level_value(lines, leaf, value);
    return util::config_value_in_section(lines, section, leaf, value);
}

/// Replace config.ini atomically: copy the current file to `<path>.bak-<unixtime>`, write the
/// candidate to a sibling temp file, fsync it, rename over the original and fsync the directory.
/// A failure before the rename leaves the original untouched; a crash after it leaves either
/// the old or the new file, never a half-written one.
bool write_config_atomic(const std::string &path, const std::string &body,
                         std::string &backup_path, std::string &why) {
    const bfs::path target(path);
    const bfs::path dir = target.parent_path();
    boost::system::error_code ec;

    if (bfs::exists(target, ec)) {
        const std::string stamp = std::to_string(static_cast<uint64_t>(now_unixtime()));
        backup_path = path + ".bak-" + stamp;
        for (int n = 1; bfs::exists(backup_path, ec); ++n) {
            backup_path = path + ".bak-" + stamp + "-" + std::to_string(n);
        }
        bfs::copy_file(target, bfs::path(backup_path), bfs::copy_options::none, ec);
        if (ec) {
            why = "cannot back up " + path + ": " + ec.message();
            return false;
        }
    }

#ifdef _WIN32
    const std::string tmp = path + ".tmp";
#else
    const std::string tmp = path + ".tmp." + std::to_string(::getpid());
#endif
    {
        std::ofstream out(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!out) {
            why = "cannot write " + tmp;
            return false;
        }
        out.write(body.data(), static_cast<std::streamsize>(body.size()));
        out.flush();
        if (!out) {
            why = "short write to " + tmp;
            return false;
        }
    }
#ifndef _WIN32
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) ::chmod(tmp.c_str(), st.st_mode & 07777);
    const int fd = ::open(tmp.c_str(), O_RDONLY);
    if (fd >= 0) {
        if (::fsync(fd) != 0) {
            ::close(fd);
            why = "cannot fsync " + tmp;
            return false;
        }
        ::close(fd);
    }
#endif
    bfs::rename(bfs::path(tmp), target, ec);
    if (ec) {
        why = "cannot replace " + path + ": " + ec.message();
        return false;
    }
#ifndef _WIN32
    const int dfd = ::open(dir.string().c_str(), O_RDONLY | O_DIRECTORY);
    if (dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    }
#endif
    return true;
}

/// Plan ids are bearer capabilities for one single use, so they come from random_device rather
/// than from a counter: an id that can be guessed is an id somebody else can spend.
std::string random_plan_id() {
    std::random_device rd;
    std::mt19937_64 gen((static_cast<uint64_t>(rd()) << 32) ^ rd());
    std::uniform_int_distribution<uint64_t> dist;
    const uint64_t a = dist(gen);
    const uint64_t b = dist(gen);
    char buf[33];
    std::snprintf(buf, sizeof(buf), "%016llx%016llx",
                  static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
    return std::string(buf, 32);
}

// ── tail a file ────────────────────────────────────────────────────────────────────────────
struct tail_result {
    bool ok = false;
    std::string error;
    uint64_t size = 0;
    uint64_t bytes_returned = 0;
    std::vector<std::string> lines;
};

int64_t elapsed_ms(const fc::time_point &started) {
    return (fc::time_point::now() - started).count() / 1000;
}

// The admin API is machine-facing: emit real JSON numbers. fc's default formatting quotes every
// int64 above 2^32-1 and every double, which turns byte counts and load averages into strings
// and forces the client to parse them twice.
std::string to_json(const fc::variant &v) {
    return fc::json::to_string(v, fc::json::legacy_generator);
}

int64_t param_int(const fc::variant_object &o, const std::string &key, int64_t def) {
    auto it = o.find(key);
    if (it == o.end()) return def;
    try {
        return it->value().as_int64();
    } catch (...) {
        return def;
    }
}

std::string param_str(const fc::variant_object &o, const std::string &key, const std::string &def) {
    auto it = o.find(key);
    if (it == o.end() || !it->value().is_string()) return def;
    return it->value().as_string();
}

tail_result tail_file(const std::string &path, uint64_t bytes, std::size_t max_lines) {
    tail_result r;
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        r.error = "cannot open file";
        return r;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end < 0) {
        r.error = "cannot stat file";
        return r;
    }
    r.size = static_cast<uint64_t>(end);
    const uint64_t take = std::min<uint64_t>(bytes, r.size);
    in.seekg(static_cast<std::streamoff>(r.size - take), std::ios::beg);
    std::string buf(static_cast<std::size_t>(take), '\0');
    in.read(&buf[0], static_cast<std::streamsize>(take));
    buf.resize(static_cast<std::size_t>(in.gcount()));
    r.bytes_returned = buf.size();

    std::vector<std::string> all = split(buf, '\n');
    // A tail that starts mid-line produces a truncated first line; drop it unless we read
    // the whole file (then the first line is genuine).
    if (take < r.size && !all.empty()) all.erase(all.begin());
    // Files end with a newline, so the final split element is an empty string — not a line.
    if (!all.empty() && all.back().empty()) all.pop_back();
    if (all.size() > max_lines) {
        all.erase(all.begin(), all.end() - static_cast<std::ptrdiff_t>(max_lines));
    }
    r.lines = std::move(all);
    r.ok = true;
    return r;
}

// ── built-in UI ──────────────────────────────────────────────────────────────────────────
// A single static page served at /ui/. It holds no secrets and no state: the operator
// pastes a bearer token, the page keeps it in sessionStorage and calls the same JSON-RPC
// endpoint. Vanilla JS, no build step, no external assets (CSP forbids them anyway).
const char *ui_page() {
    return R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>VIZ node admin</title>
<style>
  :root { color-scheme: dark; --bg:#111418; --panel:#1a1f26; --line:#2b323c;
          --fg:#e6e9ee; --dim:#8b949e; --ok:#3fb950; --warn:#d29922; --bad:#f85149;
          --acc:#58a6ff; }
  * { box-sizing: border-box; }
  body { margin:0; background:var(--bg); color:var(--fg);
         font:14px/1.45 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; }
  header { display:flex; flex-wrap:wrap; gap:10px; align-items:center; padding:14px 18px;
           border-bottom:1px solid var(--line); background:var(--panel); }
  h1 { font-size:16px; margin:0 10px 0 0; font-weight:600; }
  h2 { font-size:13px; text-transform:uppercase; letter-spacing:.08em; color:var(--dim);
       margin:0 0 10px; font-weight:600; }
  main { padding:18px; display:grid; gap:18px; max-width:1200px; }
  input[type=password], input[type=text], select { background:#0d1117; color:var(--fg);
       border:1px solid var(--line); border-radius:6px; padding:7px 9px; font:inherit; }
  button { background:#21262d; color:var(--fg); border:1px solid var(--line); border-radius:6px;
           padding:7px 12px; font:inherit; cursor:pointer; }
  button:hover { border-color:var(--acc); }
  button.danger:hover { border-color:var(--bad); }
  button:disabled { opacity:.45; cursor:default; }
  nav.tabs { display:flex; flex-wrap:wrap; gap:4px; padding:10px 18px 0;
             border-bottom:1px solid var(--line); }
  nav.tabs button { background:transparent; border:1px solid transparent; border-bottom:0;
                    border-radius:8px 8px 0 0; color:var(--dim); padding:9px 14px; }
  nav.tabs button:hover { color:var(--fg); border-color:var(--line); }
  nav.tabs button.active { background:var(--bg); color:var(--fg);
                           border-color:var(--line); border-bottom:1px solid var(--bg);
                           margin-bottom:-1px; }
  /* Per-plugin panel under the plugins table: one tab per plugin that has a status or
     editable parameters. Own class, so activate() (nav.tabs) never touches it. */
  nav.ptabs { display:flex; flex-wrap:wrap; gap:4px; border-bottom:1px solid var(--line); }
  nav.ptabs button { background:transparent; border:1px solid transparent; border-bottom:0;
                     border-radius:8px 8px 0 0; color:var(--dim); padding:7px 12px; }
  nav.ptabs button:hover { color:var(--fg); border-color:var(--line); }
  nav.ptabs button.active { background:var(--panel); color:var(--fg); border-color:var(--line);
                            border-bottom:1px solid var(--panel); margin-bottom:-1px; }
  .ppanel { background:var(--panel); border:1px solid var(--line); border-top:0;
            border-radius:0 0 8px 8px; padding:12px; }
  .ppanel h2 { margin:16px 0 8px; }
  .statusview { margin-top:8px; white-space:pre-wrap; font-size:12px; max-height:320px;
                overflow:auto; }
  tr.sel td { background:#1f2733; }
  .chip { border:1px solid var(--line); border-radius:999px; padding:3px 10px; font-size:12px;
          color:var(--dim); }
  .chip.ok { color:var(--ok); border-color:var(--ok); }
  .chip.warn { color:var(--warn); border-color:var(--warn); }
  .chip.bad { color:var(--bad); border-color:var(--bad); }
  .notice { padding:8px 18px; border-bottom:1px solid var(--line); background:#0d1117;
            font-size:12.5px; }
  .notice div { padding:1px 0; white-space:pre-wrap; }
  .cards { display:grid; grid-template-columns:repeat(auto-fit,minmax(170px,1fr)); gap:10px; }
  .card { background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:10px 12px; }
  .card .k { color:var(--dim); font-size:11px; text-transform:uppercase; letter-spacing:.06em; }
  .card .v { font-size:19px; margin-top:2px; }
  .card .s { color:var(--dim); font-size:11px; margin-top:2px; }
  .card .v.bad, .bad { color:var(--bad); } .ok { color:var(--ok); } .warn { color:var(--warn); }
  .dim { color:var(--dim); }
  .tablewrap { overflow:auto; border:1px solid var(--line); border-radius:8px; }
  table { width:100%; border-collapse:collapse; background:var(--panel); }
  th, td { padding:7px 10px; text-align:left; white-space:nowrap;
           border-bottom:1px solid var(--line); font-size:12.5px; }
  th { color:var(--dim); font-weight:600; font-size:11px; text-transform:uppercase;
       letter-spacing:.06em; }
  tr:last-child td { border-bottom:0; }
  .actions { display:flex; flex-wrap:wrap; gap:8px; align-items:center; }
  .actions input[type=text] { min-width:240px; }
  .logview { margin:0; background:var(--panel); border:1px solid var(--line); border-radius:8px;
             padding:10px 12px; min-height:60px; max-height:62vh; overflow:auto;
             font-size:12.5px; }
  .logview div { white-space:pre-wrap; word-break:break-word; }
  /* Radial progress on a button that is waiting for a slow HTTP response. The button is
     disabled for the whole request, so a second click cannot start a second download. */
  button.loading { position:relative; padding-left:30px; opacity:.75; }
  button.loading::before { content:''; position:absolute; left:9px; top:50%; width:12px;
      height:12px; margin-top:-6px; border:2px solid var(--line); border-top-color:var(--acc);
      border-radius:50%; animation:spin .7s linear infinite; }
  @keyframes spin { to { transform:rotate(360deg); } }
</style>
</head>
<body>
<header>
  <h1>VIZ node admin</h1>
  <input id="token" type="password" placeholder="bearer token" autocomplete="off" size="36">
  <button id="save">Save token</button>
  <span id="who" class="chip">no token</span>
  <span id="state" class="dim">—</span>
</header>
<div id="notice" class="notice" hidden></div>
<nav class="tabs">
  <button data-tab="status" class="active">Status</button>
  <button data-tab="peers">Peers</button>
  <button data-tab="snapshots">Snapshots</button>
  <button data-tab="logs">Logs</button>
  <button data-tab="plugins">Plugins</button>
  <button data-tab="objects">Objects</button>
  <button data-tab="validators">Validators</button>
</nav>
<main>
  <section id="tab-status">
    <h2>Node</h2>
    <div class="cards" id="cards"></div>
  </section>

  <section id="tab-peers" hidden>
    <h2>Peers</h2>
    <div class="actions" id="peerActions" style="margin-bottom:10px" hidden>
      <input id="endpoint" type="text" placeholder="IP:port, e.g. 1.2.3.4:1776" autocomplete="off">
      <button id="add">Add node</button>
      <button id="addTrusted">Add as trusted</button>
      <button id="clearBanned" class="danger">Clear banned list</button>
    </div>
    <div id="peerRo" class="dim" style="margin-bottom:10px" hidden>
      read-only token: peer actions are hidden
    </div>
    <div class="tablewrap">
      <table>
        <thead><tr><th>Endpoint</th><th>State</th><th>Dir</th><th>Head</th><th>Lag</th>
          <th>Peer</th><th>Flags</th><th>Since</th><th>Note</th></tr></thead>
        <tbody id="peers"></tbody>
      </table>
    </div>
  </section>

  <section id="tab-snapshots" hidden>
    <h2>Snapshots</h2>
    <div class="cards" id="snapCards"></div>
    <div class="actions" id="snapActions" style="margin:10px 0" hidden>
      <button id="snapCreate">Create snapshot</button>
      <button id="snapRestoreLatest" class="danger">Restore latest</button>
      <span class="dim">restore wipes shared memory and re-imports the chosen snapshot in-process</span>
    </div>
    <div id="snapRo" class="dim" style="margin:10px 0" hidden>
      read-only token: create and restore are hidden
    </div>
    <div class="tablewrap">
      <table>
        <thead><tr><th>Block</th><th>File</th><th>Size</th><th>Age</th><th>Actions</th></tr></thead>
        <tbody id="snapshots"></tbody>
      </table>
    </div>
    <div class="actions" style="margin-top:10px">
      <button id="snapMore" hidden>Load more</button>
      <span id="snapCount" class="dim"></span>
    </div>
  </section>

  <section id="tab-logs" hidden>
    <h2>Logs</h2>
    <div class="actions" style="margin-bottom:10px">
      <select id="logName"></select>
      <button id="logToggle">Play</button>
      <button id="logClear">Clear</button>
      <span id="logState" class="dim">paused</span>
      <span class="dim">live tail starts paused; Play appends new lines at the bottom</span>
    </div>
    <pre id="logView" class="logview"><span class="dim">no token — enter one above</span></pre>
  </section>

  <section id="tab-plugins" hidden>
    <h2>Plugins</h2>
    <div class="actions" id="plugBanner" hidden style="margin-bottom:10px">
      <span id="plugBannerText" class="warn"></span>
      <button id="plugRestart" class="danger">Restart node</button>
    </div>
    <div class="tablewrap">
      <table>
        <thead><tr><th>Plugin</th><th>Configured</th><th>Observed</th><th>Parameters</th><th></th></tr></thead>
        <tbody id="plugRows"></tbody>
      </table>
    </div>
    <div class="actions" id="plugAddBox" style="margin-top:10px" hidden>
      <select id="plugAddName"></select>
      <button id="plugAdd">Add plugin</button>
      <span class="dim">writes config.ini; the plugin starts on the next node restart</span>
    </div>
    <div id="plugRo" class="dim" style="margin-top:10px" hidden>
      read-only token — plugin changes need the plugins scope
    </div>

    <div id="plugDetail" style="margin-top:18px" hidden>
      <nav class="ptabs" id="plugTabs"></nav>
      <div class="ppanel">
        <div class="actions" id="plugHead"></div>
        <div id="plugStatusBlock">
          <h2>Status</h2>
          <div class="actions"><button id="plugStatusRefresh">Refresh</button>
            <span id="plugStatusNote" class="dim"></span></div>
          <div id="plugStatus" class="statusview"></div>
        </div>
        <div id="plugParamBlock">
          <h2>Parameters</h2>
          <div id="paramBox" class="dim">no token — enter one above</div>
          <div class="actions" id="paramActions" style="margin-top:10px" hidden>
            <button id="paramSave">Save parameters</button>
            <span id="paramState" class="dim"></span>
          </div>
        </div>
      </div>
    </div>

    <h2 style="margin-top:18px">Logging</h2>
    <div id="logSettings" class="dim">no token — enter one above</div>
    <div class="actions" id="logSettingsActions" style="margin-top:10px" hidden>
      <button id="logSettingsSave">Save log settings</button>
      <span id="logSettingsState" class="dim"></span>
    </div>
  </section>

  <section id="tab-objects" hidden>
    <h2>Objects</h2>
    <div class="actions" style="margin-bottom:10px">
      <button id="objCheck">Check</button>
      <span id="objVerdict" class="dim">not checked yet</span>
    </div>
    <div id="objSummary" class="dim">no token — enter one above</div>
    <div id="objFindings" hidden style="margin-top:10px"></div>
    <div class="tablewrap" style="margin-top:10px">
      <table>
        <thead><tr><th>Object type</th><th>type_id</th><th>Objects</th><th>Index revision</th></tr></thead>
        <tbody id="objRows"></tbody>
      </table>
    </div>

    <h2 style="margin-top:18px">Repair</h2>
    <div class="dim" id="objRepairHint">
      Repair writes a marker and restarts the node; the next startup rebuilds state.
      Check first: the verdict names which mode fits.
    </div>
    <div class="actions" style="margin-top:8px">
      <select id="objRepairMode">
        <option value="replay">replay from block log (keeps blocks)</option>
        <option value="resync">full resync (drops state and blocks)</option>
      </select>
      <button id="objRepair" class="danger">Restart with repair</button>
      <span id="objRepairState" class="dim"></span>
    </div>
  </section>

  <section id="tab-validators" hidden>
    <h2>Active validators</h2>
    <div class="cards" id="valCards"></div>
    <div class="tablewrap" style="margin-top:12px">
      <table>
        <thead><tr><th>#</th><th>Validator in the current schedule</th></tr></thead>
        <tbody id="valNames"></tbody>
      </table>
    </div>
    <h2 style="margin-top:18px">This node's validators (validator_guard)</h2>
    <div id="guardBox" class="dim">not loaded</div>
  </section>
</main>
<script>
(function () {
  'use strict';
  var $ = function (id) { return document.getElementById(id); };
  var token = sessionStorage.getItem('vizAdminToken') || '';
  // The page is served at "/ui/" when the listener is reached directly, but an operator
  // may publish it behind an nginx prefix (e.g. "/admin/ui/"). JSON-RPC and snapshot
  // downloads then live under that same prefix, so derive it from our own URL.
  var base = location.pathname.replace(/\/ui\/?$/, '/');
  var scopes = null;            // null until admin.auth.me answers
  var activeTab = sessionStorage.getItem('vizAdminTab') || 'status';
  var tickTimer = null;
  var snapItems = [];
  var snapHasMore = false;
  var snapLoading = false;
  var snapPaged = false;      // the operator pressed Load more: stop auto-refreshing page 1
  var downloading = Object.create(null);
  var logLines = [];
  var logName = '';
  var logPlaying = false;
  var logTimer = null;
  var logBusy = false;
  var logPendingReset = false;
  var cfgAllow = [];          // admin.config.get allow[] — what the parameter panels render
  var cfgAllowKnown = false;  // false until config.get answers (older builds omit allow)
  var plugInfo = Object.create(null);  // admin.plugins.list entries by name
  var plugSel = sessionStorage.getItem('vizAdminPlugin') || '';  // plugin open in the panel

  function hasScope(s) { return !!(scopes && scopes.indexOf(s) >= 0); }

  function notice(msg, cls) {
    var n = $('notice');
    n.hidden = false;
    var d = document.createElement('div');
    d.textContent = new Date().toLocaleTimeString() + '  ' + msg;
    if (cls) d.className = cls;
    n.appendChild(d);
    while (n.children.length > 4) n.removeChild(n.firstChild);
  }

  function setState(msg, cls) {
    var el = $('state');
    el.textContent = msg;
    el.className = cls || 'dim';
  }

  function rpc(method, params) {
    if (!token) return Promise.reject(new Error('no token'));
    return fetch(base, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Authorization': 'Bearer ' + token },
      body: JSON.stringify({ jsonrpc: '2.0', id: Date.now(), method: method, params: params || {} })
    }).then(function (res) {
      return res.json().then(function (body) { return { status: res.status, body: body }; });
    }).then(function (r) {
      if (r.body && r.body.error) {
        var e = new Error(r.body.error.message +
          (r.body.error.data && r.body.error.data.reason ? ': ' + r.body.error.data.reason : ''));
        e.rpcCode = r.body.error.code;
        e.httpStatus = r.status;
        throw e;
      }
      if (!r.body || !('result' in r.body)) throw new Error('HTTP ' + r.status + ': empty response');
      return r.body.result;
    });
  }

  function uptime(sec) {
    sec = Number(sec) || 0;
    var d = Math.floor(sec / 86400), h = Math.floor(sec % 86400 / 3600),
        m = Math.floor(sec % 3600 / 60), s = sec % 60;
    return (d ? d + 'd ' : '') + (d || h ? h + 'h ' : '') + m + 'm ' + s + 's';
  }

  function card(label, value, sub, cls) {
    var el = document.createElement('div');
    el.className = 'card';
    var k = document.createElement('div'); k.className = 'k'; k.textContent = label;
    var v = document.createElement('div'); v.className = 'v' + (cls ? ' ' + cls : '');
    v.textContent = value == null ? '-' : String(value);
    el.appendChild(k); el.appendChild(v);
    if (sub) { var s = document.createElement('div'); s.className = 's'; s.textContent = sub;
               el.appendChild(s); }
    return el;
  }

  function lagText(lag) {
    lag = Number(lag) || 0;
    if (lag > 0) return '+' + lag;
    if (lag < 0) return String(lag);
    return '0';
  }

  function sizeText(bytes) {
    if (bytes == null) return '-';
    bytes = Number(bytes) || 0;
    if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(2) + ' GB';
    if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + ' MB';
    if (bytes >= 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return bytes + ' B';
  }

  function ageText(sec) {
    if (sec == null) return '-';
    sec = Number(sec);
    if (isNaN(sec)) return '-';
    if (sec < 0) sec = 0;
    if (sec < 120) return sec + 's';
    if (sec < 7200) return Math.floor(sec / 60) + 'm';
    if (sec < 172800) return Math.floor(sec / 3600) + 'h';
    return Math.floor(sec / 86400) + 'd';
  }

  function button(label, cls, fn) {
    var b = document.createElement('button');
    b.textContent = label;
    if (cls) b.className = cls;
    b.addEventListener('click', fn);
    return b;
  }

  // ── identity and scopes ────────────────────────────────────────────────────────────────
  function applyScopes() {
    var canSnap = hasScope('snapshot');
    var canP2p = hasScope('p2p');
    // The "read-only" note appears only once we actually know the scopes; before that the
    // page simply has no token and the missing token is the message.
    var known = scopes !== null;
    $('snapActions').hidden = !canSnap;
    $('snapRo').hidden = !known || canSnap;
    $('peerActions').hidden = !canP2p;
    $('peerRo').hidden = !known || canP2p;
    $('snapCreate').disabled = !canSnap;
    $('snapRestoreLatest').disabled = !canSnap;
    // The banner outlives a token change: it reports a file that was already written, so only
    // the button is gated. Hiding the banner would hide the fact that a restart is pending.
    $('plugRestart').disabled = !hasScope('process');
    $('objRepair').disabled = !hasScope('process');
  }

  function refreshAuth() {
    if (!token) {
      scopes = null;
      $('who').textContent = 'no token';
      $('who').className = 'chip warn';
      setState('—');
      applyScopes();
      return Promise.resolve(false);
    }
    return rpc('admin.auth.me').then(function (r) {
      scopes = r.scopes || [];
      $('who').textContent = r.label + ' · ' + (scopes.join(',') || 'no scopes');
      $('who').className = 'chip ' + ((hasScope('snapshot') || hasScope('p2p')) ? 'ok' : 'warn');
      $('who').title = r.expires ? 'expires ' + new Date(r.expires * 1000).toLocaleString() : 'no expiry';
      setState('connected · ' + new Date().toLocaleTimeString());
      applyScopes();
      return true;
    }).catch(function (e) {
      scopes = null;
      if (e.rpcCode === -32001 || e.httpStatus === 401) {
        setState('token rejected (401)', 'bad');
        $('who').textContent = 'rejected';
        $('who').className = 'chip bad';
      } else {
        setState('error: ' + e.message, 'bad');
        $('who').textContent = 'error';
        $('who').className = 'chip bad';
      }
      applyScopes();
      return false;
    });
  }

  // ── status / peers ─────────────────────────────────────────────────────────────────────
  function renderPeers(peers, err) {
    var body = $('peers');
    body.textContent = '';
    if (!peers || !peers.length) {
      var tr = document.createElement('tr');
      var td = document.createElement('td');
      td.colSpan = 9; td.className = 'dim';
      td.textContent = err ? 'p2p status unavailable: ' + err
                           : 'no peers connected or known';
      tr.appendChild(td); body.appendChild(tr);
      return;
    }
    peers.forEach(function (p) {
      var tr = document.createElement('tr');
      function cell(text, cls) {
        var td = document.createElement('td');
        td.textContent = text == null ? '' : String(text);
        if (cls) td.className = cls;
        tr.appendChild(td);
      }
      cell(p.endpoint);
      cell(p.lifecycle, p.lifecycle === 'ACTIVE' ? 'ok' :
           (p.lifecycle === 'BANNED' ? 'bad' : 'warn'));
      cell(p.incoming ? 'in' : 'out');
      cell(p.peer_head || '-');
      cell(lagText(p.head_lag), p.head_lag < 0 ? 'warn' : '');
      cell(p.node_status + '/' + p.fork_status);
      var flags = [];
      if (p.trusted) flags.push('trusted');
      if (p.exchange_enabled) flags.push('exchange');
      if (p.fork_alignment) flags.push('aligned');
      cell(flags.join(',') || '-');
      cell(p.connected_sec ? uptime(p.connected_sec) : '-');
      cell(p.ban_reason || (p.next_reconnect_in_sec ? 'retry in ' + p.next_reconnect_in_sec + 's' : ''),
           p.ban_reason ? 'bad' : 'dim');
      body.appendChild(tr);
    });
  }

  function renderStatus(info, p2p, p2pErr) {
    var cards = $('cards');
    cards.textContent = '';
    if (info) {
      var em = !!info.emergency_consensus_active;
      cards.appendChild(card('Emergency consensus', em ? 'ACTIVE' : 'normal',
        em ? 'since block #' + info.emergency_consensus_start_block
           : 'validators sign normally', em ? 'bad' : 'ok'));
      cards.appendChild(card('Emergency key',
        info.emergency_key_configured ? 'configured' : 'not configured',
        'validator plugin emergency key',
        info.emergency_key_configured ? 'ok' : (em ? 'warn' : '')));
      cards.appendChild(card('Head / LIB', '#' + info.head_block_num,
        'lib #' + info.last_irreversible_block_num));
      cards.appendChild(card('Head age', ageText(info.head_age_sec),
        'block time vs server clock', Number(info.head_age_sec) > 30 ? 'warn' : ''));
      cards.appendChild(card('Version', info.version, 'hardforks: ' + info.hardfork_count));
      cards.appendChild(card('Uptime', uptime(info.uptime_sec), 'since process start'));
      if (info.recovering) {
        cards.appendChild(card('Recovering', 'YES',
          'chain is replaying or rebuilding state', 'warn'));
      }
    }
    if (p2p) {
      cards.appendChild(card('Node status', p2p.node_status, 'fork: ' + p2p.fork_status,
        p2p.node_status === 'FORWARD' ? 'ok' : 'warn'));
      cards.appendChild(card('Peers', p2p.active + ' active / ' + p2p.connections,
        p2p.known_peers + ' known, ' + p2p.trusted_peers + ' trusted'));
      cards.appendChild(card('Best peer head', p2p.best_peer_head ? '#' + p2p.best_peer_head : '-',
        p2p.best_peer_head ? 'lag ' + lagText(p2p.best_peer_head_lag) +
        ' (' + p2p.peers_ahead + ' ahead)' : 'no peer heads yet',
        p2p.best_peer_head_lag < -2 ? 'warn' : ''));
      cards.appendChild(card('Banned', p2p.banned_peers, 'soft-banned entries',
        p2p.banned_peers ? 'warn' : ''));
      var notes = [];
      if (p2p.paused) notes.push('BLOCK PROCESSING PAUSED');
      if (p2p.catching_up) notes.push('catching up');
      if (p2p.isolated_peers) notes.push('isolated-peers mode');
      if (p2p.fork_status !== 'NORMAL') notes.push('fork: ' + p2p.fork_status);
      cards.appendChild(card('Flags', notes.length ? notes.join(' · ') : 'ok',
        'as of ' + p2p.as_of, notes.length ? 'warn' : 'ok'));
    }
    if (!info) {
      cards.appendChild(card('Node', 'unavailable', p2pErr || 'node_info failed', 'bad'));
    }
  }

  function refreshStatus() {
    if (!token) return Promise.resolve();
    return Promise.all([
      rpc('admin.node_info').then(function (r) { return { ok: true, value: r }; },
                                  function (e) { return { ok: false, error: e }; }),
      rpc('admin.p2p.status').then(function (r) { return { ok: true, value: r }; },
                                   function (e) { return { ok: false, error: e }; })
    ]).then(function (res) {
      var info = res[0].ok ? res[0].value : null;
      var p2p = res[1].ok ? res[1].value : null;
      var perr = res[1].ok ? null :
        (res[1].error.rpcCode === -32004 ? 'p2p plugin is not enabled' : res[1].error.message);
      renderStatus(info, p2p, perr);
      if (p2p) {
        renderPeers(p2p.peers, null);
      } else {
        renderPeers(null, perr);
      }
      var e = !res[0].ok ? res[0].error : (!res[1].ok ? res[1].error : null);
      if (e && (e.rpcCode === -32001 || e.httpStatus === 401)) {
        setState('token rejected (401)', 'bad');
      } else if (info || p2p) {
        setState('updated ' + new Date().toLocaleTimeString(), 'ok');
      } else {
        setState('error: ' + (e ? e.message : 'no data'), 'bad');
      }
    });
  }

  // ── snapshots ──────────────────────────────────────────────────────────────────────────
  function downloadSnapshot(name, btn) {
    if (!token) { notice('enter a token first', 'bad'); return; }
    if (downloading[name]) return;   // a second click while the first is in flight is a no-op
    downloading[name] = true;
    btn.disabled = true;
    btn.className = 'loading';
    var old = btn.textContent;
    btn.textContent = 'Downloading…';
    notice('requesting ' + name + ' …');
    fetch(base + 'snapshot/' + encodeURIComponent(name), {
      headers: { 'Authorization': 'Bearer ' + token }
    }).then(function (res) {
      if (!res.ok) {
        return res.text().then(function (t) {
          throw new Error('HTTP ' + res.status + (t ? ': ' + t : ''));
        });
      }
      return res.blob();
    }).then(function (blob) {
      var a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = name;
      document.body.appendChild(a);
      a.click();
      setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 2000);
      notice('downloaded ' + name + ' (' + sizeText(blob.size) + ')', 'ok');
    }).catch(function (e) {
      notice('download failed: ' + e.message, 'bad');
    }).then(function () {
      downloading[name] = false;
      btn.disabled = false;
      btn.className = '';
      btn.textContent = old;
    });
  }

  function restoreSnapshot(name, blockNum) {
    var what = blockNum ? 'block #' + blockNum : name;
    if (!confirm('Restore ' + what + ' from ' + name + '?\n\n' +
                 'Shared memory is wiped and the snapshot is re-imported in-process. ' +
                 'The node pauses block processing while it happens; HTTP stays up.')) return;
    act('admin.snapshot.restore', { name: name }, 'restore started for ' + name);
  }

  function renderSnapshotRows(st) {
    var body = $('snapshots');
    body.textContent = '';
    if (!snapItems.length) {
      var tr = document.createElement('tr');
      var td = document.createElement('td');
      td.colSpan = 5; td.className = 'dim';
      td.textContent = 'no snapshot files in ' + (st.dir || 'the snapshot directory');
      tr.appendChild(td); body.appendChild(tr);
    }
    snapItems.forEach(function (p) {
      var tr = document.createElement('tr');
      function cell(text, cls) {
        var td = document.createElement('td');
        td.textContent = text == null ? '' : String(text);
        if (cls) td.className = cls;
        tr.appendChild(td);
        return td;
      }
      cell(p.block_num ? '#' + p.block_num : '-');
      cell(p.name + (p.latest ? '  (latest)' : ''), p.latest ? 'ok' : '');
      cell(sizeText(p.size_bytes));
      cell(ageText(st.as_of - p.modified));
      var actions = cell('');
      var dl = button('Download', '', function () { downloadSnapshot(p.name, dl); });
      if (downloading[p.name]) {
        dl.disabled = true; dl.className = 'loading'; dl.textContent = 'Downloading…';
      }
      actions.appendChild(dl);
      if (hasScope('snapshot')) {
        actions.appendChild(button('Restore', 'danger', function () {
          restoreSnapshot(p.name, p.block_num);
        }));
      }
      body.appendChild(tr);
    });
    $('snapMore').hidden = !snapHasMore;
    $('snapCount').textContent = snapItems.length +
      (st.snapshot_total != null ? ' of ' + st.snapshot_total : '') + ' files shown';
  }

  function renderSnapCards(st) {
    var cards = $('snapCards');
    cards.textContent = '';
    var state, sub, cls;
    if (st.restoring) { state = 'restoring'; sub = 'shared memory is being re-imported'; cls = 'warn'; }
    else if (st.in_progress) { state = 'creating'; sub = 'a snapshot is being written now'; cls = 'warn'; }
    else { state = 'idle'; sub = 'no snapshot job is running'; cls = 'ok'; }
    cards.appendChild(card('Snapshot state', state,
      sub + (st.head_block_num == null ? '' : ' · head #' + st.head_block_num), cls));
    cards.appendChild(card('Snapshots', st.snapshot_count, st.dir));
    cards.appendChild(card('Latest', st.latest_name || '-',
      st.latest_name ? 'used by Restore latest and /snapshot/latest' : 'none yet',
      st.latest_name ? '' : 'warn'));
  }

  function refreshSnapshots(reset) {
    if (!token || snapLoading) return Promise.resolve();
    snapLoading = true;
    var offset = reset ? 0 : snapItems.length;
    // Anchor the next page to the last row actually shown, not to its index: snapshots are
    // written and pruned while the operator pages, which shifts every offset below the change.
    var after = (!reset && snapItems.length) ? snapItems[snapItems.length - 1].name : '';
    return rpc('admin.snapshot.status', { offset: offset, limit: 20, after: after }).then(function (st) {
      if (after && st.after_found === false) {
        // The anchor was pruned between pages; the stable answer is to start over from page 1.
        snapLoading = false;
        notice('snapshot list changed under the pager; reloaded from the newest', 'warn');
        return refreshSnapshots(true);
      }
      if (reset) snapItems = [];
      var seen = Object.create(null);
      for (var k = 0; k < snapItems.length; k++) seen[snapItems[k].name] = true;
      var add = st.snapshots || [];
      for (var i = 0; i < add.length; i++) {
        if (!seen[add[i].name]) { snapItems.push(add[i]); seen[add[i].name] = true; }
      }
      snapHasMore = !!st.has_more;
      renderSnapCards(st);
      renderSnapshotRows(st);
      $('snapCreate').disabled = !hasScope('snapshot') || st.in_progress || st.restoring;
      $('snapRestoreLatest').disabled = !hasScope('snapshot') || st.restoring;
    }).catch(function (e) {
      var msg = e.rpcCode === -32004 ? 'snapshot plugin is not enabled on this node' : e.message;
      var cards = $('snapCards');
      cards.textContent = '';
      cards.appendChild(card('Snapshots', 'unavailable', msg, 'warn'));
      $('snapshots').textContent = '';
      notice('snapshots: ' + msg, 'bad');
    }).then(function () { snapLoading = false; });
  }

  // ── logs ───────────────────────────────────────────────────────────────────────────────
  var ANSI = /\x1b\[([0-9;]*)m/;

  function ansiFragments(text) {
    var parts = text.split(ANSI);
    var frag = document.createDocumentFragment();
    var cls = '';
    for (var i = 0; i < parts.length; i++) {
      if (i % 2 === 0) {
        if (!parts[i]) continue;
        var span = document.createElement('span');
        span.textContent = parts[i];
        if (cls) span.className = cls;
        frag.appendChild(span);
      } else {
        var c = parts[i];
        if (c === '' || c === '0') cls = '';
        else if (/^(1;)?31$|^91$/.test(c)) cls = 'bad';
        else if (/^(1;)?33$|^93$/.test(c)) cls = 'warn';
        else if (/^(1;)?32$|^92$/.test(c)) cls = 'ok';
        else if (/^(1;)?3[46]$|^9[46]$/.test(c)) cls = 'dim';
        else if (/^1$/.test(c)) cls = 'strong';
      }
    }
    return frag;
  }

  function levelClass(t) {
    // The file appender writes plain text without a level column and without ANSI, so the
    // colour has to come from the message itself. Console stdout does carry ANSI and is
    // parsed separately above.
    if (/\b(FATAL|CRITICAL|ERROR)\b|exception|failed|rejected/.test(t)) return 'bad';
    if (/\b(WARN|WARNING)\b|banned|reset|reconnect|behind schedule|lagging/.test(t)) return 'warn';
    if (/Validated block|Produced block|started|connected/.test(t)) return 'ok';
    if (/\b(INFO|NOTICE)\b/.test(t)) return 'dim';
    return '';
  }

  function addLogLine(text) {
    var view = $('logView');
    var el = document.createElement('div');
    var cls = levelClass(text);
    if (cls) el.className = cls;
    if (text.indexOf('\x1b[') >= 0) el.appendChild(ansiFragments(text));
    else el.textContent = text;
    view.appendChild(el);
  }

  function appendLog(lines, reset) {
    var view = $('logView');
    if (reset) { logLines = []; view.textContent = ''; }
    if (!lines || !lines.length) {
      if (!logLines.length) {
        view.textContent = '';
        var d = document.createElement('div');
        d.className = 'dim';
        d.textContent = 'the log file is empty so far';
        view.appendChild(d);
      }
      return;
    }
    var atBottom = view.scrollTop + view.clientHeight >= view.scrollHeight - 24;
    var start = 0;
    // The "file is empty" placeholder is not part of the line buffer; drop it before the
    // first real line arrives, or it would sit above the log forever.
    if (!reset && !logLines.length) view.textContent = '';
    if (logLines.length) {
      var idx = lines.lastIndexOf(logLines[logLines.length - 1]);
      if (idx >= 0) {
        start = idx + 1;
      } else {
        var maxK = Math.min(logLines.length, lines.length);
        for (var k = maxK; k > 0; k--) {
          var same = true;
          for (var i = 0; i < k; i++) {
            if (logLines[logLines.length - k + i] !== lines[i]) { same = false; break; }
          }
          if (same) { start = k; break; }
        }
      }
    }
    for (var j = start; j < lines.length; j++) {
      addLogLine(lines[j]);
      logLines.push(lines[j]);
    }
    while (logLines.length > 2000 && view.firstChild) {
      view.removeChild(view.firstChild);
      logLines.shift();
    }
    if (atBottom) view.scrollTop = view.scrollHeight;
  }

  function tailOnce(reset) {
    if (!token || !logName) return Promise.resolve();
    if (logBusy) { logPendingReset = logPendingReset || !!reset; return Promise.resolve(); }
    logBusy = true;
    var name = logName;
    return rpc('admin.logs.tail', { name: name, lines: 300, bytes: 131072 })
      .then(function (r) {
        if (name !== logName) { logPendingReset = true; return; }
        appendLog(r.lines || [], !!reset);
        $('logState').textContent = logPlaying ? 'playing' : 'paused';
      }).catch(function (e) {
        notice('log: ' + e.message, 'bad');
        $('logState').textContent = 'error';
      }).then(function () {
        logBusy = false;
        if (logPendingReset) { logPendingReset = false; tailOnce(true); }
      });
  }

  function setPlaying(on) {
    logPlaying = on;
    $('logToggle').textContent = on ? 'Pause' : 'Play';
    $('logState').textContent = on ? 'playing' : 'paused';
    if (logTimer) { clearInterval(logTimer); logTimer = null; }
    if (on) {
      tailOnce(false);
      logTimer = setInterval(function () {
        if (!document.hidden) tailOnce(false);
      }, 2000);
    }
  }

  function loadLogList() {
    if (!token) return Promise.resolve();
    return rpc('admin.logs.list').then(function (r) {
      var logs = r.logs || [];
      var sel = $('logName');
      var prev = logName || sel.value;
      sel.textContent = '';
      if (!logs.length) {
        var o = document.createElement('option');
        o.value = ''; o.textContent = '(no admin-log-file entries configured)';
        sel.appendChild(o);
        logName = '';
        $('logToggle').disabled = true;
        return;
      }
      logs.forEach(function (l) {
        var o = document.createElement('option');
        o.value = l.name;
        o.textContent = l.name + (l.exists ? ' · ' + sizeText(l.size) : ' · missing');
        sel.appendChild(o);
      });
      if (prev && logs.some(function (l) { return l.name === prev; })) sel.value = prev;
      logName = sel.value;
      $('logToggle').disabled = !logName;
      if (!logLines.length) tailOnce(true);
    }).catch(function (e) { notice('logs: ' + e.message, 'bad'); });
  }

  // ── validators ─────────────────────────────────────────────────────────────────────────
  function renderGuard(r) {
    var box = $('guardBox');
    box.textContent = '';
    box.className = '';
    var g = (r && r.guard) || {};
    var line = document.createElement('div');
    line.className = g.available ? 'ok' : 'dim';
    line.textContent = g.available ? 'validator_guard is live'
      : (g.reason || 'validator_guard is not loaded');
    box.appendChild(line);
    var list = (r && r.validators) || [];
    if (!list.length) return;
    var wrap = document.createElement('div');
    wrap.className = 'tablewrap';
    wrap.style.marginTop = '8px';
    var table = document.createElement('table');
    var thead = document.createElement('thead');
    var htr = document.createElement('tr');
    ['Validator', 'On chain', 'Enabled', 'Hold', 'Active authority', 'Scheduled in',
     'Signing key', 'Configured key'].forEach(function (h) {
      var th = document.createElement('th'); th.textContent = h; htr.appendChild(th);
    });
    thead.appendChild(htr); table.appendChild(thead);
    var tbody = document.createElement('tbody');
    list.forEach(function (v) {
      var tr = document.createElement('tr');
      function cell(text, cls) {
        var td = document.createElement('td');
        td.textContent = text == null || text === '' ? '-' : String(text);
        if (cls) td.className = cls;
        tr.appendChild(td);
      }
      cell(v.name);
      cell(v.on_chain ? 'yes' : 'no', v.on_chain ? 'ok' : 'warn');
      cell(v.enabled ? 'yes' : 'no', v.enabled ? 'ok' : 'warn');
      cell(v.hold ? 'yes' : 'no', v.hold ? 'warn' : '');
      cell(v.active_authority ? 'yes' : 'no', v.active_authority ? 'ok' : 'warn');
      cell(v.scheduled_in == null ? '-' : v.scheduled_in);
      cell(v.signing_key || '-');
      cell(v.configured_key || '-');
      tbody.appendChild(tr);
    });
    table.appendChild(tbody);
    wrap.appendChild(table);
    box.appendChild(wrap);
  }

  function refreshValidators() {
    if (!token) return Promise.resolve();
    return Promise.all([
      rpc('admin.validators.active').then(function (r) { return { ok: true, value: r }; },
                                          function (e) { return { ok: false, error: e }; }),
      rpc('admin.validator.list').then(function (r) { return { ok: true, value: r }; },
                                       function (e) { return { ok: false, error: e }; })
    ]).then(function (res) {
      var cards = $('valCards');
      cards.textContent = '';
      var body = $('valNames');
      body.textContent = '';
      if (res[0].ok) {
        var v = res[0].value;
        cards.appendChild(card('Scheduled validators', v.count,
          'num_scheduled=' + v.num_scheduled + ' · head #' + v.head_block_num));
        cards.appendChild(card('Emergency consensus',
          v.emergency_consensus_active ? 'ACTIVE' : 'normal',
          v.emergency_consensus_active ? 'since block #' + v.emergency_consensus_start_block
                                      : 'validators sign normally',
          v.emergency_consensus_active ? 'bad' : 'ok'));
        cards.appendChild(card('Validator plugin',
          v.validator_plugin_loaded ? 'started' : 'not started',
          'this process', v.validator_plugin_loaded ? 'ok' : 'dim'));
        var names = v.validators || [];
        if (!names.length) {
          var tr = document.createElement('tr');
          var td = document.createElement('td');
          td.colSpan = 2; td.className = 'dim'; td.textContent = 'the schedule is empty';
          tr.appendChild(td); body.appendChild(tr);
        }
        names.forEach(function (n, i) {
          var tr = document.createElement('tr');
          var a = document.createElement('td'); a.textContent = i + 1; a.className = 'dim';
          var b = document.createElement('td'); b.textContent = n;
          tr.appendChild(a); tr.appendChild(b); body.appendChild(tr);
        });
      } else {
        cards.appendChild(card('Validators', 'unavailable', res[0].error.message, 'bad'));
      }
      if (res[1].ok) {
        renderGuard(res[1].value);
      } else {
        $('guardBox').textContent = 'validator_guard: ' + res[1].error.message;
        $('guardBox').className = 'dim';
      }
    });
  }

  // ── actions ────────────────────────────────────────────────────────────────────────────
  function act(method, params, okText) {
    return rpc(method, params).then(function (r) {
      notice(okText + ': ' + JSON.stringify(r), 'ok');
      return refreshActiveTab();
    }).catch(function (e) {
      notice(method + ' failed: ' + e.message, 'bad');
    });
  }

  function endpoint() {
    var v = $('endpoint').value.trim();
    if (!v) { notice('enter IP:port first', 'bad'); return null; }
    return v;
  }

  // ── plugins, parameters and logging ───────────────────────────────────────────────────
  // Plugins whose loss changes what the node DOES, not just how fast it answers. They get a
  // second confirmation that names the consequence, because "disable p2p" reads like a
  // harmless checkbox until the chain stops moving.
  var DANGEROUS_PLUGINS = { p2p: 1, webserver: 1, json_rpc: 1, validator: 1,
                            validator_api: 1, validator_guard: 1 };
  var PLUGIN_IMPACT = {
    p2p: 'The node stops syncing with the network; peers go silent.',
    webserver: 'The HTTP API on the webserver plugin stops answering.',
    json_rpc: 'JSON-RPC clients lose their endpoint.',
    validator: 'The node stops signing blocks and votes.',
    validator_api: 'Validator API calls fail.',
    validator_guard: 'Runaway-validator protection turns off.'
  };

  function cellText(row, text, cls) {
    var c = document.createElement('td');
    c.textContent = text;
    if (cls) c.className = cls;
    row.appendChild(c);
    return c;
  }

  function needsRestartBanner(text) {
    $('plugBannerText').textContent = text;
    $('plugBanner').hidden = false;
  }

  function clearRestartBanner() { $('plugBanner').hidden = true; }

  function specInput(s) {
    var el;
    if (s.type === 'boolean' || s.type === 'log_level') {
      el = document.createElement('select');
      var opts = s.type === 'boolean' ? ['true', 'false'] : (s.values || []);
      for (var i = 0; i < opts.length; i++) {
        var o = document.createElement('option');
        o.value = opts[i];
        o.textContent = opts[i];
        el.appendChild(o);
      }
    } else {
      el = document.createElement('input');
      el.type = 'number';
      if (s.min != null) el.min = s.min;
      if (s.max != null) el.max = s.max;
      el.style.width = '9em';
    }
    // The key is the address config.plan takes, so the panel never reconstructs sections.
    el.setAttribute('data-key', s.key);
    el.setAttribute('data-type', s.type);
    el.value = s.value == null ? '' : String(s.value);
    el.setAttribute('data-initial', el.value);
    if (s.valid === false) el.className = 'bad';
    return el;
  }

  function renderSettings(container, groups) {
    container.textContent = '';
    if (!cfgAllowKnown) {
      container.textContent = 'this node does not expose the allow-list (older build)';
      container.className = 'dim';
      return;
    }
    var shown = 0;
    for (var gi = 0; gi < groups.length; gi++) {
      var group = groups[gi];
      var items = cfgAllow.filter(function (s) { return s.group === group; });
      if (!items.length) continue;
      shown++;
      var label = document.createElement('div');
      label.className = 'dim';
      label.style.cssText =
        'margin:14px 0 6px;text-transform:uppercase;letter-spacing:.06em;font-size:11px';
      label.textContent = group;
      container.appendChild(label);
      var wrap = document.createElement('div');
      wrap.className = 'tablewrap';
      var table = document.createElement('table');
      var body = document.createElement('tbody');
      for (var i = 0; i < items.length; i++) {
        var s = items[i];
        var tr = document.createElement('tr');
        cellText(tr, s.key);
        var vc = document.createElement('td');
        vc.appendChild(specInput(s));
        tr.appendChild(vc);
        cellText(tr, s.needs_restart ? 'restart' : 'live', 'dim');
        cellText(tr, s.defaulted ? 'code default' : (s.valid === false ? 'unreadable' : ''), 'dim');
        body.appendChild(tr);
      }
      table.appendChild(body);
      wrap.appendChild(table);
      container.appendChild(wrap);
    }
    container.className = '';
    if (!shown) {
      container.textContent = 'nothing to configure';
      container.className = 'dim';
    }
  }

  function lockInputs(container, locked) {
    var els = container.querySelectorAll('input,select,button');
    for (var i = 0; i < els.length; i++) els[i].disabled = locked;
  }

  // Documentation lives on GitHub Pages (built from master). Plugins with a page of their
  // own link to it; the rest link to their section of the overview (vitepress turns the
  // heading `account_by_key` into the anchor "account-by-key"); a plugin the overview does
  // not describe links to the overview itself rather than to an anchor that does not exist.
  var DOCS = 'https://viz-blockchain.github.io/viz-cpp-node/';
  var DOC_PAGES = {
    chain: 'plugins/chain', snapshot: 'plugins/snapshot', block_archive: 'plugins/block-archive',
    database_api: 'plugins/database-api', prediction_market_api: 'plugins/prediction-market-api',
    validator: 'plugins/validator', webserver: 'plugins/webserver', node_admin: 'plugins/node-admin',
    validator_guard: 'node/validator-guard'
  };
  var DOC_ANCHORS = ['json_rpc', 'p2p', 'network_broadcast_api', 'validator_api', 'account_by_key',
    'operation_history', 'account_history', 'committee_api', 'invite_api', 'paid_subscription_api'];

  function pluginDocUrl(name) {
    if (DOC_PAGES[name]) return DOCS + DOC_PAGES[name];
    if (DOC_ANCHORS.indexOf(name) >= 0) return DOCS + 'plugins/overview#' + name.replace(/_/g, '-');
    return DOCS + 'plugins/overview';
  }

  function renderPlugins(res) {
    var body = $('plugRows');
    body.textContent = '';
    var configured = Object.create(null);
    plugInfo = Object.create(null);
    var list = (res && res.plugins) || [];
    for (var i = 0; i < list.length; i++) {
      var p = list[i];
      configured[p.name] = true;
      var tr = document.createElement('tr');
      var nameCell = cellText(tr, p.name + ' ');
      var doc = document.createElement('a');
      doc.href = pluginDocUrl(p.name);
      doc.target = '_blank';
      doc.rel = 'noopener';
      doc.textContent = 'docs';
      doc.className = 'dim';
      nameCell.appendChild(doc);
      cellText(tr, 'yes');
      cellText(tr, p.state ? (p.state + (p.state_source === 'observed' ? '' : ' (inferred)'))
                           : 'inferred',
               p.state === 'started' ? 'ok' : 'dim');
      var n = (p.params || []).length;
      cellText(tr, n ? String(n) : 'none', n ? '' : 'dim');
      tr.setAttribute('data-plugin', p.name);
      plugInfo[p.name] = p;
      // Both buttons on every row, disabled when the plugin has nothing to show, so the
      // column lines up and the operator sees "no status" instead of a missing button.
      // They open the panel under the table on this plugin and scroll to the section.
      var cell = document.createElement('td');
      var sb = button('Status', '', (function (name) {
        return function () { openPlugin(name, 'status'); };
      })(p.name));
      if (!p.status_method) { sb.disabled = true; sb.title = 'this plugin exposes no status'; }
      cell.appendChild(sb);
      cell.appendChild(document.createTextNode(' '));
      var pb = button('Parameters', '', (function (name) {
        return function () { openPlugin(name, 'params'); };
      })(p.name));
      if (!n) { pb.disabled = true; pb.title = 'no parameters of this plugin are editable here'; }
      cell.appendChild(pb);
      cell.appendChild(document.createTextNode(' '));
      if (p.removable === false) {
        var fixed = document.createElement('button');
        fixed.textContent = 'protected';
        fixed.disabled = true;
        fixed.title = 'chain and node_admin cannot be disabled through the admin API';
        cell.appendChild(fixed);
      } else if (hasScope('plugins')) {
        cell.appendChild(button('Disable', 'danger', (function (name) {
          return function () { planPluginChange({ remove: [name] }, 'disable ' + name); };
        })(p.name)));
      }
      tr.appendChild(cell);
      body.appendChild(tr);
    }
    if (!list.length) {
      var empty = document.createElement('tr');
      cellText(empty, 'config.ini lists no plugins', 'dim');
      body.appendChild(empty);
    }
    var sel = $('plugAddName');
    sel.textContent = '';
    var known = (res && res.known) || [];
    for (var k = 0; k < known.length; k++) {
      if (configured[known[k]]) continue;
      var o = document.createElement('option');
      o.value = known[k];
      o.textContent = known[k];
      sel.appendChild(o);
    }
    var canAdd = hasScope('plugins') && sel.options.length > 0;
    $('plugAddBox').hidden = scopes === null;
    $('plugAdd').disabled = !canAdd;
    sel.disabled = !canAdd;
    $('plugRo').hidden = scopes === null || hasScope('plugins');
  }

  function refreshPlugins() {
    if (!token) {
      clearRestartBanner();
      $('plugRows').textContent = '';
      $('plugAddBox').hidden = true;
      $('plugRo').hidden = true;
      $('paramBox').textContent = 'no token — enter one above';
      $('paramBox').className = 'dim';
      $('logSettings').textContent = 'no token — enter one above';
      $('logSettings').className = 'dim';
      $('paramActions').hidden = true;
      $('logSettingsActions').hidden = true;
      $('plugDetail').hidden = true;
      $('plugStatus').textContent = '';
      return Promise.resolve();
    }
    return Promise.all([rpc('admin.plugins.list'), rpc('admin.config.get')])
      .then(function (res) {
        renderPlugins(res[0]);
        cfgAllow = res[1] && res[1].allow ? res[1].allow : [];
        cfgAllowKnown = !!(res[1] && res[1].allow);
        renderPlugTabs();
        renderSettings($('logSettings'), ['logging']);
        var canCfg = hasScope('config');
        lockInputs($('logSettings'), !canCfg);
        $('logSettingsActions').hidden = !canCfg || !cfgAllowKnown;
      })
      .catch(function (e) {
        $('plugRows').textContent = '';
        var tr = document.createElement('tr');
        cellText(tr, 'plugins unavailable: ' + e.message, 'bad');
        $('plugRows').appendChild(tr);
      });
  }

  function planPluginChange(change, label) {
    if (!hasScope('plugins')) { notice('plugin changes need the plugins scope', 'bad'); return; }
    var names = change.remove || change.add || [];
    var dangerous = names.some(function (n) { return DANGEROUS_PLUGINS[n]; });
    var verb = change.remove ? 'Disable ' : 'Enable ';
    var text = verb + names.join(', ') + '?\n\n';
    if (change.remove) {
      text += names.map(function (n) {
        return PLUGIN_IMPACT[n] || 'The plugin stops at the next restart.';
      }).join('\n');
    } else {
      text += 'The plugin starts at the next node restart.';
    }
    text += '\n\nThis writes config.ini; the change takes effect after a restart.';
    if (!confirm(text)) return;
    if (dangerous && !confirm('Second check: ' + verb.toLowerCase() + names.join(', ') +
                              ' on this node?')) return;
    rpc('admin.plugins.plan', change).then(function (plan) {
      var lines = (plan.diff || []).map(function (d) { return d.op + ' ' + d.name; }).join('\n');
      if (!confirm('Plan ' + plan.plan_id + ':\n' + lines + '\n\nApply it now?')) {
        rpc('admin.plan.cancel', { plan_id: plan.plan_id }).catch(function () {});
        notice('plan cancelled');
        return;
      }
      return rpc('admin.plugins.apply', { plan_id: plan.plan_id }).then(function (r) {
        notice(label + ' written; backup ' + (r.backup || '?'), 'ok');
        needsRestartBanner('config.ini changed (' + label + '). Restart the node to apply it.');
        refreshPlugins();
      });
    }).catch(function (e) {
      notice('plugin change failed: ' + e.message, 'bad');
    });
  }

  function collectSettings(container) {
    var set = {};
    var inputs = container.querySelectorAll('[data-key]');
    for (var i = 0; i < inputs.length; i++) {
      var el = inputs[i];
      var cur = el.value;
      if (cur === '' || cur === el.getAttribute('data-initial')) continue;
      if (el.getAttribute('data-type') === 'unsigned_int') {
        var n = Number(cur);
        if (!isFinite(n) || Math.floor(n) !== n || n < 0) {
          notice('not a whole number: ' + el.getAttribute('data-key'), 'bad');
          return null;
        }
      }
      set[el.getAttribute('data-key')] = cur;
    }
    return set;
  }

  function saveSettings(container, label, stateEl) {
    if (!hasScope('config')) { notice('config edits need the config scope', 'bad'); return; }
    var set = collectSettings(container);
    if (set === null) return;
    var keys = Object.keys(set);
    if (!keys.length) { stateEl.textContent = 'no changes'; return; }
    var shown = keys.map(function (k) { return k + ' = ' + set[k]; }).join('\n');
    if (!confirm('Write ' + keys.length + ' setting(s) to config.ini?\n\n' + shown +
                 '\n\nA backup is kept; the node reads config.ini at startup.')) return;
    stateEl.textContent = 'planning…';
    rpc('admin.config.plan', { set: set }).then(function (plan) {
      var lines = (plan.diff || []).map(function (d) {
        return d.key + ': ' + (d.from == null ? '(unset)' : d.from) + ' -> ' + d.to;
      }).join('\n');
      if (!confirm('Plan ' + plan.plan_id + ':\n' + lines + '\n\nApply it now?')) {
        rpc('admin.plan.cancel', { plan_id: plan.plan_id }).catch(function () {});
        stateEl.textContent = 'cancelled';
        return;
      }
      return rpc('admin.config.apply', { plan_id: plan.plan_id }).then(function (r) {
        stateEl.textContent = 'written';
        notice(label + ' written; backup ' + (r.backup || '?'), 'ok');
        needsRestartBanner('config.ini changed (' + label + '). Restart the node to apply it.');
        refreshPlugins();
      });
    }).catch(function (e) {
      stateEl.textContent = 'failed';
      notice(label + ' failed: ' + e.message, 'bad');
    });
  }

  // ── objects (chainbase) ────────────────────────────────────────────────────────────────
  function renderObjects(res) {
    var box = $('objSummary');
    var lines = [
      'shared memory: ' + (res.shared_memory_path || '-') + ' — ' + sizeText(res.shared_memory_bytes),
      'mapped ' + sizeText(res.mapped_bytes) + ' · free ' + sizeText(res.free_bytes) +
        ' · reserved ' + sizeText(res.reserved_bytes),
      'revision ' + res.revision + ' · head block ' + res.head_block_num +
        ' · ' + res.total_objects + ' objects in ' + res.index_count + ' indexes',
      'schema ' + (res.schema ? (res.schema.stored + ' / ' + res.schema.compiled +
        (res.schema.match ? ' (match)' : ' (MISMATCH)')) : '-') +
        ' · block log ' + (res.block_log && res.block_log.present ? sizeText(res.block_log.bytes) : 'absent') +
        (res.markers && res.markers.length ? ' · markers: ' + res.markers.join(', ') : '')
    ];
    if (res.lock_error) lines.push('LOCK ERROR: ' + res.lock_error);
    box.textContent = lines.join('\n');
    box.style.whiteSpace = 'pre-wrap';

    var body = $('objRows');
    body.textContent = '';
    var rows = res.indexes || [];
    for (var i = 0; i < rows.length; i++) {
      var tr = document.createElement('tr');
      cellText(tr, rows[i].type);
      cellText(tr, String(rows[i].type_id), 'dim');
      cellText(tr, String(rows[i].objects));
      cellText(tr, String(rows[i].revision), 'dim');
      body.appendChild(tr);
    }
    if (!rows.length) {
      var empty = document.createElement('tr');
      cellText(empty, 'no indexes reported', 'dim');
      body.appendChild(empty);
    }
  }

  function refreshObjects() {
    if (!token) {
      $('objSummary').textContent = 'no token — enter one above';
      $('objRows').textContent = '';
      $('objVerdict').textContent = 'not checked yet';
      $('objVerdict').className = 'dim';
      return Promise.resolve();
    }
    return rpc('admin.objects.list').then(renderObjects).catch(function (e) {
      $('objSummary').textContent = 'objects unavailable: ' + e.message;
    });
  }

  function showFindings(res) {
    var box = $('objFindings');
    box.textContent = '';
    box.hidden = false;
    var list = res.findings || [];
    if (!list.length) {
      var ok = document.createElement('div');
      ok.className = 'ok';
      ok.textContent = 'no findings: state, schema, markers and head/revision agree.';
      box.appendChild(ok);
      return;
    }
    for (var i = 0; i < list.length; i++) {
      var d = document.createElement('div');
      d.style.margin = '4px 0';
      d.className = list[i].severity === 'critical' ? 'bad' : 'warn';
      d.textContent = list[i].severity.toUpperCase() + ' [' + list[i].code + '] ' + list[i].text;
      box.appendChild(d);
    }
  }

  function checkObjects() {
    if (!token) { notice('enter a token first', 'bad'); return; }
    $('objVerdict').textContent = 'checking…';
    $('objVerdict').className = 'dim';
    rpc('admin.objects.check').then(function (res) {
      var cls = { ok: 'ok', warn: 'warn', corrupted: 'bad', unreadable: 'bad' }[res.verdict] || 'dim';
      $('objVerdict').textContent = res.verdict + ' — critical ' + res.critical +
        ', warnings ' + res.warnings +
        (res.recommended_repair ? ' · recommended repair: ' + res.recommended_repair : '');
      $('objVerdict').className = cls;
      if (res.recommended_repair) $('objRepairMode').value = res.recommended_repair;
      showFindings(res);
      refreshObjects();
    }).catch(function (e) {
      $('objVerdict').textContent = 'check failed: ' + e.message;
      $('objVerdict').className = 'bad';
    });
  }

  function repairObjects() {
    var mode = $('objRepairMode').value;
    if (!hasScope('process')) { notice('repair needs the process scope', 'bad'); return; }
    var text = mode === 'replay'
      ? 'Restart and rebuild state from the block log?\n\nShared memory is wiped; the block log is kept and reindexed. The node is unavailable until the replay finishes.'
      : 'Restart and wipe state AND block log?\n\nThe node comes up empty and resyncs from peers, which takes far longer than a replay.';
    if (!confirm(text)) return;
    if (!confirm('Second check: ' + (mode === 'replay' ? 'replay' : 'full resync') + ' on this node?')) return;
    $('objRepairState').textContent = 'requesting…';
    rpc('admin.objects.repair', { mode: mode, confirm: 'repair' }).then(function (r) {
      $('objRepairState').textContent = r.mode + ': ' + r.effect;
      notice('repair requested; the node is restarting', 'warn');
      setTimeout(function () { if (token) refreshAuth().then(refreshObjects); }, 5000);
    }).catch(function (e) {
      $('objRepairState').textContent = 'failed: ' + e.message;
      notice('repair failed: ' + e.message, 'bad');
    });
  }

  // ── per-plugin panel ───────────────────────────────────────────────────────────────────
  // A tab per plugin that has something to show (a status method or editable parameters);
  // plugins with neither are still listed in the table, their buttons disabled.
  function panelPlugins() {
    return Object.keys(plugInfo).filter(function (name) {
      var p = plugInfo[name];
      return p.status_method || (p.params || []).length;
    });
  }

  function settingsDirty(container) {
    var inputs = container.querySelectorAll('[data-key]');
    for (var i = 0; i < inputs.length; i++) {
      var v = inputs[i].value;
      if (v !== '' && v !== inputs[i].getAttribute('data-initial')) return true;
    }
    return false;
  }

  function renderPlugTabs() {
    var names = panelPlugins();
    var bar = $('plugTabs');
    bar.textContent = '';
    $('plugDetail').hidden = !names.length;
    if (!names.length) return;
    if (names.indexOf(plugSel) < 0) plugSel = names[0];
    names.forEach(function (name) {
      var b = button(name, '', function () { selectPlugin(name); });
      b.setAttribute('data-plugin', name);
      bar.appendChild(b);
    });
    renderPlugPanel();
  }

  // Switching plugins re-renders the parameter inputs; values typed but not saved would
  // vanish silently, so ask first.
  function selectPlugin(name) {
    if (name !== plugSel && settingsDirty($('paramBox')) &&
        !confirm('Discard unsaved parameter changes of ' + plugSel + '?')) return false;
    if (name !== plugSel) $('paramState').textContent = '';
    plugSel = name;
    sessionStorage.setItem('vizAdminPlugin', name);
    renderPlugPanel();
    return true;
  }

  function openPlugin(name, section) {
    if (!selectPlugin(name)) return;
    $(section === 'status' ? 'plugStatusBlock' : 'plugParamBlock')
      .scrollIntoView({ behavior: 'smooth', block: 'start' });
  }

  function renderPlugPanel() {
    var p = plugInfo[plugSel];
    if (!p) return;
    var tabs = $('plugTabs').querySelectorAll('button');
    for (var i = 0; i < tabs.length; i++) {
      tabs[i].className = tabs[i].getAttribute('data-plugin') === plugSel ? 'active' : '';
    }
    var rows = $('plugRows').querySelectorAll('tr[data-plugin]');
    for (var r = 0; r < rows.length; r++) {
      rows[r].className = rows[r].getAttribute('data-plugin') === plugSel ? 'sel' : '';
    }
    var head = $('plugHead');
    head.textContent = '';
    var title = document.createElement('strong');
    title.textContent = p.name;
    head.appendChild(title);
    var st = document.createElement('span');
    st.className = p.state === 'started' ? 'chip ok' : 'chip';
    st.textContent = p.state || 'unobserved';
    head.appendChild(st);
    var doc = document.createElement('a');
    doc.href = pluginDocUrl(p.name);
    doc.target = '_blank';
    doc.rel = 'noopener';
    doc.className = 'dim';
    doc.textContent = 'documentation';
    head.appendChild(doc);

    $('plugStatusRefresh').hidden = !p.status_method;
    if (p.status_method) {
      showPluginStatus(p.name);
    } else {
      $('plugStatusNote').textContent = '';
      $('plugStatus').textContent = 'this plugin exposes no status';
      $('plugStatus').className = 'statusview dim';
    }

    var hasParams = (p.params || []).length > 0;
    var canCfg = hasScope('config');
    if (hasParams) {
      renderSettings($('paramBox'), [p.name]);
      lockInputs($('paramBox'), !canCfg);
    } else {
      $('paramBox').textContent = 'no parameters of this plugin are editable here';
      $('paramBox').className = 'dim';
    }
    $('paramActions').hidden = !hasParams || !canCfg || !cfgAllowKnown;
  }

  function showPluginStatus(name) {
    var box = $('plugStatus');
    box.className = 'statusview dim';
    box.textContent = 'loading ' + name + '…';
    $('plugStatusNote').textContent = '';
    rpc('admin.plugins.status', { name: name }).then(function (r) {
      if (name !== plugSel) return;  // the operator moved to another plugin meanwhile
      box.className = 'statusview';
      $('plugStatusNote').textContent = 'loaded ' + new Date().toLocaleTimeString() +
        (r.detail_available ? ' · ' + r.detail_method : '');
      box.textContent = r.detail_available
        ? JSON.stringify(r.detail, null, 2)
        : 'state: ' + (r.state || 'unobserved') + ' (' + r.state_source + ')\n' +
          (r.reason || 'no status available');
    }).catch(function (e) {
      if (name !== plugSel) return;
      box.className = 'statusview bad';
      box.textContent = 'status of ' + name + ' failed: ' + e.message;
    });
  }

  // ── tabs and timers ────────────────────────────────────────────────────────────────────
  function refreshActiveTab() {
    if (!token) return Promise.resolve();
    if (activeTab === 'status' || activeTab === 'peers') return refreshStatus();
    if (activeTab === 'snapshots') { snapPaged = false; return refreshSnapshots(true); }
    if (activeTab === 'logs') return loadLogList();
    if (activeTab === 'plugins') return refreshPlugins();
    if (activeTab === 'objects') return refreshObjects();
    if (activeTab === 'validators') return refreshValidators();
    return Promise.resolve();
  }

  function activate(name) {
    activeTab = name;
    sessionStorage.setItem('vizAdminTab', name);
    ['status', 'peers', 'snapshots', 'logs', 'plugins', 'objects', 'validators'].forEach(function (t) {
      $('tab-' + t).hidden = t !== name;
    });
    var tabs = document.querySelectorAll('nav.tabs button');
    for (var i = 0; i < tabs.length; i++) {
      tabs[i].className = tabs[i].getAttribute('data-tab') === name ? 'active' : '';
    }
    // Play is an explicit operator decision: switching tabs does not silently stop the tail.
    refreshActiveTab();
    if (!tickTimer) {
      tickTimer = setInterval(function () {
        if (document.hidden || !token) return;
        if (activeTab === 'status' || activeTab === 'peers') refreshStatus();
        // Page 1 is refreshed on a timer; once the operator asked for older pages we leave
        // the list alone instead of appending the next page every tick.
        else if (activeTab === 'snapshots') { if (!snapPaged) refreshSnapshots(true); }
        // Plugins and Objects are deliberately not on the timer: a plugins refresh re-renders
        // the parameter inputs and would throw away values the operator is in the middle of
        // typing, and an objects refresh would repaint the index table under the Check verdict.
        // Both reload on activation, and plugins also after every apply.
        else if (activeTab === 'validators') refreshValidators();
      }, 5000);
    }
  }

  $('save').addEventListener('click', function () {
    token = $('token').value.trim();
    if (token) { sessionStorage.setItem('vizAdminToken', token); notice('token saved for this tab'); }
    else { sessionStorage.removeItem('vizAdminToken'); }
    refreshAuth().then(refreshActiveTab);
  });
  $('token').addEventListener('keydown', function (e) {
    if (e.key === 'Enter') $('save').click();
  });
  var tabButtons = document.querySelectorAll('nav.tabs button');
  for (var tbi = 0; tbi < tabButtons.length; tbi++) {
    tabButtons[tbi].addEventListener('click', function () {
      activate(this.getAttribute('data-tab'));
    });
  }
  $('add').addEventListener('click', function () {
    var ep = endpoint(); if (ep) act('admin.p2p.add_node', { endpoint: ep }, 'added ' + ep);
  });
  $('addTrusted').addEventListener('click', function () {
    var ep = endpoint(); if (ep) act('admin.p2p.add_trusted', { endpoint: ep }, 'trusted ' + ep);
  });
  $('clearBanned').addEventListener('click', function () {
    if (!confirm('Clear every soft-ban and retry those peers now?')) return;
    act('admin.p2p.clear_banned', {}, 'bans cleared');
  });
  $('snapCreate').addEventListener('click', function () {
    act('admin.snapshot.create', {}, 'snapshot creation requested');
  });
  $('snapRestoreLatest').addEventListener('click', function () {
    if (!confirm('Restore the LATEST snapshot?\n\n' +
                 'Shared memory is wiped and the snapshot is re-imported in-process. ' +
                 'The node pauses block processing while it happens; HTTP stays up.')) return;
    act('admin.snapshot.restore', { latest: true }, 'restore started for the latest snapshot');
  });
  $('snapMore').addEventListener('click', function () {
    if (snapHasMore && !snapLoading) { snapPaged = true; refreshSnapshots(false); }
  });
  $('logToggle').addEventListener('click', function () { setPlaying(!logPlaying); });
  $('logClear').addEventListener('click', function () {
    logLines = [];
    $('logView').textContent = '';
  });
  $('logName').addEventListener('change', function () {
    logName = this.value;
    setPlaying(false);
    tailOnce(true);
  });
  $('plugAdd').addEventListener('click', function () {
    var name = $('plugAddName').value;
    if (!name) { notice('no plugin selected', 'bad'); return; }
    planPluginChange({ add: [name] }, 'enable ' + name);
  });
  $('objCheck').addEventListener('click', checkObjects);
  $('objRepair').addEventListener('click', repairObjects);
  $('paramSave').addEventListener('click', function () {
    saveSettings($('paramBox'), plugSel + ' parameters', $('paramState'));
  });
  $('plugStatusRefresh').addEventListener('click', function () {
    if (plugSel) showPluginStatus(plugSel);
  });
  $('logSettingsSave').addEventListener('click', function () {
    saveSettings($('logSettings'), 'log settings', $('logSettingsState'));
  });
  $('plugRestart').addEventListener('click', function () {
    if (!hasScope('process')) { notice('restart needs the process scope', 'bad'); return; }
    if (!confirm('Restart the node now?\n\nThe process exits and a supervisor ' +
                 '(docker --restart / systemd Restart=always) starts it again. ' +
                 'Without a supervisor the node stays down.')) return;
    if (!confirm('Second check: restart this node?')) return;
    rpc('admin.restart', { confirm: 'restart' }).then(function () {
      notice('restart requested — waiting for the node to come back', 'warn');
      clearRestartBanner();
      setTimeout(function () { if (token) refreshAuth().then(refreshActiveTab); }, 5000);
    }).catch(function (e) {
      notice('restart failed: ' + e.message, 'bad');
    });
  });

  if (token) { $('token').value = token; }
  applyScopes();
  activate(activeTab);
  refreshAuth().then(function (ok) {
    if (!ok && token) setState('token rejected or unreachable', 'bad');
    // Re-render once the scopes are known: the plugins tab draws toggles from them, and the
    // fetch that activate() started ran with scopes === null.
    if (ok) refreshActiveTab();
  });
  window.addEventListener('beforeunload', function () {
    if (tickTimer) clearInterval(tickTimer);
    if (logTimer) clearInterval(logTimer);
  });
})();
</script>
</body>
</html>
)HTML";
}

// ── websocketpp server config (same stub-log shape as webserver_plugin) ─────────────────────
struct asio_with_stub_log : public websocketpp::config::asio {
    typedef asio_with_stub_log type;
    typedef asio base;

    typedef base::concurrency_type concurrency_type;
    typedef base::request_type request_type;
    typedef base::response_type response_type;
    typedef base::message_type message_type;
    typedef base::con_msg_manager_type con_msg_manager_type;
    typedef base::endpoint_msg_manager_type endpoint_msg_manager_type;
    typedef base::alog_type alog_type;
    typedef base::elog_type elog_type;
    typedef base::rng_type rng_type;

    struct transport_config : public base::transport_config {
        typedef type::concurrency_type concurrency_type;
        typedef type::alog_type alog_type;
        typedef type::elog_type elog_type;
        typedef type::request_type request_type;
        typedef type::response_type response_type;
        typedef websocketpp::transport::asio::basic_socket::endpoint socket_type;
    };

    typedef websocketpp::transport::asio::endpoint<transport_config> transport_type;

    static const long timeout_open_handshake = 0;
};

typedef websocketpp::server<asio_with_stub_log> websocket_server_type;

// ── JSON-RPC error ─────────────────────────────────────────────────────────────────────────
struct rpc_error {
    int code;
    std::string message;
    std::string reason;
    std::string hint;
};

} // namespace

// ── impl ───────────────────────────────────────────────────────────────────────────────────
struct node_admin_plugin::impl {
    explicit impl(chain::plugin &c) : chain(c) {}

    chain::plugin &chain;

    // listener
    asio::io_context ios;
    websocket_server_type server;
    std::thread thread;
    std::unique_ptr<tcp::endpoint> endpoint;
    bool listening = false;
    fc::time_point started_at = fc::time_point::now();

    // auth / policy
    std::vector<token_info> tokens;
    std::string audit_path;
    std::map<std::string, std::string> log_files; // name -> path
    bool allow_remote = false;

    // rate limit: label -> (window start sec, count in window)
    std::mutex rate_mutex;
    std::map<std::string, std::pair<uint32_t, uint32_t>> rate;

    std::atomic<uint64_t> request_seq{0};
    std::mutex audit_mutex;

    // plan/apply for config.ini (design §0.3). A plan is a single-use, token-bound snapshot of
    // "what will be written": the file's hash at plan time, the exact key/value set or plugin
    // edit, and an expiry. apply re-checks the hash, so a hand edit between plan and apply is a
    // refusal, not an overwrite.
    struct pending_plan {
        std::string id;
        std::string label;
        std::string kind; // "config" | "plugins"
        uint32_t created = 0;
        uint32_t expires = 0;
        std::string base_sha256;
        std::map<std::string, std::string> set; // config
        std::vector<std::string> add;           // plugins
        std::vector<std::string> remove;        // plugins
        // validators: [{name, action, signing_key, expected_key, hold_from}]
        fc::variants actions;
        fc::variant diff;
    };
    std::mutex plans_mutex;
    std::map<std::string, pending_plan> plans;
    // Serializes the read-hash-write of an apply: without it two concurrent applies each
    // pass their own hash check and the later rename silently drops the other's edit.
    std::mutex config_write_mutex;
    uint32_t plan_ttl_sec = 120;
    std::atomic<bool> restart_requested{false};
    // Startup mode of the chain plugin, captured while node_admin still sees the option
    // map. It decides what a repair marker really does: with --replay-from-snapshot the
    // chain plugin takes the snapshot path before it ever looks at force_replay, so the
    // marker's "replay from block 1" is superseded, and a force_resync comes back from the
    // snapshot rather than from genesis. Reporting the generic effect there would be a lie.
    bool snapshot_replay_from = false;
    bool snapshot_configured = false;

    void store_plan(const pending_plan &p);
    pending_plan take_plan(const std::string &id, const token_info &token, const std::string &kind);

    // ── listener ───────────────────────────────────────────────────────────────────────────
    void start();
    void stop();
    void handle_http(connection_hdl hdl);

    // ── auth ───────────────────────────────────────────────────────────────────────────────
    const token_info *authenticate(const std::string &header, std::string &why) const;
    bool rate_ok(const std::string &label);

    // ── dispatch ───────────────────────────────────────────────────────────────────────────
    fc::variant dispatch(const std::string &method, const fc::variant &params,
                         const token_info &token);
    fc::variant rpc_ok(const fc::variant &id, const fc::variant &result) const;
    fc::variant rpc_err(const fc::variant &id, const rpc_error &e) const;

    // ── methods ────────────────────────────────────────────────────────────────────────────
    fc::mutable_variant_object make_node_info() const;
    fc::variant h_auth_me(const token_info &token);
    fc::variant h_ping(const fc::variant &params);
    fc::variant h_node_info(const fc::variant &params);
    fc::variant h_diagnostics(const fc::variant &params);
    fc::variant h_plugins_list(const fc::variant &params);
    fc::variant h_plugins_status(const fc::variant &params);
    fc::variant h_config_get(const fc::variant &params);
    fc::variant h_objects_list(const fc::variant &params);
    fc::variant h_objects_check(const fc::variant &params);
    fc::variant h_objects_repair(const fc::variant &params);
    objects_state collect_objects() const;
    fc::variant h_logs_list(const fc::variant &params);
    fc::variant h_logs_tail(const fc::variant &params);
    fc::variant h_audit_tail(const fc::variant &params);
    fc::variant h_validators_active(const fc::variant &params);

    // ── P2P operator methods ──────────────────────────────────────────────────────────────
    graphene::plugins::p2p::p2p_plugin &p2p_started() const;
    fc::variant h_p2p_status(const fc::variant &params);
    fc::variant h_p2p_add_node(const fc::variant &params, bool trusted);
    fc::variant h_p2p_clear_banned(const fc::variant &params);

    // ── snapshot operator methods ─────────────────────────────────────────────────────────
    graphene::plugins::snapshot::snapshot_plugin &snapshot_started() const;
    fc::variant snapshot_list_variant() const;
    fc::variant h_snapshot_status(const fc::variant &params);
    fc::variant h_snapshot_create(const fc::variant &params);
    fc::variant h_snapshot_restore(const fc::variant &params);

    // ── webserver operator methods ────────────────────────────────────────────────────────
    graphene::plugins::webserver::webserver_plugin &webserver_started() const;
    fc::variant h_webserver_status(const fc::variant &params);
    fc::variant h_webserver_ips(const fc::variant &params);
    fc::variant h_webserver_limits(const fc::variant &params);
    fc::variant h_webserver_stats(const fc::variant &params);
    fc::variant h_webserver_list_add(const fc::variant &params, bool whitelist);
    fc::variant h_webserver_list_remove(const fc::variant &params, bool whitelist);
    fc::variant h_webserver_reset(const fc::variant &params);
    fc::variant h_webserver_reload(const fc::variant &params);

    // ── validator chain operations (delegate signed through validator_guard) ──────────────
    // The plugin keeps both keys and never exposes them; node_admin only drives it and
    // reports the outcome. The `chain` scope additionally requires a live guard.
    graphene::plugins::validator_guard::validator_guard_plugin *guard_plugin() const;
    bool guard_ready() const;
    fc::variant h_validator_list(const fc::variant &params);
    fc::variant h_validator_plan(const fc::variant &params, const token_info &token);
    fc::variant h_validator_apply(const fc::variant &params, const token_info &token);

    // ── config/plugin editor and restart (plan/apply) ─────────────────────────────────────
    fc::variant h_config_plan(const fc::variant &params, const token_info &token);
    fc::variant h_config_apply(const fc::variant &params, const token_info &token);
    fc::variant h_config_backups(const fc::variant &params);
    fc::variant h_plugins_plan(const fc::variant &params, const token_info &token);
    fc::variant h_plugins_apply(const fc::variant &params, const token_info &token);
    fc::variant h_plan_cancel(const fc::variant &params, const token_info &token);
    fc::variant h_restart(const fc::variant &params);

    // ── MCP (Streamable HTTP) ──────────────────────────────────────────────────────────────
    fc::variant mcp_tools() const;
    fc::variant h_mcp_initialize(const fc::variant &params);
    fc::variant h_mcp_ping(const fc::variant &params);
    fc::variant h_mcp_tools_list(const fc::variant &params);
    fc::variant h_mcp_tools_call(const fc::variant &params, const token_info &token);

    // ── audit ──────────────────────────────────────────────────────────────────────────────
    void audit(const std::string &rid, const std::string &label, const std::string &method,
               const fc::variant &params, const std::string &outcome, int code,
               int64_t duration_ms, const std::string &remote);
    void audit_line(const std::string &line);

    std::string data_dir() const { return appbase::app().data_dir().string(); }
    std::string config_path() const { return appbase::app().config_path().string(); }

    void respond(websocket_server_type::connection_ptr con,
                 websocketpp::http::status_code::value status, const std::string &body,
                 const std::string &content_type = "application/json",
                 int64_t content_length_override = -1) {
        try {
            con->append_header("Content-Type", content_type);
            con->append_header("Cache-Control", "no-store");
            con->set_body(body);
            // set_body rewrites Content-Length (0 for a HEAD reply), so an explicit
            // length must be applied after it, not before.
            if (content_length_override >= 0) {
                con->replace_header("Content-Length",
                                    std::to_string(content_length_override));
            }
            con->set_status(status);
            con->send_http_response();
        } catch (const std::exception &e) {
            wlog("node_admin: failed to send response: ${e}", ("e", e.what()));
        } catch (...) {
        }
    }
};

// ── option registration ────────────────────────────────────────────────────────────────────
void node_admin_plugin::set_program_options(
    bpo::options_description &command_line_options,
    bpo::options_description &config_file_options
) {
    // Config-file description only, like testnet_plugin: appbase also accepts these on the
    // command line, and adding the same long name to both descriptions makes boost refuse
    // every start with "option is ambiguous".
    bpo::options_description opts("Node administration API");
    opts.add_options()
        ("admin-endpoint", bpo::value<std::string>(),
         "HTTP endpoint of the admin API, e.g. 127.0.0.1:8099. Empty (default) keeps the "
         "plugin inert.")
        ("admin-token", bpo::value<std::vector<std::string>>()->composing()->multitoken(),
         "Access token: label:scope[,scope]:sha256hex[:expires-unixtime]. Repeatable.")
        ("admin-allow-remote", bpo::value<bool>()->default_value(false),
         "Allow admin-endpoint on a non-loopback address. Off by default; a remote admin "
         "port is a security decision, not a convenience.")
        ("admin-plan-ttl", bpo::value<uint32_t>()->default_value(120),
         "Seconds a config/plugin plan stays applicable. Plans are single-use and bound to the "
         "token that created them. Default: 120.")
        ("admin-audit-log", bpo::value<std::string>()->default_value(""),
         "Append-only audit log path. Default: node-admin-audit.log next to config.ini.")
        ("admin-log-file", bpo::value<std::vector<std::string>>()->composing()->multitoken(),
         "File the admin API may tail, as name=path or path. Repeatable. Nothing outside "
         "this allow-list can be read through admin.logs.tail.");
    config_file_options.add(opts);
    (void) command_line_options;
}

node_admin_plugin::node_admin_plugin() {}
node_admin_plugin::~node_admin_plugin() = default;

// ── lifecycle ─────────────────────────────────────────────────────────────────────────────
void node_admin_plugin::plugin_initialize(const bpo::variables_map &options) {
    pimpl.reset(new impl(appbase::app().get_plugin<chain::plugin>()));

    if (!options.count("admin-endpoint")) {
        ilog("node_admin: admin-endpoint is not set; the admin API is disabled");
        return;
    }

    const std::string ep = options.at("admin-endpoint").as<std::string>();
    if (trim(ep).empty()) {
        ilog("node_admin: admin-endpoint is empty; the admin API is disabled");
        return;
    }

    pimpl->allow_remote = options.at("admin-allow-remote").as<bool>();
    // Same option map the chain plugin reads. Guarded with count(): node_admin must not make
    // a snapshot option mandatory for nodes that start without one.
    pimpl->snapshot_replay_from =
            options.count("replay-from-snapshot") && options.at("replay-from-snapshot").as<bool>();
    pimpl->snapshot_configured =
            (options.count("snapshot-auto-latest") && options.at("snapshot-auto-latest").as<bool>()) ||
            (options.count("snapshot") && !trim(options.at("snapshot").as<std::string>()).empty());
    // A zero TTL would make every plan unusable before it is returned; clamp like the
    // webserver limits do.
    pimpl->plan_ttl_sec = std::max<uint32_t>(1, options.at("admin-plan-ttl").as<uint32_t>());

    // Tokens are mandatory: an admin port without authentication is never a valid state.
    FC_ASSERT(options.count("admin-token"), "node_admin: admin-endpoint is set but no admin-token is configured");
    for (const std::string &spec : options.at("admin-token").as<std::vector<std::string>>()) {
        pimpl->tokens.push_back(parse_token_spec(spec));
    }
    FC_ASSERT(!pimpl->tokens.empty(), "node_admin: admin-endpoint is set but no admin-token is configured");

    const std::string audit = trim(options.at("admin-audit-log").as<std::string>());
    if (audit.empty()) {
        bfs::path p = appbase::app().config_path();
        p = p.parent_path() / "node-admin-audit.log";
        pimpl->audit_path = p.string();
    } else {
        pimpl->audit_path = audit;
    }

    if (options.count("admin-log-file")) {
        for (const std::string &spec : options.at("admin-log-file").as<std::vector<std::string>>()) {
            const std::size_t eq = spec.find('=');
            std::string name;
            std::string path;
            if (eq == std::string::npos) {
                path = trim(spec);
                name = bfs::path(path).filename().string();
            } else {
                name = trim(spec.substr(0, eq));
                path = trim(spec.substr(eq + 1));
            }
            FC_ASSERT(!name.empty() && !path.empty(),
                      "admin-log-file: expected 'name=path' or 'path', got '${s}'", ("s", spec));
            pimpl->log_files[name] = path;
        }
    }

    // Resolve and validate the endpoint. The listener is loopback-only unless the operator
    // says otherwise: a bearer token on an open port is a different threat model.
    std::vector<tcp::endpoint> eps = appbase::app().resolve_string_to_ip_endpoints(ep);
    FC_ASSERT(!eps.empty(), "admin-endpoint '${e}' did not resolve", ("e", ep));
    const tcp::endpoint chosen = eps.front();
    if (!chosen.address().is_loopback() && !pimpl->allow_remote) {
        FC_ASSERT(false,
                  "admin-endpoint '${e}' is not a loopback address; set admin-allow-remote = true "
                  "if this is deliberate", ("e", ep));
    }
    pimpl->endpoint.reset(new tcp::endpoint(chosen));

    ilog("node_admin: configured on ${host}:${port}, tokens=${n}, audit=${audit}",
         ("host", chosen.address().to_string())("port", chosen.port())
         ("n", pimpl->tokens.size())("audit", pimpl->audit_path));
    if (!chosen.address().is_loopback()) {
        wlog("node_admin: listening on a NON-loopback address ${host} — the admin API is exposed to the network",
             ("host", chosen.address().to_string()));
    }
}

void node_admin_plugin::plugin_startup() {
    if (!pimpl || !pimpl->endpoint) return;
    pimpl->start();
}

void node_admin_plugin::plugin_shutdown() {
    if (pimpl) pimpl->stop();
}

void node_admin_plugin::impl::start() {
    server.clear_access_channels(websocketpp::log::alevel::all);
    server.clear_error_channels(websocketpp::log::elevel::all);
    server.init_asio(&ios);
    server.set_reuse_addr(true);
    server.set_max_http_body_size(kMaxBodyBytes);

    server.set_http_handler([this](connection_hdl hdl) { this->handle_http(hdl); });

    server.listen(*endpoint);
    server.start_accept();
    listening = true;
    started_at = fc::time_point::now();

    thread = std::thread([this]() {
        try {
            ios.run();
        } catch (const std::exception &e) {
            elog("node_admin: listener stopped with an error: ${e}", ("e", e.what()));
        } catch (...) {
            elog("node_admin: listener stopped with an unknown error");
        }
    });
    ilog("node_admin: listening on ${host}:${port}",
         ("host", endpoint->address().to_string())("port", endpoint->port()));
}

void node_admin_plugin::impl::stop() {
    if (!listening) return;
    listening = false;
    try {
        server.stop_listening();
    } catch (...) {
    }
    ios.stop();
    if (thread.joinable()) thread.join();
    ilog("node_admin: listener stopped");
}

// ── auth / rate limit ─────────────────────────────────────────────────────────────────────
const token_info *node_admin_plugin::impl::authenticate(const std::string &header,
                                                        std::string &why) const {
    static const std::string prefix = "bearer ";
    const std::string h = trim(header);
    if (h.size() <= prefix.size() || to_lower(h.substr(0, prefix.size())) != prefix) {
        why = "missing or malformed Authorization header (expected 'Bearer <token>')";
        return nullptr;
    }
    const std::string token = trim(h.substr(prefix.size()));
    if (token.empty()) {
        why = "empty bearer token";
        return nullptr;
    }
    const std::string hash = fc::sha256::hash(token).str();
    const uint64_t now = static_cast<uint64_t>(fc::time_point::now().sec_since_epoch());
    for (const token_info &t : tokens) {
        if (!ct_equal(hash, t.hash_hex)) continue;
        if (t.expires != 0 && now > t.expires) {
            why = "token expired";
            return nullptr;
        }
        return &t;
    }
    why = "unknown token";
    return nullptr;
}

bool node_admin_plugin::impl::rate_ok(const std::string &label) {
    const uint32_t now = static_cast<uint32_t>(fc::time_point::now().sec_since_epoch());
    std::lock_guard<std::mutex> lock(rate_mutex);
    auto &slot = rate[label];
    if (slot.first != now) {
        slot.first = now;
        slot.second = 0;
    }
    if (++slot.second > kRatePerSecond) return false;
    return true;
}

// ── HTTP entry ────────────────────────────────────────────────────────────────────────────
void node_admin_plugin::impl::handle_http(connection_hdl hdl) {
    websocket_server_type::connection_ptr con;
    try {
        con = server.get_con_from_hdl(hdl);
    } catch (...) {
        return;
    }
    if (!con) return;
    con->defer_http_response();

    const fc::time_point started = fc::time_point::now();
    const std::string rid = std::to_string(static_cast<unsigned long long>(++request_seq));
    // The real peer, not X-Forwarded-For: this listener is never behind a proxy by design, so
    // that header would only let a client choose its own audit attribution.
    std::string remote;
    try {
        remote = con->get_remote_endpoint();
    } catch (...) {
    }

    auto finish = [&](websocketpp::http::status_code::value status, const std::string &body) {
        respond(con, status, body);
    };

    // ── built-in UI: one static page, no data and no secrets in it ────────────────────────
    // The page cannot carry a bearer token on navigation, and it does not need one: it is
    // the same bytes for everybody and all real calls go back to this listener as POSTs
    // with the operator-supplied token. Serving it unauthenticated therefore leaks nothing.
    const std::string req_method = con->get_request().get_method();
    std::string path = con->get_request().get_uri();
    {
        const std::size_t qpos = path.find('?');
        if (qpos != std::string::npos) path = path.substr(0, qpos);
    }
    if (req_method == "GET" || req_method == "HEAD") {
        if (path == "/ui" || path == "/ui/") {
            try {
                con->append_header("X-Content-Type-Options", "nosniff");
                con->append_header("X-Frame-Options", "DENY");
                con->append_header("Referrer-Policy", "no-referrer");
                con->append_header("Content-Security-Policy",
                    "default-src 'none'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; "
                    "connect-src 'self'; base-uri 'none'; form-action 'none'; "
                    "frame-ancestors 'none'");
            } catch (const std::exception &e) {
                wlog("node_admin: failed to set UI headers: ${e}", ("e", e.what()));
            }
            respond(con, websocketpp::http::status_code::ok,
                    req_method == "HEAD" ? std::string() : std::string(ui_page()),
                    "text/html; charset=utf-8");
            audit(rid, "-", req_method + " " + path, fc::variant(), "ui", 0,
                  elapsed_ms(started), remote);
            return;
        }

        // Snapshot download: the newest file by default, or a named file from list_snapshots().
        // Unlike /ui/ this carries node state, so it is authenticated like every JSON-RPC call.
        static const std::string snap_prefix = "/snapshot/";
        if (path == "/snapshot" || path.compare(0, snap_prefix.size(), snap_prefix) == 0) {
            std::string why;
            const token_info *token =
                authenticate(con->get_request().get_header("Authorization"), why);
            if (!token) {
                respond(con, websocketpp::http::status_code::unauthorized,
                        to_json(rpc_err(fc::variant(), rpc_error{-32001, "unauthorized", why, ""})));
                audit(rid, "-", req_method + " " + path, fc::variant(), "unauthorized",
                      -32001, elapsed_ms(started), remote);
                return;
            }
            if (!rate_ok(token->label)) {
                respond(con, websocketpp::http::status_code::too_many_requests,
                        to_json(rpc_err(fc::variant(),
                                        rpc_error{-32005, "rate limit exceeded", "", ""})));
                audit(rid, token->label, req_method + " " + path, fc::variant(), "rate_limited",
                      -32005, elapsed_ms(started), remote);
                return;
            }
            if (token->scopes.find("read") == token->scopes.end()) {
                respond(con, websocketpp::http::status_code::forbidden,
                        to_json(rpc_err(fc::variant(),
                                        rpc_error{-32002, "forbidden",
                                                  "token '" + token->label + "' has no 'read' scope",
                                                  ""})));
                audit(rid, token->label, req_method + " " + path, fc::variant(), "forbidden",
                      -32002, elapsed_ms(started), remote);
                return;
            }

            std::string file;
            std::string filename;
            try {
                auto &snap = snapshot_started();
                if (path == "/snapshot" || path == "/snapshot/") {
                    respond(con, websocketpp::http::status_code::bad_request,
                            to_json(rpc_err(fc::variant(),
                                            rpc_error{-32602, "invalid params",
                                                      "use /snapshot/latest or /snapshot/<name>",
                                                      "names come from admin.snapshot.status"})));
                    audit(rid, token->label, req_method + " " + path, fc::variant(),
                          "bad_request", -32602, elapsed_ms(started), remote);
                    return;
                }
                const std::string name = path.substr(snap_prefix.size());
                if (name == "latest") {
                    file = snap.latest_snapshot_path();
                } else if (is_plain_snapshot_name(name)) {
                    file = (bfs::path(snap.snapshot_directory()) / name).string();
                } else {
                    respond(con, websocketpp::http::status_code::bad_request,
                            to_json(rpc_err(fc::variant(),
                                            rpc_error{-32602, "invalid snapshot name", name,
                                                      "expected a file name ending in .vizjson"})));
                    audit(rid, token->label, req_method + " " + path, fc::variant(),
                          "bad_request", -32602, elapsed_ms(started), remote);
                    return;
                }
                if (!file.empty()) filename = bfs::path(file).filename().string();
            } catch (const rpc_error &e) {
                respond(con, websocketpp::http::status_code::service_unavailable,
                        to_json(rpc_err(fc::variant(), e)));
                audit(rid, token->label, req_method + " " + path, fc::variant(), "error",
                      e.code, elapsed_ms(started), remote);
                return;
            } catch (const std::exception &e) {
                respond(con, websocketpp::http::status_code::internal_server_error,
                        to_json(rpc_err(fc::variant(),
                                        rpc_error{-32099, "internal error", e.what(), ""})));
                audit(rid, token->label, req_method + " " + path, fc::variant(),
                      "internal_error", -32099, elapsed_ms(started), remote);
                return;
            }

            boost::system::error_code ec;
            if (file.empty() || !bfs::exists(file, ec) || ec ||
                !bfs::is_regular_file(file, ec) || ec) {
                respond(con, websocketpp::http::status_code::not_found,
                        to_json(rpc_err(fc::variant(),
                                        rpc_error{-32602, "snapshot not found",
                                                  file.empty() ? "no snapshots in the snapshot directory" : file,
                                                  "see admin.snapshot.status"})));
                audit(rid, token->label, req_method + " " + path, fc::variant(), "not_found",
                      -32602, elapsed_ms(started), remote);
                return;
            }
            const uintmax_t size = bfs::file_size(file, ec);
            if (ec || size > kMaxSnapshotDownloadBytes) {
                respond(con, websocketpp::http::status_code::request_entity_too_large,
                        to_json(rpc_err(fc::variant(),
                                        rpc_error{-32600, "snapshot too large to serve over HTTP",
                                                  std::to_string(size) + " bytes (limit " +
                                                      std::to_string(kMaxSnapshotDownloadBytes) + ")",
                                                  "copy the file with scp instead"})));
                audit(rid, token->label, req_method + " " + path, fc::variant(),
                      "too_large", -32600, elapsed_ms(started), remote);
                return;
            }

            std::string content;
            if (req_method != "HEAD") {
                std::ifstream in(file, std::ios::binary);
                if (!in) {
                    respond(con, websocketpp::http::status_code::internal_server_error,
                            to_json(rpc_err(fc::variant(),
                                            rpc_error{-32099, "cannot open snapshot file",
                                                      file, ""})));
                    audit(rid, token->label, req_method + " " + path, fc::variant(),
                          "open_failed", -32099, elapsed_ms(started), remote);
                    return;
                }
                content.resize(static_cast<std::size_t>(size));
                if (size) in.read(&content[0], static_cast<std::streamsize>(size));
                content.resize(static_cast<std::size_t>(in.gcount()));
                if (!in && !in.eof()) {
                    respond(con, websocketpp::http::status_code::internal_server_error,
                            to_json(rpc_err(fc::variant(),
                                            rpc_error{-32099, "cannot read snapshot file",
                                                      file, ""})));
                    audit(rid, token->label, req_method + " " + path, fc::variant(),
                          "read_failed", -32099, elapsed_ms(started), remote);
                    return;
                }
            }

            try {
                con->append_header("X-Content-Type-Options", "nosniff");
                con->append_header("Content-Disposition",
                                   "attachment; filename=\"" + filename + "\"");
            } catch (const std::exception &e) {
                wlog("node_admin: failed to set snapshot headers: ${e}", ("e", e.what()));
            }
            respond(con, websocketpp::http::status_code::ok, content,
                    "application/octet-stream",
                    // A HEAD reply carries the headers a GET would return, so the file
                    // size must be explicit: the body is empty by design.
                    req_method == "HEAD" ? static_cast<int64_t>(size) : -1);
            audit(rid, token->label, req_method + " " + path,
                  fc::mutable_variant_object("file", filename)("bytes", static_cast<int64_t>(size)),
                  "snapshot_download", 0, elapsed_ms(started), remote);
            return;
        }
    }

    if (con->get_request().get_method() != "POST") {
        const fc::variant id;
        finish(websocketpp::http::status_code::method_not_allowed,
               to_json(rpc_err(id, rpc_error{-32600, "only POST is supported", "", ""})));
        audit(rid, "-", con->get_request().get_method(), fc::variant(), "method_not_allowed",
              -32600, elapsed_ms(started), remote);
        return;
    }

    const std::string body = con->get_request_body();
    if (body.empty()) {
        finish(websocketpp::http::status_code::bad_request,
               to_json(rpc_err(fc::variant(), rpc_error{-32700, "empty request body", "", ""})));
        audit(rid, "-", "-", fc::variant(), "empty_body", -32700, elapsed_ms(started), remote);
        return;
    }
    if (body.size() > kMaxBodyBytes) {
        finish(websocketpp::http::status_code::request_entity_too_large,
               to_json(rpc_err(fc::variant(), rpc_error{-32600, "request body too large", "", ""})));
        audit(rid, "-", "-", fc::variant(), "body_too_large", -32600, elapsed_ms(started), remote);
        return;
    }

    // ── auth first: parsing untrusted JSON before authentication is needless attack surface
    std::string why;
    const token_info *token = authenticate(con->get_request().get_header("Authorization"), why);
    if (!token) {
        finish(websocketpp::http::status_code::unauthorized,
               to_json(rpc_err(fc::variant(),
                                           rpc_error{-32001, "unauthorized", why, ""})));
        audit(rid, "-", "-", fc::variant(), "unauthorized", -32001, elapsed_ms(started), remote);
        return;
    }
    if (!rate_ok(token->label)) {
        finish(websocketpp::http::status_code::too_many_requests,
               to_json(rpc_err(fc::variant(),
                                           rpc_error{-32005, "rate limit exceeded", "", ""})));
        audit(rid, token->label, "-", fc::variant(), "rate_limited", -32005, elapsed_ms(started), remote);
        return;
    }

    // ── parse
    fc::variant request;
    try {
        request = fc::json::from_string(body);
    } catch (const fc::exception &e) {
        finish(websocketpp::http::status_code::bad_request,
               to_json(rpc_err(fc::variant(),
                                           rpc_error{-32700, "parse error", e.to_string(), ""})));
        audit(rid, token->label, "-", fc::variant(), "parse_error", -32700, elapsed_ms(started), remote);
        return;
    }
    if (!request.is_object()) {
        finish(websocketpp::http::status_code::bad_request,
               to_json(rpc_err(fc::variant(),
                                           rpc_error{-32600, "invalid request", "expected a JSON object", ""})));
        audit(rid, token->label, "-", fc::variant(), "invalid_request", -32600, elapsed_ms(started), remote);
        return;
    }

    const fc::variant_object &obj = request.get_object();
    fc::variant id;
    auto id_it = obj.find("id");
    if (id_it != obj.end()) id = id_it->value();

    if (!obj.contains("method") || !obj["method"].is_string()) {
        finish(websocketpp::http::status_code::bad_request,
               to_json(rpc_err(id, rpc_error{-32600, "invalid request", "missing 'method'", ""})));
        audit(rid, token->label, "-", fc::variant(), "invalid_request", -32600, elapsed_ms(started), remote);
        return;
    }
    const std::string method = obj["method"].as_string();
    fc::variant params;
    auto p_it = obj.find("params");
    if (p_it != obj.end()) params = p_it->value();

    // JSON-RPC notification (no id): MCP clients send `notifications/initialized` this way and
    // expect 202 with no body. Answering a notification with a response is a protocol error.
    if (method.compare(0, 14, "notifications/") == 0) {
        finish(websocketpp::http::status_code::accepted, "");
        audit(rid, token->label, method, params, "notification", 0, elapsed_ms(started), remote);
        return;
    }

    try {
        const fc::variant result = dispatch(method, params, *token);
        finish(websocketpp::http::status_code::ok, to_json(rpc_ok(id, result)));
        audit(rid, token->label, method, params, "ok", 0, elapsed_ms(started), remote);
    } catch (const rpc_error &e) {
        finish(websocketpp::http::status_code::ok, to_json(rpc_err(id, e)));
        audit(rid, token->label, method, params, "error", e.code, elapsed_ms(started), remote);
    } catch (const fc::exception &e) {
        finish(websocketpp::http::status_code::ok,
               to_json(rpc_err(id, rpc_error{-32099, "internal error", e.to_string(), ""})));
        audit(rid, token->label, method, params, "internal_error", -32099, elapsed_ms(started), remote);
    } catch (const std::exception &e) {
        finish(websocketpp::http::status_code::ok,
               to_json(rpc_err(id, rpc_error{-32099, "internal error", e.what(), ""})));
        audit(rid, token->label, method, params, "internal_error", -32099, elapsed_ms(started), remote);
    }
}

// ── dispatch ─────────────────────────────────────────────────────────────────────────────
fc::variant node_admin_plugin::impl::rpc_ok(const fc::variant &id, const fc::variant &result) const {
    fc::mutable_variant_object r;
    r("jsonrpc", "2.0");
    r("id", id);
    r("result", result);
    return fc::variant(r);
}

fc::variant node_admin_plugin::impl::rpc_err(const fc::variant &id, const rpc_error &e) const {
    fc::mutable_variant_object err;
    err("code", static_cast<int64_t>(e.code));
    err("message", e.message);
    if (!e.reason.empty() || !e.hint.empty()) {
        fc::mutable_variant_object data;
        if (!e.reason.empty()) data("reason", e.reason);
        if (!e.hint.empty()) data("hint", e.hint);
        err("data", data);
    }
    fc::mutable_variant_object r;
    r("jsonrpc", "2.0");
    r("id", id);
    r("error", err);
    return fc::variant(r);
}

fc::variant node_admin_plugin::impl::dispatch(const std::string &method, const fc::variant &params,
                                             const token_info &token) {
    // v1 is read-only; every method needs the `read` scope. Later stages add their own
    // scopes (plugins/config/process/chain) on the same check.
    if (token.scopes.find("read") == token.scopes.end()) {
        throw rpc_error{-32002, "forbidden", "token '" + token.label + "' has no 'read' scope", ""};
    }

    // The UI asks who it is so it can hide actions the token cannot perform. Answer with the
    // token's own label and scopes — never with the token or its hash.
    if (method == "admin.auth.me") return h_auth_me(token);
    if (method == "admin.ping") return h_ping(params);
    if (method == "admin.node_info") return h_node_info(params);
    if (method == "admin.diagnostics") return h_diagnostics(params);
    if (method == "admin.plugins.list") return h_plugins_list(params);
    if (method == "admin.plugins.status") return h_plugins_status(params);
    if (method == "admin.config.get") return h_config_get(params);
    // Chainbase introspection: which object types exist, how many rows each holds, and how
    // much of the mapped file is used. Read-only; the check is the same view plus a verdict.
    if (method == "admin.objects.list") return h_objects_list(params);
    if (method == "admin.objects.check") return h_objects_check(params);
    if (method == "admin.logs.list") return h_logs_list(params);
    if (method == "admin.logs.tail") return h_logs_tail(params);
    if (method == "admin.audit.tail") return h_audit_tail(params);
    if (method == "admin.validators.active") return h_validators_active(params);
    if (method == "admin.p2p.status") return h_p2p_status(params);
    if (method == "admin.snapshot.status") return h_snapshot_status(params);
    if (method == "admin.webserver.status") return h_webserver_status(params);
    if (method == "admin.webserver.ips") return h_webserver_ips(params);

    // Mutating peer operations need `p2p` on top of `read`: a read-only token must not be able
    // to change whom this node talks to. Both required scopes are named in the error.
    if (method == "admin.p2p.add_node" || method == "admin.p2p.add_trusted" ||
        method == "admin.p2p.clear_banned") {
        if (token.scopes.find("p2p") == token.scopes.end()) {
            throw rpc_error{-32002, "forbidden",
                            "token '" + token.label + "' has no 'p2p' scope",
                            "peer operations need a token with scopes 'read,p2p'"};
        }
        if (method == "admin.p2p.add_node") return h_p2p_add_node(params, false);
        if (method == "admin.p2p.add_trusted") return h_p2p_add_node(params, true);
        return h_p2p_clear_banned(params);
    }

    // Snapshot creation and restore rewrite node state / disk, so they need `snapshot`
    // on top of `read`. Listing and downloading stay under `read`.
    if (method == "admin.snapshot.create" || method == "admin.snapshot.restore") {
        if (token.scopes.find("snapshot") == token.scopes.end()) {
            throw rpc_error{-32002, "forbidden",
                            "token '" + token.label + "' has no 'snapshot' scope",
                            "snapshot operations need a token with scopes 'read,snapshot'"};
        }
        if (method == "admin.snapshot.create") return h_snapshot_create(params);
        return h_snapshot_restore(params);
    }

    // The webserver knobs (limits, IP lists, stats switch, reset/reload) change how the
    // public API treats its clients, so they need the `webserver` scope. Reads stay under
    // `read`. Changes are applied to the live plugin immediately and persisted to its
    // admin state file; config.ini is only the startup seed.
    if (method == "admin.webserver.limits" || method == "admin.webserver.stats" ||
        method == "admin.webserver.whitelist_add" || method == "admin.webserver.whitelist_remove" ||
        method == "admin.webserver.blacklist_add" || method == "admin.webserver.blacklist_remove" ||
        method == "admin.webserver.reset" || method == "admin.webserver.reload") {
        if (token.scopes.find("webserver") == token.scopes.end()) {
            throw rpc_error{-32002, "forbidden",
                            "token '" + token.label + "' has no 'webserver' scope",
                            "webserver operations need a token with scopes 'read,webserver'"};
        }
        if (method == "admin.webserver.limits") return h_webserver_limits(params);
        if (method == "admin.webserver.stats") return h_webserver_stats(params);
        if (method == "admin.webserver.whitelist_add") return h_webserver_list_add(params, true);
        if (method == "admin.webserver.whitelist_remove") return h_webserver_list_remove(params, true);
        if (method == "admin.webserver.blacklist_add") return h_webserver_list_add(params, false);
        if (method == "admin.webserver.blacklist_remove") return h_webserver_list_remove(params, false);
        if (method == "admin.webserver.reset") return h_webserver_reset(params);
        return h_webserver_reload(params);
    }

    // Config.ini edits are two-phase: plan returns the exact diff plus a single-use,
    // token-bound plan_id; apply re-validates against the current file and writes atomically
    // with a backup. Reads (config.backups) stay under `read`.
    if (method == "admin.config.plan" || method == "admin.config.apply" ||
        method == "admin.config.backups" || method == "admin.plugins.plan" ||
        method == "admin.plugins.apply") {
        const bool cfg = method == "admin.config.plan" || method == "admin.config.apply";
        const bool plg = method == "admin.plugins.plan" || method == "admin.plugins.apply";
        if (cfg && token.scopes.find("config") == token.scopes.end()) {
            throw rpc_error{-32002, "forbidden",
                            "token '" + token.label + "' has no 'config' scope",
                            "config edits need a token with scopes 'read,config'"};
        }
        if (plg && token.scopes.find("plugins") == token.scopes.end()) {
            throw rpc_error{-32002, "forbidden",
                            "token '" + token.label + "' has no 'plugins' scope",
                            "plugin edits need a token with scopes 'read,plugins'"};
        }
        if (method == "admin.config.plan") return h_config_plan(params, token);
        if (method == "admin.config.apply") return h_config_apply(params, token);
        if (method == "admin.config.backups") return h_config_backups(params);
        if (method == "admin.plugins.plan") return h_plugins_plan(params, token);
        return h_plugins_apply(params, token);
    }

    if (method == "admin.plan.cancel") return h_plan_cancel(params, token);

    if (method == "admin.restart" || method == "admin.objects.repair") {
        if (token.scopes.find("process") == token.scopes.end()) {
            throw rpc_error{-32002, "forbidden",
                            "token '" + token.label + "' has no 'process' scope",
                            "restart and state repair need a token with scopes 'read,process'"};
        }
        if (method == "admin.objects.repair") return h_objects_repair(params);
        return h_restart(params);
    }

    // Validator chain operations are signed INSIDE validator_guard with its active keys
    // (decision q#3051=D): node_admin never sees a key. Reads stay under `read`; plan/apply
    // need `chain`, and that scope is usable only while a live guard holds an active key.
    if (method == "admin.validator.list" || method == "admin.validator.status") {
        return h_validator_list(params);
    }
    if (method == "admin.validator.plan" || method == "admin.validator.apply") {
        if (token.scopes.find("chain") == token.scopes.end()) {
            throw rpc_error{-32002, "forbidden",
                            "token '" + token.label + "' has no 'chain' scope",
                            "validator operations need a token with scopes 'read,chain'"};
        }
        if (!guard_ready()) {
            throw rpc_error{-32002, "forbidden", "validator-guard is not configured",
                            "chain operations are signed by validator_guard; configure the "
                            "plugin with validator-guard-validator = [name,signing_wif,active_wif]"};
        }
        if (method == "admin.validator.plan") return h_validator_plan(params, token);
        return h_validator_apply(params, token);
    }

    // MCP Streamable HTTP on the same authenticated listener (decision q#3052=B: the MCP
    // endpoint lives inside the plugin, not in a sidecar). Tools are 1:1 with the admin.*
    // methods above, so the audit trail and scopes are shared.
    if (method == "initialize") return h_mcp_initialize(params);
    if (method == "ping") return h_mcp_ping(params);
    if (method == "tools/list") return h_mcp_tools_list(params);
    if (method == "tools/call") return h_mcp_tools_call(params, token);

    throw rpc_error{-32601, "method not found", method, ""};
}

// ── methods ──────────────────────────────────────────────────────────────────────────────
fc::mutable_variant_object node_admin_plugin::impl::make_node_info() const {
    const graphene::chain::database &db = chain.db();
    const auto head_time = db.head_block_time();
    const auto now = fc::time_point::now().sec_since_epoch();
    const int64_t head_age = static_cast<int64_t>(now) - static_cast<int64_t>(head_time.sec_since_epoch());

    fc::mutable_variant_object m;
    m("server_time", now_iso());
    m("version", std::string(CHAIN_VERSION));
    m("hardfork_count", static_cast<int64_t>(CHAIN_NUM_HARDFORKS));
    m("chain_id", db.get_chain_id().str());
    m("head_block_num", static_cast<int64_t>(db.head_block_num()));
    m("head_block_id", db.head_block_id().str());
    m("head_block_time", iso_utc(head_time));
    // An empty chain reports head time 1970, so the "age" would be 56 years and mean nothing.
    m("head_age_sec", db.head_block_num() == 0 ? fc::variant() : fc::variant(head_age < 0 ? 0 : head_age));
    const auto &dgp = db.get_dynamic_global_properties();
    m("last_irreversible_block_num", static_cast<int64_t>(dgp.last_irreversible_block_num));
    // Emergency consensus is a chain-wide state voted in by validators. It used to be visible
    // only in the p2p hello and in the log, never on the operator page.
    m("emergency_consensus_active", dgp.emergency_consensus_active);
    m("emergency_consensus_start_block",
      static_cast<int64_t>(dgp.emergency_consensus_active ? dgp.emergency_consensus_start_block : 0));
    bool emergency_key = false;
    if (auto *vp = appbase::app().find_plugin<graphene::plugins::validator_plugin::validator_plugin>()) {
        emergency_key = vp->is_emergency_key_configured();
    }
    m("emergency_key_configured", emergency_key);
    m("recovering", chain.is_recovering());
    m("uptime_sec", static_cast<int64_t>((fc::time_point::now() - started_at).count() / 1000000));
    return m;
}

fc::variant node_admin_plugin::impl::h_ping(const fc::variant &) {
    fc::mutable_variant_object m;
    m("ok", true);
    m("server_time", now_iso());
    return fc::variant(m);
}

// The UI cannot guess what a token may do: the same page is opened with a read-only token and
// with a full one. It asks here and hides the actions the token cannot perform. Only the label,
// the scopes and the expiry leave this method — never the token or its hash.
fc::variant node_admin_plugin::impl::h_auth_me(const token_info &token) {
    fc::variants scopes;
    for (const std::string &s : token.scopes) scopes.push_back(fc::variant(s));

    fc::mutable_variant_object m;
    m("label", token.label);
    m("scopes", scopes);
    m("expires", token.expires == 0 ? fc::variant()
                                    : fc::variant(static_cast<int64_t>(token.expires)));
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_node_info(const fc::variant &) {
    return fc::variant(make_node_info());
}

fc::variant node_admin_plugin::impl::h_diagnostics(const fc::variant &) {
    fc::mutable_variant_object m = make_node_info();

    m("data_dir", data_dir());
    m("config_path", config_path());

    // peers: read through the p2p thread, because the peer map is not thread-safe
    bool p2p_loaded = false;
    uint32_t peers = 0;
    if (auto *p2p = appbase::app().find_plugin<graphene::plugins::p2p::p2p_plugin>()) {
        p2p_loaded = true;
        if (p2p->get_state() == appbase::abstract_plugin::started) {
            peers = p2p->get_connections_count_sync();
        }
    }
    m("p2p_loaded", p2p_loaded);
    m("peers", static_cast<int64_t>(peers));

    // configured plugins: appbase keeps its registry private, so config.ini is what we can see.
    // `plugins_observed` counts the names whose lifecycle state is read through a typed
    // find_plugin<T>(); the rest are known to be loaded by construction (appbase aborts startup
    // on an unknown plugin name) but their state cannot be read from here.
    bool cfg_exists = false;
    const std::vector<cfg_entry> cfg = parse_config_file(config_path(), &cfg_exists);
    std::set<std::string> configured;
    for (const auto &e : cfg) {
        if (e.key != "plugin" || e.value.empty()) continue;
        for (const std::string &name : util::plugin_names_in_value(e.value)) configured.insert(name);
    }
    std::size_t observed_count = 0;
    for (const auto &name : configured) {
        if (observe_plugin(name) != nullptr) ++observed_count;
    }
    m("plugins_configured", static_cast<int64_t>(configured.size()));
    m("plugins_observed", static_cast<int64_t>(observed_count));

    // shared memory file
    boost::system::error_code ec;
    // shared_memory.bin follows shared-file-dir, which the chain plugin resolves at startup.
    const bfs::path shm = bfs::path(chain.get_state_dir()) / "shared_memory.bin";
    const uintmax_t shm_size = bfs::file_size(shm, ec);
    if (!ec) m("shared_memory_bytes", static_cast<int64_t>(shm_size));

#ifndef _WIN32
    struct statvfs vfs;
    if (::statvfs(data_dir().c_str(), &vfs) == 0) {
        m("disk_total_bytes",
          static_cast<int64_t>(static_cast<uint64_t>(vfs.f_blocks) * vfs.f_frsize));
        m("disk_free_bytes",
          static_cast<int64_t>(static_cast<uint64_t>(vfs.f_bavail) * vfs.f_frsize));
    }
    struct rusage usage;
    if (::getrusage(RUSAGE_SELF, &usage) == 0) {
        m("rss_peak_kb", static_cast<int64_t>(usage.ru_maxrss));
    }
    double load[3] = {0, 0, 0};
    if (::getloadavg(load, 3) == 3) {
        m("loadavg", fc::variants{fc::variant(load[0]), fc::variant(load[1]), fc::variant(load[2])});
    }
#endif

    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_plugins_list(const fc::variant &) {
    bool exists = false;
    const std::vector<cfg_entry> cfg = parse_config_file(config_path(), &exists);

    fc::variants plugins;
    std::set<std::string> seen;
    for (const auto &e : cfg) {
        if (e.key != "plugin" || e.value.empty()) continue;
        // A line may name several plugins (`plugin = chain p2p`); appbase loads each of them,
        // so each gets its own row. Keeping the line as one name made state, status_method and
        // parameter grouping unreachable for every plugin on such a line.
        for (const std::string &name : util::plugin_names_in_value(e.value)) {
            if (!seen.insert(name).second) continue;

            const appbase::abstract_plugin *p = observe_plugin(name);
            const char *state = "unknown";
            const char *source = "inferred";
            if (p) {
                source = "observed";
                switch (p->get_state()) {
                    case appbase::abstract_plugin::registered: state = "registered"; break;
                    case appbase::abstract_plugin::initialized: state = "initialized"; break;
                    case appbase::abstract_plugin::started: state = "started"; break;
                    case appbase::abstract_plugin::stopped: state = "stopped"; break;
                    default: state = "unknown"; break;
                }
            }
            fc::mutable_variant_object item;
            item("name", name);
            // A configured plugin is always loaded: appbase calls get_plugin(name) for every
            // `plugin =` line and aborts startup when the name is unknown.
            item("loaded", p ? p->get_state() != appbase::abstract_plugin::registered : true);
            item("state", p ? fc::variant(std::string(state)) : fc::variant());
            item("state_source", std::string(source));
            // The UI needs both facts before it can draw a toggle: a plugin that cannot be disabled
            // must not render as merely "off", and a plugin with no allow-listed keys must say so
            // instead of showing an empty panel.
            item("removable", !util::plugin_removal_forbidden(name));
            // Plugins that answer a status question of their own. Null means the plugin exposes
            // no operator status: the panel should say so instead of drawing an empty drawer.
            const char *status_method = nullptr;
            if (name == "p2p") status_method = "admin.p2p.status";
            else if (name == "webserver") status_method = "admin.webserver.status";
            else if (name == "snapshot") status_method = "admin.snapshot.status";
            else if (name == "validator_guard") status_method = "admin.validator.list";
            item("status_method", status_method ? fc::variant(std::string(status_method)) : fc::variant());
            fc::variants params;
            for (const util::config_spec &spec : util::config_allowlist()) {
                if (util::config_key_group(spec.name) != name) continue;
                fc::mutable_variant_object pitem;
                pitem("key", spec.name);
                params.push_back(fc::variant(pitem));
            }
            item("params", params);
            plugins.push_back(fc::variant(item));
        }
    }

    // The catalogue the binary can load, so the panel can offer an enable action. It is the
    // same set admin.plugins.plan validates `add` against; a name that is already configured
    // still appears here, and the client subtracts the configured ones itself.
    fc::variants known;
    for (const std::string &name : util::known_plugins()) known.push_back(fc::variant(name));

    fc::mutable_variant_object m;
    m("config_path", config_path());
    m("config_exists", exists);
    m("known", known);
    m("note", "Entries come from config.ini. Appbase keeps its plugin registry private, so "
              "lifecycle state is observed only for the plugin types this binary's node_admin "
              "links (chain, p2p, snapshot, webserver, validator, validator_guard, node_admin); "
              "for the others loaded=true is implied by startup (an unknown plugin name aborts "
              "the node). status_method names the method that returns that plugin's own status.");
    m("plugins", plugins);
    return fc::variant(m);
}

// A plugin's lifecycle state says whether it is running; it does not say what it is doing.
// This method answers the second question for every plugin that has a status API of its own,
// and says so plainly when there is none instead of returning an empty object.
fc::variant node_admin_plugin::impl::h_plugins_status(const fc::variant &params) {
    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected {name: \"p2p\"}", ""};
    }
    const std::string name = trim(param_str(params.get_object(), "name", ""));
    if (name.empty()) {
        throw rpc_error{-32602, "invalid params", "name is required", ""};
    }

    bool exists = false;
    const std::vector<cfg_entry> cfg = parse_config_file(config_path(), &exists);
    bool configured = false;
    for (const cfg_entry &e : cfg) {
        if (e.key == "plugin" && e.value == name) { configured = true; break; }
    }

    const appbase::abstract_plugin *p = observe_plugin(name);
    std::string state = "unobserved";
    if (p) {
        switch (p->get_state()) {
            case appbase::abstract_plugin::registered: state = "registered"; break;
            case appbase::abstract_plugin::initialized: state = "initialized"; break;
            case appbase::abstract_plugin::started: state = "started"; break;
            case appbase::abstract_plugin::stopped: state = "stopped"; break;
            default: state = "unknown"; break;
        }
    }
    const bool started = p && p->get_state() == appbase::abstract_plugin::started;

    fc::mutable_variant_object m;
    m("name", name);
    m("configured", configured);
    m("state", p ? fc::variant(state) : fc::variant());
    m("state_source", p ? std::string("observed") : std::string("unobserved"));

    // Only call into a plugin that is actually started: these handlers touch thread-owned
    // state (peer map, connection counters) and would throw on a stopped plugin.
    fc::variant detail;
    std::string detail_method;
    if (started) {
        if (name == "p2p") { detail = h_p2p_status(params); detail_method = "admin.p2p.status"; }
        else if (name == "webserver") { detail = h_webserver_status(params); detail_method = "admin.webserver.status"; }
        else if (name == "snapshot") { detail = h_snapshot_status(params); detail_method = "admin.snapshot.status"; }
        else if (name == "validator_guard") { detail = h_validator_list(params); detail_method = "admin.validator.list"; }
    }
    m("detail_available", !detail_method.empty());
    m("detail_method", detail_method.empty() ? fc::variant() : fc::variant(detail_method));
    if (!detail_method.empty()) m("detail", detail);
    else if (!p) m("reason", "this binary does not link this plugin, so its lifecycle cannot be "
                             "observed; a configured plugin is loaded or startup would have aborted");
    else if (!started) m("reason", "the plugin is not running, so it has no status to report");
    else m("reason", "this plugin exposes no status of its own; only its lifecycle state is known");
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_config_get(const fc::variant &) {
    bool exists = false;
    const std::vector<cfg_entry> cfg = parse_config_file(config_path(), &exists);

    // Preserve order and duplicate keys (config.ini repeats `plugin = ...`), so this is an
    // array of sections rather than a map.
    fc::variants sections;
    std::string current;
    fc::variants current_entries;
    bool have_section = false;
    auto flush = [&]() {
        fc::mutable_variant_object s;
        s("name", current);
        s("entries", current_entries);
        sections.push_back(fc::variant(s));
        current_entries.clear();
    };
    for (const auto &e : cfg) {
        if (!have_section || e.section != current) {
            if (have_section) flush();
            current = e.section;
            have_section = true;
        }
        fc::mutable_variant_object item;
        item("key", e.key);
        item("value", sensitive_key(e.key) ? std::string("***") : e.value);
        current_entries.push_back(fc::variant(item));
    }
    if (have_section) flush();

    // The editable subset, with its current value: this is what the parameters panel renders.
    // Keys are the same full addresses admin.config.plan accepts (`logger.default.level`), so the
    // UI never has to know which keys are sectioned.
    std::string raw_text;
    std::string raw_why;
    std::vector<std::string> all;
    if (read_text_file(config_path(), raw_text, raw_why)) all = split_lines(raw_text);
    fc::variants allow;
    for (const util::config_spec &spec : util::config_allowlist()) {
        std::string value;
        const bool found = allowed_config_value(all, spec.name, value);
        // Report the value in the form the editor accepts, not as written on disk: config.ini
        // may say `yes` or `1` where the panel renders a true/false select, and an input that
        // does not match any option would silently show as blank. A value that fails
        // normalization is returned raw with valid=false so the panel can flag the line.
        bool valid = found;
        if (found) {
            std::string norm;
            std::string why;
            if (util::normalize_config_value(spec.name, value, norm, why)) value = norm;
            else valid = false;
        }
        fc::mutable_variant_object a;
        a("key", spec.name);
        a("group", util::config_key_group(spec.name));
        a("section", spec.section);
        a("type", spec.type == util::config_type::boolean ? "boolean"
                 : spec.type == util::config_type::log_level ? "log_level"
                                                            : "unsigned_int");
        a("value", found ? fc::variant(value) : fc::variant());
        a("defaulted", !found);
        a("valid", valid);
        if (spec.type == util::config_type::unsigned_int) {
            a("min", spec.min_value);
            a("max", spec.max_value);
        }
        if (spec.type == util::config_type::log_level) {
            fc::variants levels;
            for (const std::string &l : util::log_level_values()) levels.push_back(fc::variant(l));
            a("values", levels);
        }
        a("needs_restart", spec.needs_restart);
        allow.push_back(fc::variant(a));
    }

    fc::mutable_variant_object m;
    m("path", config_path());
    m("exists", exists);
    m("sections", sections);
    m("allow", allow);
    return fc::variant(m);
}

// Which logs an operator can tail. A file that is configured but not there yet is still listed:
// "the appender has not written anything" is a different fact from "nobody configured it".
fc::variant node_admin_plugin::impl::h_logs_list(const fc::variant &) {
    fc::variants arr;
    for (const auto &kv : log_files) {
        boost::system::error_code ec;
        const uintmax_t size = bfs::file_size(kv.second, ec);
        fc::mutable_variant_object m;
        m("name", kv.first);
        m("path", kv.second);
        m("exists", !ec);
        m("size", ec ? fc::variant() : fc::variant(static_cast<int64_t>(size)));
        arr.push_back(fc::variant(m));
    }

    fc::mutable_variant_object m;
    m("logs", arr);
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_logs_tail(const fc::variant &params) {
    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected an object {name, bytes?, lines?}", ""};
    }
    const fc::variant_object &o = params.get_object();
    const std::string name = param_str(o, "name", "");
    if (name.empty()) {
        throw rpc_error{-32602, "invalid params", "missing 'name'", "known names: the admin-log-file entries"};
    }
    auto it = log_files.find(name);
    if (it == log_files.end()) {
        std::string known;
        for (const auto &kv : log_files) {
            if (!known.empty()) known += ", ";
            known += kv.first;
        }
        throw rpc_error{-32004, "unknown log", "no admin-log-file entry named '" + name + "'",
                        known.empty() ? "no admin-log-file entries are configured" : "known: " + known};
    }

    const int64_t bytes = std::max<int64_t>(1, std::min<int64_t>(
        param_int(o, "bytes", 16 * 1024), static_cast<int64_t>(kMaxLogBytes)));
    const int64_t lines = std::max<int64_t>(1, std::min<int64_t>(
        param_int(o, "lines", 200), static_cast<int64_t>(kMaxLines)));

    tail_result t = tail_file(it->second, static_cast<uint64_t>(bytes), static_cast<std::size_t>(lines));
    if (!t.ok) {
        throw rpc_error{-32004, "cannot read log", t.error, it->second};
    }

    fc::variants out;
    for (const std::string &line : t.lines) out.push_back(fc::variant(mask_text(line)));

    fc::mutable_variant_object m;
    m("name", name);
    m("path", it->second);
    m("size", static_cast<int64_t>(t.size));
    m("bytes_returned", static_cast<int64_t>(t.bytes_returned));
    m("lines", out);
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_audit_tail(const fc::variant &params) {
    const fc::variant_object empty;
    const fc::variant_object &o = params.is_object() ? params.get_object() : empty;
    const int64_t lines = std::max<int64_t>(1, std::min<int64_t>(
        param_int(o, "lines", 100), static_cast<int64_t>(kMaxLines)));

    tail_result t = tail_file(audit_path, kMaxLogBytes, static_cast<std::size_t>(lines));
    if (!t.ok) {
        throw rpc_error{-32004, "cannot read audit log", t.error, audit_path};
    }
    fc::variants out;
    for (const std::string &line : t.lines) out.push_back(fc::variant(line));

    fc::mutable_variant_object m;
    m("path", audit_path);
    m("size", static_cast<int64_t>(t.size));
    m("lines", out);
    return fc::variant(m);
}

// ── P2P operator methods ─────────────────────────────────────────────────────────────────
graphene::plugins::p2p::p2p_plugin &node_admin_plugin::impl::p2p_started() const {
    auto *p2p = appbase::app().find_plugin<graphene::plugins::p2p::p2p_plugin>();
    if (p2p == nullptr || p2p->get_state() != appbase::abstract_plugin::started) {
        throw rpc_error{-32004, "p2p unavailable",
                        "the p2p plugin is not loaded or not started",
                        "peer administration needs 'plugin = p2p' in config.ini"};
    }
    return *p2p;
}

// Mirrors what the node writes to its own log every minute (log_node_status) and every five
// minutes (log_peer_stats): the same facts, on demand, for an operator or agent.
fc::variant node_admin_plugin::impl::h_p2p_status(const fc::variant &) {
    const graphene::network::dlt_peer_report r = p2p_started().get_peer_report_sync();

    fc::mutable_variant_object m;
    m("as_of", now_iso());
    m("uptime_sec", static_cast<int64_t>(r.uptime_sec));
    m("node_status", r.node_status);
    m("fork_status", r.fork_status);
    m("paused", r.paused);
    m("isolated_peers", r.isolated_peers);
    m("catching_up", r.catching_up);
    m("head_block_num", static_cast<int64_t>(r.our_head));
    m("lib_block_num", static_cast<int64_t>(r.our_lib));
    m("dlt_earliest", static_cast<int64_t>(r.dlt_earliest));
    m("dlt_latest", static_cast<int64_t>(r.dlt_latest));
    m("connections", static_cast<int64_t>(r.connection_count));
    m("active", static_cast<int64_t>(r.active_count));
    m("max_connections", static_cast<int64_t>(r.max_connections));
    m("known_peers", static_cast<int64_t>(r.known_peers));
    m("trusted_peers", static_cast<int64_t>(r.trusted_peers));
    m("banned_peers", static_cast<int64_t>(r.banned_peers));
    m("best_peer_head", static_cast<int64_t>(r.best_peer_head));
    // our head minus the best peer head: negative means we are behind the network
    m("best_peer_head_lag", r.best_peer_head_lag);
    m("peers_ahead", static_cast<int64_t>(r.peers_ahead));

    fc::variants peers;
    for (const graphene::network::dlt_peer_report_entry &p : r.peers) {
        fc::mutable_variant_object e;
        e("endpoint", p.endpoint);
        e("node_id", p.node_id);
        e("lifecycle", p.lifecycle);
        e("node_status", p.node_status);
        e("fork_status", p.fork_status);
        e("incoming", p.incoming);
        e("trusted", p.trusted);
        e("exchange_enabled", p.exchange_enabled);
        e("fork_alignment", p.fork_alignment);
        e("connected_sec", static_cast<int64_t>(p.connected_sec));
        e("peer_head", static_cast<int64_t>(p.peer_head));
        e("peer_lib", static_cast<int64_t>(p.peer_lib));
        e("peer_dlt_earliest", static_cast<int64_t>(p.peer_dlt_earliest));
        e("peer_dlt_latest", static_cast<int64_t>(p.peer_dlt_latest));
        e("head_lag", p.head_lag);
        e("spam_strikes", static_cast<int64_t>(p.spam_strikes));
        e("ban_reason", p.ban_reason);
        e("ban_duration_sec", static_cast<int64_t>(p.ban_duration_sec));
        e("reconnect_backoff_sec", static_cast<int64_t>(p.reconnect_backoff_sec));
        e("next_reconnect_in_sec", static_cast<int64_t>(p.next_reconnect_in_sec));
        peers.push_back(fc::variant(e));
    }
    m("peers", peers);
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_p2p_add_node(const fc::variant &params, bool trusted) {
    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected an object {endpoint}",
                        "endpoint is IPv4:port, e.g. 1.2.3.4:1776"};
    }
    const fc::variant_object &o = params.get_object();
    const std::string text = trim(param_str(o, "endpoint", ""));
    if (text.empty()) {
        throw rpc_error{-32602, "invalid params", "missing 'endpoint'",
                        "endpoint is IPv4:port, e.g. 1.2.3.4:1776"};
    }

    fc::ip::endpoint ep;
    try {
        ep = fc::ip::endpoint::from_string(text);
    } catch (const fc::exception &) {
        throw rpc_error{-32602, "invalid endpoint", text,
                        "expected IPv4:port, e.g. 1.2.3.4:1776"};
    }
    if (ep.port() == 0) {
        throw rpc_error{-32602, "invalid endpoint", text, "port must be 1..65535"};
    }

    // Serialized onto the p2p thread: the peer maps are owned there.
    p2p_started().add_peer_sync(ep, trusted);

    fc::mutable_variant_object m;
    m("endpoint", std::string(ep));
    m("trusted", trusted);
    m("status", "added");
    m("note", trusted
        ? "trusted peers are never soft-banned and are kept through known-peer GC; the trusted "
          "set lives in memory and is not persisted across restarts"
        : "the peer is dialed now; use admin.p2p.add_trusted to exempt it from soft bans");
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_p2p_clear_banned(const fc::variant &) {
    const uint32_t cleared = p2p_started().clear_banned_peers_sync();
    fc::mutable_variant_object m;
    m("cleared", static_cast<int64_t>(cleared));
    m("status", "ok");
    return fc::variant(m);
}

// ── snapshots ────────────────────────────────────────────────────────────────────────────
graphene::plugins::snapshot::snapshot_plugin &node_admin_plugin::impl::snapshot_started() const {
    auto *snap = appbase::app().find_plugin<graphene::plugins::snapshot::snapshot_plugin>();
    if (!snap) {
        throw rpc_error{-32004, "snapshot unavailable",
                        "the snapshot plugin is not loaded",
                        "add 'plugin = snapshot' to config.ini"};
    }
    return *snap;
}

fc::variant node_admin_plugin::impl::snapshot_list_variant() const {
    auto &snap = snapshot_started();
    const std::string latest_path = snap.latest_snapshot_path();
    fc::variants files;
    for (const auto &f : snap.list_snapshots()) {
        fc::mutable_variant_object m;
        m("name", f.name);
        m("path", f.path);
        m("block_num", static_cast<int64_t>(f.block_num));
        m("size_bytes", static_cast<int64_t>(f.size_bytes));
        m("modified", static_cast<int64_t>(f.modified));
        m("modified_iso", iso_utc(fc::time_point_sec(f.modified)));
        m("latest", !latest_path.empty() && f.path == latest_path);
        files.push_back(fc::variant(m));
    }
    return fc::variant(files);
}

fc::variant node_admin_plugin::impl::h_snapshot_status(const fc::variant &params) {
    const fc::variant_object empty;
    const fc::variant_object &o = params.is_object() ? params.get_object() : empty;
    // A node that has been running for a year accumulates hundreds of snapshots; the page shows
    // the newest page and asks for the next one on demand.
    const int64_t offset = std::max<int64_t>(0, param_int(o, "offset", 0));
    const int64_t limit = std::max<int64_t>(1, std::min<int64_t>(
        param_int(o, "limit", 20), static_cast<int64_t>(kMaxSnapshotPage)));
    // Plain offset paging on a list that changes under the reader repeats or skips rows: a
    // snapshot written between two pages shifts every index below it by one, and the retention
    // sweep removes from the tail. A client that sends the name of the last row it already has
    // gets the page that follows THAT row ("after"); offset stays the fallback for an anchor that
    // has already been pruned, and the reply says so instead of silently serving a shifted page.
    const std::string after = param_str(o, "after", "");

    auto &snap = snapshot_started();
    const bool recovering = chain.is_recovering();
    const fc::variant all_files = snapshot_list_variant();
    const fc::variants &files = all_files.get_array();
    const std::string latest_path = snap.latest_snapshot_path();

    fc::variants page;
    std::size_t begin = std::min<std::size_t>(static_cast<std::size_t>(offset), files.size());
    bool after_found = false;
    if (!after.empty()) {
        for (std::size_t i = 0; i < files.size(); ++i) {
            if (files[i].get_object()["name"].as_string() == after) {
                begin = i + 1;
                after_found = true;
                break;
            }
        }
    }
    const std::size_t end = std::min<std::size_t>(
        begin + static_cast<std::size_t>(limit), files.size());
    for (std::size_t i = begin; i < end; ++i) page.push_back(files[i]);

    fc::mutable_variant_object m;
    m("as_of", now_iso());
    m("dir", snap.snapshot_directory());
    // While a restore runs the database is closed or being rebuilt; reading head there
    // would either block or report a half-built value, so it is null during that window.
    m("head_block_num", recovering ? fc::variant()
                                   : fc::variant(static_cast<int64_t>(chain.db().head_block_num())));
    m("snapshot_count", static_cast<int64_t>(files.size()));
    m("snapshot_total", static_cast<int64_t>(files.size()));
    m("offset", offset);
    m("limit", limit);
    m("after", after.empty() ? fc::variant() : fc::variant(after));
    m("after_found", after_found);
    m("has_more", static_cast<int64_t>(end) < static_cast<int64_t>(files.size()));
    m("in_progress", snap.is_snapshot_in_progress());
    m("restoring", snap.is_restoring());
    m("recovering", recovering);
    m("latest_path", latest_path);
    m("latest_name", latest_path.empty()
                         ? fc::variant()
                         : fc::variant(bfs::path(latest_path).filename().string()));
    m("snapshots", page);
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_snapshot_create(const fc::variant &) {
    auto &snap = snapshot_started();
    std::string path;
    const std::string status = snap.request_snapshot_creation(path);

    if (status == "empty") {
        throw rpc_error{-32000, "no state", "node head is 0: nothing to snapshot",
                        "load a snapshot or wait for the first block"};
    }
    if (status == "unavailable") {
        throw rpc_error{-32004, "snapshot unavailable", "snapshot plugin is not initialized", ""};
    }

    fc::mutable_variant_object m;
    m("status", status);
    m("path", path);
    if (status == "exists") {
        m("note", "a snapshot for the current head block already exists; nothing was written");
    } else if (status == "busy") {
        m("note", "another snapshot creation or restore is running; retry when it finishes");
    } else {
        m("note", "snapshot creation runs on the node's snapshot thread; poll "
                  "admin.snapshot.status until in_progress is false");
    }
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_snapshot_restore(const fc::variant &params) {
    if (!params.is_object() && !params.is_null()) {
        throw rpc_error{-32602, "invalid params", "expected an object {name|path|latest}", ""};
    }
    const fc::variant_object empty;
    const fc::variant_object &o = params.is_object() ? params.get_object() : empty;

    const std::string name = trim(param_str(o, "name", ""));
    const std::string given_path = trim(param_str(o, "path", ""));
    bool latest = true;
    auto l_it = o.find("latest");
    if (l_it != o.end() && !l_it->value().is_null()) {
        if (!l_it->value().is_bool()) {
            throw rpc_error{-32602, "invalid params", "'latest' must be a boolean", ""};
        }
        latest = l_it->value().as_bool();
    }

    auto &snap = snapshot_started();
    const bfs::path dir(snap.snapshot_directory());
    std::string target;

    if (!given_path.empty()) {
        const bfs::path p(given_path);
        boost::system::error_code ec;
        if (!bfs::exists(p, ec) || ec || !bfs::is_regular_file(p, ec) || ec) {
            throw rpc_error{-32602, "snapshot not found", given_path, "see admin.snapshot.status"};
        }
        // An absolute path is accepted only if it sits directly in the snapshot directory:
        // the admin API must not import arbitrary files from the node's filesystem.
        try {
            const bfs::path canon_p = bfs::canonical(p);
            const bfs::path canon_dir = bfs::canonical(dir);
            if (canon_p.parent_path() != canon_dir) {
                throw rpc_error{-32602, "path outside snapshot directory", given_path,
                                "snapshot files live in " + dir.string()};
            }
            target = canon_p.string();
        } catch (const rpc_error &) {
            throw;
        } catch (const std::exception &e) {
            throw rpc_error{-32602, "cannot resolve snapshot path", given_path, e.what()};
        }
    } else if (!name.empty()) {
        if (!is_plain_snapshot_name(name)) {
            throw rpc_error{-32602, "invalid snapshot name", name,
                            "expected a file name ending in .vizjson (see admin.snapshot.status)"};
        }
        target = (dir / name).string();
        boost::system::error_code ec;
        if (!bfs::exists(target, ec) || ec) {
            throw rpc_error{-32602, "snapshot not found", name, "see admin.snapshot.status"};
        }
    } else if (!latest) {
        throw rpc_error{-32602, "invalid params", "provide 'name'/'path' or set 'latest': true", ""};
    }

    std::string error;
    if (!snap.restore_snapshot_now(target, error)) {
        throw rpc_error{-32000, "restore not started", error, "see admin.snapshot.status"};
    }

    const std::string resolved = target.empty() ? snap.latest_snapshot_path() : target;
    int64_t block = 0;
    for (const auto &f : snap.list_snapshots()) {
        if (f.path == resolved) { block = static_cast<int64_t>(f.block_num); break; }
    }

    fc::mutable_variant_object m;
    m("status", "started");
    m("snapshot", resolved);
    m("snapshot_name", resolved.empty() ? std::string() : bfs::path(resolved).filename().string());
    m("block_num", block);
    m("process_restarted", false);
    m("note", "in-process restore: P2P is paused, shared memory is wiped, the snapshot is "
              "imported, dlt_block_log is replayed and P2P resumes. The OS process and its "
              "uptime are unchanged. Progress: admin.snapshot.status (restoring).");
    return fc::variant(m);
}

// ── webserver operator methods ───────────────────────────────────────────────────────────
// The webserver plugin keeps its limits and IP lists in live state: every change below is
// visible to the very next request, without a restart and without re-reading config.ini.
// config.ini seeds the state at startup; changes are persisted to the plugin's admin state
// file (<data-dir>/webserver-admin.json by default) so they survive a restart.
namespace ws = graphene::plugins::webserver;

graphene::plugins::webserver::webserver_plugin &node_admin_plugin::impl::webserver_started() const {
    auto *plugin = appbase::app().find_plugin<ws::webserver_plugin>();
    if (plugin == nullptr || plugin->get_state() != appbase::abstract_plugin::started) {
        throw rpc_error{-32004, "webserver unavailable",
                        "the webserver plugin is not loaded or not started",
                        "webserver administration needs 'plugin = webserver' in config.ini"};
    }
    return *plugin;
}

fc::variant node_admin_plugin::impl::h_webserver_status(const fc::variant &) {
    const ws::webserver_admin_state st = webserver_started().admin_state(0);

    fc::mutable_variant_object limits;
    limits("enabled", st.limits.enabled);
    limits("per_minute", st.limits.per_minute);
    limits("per_hour", st.limits.per_hour);
    limits("per_day", st.limits.per_day);

    fc::variants wl, bl;
    for (const auto &ip : st.whitelist) wl.push_back(fc::variant(ip));
    for (const auto &ip : st.blacklist) bl.push_back(fc::variant(ip));

    fc::mutable_variant_object m;
    m("as_of", now_iso());
    m("stats_enabled", st.stats_enabled);
    m("limits", limits);
    m("total_requests", static_cast<int64_t>(st.total_requests));
    m("blocked_requests", static_cast<int64_t>(st.blocked_requests));
    m("limited_requests", static_cast<int64_t>(st.limited_requests));
    m("tracked_ips", static_cast<int64_t>(st.tracked_ips));
    m("whitelist", wl);
    m("blacklist", bl);
    m("note", "limits are off unless stats are enabled; the blacklist is enforced even "
              "when stats are off. Changes apply to the next request.");
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_webserver_ips(const fc::variant &params) {
    const fc::variant_object empty;
    const fc::variant_object &o = params.is_object() ? params.get_object() : empty;
    const int64_t limit = std::max<int64_t>(1, std::min<int64_t>(param_int(o, "limit", 200), 5000));

    const ws::webserver_admin_state st =
        webserver_started().admin_state(static_cast<std::size_t>(limit));

    fc::variants ips;
    for (const auto &s : st.ips) {
        fc::mutable_variant_object r;
        r("ip", s.ip);
        r("total", static_cast<int64_t>(s.total));
        r("minute", static_cast<int64_t>(s.minute));
        r("hour", static_cast<int64_t>(s.hour));
        r("day", static_cast<int64_t>(s.day));
        r("blocked", static_cast<int64_t>(s.blocked));
        r("limited", static_cast<int64_t>(s.limited));
        r("last_seen", static_cast<int64_t>(s.last_seen));
        r("last_seen_iso", s.last_seen ? fc::variant(unixtime_iso(s.last_seen)) : fc::variant());
        r("whitelisted", s.whitelisted);
        r("blacklisted", s.blacklisted);
        ips.push_back(fc::variant(r));
    }

    fc::mutable_variant_object m;
    m("as_of", now_iso());
    m("tracked_ips", static_cast<int64_t>(st.tracked_ips));
    m("returned", static_cast<int64_t>(ips.size()));
    m("ips", ips);
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_webserver_limits(const fc::variant &params) {
    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected an object",
                        "{enabled, per_minute, per_hour, per_day}; omitted fields keep their value"};
    }
    auto &plugin = webserver_started();
    const ws::webserver_admin_state st = plugin.admin_state(0);
    const fc::variant_object &o = params.get_object();

    const bool enabled = o.contains("enabled") ? o["enabled"].as_bool() : st.limits.enabled;
    const uint32_t per_minute = static_cast<uint32_t>(
        std::max<int64_t>(1, param_int(o, "per_minute", st.limits.per_minute)));
    const uint32_t per_hour = static_cast<uint32_t>(
        std::max<int64_t>(1, param_int(o, "per_hour", st.limits.per_hour)));
    const uint32_t per_day = static_cast<uint32_t>(
        std::max<int64_t>(1, param_int(o, "per_day", st.limits.per_day)));

    std::string error;
    if (!plugin.set_limits(enabled, per_minute, per_hour, per_day, error)) {
        throw rpc_error{-32000, "limits not applied", error, ""};
    }

    const ws::webserver_admin_state after = plugin.admin_state(0);
    fc::mutable_variant_object limits;
    limits("enabled", after.limits.enabled);
    limits("per_minute", after.limits.per_minute);
    limits("per_hour", after.limits.per_hour);
    limits("per_day", after.limits.per_day);

    fc::mutable_variant_object m;
    m("status", "applied");
    m("limits", limits);
    m("stats_enabled", after.stats_enabled);
    m("note", "applied to the live webserver on the next request and persisted to its "
              "admin state file; config.ini is unchanged.");
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_webserver_stats(const fc::variant &params) {
    if (!params.is_object() || !params.get_object().contains("enabled")) {
        throw rpc_error{-32602, "invalid params", "expected an object {enabled: bool}", ""};
    }
    auto &plugin = webserver_started();
    const bool enabled = params.get_object()["enabled"].as_bool();
    plugin.set_stats_enabled(enabled);

    const ws::webserver_admin_state after = plugin.admin_state(0);
    fc::mutable_variant_object m;
    m("status", enabled ? "enabled" : "disabled");
    m("stats_enabled", after.stats_enabled);
    m("limits_enabled", after.limits.enabled);
    m("note", "applied to the live webserver immediately and persisted; limits enforcement "
              "is controlled separately by admin.webserver.limits.");
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_webserver_list_add(const fc::variant &params, bool whitelist) {
    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected an object {ip}", ""};
    }
    const std::string ip = trim(param_str(params.get_object(), "ip", ""));
    if (ip.empty()) {
        throw rpc_error{-32602, "invalid params", "missing 'ip'", "a single IPv4 or IPv6 address"};
    }
    auto &plugin = webserver_started();
    const bool ok = whitelist ? plugin.whitelist_add(ip) : plugin.blacklist_add(ip);
    if (!ok) {
        throw rpc_error{-32602, "invalid ip", ip, "a single IPv4 or IPv6 address"};
    }

    const ws::webserver_admin_state st = plugin.admin_state(0);
    fc::variants wl, bl;
    for (const auto &v : st.whitelist) wl.push_back(fc::variant(v));
    for (const auto &v : st.blacklist) bl.push_back(fc::variant(v));

    fc::mutable_variant_object m;
    m("status", "added");
    m("list", whitelist ? "whitelist" : "blacklist");
    m("ip", ip);
    m("whitelist", wl);
    m("blacklist", bl);
    m("note", whitelist
        ? "whitelisted addresses bypass the rate limits (they are still counted)"
        : "blacklisted addresses are rejected with HTTP 403; the blacklist wins over the whitelist");
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_webserver_list_remove(const fc::variant &params, bool whitelist) {
    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected an object {ip}", ""};
    }
    const std::string ip = trim(param_str(params.get_object(), "ip", ""));
    if (ip.empty()) {
        throw rpc_error{-32602, "invalid params", "missing 'ip'", ""};
    }
    auto &plugin = webserver_started();
    const bool erased = whitelist ? plugin.whitelist_remove(ip) : plugin.blacklist_remove(ip);

    const ws::webserver_admin_state st = plugin.admin_state(0);
    fc::variants wl, bl;
    for (const auto &v : st.whitelist) wl.push_back(fc::variant(v));
    for (const auto &v : st.blacklist) bl.push_back(fc::variant(v));

    fc::mutable_variant_object m;
    m("status", erased ? "removed" : "not_found");
    m("list", whitelist ? "whitelist" : "blacklist");
    m("ip", ip);
    m("whitelist", wl);
    m("blacklist", bl);
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_webserver_reset(const fc::variant &) {
    auto &plugin = webserver_started();
    plugin.reset_stats();
    fc::mutable_variant_object m;
    m("status", "reset");
    m("note", "counters zeroed; IP lists and limits are unchanged");
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_webserver_reload(const fc::variant &) {
    auto &plugin = webserver_started();
    std::string error;
    if (!plugin.reload_admin_state(error)) {
        throw rpc_error{-32000, "state not reloaded", error,
                        "fix the admin state file or change values via admin.webserver.limits"};
    }
    return h_webserver_status(fc::variant(fc::mutable_variant_object()));
}


// ── config/plugin editor: plan/apply ─────────────────────────────────────────────────────
void node_admin_plugin::impl::store_plan(const pending_plan &p) {
    std::lock_guard<std::mutex> lock(plans_mutex);
    const uint32_t now = now_unixtime();
    for (auto it = plans.begin(); it != plans.end();) {
        if (it->second.expires <= now) it = plans.erase(it);
        else ++it;
    }
    // Plans are tiny, but an authenticated caller must not be able to grow this map without
    // bound; 64 outstanding plans is far more than any single operator needs.
    if (plans.size() >= 64) {
        auto oldest = plans.begin();
        for (auto it = plans.begin(); it != plans.end(); ++it) {
            if (it->second.created < oldest->second.created) oldest = it;
        }
        plans.erase(oldest);
    }
    plans[p.id] = p;
}

node_admin_plugin::impl::pending_plan node_admin_plugin::impl::take_plan(
    const std::string &id, const token_info &token, const std::string &kind) {
    if (id.empty()) {
        throw rpc_error{-32602, "invalid params", "missing plan_id",
                        "create a plan first: admin.config.plan, admin.plugins.plan or "
                        "admin.validator.plan"};
    }
    std::lock_guard<std::mutex> lock(plans_mutex);
    auto it = plans.find(id);
    if (it == plans.end()) {
        throw rpc_error{-32004, "unknown plan", id,
                        "plans are single-use and expire; create a new one"};
    }
    if (it->second.kind != kind) {
        throw rpc_error{-32602, "invalid params",
                        "plan kind is '" + it->second.kind + "', not '" + kind + "'", ""};
    }
    if (it->second.label != token.label) {
        throw rpc_error{-32002, "forbidden", "plan belongs to another token",
                        "a plan is bound to the token that created it"};
    }
    if (it->second.expires <= now_unixtime()) {
        plans.erase(it);
        throw rpc_error{-32004, "plan expired", id, "create a new plan"};
    }
    pending_plan p = it->second;
    // Single-use: consumed even when the write below fails, so a plan cannot be replayed.
    plans.erase(it);
    return p;
}

fc::variant node_admin_plugin::impl::h_config_plan(const fc::variant &params,
                                                   const token_info &token) {
    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected {set: {key: value}}", ""};
    }
    const fc::variant_object &o = params.get_object();
    auto it = o.find("set");
    if (it == o.end() || !it->value().is_object() || it->value().get_object().size() == 0) {
        throw rpc_error{-32602, "invalid params",
                        "'set' must be a non-empty object of key: value",
                        "only allow-listed keys are accepted; see admin.config.get"};
    }

    std::map<std::string, std::string> set;
    for (const auto &kv : it->value().get_object()) {
        std::string raw;
        if (kv.value().is_string()) raw = kv.value().as_string();
        else if (kv.value().is_bool()) raw = kv.value().as_bool() ? "true" : "false";
        else if (kv.value().is_int64()) raw = std::to_string(kv.value().as_int64());
        else if (kv.value().is_uint64()) raw = std::to_string(kv.value().as_uint64());
        else {
            throw rpc_error{-32602, "invalid params",
                            "value for '" + kv.key() + "' must be a string, number or boolean",
                            ""};
        }
        std::string norm;
        std::string why;
        if (!normalize_config_value(kv.key(), raw, norm, why)) {
            throw rpc_error{-32602, "invalid params", why,
                            "the admin API edits only allow-listed keys"};
        }
        set[kv.key()] = norm;
    }

    std::string text;
    std::string why;
    if (!read_text_file(config_path(), text, why)) {
        throw rpc_error{-32004, "config unavailable", why, "check the data directory"};
    }
    std::vector<std::string> lines = split_lines(text);
    fc::variants diff;
    for (const auto &kv : set) {
        std::string old;
        const bool found = allowed_config_value(lines, kv.first, old);
        if (found && old == kv.second) continue;
        line_edit e = set_allowed_config_value(lines, kv.first, kv.second);
        if (!e.error.empty()) throw rpc_error{-32602, "invalid params", e.error, ""};
        lines = std::move(e.lines);
        fc::mutable_variant_object d;
        d("key", kv.first);
        d("from", found ? fc::variant(old) : fc::variant());
        d("to", kv.second);
        diff.push_back(fc::variant(d));
    }
    if (diff.empty()) {
        throw rpc_error{-32602, "invalid params", "no changes: every value already matches", ""};
    }

    pending_plan p;
    p.id = random_plan_id();
    p.label = token.label;
    p.kind = "config";
    p.created = now_unixtime();
    p.expires = p.created + plan_ttl_sec;
    p.base_sha256 = text_sha256(text);
    p.set = std::move(set);
    p.diff = fc::variant(diff);
    store_plan(p);

    fc::mutable_variant_object m;
    m("plan_id", p.id);
    m("kind", "config");
    m("config_path", config_path());
    m("diff", diff);
    m("needs_restart", true);
    m("expires_at", unixtime_iso(p.expires));
    m("hint", "nothing was written yet; apply with admin.config.apply {plan_id}. The plan is "
              "single-use, bound to this token, and refused if config.ini changes meanwhile.");
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_config_apply(const fc::variant &params,
                                                    const token_info &token) {
    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected {plan_id}", ""};
    }
    const pending_plan p =
        take_plan(trim(param_str(params.get_object(), "plan_id", "")), token, "config");

    std::lock_guard<std::mutex> write_lock(config_write_mutex);
    std::string text;
    std::string why;
    if (!read_text_file(config_path(), text, why)) {
        throw rpc_error{-32004, "config unavailable", why, "check the data directory"};
    }
    if (text_sha256(text) != p.base_sha256) {
        throw rpc_error{-32000, "config changed",
                        "config.ini was modified after this plan was made; nothing was written",
                        "create a new plan with admin.config.plan"};
    }

    std::vector<std::string> lines = split_lines(text);
    for (const auto &kv : p.set) {
        std::string norm;
        std::string why2;
        if (!normalize_config_value(kv.first, kv.second, norm, why2)) {
            throw rpc_error{-32602, "invalid params", why2, ""};
        }
        line_edit e = set_allowed_config_value(lines, kv.first, norm);
        if (!e.error.empty()) throw rpc_error{-32602, "invalid params", e.error, ""};
        lines = std::move(e.lines);
    }

    std::string backup;
    std::string werr;
    if (!write_config_atomic(config_path(), join_lines(lines, true), backup, werr)) {
        throw rpc_error{-32000, "write failed", werr,
                        "the original config.ini was left untouched"};
    }

    fc::mutable_variant_object m;
    m("applied", true);
    m("kind", "config");
    m("config_path", config_path());
    m("backup", backup);
    m("diff", p.diff);
    m("needs_restart", true);
    m("restart_hint",
      "config.ini is read at startup: call admin.restart {confirm:\"restart\"} once the "
      "process is supervised (docker --restart unless-stopped or systemd Restart=always).");
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_config_backups(const fc::variant &) {
    const bfs::path cfg(config_path());
    const std::string prefix = cfg.filename().string() + ".bak-";
    struct backup_row {
        std::time_t mtime;
        std::string name;
        uintmax_t size;
    };
    std::vector<backup_row> rows;
    boost::system::error_code ec;
    bfs::directory_iterator it(cfg.parent_path(), ec);
    const bfs::directory_iterator end;
    if (ec) {
        throw rpc_error{-32004, "config unavailable",
                        "cannot list " + cfg.parent_path().string() + ": " + ec.message(), ""};
    }
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const std::string name = it->path().filename().string();
        if (name.size() <= prefix.size() || name.compare(0, prefix.size(), prefix) != 0) continue;
        const uintmax_t size = bfs::file_size(it->path(), ec);
        if (ec) continue;
        rows.push_back(backup_row{bfs::last_write_time(it->path(), ec), name, size});
    }
    std::sort(rows.begin(), rows.end(), [](const backup_row &a, const backup_row &b) {
        if (a.mtime != b.mtime) return a.mtime > b.mtime;
        return a.name > b.name;
    });

    fc::variants files;
    for (const backup_row &r : rows) {
        fc::mutable_variant_object f;
        f("name", r.name);
        f("path", (cfg.parent_path() / r.name).string());
        f("bytes", static_cast<int64_t>(r.size));
        f("modified", iso_utc(fc::time_point_sec(static_cast<uint32_t>(r.mtime))));
        files.push_back(fc::variant(f));
    }

    fc::mutable_variant_object m;
    m("config_path", config_path());
    m("count", static_cast<int64_t>(files.size()));
    m("backups", files);
    m("hint", "each apply writes one backup before replacing config.ini; copy one back and "
              "restart to roll back by hand");
    return fc::variant(m);
}

/// Read an array of strings from params, ignoring an absent or null key.
static void read_string_list(const fc::variant_object &o, const char *key,
                             std::vector<std::string> &out) {
    auto it = o.find(key);
    if (it == o.end() || it->value().is_null()) return;
    if (!it->value().is_array()) {
        throw rpc_error{-32602, "invalid params", std::string("'") + key + "' must be an array",
                        ""};
    }
    for (const fc::variant &v : it->value().get_array()) {
        if (!v.is_string()) {
            throw rpc_error{-32602, "invalid params",
                            std::string("'") + key + "' must contain plugin name strings", ""};
        }
        const std::string name = trim(v.as_string());
        if (!name.empty()) out.push_back(name);
    }
}

fc::variant node_admin_plugin::impl::h_plugins_plan(const fc::variant &params,
                                                    const token_info &token) {
    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected {add: [], remove: []}", ""};
    }
    const fc::variant_object &o = params.get_object();
    std::vector<std::string> add;
    std::vector<std::string> remove;
    read_string_list(o, "add", add);
    read_string_list(o, "remove", remove);
    if (add.empty() && remove.empty()) {
        throw rpc_error{-32602, "invalid params", "nothing to do: add and remove are empty",
                        "example: {\"add\":[\"block_info\"],\"remove\":[]}"};
    }

    std::string text;
    std::string why;
    if (!read_text_file(config_path(), text, why)) {
        throw rpc_error{-32004, "config unavailable", why, "check the data directory"};
    }
    const line_edit e = edit_plugin_lines(split_lines(text), add, remove);
    if (!e.error.empty()) throw rpc_error{-32602, "invalid params", e.error, ""};

    fc::variants diff;
    for (const std::string &name : add) {
        fc::mutable_variant_object d;
        d("op", "add");
        d("name", name);
        diff.push_back(fc::variant(d));
    }
    for (const std::string &name : remove) {
        fc::mutable_variant_object d;
        d("op", "remove");
        d("name", name);
        diff.push_back(fc::variant(d));
    }

    pending_plan p;
    p.id = random_plan_id();
    p.label = token.label;
    p.kind = "plugins";
    p.created = now_unixtime();
    p.expires = p.created + plan_ttl_sec;
    p.base_sha256 = text_sha256(text);
    p.add = std::move(add);
    p.remove = std::move(remove);
    p.diff = fc::variant(diff);
    store_plan(p);

    fc::mutable_variant_object m;
    m("plan_id", p.id);
    m("kind", "plugins");
    m("config_path", config_path());
    m("diff", diff);
    m("needs_restart", true);
    m("expires_at", unixtime_iso(p.expires));
    m("hint", "nothing was written yet; apply with admin.plugins.apply {plan_id}. The plan is "
              "single-use, bound to this token, and refused if config.ini changes meanwhile.");
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_plugins_apply(const fc::variant &params,
                                                     const token_info &token) {
    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected {plan_id}", ""};
    }
    const pending_plan p =
        take_plan(trim(param_str(params.get_object(), "plan_id", "")), token, "plugins");

    std::lock_guard<std::mutex> write_lock(config_write_mutex);
    std::string text;
    std::string why;
    if (!read_text_file(config_path(), text, why)) {
        throw rpc_error{-32004, "config unavailable", why, "check the data directory"};
    }
    if (text_sha256(text) != p.base_sha256) {
        throw rpc_error{-32000, "config changed",
                        "config.ini was modified after this plan was made; nothing was written",
                        "create a new plan with admin.plugins.plan"};
    }

    const line_edit e = edit_plugin_lines(split_lines(text), p.add, p.remove);
    if (!e.error.empty()) throw rpc_error{-32602, "invalid params", e.error, ""};

    std::string backup;
    std::string werr;
    if (!write_config_atomic(config_path(), join_lines(e.lines, true), backup, werr)) {
        throw rpc_error{-32000, "write failed", werr,
                        "the original config.ini was left untouched"};
    }

    fc::mutable_variant_object m;
    m("applied", true);
    m("kind", "plugins");
    m("config_path", config_path());
    m("backup", backup);
    m("diff", p.diff);
    m("needs_restart", true);
    m("restart_hint",
      "plugin changes are applied by a restart: call admin.restart {confirm:\"restart\"} once "
      "the process is supervised (docker --restart unless-stopped or systemd Restart=always).");
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_plan_cancel(const fc::variant &params,
                                                   const token_info &token) {
    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected {plan_id}", ""};
    }
    const std::string id = trim(param_str(params.get_object(), "plan_id", ""));
    if (id.empty()) throw rpc_error{-32602, "invalid params", "missing plan_id", ""};

    std::lock_guard<std::mutex> lock(plans_mutex);
    auto it = plans.find(id);
    if (it == plans.end()) {
        throw rpc_error{-32004, "unknown plan", id, "plans are single-use and expire"};
    }
    if (it->second.label != token.label) {
        throw rpc_error{-32002, "forbidden", "plan belongs to another token", ""};
    }
    plans.erase(it);

    fc::mutable_variant_object m;
    m("cancelled", true);
    m("plan_id", id);
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_restart(const fc::variant &params) {
    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected {confirm: \"restart\"}", ""};
    }
    const std::string confirm = trim(param_str(params.get_object(), "confirm", ""));
    if (confirm != "restart") {
        throw rpc_error{-32602, "invalid params",
                        "confirm must be exactly the word 'restart'",
                        "this stops the node process; a supervisor has to bring it back"};
    }
    if (restart_requested.exchange(true)) {
        throw rpc_error{-32000, "restart already requested",
                        "the process is already shutting down", ""};
    }

    // Reply first, quit second: the handler runs on the listener thread and appbase::quit()
    // stops the io_service, so the short delay lets this HTTP response reach the client.
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        ilog("node_admin: restart requested through the admin API; asking appbase to quit");
        appbase::app().quit();
    }).detach();

    fc::mutable_variant_object m;
    m("ok", true);
    m("restarting", true);
    m("exit_code", 0);
    m("hint", "the process exits cleanly right after this reply. Bring-up belongs to the "
              "supervisor: docker --restart unless-stopped or systemd Restart=always. Without "
              "one, the node stays down.");
    return fc::variant(m);
}

// ── shared-memory objects (chainbase) ───────────────────────────────────────────────────
objects_state node_admin_plugin::impl::collect_objects() const {
    objects_state s;
    s.state_dir = chain.get_state_dir();
    boost::system::error_code ec;
    const bfs::path dir(s.state_dir);
    s.state_dir_exists = !s.state_dir.empty() && bfs::exists(dir, ec) && bfs::is_directory(dir, ec);

    // The chain plugin opens the database with <data-dir>/state as its data dir, so block_log
    // and schema_version live NEXT TO shared_memory.bin, not in the application data dir.
    // Reading the app data dir reported both as absent on every healthy node, and the judge
    // then called that state "corrupted" and offered a resync.
    const bfs::path chain_state = bfs::path(data_dir()) / "state";

    // The block log is a file in graphene, but the chain plugin wipes it with remove_all, so
    // both shapes are read: a directory here would silently report "no block log" otherwise.
    const bfs::path block_log = chain_state / "block_log";
    s.block_log_present = bfs::exists(block_log, ec);
    if (s.block_log_present) {
        if (bfs::is_regular_file(block_log, ec)) {
            s.block_log_bytes = bfs::file_size(block_log, ec);
        } else if (bfs::is_directory(block_log, ec)) {
            for (bfs::directory_iterator it(block_log), end; it != end; ++it) {
                if (bfs::is_regular_file(it->path(), ec)) s.block_log_bytes += bfs::file_size(it->path(), ec);
            }
        }
    }

    if (s.state_dir_exists) {
        const bfs::path shm = dir / "shared_memory.bin";
        s.shm_exists = bfs::exists(shm, ec);
        if (s.shm_exists) s.shm_bytes = bfs::file_size(shm, ec);

        // Files that a successful startup or a successful resize removes. Finding one while
        // the node is running means an operation was interrupted, which is exactly the state
        // a repair has to clear.
        for (const char *marker : {"undo_all_in_progress", "resize_in_progress",
                                   "force_resync", "force_replay"}) {
            if (bfs::exists(dir / marker, ec)) s.markers.push_back(marker);
        }
    }

    s.schema_file_present = read_uint_file(chain_state / "schema_version", s.stored_schema);

    graphene::chain::database &db = chain.db();
    s.recovering = chain.is_recovering();
    try {
        // Sizes are read under the same weak read lock the chain API uses: without it the
        // multi-index containers would be walked while the block thread mutates them.
        db.with_weak_read_lock([&]() {
            s.revision = static_cast<int64_t>(db.revision());
            s.head_block_num = db.head_block_num();
            s.mapped_bytes = static_cast<uint64_t>(db.max_memory());
            s.reserved_bytes = static_cast<uint64_t>(db.reserved_memory());
            s.free_bytes = static_cast<uint64_t>(db.segment_manager()->get_free_memory());
            for (auto it = db.index_list_begin(); it != db.index_list_end(); ++it) {
                chainbase::abstract_index *idx = *it;
                const int64_t n = static_cast<int64_t>(idx->size());
                const std::string type = idx->name();
                objects_check::index_row row;
                row.type = type;
                row.type_id = static_cast<int64_t>(idx->type_id());
                row.objects = n;
                row.revision = static_cast<int64_t>(idx->revision());
                s.indexes.push_back(row);
                s.total_objects += n;
                if (ends_with_text(type, "::dynamic_global_property_object")) s.dgp_objects = n;
                else if (ends_with_text(type, "::validator_schedule_object")) s.schedule_objects = n;
                else if (ends_with_text(type, "::account_object")) s.account_objects = n;
            }
        });
    } catch (const std::exception &e) {
        s.lock_error = e.what();
    }

    std::sort(s.indexes.begin(), s.indexes.end(),
              [](const objects_check::index_row &a, const objects_check::index_row &b) {
                  if (a.objects != b.objects) return a.objects > b.objects;
                  return a.type < b.type;
              });
    return s;
}

namespace {
std::string state_dir_marker_path(const objects_state &s, const char *name) {
    return (bfs::path(s.state_dir) / name).string();
}
} // namespace

fc::variant node_admin_plugin::impl::h_objects_list(const fc::variant &) {
    const objects_state s = collect_objects();

    fc::variants indexes;
    for (const auto &row : s.indexes) {
        fc::mutable_variant_object e;
        e("type", row.type);
        e("type_id", row.type_id);
        e("objects", row.objects);
        e("revision", row.revision);
        indexes.push_back(fc::variant(e));
    }

    fc::mutable_variant_object schema;
    schema("file_present", s.schema_file_present);
    schema("stored", static_cast<int64_t>(s.stored_schema));
    schema("compiled", static_cast<int64_t>(CHAIN_SCHEMA_VERSION));
    schema("match", s.schema_file_present && s.stored_schema == CHAIN_SCHEMA_VERSION);

    fc::mutable_variant_object block_log;
    block_log("path", (bfs::path(data_dir()) / "state" / "block_log").string());
    block_log("present", s.block_log_present);
    block_log("bytes", static_cast<int64_t>(s.block_log_bytes));

    fc::mutable_variant_object m;
    m("state_dir", s.state_dir);
    m("state_dir_exists", s.state_dir_exists);
    m("shared_memory_path", (bfs::path(s.state_dir) / "shared_memory.bin").string());
    m("shared_memory_exists", s.shm_exists);
    m("shared_memory_bytes", static_cast<int64_t>(s.shm_bytes));
    m("mapped_bytes", static_cast<int64_t>(s.mapped_bytes));
    m("free_bytes", static_cast<int64_t>(s.free_bytes));
    m("used_bytes", static_cast<int64_t>(s.mapped_bytes > s.free_bytes ? s.mapped_bytes - s.free_bytes : 0));
    m("reserved_bytes", static_cast<int64_t>(s.reserved_bytes));
    m("revision", s.revision);
    m("head_block_num", static_cast<int64_t>(s.head_block_num));
    m("recovering", s.recovering);
    m("schema", schema);
    m("block_log", block_log);
    fc::variants markers;
    for (const std::string &x : s.markers) markers.push_back(fc::variant(x));
    m("markers", markers);
    m("index_count", static_cast<int64_t>(s.indexes.size()));
    m("total_objects", s.total_objects);
    m("indexes", indexes);
    m("generated_at", now_iso());
    // Always present: a client asks "did the lock answer?" and must be able to tell null from
    // a missing field without knowing this server's omissions.
    m("lock_error", s.lock_error.empty() ? fc::variant() : fc::variant(s.lock_error));
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_objects_check(const fc::variant &) {
    const objects_state s = collect_objects();
    const objects_check::judgement j =
        objects_check::judge(s, static_cast<uint32_t>(CHAIN_SCHEMA_VERSION));

    fc::variants findings;
    for (const objects_check::finding &f : j.findings) {
        fc::mutable_variant_object e;
        e("severity", f.severity);
        e("code", f.code);
        e("text", f.text);
        findings.push_back(fc::variant(e));
    }

    fc::mutable_variant_object m;
    m("verdict", j.verdict);
    m("critical", static_cast<int64_t>(j.critical));
    m("warnings", static_cast<int64_t>(j.warnings));
    m("findings", findings);
    m("recommended_repair", j.recommended_repair.empty() ? fc::variant() : fc::variant(j.recommended_repair));
    m("replay_available", j.replay_available);
    m("state_dir", s.state_dir);
    m("checked_at", now_iso());
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_objects_repair(const fc::variant &params) {
    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected {mode: \"replay\"|\"resync\", confirm: \"repair\"}", ""};
    }
    const std::string mode = trim(param_str(params.get_object(), "mode", "replay"));
    if (mode != "replay" && mode != "resync") {
        throw rpc_error{-32602, "invalid params", "mode must be 'replay' or 'resync'", ""};
    }
    const std::string confirm = trim(param_str(params.get_object(), "confirm", ""));
    if (confirm != "repair") {
        throw rpc_error{-32602, "invalid params",
                        "confirm must be exactly the word 'repair'",
                        "repair rewrites node state; the whole point is that it cannot happen by accident"};
    }

    const objects_state s = collect_objects();
    if (!s.state_dir_exists) {
        throw rpc_error{-32000, "state dir missing",
                        "shared-memory directory does not exist: " + s.state_dir,
                        "start the node once so it creates its state directory"};
    }
    if (mode == "replay" && !objects_check::has_replayable_blocks(s)) {
        throw rpc_error{-32000, "no block log",
                        "replay rebuilds state from the block log, and the block log is absent or empty",
                        "use mode=resync: it wipes state and blocks and rebuilds from the network"};
    }

    // Startup precedence is resync > replay: the chain plugin removes a force_replay marker
    // when force_resync is also present. Honouring the mode the operator confirmed therefore
    // means clearing the marker that would otherwise outrank it, and naming it in the reply —
    // otherwise "replay (keeps blocks)" would silently drop the block log. The chain plugin is
    // the other producer of force_resync (the p2p wedge watchdog), so this case is reachable.
    const char *opposite = mode == "replay" ? "force_resync" : "force_replay";
    const std::string opposite_path = state_dir_marker_path(s, opposite);
    boost::system::error_code rm_ec;
    const bool removed_opposite = bfs::remove(opposite_path, rm_ec);
    if (rm_ec) {
        throw rpc_error{-32000, "cannot clear conflicting marker",
                        opposite_path + ": " + rm_ec.message(),
                        "the opposite marker outranks the requested mode at startup; "
                        "repair refused rather than promising an effect it cannot deliver"};
    }

    if (restart_requested.exchange(true)) {
        throw rpc_error{-32000, "restart already requested", "the process is already shutting down", ""};
    }

    // The chain plugin reads these markers at startup, before it maps shared memory:
    //   force_replay -> wipe state, keep the block log, reindex it from block 1
    //   force_resync -> wipe state and block log, come up empty and resync from peers
    // Writing them now and quitting is safer than acting on a live mapping: the file is
    // closed by an ordinary shutdown, so the repair starts from a consistent file handle.
    const char *marker = mode == "replay" ? "force_replay" : "force_resync";
    const std::string marker_path = state_dir_marker_path(s, marker);
    {
        std::ofstream f(marker_path.c_str(), std::ios::trunc);
        f << "node_admin repair request at " << now_iso() << " mode=" << mode << "\n";
        f.flush();
        if (!f) {
            restart_requested.store(false);
            throw rpc_error{-32000, "cannot write repair marker", marker_path,
                            "check the state directory permissions"};
        }
    }

    std::thread([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        ilog("node_admin: repair requested through the admin API; asking appbase to quit");
        appbase::app().quit();
    }).detach();

    fc::mutable_variant_object m;
    m("ok", true);
    m("restarting", true);
    m("mode", mode);
    m("marker", marker_path);
    m("removed_marker", removed_opposite ? fc::variant(opposite_path) : fc::variant());
    // The effect text must match the startup the chain plugin will actually take; see
    // objects_check::repair_effect (unit-tested) for the snapshot-mode case.
    m("effect", objects_check::repair_effect(mode, snapshot_replay_from, snapshot_configured));
    m("snapshot_replay", snapshot_replay_from);
    m("hint", "the process exits cleanly right after this reply. Bring-up belongs to the "
              "supervisor: docker --restart unless-stopped or systemd Restart=always.");
    return fc::variant(m);
}

// ── validator chain operations (validator_guard) ─────────────────────────────────────────
// Decision q#3051=D: node_admin does not keep its own key. The guard already holds
// signing+active keys per validator, signs validator_update inside its own plugin, and
// exposes a plain status/action API here. `chain` is granted only while the guard is
// live with an active key, which is what the dispatch check above enforces.

graphene::plugins::validator_guard::validator_guard_plugin *
node_admin_plugin::impl::guard_plugin() const {
    return appbase::app().find_plugin<graphene::plugins::validator_guard::validator_guard_plugin>();
}

bool node_admin_plugin::impl::guard_ready() const {
    auto *g = guard_plugin();
    return g != nullptr &&
           g->get_state() == appbase::abstract_plugin::started &&
           g->chain_actions_available();
}

fc::variant node_admin_plugin::impl::h_validator_list(const fc::variant &) {
    auto *g = guard_plugin();
    const bool loaded = g != nullptr;
    const bool started = loaded && g->get_state() == appbase::abstract_plugin::started;
    const bool available = loaded && started && g->chain_actions_available();

    fc::mutable_variant_object ginfo;
    ginfo("loaded", loaded);
    ginfo("started", started);
    ginfo("available", available);
    if (!loaded) {
        ginfo("reason", "validator_guard is not loaded; add `plugin = validator_guard` to config.ini");
    } else if (!started) {
        ginfo("reason", "validator_guard has not finished starting");
    } else if (!available) {
        ginfo("reason", "no validator is configured with an active key "
                        "(validator-guard-validator = [name,signing_wif,active_wif])");
    }

    fc::variants arr;
    if (available) {
        for (const auto &s : g->validators_status()) {
            fc::mutable_variant_object m;
            m("name", s.name);
            m("on_chain", s.on_chain);
            m("enabled", s.enabled);
            m("hold", s.hold);
            m("active_authority", s.active_authority);
            m("scheduled_in", s.scheduled_in > 0 ? fc::variant(static_cast<int64_t>(s.scheduled_in))
                                                 : fc::variant());
            m("signing_key", s.signing_key.empty() ? fc::variant() : fc::variant(s.signing_key));
            m("configured_key", s.configured_key.empty() ? fc::variant()
                                                         : fc::variant(s.configured_key));
            arr.push_back(fc::variant(m));
        }
    }

    fc::mutable_variant_object m;
    m("guard", ginfo);
    m("validators", arr);
    return fc::variant(m);
}

// Who is scheduled in the current validator schedule, and whether the chain is in emergency
// consensus. Names only: signing keys live in the validator plugin and are shown by
// admin.validator.list (and only for the validators this node actually runs).
fc::variant node_admin_plugin::impl::h_validators_active(const fc::variant &) {
    const graphene::chain::database &db = chain.db();
    const auto &wso = db.get_validator_schedule_object();
    const auto &dgp = db.get_dynamic_global_properties();

    fc::variants names;
    std::set<std::string> seen;
    for (const auto &n : wso.current_shuffled_validators) {
        const std::string name = n;
        if (name.empty() || !seen.insert(name).second) continue;
        names.push_back(fc::variant(name));
    }

    bool validator_loaded = false;
    if (auto *vp = appbase::app().find_plugin<graphene::plugins::validator_plugin::validator_plugin>()) {
        validator_loaded = vp->get_state() == appbase::abstract_plugin::started;
    }

    fc::mutable_variant_object m;
    m("as_of", now_iso());
    m("head_block_num", static_cast<int64_t>(db.head_block_num()));
    m("count", static_cast<int64_t>(names.size()));
    m("num_scheduled", static_cast<int64_t>(wso.num_scheduled_validators));
    m("validators", names);
    m("emergency_consensus_active", dgp.emergency_consensus_active);
    m("emergency_consensus_start_block",
      static_cast<int64_t>(dgp.emergency_consensus_active ? dgp.emergency_consensus_start_block : 0));
    m("validator_plugin_loaded", validator_loaded);
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_validator_plan(const fc::variant &params,
                                                      const token_info &token) {
    namespace vg = graphene::plugins::validator_guard;

    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected {actions: [{name, action}]}", ""};
    }
    const fc::variant_object &o = params.get_object();
    auto it = o.find("actions");
    if (it == o.end() || !it->value().is_array() || it->value().get_array().empty()) {
        throw rpc_error{-32602, "invalid params", "'actions' must be a non-empty array",
                        "example: {\"actions\":[{\"name\":\"val1\",\"action\":\"disable\"}]}"};
    }

    auto *g = guard_plugin();
    std::map<std::string, vg::validator_status> by_name;
    for (const auto &s : g->validators_status()) by_name[s.name] = s;

    fc::variants actions;
    fc::variants diff;
    std::set<std::string> seen;
    std::size_t tx_count = 0;

    for (const fc::variant &a : it->value().get_array()) {
        if (!a.is_object()) {
            throw rpc_error{-32602, "invalid params", "each action must be an object", ""};
        }
        const fc::variant_object &ao = a.get_object();
        const std::string name = trim(param_str(ao, "name", ""));
        if (name.empty()) {
            throw rpc_error{-32602, "invalid params", "each action needs a 'name'", ""};
        }
        if (!seen.insert(name).second) {
            throw rpc_error{-32602, "invalid params",
                            "validator '" + name + "' appears twice in one plan", ""};
        }
        auto sit = by_name.find(name);
        if (sit == by_name.end()) {
            throw rpc_error{-32602, "invalid params",
                            "validator '" + name + "' is not configured in validator-guard",
                            "admin.validator.list shows the configured validators"};
        }
        if (!sit->second.on_chain) {
            throw rpc_error{-32602, "invalid params",
                            "validator '" + name + "' does not exist on chain", ""};
        }

        const std::string act = trim(param_str(ao, "action", ""));
        vg::validator_action kind;
        if (act == "enable") kind = vg::validator_action::enable;
        else if (act == "disable") kind = vg::validator_action::disable;
        else if (act == "set_key") kind = vg::validator_action::set_key;
        else {
            throw rpc_error{-32602, "invalid params",
                            "action must be 'enable', 'disable' or 'set_key', got '" + act + "'", ""};
        }

        std::string key;
        if (kind == vg::validator_action::set_key) {
            key = trim(param_str(ao, "signing_key", ""));
            if (key.empty()) {
                throw rpc_error{-32602, "invalid params", "set_key requires 'signing_key'", ""};
            }
            try {
                graphene::protocol::public_key_type parsed(key);
            } catch (const fc::exception &e) {
                throw rpc_error{-32602, "invalid params",
                                "invalid signing_key: " + e.to_string(),
                                "expected a base58 public key, e.g. VIZ8..."};
            }
        }

        const std::string from = sit->second.signing_key;
        std::string to;
        if (kind == vg::validator_action::enable) to = sit->second.configured_key;
        else if (kind == vg::validator_action::set_key) to = key;

        const bool hold_to = (kind == vg::validator_action::disable);
        const bool tx_needed = (from != to);
        const bool hold_change = (sit->second.hold != hold_to);
        if (!tx_needed && !hold_change) continue; // already in the requested state

        fc::mutable_variant_object d;
        d("name", name);
        d("action", act);
        d("from", from.empty() ? fc::variant() : fc::variant(from));
        d("to", to.empty() ? fc::variant() : fc::variant(to));
        d("hold_from", sit->second.hold);
        d("hold_to", hold_to);
        d("tx_needed", tx_needed);
        diff.push_back(fc::variant(d));

        fc::mutable_variant_object sa;
        sa("name", name);
        sa("action", act);
        sa("signing_key", key);
        sa("expected_key", from);
        sa("hold_from", sit->second.hold);
        actions.push_back(fc::variant(sa));
        if (tx_needed) ++tx_count;
    }

    if (diff.empty()) {
        throw rpc_error{-32602, "invalid params",
                        "no changes: every validator is already in the requested state", ""};
    }

    pending_plan p;
    p.id = random_plan_id();
    p.label = token.label;
    p.kind = "validators";
    p.created = now_unixtime();
    p.expires = p.created + plan_ttl_sec;
    p.actions = std::move(actions);
    p.diff = fc::variant(diff);
    store_plan(p);

    fc::mutable_variant_object m;
    m("plan_id", p.id);
    m("kind", "validators");
    m("diff", diff);
    m("transactions", static_cast<int64_t>(tx_count));
    m("expires_at", unixtime_iso(p.expires));
    m("hint", "nothing was signed or broadcast yet; apply with admin.validator.apply {plan_id}. "
              "The plan is single-use, bound to this token, and each action is refused if the "
              "on-chain signing key changed after this plan was made.");
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_validator_apply(const fc::variant &params,
                                                       const token_info &token) {
    namespace vg = graphene::plugins::validator_guard;

    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected {plan_id}", ""};
    }
    const pending_plan p =
        take_plan(trim(param_str(params.get_object(), "plan_id", "")), token, "validators");

    auto *g = guard_plugin();
    if (g == nullptr || !g->chain_actions_available()) {
        throw rpc_error{-32002, "forbidden", "validator-guard is no longer active",
                        "the plan was consumed; configure the guard and create a new plan"};
    }

    std::vector<vg::validator_action_request> reqs;
    reqs.reserve(p.actions.size());
    for (const fc::variant &sv : p.actions) {
        const fc::variant_object &so = sv.get_object();
        vg::validator_action_request r;
        r.name = param_str(so, "name", "");
        const std::string act = param_str(so, "action", "");
        if (act == "enable") r.action = vg::validator_action::enable;
        else if (act == "disable") r.action = vg::validator_action::disable;
        else r.action = vg::validator_action::set_key;
        r.signing_key = param_str(so, "signing_key", "");
        r.check_expected = true;
        r.expected_key = param_str(so, "expected_key", "");
        reqs.push_back(r);
    }

    const std::vector<vg::validator_action_result> res = g->apply_validator_actions(reqs);

    fc::variants out;
    int64_t applied = 0;
    int64_t unchanged = 0;
    int64_t failed = 0;
    for (const auto &r : res) {
        fc::mutable_variant_object m;
        m("name", r.name);
        m("action", r.action);
        if (!r.ok) {
            m("status", "failed");
            m("error", r.error);
            ++failed;
        } else if (r.changed) {
            m("status", "applied");
            m("tx_id", r.tx_id);
            ++applied;
        } else {
            m("status", "already");
            ++unchanged;
        }
        if (r.ok) m("hold", r.action == "disable");
        out.push_back(fc::variant(m));
    }

    fc::mutable_variant_object m;
    m("plan_id", p.id);
    m("kind", "validators");
    m("applied", applied);
    m("unchanged", unchanged);
    m("failed", failed);
    m("results", out);
    m("hint", "signed transactions are in this node's pending pool and were broadcast to peers; "
              "no restart is needed. A disable sets a persisted operator hold, so auto-restore "
              "will not turn the validator back on after a restart either.");
    return fc::variant(m);
}

// ── MCP (Streamable HTTP) ────────────────────────────────────────────────────────────────
// The MCP endpoint is the same loopback listener and the same bearer tokens as admin.*.
// Tools call the admin methods 1:1, so every tool invocation lands in the audit log with
// its own method name (`tools/call` is the JSON-RPC method; the tool name is in params).
fc::variant node_admin_plugin::impl::mcp_tools() const {
    auto schema = [](fc::variants props, fc::variants required) {
        fc::mutable_variant_object s;
        s("type", "object");
        s("properties", props);
        s("additionalProperties", false);
        if (!required.empty()) s("required", required);
        return fc::variant(s);
    };
    auto prop = [](const char *type, const char *desc) {
        fc::mutable_variant_object p;
        p("type", type);
        p("description", desc);
        return fc::variant(p);
    };
    auto tool = [&](const char *name, const char *desc, const fc::variant &input) {
        fc::mutable_variant_object t;
        t("name", name);
        t("description", desc);
        t("inputSchema", input);
        return fc::variant(t);
    };
    const fc::variant no_args = schema(fc::variants{}, fc::variants{});

    fc::variants logs_props;
    logs_props.push_back(prop("string", "Name from admin-log-file entries"));
    logs_props.push_back(prop("integer", "Maximum bytes to read from the tail (default 16384)"));
    logs_props.push_back(prop("integer", "Maximum lines to return (default 200)"));
    fc::variants logs_required;
    logs_required.push_back(fc::variant(std::string("name")));

    fc::variants audit_props;
    audit_props.push_back(prop("integer", "Maximum audit lines to return (default 100)"));

    fc::variants tools;
    tools.push_back(tool("admin.ping", "Liveness check: server time and ok=true.", no_args));
    tools.push_back(tool("admin.auth.me",
                         "Who the presented token is: its label and scopes. The token and its "
                         "hash are never returned. Clients use it to hide actions they cannot run.",
                         no_args));
    tools.push_back(tool("admin.node_info",
                         "Chain id, head block, hardfork, uptime, head age and emergency "
                         "consensus state.", no_args));
    tools.push_back(tool("admin.diagnostics",
                         "Node info plus data dir, peer count, disk/RSS/loadavg and plugin counters.",
                         no_args));
    tools.push_back(tool("admin.plugins.list",
                         "Plugins configured in config.ini with observed lifecycle state and the "
                         "name of the method that reports each plugin's own status.", no_args));

    fc::variants plugin_name_props;
    plugin_name_props.push_back(prop("string", "Plugin name exactly as written in config.ini, e.g. p2p"));
    fc::variants plugin_name_required;
    plugin_name_required.push_back(fc::variant(std::string("name")));
    tools.push_back(tool("admin.plugins.status",
                         "Lifecycle state plus the plugin's own status for p2p, webserver, "
                         "snapshot and validator_guard. For other plugins it says why no "
                         "status exists instead of returning an empty object.",
                         schema(plugin_name_props, plugin_name_required)));

    tools.push_back(tool("admin.objects.list",
                         "Chainbase object types with row counts per index, the mapped file "
                         "size, free space, revision vs head and state-repair markers.", no_args));
    tools.push_back(tool("admin.objects.check",
                         "Same view as admin.objects.list, judged: verdict ok/warn/corrupted/"
                         "unreadable, named findings, and the repair mode that would fix them.",
                         no_args));

    fc::variants repair_props;
    repair_props.push_back(prop("string", "'replay' rebuilds state from the block log; "
                                          "'resync' wipes state and blocks and syncs from peers"));
    repair_props.push_back(prop("string", "Must be exactly 'repair'"));
    fc::variants repair_required;
    repair_required.push_back(fc::variant(std::string("mode")));
    repair_required.push_back(fc::variant(std::string("confirm")));
    tools.push_back(tool("admin.objects.repair",
                         "Write the repair marker and restart: replay keeps the block log, "
                         "resync drops it. Needs the 'process' scope; the supervisor must "
                         "bring the node back.",
                         schema(repair_props, repair_required)));
    tools.push_back(tool("admin.config.get",
                         "Effective config.ini by sections with secret values masked.", no_args));
    tools.push_back(tool("admin.logs.list",
                         "Log files the admin API may tail, as configured by admin-log-file, "
                         "with size and whether the file exists yet.", no_args));
    tools.push_back(tool("admin.logs.tail",
                         "Tail an allow-listed log file; secrets and WIFs are masked.",
                         schema(logs_props, logs_required)));
    tools.push_back(tool("admin.audit.tail",
                         "Tail the node_admin audit log (one JSON line per request outcome).",
                         schema(audit_props, fc::variants{})));

    fc::variants endpoint_props;
    endpoint_props.push_back(prop("string", "Peer endpoint as IPv4:port, e.g. 1.2.3.4:1776"));
    fc::variants endpoint_required;
    endpoint_required.push_back(fc::variant(std::string("endpoint")));

    tools.push_back(tool("admin.p2p.status",
                         "P2P diagnostics, the same facts the node logs every minute and five "
                         "minutes: head and LIB, the DLT block range held locally, lag behind "
                         "the best peer, uptime, connection counts, and every peer with its "
                         "lifecycle, head, fork status, trust and ban state.",
                         no_args));
    tools.push_back(tool("admin.p2p.add_node",
                         "Add a P2P peer endpoint and dial it now. Needs the 'p2p' scope.",
                         schema(endpoint_props, endpoint_required)));
    tools.push_back(tool("admin.p2p.add_trusted",
                         "Add a P2P peer as trusted and dial it now: never soft-banned, kept "
                         "through known-peer GC. Needs the 'p2p' scope.",
                         schema(endpoint_props, endpoint_required)));
    tools.push_back(tool("admin.p2p.clear_banned",
                         "Clear every soft ban and retry those peers immediately. "
                         "Needs the 'p2p' scope.",
                         no_args));

    fc::variants restore_props;
    restore_props.push_back(prop("string",
        "Snapshot file name from admin.snapshot.status, e.g. snapshot-block-84360000.vizjson"));
    restore_props.push_back(prop("string",
        "Absolute path of a snapshot file located directly in the snapshot directory"));
    restore_props.push_back(prop("boolean",
        "Restore the highest-block snapshot in the snapshot directory (default when "
        "neither name nor path is given)"));

    fc::variants snap_props;
    snap_props.push_back(prop("integer", "Skip this many snapshots from the newest (default 0)"));
    snap_props.push_back(prop("integer", "Return at most this many snapshots (default 20, max 200)"));
    snap_props.push_back(prop("string",
        "Name of the last row you already have; the page continues after it. Use this instead "
        "of offset when paging through a directory that changes: offset repeats or skips rows "
        "when a snapshot is written or pruned between pages. A pruned anchor is reported as "
        "after_found=false and offset applies."));

    tools.push_back(tool("admin.snapshot.status",
                         "A page of snapshots on disk (newest block first) with size and age, the "
                         "latest file, and whether a creation or restore is currently running. "
                         "offset/limit page through the list; snapshot_total and has_more say "
                         "how much is left. after=<last name you have> is the stable cursor.",
                         schema(snap_props, fc::variants{})));
    tools.push_back(tool("admin.snapshot.create",
                         "Create a snapshot of the current head block in the snapshot directory, "
                         "asynchronously. Needs the 'snapshot' scope.",
                         no_args));
    tools.push_back(tool("admin.snapshot.restore",
                         "Restore node state from a snapshot file, or from the latest one, "
                         "in-process: pauses P2P, wipes shared memory, imports the snapshot, "
                         "replays dlt_block_log and resumes P2P. Needs the 'snapshot' scope.",
                         schema(restore_props, fc::variants{})));

    fc::variants ws_ips_props;
    ws_ips_props.push_back(prop("integer", "Maximum rows to return (default 200, max 5000)"));
    fc::variants ws_limits_props;
    ws_limits_props.push_back(prop("boolean", "Enforce the limits (default: keep the current value)"));
    ws_limits_props.push_back(prop("integer", "Max requests per IP per minute"));
    ws_limits_props.push_back(prop("integer", "Max requests per IP per hour"));
    ws_limits_props.push_back(prop("integer", "Max requests per IP per day"));
    fc::variants ws_stats_props;
    ws_stats_props.push_back(prop("boolean", "Enable or disable per-IP request accounting"));
    fc::variants ws_stats_required;
    ws_stats_required.push_back(fc::variant(std::string("enabled")));
    fc::variants ws_ip_props;
    ws_ip_props.push_back(prop("string", "A single IPv4 or IPv6 address"));
    fc::variants ws_ip_required;
    ws_ip_required.push_back(fc::variant(std::string("ip")));

    tools.push_back(tool("admin.webserver.status",
                         "Public webserver policy: stats switch, limits, request totals and "
                         "the whitelist/blacklist. Changes made by the other webserver tools "
                         "apply to the next request.",
                         no_args));
    tools.push_back(tool("admin.webserver.ips",
                         "Per-IP request counts (total, current minute/hour/day, blocked and "
                         "rate-limited), highest first.",
                         schema(ws_ips_props, fc::variants{})));
    tools.push_back(tool("admin.webserver.limits",
                         "Set the per-IP request limits and whether they are enforced. Omitted "
                         "fields keep their value. Needs the 'webserver' scope.",
                         schema(ws_limits_props, fc::variants{})));
    tools.push_back(tool("admin.webserver.stats",
                         "Turn per-IP request accounting on or off at runtime. Needs the "
                         "'webserver' scope.",
                         schema(ws_stats_props, ws_stats_required)));
    tools.push_back(tool("admin.webserver.whitelist_add",
                         "Whitelist an IP: it bypasses the rate limits (still counted). "
                         "Needs the 'webserver' scope.",
                         schema(ws_ip_props, ws_ip_required)));
    tools.push_back(tool("admin.webserver.whitelist_remove",
                         "Remove an IP from the whitelist. Needs the 'webserver' scope.",
                         schema(ws_ip_props, ws_ip_required)));
    tools.push_back(tool("admin.webserver.blacklist_add",
                         "Blacklist an IP: every request from it is rejected with HTTP 403. "
                         "Needs the 'webserver' scope.",
                         schema(ws_ip_props, ws_ip_required)));
    tools.push_back(tool("admin.webserver.blacklist_remove",
                         "Remove an IP from the blacklist. Needs the 'webserver' scope.",
                         schema(ws_ip_props, ws_ip_required)));
    tools.push_back(tool("admin.webserver.reset",
                         "Zero the request counters; lists and limits are kept. Needs the "
                         "'webserver' scope.",
                         no_args));
    tools.push_back(tool("admin.webserver.reload",
                         "Re-read the webserver admin state file (limits and IP lists) from "
                         "disk without restarting. Needs the 'webserver' scope.",
                         no_args));

    fc::mutable_variant_object set_prop;
    set_prop("type", "object");
    set_prop("description",
             "Allow-listed config keys mapped to new values (string, number or boolean). "
             "Identity, keys, data paths and listener endpoints are never editable.");
    set_prop("additionalProperties", true);
    fc::variants cfg_plan_props;
    cfg_plan_props.push_back(fc::variant(set_prop));
    fc::variants cfg_set_required;
    cfg_set_required.push_back(fc::variant(std::string("set")));
    fc::variants plan_id_props;
    plan_id_props.push_back(prop("string", "plan_id returned by the matching *.plan call"));
    fc::variants plan_id_required;
    plan_id_required.push_back(fc::variant(std::string("plan_id")));
    fc::variants list_props;
    list_props.push_back(prop("array", "Plugin names to add (must be registered in vizd)"));
    list_props.push_back(prop("array", "Plugin names to remove (chain and node_admin are protected)"));
    fc::variants confirm_props;
    confirm_props.push_back(prop("string", "Must be exactly 'restart'"));
    fc::variants confirm_required;
    confirm_required.push_back(fc::variant(std::string("confirm")));

    tools.push_back(tool("admin.config.plan",
                         "Validate an allow-listed config.ini change and return the exact diff "
                         "plus a single-use plan_id. Nothing is written. Needs the 'config' scope.",
                         schema(cfg_plan_props, cfg_set_required)));
    tools.push_back(tool("admin.config.apply",
                         "Write the change shown by admin.config.plan: re-checks the file hash, "
                         "backs the old file up and replaces config.ini atomically. Needs the "
                         "'config' scope.",
                         schema(plan_id_props, plan_id_required)));
    tools.push_back(tool("admin.config.backups",
                         "Backups written next to config.ini by previous applies, newest first.",
                         no_args));
    tools.push_back(tool("admin.plugins.plan",
                         "Validate adding/removing `plugin =` lines and return a single-use "
                         "plan_id with the exact edit. Nothing is written until apply. Needs the "
                         "'plugins' scope.",
                         schema(list_props, fc::variants{})));
    tools.push_back(tool("admin.plugins.apply",
                         "Write the plugin edit shown by admin.plugins.plan, with the same "
                         "hash check, backup and atomic replace as config.apply. Needs the "
                         "'plugins' scope.",
                         schema(plan_id_props, plan_id_required)));
    tools.push_back(tool("admin.plan.cancel",
                         "Drop a plan without applying it. Only the token that created it can "
                         "cancel it.",
                         schema(plan_id_props, plan_id_required)));
    tools.push_back(tool("admin.restart",
                         "Ask appbase to quit so the supervisor (docker --restart / systemd "
                         "Restart=always) starts the node again. Needs the 'process' scope.",
                         schema(confirm_props, confirm_required)));

    fc::variants validator_props;
    validator_props.push_back(prop("array",
        "Actions: [{name, action: 'enable'|'disable'|'set_key', signing_key?}]. One entry "
        "per validator; duplicates are refused."));
    fc::variants validator_required;
    validator_required.push_back(fc::variant(std::string("actions")));

    tools.push_back(tool("admin.validators.active",
                         "Names in the current validator schedule, the number of scheduled "
                         "slots and whether the chain is in emergency consensus. No keys.",
                         no_args));
    tools.push_back(tool("admin.validator.list",
                         "Validators monitored by validator_guard: on-chain signing key, "
                         "whether the operator hold is set, the guard's active authority and "
                         "the next scheduled slot.",
                         no_args));
    tools.push_back(tool("admin.validator.plan",
                         "Validate enable/disable/set_key for several validators and return "
                         "the exact diff plus a single-use plan_id. Nothing is signed yet. "
                         "Needs the 'chain' scope and a live validator-guard with an active key.",
                         schema(validator_props, validator_required)));
    tools.push_back(tool("admin.validator.apply",
                         "Sign and broadcast the planned validator_update transactions "
                         "through validator_guard, refusing any validator whose on-chain key "
                         "changed since the plan. Needs the 'chain' scope.",
                         schema(plan_id_props, plan_id_required)));
    return fc::variant(tools);
}

fc::variant node_admin_plugin::impl::h_mcp_initialize(const fc::variant &params) {
    std::string requested;
    if (params.is_object()) {
        requested = param_str(params.get_object(), "protocolVersion", "");
    }
    // Echo only versions we actually implement; otherwise answer with our own, as the spec
    // requires ("the server MUST respond with a protocol version it supports").
    const std::set<std::string> supported{"2024-11-05", "2025-03-26", "2025-06-18"};
    const std::string proto =
        supported.count(requested) ? requested : std::string("2025-06-18");

    fc::mutable_variant_object tools_cap;
    tools_cap("listChanged", false);
    fc::mutable_variant_object caps;
    caps("tools", tools_cap);

    fc::mutable_variant_object server;
    server("name", "viz-node-admin");
    server("version", "0.1.0");

    fc::mutable_variant_object m;
    m("protocolVersion", proto);
    m("capabilities", caps);
    m("serverInfo", server);
    m("instructions",
      "Administration of this VIZ node. Read-only tools need the 'read' scope; the mutating "
      "peer tools admin.p2p.add_node, admin.p2p.add_trusted and admin.p2p.clear_banned "
      "additionally need the 'p2p' scope; admin.snapshot.create and admin.snapshot.restore "
      "additionally need the 'snapshot' scope; the webserver policy tools "
      "(admin.webserver.limits / stats / whitelist_* / blacklist_* / reset / reload) "
      "additionally need the 'webserver' scope; admin.config.plan/apply need 'config', "
      "admin.plugins.plan/apply need 'plugins', and admin.restart needs 'process'. Validator "
      "operations (admin.validator.plan/apply) need 'chain' and are accepted only while "
      "validator_guard is live with an active key; disable sets a persisted operator hold, so "
      "the guard's auto-restore does not undo it (enable clears the hold). Config and "
      "plugin edits are two-phase: plan shows the exact diff and returns a single-use, "
      "token-bound plan_id; apply re-checks that config.ini did not change, writes it "
      "atomically and keeps a .bak copy. Both need a restart to take effect. Webserver changes "
      "apply to the next request and persist in the plugin's admin state file. A human-facing "
      "UI is served at /ui/ on this same listener and uses the same bearer token.");
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_mcp_ping(const fc::variant &) {
    return fc::variant(fc::mutable_variant_object());
}

fc::variant node_admin_plugin::impl::h_mcp_tools_list(const fc::variant &) {
    fc::mutable_variant_object m;
    m("tools", mcp_tools());
    return fc::variant(m);
}

fc::variant node_admin_plugin::impl::h_mcp_tools_call(const fc::variant &params,
                                                      const token_info &token) {
    if (!params.is_object()) {
        throw rpc_error{-32602, "invalid params", "expected an object {name, arguments}", ""};
    }
    const fc::variant_object &o = params.get_object();
    const std::string name = param_str(o, "name", "");
    if (name.empty()) {
        throw rpc_error{-32602, "invalid params", "missing tool name", "see tools/list"};
    }
    const fc::variant tools = mcp_tools();
    bool known = false;
    for (const fc::variant &t : tools.get_array()) {
        if (t.get_object()["name"].as_string() == name) {
            known = true;
            break;
        }
    }
    if (!known) {
        throw rpc_error{-32602, "unknown tool", name, "see tools/list"};
    }

    fc::variant args;
    auto a_it = o.find("arguments");
    if (a_it != o.end() && !a_it->value().is_null()) {
        if (!a_it->value().is_object()) {
            throw rpc_error{-32602, "invalid params", "'arguments' must be an object", ""};
        }
        args = a_it->value();
    }

    auto content = [](const std::string &text, bool is_error) {
        fc::mutable_variant_object item;
        item("type", "text");
        item("text", text);
        fc::mutable_variant_object m;
        m("content", fc::variants{fc::variant(item)});
        m("isError", is_error);
        return fc::variant(m);
    };

    try {
        return content(to_json(dispatch(name, args, token)), false);
    } catch (const rpc_error &e) {
        // Tool execution errors are reported inside the result (isError), not as a
        // protocol-level JSON-RPC error: the model must see what the tool said.
        std::string text = "error " + std::to_string(e.code) + ": " + e.message;
        if (!e.reason.empty()) text += ": " + e.reason;
        if (!e.hint.empty()) text += " (" + e.hint + ")";
        return content(text, true);
    } catch (const fc::exception &e) {
        return content("internal error: " + e.to_string(), true);
    } catch (const std::exception &e) {
        return content(std::string("internal error: ") + e.what(), true);
    }
}


// ── audit ────────────────────────────────────────────────────────────────────────────────
void node_admin_plugin::impl::audit(const std::string &rid, const std::string &label,
                                    const std::string &method, const fc::variant &params,
                                    const std::string &outcome, int code, int64_t duration_ms,
                                    const std::string &remote) {
    try {
        fc::mutable_variant_object m;
        m("ts", now_iso());
        m("rid", rid);
        m("token", label.empty() ? "-" : label);
        m("method", method);
        if (!params.is_null()) m("params", params);
        m("outcome", outcome);
        m("code", static_cast<int64_t>(code));
        m("ms", duration_ms);
        m("remote", remote.empty() ? "-" : remote);
        audit_line(mask_text(to_json(fc::variant(m))));
    } catch (...) {
        // Auditing must never break the request path.
    }
}

void node_admin_plugin::impl::audit_line(const std::string &line) {
    std::lock_guard<std::mutex> lock(audit_mutex);
#ifndef _WIN32
    // rotate at a fixed size instead of growing without bound
    struct stat st;
    if (::stat(audit_path.c_str(), &st) == 0 && st.st_size > static_cast<off_t>(kAuditRotateBytes)) {
        ::rename(audit_path.c_str(), (audit_path + ".1").c_str());
    }
    const int fd = ::open(audit_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;
    const std::string data = line + "\n";
    ssize_t ignored = ::write(fd, data.data(), data.size());
    (void) ignored;
    ::close(fd);
#else
    std::ofstream out(audit_path.c_str(), std::ios::app);
    if (out) out << line << "\n";
#endif
}

} // namespace node_admin
} // namespace plugins
} // namespace graphene
