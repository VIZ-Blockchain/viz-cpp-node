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
            }
        } catch (...) { /* metadata is not JSON — leave empty */ }
        return r;
    }

}}} // graphene::plugins::prediction_market_api
