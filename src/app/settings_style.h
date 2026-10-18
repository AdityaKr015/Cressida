#pragma once
#include "render/scene/text_node.h"

namespace app::settings {
  struct TextStyle {
    float size;
    FontWeight weight;
  };
  inline constexpr TextStyle pageTitle{22, FontWeight::Bold};
  inline constexpr TextStyle section{12, FontWeight::SemiBold};
  inline constexpr TextStyle rowTitle{13, FontWeight::SemiBold};
  inline constexpr TextStyle description{12, FontWeight::Normal};
  inline constexpr TextStyle navigationHeading{10, FontWeight::SemiBold};
  inline constexpr TextStyle navigationTitle{11, FontWeight::SemiBold};
  inline constexpr TextStyle control{12, FontWeight::Normal};
} // namespace app::settings
