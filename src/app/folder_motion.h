#pragma once

#include "ui/style.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <set>
#include <string>

namespace app {
  // Owning-loop state sampled only for compositor-presented frames.
  class FolderMotion {
  public:
    using Clock = std::chrono::steady_clock;
    static constexpr auto duration = std::chrono::milliseconds{Style::animNormal};
    void begin() { m_seen.clear(); }
    float set(const std::string& id, bool open, bool animate, Clock::time_point now = Clock::now()) {
      m_seen.insert(id);
      const float target = open ? 1 : 0;
      auto [at, inserted] = m_states.try_emplace(id, State{target, target, target, now});
      auto& state = at->second;
      if (!animate) {
        state.value = state.from = state.target = target;
      } else if (!inserted && state.target != target) {
        // Reverse from the last presented value rather than an unseen future sample.
        state.from = state.value;
        state.target = target;
        state.started = now;
      }
      return state.value;
    }
    void end() {
      std::erase_if(m_states, [&](const auto& item) { return !m_seen.contains(item.first); });
    }
    bool tick(Clock::time_point now = Clock::now()) {
      for (auto& [id, state] : m_states) {
        if (state.value == state.target)
          continue;
        const float t = std::clamp(std::chrono::duration<float>(now - state.started) / duration, 0.0F, 1.0F);
        state.value = std::lerp(state.from, state.target, t * t * (3 - 2 * t));
      }
      return active();
    }
    bool active() const {
      return std::ranges::any_of(m_states, [](const auto& item) { return item.second.value != item.second.target; });
    }
    void finish() {
      for (auto& [id, state] : m_states)
        state.value = state.from = state.target;
    }
    void clear() {
      m_states.clear();
      m_seen.clear();
    }
    std::size_t size() const { return m_states.size(); }

  private:
    struct State {
      float value, from, target;
      Clock::time_point started;
    };
    std::map<std::string, State> m_states;
    std::set<std::string> m_seen;
  };
} // namespace app
