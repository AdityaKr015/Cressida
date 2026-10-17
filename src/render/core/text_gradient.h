#pragma once

#include "render/core/color.h"
#include <array>

// A horizontal gradient over the shaped text's logical width. Phase is a
// normalized mirrored sweep; changing it never invalidates glyph raster caches.
struct TextGradient {
  std::array<Color, 3> colors{};
  int count = 2;
  float phase = 0;
  bool operator==(const TextGradient&) const = default;
};
