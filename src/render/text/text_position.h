#pragma once

#include "render/core/mat3.h"

#include <algorithm>
#include <cmath>

// Static text aligns to physical pixels for sharpness. Moving panes retain the
// exact transform so text and images advance together, including HiDPI scales.
inline Mat3 textQuadTransform(const Mat3& transform, float scale, bool pixelSnap) {
  auto result = transform;
  if (pixelSnap && std::abs(transform.m[1]) <= 0.0001F && std::abs(transform.m[3]) <= 0.0001F) {
    const float safeScale = std::max(1.0F, scale);
    result.m[6] = std::round(result.m[6] * safeScale) / safeScale;
    result.m[7] = std::round(result.m[7] * safeScale) / safeScale;
  }
  return result;
}
