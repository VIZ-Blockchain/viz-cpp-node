#include <graphene/utilities/file_appender_options.hpp>

#include <cerrno>
#include <cstdlib>

namespace graphene {
namespace utilities {

namespace {

bool parse_bool(const std::string& value, bool* out) {
   if (value == "true" || value == "yes" || value == "1") {
      *out = true;
      return true;
   }
   if (value == "false" || value == "no" || value == "0") {
      *out = false;
      return true;
   }
   return false;
}

bool parse_seconds(const std::string& value, int64_t* out) {
   if (value.empty())
      return false;
   char* end = nullptr;
   errno = 0;
   const long long parsed = std::strtoll(value.c_str(), &end, 10);
   if (errno != 0 || end == value.c_str() || *end != '\0')
      return false;
   *out = parsed;
   return true;
}

void warn(std::vector<std::string>* warnings, const std::string& text) {
   if (warnings)
      warnings->push_back(text);
}

std::string key_of(const std::string& section_name, const std::string& key) {
   return section_name + "." + key;
}

} // namespace

file_appender_options parse_file_appender_options(const std::string& section_name,
                                                  const std::map<std::string, std::string>& options,
                                                  std::vector<std::string>* warnings) {
   file_appender_options result;

   static const char* const known_keys[] = {
      "filename", "flush", "rotate", "rotation_interval", "rotation_limit"};

   for (const auto& option : options) {
      bool known = false;
      for (const char* key : known_keys)
         known = known || option.first == key;
      if (!known)
         warn(warnings, key_of(section_name, option.first) +
                            ": unknown option of a file appender (known: filename, flush, rotate, "
                            "rotation_interval, rotation_limit)");
   }

   auto flush = options.find("flush");
   if (flush != options.end()) {
      bool parsed;
      if (parse_bool(flush->second, &parsed))
         result.flush = parsed;
      else
         warn(warnings, key_of(section_name, "flush") + ": invalid value '" + flush->second +
                            "', using true");
   }

   auto rotate = options.find("rotate");
   if (rotate != options.end()) {
      bool parsed;
      if (parse_bool(rotate->second, &parsed))
         result.rotate = parsed;
      else
         warn(warnings, key_of(section_name, "rotate") + ": invalid value '" + rotate->second +
                            "', using true");
   }

   auto interval = options.find("rotation_interval");
   if (interval != options.end()) {
      int64_t parsed = 0;
      if (parse_seconds(interval->second, &parsed) && parsed >= 1)
         result.rotation_interval_seconds = parsed;
      else
         warn(warnings, key_of(section_name, "rotation_interval") + ": invalid value '" +
                            interval->second + "', using " +
                            std::to_string(result.rotation_interval_seconds));
   }

   auto limit = options.find("rotation_limit");
   if (limit != options.end()) {
      int64_t parsed = 0;
      if (parse_seconds(limit->second, &parsed) && parsed >= 1)
         result.rotation_limit_seconds = parsed;
      else
         warn(warnings, key_of(section_name, "rotation_limit") + ": invalid value '" +
                            limit->second + "', using " +
                            std::to_string(result.rotation_limit_seconds));
   }

   // The appender only asserts the pair when it rotates; with rotate = false the interval
   // and the limit are not used at all, so a reversed pair there is not an error.
   if (result.rotate && result.rotation_limit_seconds < result.rotation_interval_seconds) {
      warn(warnings, key_of(section_name, "rotation_limit") + ": " +
                         std::to_string(result.rotation_limit_seconds) +
                         " is less than rotation_interval " +
                         std::to_string(result.rotation_interval_seconds) +
                         ", using 3600 and 86400 (fc::file_appender requires limit >= interval)");
      result.rotation_interval_seconds = 60 * 60;
      result.rotation_limit_seconds = 24 * 60 * 60;
   }

   return result;
}

fc::file_appender::config make_file_appender_config(const std::string& section_name,
                                                    const fc::path& filename,
                                                    const std::map<std::string, std::string>& options,
                                                    std::vector<std::string>* warnings) {
   const file_appender_options parsed = parse_file_appender_options(section_name, options, warnings);
   fc::file_appender::config config;
   config.filename = filename;
   config.flush = parsed.flush;
   config.rotate = parsed.rotate;
   config.rotation_interval = fc::seconds(parsed.rotation_interval_seconds);
   config.rotation_limit = fc::seconds(parsed.rotation_limit_seconds);
   return config;
}

} // namespace utilities
} // namespace graphene
