#pragma once

// Pure helpers of the node_admin plugin: secret masking, config.ini parsing and admin-token
// parsing. They live in a header (not the plugin TU) so CTest can exercise them without
// starting a node. Behavior here is the contract the plugin relies on; keep it side-effect free.

#include <fc/exception/exception.hpp>

#include <cctype>
#include <cstdint>
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
