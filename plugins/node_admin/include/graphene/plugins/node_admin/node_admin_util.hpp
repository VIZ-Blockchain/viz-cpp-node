#pragma once

// Pure helpers of the node_admin plugin: secret masking, config.ini parsing and admin-token
// parsing. They live in a header (not the plugin TU) so CTest can exercise them without
// starting a node. Behavior here is the contract the plugin relies on; keep it side-effect free.

#include <fc/exception/exception.hpp>

#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <set>
#include <string>
#include <vector>

namespace graphene {
namespace plugins {
namespace node_admin {
namespace util {

// ── small string helpers ────────────────────────────────────────────────────────────────────
inline std::string trim(const std::string &s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

inline std::string to_lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline std::vector<std::string> split(const std::string &s, char sep) {
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

/// Names carried by one `plugin = ...` line. appbase splits that value on whitespace and
/// loads every name, so `plugin = chain p2p` is two plugins; everything that reads the file
/// (the list, the editor, the group filter) has to see the same set or it disagrees with the
/// loader — a live config.ini is written that way.
inline std::vector<std::string> plugin_names_in_value(const std::string &value) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < value.size()) {
        while (i < value.size() && std::isspace(static_cast<unsigned char>(value[i]))) ++i;
        const std::size_t start = i;
        while (i < value.size() && !std::isspace(static_cast<unsigned char>(value[i]))) ++i;
        if (i > start) out.push_back(value.substr(start, i - start));
    }
    return out;
}

/// A snapshot file name as accepted from HTTP paths and JSON params: one path component,
/// no traversal, and a snapshot extension. Names come from list_snapshots(), so anything
/// else is a client mistake (or an attempt to reach outside the snapshot directory).
inline bool is_plain_snapshot_name(const std::string &name) {
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

// ── secret masking ──────────────────────────────────────────────────────────────────────────
inline bool sensitive_key(const std::string &key) {
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
inline std::string mask_text(const std::string &text) {
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

inline std::vector<cfg_entry> parse_config_file(const std::string &path, bool *exists = nullptr) {
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

// ── config editor: allow-list ─────────────────────────────────────────────────────────────
// config.ini is the file the next start depends on, so the admin API may rewrite only keys
// from this table. Identity, keys, data locations, listener endpoints and startup modes are
// deliberately absent: those are operator decisions made by hand (design §4.6). Everything
// here is read in plugin_initialize(), so each change needs a restart; the flag stays explicit
// because live-applied keys may be added later.
enum class config_type { boolean, unsigned_int, log_level };

struct config_spec {
    const char *name;
    config_type type;
    uint64_t min_value;
    uint64_t max_value;
    bool needs_restart;
    /// Empty for a top-level `key = value`; otherwise the key lives inside `[section]`
    /// (`logger.default.level` -> section "logger.default", key "level").
    const char *section = "";
};

/// Split an allow-list name into its ini section and leaf key. A name without a section is a
/// top-level option: section empty, key unchanged. `section` must match config_spec::section,
/// which is what the editor uses to address the line.
inline void config_name_parts(const config_spec &spec, std::string &section, std::string &key) {
    section = spec.section;
    key = spec.name;
    if (!section.empty() && key.size() > section.size() + 1 &&
        key.compare(0, section.size(), section) == 0 && key[section.size()] == '.') {
        key = key.substr(section.size() + 1);
    }
}

/// Accepted logger levels, in the order the UI shows them (quiet last is deliberate: an operator
/// raising verbosity should not be one stray click from `off`, which silences the log).
inline const std::vector<std::string> &log_level_values() {
    static const std::vector<std::string> levels = {"all", "debug", "info", "warn", "error", "off"};
    return levels;
}

/// Which plugin a parameter belongs to, for grouping in the UI and in admin.plugins.list.
/// `logging` is not a plugin: it is the runtime logging section of the same panel.
inline std::string config_key_group(const std::string &name) {
    if (name.compare(0, 4, "p2p-") == 0 || name.compare(0, 4, "dlt-") == 0 ||
        name == "auto-resync-on-wedge") return "p2p";
    if (name.compare(0, 10, "webserver-") == 0) return "webserver";
    if (name.compare(0, 16, "validator-guard-") == 0) return "validator_guard";
    if (name.compare(0, 7, "logger.") == 0 || name.compare(0, 4, "log.") == 0) return "logging";
    return "other";
}

inline const std::vector<config_spec> &config_allowlist() {
    static const std::vector<config_spec> specs = {
        {"p2p-max-connections", config_type::unsigned_int, 1, 10000, true},
        {"dlt-peer-max-disconnect-hours", config_type::unsigned_int, 1, 8760, true},
        {"dlt-mempool-max-tx", config_type::unsigned_int, 1, 1000000, true},
        {"dlt-mempool-max-bytes", config_type::unsigned_int, 1024, 1073741824ULL, true},
        {"dlt-mempool-max-tx-size", config_type::unsigned_int, 256, 67108864, true},
        {"dlt-mempool-max-expiration-hours", config_type::unsigned_int, 1, 720, true},
        {"dlt-peer-exchange-max-per-reply", config_type::unsigned_int, 0, 1000, true},
        {"dlt-peer-exchange-max-per-subnet", config_type::unsigned_int, 0, 1000, true},
        {"dlt-peer-exchange-min-uptime-sec", config_type::unsigned_int, 0, 86400, true},
        {"dlt-stats-interval-sec", config_type::unsigned_int, 10, 86400, true},
        {"p2p-isolated-peers", config_type::boolean, 0, 0, true},
        {"auto-resync-on-wedge", config_type::boolean, 0, 0, true},
        {"webserver-thread-pool-size", config_type::unsigned_int, 1, 4096, true},
        {"webserver-cache-enabled", config_type::boolean, 0, 0, true},
        {"webserver-cache-size", config_type::unsigned_int, 0, 10000000, true},
        {"webserver-stats-enabled", config_type::boolean, 0, 0, true},
        {"webserver-limits-enabled", config_type::boolean, 0, 0, true},
        {"webserver-limit-m", config_type::unsigned_int, 1, 100000000, true},
        {"webserver-limit-h", config_type::unsigned_int, 1, 100000000, true},
        {"webserver-limit-d", config_type::unsigned_int, 1, 100000000, true},
        {"webserver-stats-log-max-size", config_type::unsigned_int, 1024, 1073741824ULL, true},
        {"webserver-stats-log-keep", config_type::unsigned_int, 1, 1000, true},
        {"webserver-stats-interval", config_type::unsigned_int, 1, 86400, true},
        {"validator-guard-enabled", config_type::boolean, 0, 0, true},
        {"validator-guard-interval", config_type::unsigned_int, 1, 3600, true},
        {"validator-guard-disable", config_type::boolean, 0, 0, true},
        {"validator-guard-disable-max", config_type::unsigned_int, 0, 1000, true},
        {"validator-guard-disable-on-shutdown", config_type::boolean, 0, 0, true},
        {"validator-guard-shutdown-grace", config_type::unsigned_int, 1, 3600, true},
        // Logging (owner decision 20.10.2026, Q#3081=A: the parameters panel plus log settings).
        // Levels only: appender filenames and rotation windows stay operator decisions, while the
        // capped appender's bounds are exactly the knobs against a log that fills the disk.
        {"logger.default.level", config_type::log_level, 0, 0, true, "logger.default"},
        {"logger.p2p.level", config_type::log_level, 0, 0, true, "logger.p2p"},
        {"log.capped_file_appender.node.max_lines", config_type::unsigned_int, 0, 10000000, true,
         "log.capped_file_appender.node"},
        {"log.capped_file_appender.node.flush", config_type::boolean, 0, 0, true,
         "log.capped_file_appender.node"}
    };
    return specs;
}

inline const config_spec *config_spec_for(const std::string &key) {
    for (const config_spec &s : config_allowlist()) {
        if (key == s.name) return &s;
    }
    return nullptr;
}

/// Validate and canonicalize a value. Unknown keys are refused here, so a caller cannot bypass
/// the allow-list by skipping config_spec_for().
inline bool normalize_config_value(const std::string &key, const std::string &raw,
                                   std::string &normalized, std::string &why) {
    const config_spec *spec = config_spec_for(key);
    if (!spec) {
        why = "key '" + key + "' is not in the admin allow-list";
        return false;
    }
    const std::string value = trim(raw);
    if (spec->type == config_type::log_level) {
        const std::string v = to_lower(value);
        for (const std::string &level : log_level_values()) {
            if (v == level) {
                normalized = v;
                return true;
            }
        }
        why = "'" + key + "' expects one of: all, debug, info, warn, error, off";
        return false;
    }
    if (spec->type == config_type::boolean) {
        const std::string v = to_lower(value);
        if (v == "true" || v == "yes" || v == "on" || v == "1") {
            normalized = "true";
            return true;
        }
        if (v == "false" || v == "no" || v == "off" || v == "0") {
            normalized = "false";
            return true;
        }
        why = "'" + key + "' expects a boolean (true/false)";
        return false;
    }
    if (value.empty() || value.size() > 19 ||
        value.find_first_not_of("0123456789") != std::string::npos) {
        why = "'" + key + "' expects a non-negative integer";
        return false;
    }
    const uint64_t parsed = std::strtoull(value.c_str(), nullptr, 10);
    if (parsed < spec->min_value || parsed > spec->max_value) {
        why = "'" + key + "' must be between " + std::to_string(spec->min_value) + " and " +
              std::to_string(spec->max_value);
        return false;
    }
    normalized = std::to_string(parsed);
    return true;
}

// ── config editor: plugin list ─────────────────────────────────────────────────────────────
/// Plugin names registered by programs/vizd/main.cpp (including dependencies that
/// register_dependencies() pulls in, such as json_rpc). A name absent here is refused even when
/// it looks plausible: appbase would abort startup on an unknown plugin, and that is exactly the
/// failure the plan/apply pair exists to prevent.
inline const std::set<std::string> &known_plugins() {
    static const std::set<std::string> names = {
        "chain", "json_rpc", "database_api", "p2p", "webserver", "validator",
        "validator_api", "validator_guard", "network_broadcast_api", "account_history",
        "account_by_key", "raw_block", "block_archive", "block_info", "committee_api",
        "invite_api", "paid_subscription_api", "prediction_market_api",
        "custom_protocol_api", "snapshot", "testnet_plugin", "node_admin"
    };
    return names;
}

/// Plugins whose removal would either break the node's reason to exist or cut the admin API
/// itself off; the API refuses to remove them.
inline bool plugin_removal_forbidden(const std::string &name) {
    return name == "chain" || name == "node_admin";
}

// ── config editor: raw-line transforms ────────────────────────────────────────────────────
// Lines are edited in place so comments, ordering and unknown keys survive. The functions
// below are pure: they take the current lines and return the new ones.

struct cfg_line {
    enum kind_t { other, header, entry } kind = other;
    std::string section;  // section the entry belongs to (or the new section for a header)
    std::string key;
    std::string value;
};

/// Classify one raw line (without its '\n', possibly with a trailing '\r'). `current_section`
/// is the section the line starts in; for a header, `section` is the section it opens.
inline cfg_line classify_config_line(const std::string &raw, const std::string &current_section) {
    cfg_line out;
    out.section = current_section;
    std::string line = raw;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const std::string t = trim(line);
    if (t.empty() || t[0] == '#' || t[0] == ';') return out;
    if (t[0] == '[' && t.back() == ']') {
        out.kind = cfg_line::header;
        out.section = trim(t.substr(1, t.size() - 2));
        return out;
    }
    out.kind = cfg_line::entry;
    const std::size_t eq = line.find('=');
    if (eq == std::string::npos) {
        out.key = t;
    } else {
        out.key = trim(line.substr(0, eq));
        out.value = trim(line.substr(eq + 1));
    }
    return out;
}

inline std::vector<std::string> split_lines(const std::string &text) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) {
            out.push_back(text.substr(pos));
            break;
        }
        out.push_back(text.substr(pos, nl - pos));
        pos = nl + 1;
    }
    return out;
}

inline std::string join_lines(const std::vector<std::string> &lines, bool trailing_newline = true) {
    std::string out;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i != 0) out += '\n';
        out += lines[i];
    }
    if (trailing_newline && !lines.empty()) out += '\n';
    return out;
}

inline bool text_uses_crlf(const std::vector<std::string> &lines) {
    for (const std::string &line : lines) {
        if (!line.empty() && line.back() == '\r') return true;
    }
    return false;
}

struct line_edit {
    std::vector<std::string> lines;
    std::size_t changed = 0;
    std::string error;
};

/// Replace the value of a top-level `key = value` line, or insert the key before the first
/// [section] (a key after a section header would belong to that section, not to the top level).
inline line_edit set_config_value(std::vector<std::string> lines, const std::string &key,
                                 const std::string &value) {
    line_edit out{std::move(lines), 0, ""};
    const bool crlf = text_uses_crlf(out.lines);
    std::string section;
    std::size_t first_header = std::string::npos;
    for (std::size_t i = 0; i < out.lines.size(); ++i) {
        const cfg_line l = classify_config_line(out.lines[i], section);
        if (l.kind == cfg_line::header) {
            section = l.section;
            if (first_header == std::string::npos) first_header = i;
            continue;
        }
        if (l.kind != cfg_line::entry || !l.section.empty() || l.key != key) continue;

        std::string raw = out.lines[i];
        const bool cr = !raw.empty() && raw.back() == '\r';
        if (cr) raw.pop_back();
        const std::size_t eq = raw.find('=');
        if (eq == std::string::npos) {
            std::size_t b = 0;
            while (b < raw.size() && std::isspace(static_cast<unsigned char>(raw[b]))) ++b;
            out.lines[i] = raw.substr(0, b) + key + " = " + value + (cr ? "\r" : "");
        } else {
            // Keep the original spacing around '=' so the diff is exactly the value.
            std::size_t ws = eq + 1;
            while (ws < raw.size() && (raw[ws] == ' ' || raw[ws] == '\t')) ++ws;
            out.lines[i] = raw.substr(0, eq + 1) + raw.substr(eq + 1, ws - (eq + 1)) + value +
                           (cr ? "\r" : "");
        }
        ++out.changed;
    }
    if (out.changed == 0) {
        const std::string row = key + " = " + value + (crlf ? "\r" : "");
        if (first_header == std::string::npos) {
            out.lines.push_back(row);
        } else {
            out.lines.insert(out.lines.begin() + static_cast<std::ptrdiff_t>(first_header), row);
        }
        out.changed = 1;
    }
    return out;
}

/// Replace the value of `key = value` INSIDE `[section]`. A missing section is an error, not an
/// insert: inventing `[logger.foo]` would configure a logger the node never declares, and the
/// next start would silently ignore it. A missing key inside an existing section is inserted
/// directly under its header, which is where the shipped configs keep single-line options.
inline line_edit set_config_value_in_section(std::vector<std::string> lines,
                                             const std::string &section, const std::string &key,
                                             const std::string &value) {
    line_edit out{std::move(lines), 0, ""};
    if (section.empty()) {
        out.error = "internal: empty section";
        return out;
    }
    const bool crlf = text_uses_crlf(out.lines);
    std::string current;
    std::size_t header_at = std::string::npos;
    for (std::size_t i = 0; i < out.lines.size(); ++i) {
        const cfg_line l = classify_config_line(out.lines[i], current);
        if (l.kind == cfg_line::header) {
            current = l.section;
            if (current == section) header_at = i;
            continue;
        }
        if (current != section || l.kind != cfg_line::entry || l.key != key) continue;

        std::string raw = out.lines[i];
        const bool cr = !raw.empty() && raw.back() == '\r';
        if (cr) raw.pop_back();
        const std::size_t eq = raw.find('=');
        if (eq == std::string::npos) {
            std::size_t b = 0;
            while (b < raw.size() && std::isspace(static_cast<unsigned char>(raw[b]))) ++b;
            out.lines[i] = raw.substr(0, b) + key + " = " + value + (cr ? "\r" : "");
        } else {
            std::size_t ws = eq + 1;
            while (ws < raw.size() && (raw[ws] == ' ' || raw[ws] == '\t')) ++ws;
            out.lines[i] = raw.substr(0, eq + 1) + raw.substr(eq + 1, ws - (eq + 1)) + value +
                           (cr ? "\r" : "");
        }
        ++out.changed;
        return out;
    }
    if (header_at == std::string::npos) {
        out.error = "section [" + section + "] is not present in config.ini";
        return out;
    }
    out.lines.insert(out.lines.begin() + static_cast<std::ptrdiff_t>(header_at) + 1,
                     key + " = " + value + (crlf ? "\r" : ""));
    out.changed = 1;
    return out;
}

/// Current value of `key` inside `[section]`, for the plan diff.
inline bool config_value_in_section(const std::vector<std::string> &lines,
                                    const std::string &section, const std::string &key,
                                    std::string &value) {
    std::string current;
    bool found = false;
    for (const std::string &raw : lines) {
        const cfg_line l = classify_config_line(raw, current);
        if (l.kind == cfg_line::header) {
            current = l.section;
            continue;
        }
        if (current == section && l.kind == cfg_line::entry && l.key == key) {
            value = l.value;
            found = true;
        }
    }
    return found;
}

/// Add and remove `plugin = <name>` lines. Validation is part of the transform: an unknown,
/// duplicated, protected, absent or already-configured name yields `error` and the lines are
/// not touched.
inline line_edit edit_plugin_lines(std::vector<std::string> lines,
                                   const std::vector<std::string> &add,
                                   const std::vector<std::string> &remove) {
    line_edit out{std::move(lines), 0, ""};
    std::set<std::string> configured;
    std::string section;
    for (const std::string &raw : out.lines) {
        const cfg_line l = classify_config_line(raw, section);
        if (l.kind == cfg_line::header) {
            section = l.section;
        } else if (l.kind == cfg_line::entry && l.section.empty() && l.key == "plugin" &&
                   !l.value.empty()) {
            for (const std::string &name : plugin_names_in_value(l.value)) configured.insert(name);
        }
    }

    std::set<std::string> add_set;
    std::set<std::string> remove_set;
    for (const std::string &raw : add) {
        const std::string name = trim(raw);
        if (!known_plugins().count(name)) {
            out.error = "unknown plugin '" + name + "'";
            return out;
        }
        if (!add_set.insert(name).second) {
            out.error = "plugin '" + name + "' requested twice";
            return out;
        }
        if (configured.count(name)) {
            out.error = "plugin '" + name + "' is already configured";
            return out;
        }
    }
    for (const std::string &raw : remove) {
        const std::string name = trim(raw);
        if (!configured.count(name)) {
            out.error = "plugin '" + name + "' is not configured";
            return out;
        }
        if (!remove_set.insert(name).second) {
            out.error = "plugin '" + name + "' requested for removal twice";
            return out;
        }
        if (plugin_removal_forbidden(name)) {
            out.error = "plugin '" + name + "' cannot be disabled through the admin API";
            return out;
        }
        if (add_set.count(name)) {
            out.error = "plugin '" + name + "' is both added and removed";
            return out;
        }
    }
    if (add_set.empty() && remove_set.empty()) {
        out.error = "nothing to do";
        return out;
    }

    std::vector<std::string> kept;
    kept.reserve(out.lines.size() + add_set.size());
    section.clear();
    for (const std::string &raw : out.lines) {
        const cfg_line l = classify_config_line(raw, section);
        if (l.kind == cfg_line::header) section = l.section;
        if (l.kind == cfg_line::entry && l.section.empty() && l.key == "plugin" &&
            !l.value.empty()) {
            // One line can carry several names (`plugin = chain p2p`), so removal is a rewrite
            // when some names survive: dropping the whole line would disable a plugin the
            // operator never asked to touch.
            const std::vector<std::string> names = plugin_names_in_value(l.value);
            std::vector<std::string> keep_names;
            for (const std::string &name : names) {
                if (!remove_set.count(name)) keep_names.push_back(name);
            }
            if (keep_names.size() != names.size()) {
                ++out.changed;
                if (keep_names.empty()) continue;
                std::string line = "plugin = ";
                for (std::size_t n = 0; n < keep_names.size(); ++n) {
                    if (n) line += " ";
                    line += keep_names[n];
                }
                if (!raw.empty() && raw.back() == '\r') line += "\r";
                kept.push_back(line);
                continue;
            }
        }
        kept.push_back(raw);
    }
    out.lines = std::move(kept);

    if (!add_set.empty()) {
        const bool crlf = text_uses_crlf(out.lines);
        std::size_t at = out.lines.size();
        std::size_t first_header = std::string::npos;
        bool have_plugin_line = false;
        section.clear();
        for (std::size_t i = 0; i < out.lines.size(); ++i) {
            const cfg_line l = classify_config_line(out.lines[i], section);
            if (l.kind == cfg_line::header) {
                section = l.section;
                if (first_header == std::string::npos) first_header = i;
            } else if (l.kind == cfg_line::entry && l.section.empty() && l.key == "plugin") {
                at = i + 1;
                have_plugin_line = true;
            }
        }
        if (!have_plugin_line && first_header != std::string::npos) at = first_header;
        std::vector<std::string> rows;
        rows.reserve(add_set.size());
        for (const std::string &name : add_set) {
            rows.push_back("plugin = " + name + (crlf ? "\r" : ""));
        }
        out.lines.insert(out.lines.begin() + static_cast<std::ptrdiff_t>(at), rows.begin(),
                         rows.end());
        out.changed += rows.size();
    }
    return out;
}

// ── tokens ─────────────────────────────────────────────────────────────────────────────────
struct token_info {
    std::string label;
    std::set<std::string> scopes;
    std::string hash_hex;
    uint64_t expires = 0;
};

inline token_info parse_token_spec(const std::string &spec) {
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
                  scope == "snapshot" || scope == "webserver",
                  "admin-token: unknown scope '${s}' (allowed: read, plugins, config, process, chain, p2p, snapshot, webserver)",
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

} // namespace util
} // namespace node_admin
} // namespace plugins
} // namespace graphene
