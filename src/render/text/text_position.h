#pragma once

#include "render/core/mat3.h"

#include <algorithm>
#include <cmath>
#include <optional>

// Align the stationary raster before applying a pane's exact scroll offset.
// Snapping the final moving position would quantize travel or make text recoil
// when an animation settles. A legacy unsnapped transform remains unchanged.
inline Mat3
textQuadTransform(const Mat3& transform, float scale, bool pixelSnap, std::optional<float> smoothScrollY = {}) {
  auto result = transform;
  if ((pixelSnap || smoothScrollY) && std::abs(transform.m[1]) <= 0.0001F && std::abs(transform.m[3]) <= 0.0001F) {
    const float safeScale = std::isfinite(scale) && scale > 0 ? scale : 1.0F;
    const float scroll = smoothScrollY && std::isfinite(*smoothScrollY) ? *smoothScrollY : 0;
    const auto snap = [safeScale](double coordinate) {
      // Restore the text layout's 1/1024-pixel precision before choosing a
      // pixel. Subtracting a moving float offset can otherwise move an exact
      // half-pixel tie across the rounding boundary on successive frames.
      const double phase = std::round(coordinate * safeScale * 1024.0) / 1024.0;
      return static_cast<float>(std::round(phase) / safeScale);
    };
    result.m[6] = snap(result.m[6]);
    result.m[7] = snap(static_cast<double>(result.m[7]) - scroll) + scroll;
  }
  return result;
}
