#pragma once

#include "app/theme_catalog.h"

#include <memory>
#include <optional>

namespace app {
  // Filesystem and desktop preference events return as immutable snapshots.
  // The worker never touches application state, native controls or the scene.
  class ThemeRuntime {
  public:
    ThemeRuntime();
    ~ThemeRuntime();
    ThemeRuntime(const ThemeRuntime&) = delete;
    ThemeRuntime& operator=(const ThemeRuntime&) = delete;
    ThemeSnapshot
    start(const std::filesystem::path& bundled, const std::filesystem::path& user, bool watch, bool desktop = true);
    int eventFd() const;
    std::optional<ThemeSnapshot> drain();

  private:
    struct Impl;
    std::unique_ptr<Impl> m;
  };
} // namespace app
