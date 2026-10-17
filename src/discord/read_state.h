#pragma once
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace discord {
  struct UnreadCursor {
    std::string after, latest;
    bool operator==(const UnreadCursor&) const = default;
  };
  // Gateway-owned channel read state. Numeric snowflakes compare without overflow.
  class ReadState {
  public:
    void ready(const nlohmann::json& value);
    void channels(const nlohmann::json& value, const std::string& guild = {});
    void message(const nlohmann::json& value, const std::string& self);
    void acknowledge(const nlohmann::json& value);
    std::map<std::string, std::string> unread() const;
    std::map<std::string, UnreadCursor> unreadCursors() const;

  private:
    struct Entry {
      std::string latest, acknowledged, guild;
      int mentions = 0;
    };
    std::map<std::string, Entry> m_channels;
  };
} // namespace discord
