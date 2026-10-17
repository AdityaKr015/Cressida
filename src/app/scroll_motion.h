#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

namespace app {
  // Integrate wheel events between frames, but publish positions only on tick.
  // A reversal starts from the last visible position, never an unseen advance
  // in the old direction. Touchpad offsets remain direct.
  class ScrollMotion {
  public:
    using Clock = std::chrono::steady_clock;
    static float distance(float normalized, bool precise) { return precise ? normalized : normalized * 1.25F; }
    bool active(float& offset) const {
      const auto at = m_states.find(&offset);
      return at != m_states.end() && at->second.active;
    }
    void rebase(float& offset, float delta) {
      auto& state = m_states[&offset];
      synchronize(offset, state);
      const bool toBottom = state.active && state.target <= 1;
      offset += delta;
      state.last = offset;
      state.position += delta;
      state.target = toBottom ? 0 : state.target + delta;
    }
    void limit(float& offset, float maximum) {
      auto& state = m_states[&offset];
      synchronize(offset, state);
      state.maximum = std::max(0.0F, maximum);
      offset = std::clamp(offset, 0.0F, state.maximum);
      state.position = std::clamp(state.position, 0.0F, state.maximum);
      state.target = std::clamp(state.active ? state.target : offset, 0.0F, state.maximum);
      state.last = offset;
    }
    void scroll(float& offset, float delta, bool precise, Clock::time_point now = Clock::now()) {
      if (!std::isfinite(delta) || delta == 0)
        return;
      auto& state = m_states[&offset];
      synchronize(offset, state);
      if (precise) {
        offset = state.last = state.position = state.target = std::clamp(offset + delta, 0.0F, state.maximum);
        state.active = false;
        state.velocity = 0;
        return;
      }
      advance(state, now);
      const bool reversing = delta * (state.target - offset) < 0 || delta * state.velocity < 0;
      const float base = reversing ? offset : state.target;
      if (reversing) {
        state.position = offset;
        state.velocity = 0;
      }
      state.target = std::clamp(base + delta, 0.0F, state.maximum);
      state.sampled = now;
      state.active = std::abs(state.target - state.position) >= 0.01F;
      if (!state.active) {
        state.position = state.target;
        state.velocity = 0;
      }
    }
    bool tick(Clock::time_point now = Clock::now()) {
      bool active = false;
      for (auto& [offset, state] : m_states) {
        synchronize(*offset, state);
        advance(state, now);
        *offset = state.last = state.position;
        active |= state.active;
      }
      return active;
    }
    void stop(float& offset) {
      auto& state = m_states[&offset];
      state.active = false;
      state.velocity = 0;
      state.target = state.last = state.position = offset;
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
      float maximum = 0, target = 0, last = 0, position = 0, velocity = 0;
      Clock::time_point sampled{};
      bool active = false;
    };
    static void synchronize(float offset, State& state) {
      if (offset != state.last) {
        state.active = false;
        state.velocity = 0;
        state.position = state.target = state.last = offset;
      }
    }
    static void advance(State& state, Clock::time_point now) {
      if (!state.active)
        return;
      const float elapsed = std::max(0.0F, std::chrono::duration<float>(now - state.sampled).count());
      state.sampled = now;
      // Analytical integration gives identical motion across refresh rates.
      // ~95% of a notch is travelled in 132 milliseconds.
      constexpr float frequency = 36;
      const float displacement = state.position - state.target;
      const float coefficient = state.velocity + frequency * displacement;
      const float decay = std::exp(-frequency * elapsed);
      const float next = state.target + (displacement + coefficient * elapsed) * decay;
      state.velocity = (state.velocity - frequency * coefficient * elapsed) * decay;
      const float previous = state.position;
      state.position = std::clamp(next, std::min(previous, state.target), std::max(previous, state.target));
      if (next != state.position)
        state.velocity = 0;
      if (state.position == state.target
          || (std::abs(state.target - state.position) < 0.05F && std::abs(state.velocity) < 0.5F)) {
        state.active = false;
        state.velocity = 0;
        state.position = state.target;
      }
    }
    std::map<float*, State> m_states;
  };
} // namespace app
