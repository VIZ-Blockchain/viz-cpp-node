#include <graphene/plugins/node_admin/node_admin_plugin.hpp>

#include <graphene/plugins/p2p/p2p_plugin.hpp>
#include <graphene/plugins/snapshot/plugin.hpp>
#include <graphene/chain/database.hpp>
#include <graphene/chain/global_property_object.hpp>
#include <graphene/protocol/version.hpp>

#include <appbase/application.hpp>

#include <fc/crypto/sha256.hpp>
#include <fc/io/json.hpp>
#include <fc/log/logger.hpp>
#include <fc/time.hpp>

#include <boost/asio.hpp>
#include <boost/filesystem.hpp>

#include <websocketpp/config/asio.hpp>
#include <websocketpp/server.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
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

// appbase exposes only a typed find_plugin<T>(); the name-based lookup is protected and the
// plugin registry has no public enumeration. So the config list is the visible population, and
// the state below is genuinely observed only for the plugins whose type this TU links.
const appbase::abstract_plugin *observe_plugin(const std::string &name) {
    if (name == "chain") return appbase::app().find_plugin<graphene::plugins::chain::plugin>();
    if (name == "p2p") return appbase::app().find_plugin<graphene::plugins::p2p::p2p_plugin>();
    if (name == "snapshot") return appbase::app().find_plugin<graphene::plugins::snapshot::snapshot_plugin>();
    if (name == node_admin_plugin::name()) return appbase::app().find_plugin<node_admin_plugin>();
    return nullptr;
}

// ── limits ──────────────────────────────────────────────────────────────────────────────────
const std::size_t kMaxBodyBytes = 64 * 1024;
const std::size_t kMaxLogBytes = 64 * 1024;
const std::size_t kMaxLines = 2000;
const std::size_t kAuditRotateBytes = 16 * 1024 * 1024;
const uint32_t kRatePerSecond = 20;
// Snapshot files are served in one HTTP response, so the body has to fit in memory.
// VIZ mainnet snapshots are ~4 MB compressed; the cap is for a pathological file, not
// the normal case. Larger files stay downloadable with scp from the node.
const uint64_t kMaxSnapshotDownloadBytes = 512ULL * 1024 * 1024;

// ── small string helpers ────────────────────────────────────────────────────────────────────
std::string trim(const std::string &s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string to_lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

/// A snapshot file name as accepted from HTTP paths and JSON params: one path component,
/// no traversal, and a snapshot extension. Names come from list_snapshots(), so anything
/// else is a client mistake (or an attempt to reach outside the snapshot directory).
bool is_plain_snapshot_name(const std::string &name) {
    if (name.empty() || name.size() > 255) return false;
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
    if (name == "." || name == "..") return false;
    const std::string lower = to_lower(name);
    auto ends_with = [&](const char *suffix) {
        const std::size_t n = std::strlen(suffix);
        return lower.size() > n && lower.compare(lower.size() - n, n, suffix) == 0;
    };
    return ends_with(".vizjson") || ends_with(".json");
}

std::vector<std::string> split(const std::string &s, char sep) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (true) {
        const std::size_t next = s.find(sep, pos);
        if (next == std::string::npos) {
            out.push_back(s.substr(pos));
            return out;
        }
        out.push_back(s.substr(pos, next - pos));
        pos = next + 1;
    }
}

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

// ── secret masking ──────────────────────────────────────────────────────────────────────────
bool sensitive_key(const std::string &key) {
    const std::string k = to_lower(key);
    static const char *needles[] = {
        "private-key", "private_key", "password", "secret", "token", "wif", "emergency"
    };
    for (const char *n : needles) {
        if (k.find(n) != std::string::npos) return true;
    }
    return false;
}

/// Redact key material from free text (log tail). Deliberately coarse: a false positive
/// (masking an innocent word) is harmless; a false negative is a leaked WIF.
std::string mask_text(const std::string &text) {
    static const std::regex wif(
        "(^|[^A-Za-z0-9])(5[HJK][1-9A-HJ-NP-Za-km-z]{49}|[KL][1-9A-HJ-NP-Za-km-z]{51})([^A-Za-z0-9]|$)",
        std::regex::ECMAScript);
    static const std::regex inline_secret(
        "((private-key|emergency-private-key|admin-token|password|secret)[[:space:]]*[=:][[:space:]]*)([^[:space:]\\\"]+)",
        std::regex::ECMAScript | std::regex::icase);
    std::string out = std::regex_replace(text, wif, "$1***$3");
    out = std::regex_replace(out, inline_secret, "$1***");
    return out;
}

// ── config file ────────────────────────────────────────────────────────────────────────────
struct cfg_entry {
    std::string section;
    std::string key;
    std::string value;
};

std::vector<cfg_entry> parse_config_file(const std::string &path, bool *exists = nullptr) {
    std::vector<cfg_entry> entries;
    std::ifstream in(path.c_str());
    if (!in) {
        if (exists) *exists = false;
        return entries;
    }
    if (exists) *exists = true;

    std::string section;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';') continue;
        if (t[0] == '[' && t.back() == ']') {
            section = trim(t.substr(1, t.size() - 2));
            continue;
        }
        const std::size_t eq = t.find('=');
        if (eq == std::string::npos) {
            entries.push_back(cfg_entry{section, trim(t), ""});
        } else {
            entries.push_back(cfg_entry{section, trim(t.substr(0, eq)), trim(t.substr(eq + 1))});
        }
    }
    return entries;
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
  header { display:flex; flex-wrap:wrap; gap:12px; align-items:center; padding:14px 18px;
           border-bottom:1px solid var(--line); background:var(--panel); }
  h1 { font-size:16px; margin:0 12px 0 0; font-weight:600; }
  h2 { font-size:13px; text-transform:uppercase; letter-spacing:.08em; color:var(--dim);
       margin:0 0 8px; font-weight:600; }
  main { padding:18px; display:grid; gap:18px; max-width:1200px; }
  input[type=password], input[type=text] { background:#0d1117; color:var(--fg);
       border:1px solid var(--line); border-radius:6px; padding:7px 9px; font:inherit; }
  button { background:#21262d; color:var(--fg); border:1px solid var(--line); border-radius:6px;
           padding:7px 12px; font:inherit; cursor:pointer; }
  button:hover { border-color:var(--acc); }
  button.danger:hover { border-color:var(--bad); }
  button:disabled { opacity:.45; cursor:default; }
  .cards { display:grid; grid-template-columns:repeat(auto-fit,minmax(170px,1fr)); gap:10px; }
  .card { background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:10px 12px; }
  .card .k { color:var(--dim); font-size:11px; text-transform:uppercase; letter-spacing:.06em; }
  .card .v { font-size:19px; margin-top:2px; }
  .card .s { color:var(--dim); font-size:11px; margin-top:2px; }
  .ok { color:var(--ok); } .warn { color:var(--warn); } .bad { color:var(--bad); }
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
  pre#log { margin:0; background:var(--panel); border:1px solid var(--line); border-radius:8px;
            padding:10px 12px; min-height:60px; max-height:220px; overflow:auto; color:var(--dim); }
</style>
</head>
<body>
<header>
  <h1>VIZ node admin</h1>
  <input id="token" type="password" placeholder="bearer token" autocomplete="off" size="38">
  <button id="save">Save token</button>
  <span id="state" class="dim">no token</span>
</header>
<main>
  <section>
    <h2>Node</h2>
    <div class="cards" id="cards"></div>
  </section>

  <section>
    <h2>Peers</h2>
    <div class="tablewrap">
      <table>
        <thead><tr><th>Endpoint</th><th>State</th><th>Dir</th><th>Head</th><th>Lag</th>
          <th>Peer</th><th>Flags</th><th>Since</th><th>Note</th></tr></thead>
        <tbody id="peers"></tbody>
      </table>
    </div>
  </section>

  <section>
    <h2>Snapshots</h2>
    <div class="cards" id="snapCards"></div>
    <div class="actions" style="margin:10px 0">
      <button id="snapCreate">Create snapshot</button>
      <button id="snapRestoreLatest" class="danger">Restore latest</button>
      <span class="dim">restore wipes shared memory and re-imports the chosen snapshot in-process</span>
    </div>
    <div class="tablewrap">
      <table>
        <thead><tr><th>Block</th><th>File</th><th>Size</th><th>Age</th><th>Actions</th></tr></thead>
        <tbody id="snapshots"></tbody>
      </table>
    </div>
  </section>

  <section>
    <h2>Actions</h2>
    <div class="actions">
      <input id="endpoint" type="text" placeholder="IP:port, e.g. 1.2.3.4:1776" autocomplete="off">
      <button id="add">Add node</button>
      <button id="addTrusted">Add as trusted</button>
      <button id="clearBanned" class="danger">Clear banned list</button>
    </div>
  </section>

  <section>
    <h2>Log</h2>
    <pre id="log"></pre>
  </section>
</main>
<script>
(function () {
  'use strict';
  var $ = function (id) { return document.getElementById(id); };
  var token = sessionStorage.getItem('vizAdminToken') || '';
  var timer = null;

  function say(msg, cls) {
    var el = $('log');
    var t = new Date().toISOString().replace('T', ' ').replace('Z', 'Z');
    var line = document.createElement('div');
    line.textContent = t + '  ' + msg;
    if (cls) line.className = cls;
    el.appendChild(line);
    el.scrollTop = el.scrollHeight;
  }

  function setState(msg, cls) {
    var el = $('state');
    el.textContent = msg;
    el.className = cls || 'dim';
  }

  function rpc(method, params) {
    if (!token) return Promise.reject(new Error('no token'));
    return fetch('/', {
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
    v.textContent = value;
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

  function renderPeers(peers) {
    var body = $('peers');
    body.textContent = '';
    if (!peers || !peers.length) {
      var tr = document.createElement('tr');
      var td = document.createElement('td');
      td.colSpan = 9; td.className = 'dim'; td.textContent = 'no peers connected or known';
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

  function sizeText(bytes) {
    bytes = Number(bytes) || 0;
    if (bytes >= 1073741824) return (bytes / 1073741824).toFixed(2) + ' GB';
    if (bytes >= 1048576) return (bytes / 1048576).toFixed(1) + ' MB';
    if (bytes >= 1024) return (bytes / 1024).toFixed(1) + ' KB';
    return bytes + ' B';
  }

  function ageText(sec) {
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

  function downloadSnapshot(name) {
    if (!token) { say('enter a token first', 'bad'); return; }
    say('downloading ' + name + ' …');
    fetch('/snapshot/' + encodeURIComponent(name), {
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
      setTimeout(function () { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
      say('downloaded ' + name + ' (' + sizeText(blob.size) + ')', 'ok');
    }).catch(function (e) {
      say('download failed: ' + e.message, 'bad');
    });
  }

  function restoreSnapshot(name, blockNum) {
    var what = blockNum ? 'block #' + blockNum : name;
    if (!confirm('Restore ' + what + ' from ' + name + '?\n\n' +
                 'Shared memory is wiped and the snapshot is re-imported in-process. ' +
                 'The node pauses block processing while it happens; HTTP stays up.')) return;
    act('admin.snapshot.restore', { name: name }, 'restore started for ' + name);
  }

  function renderSnapshots(st) {
    var cards = $('snapCards');
    cards.textContent = '';
    var state = st.restoring ? 'restoring' : (st.in_progress ? 'creating' : 'idle');
    cards.appendChild(card('State', state, 'head ' +
      (st.head_block_num == null ? 'n/a during restore' : '#' + st.head_block_num),
      st.restoring || st.in_progress ? 'warn' : 'ok'));
    cards.appendChild(card('Snapshots', st.snapshot_count, st.dir));
    cards.appendChild(card('Latest', st.latest_name || '-',
      st.latest_name ? 'used by Restore latest and /snapshot/latest' : 'none yet',
      st.latest_name ? '' : 'warn'));

    var body = $('snapshots');
    body.textContent = '';
    if (!st.snapshots || !st.snapshots.length) {
      var tr = document.createElement('tr');
      var td = document.createElement('td');
      td.colSpan = 5; td.className = 'dim';
      td.textContent = 'no snapshot files in ' + st.dir;
      tr.appendChild(td); body.appendChild(tr);
      return;
    }
    st.snapshots.forEach(function (p) {
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
      actions.appendChild(button('Download', '', function () { downloadSnapshot(p.name); }));
      actions.appendChild(button('Restore', 'danger', function () {
        restoreSnapshot(p.name, p.block_num);
      }));
      body.appendChild(tr);
    });
  }

  var lastSnapError = '';

  function refreshSnapshots() {
    if (!token) return Promise.resolve();
    return rpc('admin.snapshot.status').then(function (st) {
      lastSnapError = '';
      renderSnapshots(st);
    }).catch(function (e) {
      var msg = e.rpcCode === -32004 ? 'snapshot plugin is not enabled on this node' : e.message;
      var cards = $('snapCards');
      cards.textContent = '';
      cards.appendChild(card('Snapshots', 'unavailable', msg, 'warn'));
      $('snapshots').textContent = '';
      if (msg !== lastSnapError) { say('snapshots: ' + msg, 'bad'); lastSnapError = msg; }
    });
  }

  function render(s) {
    var cards = $('cards');
    cards.textContent = '';
    var syncCls = s.node_status === 'FORWARD' ? 'ok' : 'warn';
    cards.appendChild(card('Node status', s.node_status, 'fork: ' + s.fork_status, syncCls));
    cards.appendChild(card('Head / LIB', '#' + s.head_block_num, 'lib #' + s.lib_block_num));
    cards.appendChild(card('DLT range', s.dlt_earliest + ' - ' + s.dlt_latest,
                           'blocks available locally'));
    cards.appendChild(card('Peers', s.active + ' active / ' + s.connections,
                           s.known_peers + ' known, ' + s.trusted_peers + ' trusted'));
    cards.appendChild(card('Best peer head', s.best_peer_head ? '#' + s.best_peer_head : '-',
                           s.best_peer_head ? 'lag ' + lagText(s.best_peer_head_lag) +
                           ' (' + s.peers_ahead + ' ahead)' : 'no peer heads yet',
                           s.best_peer_head_lag < -2 ? 'warn' : ''));
    cards.appendChild(card('Uptime', uptime(s.uptime_sec), 'since process start'));
    cards.appendChild(card('Banned', s.banned_peers, 'soft-banned entries',
                           s.banned_peers ? 'warn' : ''));
    var notes = [];
    if (s.paused) notes.push('BLOCK PROCESSING PAUSED');
    if (s.catching_up) notes.push('catching up');
    if (s.isolated_peers) notes.push('isolated-peers mode');
    if (s.fork_status !== 'NORMAL') notes.push('fork: ' + s.fork_status);
    cards.appendChild(card('Flags', notes.length ? notes.join(' · ') : 'ok',
                           'as of ' + s.as_of, notes.length ? 'warn' : 'ok'));
    renderPeers(s.peers);
  }

  function refresh() {
    if (!token) { setState('no token', 'warn'); return Promise.resolve(); }
    return rpc('admin.p2p.status').then(function (s) {
      setState('connected · updated ' + new Date().toLocaleTimeString(), 'ok');
      render(s);
      return refreshSnapshots();
    }).catch(function (e) {
      if (e.rpcCode === -32001 || e.httpStatus === 401) {
        setState('token rejected (401) — check the token and its scopes', 'bad');
      } else {
        setState('error: ' + e.message, 'bad');
      }
      say('status: ' + e.message, 'bad');
    });
  }

  function act(method, params, okText) {
    return rpc(method, params).then(function (r) {
      say(okText + ': ' + JSON.stringify(r), 'ok');
      return refresh();
    }).catch(function (e) {
      say(method + ' failed: ' + e.message, 'bad');
    });
  }

  function endpoint() {
    var v = $('endpoint').value.trim();
    if (!v) { say('enter IP:port first', 'bad'); return null; }
    return v;
  }

  $('save').addEventListener('click', function () {
    token = $('token').value.trim();
    if (token) { sessionStorage.setItem('vizAdminToken', token); say('token saved for this tab'); }
    else { sessionStorage.removeItem('vizAdminToken'); }
    refresh();
  });
  $('token').addEventListener('keydown', function (e) {
    if (e.key === 'Enter') $('save').click();
  });
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

  if (token) { $('token').value = token; }
  refresh();
  timer = setInterval(function () {
    if (!document.hidden) refresh();
  }, 5000);
  window.addEventListener('beforeunload', function () { clearInterval(timer); });
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

// ── tokens ─────────────────────────────────────────────────────────────────────────────────
struct token_info {
    std::string label;
    std::set<std::string> scopes;
    std::string hash_hex;
    uint64_t expires = 0;
};

token_info parse_token_spec(const std::string &spec) {
    // label:scope[,scope]:sha256hex[:expires-unixtime]
    const std::vector<std::string> parts = split(spec, ':');
    FC_ASSERT(parts.size() >= 3 && parts.size() <= 4,
              "admin-token: expected 'label:scope[,scope]:sha256hex[:expires]', got '${s}'",
              ("s", spec));
    token_info t;
    t.label = trim(parts[0]);
    FC_ASSERT(!t.label.empty(), "admin-token: empty label");

    for (const std::string &raw : split(parts[1], ',')) {
        const std::string scope = trim(raw);
        FC_ASSERT(scope == "read" || scope == "plugins" || scope == "config" ||
                  scope == "process" || scope == "chain" || scope == "p2p" ||
                  scope == "snapshot",
                  "admin-token: unknown scope '${s}' (allowed: read, plugins, config, process, chain, p2p, snapshot)",
                  ("s", scope));
        t.scopes.insert(scope);
    }
    FC_ASSERT(!t.scopes.empty(), "admin-token: token '${l}' has no scopes", ("l", t.label));

    t.hash_hex = to_lower(trim(parts[2]));
    FC_ASSERT(t.hash_hex.size() == 64 &&
              t.hash_hex.find_first_not_of("0123456789abcdef") == std::string::npos,
              "admin-token: token '${l}' hash is not 64 hex chars", ("l", t.label));

    if (parts.size() == 4) {
        const std::string exp = trim(parts[3]);
        FC_ASSERT(!exp.empty() && exp.find_first_not_of("0123456789") == std::string::npos,
                  "admin-token: token '${l}' has a non-numeric expiry", ("l", t.label));
        t.expires = std::stoull(exp);
    }
    return t;
}

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
    fc::variant h_ping(const fc::variant &params);
    fc::variant h_node_info(const fc::variant &params);
    fc::variant h_diagnostics(const fc::variant &params);
    fc::variant h_plugins_list(const fc::variant &params);
    fc::variant h_config_get(const fc::variant &params);
    fc::variant h_logs_tail(const fc::variant &params);
    fc::variant h_audit_tail(const fc::variant &params);

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

    if (method == "admin.ping") return h_ping(params);
    if (method == "admin.node_info") return h_node_info(params);
    if (method == "admin.diagnostics") return h_diagnostics(params);
    if (method == "admin.plugins.list") return h_plugins_list(params);
    if (method == "admin.config.get") return h_config_get(params);
    if (method == "admin.logs.tail") return h_logs_tail(params);
    if (method == "admin.audit.tail") return h_audit_tail(params);
    if (method == "admin.p2p.status") return h_p2p_status(params);
    if (method == "admin.snapshot.status") return h_snapshot_status(params);

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
    m("last_irreversible_block_num",
      static_cast<int64_t>(db.get_dynamic_global_properties().last_irreversible_block_num));
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
        if (e.key == "plugin" && !e.value.empty()) configured.insert(e.value);
    }
    std::size_t observed_count = 0;
    for (const auto &name : configured) {
        if (observe_plugin(name) != nullptr) ++observed_count;
    }
    m("plugins_configured", static_cast<int64_t>(configured.size()));
    m("plugins_observed", static_cast<int64_t>(observed_count));

    // shared memory file
    boost::system::error_code ec;
    const bfs::path shm = bfs::path(data_dir()) / "shared_memory.bin";
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
        if (!seen.insert(e.value).second) continue;

        const appbase::abstract_plugin *p = observe_plugin(e.value);
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
        item("name", e.value);
        // A configured plugin is always loaded: appbase calls get_plugin(name) for every
        // `plugin =` line and aborts startup when the name is unknown.
        item("loaded", p ? p->get_state() != appbase::abstract_plugin::registered : true);
        item("state", p ? fc::variant(std::string(state)) : fc::variant());
        item("state_source", std::string(source));
        plugins.push_back(fc::variant(item));
    }

    fc::mutable_variant_object m;
    m("config_path", config_path());
    m("config_exists", exists);
    m("note", "Entries come from config.ini. Appbase keeps its plugin registry private, so "
              "lifecycle state is observed only for chain, p2p and node_admin; for the others "
              "loaded=true is implied by startup (an unknown plugin name aborts the node).");
    m("plugins", plugins);
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

    fc::mutable_variant_object m;
    m("path", config_path());
    m("exists", exists);
    m("sections", sections);
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

fc::variant node_admin_plugin::impl::h_snapshot_status(const fc::variant &) {
    auto &snap = snapshot_started();
    const bool recovering = chain.is_recovering();
    const fc::variant files = snapshot_list_variant();
    const std::string latest_path = snap.latest_snapshot_path();

    fc::mutable_variant_object m;
    m("as_of", now_iso());
    m("dir", snap.snapshot_directory());
    // While a restore runs the database is closed or being rebuilt; reading head there
    // would either block or report a half-built value, so it is null during that window.
    m("head_block_num", recovering ? fc::variant()
                                   : fc::variant(static_cast<int64_t>(chain.db().head_block_num())));
    m("snapshot_count", static_cast<int64_t>(files.size()));
    m("in_progress", snap.is_snapshot_in_progress());
    m("restoring", snap.is_restoring());
    m("recovering", recovering);
    m("latest_path", latest_path);
    m("latest_name", latest_path.empty()
                         ? fc::variant()
                         : fc::variant(bfs::path(latest_path).filename().string()));
    m("snapshots", files);
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
    tools.push_back(tool("admin.node_info",
                         "Chain id, head block, hardfork, uptime and head age.", no_args));
    tools.push_back(tool("admin.diagnostics",
                         "Node info plus data dir, peer count, disk/RSS/loadavg and plugin counters.",
                         no_args));
    tools.push_back(tool("admin.plugins.list",
                         "Plugins configured in config.ini with observed lifecycle state.", no_args));
    tools.push_back(tool("admin.config.get",
                         "Effective config.ini by sections with secret values masked.", no_args));
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

    tools.push_back(tool("admin.snapshot.status",
                         "Snapshots on disk (newest block first) with size and age, the latest "
                         "file, and whether a creation or restore is currently running.",
                         no_args));
    tools.push_back(tool("admin.snapshot.create",
                         "Create a snapshot of the current head block in the snapshot directory, "
                         "asynchronously. Needs the 'snapshot' scope.",
                         no_args));
    tools.push_back(tool("admin.snapshot.restore",
                         "Restore node state from a snapshot file, or from the latest one, "
                         "in-process: pauses P2P, wipes shared memory, imports the snapshot, "
                         "replays dlt_block_log and resumes P2P. Needs the 'snapshot' scope.",
                         schema(restore_props, fc::variants{})));
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
      "additionally need the 'snapshot' scope. A human-facing UI is served at /ui/ on this "
      "same listener and uses the same bearer token.");
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
