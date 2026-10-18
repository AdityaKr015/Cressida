#pragma once

#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

namespace api {
  struct Gif {
    std::string id, title, url, src, preview;
    int width = 0, height = 0;
    unsigned format = 1, order = 0;
  };
  bool validGif(const Gif& gif);
  std::string gifSearchPath(const std::string& query);
  std::vector<Gif> parseGifs(const nlohmann::json& value);
  std::vector<Gif> parseGifFavorites(const nlohmann::json& value);
  nlohmann::json gifFavoritePatch(const nlohmann::json& snapshot, const Gif& gif, bool add);
  nlohmann::json gifJson(const Gif& gif);
  Gif gifFromJson(const nlohmann::json& value);
} // namespace api
