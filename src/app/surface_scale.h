#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace app {

  struct SurfaceGeometry {
    int bufferWidth, bufferHeight;
    int logicalWidth, logicalHeight;
    int bufferScale;
    float contentScale;
  };

  // Monitor density sizes the native buffer; application zoom only changes the
  // coordinate space painted into it. Fractional preferences require a viewport.
  class SurfaceScale {
  public:
    void setFractionalAvailable(bool available) { m_fractionalAvailable = available; }

    void preferredFractional(uint32_t numerator) {
      if (numerator >= 1 && numerator <= 960)
        m_fractionalNumerator = numerator;
    }

    void preferredInteger(int scale) {
      if (validInteger(scale))
        m_preferredInteger = scale;
    }

    void output(uint32_t registryId, int scale) {
      if (validInteger(scale))
        m_outputs[registryId].scale = scale;
    }

    void enter(uint32_t id) {
      if (auto it = m_outputs.find(id); it != m_outputs.end())
        it->second.entered = true;
    }

    void leave(uint32_t id) {
      if (auto it = m_outputs.find(id); it != m_outputs.end())
        it->second.entered = false;
    }

    void remove(uint32_t id) { m_outputs.erase(id); }

    float factor() const { return static_cast<float>(density()); }
    bool usesViewport() const { return m_fractionalAvailable; }

    SurfaceGeometry geometry(int width, int height, float uiScale) const {
      width = std::max(1, width);
      height = std::max(1, height);
      const double zoom = std::isfinite(uiScale) ? std::clamp(uiScale, 0.67F, 1.5F) : 1.0F;
      const double scale = density();
      const auto dimension = [](double value) {
        return static_cast<int>(std::clamp(value, 1.0, static_cast<double>(std::numeric_limits<int>::max())));
      };
      return {
          .bufferWidth = dimension(std::round(width * scale)),
          .bufferHeight = dimension(std::round(height * scale)),
          .logicalWidth = dimension(std::floor(width / zoom)),
          .logicalHeight = dimension(std::floor(height / zoom)),
          .bufferScale = usesViewport() ? 1 : static_cast<int>(scale),
          .contentScale = static_cast<float>(scale * zoom),
      };
    }

  private:
    struct Output {
      int scale = 1;
      bool entered = false;
    };

    static bool validInteger(int scale) { return scale >= 1 && scale <= 8; }

    double density() const {
      if (m_fractionalAvailable && m_fractionalNumerator)
        return static_cast<double>(m_fractionalNumerator) / 120.0;
      if (m_preferredInteger)
        return m_preferredInteger;
      int scale = 1;
      for (const auto& [id, output] : m_outputs)
        if (output.entered)
          scale = std::max(scale, output.scale);
      return scale;
    }

    bool m_fractionalAvailable = false;
    uint32_t m_fractionalNumerator = 0;
    int m_preferredInteger = 0;
    std::unordered_map<uint32_t, Output> m_outputs;
  };

} // namespace app
