#include "app/client_application.h"
#include "app/theme.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace app {
  using namespace theme;

  void ClientApplication::showGuildProfile(const std::string& guild) {
    if (!discord::validSnowflake(guild))
      return;
    closeContext();
    closeGifPicker();
    closeEmojiPicker();
    closeDialog();
    m_scrolling.stopAll();
    m_dialogKind = "guild-profile";
    m_dialogError.clear();
    m_guildProfile.id = guild;
    m_guildProfile.name = "Server";
    if (const auto at = std::ranges::find(m_guilds, guild, &discord::Guild::id); at != m_guilds.end()) {
      m_guildProfile.name = at->name;
      m_guildProfile.icon = at->icon;
    }
    m_profileAnchorX = m_pointerX;
    m_profileAnchorY = m_pointerY;
    m_dialogAction = discord::Interaction{
        .kind = discord::InteractionKind::GuildProfile, .guild = guild, .revision = m_dialogGeneration
    };
    m_dialogPending = !m_demo && startInteraction(*m_dialogAction);
    if (m_demo) {
      m_guildProfile = {
          guild, "The Moonlit Cafe", "", "demo-preview",
          "A little space for art, software, and good company. Come share what you are making, or stay for a quiet "
          "orbit."
      };
      m_guildProfile.tag = discord::GuildTag{guild, "MOON", "demo-profile-badge"};
      m_guildProfile.members = 75205;
      m_guildProfile.online = 23918;
      m_guildProfile.joinable = true;
      m_guildProfile.traits = {{"art", "🎨"}, {"native apps", "🌙"}, {"community", "💜"}};
      m_profileAnchorX = m_profileAnchorY = -1;
    } else if (!m_dialogPending)
      m_dialogError = m_status;
    m_window.invalidate();
  }

  void ClientApplication::drawGuildProfile(Node& root, float w, float h) {
    m_tooltip.clear();
    const auto& guild = m_guildProfile;
    const float width = std::min(370.0F, w - 24), inner = width - 36;
    const float bannerHeight = h < 510 ? 76 : 116;
    const auto description =
        m_window.target().renderer().measureText(guild.description, 13, FontWeight::Normal, inner, 5);
    const float descriptionHeight = std::min(105.0F, std::max(20.0F, description.bottom - description.top));
    const float desired = bannerHeight + 124 + descriptionHeight + (guild.traits.empty() ? 0 : 78) + 104;
    const float height = std::min(h - 64, desired);
    const float x = std::clamp(
        m_profileAnchorX < 0           ? (w - width) / 2
            : m_profileAnchorX < w / 2 ? m_profileAnchorX + 16
                                       : m_profileAnchorX - width - 16,
        12.0F, std::max(12.0F, w - width - 12)
    );
    const float y = std::clamp(
        m_profileAnchorY < 0 ? (h - height) / 2 : m_profileAnchorY - 96, 44.0F, std::max(44.0F, h - height - 12)
    );
    m_dialogBounds = {x, y, width, height, Action::None, {}};
    rectangle(root, 0, 36, w, h - 36, Color{0, 0, 0, 0.24F});
    m_hits.push_back({0, 36, w, h - 36, Action::None, {}});
    rectangle(root, x - 1, y + 3, width + 2, height + 2, Color{0, 0, 0, 0.35F}, 15);
    rectangle(root, x, y, width, height, lerpColor(sidebar, muted, 0.25F), 14);
    rectangle(root, x + 1, y + 1, width - 2, height - 2, sidebar, 13);
    m_hits.push_back(m_dialogBounds);
    if (!picture(root, x + 1, y + 1, width - 2, bannerHeight + 12, guild.banner, 13))
      rectangle(
          root, x + 1, y + 1, width - 2, bannerHeight + 12,
          guild.accent ? rgbHex(*guild.accent) : lerpColor(accent, panel, 0.45F), 13
      );
    rectangle(root, x + 1, y + bannerHeight, width - 2, 16, sidebar);
    rectangle(root, x + 14, y + bannerHeight - 38, 88, 88, sidebar, 44);
    avatar(root, x + 20, y + bannerHeight - 32, 76, guild.icon, guild.name);
    rectangle(root, x + width - 42, y + 10, 30, 30, Color{0, 0, 0, 0.4F}, 15);
    iconButton(root, {x + width - 42, y + 10, 30, 30, Action::CloseDialog, {}}, "x", "Close server preview");
    label(
        root, x + 18, y + bannerHeight + 58, inner - (guild.verified || guild.partnered ? 26 : 0), guild.name, 19,
        foreground, FontWeight::Bold
    );
    if (guild.verified || guild.partnered)
      glyph(root, x + width - 40, y + bannerHeight + 59, 20, "rosette-discount-check", accent);
    drawGuildTag(root, x + 114, y + bannerHeight + 12, inner - 100, guild.tag, false);
    std::string counts;
    if (guild.online)
      counts = std::to_string(*guild.online) + " online";
    if (guild.members) {
      if (!counts.empty())
        counts += " · ";
      counts += std::to_string(*guild.members) + " members";
    }
    label(root, x + 18, y + bannerHeight + 88, inner, counts, 12, muted);
    auto clipped = std::make_unique<Node>();
    clipped->setPosition(x + 18, y + bannerHeight + 122);
    clipped->setSize(inner, std::max(0.0F, height - bannerHeight - 206));
    clipped->setClipChildren(true);
    if (m_dialogPending && m_dialogAction && m_dialogAction->kind == discord::InteractionKind::GuildProfile) {
      for (int row = 0; row < 3; ++row)
        rectangle(*clipped, 0, row * 22, inner * (row == 2 ? .6F : .95F), 10, lerpColor(sidebar, muted, .15F), 5);
    } else {
      label(*clipped, 0, 0, inner, guild.description, 13, foreground, FontWeight::Normal, 5);
      float tx = 0, ty = descriptionHeight + 20;
      for (const auto& trait : guild.traits) {
        const auto text = (trait.emoji.empty() ? "" : trait.emoji + " ") + trait.name;
        const float size = std::min(inner, m_window.target().renderer().measureText(text, 12).width + 20);
        if (tx > 0 && tx + size > inner) {
          tx = 0;
          ty += 30;
        }
        rectangle(*clipped, tx, ty, size, 25, panel, 12);
        label(*clipped, tx + 10, ty + 5, size - 20, text, 12, foreground);
        tx += size + 6;
      }
    }
    root.addChild(std::move(clipped));
    const bool member = std::ranges::find(m_guilds, guild.id, &discord::Guild::id) != m_guilds.end();
    const bool joined = member || m_guildPreviewJoined;
    const auto status = !m_dialogError.empty() ? m_dialogError
        : m_guildPreviewVerification           ? guild.recruitment ? "Continue joining this server in Discord."
                                                                   : "Joined. Complete server verification in Discord."
        : !guild.joinable && !joined && !m_dialogPending ? "An invite is required to join this server."
                                                         : "";
    label(root, x + 18, y + height - 78, inner, status, 11, muted, FontWeight::Normal, 2);
    if (!m_dialogError.empty()) {
      button(root, {x + 18, y + height - 42, inner, 30, Action::GuildProfileRetry, {}}, "Retry preview");
      return;
    }
    const auto action = m_dialogPending ? Action::None
        : joined                        ? Action::OpenPreviewGuild
        : guild.joinable                ? Action::JoinGuild
                                        : Action::None;
    button(
        root, {x + 18, y + height - 46, inner, 34, action, {}},
        m_dialogPending         ? "Loading…"
            : joined            ? m_guildPreviewVerification ? "Open in Discord" : "Open Server"
            : guild.recruitment ? "Request to Join"
                                : "Join",
        action != Action::None, false, true, true
    );
  }

  bool ClientApplication::guildProfileAction(const Hit& hit) {
    if (hit.action == Action::GuildProfile) {
      showGuildProfile(hit.id);
      return true;
    }
    if (hit.action != Action::GuildProfileRetry
        && hit.action != Action::JoinGuild
        && hit.action != Action::OpenPreviewGuild)
      return false;
    if (m_dialogKind != "guild-profile" || m_dialogPending)
      return true;
    if (hit.action == Action::GuildProfileRetry) {
      const auto guild = m_guildProfile.id;
      showGuildProfile(guild);
    } else if (hit.action == Action::JoinGuild && m_guildProfile.joinable && !m_guildPreviewJoined) {
      discord::Interaction join{
          .kind = discord::InteractionKind::JoinGuild, .guild = m_guildProfile.id, .revision = ++m_dialogGeneration
      };
      if (m_demo) {
        // Simulate only a server acknowledgement; never send an account mutation.
        m_dialogAction = join;
        discord::Event event;
        event.interaction = join;
        interactionResult(event);
      } else if (startInteraction(join)) {
        m_dialogAction = join;
        m_dialogPending = true;
        m_dialogError.clear();
      } else
        m_dialogError = m_status;
    } else if (hit.action == Action::OpenPreviewGuild) {
      const auto guild = m_guildProfile;
      const bool member = std::ranges::find(m_guilds, guild.id, &discord::Guild::id) != m_guilds.end();
      if (m_guildPreviewVerification) {
        if (!m_demo)
          activate({0, 0, 0, 0, Action::OpenUrl, "https://discord.com/channels/" + guild.id});
      } else if (member || m_guildPreviewJoined) {
        if (!member) {
          m_guilds.push_back({guild.id, guild.name, guild.icon});
          m_folders.push_back({{}, {}, 0, {guild.id}});
        }
        closeDialog();
        selectGuild(guild.id);
      }
    }
    m_window.invalidate();
    return true;
  }
} // namespace app
