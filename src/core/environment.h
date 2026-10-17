#pragma once

#include <cstdlib>

namespace cressida {
  // An explicitly empty current override also takes precedence over the old name.
  inline const char* environment(const char* name, const char* legacyName) {
    if (const auto* value = std::getenv(name))
      return value;
    return std::getenv(legacyName);
  }
} // namespace cressida
