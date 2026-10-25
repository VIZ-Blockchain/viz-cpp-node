// Contract of the [log.file_appender.<name>] section (goal #7270).
//
// Before 2026-10-25 vizd read only `filename` from the section and hard-coded the four
// rotation options, so an operator could edit rotation_interval, see the key accepted and
// get nothing — a phantom option. These checks pin the replacement: the defaults equal the
// old hard-coded values, every key of the section is honoured, and a bad or unknown key is
// reported instead of silently ignored (a typo must not cost the log file).

#define BOOST_TEST_MODULE file_appender_options
#include <boost/test/unit_test.hpp>

#include <graphene/utilities/file_appender_options.hpp>

#include <fc/reflect/variant.hpp>
#include <fc/variant.hpp>

#include <string>
#include <vector>

using graphene::utilities::file_appender_options;
using graphene::utilities::make_file_appender_config;
using graphene::utilities::parse_file_appender_options;

namespace {

typedef std::map<std::string, std::string> ini_section;

const char* const section = "log.file_appender.p2p";

std::string joined(const std::vector<std::string>& warnings) {
   std::string out;
   for (const std::string& w : warnings) out += w + "\n";
   return out;
}

bool mentions(const std::vector<std::string>& warnings, const std::string& needle) {
   return joined(warnings).find(needle) != std::string::npos;
}

} // namespace

BOOST_AUTO_TEST_CASE(defaults_match_the_values_that_were_hard_coded) {
   ini_section section_options;
   section_options["filename"] = "logs/p2p/p2p.log";
   std::vector<std::string> warnings;
   const file_appender_options o = parse_file_appender_options(section, section_options, &warnings);
   BOOST_CHECK(o.flush);
   BOOST_CHECK(o.rotate);
   BOOST_CHECK_EQUAL(o.rotation_interval_seconds, 3600);
   BOOST_CHECK_EQUAL(o.rotation_limit_seconds, 86400);
   BOOST_CHECK(warnings.empty());
}

BOOST_AUTO_TEST_CASE(all_four_keys_reach_the_result) {
   ini_section section_options;
   section_options["filename"] = "logs/p2p/p2p.log";
   section_options["flush"] = "no";
   section_options["rotate"] = "yes";
   section_options["rotation_interval"] = "300";
   section_options["rotation_limit"] = "1200";
   std::vector<std::string> warnings;
   const file_appender_options o = parse_file_appender_options(section, section_options, &warnings);
   BOOST_CHECK(!o.flush);
   BOOST_CHECK(o.rotate);
   BOOST_CHECK_EQUAL(o.rotation_interval_seconds, 300);
   BOOST_CHECK_EQUAL(o.rotation_limit_seconds, 1200);
   BOOST_CHECK(warnings.empty());
}

BOOST_AUTO_TEST_CASE(boolean_spellings) {
   const char* const false_forms[] = {"false", "no", "0"};
   const char* const true_forms[] = {"true", "yes", "1"};
   for (const char* form : false_forms) {
      ini_section section_options;
      section_options["rotate"] = form;
      section_options["flush"] = form;
      BOOST_CHECK(!parse_file_appender_options(section, section_options).rotate);
      BOOST_CHECK(!parse_file_appender_options(section, section_options).flush);
   }
   for (const char* form : true_forms) {
      ini_section section_options;
      section_options["rotate"] = form;
      section_options["flush"] = form;
      BOOST_CHECK(parse_file_appender_options(section, section_options).rotate);
      BOOST_CHECK(parse_file_appender_options(section, section_options).flush);
   }
}

BOOST_AUTO_TEST_CASE(invalid_boolean_keeps_the_default_and_says_so) {
   ini_section section_options;
   section_options["rotate"] = "maybe";
   std::vector<std::string> warnings;
   const file_appender_options o = parse_file_appender_options(section, section_options, &warnings);
   BOOST_CHECK(o.rotate);
   BOOST_CHECK(mentions(warnings, "log.file_appender.p2p.rotate: invalid value 'maybe', using true"));
}

BOOST_AUTO_TEST_CASE(invalid_interval_keeps_the_default_and_says_so) {
   ini_section section_options;
   section_options["rotation_interval"] = "hourly";
   std::vector<std::string> warnings;
   const file_appender_options o = parse_file_appender_options(section, section_options, &warnings);
   BOOST_CHECK_EQUAL(o.rotation_interval_seconds, 3600);
   BOOST_CHECK(mentions(warnings, "rotation_interval: invalid value 'hourly', using 3600"));
}

BOOST_AUTO_TEST_CASE(zero_and_negative_intervals_are_invalid) {
   for (const char* bad : {"0", "-5"}) {
      ini_section section_options;
      section_options["rotation_interval"] = bad;
      std::vector<std::string> warnings;
      const file_appender_options o = parse_file_appender_options(section, section_options, &warnings);
      BOOST_CHECK_EQUAL(o.rotation_interval_seconds, 3600);
      BOOST_CHECK(mentions(warnings, "rotation_interval: invalid value"));
   }
}

BOOST_AUTO_TEST_CASE(limit_below_interval_restores_both_defaults_when_rotating) {
   // fc::file_appender asserts rotation_limit >= rotation_interval at construction; letting
   // that pair through would cost the appender, i.e. the log file itself.
   ini_section section_options;
   section_options["rotation_interval"] = "7200";
   section_options["rotation_limit"] = "3600";
   std::vector<std::string> warnings;
   const file_appender_options o = parse_file_appender_options(section, section_options, &warnings);
   BOOST_CHECK_EQUAL(o.rotation_interval_seconds, 3600);
   BOOST_CHECK_EQUAL(o.rotation_limit_seconds, 86400);
   BOOST_CHECK(mentions(warnings, "less than rotation_interval"));
}

BOOST_AUTO_TEST_CASE(limit_below_interval_is_fine_when_rotation_is_off) {
   // With rotate = false the appender never looks at the pair, so the assert cannot fire.
   ini_section section_options;
   section_options["rotate"] = "false";
   section_options["rotation_interval"] = "7200";
   section_options["rotation_limit"] = "3600";
   std::vector<std::string> warnings;
   const file_appender_options o = parse_file_appender_options(section, section_options, &warnings);
   BOOST_CHECK_EQUAL(o.rotation_interval_seconds, 7200);
   BOOST_CHECK_EQUAL(o.rotation_limit_seconds, 3600);
   BOOST_CHECK(warnings.empty());
}

BOOST_AUTO_TEST_CASE(unknown_key_is_reported_not_ignored) {
   // The whole class of phantom options: the parser accepts any key, so a typo must be named.
   ini_section section_options;
   section_options["filename"] = "logs/p2p/p2p.log";
   section_options["rotation_intervall"] = "600";
   std::vector<std::string> warnings;
   const file_appender_options o = parse_file_appender_options(section, section_options, &warnings);
   BOOST_CHECK_EQUAL(o.rotation_interval_seconds, 3600);
   BOOST_CHECK(mentions(warnings, "rotation_intervall: unknown option of a file appender"));
}

BOOST_AUTO_TEST_CASE(config_carries_the_parsed_values_into_the_appender_channel) {
   ini_section section_options;
   section_options["flush"] = "false";
   section_options["rotate"] = "true";
   section_options["rotation_interval"] = "300";
   section_options["rotation_limit"] = "1200";
   std::vector<std::string> warnings;
   const fc::file_appender::config config =
      make_file_appender_config(section, fc::path("logs/p2p/p2p.log"), section_options, &warnings);
   BOOST_CHECK(warnings.empty());
   BOOST_CHECK(config.filename.generic_string() == "logs/p2p/p2p.log");
   BOOST_CHECK(!config.flush);
   BOOST_CHECK(config.rotate);
   BOOST_CHECK(config.rotation_interval == fc::seconds(300));
   BOOST_CHECK(config.rotation_limit == fc::seconds(1200));

   // The appender itself is constructed from a variant (file_appender(const variant&) does
   // args.as<config>()), so this round trip is the channel the values actually travel.
   const fc::file_appender::config back = fc::variant(config).as<fc::file_appender::config>();
   BOOST_CHECK(back.rotation_interval == fc::seconds(300));
   BOOST_CHECK(back.rotation_limit == fc::seconds(1200));
   BOOST_CHECK(back.rotate);
   BOOST_CHECK(!back.flush);
}
