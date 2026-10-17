#pragma once
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace discord {
  struct UnreadCursor {
    std::string after, latest;
    int mentions = 0;
    // Bounded live mention journal, used to filter counts during a local ACK.
    std::vector<std::string> mentionMessages;
    bool operator==(const UnreadCursor&) const = default;
  };
  // Gateway-owned channel read state. Numeric snowflakes compare without overflow.
  class ReadState {
  public:
    void ready(const nlohmann::json& value);
    void channels(const nlohmann::json& value, const std::string& guild = {});
    void context(std::string_view type, const nlohmann::json& value, const std::string& self);
    void message(const nlohmann::json& value, const std::string& self, bool blocked = false, bool muted = false);
    void removed(const nlohmann::json& value);
    void acknowledge(const nlohmann::json& value);
    std::map<std::string, std::string> unread() const;
    std::map<std::string, UnreadCursor> unreadCursors() const;

  private:
    struct Entry {
      std::string latest, acknowledged, guild, processed;
      int mentions = 0;
    };
    std::map<std::string, Entry> m_channels;
    struct GuildContext {
      std::set<std::string> roles;
      bool suppressRoles = false, suppressEveryone = false;
    };
    std::map<std::string, GuildContext> m_guilds;
    std::map<std::string, std::string> m_mentions;
  };
} // namespace discord
