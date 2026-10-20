#pragma once

#include <cstdint>

namespace app {
  // xdg-decoration modes. Only a subsequent xdg_surface configure makes a
  // pending compositor choice effective; a client request never changes it.
  class WindowDecorations {
  public:
    enum class Mode : std::uint32_t { Client = 1, Server = 2 };
    bool preferDesktop(bool enabled) {
      if (m_desktop == enabled)
        return false;
      m_desktop = enabled;
      return true;
    }
    bool desktopPreferred() const { return m_desktop; }
    Mode requested() const { return m_desktop ? Mode::Server : Mode::Client; }
    void configure(std::uint32_t mode) {
      if (mode == static_cast<std::uint32_t>(Mode::Client) || mode == static_cast<std::uint32_t>(Mode::Server))
        m_pending = static_cast<Mode>(mode);
    }
    void surfaceConfigured() { m_effective = m_pending; }
    bool clientSide() const { return m_effective == Mode::Client; }
    bool needsTitlebar(bool fullscreen) const { return !fullscreen && clientSide(); }

  private:
    bool m_desktop = true;
    Mode m_pending = Mode::Client, m_effective = Mode::Client;
  };
} // namespace app
