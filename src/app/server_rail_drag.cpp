#include "app/client_application.h"
#include "app/theme.h"

#include <cmath>
#include <limits>
#include <utility>

namespace app {
  bool ClientApplication::beginRailDrag(const Hit& hit) {
    if ((hit.action != Action::Guild && hit.action != Action::Folder) || !sidebarResizeAvailable())
      return false;
    if (m_railSaving)
      return false;
    m_railDrag = RailDrag{.source = hit, .x = m_pointerX, .y = m_pointerY, .revision = m_guildLayoutRevision};
    m_scrolling.stop(m_guildScroll);
    return true;
  }
  bool ClientApplication::railDragMotion() {
    if (!m_railDrag)
      return false;
    auto& drag = *m_railDrag;
    const bool sourceExists = drag.source.action == Action::Folder
        ? std::ranges::find(m_folders, drag.source.id, &api::GuildFolder::id) != m_folders.end()
        : std::ranges::find(m_guilds, drag.source.id, &api::Guild::id) != m_guilds.end();
    if (!sidebarResizeAvailable() || drag.revision != m_guildLayoutRevision || !sourceExists) {
      finishRailDrag(true);
      return true;
    }
    if (!drag.active && std::hypot(m_pointerX - drag.x, m_pointerY - drag.y) >= Style::dragStartThreshold) {
      drag.active = true;
      drag.tick = FolderMotion::Clock::now();
      m_controlInput.cancelPointerCapture();
    }
    drag.drop.reset();
    if (!drag.active)
      return true;
    using R = layout::ServerRail;
    if (m_pointerX < layout::Floating::inset
        || m_pointerX >= layout::Sidebar::railWidth - layout::Floating::inset
        || m_pointerY < R::visibleTop
        || m_pointerY >= m_railBottom)
      return true;
    const Hit* target = nullptr;
    float distance = std::numeric_limits<float>::max();
    for (const auto& hit : m_railTargets) {
      const auto current = std::abs(m_pointerY - (hit.y + hit.height / 2));
      if (current < distance) {
        distance = current;
        target = &hit;
      }
    }
    if (!target)
      return true;
    const bool sourceFolder = drag.source.action == Action::Folder;
    const bool targetFolder = target->action == Action::Folder;
    api::RailPlacement placement =
        m_pointerY < target->y + target->height / 2 ? api::RailPlacement::Before : api::RailPlacement::After;
    if (!sourceFolder
        && m_pointerY >= target->y + target->height * R::dropEdgeFraction
        && m_pointerY < target->y + target->height * (1 - R::dropEdgeFraction))
      placement = api::RailPlacement::Into;
    api::GuildRailMove move{drag.source.id, target->id, sourceFolder, targetFolder, placement};
    if (!sourceFolder && !targetFolder) {
      const auto parent = std::ranges::find_if(m_folders, [&](const auto& folder) {
        return !folder.id.empty() && std::ranges::find(folder.guildIds, target->id) != folder.guildIds.end();
      });
      if (parent != m_folders.end()
          && (m_pointerX < R::folderX
              || m_pointerX >= R::folderX + R::folderSize
              || m_pointerY >= target->y + target->height + R::folderGap / 2)) {
        move.target = parent->id;
        move.targetFolder = true;
        move.placement = api::RailPlacement::After;
      }
    }
    if (!api::validRailMove(move))
      return true;
    if (sourceFolder && !targetFolder) {
      const auto parent = std::ranges::find_if(m_folders, [&](const auto& folder) {
        return std::ranges::find(folder.guildIds, target->id) != folder.guildIds.end();
      });
      if (parent != m_folders.end() && parent->id == drag.source.id)
        return true;
      if (parent != m_folders.end() && !parent->id.empty()) {
        move.target = parent->id;
        move.targetFolder = true;
      }
    }
    drag.target = *target;
    if (move.targetFolder)
      if (const auto header = std::ranges::find_if(
              m_railTargets, [&](const auto& h) { return h.action == Action::Folder && h.id == move.target; }
          );
          header != m_railTargets.end())
        drag.target = *header;
    drag.drop = std::move(move);
    return true;
  }
  bool ClientApplication::tickRailDrag() {
    if (!m_railDrag)
      return false;
    if ((!m_demo && !m_window.active()) || !sidebarResizeAvailable()) {
      finishRailDrag(true);
      return false;
    }
    railDragMotion();
    if (!m_railDrag || !m_railDrag->active)
      return false;
    auto& drag = *m_railDrag;
    using R = layout::ServerRail;
    const auto now = FolderMotion::Clock::now();
    const auto elapsed = std::min(
        std::chrono::duration<float>(now - drag.tick).count(),
        std::chrono::duration<float>(FolderMotion::duration).count()
    );
    drag.tick = now;
    if (m_pointerX >= 0 && m_pointerX < layout::Sidebar::railWidth) {
      const float direction = m_pointerY < R::visibleTop + R::autoScrollEdge ? -1
          : m_pointerY > m_railBottom - R::autoScrollEdge                    ? 1
                                                                             : 0;
      m_guildScroll = std::max(0.0F, m_guildScroll + direction * R::autoScrollSpeed * elapsed);
    }
    const std::string folder = drag.drop && drag.drop->targetFolder && drag.drop->placement == api::RailPlacement::Into
        ? drag.drop->target
        : "";
    if (folder != drag.hoverFolder) {
      drag.hoverFolder = folder;
      drag.hover = now;
    } else if (
        !folder.empty()
        && !m_openFolders.contains(folder)
        && now - drag.hover >= std::chrono::milliseconds(Style::animSlow)
    ) {
      m_openFolders.insert(folder);
      saveExpandedFolders();
    }
    return true;
  }
  void ClientApplication::finishRailDrag(bool cancel) {
    if (!m_railDrag)
      return;
    auto drag = std::move(*m_railDrag);
    m_railDrag.reset();
    if (!cancel && drag.revision == m_guildLayoutRevision && sidebarResizeAvailable()) {
      if (!drag.active) {
        if (drag.source.contains(m_pointerX, m_pointerY))
          activate(drag.source);
      } else if (drag.drop) {
        api::Interaction action{.kind = api::InteractionKind::ReorderGuilds, .railMove = drag.drop};
        action.guild = drag.source.id;
        if (drag.source.action == Action::Folder) {
          const auto folder = std::ranges::find(m_folders, drag.source.id, &api::GuildFolder::id);
          if (folder == m_folders.end() || folder->guildIds.empty())
            return;
          action.guild = folder->guildIds.front();
        }
        if (m_demo)
          m_demoRailMove = drag.drop;
        else
          m_railSaving = startInteraction(std::move(action));
      }
    }
    updateCursor();
    m_window.invalidate();
  }
  void ClientApplication::drawRailDrag(Node& root) {
    if (!m_railDrag || !m_railDrag->active)
      return;
    railDragMotion();
    if (!m_railDrag)
      return;
    using R = layout::ServerRail;
    const auto& drag = *m_railDrag;
    if (drag.drop) {
      const auto& target = drag.target;
      if (drag.drop->placement == api::RailPlacement::Into) {
        const float stroke = std::min(R::dropStroke, target.height / 2);
        rectangle(root, R::folderX, target.y, R::folderSize, stroke, theme::accent, Style::radiusXs);
        rectangle(
            root, R::folderX, target.y + target.height - stroke, R::folderSize, stroke, theme::accent, Style::radiusXs
        );
        rectangle(root, R::folderX, target.y, R::dropStroke, target.height, theme::accent, Style::radiusXs);
        rectangle(
            root, R::folderX + R::folderSize - R::dropStroke, target.y, R::dropStroke, target.height, theme::accent,
            Style::radiusXs
        );
      } else {
        float y = drag.drop->placement == api::RailPlacement::Before ? target.y : target.y + target.height;
        // A folder edge brackets the whole group, including its visible children.
        if (drag.drop->targetFolder && drag.drop->placement == api::RailPlacement::After)
          for (const auto& folder : m_folders)
            if (folder.id == drag.drop->target)
              for (const auto& child : m_railTargets)
                if (child.action == Action::Guild
                    && std::ranges::find(folder.guildIds, child.id) != folder.guildIds.end())
                  y = std::max(y, child.y + child.height);
        y = std::clamp(y, R::visibleTop + R::dropStroke / 2, m_railBottom - R::dropStroke / 2);
        rectangle(
            root, R::folderX, y - R::dropStroke / 2, R::folderSize, R::dropStroke, theme::accent, Style::radiusXs
        );
      }
    }
    // The existing avatar/icon cache supplies the drag preview, with no new
    // decoding or pointer target layered above the rail.
    if (m_pointerX < layout::Sidebar::railWidth && m_pointerY >= R::visibleTop && m_pointerY < m_railBottom) {
      const float size = R::folderGlyphSize;
      const float x = std::clamp(
          m_pointerX + Style::spaceXs, R::folderX, layout::Sidebar::railWidth - size - layout::Floating::inset
      );
      const float y = std::clamp(m_pointerY + Style::spaceXs, R::visibleTop, m_railBottom - size);
      if (drag.source.action == Action::Folder)
        glyph(root, x, y, size, "folder-filled", theme::accent);
      else if (const auto guild = std::ranges::find(m_guilds, drag.source.id, &api::Guild::id); guild != m_guilds.end())
        avatar(root, x, y, size, guild->icon, guild->name);
    }
  }
} // namespace app
