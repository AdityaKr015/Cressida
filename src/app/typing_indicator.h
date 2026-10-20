#pragma once

#include "api/typing.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace app {
  // Ephemeral state for the selected conversation, owned by the UI loop.
  class TypingIndicator {
  public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto lifetime = std::chrono::seconds{10};
    static constexpr std::size_t maximumUsers = 256, maximumNames = 3;
    struct User {
      std::string id, name;
      std::int64_t timestamp;
      Clock::time_point expires;
    };
    bool start(
        const api::TypingStart& event, const std::string& channel, const std::string& guild, const std::string& self,
        Clock::time_point now = Clock::now(),
        std::chrono::system_clock::time_point wall = std::chrono::system_clock::now()
    ) {
      if (event.timestamp < 0
          || event.channel != channel
          || event.guild != guild
          || event.user == self
          || !api::validSnowflake(event.user)
          || !api::validSnowflake(channel))
        return false;
      const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(wall.time_since_epoch()).count();
      const auto age = event.timestamp < seconds ? seconds - event.timestamp : 0;
      if (event.timestamp < 0 || age >= lifetime.count())
        return false;
      if (m_channel != channel || m_guild != guild) {
        clear();
        m_channel = channel;
        m_guild = guild;
      }
      expire(now);
      const auto at = std::ranges::find(m_users, event.user, &User::id);
      if (at != m_users.end()) {
        if (event.timestamp <= at->timestamp)
          return false;
        at->timestamp = event.timestamp;
        at->expires = now + lifetime - std::chrono::seconds{age};
        if (!event.name.empty())
          at->name = event.name;
      } else {
        if (m_users.size() >= maximumUsers)
          return false;
        m_users.push_back({event.user, event.name, event.timestamp, now + lifetime - std::chrono::seconds{age}});
      }
      return true;
    }
    void clear() {
      m_users.clear();
      m_channel.clear();
      m_guild.clear();
    }
    bool message(const std::string& channel, const std::string& user) {
      return channel == m_channel && std::erase_if(m_users, [&](const auto& entry) { return entry.id == user; }) != 0;
    }
    bool expire(Clock::time_point now = Clock::now()) {
      return std::erase_if(m_users, [&](const auto& entry) { return entry.expires <= now; }) != 0;
    }
    bool contains(const std::string& channel, const std::string& user, Clock::time_point now = Clock::now()) const {
      return channel == m_channel
          && std::ranges::any_of(m_users, [&](const auto& entry) { return entry.id == user && entry.expires > now; });
    }
    std::optional<Clock::time_point> deadline() const {
      const auto at = std::ranges::min_element(m_users, {}, &User::expires);
      return at == m_users.end() ? std::nullopt : std::make_optional(at->expires);
    }
    template <class Resolve> std::string text(Resolve resolve) const {
      std::vector<std::string> names;
      std::size_t count = 0;
      for (const auto& user : m_users) {
        const auto name = resolve(user);
        if (!name)
          continue;
        ++count;
        if (names.size() < maximumNames) {
          auto text = name->empty() ? std::string{"Someone"} : *name;
          for (auto& c : text)
            if (static_cast<unsigned char>(c) < ' ' || c == '\x7f')
              c = ' ';
          names.push_back(std::move(text));
        }
      }
      if (!count)
        return {};
      std::string result;
      for (std::size_t i = 0; i < names.size(); ++i) {
        if (i)
          result += i + 1 == names.size() && count <= maximumNames ? (count == 2 ? " and " : ", and ") : ", ";
        result += names[i];
      }
      if (count > maximumNames)
        result += ", and others";
      return result + (count == 1 ? " is typing…" : " are typing…");
    }

  private:
    std::string m_channel, m_guild;
    std::vector<User> m_users;
  };
} // namespace app
