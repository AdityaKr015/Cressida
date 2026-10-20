#include "app/client_application.h"
#include "render/scene/rect_node.h"

#include <iostream>
#include <linux/input-event-codes.h>
#include <stdexcept>

namespace app {
  void ClientApplication::demoFolderAnimationStep() {
    using R = layout::ServerRail;
    const auto require = [](bool ok, const char* reason) {
      if (!ok)
        throw std::runtime_error(reason);
    };
    const auto hit = [&](Action action, const std::string& id) -> Hit {
      const auto at =
          std::ranges::find_if(m_hits, [&](const auto& item) { return item.action == action && item.id == id; });
      require(at != m_hits.end(), "Animated rail retains the visible folder/server hit");
      return *at;
    };
    const auto click = [&] {
      const auto header = hit(Action::Folder, "10");
      const auto x = header.x + header.width / 2;
      const auto y = header.y + header.height / 2 + m_window.titlebarHeight() - layout::Window::titlebarHeight;
      m_window.motion(x, y);
      m_window.click(x, y, BTN_LEFT);
      m_window.released();
    };
    const auto expanded = [&] {
      const auto header = hit(Action::Folder, "10");
      require(header.height == R::folderHeaderHeight, "Open folders have compact glyph headers");
      const auto first = hit(Action::Guild, "1"), second = hit(Action::Guild, "2");
      require(
          first.y == header.y + header.height && second.y - first.y == R::folderChildStep,
          "Expanded children share compact measured positions"
      );
      const auto frame = R::folderFrame(2, 1);
      int backgrounds = 0;
      const auto inspect = [&](auto&& self, const Node& node) -> void {
        if (node.type() == NodeType::Rect && node.width() == R::folderSize && node.height() == frame.height)
          ++backgrounds;
        for (const auto& child : node.children())
          self(self, *child);
      };
      inspect(inspect, *m_scene);
      require(backgrounds == 1, "One continuous surface encloses the open folder and its children");
    };
    const auto step = m_demoInteractionStep++;
    if (step == 0) {
      expanded();
      click();
      require(
          !m_openFolders.contains("10") && m_folderMotion.active(),
          "Native clicks begin closing without losing persisted state"
      );
    } else if (step == 1) {
      require(m_folderMotion.active(), "The compositor-paced close keeps its children while sliding");
    } else if (step == 2) {
      const auto header = hit(Action::Folder, "10"), child = hit(Action::Guild, "2"),
                 following = hit(Action::Guild, "3");
      require(
          header.height > R::folderHeaderHeight
              && header.height < R::folderSize
              && child.height > 0
              && child.height < layout::Avatar::rail
              && following.y < R::top + R::folderFrame(2, 1).height + R::folderGap,
          "Intermediate frames slide following servers and clip partially hidden child hits"
      );
      click();
    } else if (step == 3) {
      require(
          m_openFolders.contains("10") && m_folderMotion.active(),
          "A click during closing reverses the visible transition"
      );
    } else if (step == 4) {
      m_preferences.reducedMotion = true;
      click();
    } else if (step == 5) {
      require(
          !m_folderMotion.active() && hit(Action::Folder, "10").height == R::folderSize,
          "Reduced motion collapses directly to the miniature header"
      );
      require(
          std::ranges::none_of(
              m_hits,
              [](const auto& item) { return item.action == Action::Guild && (item.id == "1" || item.id == "2"); }
          ),
          "Collapsed children have no selectable hidden targets"
      );
      click();
    } else if (step == 6) {
      expanded();
      m_preferences.reducedMotion = false;
      m_openFolders.clear();
      saveExpandedFolders();
      require(m_folderMotion.active(), "Close All Folders uses the same animated targets");
    } else if (step == 7) {
      std::erase_if(m_folders, [](const auto& folder) { return folder.id == "10"; });
    } else if (step == 8) {
      require(
          m_folderMotion.size() == 0 && !m_folderMotion.active(), "Removed folders release in-flight animation state"
      );
      require(
          std::ranges::none_of(m_hits, [](const auto& item) { return item.action == Action::Folder; }),
          "Removed folder headers cannot retain raced click targets"
      );
      std::clog << "Offline folder surfaces, compact headers, sliding positions, clipped hits, reversal, reduced "
                   "motion and removal passed\n";
    }
    m_window.invalidate();
  }
} // namespace app
