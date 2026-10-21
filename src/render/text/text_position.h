#pragma once

#include "render/core/mat3.h"

#include <algorithm>
#include <cmath>
#include <optional>

// Align the final glyph raster to physical pixels after ancestor translations.
// Apply the same rule while scrolling and at rest so settling keeps its phase.
// A legacy unsnapped transform remains unchanged.
inline Mat3
textQuadTransform(const Mat3& transform, float scale, bool pixelSnap, std::optional<float> smoothScrollY = {}) {
  auto result = transform;
  if ((pixelSnap || smoothScrollY) && std::abs(transform.m[1]) <= 0.0001F && std::abs(transform.m[3]) <= 0.0001F) {
    const float safeScale = std::isfinite(scale) && scale > 0 ? scale : 1.0F;
    const auto snap = [safeScale](double coordinate) {
      // Restore the text layout's 1/1024-pixel precision before choosing a
      // pixel so floating-point noise cannot flip a half-pixel rounding tie.
      const double phase = std::round(coordinate * safeScale * 1024.0) / 1024.0;
      return static_cast<float>(std::round(phase) / safeScale);
    };
    result.m[6] = snap(result.m[6]);
    result.m[7] = snap(result.m[7]);
  }
  return result;
}
