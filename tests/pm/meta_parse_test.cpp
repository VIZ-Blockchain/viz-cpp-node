// HF14 prediction-market metadata parsing — pure helpers used by the prediction_market_api
// plugin (off-chain index builder). Markets carry a free-form `metadata` JSON string; the plugin
// extracts the keys it indexes and ignores everything else, and never throws on junk input.

#define BOOST_TEST_MODULE pm_meta_parse
#include <boost/test/unit_test.hpp>

#include <graphene/plugins/prediction_market_api/meta_parse.hpp>

using namespace graphene::plugins::prediction_market_api;

BOOST_AUTO_TEST_SUITE(pm_meta_parse)

BOOST_AUTO_TEST_CASE(parse_full_metadata) {
    auto m = parse_market_metadata(
        R"({"category":"sports","subcategory":"soccer","tags":["world-cup","final"],
            "banned_jurisdictions":["US","FR"],"unknown_key":{"x":1},"extra":[1,2,3]})");
    BOOST_CHECK_EQUAL(m.category, "sports");
    BOOST_CHECK_EQUAL(m.subcategory, "soccer");
    BOOST_CHECK_EQUAL(m.tags, "world-cup,final");
    BOOST_CHECK_EQUAL(m.banned_jurisdictions, "US,FR");   // unknown_key / extra ignored
}

BOOST_AUTO_TEST_CASE(non_json_yields_empty) {
    for (const char* junk : {"Will it rain tomorrow?", "", "not json {", "[1,2,3]", "42"}) {
        auto m = parse_market_metadata(junk);                 // never throws
        BOOST_CHECK(m.category.empty());
        BOOST_CHECK(m.banned_jurisdictions.empty());
    }
}

BOOST_AUTO_TEST_CASE(jurisdictions_banned_alias_and_string_form) {
    BOOST_CHECK_EQUAL(parse_market_metadata(R"({"jurisdictions_banned":["CN"]})").banned_jurisdictions, "CN");
    // a bare string (not an array) is accepted as-is
    BOOST_CHECK_EQUAL(parse_market_metadata(R"({"banned_jurisdictions":"RU"})").banned_jurisdictions, "RU");
}

BOOST_AUTO_TEST_CASE(jurisdiction_csv_membership) {
    BOOST_CHECK(meta_csv_contains("US,FR,DE", "FR"));         // middle
    BOOST_CHECK(meta_csv_contains("US,FR,DE", "US"));         // first
    BOOST_CHECK(meta_csv_contains("US,FR,DE", "DE"));         // last
    BOOST_CHECK(meta_csv_contains("FR", "FR"));               // single
    BOOST_CHECK(!meta_csv_contains("US,FR,DE", "U"));         // not a partial-token match
    BOOST_CHECK(!meta_csv_contains("US,FR,DE", "USA"));       // superset
    BOOST_CHECK(!meta_csv_contains("USA,FRA", "US"));         // prefix, not whole token
    BOOST_CHECK(!meta_csv_contains("US,FR,DE", "GB"));        // absent
    BOOST_CHECK(!meta_csv_contains("", "US"));                // empty csv
    BOOST_CHECK(!meta_csv_contains("US,FR", ""));             // empty token
}

BOOST_AUTO_TEST_SUITE_END()
