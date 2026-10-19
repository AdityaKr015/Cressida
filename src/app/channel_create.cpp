#include "app/client_application.h"
#include "app/theme.h"
#include "render/scene/rect_node.h"
#include "ui/controls/toggle.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <glib.h>

namespace app {
  using namespace theme;
  namespace {
    bool createMatches(const std::string& text, const std::string& query) {
      auto* foldedText = g_utf8_casefold(text.data(), text.size());
      auto* foldedQuery = g_utf8_casefold(query.data(), query.size());
      const bool found = foldedText && foldedQuery && std::string_view(foldedText).contains(foldedQuery);
      g_free(foldedText);
      g_free(foldedQuery);
      return found;
    }
  } // namespace
  void ClientApplication::openChannelCreate(api::Interaction action) {
    closeDialog();
    m_dialogKind = "sidebar-create";
    m_dialogTitle = action.channelType == 4 ? "Create Category" : "Create Channel";
    m_dialogAction = std::move(action);
    m_dialogText.clear();
    m_dialogError.clear();
    m_sidebarInviteUrl.clear();
    m_channelCreateFilter.clear();
    m_channelCreateAccessStep = false;
    m_scrolling.stop(m_channelCreateScroll);
    m_channelCreateScroll = 0;
    m_focusedInput = "dialog";
    const auto permissions = m_dialogAction->parentId && !m_dialogAction->parentId->empty()
        ? serverChannelPermissions(m_dialogAction->guild, *m_dialogAction->parentId)
        : serverGuildPermissions(m_dialogAction->guild);
    if (!permissions)
      m_dialogError = "Server permissions are not available yet. Reopen this action after the server finishes loading.";
    else if (!(*permissions & api::manageChannel))
      m_dialogError = "You no longer have permission to create channels in this server.";
    m_window.invalidate();
  }

  bool ClientApplication::channelCreateAction(const Hit& hit) {
    if (hit.action != Action::ChannelCreateType
        && hit.action != Action::ChannelCreatePrivate
        && hit.action != Action::ChannelCreateBack
        && hit.action != Action::ChannelCreateAccess)
      return false;
    if (m_dialogKind != "sidebar-create"
        || !m_dialogAction
        || m_dialogPending
        || m_dialogAction->kind != api::InteractionKind::CreateChannel)
      return true;
    if (hit.action == Action::ChannelCreateType) {
      int type = -1;
      const auto parsed = std::from_chars(hit.id.data(), hit.id.data() + hit.id.size(), type);
      const auto guild = std::ranges::find(m_guilds, m_dialogAction->guild, &api::Guild::id);
      const bool community =
          guild != m_guilds.end() && std::ranges::find(guild->features, "COMMUNITY") != guild->features.end();
      if (parsed.ec == std::errc{} && (type == 0 || type == 2 || (type == 15 && community)))
        m_dialogAction->channelType = type;
    } else if (hit.action == Action::ChannelCreatePrivate) {
      const auto permissions = m_dialogAction->parentId && !m_dialogAction->parentId->empty()
          ? serverChannelPermissions(m_dialogAction->guild, *m_dialogAction->parentId).value_or(0)
          : serverGuildPermissions(m_dialogAction->guild).value_or(0);
      if (permissions & api::managePermissions) {
        m_dialogAction->privateChannel = !m_dialogAction->privateChannel;
        m_dialogAction->channelAccess.clear();
      }
    } else if (hit.action == Action::ChannelCreateBack) {
      m_channelCreateAccessStep = false;
      m_focusedInput = "dialog";
    } else if (hit.id.size() > 2 && (hit.id.starts_with("0:") || hit.id.starts_with("1:"))) {
      const auto id = hit.id.substr(2);
      const auto type = hit.id[0] - '0';
      const auto roles = m_roleDirectory.allRoles(m_dialogAction->guild);
      const bool target = type == 1 || std::ranges::find(roles, id, &api::ColoredRole::id) != roles.end();
      if (target && api::validSnowflake(id) && id != m_dialogAction->guild && id != m_account.id) {
        auto& access = m_dialogAction->channelAccess;
        const auto found =
            std::ranges::find_if(access, [&](const auto& entry) { return entry.id == id && entry.type == type; });
        if (found != access.end())
          access.erase(found);
        else if (access.size() < 100)
          access.push_back({id, "1024", "0", type});
      }
    }
    m_dialogError.clear();
    m_window.invalidate();
    return true;
  }

  void ClientApplication::drawChannelCreate(Node& root, float w, float h) {
    if (!m_dialogAction)
      return;
    auto& action = *m_dialogAction;
    const bool category = action.channelType == 4;
    const auto guild = std::ranges::find(m_guilds, action.guild, &api::Guild::id);
    const bool community =
        guild != m_guilds.end() && std::ranges::find(guild->features, "COMMUNITY") != guild->features.end();
    const float width = std::min(448.0F, w - 32);
    const float errorHeight = m_dialogError.empty() ? 0 : 30;
    const float fullHeight = (m_channelCreateAccessStep ? 460 : category ? 298 : community ? 496 : 444) + errorHeight;
    const float height = std::min(fullHeight, h - 60);
    const float x = (w - width) / 2, y = std::max(42.0F, (h - height + 36) / 2);
    rectangle(root, 0, 36, w, h - 36, Color{0, 0, 0, .65F});
    m_hits.push_back({0, 36, w, h - 36, Action::None, {}});
    auto surface = std::make_unique<RectNode>();
    surface->setPosition(x, y);
    surface->setSize(width, height);
    surface->setStyle({.fill = background, .border = raised, .radius = 16, .borderWidth = 1});
    root.addChild(std::move(surface));
    m_dialogBounds = {x, y, width, height, Action::None, {}};
    m_hits.push_back(m_dialogBounds);
    label(
        root, x + 24, y + 23, width - 76,
        m_channelCreateAccessStep ? "Add members or roles"
            : category            ? "Create Category"
                                  : "Create Channel",
        18, foreground, FontWeight::SemiBold
    );
    iconButton(
        root, {x + width - 42, y + 14, 28, 28, m_dialogPending ? Action::None : Action::CloseDialog, {}}, "x", "Close"
    );
    if (action.parentId && !m_channelCreateAccessStep) {
      const auto* parent = serverChannel(action.guild, *action.parentId);
      if (parent)
        label(root, x + 24, y + 49, width - 48, "in " + parent->name, 11, muted);
    }
    const float contentY = y + 72, viewport = height - 142 - errorHeight;
    auto clip = std::make_unique<Node>();
    clip->setPosition(x + 24, contentY);
    clip->setSize(width - 48, viewport);
    clip->setClipChildren(true);
    auto body = std::make_unique<Node>();
    const float previousScroll = m_channelCreateScroll;
    body->setPosition(0, -previousScroll);
    auto& page = *body;
    const float inner = width - 48;
    const auto firstHit = m_hits.size();
    const auto bordered = [&](float top, float boxHeight, Color fill, bool focus = false) {
      auto box = std::make_unique<RectNode>();
      box->setPosition(0, top);
      box->setSize(inner, boxHeight);
      box->setStyle({.fill = fill, .border = focus ? withAlpha(accent, .65F) : raised, .radius = 7, .borderWidth = 1});
      page.addChild(std::move(box));
    };
    float cursor = 0;
    if (m_channelCreateAccessStep) {
      label(
          page, 0, 0, inner, "Only selected members and roles can view this channel.", 12, muted, FontWeight::Normal, 2
      );
      bordered(39, 36, sidebar);
      auto& search = inputControl(
          page, 0, 39, inner, 36, "channel-create-access", m_channelCreateFilter, "Search members or roles"
      );
      search.setFrameVisible(false);
      search.setEnabled(!m_dialogPending);
      cursor = 91;
      struct Target {
        std::string id, name, avatar;
        int type;
        std::uint32_t color = 0;
      };
      std::vector<Target> targets;
      for (const auto& role : m_roleDirectory.allRoles(action.guild))
        if (role.id != action.guild
            && (m_channelCreateFilter.empty() || createMatches(role.name, m_channelCreateFilter)))
          targets.push_back({role.id, role.name, {}, 0, role.colors.primary});
      const auto addMembers = [&](const auto& members) {
        for (const auto& member : members)
          if (!member.user.id.empty()
              && member.user.id != m_account.id
              && (m_channelCreateFilter.empty() || createMatches(member.user.name, m_channelCreateFilter)))
            targets.push_back({member.user.id, userName(member.user.id, member.user.name), member.user.avatar, 1});
      };
      if (!m_dmMode && action.guild == m_guildId)
        addMembers(m_members);
      else
        for (const auto& channel : serverChannels(action.guild))
          if (const auto cached = m_channelMembers.find(channel.id); cached != m_channelMembers.end())
            addMembers(cached->second.rows);
      if (api::validSnowflake(m_channelCreateFilter) && m_channelCreateFilter != m_account.id)
        targets.push_back({m_channelCreateFilter, userName(m_channelCreateFilter, m_channelCreateFilter), {}, 1});
      std::set<std::pair<int, std::string>> seen;
      int lastType = -1;
      for (const auto& target : targets) {
        if (!seen.emplace(target.type, target.id).second)
          continue;
        if (lastType != target.type) {
          label(page, 0, cursor, inner, target.type ? "Members" : "Roles", 11, muted, FontWeight::SemiBold);
          cursor += 25;
          lastType = target.type;
        }
        const bool selected = std::ranges::any_of(action.channelAccess, [&](const auto& entry) {
          return entry.id == target.id && entry.type == target.type;
        });
        const Hit hit{
            x + 24,
            contentY + cursor - previousScroll,
            inner,
            38,
            m_dialogPending ? Action::None : Action::ChannelCreateAccess,
            std::to_string(target.type) + ":" + target.id
        };
        if (hovered(hit.x, hit.y, hit.width, hit.height) || selected)
          rectangle(page, 0, cursor, inner, 38, selected ? withAlpha(accent, .10F) : sidebar, 6);
        if (target.type)
          avatar(page, 8, cursor + 6, 26, target.avatar, target.name);
        else
          glyph(page, 12, cursor + 11, 16, "circle-filled", target.color ? rgbHex(target.color) : muted);
        label(page, 44, cursor + 10, inner - 80, target.name, 13, foreground);
        glyph(page, inner - 28, cursor + 11, 16, selected ? "checkbox" : "square", selected ? accent : muted);
        m_hits.push_back(hit);
        cursor += 40;
      }
      if (targets.empty()) {
        label(page, 0, cursor + 10, inner, "No matching members or roles.", 12, muted);
        cursor += 42;
      }
    } else {
      if (!category) {
        label(page, 0, 0, inner, "Channel Type", 13, foreground, FontWeight::Medium);
        cursor = 26;
        struct Choice {
          int type;
          const char* icon;
          const char* name;
          const char* description;
        };
        const std::array choices{
            Choice{0, "hash", "Text", "Send messages, images, GIFs, emoji, opinions and puns"},
            Choice{2, "volume", "Voice", "Hang out together with voice, video and screen share"},
            Choice{15, "messages", "Forum", "Create a space for organised discussions"}
        };
        for (const auto& choice : choices) {
          if (choice.type == 15 && !community)
            continue;
          const Hit hit{
              x + 24,
              contentY + cursor - previousScroll,
              inner,
              52,
              m_dialogPending ? Action::None : Action::ChannelCreateType,
              std::to_string(choice.type)
          };
          if (hovered(hit.x, hit.y, hit.width, hit.height))
            rectangle(page, -4, cursor - 3, inner + 8, 52, withAlpha(sidebar, .7F), 6);
          glyph(
              page, 0, cursor + 3, 19, action.channelType == choice.type ? "circle-dot" : "circle",
              action.channelType == choice.type ? accent : muted
          );
          glyph(page, 30, cursor + 2, 20, choice.icon, foreground);
          label(page, 55, cursor + 4, inner - 55, choice.name, 13, foreground);
          label(page, 30, cursor + 25, inner - 30, choice.description, 11, muted, FontWeight::Normal, 2);
          m_hits.push_back(hit);
          cursor += 54;
        }
        cursor += 9;
      }
      label(page, 0, cursor, inner, category ? "Category Name" : "Channel Name", 13, foreground, FontWeight::Medium);
      cursor += 25;
      bordered(cursor, 36, sidebar, m_focusedInput == "dialog");
      glyph(page, 11, cursor + 9, 18, category ? "folder" : action.channelType == 2 ? "volume" : "hash", muted);
      auto& name = inputControl(
          page, 33, cursor, inner - 33, 36, "dialog", m_dialogText, category ? "New category" : "new-channel"
      );
      name.setFrameVisible(false);
      name.setHorizontalPadding(3);
      name.setEnabled(!m_dialogPending);
      cursor += 57;
      glyph(page, 0, cursor + 1, 15, "lock", muted);
      label(
          page, 20, cursor, inner - 76, category ? "Private Category" : "Private Channel", 13, foreground,
          FontWeight::Medium
      );
      const auto permissions = action.parentId && !action.parentId->empty()
          ? serverChannelPermissions(action.guild, *action.parentId).value_or(0)
          : serverGuildPermissions(action.guild).value_or(0);
      const bool canPrivate = permissions & api::managePermissions;
      const std::string key = "toggle:channel-create-private";
      std::unique_ptr<Node> saved;
      if (auto old = m_detachedInputs.find(key); old != m_detachedInputs.end()) {
        saved = std::move(old->second);
        m_detachedInputs.erase(old);
      }
      auto toggle = saved ? std::unique_ptr<Toggle>(static_cast<Toggle*>(saved.release())) : std::make_unique<Toggle>();
      m_widgets[key] = toggle.get();
      toggle->setToggleSize(ToggleSize::Medium);
      toggle->setCheckedImmediate(action.privateChannel);
      toggle->setEnabled(canPrivate && !m_dialogPending);
      toggle->setPosition(inner - 40, cursor - 1);
      toggle->setFrameSize(40, 24);
      toggle->setOnChange([this](bool) { activate({0, 0, 0, 0, Action::ChannelCreatePrivate, {}}); });
      page.addChild(std::move(toggle));
      const std::string description = canPrivate
          ? "Only selected members and roles will be able to view this "
              + std::string(category ? "category." : "channel.")
          : "You need Manage Permissions to create a private " + std::string(category ? "category." : "channel.");
      cursor += 22;
      cursor += label(page, 0, cursor, inner - 44, description, 12, muted, FontWeight::Normal, 0);
      cursor += 12;
    }
    m_scrolling.limit(m_channelCreateScroll, std::max(0.0F, cursor - viewport));
    clip->setTextScrollOffset(-m_channelCreateScroll);
    body->setPosition(0, -m_channelCreateScroll);
    const float correction = previousScroll - m_channelCreateScroll;
    const float bottom = contentY + viewport;
    for (std::size_t i = firstHit; i < m_hits.size(); ++i) {
      auto& hit = m_hits[i];
      hit.y += correction;
      const float top = std::max(contentY, hit.y), end = std::min(bottom, hit.y + hit.height);
      hit.y = top;
      hit.height = std::max(0.0F, end - top);
    }
    clip->addChild(std::move(body));
    root.addChild(std::move(clip));
    settingsScrollbar(
        root, "channel-create-scrollbar", {x + width - 16, contentY, 7, viewport, Action::None, {}}, cursor,
        m_channelCreateScroll
    );
    if (!m_dialogError.empty())
      label(root, x + 24, y + height - 91, width - 48, m_dialogError, 11, danger, FontWeight::Normal, 2);
    const float buttonWidth = (width - 56) / 2;
    button(
        root,
        {x + 24,
         y + height - 58,
         buttonWidth,
         36,
         m_dialogPending                 ? Action::None
             : m_channelCreateAccessStep ? Action::ChannelCreateBack
                                         : Action::CloseDialog,
         {}},
        m_channelCreateAccessStep ? "Back" : "Cancel", false, false, true, true
    );
    auto validated = action;
    validated.text = m_dialogText;
    validated.user = m_account.id;
    const auto createPermissions = action.parentId && !action.parentId->empty()
        ? serverChannelPermissions(action.guild, *action.parentId).value_or(0)
        : serverGuildPermissions(action.guild).value_or(0);
    const bool valid = (createPermissions & api::manageChannel) && api::interactionRequest(validated).has_value();
    const std::string caption = m_dialogPending               ? "Creating…"
        : action.privateChannel && !m_channelCreateAccessStep ? "Next"
        : category                                            ? "Create Category"
                                                              : "Create Channel";
    button(
        root,
        {x + 32 + buttonWidth,
         y + height - 58,
         buttonWidth,
         36,
         m_dialogPending || !valid ? Action::None : Action::SidebarSubmit,
         {}},
        caption, true, false, true, true
    );
    if (!valid || m_dialogPending)
      rectangle(root, x + 32 + buttonWidth, y + height - 58, buttonWidth, 36, withAlpha(background, .5F), 10);
    m_tooltip.clear();
  }
} // namespace app
