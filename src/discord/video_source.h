#pragma once
#include "media/video_player.h"

#include <string_view>
namespace discord {
  // Fixed public media origins only; no Authorization, redirects or disk cache.
  std::shared_ptr<media::VideoInput> videoSource(std::string_view url);
} // namespace discord
