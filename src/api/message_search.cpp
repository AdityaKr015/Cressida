#include "api/message_search.h"

#include "api/models.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <limits>

namespace api {
  namespace {
    constexpr std::size_t maximumSearchBytes = 1024, maximumSearchFilters = 64;
    constexpr std::uint64_t snowflakeEpoch = 1420070400000ULL;
    constexpr unsigned snowflakeTimestampShift = 22;
    bool whitespace(char c) { return c == ' ' || c == '\t'; }
    std::string lower(std::string value) {
      for (auto& c : value)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      return value;
    }
    bool filter(std::string_view key) {
      constexpr std::array names{"from", "in", "mentions", "has", "before", "after", "during", "author"};
      return std::ranges::find(names, key) != names.end();
    }
    std::size_t wordEnd(std::string_view source, std::size_t at) {
      bool quoted = false, escaped = false;
      while (at < source.size()) {
        const char c = source[at];
        if (!quoted && whitespace(c))
          break;
        if (!escaped && c == '"')
          quoted = !quoted;
        escaped = !escaped && c == '\\';
        ++at;
      }
      return at;
    }
    std::optional<std::uint64_t> dateBound(std::string_view value, bool following) {
      if (value.size() != 10 || value[4] != '-' || value[7] != '-')
        return {};
      const auto number = [&](std::size_t start, std::size_t count) {
        int result = -1;
        const auto end = value.data() + start + count;
        const auto parsed = std::from_chars(value.data() + start, end, result);
        return parsed.ec == std::errc{} && parsed.ptr == end ? result : -1;
      };
      const std::chrono::year_month_day date{
          std::chrono::year{number(0, 4)}, std::chrono::month{static_cast<unsigned>(number(5, 2))},
          std::chrono::day{static_cast<unsigned>(number(8, 2))}
      };
      if (!date.ok())
        return {};
      const auto time = std::chrono::sys_days{date} + std::chrono::days{following ? 1 : 0};
      const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count();
      if (ms < static_cast<std::int64_t>(snowflakeEpoch)
          || static_cast<std::uint64_t>(ms) - snowflakeEpoch
              > (std::numeric_limits<std::uint64_t>::max() >> snowflakeTimestampShift))
        return {};
      return (static_cast<std::uint64_t>(ms) - snowflakeEpoch) << snowflakeTimestampShift;
    }
  } // namespace

  std::vector<SearchToken> messageSearchTokens(std::string_view source) {
    std::vector<SearchToken> result;
    for (std::size_t at = 0; at < source.size();) {
      if (whitespace(source[at])) {
        ++at;
        continue;
      }
      const auto begin = at;
      auto end = wordEnd(source, at);
      const auto colon = source.find(':', at);
      if (colon < end && filter(lower(std::string(source.substr(at, colon - at))))) {
        const auto key = lower(std::string(source.substr(at, colon - at)));
        auto start = colon + 1;
        if (start == end) {
          while (start < source.size() && whitespace(source[start]))
            ++start;
          // A new filter must not become the previous filter's missing value.
          const auto nextEnd = wordEnd(source, start), nextColon = source.find(':', start);
          if (start < source.size()
              && !(nextColon < nextEnd && filter(lower(std::string(source.substr(start, nextColon - start))))))
            end = nextEnd;
          else
            start = end;
        }
        auto value = std::string(source.substr(start, end - start));
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
          value = value.substr(1, value.size() - 2);
        result.push_back({begin, end, key, std::move(value)});
      }
      at = std::max(begin + 1, end);
    }
    return result;
  }

  MessageSearch parseMessageSearch(std::string_view source, const SearchResolver& resolve) {
    MessageSearch result;
    const auto fail = [&](std::string error) {
      result.error = std::move(error);
      return result;
    };
    if (source.size() > maximumSearchBytes
        || std::ranges::any_of(source, [](unsigned char c) { return c < 32 || c == 127; }))
      return fail("Searches can contain up to 1,024 characters on one line.");
    const auto tokens = messageSearchTokens(source);
    if (tokens.size() > maximumSearchFilters)
      return fail("This search has too many filters.");
    std::optional<std::uint64_t> minimum, maximum;
    const auto parameter = [&](std::string key, std::string value) {
      const auto entry = std::pair(std::move(key), std::move(value));
      if (std::ranges::find(result.parameters, entry) == result.parameters.end())
        result.parameters.push_back(entry);
    };
    std::size_t consumed = 0;
    const auto content = [&](std::string_view part) {
      while (!part.empty() && whitespace(part.front()))
        part.remove_prefix(1);
      while (!part.empty() && whitespace(part.back()))
        part.remove_suffix(1);
      if (!part.empty()) {
        if (!result.content.empty())
          result.content += ' ';
        result.content += part;
      }
    };
    std::string canonicalFilters;
    for (const auto& token : tokens) {
      content(source.substr(consumed, token.begin - consumed));
      consumed = token.end;
      auto value = token.value;
      if (value.empty() || (value.front() == '"' && (value.size() == 1 || value.back() != '"')))
        return fail("Choose a value for " + token.key + ":.");
      if (token.key == "from" || token.key == "mentions" || token.key == "in") {
        if (value.starts_with("<") && value.ends_with(">")) {
          const auto marker = token.key == "in" ? "<#" : value.starts_with("<@!") ? "<@!" : "<@";
          if (value.starts_with(marker))
            value = value.substr(std::string_view(marker).size(), value.size() - std::string_view(marker).size() - 1);
        }
        if (!validSnowflake(value) && resolve) {
          if (auto id = resolve(token.key, value))
            value = *id;
        }
        if (!validSnowflake(value))
          return fail(
              "Choose a matching "
              + std::string(token.key == "in" ? "channel" : "person")
              + " for "
              + token.key
              + ":, or enter its ID."
          );
        parameter(token.key == "from" ? "author_id" : token.key == "in" ? "channel_id" : "mentions", value);
      } else if (token.key == "has") {
        value = lower(value);
        const auto type = value.starts_with('-') ? value.substr(1) : value;
        constexpr std::array types{"image", "video", "sound",    "file", "sticker", "embed",
                                   "link",  "poll",  "snapshot", "gif",  "media"};
        if (std::ranges::find(types, type) == types.end())
          return fail("Choose a supported content type for has:.");
        if (type == "gif")
          parameter("embed_type", value.starts_with('-') ? "-gif" : "gif");
        else if (type == "media") {
          parameter("has", value.starts_with('-') ? "-image" : "image");
          parameter("has", value.starts_with('-') ? "-video" : "video");
        } else
          parameter("has", value);
      } else if (token.key == "author") {
        value = lower(value);
        if (value != "user" && value != "bot" && value != "webhook")
          return fail("Choose user, bot or webhook for author:.");
        parameter("author_type", value);
      } else {
        const auto begin = dateBound(value, false), end = dateBound(value, true);
        if (!begin || !end)
          return fail("Use a valid date in YYYY-MM-DD format from 2015 onward.");
        if (token.key != "before") {
          const auto bound = *begin > 0 ? *begin - 1 : 0;
          minimum = minimum ? std::max(*minimum, bound) : bound;
        }
        if (token.key != "after") {
          const auto bound = token.key == "during" ? *end : *begin;
          maximum = maximum ? std::min(*maximum, bound) : bound;
        }
      }
      canonicalFilters += (canonicalFilters.empty() ? "" : " ") + token.key + ":" + value;
    }
    content(source.substr(consumed));
    if (minimum && maximum && *minimum >= *maximum)
      return fail("The search date range is empty.");
    if (minimum)
      parameter("min_id", std::to_string(*minimum));
    if (maximum)
      parameter("max_id", std::to_string(*maximum));
    result.canonical =
        result.content + (result.content.empty() || canonicalFilters.empty() ? "" : " ") + canonicalFilters;
    if (result.content.empty() && result.parameters.empty())
      return fail("Enter a search or choose a filter.");
    return result;
  }
} // namespace api
