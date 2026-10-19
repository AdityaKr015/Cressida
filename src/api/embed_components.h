#pragma once

#include "api/models.h"

namespace api {
  // A bounded, render-only projection of Discord's link-preview components.
  // Only link buttons are accepted; previews cannot invoke app interactions.
  std::vector<EmbedComponent> parseEmbedComponents(const nlohmann::json& value);
} // namespace api
