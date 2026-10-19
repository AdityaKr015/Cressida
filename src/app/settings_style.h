#pragma once
#include "app/layout_metrics.h"
#include "render/scene/text_node.h"

namespace app::settings {
  struct TextStyle {
    float size;
    FontWeight weight;
  };
  inline constexpr TextStyle pageTitle{Style::fontSizePage, FontWeight::Bold};
  inline constexpr TextStyle section{Style::fontSizeDescription, FontWeight::SemiBold};
  inline constexpr TextStyle rowTitle{Style::fontSizeCaption, FontWeight::SemiBold};
  inline constexpr TextStyle description{Style::fontSizeDescription, FontWeight::Normal};
  inline constexpr TextStyle navigationHeading{Style::fontSizeDetail, FontWeight::SemiBold};
  inline constexpr TextStyle navigationTitle{Style::fontSizeMini, FontWeight::SemiBold};
  inline constexpr TextStyle control{Style::fontSizeDescription, FontWeight::Normal};
} // namespace app::settings
