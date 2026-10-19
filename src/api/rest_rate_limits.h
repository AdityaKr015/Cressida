#pragma once

#include <algorithm>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <string_view>

namespace api {
  // Shared admission state, never a lock around HTTP or response decoding.
  // Query cursors share a route; a route-specific 429 cannot stall other routes.
  class RestRateLimits {
  public:
    using Clock = std::chrono::steady_clock;
    Clock::time_point deadline(std::string_view path) const {
      std::lock_guard lock(m_mutex);
      const auto at = m_routes.find(route(path));
      return at == m_routes.end() ? m_global : std::max(m_global, at->second);
    }
    void
    limited(std::string_view path, std::chrono::milliseconds wait, bool global, Clock::time_point now = Clock::now()) {
      std::lock_guard lock(m_mutex);
      const auto until = now + std::clamp(wait, std::chrono::milliseconds(0), std::chrono::milliseconds(86400100));
      if (global) {
        m_global = std::max(m_global, until);
        return;
      }
      std::erase_if(m_routes, [&](const auto& entry) { return entry.second <= now; });
      const auto key = route(path);
      if (!m_routes.contains(key) && m_routes.size() >= 512) {
        // At capacity remain conservative rather than forgetting an active limit.
        m_global = std::max(m_global, until);
        return;
      }
      auto& current = m_routes[key];
      current = std::max(current, until);
    }
    void clear() {
      std::lock_guard lock(m_mutex);
      m_global = {};
      m_routes.clear();
    }

  private:
    static std::string route(std::string_view path) {
      path = path.substr(0, path.find('?'));
      std::string result;
      std::string_view group;
      std::size_t part = 0;
      while (!path.empty()) {
        if (path.front() == '/') {
          result += '/';
          path.remove_prefix(1);
          continue;
        }
        const auto end = path.find('/');
        const auto segment = path.substr(0, end);
        if (part == 0)
          group = segment;
        const bool major = part == 1 && (group == "channels" || group == "guilds" || group == "webhooks");
        const bool numeric =
            !segment.empty() && std::ranges::all_of(segment, [](char c) { return c >= '0' && c <= '9'; });
        result += numeric && !major ? ":id" : std::string(segment);
        ++part;
        if (end == std::string_view::npos)
          break;
        path.remove_prefix(end);
      }
      return result;
    }
    mutable std::mutex m_mutex;
    Clock::time_point m_global{};
    std::map<std::string, Clock::time_point, std::less<>> m_routes;
  };
} // namespace api
