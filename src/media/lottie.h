#pragma once

#include "media/animation.h"

namespace media {
  // Native, memory-only rendering of bounded vector Lottie stickers. External
  // images/fonts/resources, text, expressions and cyclic precompositions are
  // rejected before rlottie sees the JSON. Results use ordinary packed RGBA
  // Animation frames and the existing visible-media playback clock.
  std::shared_ptr<const Animation> decodeLottie(std::span<const std::uint8_t> bytes, std::stop_token stop = {});
} // namespace media
