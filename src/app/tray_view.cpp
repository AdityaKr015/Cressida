#include "app/client_application.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace app {
  void ClientApplication::closeToTray() {
    // A window must remain reachable when the desktop has no tray host.
    if (!m_trayAvailable) {
      m_window.close();
      return;
    }
    saveSelection();
    finishSidebarResize();
    closeContext();
    closeSelectDropdown();
    m_controlInput.cancelPointerCapture();
    m_controlInput.setFocus(nullptr);
    m_dragDraft = m_dragMessage = m_dragImage = false;
    m_scrolling.stopAll();
    if (m_videoExpanded)
      setVideoExpanded(false);
    if (!m_videoSource.empty()) {
      m_video.pause(true);
      m_videoPlayRequested = false;
    }
    if (!m_soundPreviewSource.empty())
      m_soundPreview.pause(true);
    m_window.hide();
  }
  void ClientApplication::trayEvent(TrayEvent event) {
    if (m_demo && m_demoOverlay == "tray-cycle")
      std::clog << "Offline tray command " << static_cast<int>(event.command) << '\n';
    switch (event.command) {
    case TrayCommand::Availability:
      m_trayAvailable = event.available;
      if (!event.available && !m_window.visible())
        m_window.show();
      break;
    case TrayCommand::Open:
      m_window.show(std::move(event.activationToken));
      break;
    case TrayCommand::Restart:
      saveSelection();
      m_restartRequested = true;
      if (!m_demo)
        m_window.close();
      break;
    case TrayCommand::Close:
      m_restartRequested = false;
      m_window.close();
      break;
    }
  }
  void ClientApplication::demoTrayStep() {
    const auto require = [](bool value, const char* message) {
      if (!value)
        throw std::runtime_error(message);
    };
    const auto step = m_demoInteractionStep++;
    std::clog << "Offline tray lifecycle step " << step << '\n';
    require(m_window.visible(), "Only mapped windows present tray replay frames");
    if (step == 0) {
      m_drafts[m_channelId] = "Draft retained while hidden";
      m_window.closeRequested = [this] { closeToTray(); };
      trayEvent({.available = true});
      activate({.action = Action::Close});
      require(!m_window.visible() && !m_window.active(), "The window close button hides and unfocuses the window");
      m_trayDemoTimer.start(std::chrono::milliseconds(Style::animFast), [this, require] {
        const auto hiddenGeneration = m_window.toplevelGeneration();
        trayEvent({.command = TrayCommand::Open});
        const auto restoredGeneration = m_window.toplevelGeneration();
        require(restoredGeneration == hiddenGeneration + 1, "A genuinely hidden window recreates its shell role once");
        require(!m_window.active(), "Restored surface has not yet received desktop focus");
        trayEvent({.command = TrayCommand::Open});
        desktopNoticeAction(
            {.account = m_account.id, .guild = m_guildId, .channel = m_channelId, .generation = m_session}
        );
        require(
            m_window.toplevelGeneration() == restoredGeneration,
            "Tray and notification activation of an unfocused visible window must preserve its native role"
        );
      });
    } else if (step == 1) {
      const auto generation = m_window.toplevelGeneration();
      trayEvent({.command = TrayCommand::Open, .activationToken = "offline-activation-token"});
      trayEvent({.command = TrayCommand::Open});
      desktopNoticeAction(
          {.account = m_account.id, .guild = m_guildId, .channel = m_channelId, .generation = m_session}
      );
      require(
          m_window.toplevelGeneration() == generation,
          "Repeated activation of a presented window preserves its native role"
      );
      require(
          m_trayHiddenTicks > 0 && m_connected && m_drafts.at(m_channelId) == "Draft retained while hidden",
          "Hidden windows keep the owning event loop, connection and draft alive"
      );
      m_window.requestClose();
      require(!m_window.visible(), "Compositor close requests use the same hide path");
      m_trayDemoTimer.start(std::chrono::milliseconds(Style::animFast), [this] { trayEvent({.available = false}); });
    } else if (step == 2) {
      require(!m_trayAvailable && m_window.visible(), "Losing the tray host restores a hidden window");
      trayEvent({.available = true});
      m_window.requestClose();
      m_trayDemoTimer.start(std::chrono::milliseconds(Style::animFast), [this] {
        trayEvent({.command = TrayCommand::Open});
      });
    } else if (step == 3) {
      trayEvent({.command = TrayCommand::Restart});
      require(m_restartRequested, "Restart requests an orderly relaunch independently of window hiding");
      m_restartRequested = false;
      require(
          m_drafts.at(m_channelId) == "Draft retained while hidden", "Repeated hide and restore never clears the draft"
      );
      std::clog << "Offline tray focus without remapping, hidden restore, notification activation, host loss, draft "
                   "and restart replay passed\n";
      trayEvent({.command = TrayCommand::Close});
    }
    m_window.invalidate();
  }
} // namespace app
