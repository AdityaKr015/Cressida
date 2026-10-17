#pragma once

#include "render/animation/animation.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

namespace app {
  // Uses the copied ScrollView's accumulated wheel targets and cubic easing.
  // Offsets belong to the application; scene rebuilds cannot restart motion.
  class ScrollMotion {
  public:
    using Clock = std::chrono::steady_clock;
    void limit(float& offset, float maximum) {
      auto& state = m_states[&offset];
      if (offset != state.last)
        state.active = false;
      state.maximum = std::max(0.0F, maximum);
      offset = std::clamp(offset, 0.0F, state.maximum);
      state.target = std::clamp(state.active ? state.target : offset, 0.0F, state.maximum);
      state.last = offset;
    }
    void scroll(float& offset, float delta, bool precise, Clock::time_point now = Clock::now()) {
      if (!std::isfinite(delta))
        return;
      auto& state = m_states[&offset];
      sample(offset, state, now);
      const bool reversing = state.active && delta * (state.target - offset) < 0;
      const float base = state.active && !reversing ? state.target : offset;
      state.target = std::clamp(base + delta, 0.0F, state.maximum);
      state.from = offset;
      state.started = now;
      state.duration = std::clamp(std::abs(state.target - offset) * 0.45F, 50.0F, 200.0F);
      state.active = !precise && std::abs(state.target - offset) >= 0.01F;
      if (!state.active)
        offset = state.last = state.target;
    }
    bool tick(Clock::time_point now = Clock::now()) {
      bool active = false;
      for (auto& [offset, state] : m_states) {
        sample(*offset, state, now);
        active |= state.active;
      }
      return active;
    }
    void stop(float& offset) {
      auto& state = m_states[&offset];
      state.active = false;
      state.target = state.last = offset;
    }
    void stopAll() {
      for (auto& [offset, state] : m_states)
        stop(*offset);
    }
    float target(float& offset) const {
      const auto found = m_states.find(&offset);
      return found == m_states.end() ? offset : found->second.target;
    }

  private:
    struct State {
      float maximum = 0, from = 0, target = 0, last = 0, duration = 50;
      Clock::time_point started{};
      bool active = false;
    };
    static void sample(float& offset, State& state, Clock::time_point now) {
      if (!state.active) {
        state.last = offset;
        return;
      }
      if (offset != state.last) {
        state.active = false;
        state.target = state.last = offset;
        return;
      }
      const auto elapsed = std::chrono::duration<float, std::milli>(now - state.started).count();
      const float t = std::clamp(elapsed / state.duration, 0.0F, 1.0F);
      offset = state.last = std::clamp(
          state.from + (state.target - state.from) * applyEasing(Easing::EaseOutCubic, t), 0.0F, state.maximum
      );
      if (t >= 1) {
        state.active = false;
        offset = state.last = state.target;
      }
    }
    std::map<float*, State> m_states;
  };
} // namespace app
