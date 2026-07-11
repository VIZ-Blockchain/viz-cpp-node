#pragma once

// Pure, dependency-light helpers for prediction-market metadata: parse a market's free-form
// `metadata` field as JSON and test CSV jurisdiction membership. Kept free of chain/database
// types so they can be unit-tested in isolation (tests/pm/meta_parse_test.cpp). The unified
// prediction_market_api plugin's applied_block handler and list_markets_by_category call these.

#include <string>
#include <fc/io/json.hpp>
#include <fc/variant.hpp>
#include <fc/variant_object.hpp>

namespace graphene { namespace plugins { namespace prediction_market_api {

    struct parsed_meta {
        std::string category;
        std::string subcategory;
        std::string tags;                 ///< CSV
        std::string banned_jurisdictions; ///< CSV
        std::string title;                ///< human-readable question
        std::string image;                ///< icon/cover URL
        std::string condition_id;         ///< source dedup id (for client back-link)
        std::string description;          ///< short resolution rules (how the oracle resolves) — surface-level, for clients
        std::string event;                ///< parent grouping key (e.g. one match/game); siblings share it
        std::string event_title;          ///< human-readable event label (e.g. "Dota 2: A vs B") for the event page/cards
        bool        child = false;        ///< true = a child/prop market of a parent event; hidden from category/tag listings by default
    };

    /// Flatten a JSON array (or bare string) into a comma-separated string.
    inline std::string meta_join_array(const fc::variant& v) {
        std::string s;
        if (v.is_array()) {
            for (const auto& e : v.get_array()) { if (!s.empty()) s += ","; s += e.as_string(); }
        } else if (v.is_string()) {
            s = v.as_string();
        }
        return s;
    }

    /// ASCII-lowercase a copy. Used for case-insensitive tag matching (tags are English labels
    /// with inconsistent source casing — "Dota 2" vs "counter strike 2"; clients pass them
    /// lowercased). ASCII-only keeps it locale-independent and deterministic.
    inline std::string meta_ascii_lower(std::string s) {
        for (char& c : s) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        return s;
    }

    /// CSV membership with exact-token boundaries (commas), so "US" never matches "USA".
    inline bool meta_csv_contains(const std::string& csv, const std::string& token) {
        if (token.empty() || csv.empty()) return false;
        size_t pos = 0;
        while ((pos = csv.find(token, pos)) != std::string::npos) {
            bool left  = (pos == 0) || csv[pos-1] == ',';
            bool right = (pos + token.size() == csv.size()) || csv[pos+token.size()] == ',';
            if (left && right) return true;
            pos += token.size();
        }
        return false;
    }

    /// Case-insensitive CSV membership — both sides ASCII-lowercased, so a "dota 2" query matches
    /// a stored "Dota 2" tag. Same comma-boundary semantics as meta_csv_contains.
    inline bool meta_csv_contains_ci(const std::string& csv, const std::string& token) {
        return meta_csv_contains(meta_ascii_lower(csv), meta_ascii_lower(token));
    }

    /// Parse a market's free-form `metadata` field as JSON, extracting the keys we index and
    /// ignoring everything else. Non-JSON / empty input yields an empty result — never throws.
    inline parsed_meta parse_market_metadata(const std::string& metadata) {
        parsed_meta r;
        try {
            fc::variant v = fc::json::from_string(metadata);
            if (v.is_object()) {
                const auto& o = v.get_object();
                if (o.contains("category"))    r.category    = o["category"].as_string();
                if (o.contains("subcategory")) r.subcategory = o["subcategory"].as_string();
                if (o.contains("tags"))        r.tags        = meta_join_array(o["tags"]);
                if (o.contains("banned_jurisdictions"))
                    r.banned_jurisdictions = meta_join_array(o["banned_jurisdictions"]);
                else if (o.contains("jurisdictions_banned"))
                    r.banned_jurisdictions = meta_join_array(o["jurisdictions_banned"]);
                if (o.contains("title"))        r.title        = o["title"].as_string();
                if (o.contains("image"))        r.image        = o["image"].as_string();
                if (o.contains("condition_id")) r.condition_id = o["condition_id"].as_string();
                if (o.contains("description"))  r.description  = o["description"].as_string();
                if (o.contains("event"))        r.event        = o["event"].as_string();
                if (o.contains("event_title"))  r.event_title  = o["event_title"].as_string();
                // child: props side of a split match ("<slug>-more-markets"). Accept bool / int(1) /
                // "1"/"true" — the parser emits it as an integer 1.
                if (o.contains("child")) {
                    const auto& c = o["child"];
                    try { r.child = c.as_bool(); }
                    catch (...) {
                        try { r.child = (c.as_int64() != 0); }
                        catch (...) { const auto s = c.as_string(); r.child = (s == "1" || s == "true"); }
                    }
                }
            }
        } catch (...) { /* metadata is not JSON — leave empty */ }
        return r;
    }

}}} // graphene::plugins::prediction_market_api
