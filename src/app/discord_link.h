#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace app {
  struct DiscordLink {
    std::string guild, channel, message;
    bool operator==(const DiscordLink&) const = default;
  };

  // Parse destinations, not arbitrary URLs containing a Discord-looking path.
  inline std::optional<DiscordLink> discordLink(std::string_view url) {
    const auto scheme = url.find("://");
    if (scheme == std::string_view::npos)
      return {};
    auto lower = [](std::string_view value) {
      std::string result(value);
      std::ranges::transform(result, result.begin(), [](unsigned char c) { return std::tolower(c); });
      return result;
    };
    const auto protocol = lower(url.substr(0, scheme));
    if (protocol != "https" && protocol != "http")
      return {};
    url.remove_prefix(scheme + std::string_view("://").size());
    const auto slash = url.find('/');
    if (slash == std::string_view::npos)
      return {};
    const auto host = lower(url.substr(0, slash));
    if (host != "discord.com"
        && host != "www.discord.com"
        && host != "canary.discord.com"
        && host != "ptb.discord.com"
        && host != "discordapp.com"
        && host != "www.discordapp.com"
        && host != "canary.discordapp.com"
        && host != "ptb.discordapp.com")
      return {};
    url.remove_prefix(slash);
    if (!url.starts_with("/channels/"))
      return {};
    url.remove_prefix(std::string_view("/channels/").size());
    url = url.substr(0, url.find_first_of("?#"));
    if (url.ends_with('/'))
      url.remove_suffix(1);
    auto part = [&] {
      const auto end = url.find('/');
      const auto value = url.substr(0, end);
      url = end == std::string_view::npos ? std::string_view{} : url.substr(end + 1);
      return std::string(value);
    };
    DiscordLink result{part(), part(), {}};
    if (!url.empty())
      result.message = part();
    auto snowflake = [](std::string_view value) {
      constexpr std::size_t maxSnowflakeDigits = 20;
      if (value.empty() || value.size() > maxSnowflakeDigits || value.front() == '0')
        return false;
      std::uint64_t parsed = 0;
      const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
      return error == std::errc{} && end == value.data() + value.size() && parsed != 0;
    };
    if (!url.empty()
        || (result.guild != "@me" && !snowflake(result.guild))
        || !snowflake(result.channel)
        || (!result.message.empty() && !snowflake(result.message)))
      return {};
    return result;
  }
} // namespace app
