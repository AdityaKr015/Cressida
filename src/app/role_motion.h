#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <string>

namespace app {
  // Called only on the owning UI thread, once per presented scene. Unused and
  // unhovered names stop immediately; no independent animation timer exists.
  class RoleMotion {
  public:
    using Clock = std::chrono::steady_clock;
    void clear() { m_starts.clear(); m_seen.clear(); }
    void begin() { m_seen.clear(); }
    float phase(const std::string& key, bool hovered, Clock::time_point now = Clock::now()) {
      if (!hovered) {
        m_starts.erase(key);
        return 0;
      }
      if (!m_starts.contains(key) && m_starts.size() >= 256)
        return 0;
      const auto [at, inserted] = m_starts.try_emplace(key, now);
      m_seen.insert(key);
      const auto elapsed = std::max(0.0, std::chrono::duration<double>(now - at->second).count());
      return static_cast<float>(std::fmod(elapsed / 4.0, 1.0));
    }
    void end() {
      std::erase_if(m_starts, [&](const auto& entry) { return !m_seen.contains(entry.first); });
    }
    bool active() const { return !m_starts.empty(); }
  private:
    std::map<std::string, Clock::time_point> m_starts;
    std::set<std::string> m_seen;
  };
} // namespace app
