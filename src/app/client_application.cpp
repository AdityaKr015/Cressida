#include "app/client_application.h"

#include "app/message_layout.h"
#include "app/theme.h"
#include "core/deferred_call.h"
#include "core/environment.h"
#include "core/input/key_modifiers.h"
#include "core/timer_manager.h"
#include "render/core/texture_manager.h"
#include "render/scene/image_node.h"
#include "render/scene/input_area.h"
#include "render/scene/rect_node.h"
#include "render/scene/text_node.h"
#include "ui/palette.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <xkbcommon/xkbcommon-keysyms.h>

namespace app {
  using namespace theme;
  namespace {
    std::string initial(const std::string& name) {
      if (name.empty())
        return "?";
      std::size_t end = 1;
      while (end < name.size() && (static_cast<unsigned char>(name[end]) & 0xC0) == 0x80)
        ++end;
      auto value = name.substr(0, end);
      if (end == 1)
        value[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(value[0])));
      return value;
    }

  } // namespace

  ClientApplication::ClientApplication(bool demoMode, std::string token) : m_token(std::move(token)) {
    m_edit.end(m_token);
    m_showTokenEntry = !m_token.empty();
    m_fieldFocused = m_showTokenEntry;
    m_window.draw = [this] {
      prepareDemo();
      draw();
    };
    m_window.motion = [this](float x, float y) {
      m_pointerX = x;
      y += 36 - m_window.titlebarHeight();
      m_pointerY = y;
      m_controlInput.pointerMotion(x, y + m_window.titlebarHeight() - 36, 0);
      if (m_dragImage) {
        m_viewPanX += x - m_imageDragX;
        m_viewPanY += y - m_imageDragY;
        m_imageDragX = x;
        m_imageDragY = y;
      }
      updateCursor();
      m_window.invalidate();
      if (m_dragDraft)
        positionCaret(x, y, true);
      if (m_dragMessage)
        positionChatSelection(x, y);
    };
    m_window.released = [this] {
      m_controlInput.pointerButton(m_pointerX, m_pointerY + m_window.titlebarHeight() - 36, 272, false, 0, 0, false);
      m_dragDraft = m_dragMessage = m_dragImage = false;
    };
    m_window.pointerLeft = [this] {
      m_controlInput.pointerLeave();
      m_pointerX = m_pointerY = -1000;
      m_window.invalidate();
    };
    m_window.click = [this](float x, float y, std::uint32_t) {
      y += 36 - m_window.titlebarHeight();
      m_pointerX = x;
      m_pointerY = y;
      if (m_viewImage.empty()
          && !m_presenceMenu
          && !m_emojiMenu
          && m_controlInput.pointerButton(x, y + m_window.titlebarHeight() - 36, 272, true, 0, 0, false)) {
        m_window.invalidate();
        return;
      }
      m_controlInput.setFocus(nullptr);
      m_focusedInput.clear();
      for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it) {
        if (it->contains(x, y)) {
          const auto hit = *it;
          activate(hit);
          return;
        }
      }
      m_fieldFocused = false;
      m_selectAll = false;
      m_window.invalidate();
    };
    m_window.key = [this](const Key& key) { onKey(key); };
    m_window.zoom = [this](float delta) {
      if (!m_viewImage.empty()) {
        m_viewZoom = std::clamp(m_viewZoom * (delta < 0 ? 1.1F : 1 / 1.1F), 0.25F, 4.0F);
        m_window.invalidate();
        return;
      }
      m_window.setUiScale(m_window.uiScale() * (delta < 0 ? 1.1F : 1 / 1.1F));
      saveSelection();
    };
    m_window.pasted = [this](std::string bytes, std::string mime) {
      if (m_pasteConnected != m_connected || m_pasteSession != m_session || m_pasteChannel != m_channelId)
        return;
      if (!m_pasteInput.empty()) {
        auto id = std::exchange(m_pasteInput, {});
        if (id == m_focusedInput && mime.starts_with("text/plain")) {
          pasteControl(id, std::move(bytes));
          return;
        }
        if (id != "composer")
          return;
      }
      if (mime == "error") {
        m_status = "Clipboard transfer failed or exceeded 20 MiB.";
        m_window.invalidate();
        return;
      }
      if (mime.starts_with("text/plain")) {
        insert(std::move(bytes));
        return;
      }
      if (!m_connected || m_demo || m_sending || m_channelId.empty() || !m_fieldFocused)
        return;
      discord::Upload upload{
          "pasted-image-"
              + std::to_string(++m_attachmentSerial)
              + (mime == "image/png"        ? ".png"
                     : mime == "image/jpeg" ? ".jpg"
                                            : ".webp"),
          mime,
          {bytes.begin(), bytes.end()}
      };
      std::vector<discord::Upload> uploads;
      for (const auto& image : m_attachments[m_channelId])
        uploads.push_back(image.upload);
      uploads.push_back(upload);
      if (!discord::validUploads(uploads)) {
        m_status = "Attachments exceed the 20 MiB draft limit.";
        m_window.invalidate();
        return;
      }
      auto texture = m_window.context().textureManager().loadFromEncodedBytes(upload.bytes.data(), upload.bytes.size());
      if (!texture.valid()) {
        m_status = "Could not decode the pasted image.";
        m_window.invalidate();
        return;
      }
      m_attachments[m_channelId].push_back({std::move(upload), texture, std::to_string(m_attachmentSerial)});
      m_attachmentScroll = std::numeric_limits<float>::max();
      m_window.invalidate();
    };
    m_window.scroll = [this](float delta) {
      if (m_settingsOpen) {
        auto& scroll = m_pointerX < m_settingsNavRight ? m_settingsNavScroll : m_settingsScroll;
        scroll = std::max(0.0F, scroll + delta * 3);
        m_window.invalidate();
        return;
      }
      if (!m_viewImage.empty()) {
        m_viewZoom = std::clamp(m_viewZoom * (delta < 0 ? 1.15F : 1 / 1.15F), 0.25F, 4.0F);
        m_window.invalidate();
        return;
      }
      if (!m_connected)
        return;
      if (!m_attachments[m_channelId].empty()
          && m_pointerX >= m_attachmentX
          && m_pointerX < m_attachmentX + m_attachmentWidth
          && m_pointerY >= m_attachmentY
          && m_pointerY < m_attachmentY + 90)
        m_attachmentScroll = std::clamp(
            m_attachmentScroll + delta * 3, 0.0F,
            std::max(0.0F, m_attachments[m_channelId].size() * 96.0F - m_attachmentWidth)
        );
      else if (m_pointerX < 78)
        m_guildScroll = std::max(0.0F, m_guildScroll + delta * 3);
      else if (m_pointerX < 326)
        m_channelScroll = std::max(0.0F, m_channelScroll + delta * 3);
      else if (m_showMembers && m_window.width() >= 1000 && m_pointerX >= m_window.width() - 240) {
        m_memberScroll = std::clamp(
            m_memberScroll + delta * 3, 0.0F, std::max(0.0F, m_memberCount * 44.0F - (m_window.height() - 130))
        );
        const int page = static_cast<int>(m_memberScroll / 44) / 100 * 100;
        if (page != m_memberPage && !m_demo) {
          m_memberPage = page;
          m_client.subscribe(m_guildId, m_channelId, page);
        }
      } else
        m_messageScroll = std::clamp(m_messageScroll - delta * 3, 0.0F, m_messageMaxScroll);
      m_window.invalidate();
    };
    if (demoMode)
      demo();
  }

  int ClientApplication::run(bool smokeTest) {
    m_window.initialize();
    if (!m_demo && !m_token.empty())
      connect();
    m_window.run(m_client.eventFd(), [this] { tick(); }, smokeTest ? 1 : 0, m_assets.eventFd());
    m_client.disconnect();
    return 0;
  }

  void ClientApplication::prepareDemo() {
    if (!m_demo || m_demoPrepared)
      return;
    m_demoPrepared = true;
    if (m_demo && m_window.context().makeCurrent(m_window.target())) {
      // Procedural offline fixture, so media previews never need a CDN in demo.
      std::vector<unsigned char> pixels(640 * 360 * 4);
      for (int y = 0; y < 360; ++y)
        for (int x = 0; x < 640; ++x) {
          const auto offset = (y * 640 + x) * 4;
          pixels[offset] = 40 + 180 * x / 640;
          pixels[offset + 1] = 50 + 150 * y / 360;
          pixels[offset + 2] = 180;
          pixels[offset + 3] = 255;
        }
      m_images["demo-preview"].texture = m_window.context().textureManager().loadFromRgba(pixels.data(), 640, 360);
      discord::Message preview{"6", m_channelId, "Alex", "An image and a video preview", "2026-10-17T09:50:00Z"};
      preview.attachments.push_back(
          {"7", "preview.png", "https://example.com/preview.png", "image/png", {"demo-preview", 1280, 720}}
      );
      discord::Embed video;
      video.type = "video";
      video.provider = "YouTube";
      video.title = "Offline video preview";
      video.url = "https://www.youtube.com";
      video.thumbnail = {"demo-preview", 1280, 720};
      preview.embeds.push_back(std::move(video));
      m_messages.push_back(std::move(preview));
      if (const auto* option = cressida::environment("CRESSIDA_DEMO_VIEW", "DISCORD_CPP_DEMO_VIEW")) {
        const std::string view(option);
        if (view == "composer") {
          m_drafts[m_channelId] = "A centered message";
        } else if (view == "emoji-picker")
          m_emojiMenu = true;
        else if (view == "unread") {
          m_privateChannels = {{"900", "Maya", 1}, {"901", "Studio", 3}};
          m_unreadDMs = {"900", "901"};
          m_unreadChannels = {"102", "104", "900", "901"};
          m_unreadGuilds = {{"102", "1"}, {"104", "1"}, {"900", ""}, {"901", ""}};
          m_messages.back().content = "Hi <@11>, check <#102>. `literal <@11>`";
          m_messages.back().mentions = {{"11", "Maya"}};
        } else if (view == "dm-presence") {
          m_privateChannels.clear();
          for (const auto& [id, name] : std::vector<std::pair<std::string, std::string>>{
                   {"11", "Maya"}, {"12", "Noah"}, {"13", "Robin"}, {"14", "Sam"}
               }) {
            discord::Channel channel;
            channel.id = "90" + id;
            channel.type = 1;
            channel.name = name;
            channel.recipients.push_back({id, name});
            m_privateChannels.push_back(std::move(channel));
          }
          discord::UserUpdates users;
          users.users["11"] = {"My friend 🌿", "online"};
          users.users["12"] = {{}, "idle"};
          users.users["13"] = {{}, "dnd"};
          users.users["14"] = {{}, "offline"};
          m_userDirectory.apply(users);
          showDirectMessages();
          selectChannel("9011");
          updateDmMembers();
          m_messages.front().authorId = "11";
          m_messages.front().content = "Hi <@11>! This uses your personal nickname.";
          m_messages.front().mentions = {{"11", "Maya"}};
          m_messageLayouts.clear();
        } else if (view == "image") {
          m_viewImage = "demo-preview";
          m_viewUrl = "https://example.com/preview.png";
          m_viewerImages = {{m_viewImage, m_viewUrl}};
        } else if (view == "status-menu")
          m_presenceMenu = true;
        else if (view == "hover") {
          m_pointerX = 160;
          m_pointerY = 185;
        } else if (view == "appearance-light") {
          m_preferences.theme = "light";
          openSettings();
          m_settingsPage = "appearance";
        } else if (
            view == "account"
            || view == "profile"
            || view == "appearance"
            || view == "text"
            || view == "presence"
            || view == "privacy"
            || view == "notifications"
            || view == "activity"
            || view == "friends"
            || view == "keybinds"
            || view == "accessibility"
            || view == "search"
        ) {
          openSettings();
          selectSettingsPage(view == "search" ? "privacy" : view);
          if (view == "search")
            m_settingsFields["search"] = "privacy";
        }
      }
    }
  }

  void ClientApplication::rectangle(Node& parent, float x, float y, float w, float h, Color fill, float radius) {
    auto node = std::make_unique<RectNode>();
    node->setPosition(x, y);
    node->setSize(std::max(w, 0.0F), std::max(h, 0.0F));
    node->setStyle(RoundedRectStyle{.fill = fill, .radius = radius});
    parent.addChild(std::move(node));
  }

  float ClientApplication::label(
      Node& parent, float x, float y, float width, std::string text, float size, Color color, FontWeight weight,
      int lines, bool markup
  ) {
    auto node = std::make_unique<TextNode>();
    auto metrics = m_window.target().renderer().measureText(
        text, size, weight, markup ? 0 : width, lines, TextAlign::Start, {}, TextEllipsize::End, markup
    );
    node->setText(std::move(text));
    node->setFontSize(size);
    node->setUseMarkup(markup);
    node->setFontWeight(weight);
    node->setColor(color);
    // Rich fragments are already wrapped and measured by message_layout.
    // Keep their Pango attributes intact rather than applying a second width
    // constraint (the renderer replaces markup attributes when constraining).
    node->setMaxWidth(markup ? 0 : width);
    node->setMaxLines(lines);
    // TextNode's origin is the baseline; this helper accepts the logical top edge.
    node->setPosition(x, y - metrics.top);
    // TextMetrics exposes top/bottom rather than an assumed line-height.
    const float height = std::max(size + 4, metrics.bottom - metrics.top);
    node->setSize(width, height);
    parent.addChild(std::move(node));
    return height;
  }

  void ClientApplication::button(Node& parent, Hit hit, std::string text, bool primary, bool selected, bool fill) {
    float offsetX = 0, offsetY = 0;
    for (auto* node = &parent; node && node != m_scene.get(); node = node->parent()) {
      offsetX += node->x();
      offsetY += node->y();
    }
    const auto natural = m_window.target().renderer().measureText(text, 14, FontWeight::Medium, 0, 1);
    if (!fill)
      hit.width = std::min(hit.width, std::ceil(natural.width) + 24);
    if (!fill)
      hit.height = std::min(hit.height, std::ceil(natural.bottom - natural.top) + 14);
    const bool over = hit.contains(m_pointerX - offsetX, m_pointerY - offsetY);
    rectangle(
        parent, hit.x, hit.y, hit.width, hit.height,
        primary                ? (over ? rgbHex(m_preferences.theme == "light" ? 0x8063C6 : 0xDACDFB) : accent)
            : selected || over ? raised
                               : panel,
        10
    );
    const auto metrics = m_window.target().renderer().measureText(text, 14, FontWeight::Medium, hit.width - 24, 1);
    const float textHeight = metrics.bottom - metrics.top;
    label(
        parent, hit.x + 14, hit.y + (hit.height - textHeight) / 2, hit.width - 24, std::move(text), 14,
        primary ? ink : foreground, FontWeight::Medium
    );
    hit.x += offsetX;
    hit.y += offsetY;
    m_hits.push_back(std::move(hit));
  }

  bool ClientApplication::image(Node& parent, float x, float y, float size, const std::string& path, float radius) {
    return picture(parent, x, y, size, size, path, radius);
  }

  bool ClientApplication::picture(
      Node& parent, float x, float y, float width, float height, const std::string& path, float radius
  ) {
    if (path.empty() || (m_demo && !m_images.contains(path)))
      return false;
    auto& cached = m_images[path];
    cached.touched = ++m_imageSerial;
    if (!cached.texture.valid()) {
      if (!m_pendingImages.contains(path) && std::chrono::steady_clock::now() >= cached.retry && m_assets.request(path))
        m_pendingImages.insert(path);
      return false;
    }
    auto node = std::make_unique<ImageNode>();
    node->setPosition(x, y);
    node->setSize(width, height);
    node->setTextureId(cached.texture.id);
    node->setTextureSize(cached.texture.width, cached.texture.height);
    node->setFitMode(ImageFitMode::Cover);
    node->setRadius(radius);
    parent.addChild(std::move(node));
    return true;
  }

  void ClientApplication::avatar(
      Node& parent, float x, float y, float size, const std::string& path, const std::string& name
  ) {
    if (image(parent, x, y, size, path, size / 2))
      return;
    rectangle(parent, x, y, size, size, raised, size / 2);
    label(
        parent, x + size * 0.28F, y + size * 0.17F, size * 0.7F, initial(name), size * 0.45F, accent,
        FontWeight::SemiBold
    );
  }

  void ClientApplication::draw() {
    theme::select(m_preferences.theme);
    const float w = m_window.width(), h = m_window.height() + 36 - m_window.titlebarHeight();
    m_tooltip.clear();
    m_textAreas.clear();
    m_messageText.clear();
    if (m_viewImage.empty())
      m_viewerImages.clear();
    beginControls();
    if (!m_scene)
      m_scene = std::make_unique<Node>();
    while (!m_scene->children().empty()) {
      auto removed = m_scene->removeChild(m_scene->children().back().get());
    }
    m_scene->setSize(w, h);
    m_scene->setPosition(0, m_window.titlebarHeight() - 36);
    auto& root = *m_scene;
    m_hits.clear();
    rectangle(root, 0, 0, w, h, background);
    if (m_window.titlebarHeight() > 0) {
      rectangle(root, 0, 0, w, 36, rgbHex(0x121318));
      label(root, 18, 8, 300, "CRESSIDA", 11, muted, FontWeight::SemiBold);
      for (int i = 0; i < 3; ++i) {
        const float x = w - 144 + i * 48;
        if (hovered(x, 0, 48, 36))
          rectangle(root, x, 0, 48, 36, i == 2 ? rgbHex(0xED4245) : raised);
        glyph(
            root, x + 15, 8, 18,
            i == 0       ? "minus"
                : i == 1 ? (m_window.maximized() ? "squares" : "square")
                         : "x",
            foreground
        );
        m_hits.push_back({x, 0, 48, 36, i == 0 ? Action::Minimize : i == 1 ? Action::Maximize : Action::Close, {}});
      }
    }

    if (!m_connected) {
      const float x = (w - 460) / 2, y = std::max(62.0F, (h - 360) / 2);
      rectangle(root, x, y, 460, 360, sidebar, 22);
      label(root, x + 32, y + 28, 390, "A LITTLE CLOSER TO YOUR PEOPLE", 10, accent, FontWeight::SemiBold);
      label(root, x + 32, y + 58, 390, "Welcome back.", 32, foreground, FontWeight::Bold);
      label(root, x + 32, y + 106, 390, "Connect your Discord account and settle in.", 14, muted);
      if (!m_showTokenEntry) {
        label(
            root, x + 32, y + 151, 390, "Sign in using Discord in your default browser.", 13, muted, FontWeight::Normal,
            2
        );
        button(
            root, {x + 32, y + 200, 396, 46, Action::DiscordLogin, {}},
            m_login.active() ? "Waiting for your browser…" : "Sign in with Discord", true
        );
        button(root, {x + 32, y + 260, 190, 38, Action::Demo, {}}, "Explore demo");
        button(
            root, {x + 238, y + 260, 190, 38, m_login.active() ? Action::CancelLogin : Action::ToggleToken, {}},
            m_login.active() ? "Cancel sign-in" : "Use a token instead"
        );
        label(
            root, x + 32, y + 316, 396,
            m_status.empty() ? "Sign in on Discord's own page. Your password stays there." : m_status, 11, muted,
            FontWeight::Normal, 2
        );
      } else {
        label(root, x + 32, y + 151, 390, "Account token", 12, foreground, FontWeight::Medium);
        inputControl(root, x + 32, y + 177, 396, 48, "token", m_token, "Paste your account token", false, true);
        button(
            root, {x + 32, y + 241, 234, 44, Action::Connect, {}}, m_connecting ? "Connecting…" : "Connect account",
            true
        );
        button(root, {x + 278, y + 241, 150, 44, Action::ToggleToken, {}}, "Back to sign-in");
        label(
            root, x + 32, y + 306, 396, m_status.empty() ? "Your token stays in memory for this session." : m_status,
            12, m_status.empty() ? muted : rgbHex(0xF4B6B6), FontWeight::Normal, 2
        );
      }
    } else if (!m_settingsOpen) {
      rectangle(root, 0, 36, 78, h - 36, rgbHex(0x14151A));
      rectangle(root, 78, 36, 248, h - 36, sidebar);
      rectangle(root, 78, 36, 248, 66, sidebar);
      std::string guildName = m_dmMode ? "Direct messages" : "Your servers";
      for (const auto& guild : m_guilds)
        if (guild.id == m_guildId)
          guildName = guild.name;
      label(root, 98, 57, 208, guildName, 17, foreground, FontWeight::SemiBold);
      label(root, 98, 113, 200, m_dmMode ? "CONVERSATIONS" : "CHANNELS", 10, muted, FontWeight::SemiBold);
      iconButton(root, {14, 48, 48, 48, Action::Home, {}}, "message-circle", "Direct messages", m_dmMode);

      struct RailRow {
        const discord::Guild* guild;
        const discord::GuildFolder* folder;
        const discord::Channel* dm = nullptr;
      };
      std::vector<RailRow> rail;
      for (const auto& dm : m_privateChannels)
        if (m_unreadDMs.contains(dm.id))
          rail.push_back({nullptr, nullptr, &dm});
      const auto unreadDMRows = rail.size();
      for (const auto& folder : m_folders) {
        if (!folder.id.empty())
          rail.push_back({nullptr, &folder});
        if (folder.id.empty() || m_openFolders.contains(folder.id)) {
          for (const auto& guildId : folder.guildIds) {
            auto it = std::ranges::find(m_guilds, guildId, &discord::Guild::id);
            if (it != m_guilds.end())
              rail.push_back({&*it, &folder});
          }
        }
      }
      m_guildScroll = std::min(m_guildScroll, std::max(0.0F, rail.size() * 60.0F - (h - 164)));
      for (std::size_t i = 0; i < rail.size(); ++i) {
        const float y = 113 + i * 60 - m_guildScroll;
        if (y < 102 || y + 48 > h - 50)
          continue;
        const auto& row = rail[i];
        if (row.dm) {
          const auto& channel = *row.dm;
          const bool over = hovered(14, y, 48, 48);
          avatar(root, 14, y, 48, channel.icon, channelName(channel));
          if (channel.type == 1 && !channel.recipients.empty())
            statusBadge(root, 48, y + 34, userStatus(channel.recipients.front().id), rgbHex(0x14151A));
          rectangle(root, 0, y + 16, 4, 16, foreground, 2);
          rectangle(root, 48, y - 2, 18, 18, rgbHex(0xED4245), 9);
          label(root, 54, y, 12, "•", 12, rgbHex(0xFFFFFF), FontWeight::Bold);
          if (over)
            rectangle(root, 70, y + 14, 4, 20, accent, 2);
          m_hits.push_back({14, y, 48, 48, Action::DirectMessage, channel.id});
          if (i + 1 == unreadDMRows)
            rectangle(root, 22, y + 55, 32, 2, raised, 1);
          continue;
        }
        if (!row.guild) {
          auto color = rgbHex(row.folder->color);
          color.a = hovered(10, y, 56, 50) ? 0.65F : 0.35F;
          rectangle(root, 10, y, 56, 50, color, 14);
          for (std::size_t j = 0; j < std::min<std::size_t>(4, row.folder->guildIds.size()); ++j) {
            auto guild = std::ranges::find(m_guilds, row.folder->guildIds[j], &discord::Guild::id);
            if (guild != m_guilds.end())
              avatar(root, 17 + (j % 2) * 22, y + 5 + (j / 2) * 22, 18, guild->icon, guild->name);
          }
          m_hits.push_back({10, y, 56, 50, Action::Folder, row.folder->id});
          continue;
        }
        const auto& guild = *row.guild;
        if (!row.folder->id.empty())
          rectangle(root, 8, y - 6, 60, 60, panel, 10);
        const bool selected = guild.id == m_guildId;
        const bool over = hovered(14, y, 48, 48);
        rectangle(root, 14, y, 48, 48, selected ? accent : over ? panel : raised, selected || over ? 16 : 23);
        if (!image(root, 14, y, 48, guild.icon, selected || over ? 16 : 23))
          label(root, 30, y + 12, 24, initial(guild.name), 19, selected ? ink : foreground, FontWeight::SemiBold);
        const bool unread = std::ranges::any_of(m_unreadGuilds, [&](const auto& state) {
          return state.second == guild.id && m_unreadChannels.contains(state.first);
        });
        if (selected || over || unread)
          rectangle(
              root, 0, y + (selected ? 14 : 19), 4, selected ? 20 : 10, selected || over ? accent : foreground, 2
          );
        m_hits.push_back({14, y, 48, 48, Action::Guild, guild.id});
      }
      std::vector<const discord::Channel*> visibleChannels;
      for (const auto& channel : m_channels)
        if (channel.type == 4 || !m_collapsedCategories.contains(channel.parentId) || channel.id == m_channelId)
          visibleChannels.push_back(&channel);
      m_channelScroll = std::min(m_channelScroll, std::max(0.0F, visibleChannels.size() * 38.0F - (h - 228)));
      for (std::size_t i = 0; i < visibleChannels.size(); ++i) {
        const float y = 142 + i * 38 - m_channelScroll;
        if (y < 138 || y + 34 > h - 76)
          continue;
        const auto& channel = *visibleChannels[i];
        if (channel.type == 4) {
          if (hovered(90, y, 224, 34))
            rectangle(root, 90, y, 224, 34, panel, 8);
          glyph(
              root, 98, y + 8, 16, m_collapsedCategories.contains(channel.id) ? "chevron-right" : "chevron-down", muted
          );
          label(root, 119, y + 9, 186, channel.name, 11, muted, FontWeight::SemiBold);
          m_hits.push_back({90, y, 224, 34, Action::Category, channel.id});
          continue;
        }
        const bool selected = channel.id == m_channelId;
        const bool unread = m_unreadChannels.contains(channel.id) || m_unreadDMs.contains(channel.id);
        const bool over = hovered(90, y, 224, 34);
        if (selected || over)
          rectangle(root, 90, y, 224, 34, selected ? raised : panel, 8);
        if (unread && !selected)
          rectangle(root, 80, y + 14, 4, 6, foreground, 2);
        if (m_dmMode) {
          avatar(root, 99, y + 3, 28, channel.icon, channelName(channel));
          if (channel.type == 1 && !channel.recipients.empty())
            statusBadge(
                root, 119, y + 21, userStatus(channel.recipients.front().id),
                selected   ? raised
                    : over ? panel
                           : sidebar
            );
        } else
          label(root, 102, y + 7, 20, "#", 17, selected ? accent : muted);
        label(
            root, m_dmMode ? 136 : 128, y + 8, m_dmMode ? 165 : 173, m_dmMode ? channelName(channel) : channel.name, 14,
            selected || over || unread ? foreground : muted,
            selected || unread ? FontWeight::SemiBold : FontWeight::Normal
        );
        m_hits.push_back({90, y, 224, 34, Action::Channel, channel.id});
      }
      if (m_channels.empty())
        label(
            root, 100, 151, 200,
            (m_dmMode ? m_loadingPrivate : m_loadingChannels) ? "Loading…"
                : m_dmMode                                    ? "No conversations yet."
                                                              : "No text channels to show.",
            13, muted, FontWeight::Normal, 2
        );
      rectangle(root, 78, h - 72, 248, 72, rgbHex(0x1A1B22));
      if (hovered(88, h - 65, 180, 58))
        rectangle(root, 88, h - 65, 180, 58, panel, 8);
      // Redraw the account chip above its hover background.
      avatar(root, 94, h - 54, 34, m_account.avatar, m_account.name);
      rectangle(root, 118, h - 30, 11, 11, presenceColor(m_presence.status), 6);
      label(root, 140, h - 55, 128, m_account.name, 13, foreground, FontWeight::Medium);
      label(
          root, 140, h - 33, 128,
          !m_live && !m_demo             ? m_gatewayStatus
              : !m_presence.text.empty() ? m_presence.text
                                         : std::string(presenceName(m_presence.status)),
          10, muted
      );
      m_hits.push_back({88, h - 65, 180, 58, Action::TogglePresence, {}});
      iconButton(root, {276, h - 57, 36, 36, Action::Settings, {}}, "settings", "User settings");

      const float memberWidth = m_showMembers && w >= 1000 ? 240 : 0;
      const float left = 326, bodyWidth = w - left - memberWidth;
      rectangle(root, left, 36, bodyWidth, 66, background);
      std::string heading = "Choose a channel";
      float headingOffset = 24;
      for (const auto& channel : m_channels)
        if (channel.id == m_channelId) {
          heading = (m_dmMode ? "" : "#  ") + channelName(channel);
          if (m_dmMode && channel.type == 1 && !channel.recipients.empty()) {
            statusBadge(root, left + 24, 64, userStatus(channel.recipients.front().id), background);
            headingOffset = 44;
          }
        }
      label(
          root, left + headingOffset, 57, bodyWidth - 160 - (headingOffset - 24), heading, 19, foreground,
          FontWeight::SemiBold
      );
      iconButton(root, {w - 64, 50, 36, 36, Action::ToggleMembers, {}}, "users", "Member list", m_showMembers);
      rectangle(root, left, 101, bodyWidth, 1, raised);
      const auto& draft = m_drafts[m_channelId];
      m_edit.clamp(draft);
      m_draftWidth = std::max(32.0F, bodyWidth - 150);
      m_draftOffsets = TextEdit::stops(draft);
      m_window.target().renderer().measureTextCursorStopsWrapped(draft, 14, m_draftOffsets, m_draftWidth, m_draftStops);
      const float fullDraftHeight =
          m_draftStops.empty() ? 22 : m_draftStops.back().y + std::max(22.0F, m_draftStops.back().height);
      m_draftHeight = std::clamp(fullDraftHeight, 22.0F, std::min(160.0F, h * 0.3F));
      const float attachmentHeight = m_attachments[m_channelId].empty() ? 0 : 106;
      const float composerHeight = m_draftHeight + 24 + attachmentHeight;
      const float composerY = h - 30 - composerHeight;
      const float top = 122, bottom = composerY - 16, textWidth = std::max(32.0F, bodyWidth - 104);
      const MessageMeasure measure = [&](std::string_view text, const MessageStyle& style) {
        return m_window.target()
            .renderer()
            .measureText(
                messageMarkup(text, style), style.size, FontWeight::Normal, 0, 1, TextAlign::Start, {},
                TextEllipsize::End, true
            )
            .width;
      };
      auto content = std::make_unique<Node>();
      content->setPosition(left + 24, top);
      content->setSize(bodyWidth - 48, std::max(0.0F, bottom - top));
      content->setClipChildren(true);
      std::vector<float> heights;
      std::vector<const MessageLayout*> layouts;
      float totalHeight = 24;
      for (const auto& message : m_messages) {
        auto& cached = m_messageLayouts[message.id];
        const auto& renderer = m_window.target().renderer();
        if (cached.width != textWidth
            || cached.scale != renderer.renderScale()
            || cached.fonts != renderer.textMetricsGeneration()) {
          auto displayed = message;
          for (const auto& member : m_members)
            if (!member.user.id.empty())
              displayed.mentions.try_emplace(member.user.id, member.user.name);
          for (const auto& channel : m_channels)
            displayed.mentions.try_emplace(channel.id, channel.name);
          displayed.mentions.try_emplace(m_account.id, m_account.name);
          for (auto& [id, name] : displayed.mentions)
            name = userName(id, name);
          if (!m_preferences.showEmbeds)
            displayed.embeds.clear();
          if (!m_preferences.showImages)
            for (auto& attachment : displayed.attachments)
              attachment.image.url.clear();
          cached.layout = layoutRichMessage(displayed, textWidth, measure, m_preferences.messageSize);
          cached.width = textWidth;
          cached.scale = renderer.renderScale();
          cached.fonts = renderer.textMetricsGeneration();
        }
        const float height = std::max(22.0F, cached.layout.height) + (m_preferences.compact ? 26 : 34);
        layouts.push_back(&cached.layout);
        heights.push_back(height);
        totalHeight += height;
      }
      m_messageMaxScroll = std::max(0.0F, totalHeight - (bottom - top));
      m_messageScroll = std::clamp(m_messageScroll, 0.0F, m_messageMaxScroll);
      float y = std::min(0.0F, bottom - top - totalHeight) + m_messageScroll + 12;
      for (std::size_t i = 0; i < m_messages.size(); ++i) {
        const auto& message = m_messages[i];
        if (y + heights[i] >= 0 && y <= bottom - top) {
          const auto firstHit = m_hits.size();
          const auto author = userName(message.authorId, message.author);
          avatar(*content, 0, y + 1, 34, message.avatar, author);
          label(*content, 50, y, std::max(24.0F, textWidth - 64), author, 13, foreground, FontWeight::SemiBold);
          const std::string timestamp = message.timestamp.size() >= 16 ? message.timestamp.substr(11, 5) : "";
          label(*content, bodyWidth - 132, y + 1, 56, timestamp, 10, muted);
          const auto& layout = *layouts[i];
          const float bodyY = y + 18;
          m_messageText[message.id] = layout.plainText;
          m_hits.push_back({left + 74, top + bodyY, textWidth, layout.height, Action::SelectMessage, message.id});
          // Card backgrounds precede nested code/quote decoration.
          for (const auto& box : layout.boxes) {
            rectangle(
                *content, 50 + box.x, bodyY + box.y, box.width, box.height,
                box.quote       ? muted
                    : box.embed ? sidebar
                                : rgbHex(0x111318),
                box.quote ? 0 : 6
            );
            if (box.embed)
              rectangle(*content, 50 + box.x, bodyY + box.y, 3, box.height, rgbHex(box.color), 1);
            if (!box.code.empty()) {
              const float bx = left + 74 + box.x + box.width - 54, by = top + bodyY + box.y + 4;
              if (m_pointerX >= left && m_pointerY >= top + y && m_pointerY < top + y + heights[i]) {
                button(
                    *content, {50 + box.x + box.width - 54, bodyY + box.y + 4, 50, 24, Action::CopyCode, box.code},
                    "Copy"
                );
                // Scene-local button geometry must be translated for input.
                m_hits.back().x = bx;
                m_hits.back().y = by;
              }
            }
          }
          for (const auto& run : layout.runs) {
            const float rx = 50 + run.x, ry = bodyY + run.y;
            const bool hidden = run.style.spoiler && !m_revealedSpoilers.contains(message.id);
            if (hidden) {
              rectangle(*content, rx, ry, run.width, 20, raised, 3);
              m_hits.push_back({left + 24 + rx, top + ry, run.width, 22, Action::RevealSpoiler, message.id});
              continue;
            }
            if (m_selectedMessage == message.id && m_selectedText == layout.plainText && m_chatEdit.selected()) {
              const auto from = std::max(std::min(m_chatEdit.caret, m_chatEdit.anchor), run.offset);
              const auto to = std::min(std::max(m_chatEdit.caret, m_chatEdit.anchor), run.offset + run.text.size());
              if (to > from) {
                const auto offset =
                    run.image.empty() ? measure(std::string_view(run.text).substr(0, from - run.offset), run.style) : 0;
                const auto length = run.image.empty()
                    ? measure(std::string_view(run.text).substr(from - run.offset, to - from), run.style)
                    : run.width;
                rectangle(*content, rx + offset, ry, length, 22, rgbHex(0x574D70), 2);
              }
            }
            if (run.image.empty())
              label(
                  *content, rx, ry, std::max(1.0F, run.width + 2), messageMarkup(run.text, run.style), run.style.size,
                  run.style.link.empty() && !run.style.mention ? foreground : accent, FontWeight::Normal, 1, true
              );
            else if (!image(*content, rx, ry, run.width, run.image, 0))
              label(*content, rx, ry, 22, "…", 14, muted);
            m_textAreas.push_back({run, message.id, left + 24 + rx, top + ry});
            if (!run.style.link.empty())
              m_hits.push_back({left + 24 + rx, top + ry, run.width, 22, Action::OpenUrl, run.style.link});
          }
          for (const auto& pic : layout.pictures) {
            rectangle(*content, 50 + pic.x, bodyY + pic.y, pic.width, pic.height, panel, 6);
            picture(*content, 50 + pic.x, bodyY + pic.y, pic.width, pic.height, pic.media.url, 6);
            const float px = left + 74 + pic.x, py = top + bodyY + pic.y;
            if (pic.video) {
              rectangle(
                  *content, 50 + pic.x + pic.width / 2 - 25, bodyY + pic.y + pic.height / 2 - 20, 50, 40,
                  hovered(px, py, pic.width, pic.height) ? accent : panel, 10
              );
              label(
                  *content, 50 + pic.x + pic.width / 2 - 7, bodyY + pic.y + pic.height / 2 - 11, 24, "▶", 20, foreground
              );
              m_hits.push_back({px, py, pic.width, pic.height, Action::OpenUrl, pic.link});
            } else {
              if (m_viewImage.empty())
                m_viewerImages.push_back({pic.media.url, pic.link.empty() ? pic.media.url : pic.link});
              m_hits.push_back({px, py, pic.width, pic.height, Action::ViewImage, pic.media.url});
            }
          }
          if (m_pointerX >= left && m_pointerY >= top + y && m_pointerY < top + y + heights[i]) {
            button(*content, {bodyWidth - 182, y - 4, 52, 24, Action::CopyMessage, message.id}, "Copy");
          }
          for (auto index = firstHit; index < m_hits.size(); ++index) {
            auto& hit = m_hits[index];
            const float end = std::min(bottom, hit.y + hit.height);
            hit.y = std::max(top, hit.y);
            hit.height = std::max(0.0F, end - hit.y);
          }
        }
        y += heights[i];
      }
      if (m_messages.empty()) {
        label(
            *content, 18, 36, bodyWidth - 88,
            m_loadingMessages         ? "Loading conversation…"
                : m_channelId.empty() ? "Make yourself at home."
                                      : "The conversation starts here.",
            25, foreground, FontWeight::Bold
        );
        label(
            *content, 18, 80, bodyWidth - 88,
            m_channelId.empty() ? "Choose a server and a text channel to get started." : "Send the first message.", 14,
            muted, FontWeight::Normal, 2
        );
      }
      root.addChild(std::move(content));
      rectangle(root, left + 22, composerY, bodyWidth - 44, composerHeight, m_fieldFocused ? raised : panel, 14);
      m_attachmentX = left + 36;
      m_attachmentY = composerY + 10;
      m_attachmentWidth = bodyWidth - 72;
      m_attachmentScroll = std::clamp(
          m_attachmentScroll, 0.0F, std::max(0.0F, m_attachments[m_channelId].size() * 96.0F - m_attachmentWidth)
      );
      auto previews = std::make_unique<Node>();
      previews->setPosition(m_attachmentX, m_attachmentY);
      previews->setSize(m_attachmentWidth, 86);
      previews->setClipChildren(true);
      float previewX = -m_attachmentScroll;
      for (const auto& attachment : m_attachments[m_channelId]) {
        auto preview = std::make_unique<ImageNode>();
        preview->setPosition(previewX, 2);
        preview->setSize(82, 82);
        preview->setTextureId(attachment.texture.id);
        preview->setTextureSize(attachment.texture.width, attachment.texture.height);
        preview->setFitMode(ImageFitMode::Contain);
        previews->addChild(std::move(preview));
        button(*previews, {previewX + 58, 0, 28, 28, Action::RemoveAttachment, attachment.key}, "×");
        auto& hit = m_hits.back();
        const auto right = std::min(hit.x + hit.width, m_attachmentX + m_attachmentWidth);
        hit.x = std::max(hit.x, m_attachmentX);
        hit.width = std::max(0.0F, right - hit.x);
        previewX += 96;
      }
      root.addChild(std::move(previews));
      m_draftX = left + 74;
      m_composerTop = composerY;
      m_draftY = composerY + attachmentHeight + 4;
      inputControl(
          root, left + 68, m_draftY, m_draftWidth + 12, composerHeight - attachmentHeight - 8, "composer", draft,
          m_demo ? "Demo mode is read-only" : "Message this channel…", true
      );
      const auto actionY = composerY + composerHeight - 39;
      iconButton(
          root, {left + 30, actionY, 32, 32, m_sending ? Action::None : Action::PasteClipboard, {}},
          m_sending ? "loader" : "clipboard-plus", m_sending ? "Sending…" : "Paste from clipboard"
      );
      iconButton(
          root, {left + bodyWidth - 62, actionY, 32, 32, Action::ToggleEmoji, {}}, "mood-smile", "Emoji", m_emojiMenu
      );
      if (!m_status.empty())
        label(root, left + 28, 105, bodyWidth - 56, m_status, 11, muted);
      if (memberWidth > 0) {
        const float memberX = w - memberWidth;
        rectangle(root, memberX, 102, memberWidth, h - 102, sidebar);
        label(
            root, memberX + 18, 118, memberWidth - 36, m_dmMode ? "PARTICIPANTS" : "MEMBERS", 11, muted,
            FontWeight::SemiBold
        );
        auto members = std::make_unique<Node>();
        members->setPosition(memberX, 148);
        members->setSize(memberWidth, h - 158);
        members->setClipChildren(true);
        for (const auto& member : m_members) {
          const float rowY = member.index * 44.0F - m_memberScroll;
          if (rowY < -44 || rowY > h - 148)
            continue;
          if (!member.group.empty())
            label(
                *members, 18, rowY + 12, memberWidth - 36,
                member.group + (member.count >= 0 ? " — " + std::to_string(member.count) : ""), 11, muted,
                FontWeight::SemiBold
            );
          else if (!member.user.id.empty()) {
            if (hovered(memberX + 8, 148 + rowY, memberWidth - 16, 42))
              rectangle(*members, 8, rowY, memberWidth - 16, 42, panel, 6);
            const auto name = userName(member.user.id, member.user.name);
            const auto status = m_dmMode ? userStatus(member.user.id) : member.status;
            avatar(*members, 16, rowY + 5, 32, member.user.avatar, name);
            statusBadge(*members, 38, rowY + 27, status, sidebar);
            label(*members, 60, rowY + 12, memberWidth - 78, name, 13, status == "offline" ? muted : foreground);
          }
        }
        if (m_members.empty())
          label(*members, 18, 8, memberWidth - 36, m_live ? "Loading members…" : "Connecting…", 13, muted);
        root.addChild(std::move(members));
      }
    }
    if (m_settingsOpen)
      drawSettings(root, w, h);
    else if (m_presenceMenu)
      drawPresenceMenu(root, h);
    if (m_emojiMenu && !m_settingsOpen && m_viewImage.empty())
      drawEmojiPicker(root, w, h);
    if (!m_viewImage.empty())
      drawImageViewer(root, w, h);
    drawTooltip(root, w, h);
    finishControls();
    if (!m_demo && readingChannel(m_channelId) && !m_messages.empty()) {
      const auto& latest = m_messages.back();
      if (m_localRead[m_channelId] != latest.id && m_client.acknowledge(m_channelId, latest.id)) {
        m_localRead[m_channelId] = latest.id;
        m_unreadChannels.erase(m_channelId);
        m_unreadDMs.erase(m_channelId);
      }
    }
    updateCursor();
    m_window.context().renderScene(m_window.target(), m_scene.get());
  }

  std::string ClientApplication::userName(const std::string& id, const std::string& fallback) const {
    return m_userDirectory.name(id, fallback);
  }

  std::string ClientApplication::userStatus(const std::string& id, const std::string& fallback) const {
    if (id == m_account.id)
      return m_presence.status == "invisible" ? "offline" : m_presence.status;
    return m_userDirectory.status(id, fallback);
  }

  std::string ClientApplication::channelName(const discord::Channel& channel) const {
    if ((channel.type != 1 && channel.type != 3) || (channel.type == 3 && channel.named) || channel.recipients.empty())
      return channel.name;
    std::string name;
    for (const auto& recipient : channel.recipients) {
      if (!name.empty())
        name += ", ";
      name += userName(recipient.id, recipient.name);
    }
    return name;
  }

  void ClientApplication::updateDmMembers() {
    if (!m_dmMode)
      return;
    m_members.clear();
    const auto channel = std::ranges::find(m_channels, m_channelId, &discord::Channel::id);
    if (channel != m_channels.end()) {
      int index = 0;
      for (const auto& user : channel->recipients)
        if (user.id != m_account.id)
          m_members.push_back({index++, user, userStatus(user.id)});
      if (!m_account.id.empty())
        m_members.push_back({index, m_account, userStatus(m_account.id)});
    }
    m_memberCount = m_members.size();
  }

  void ClientApplication::statusBadge(Node& parent, float x, float y, const std::string& status, Color backdrop) {
    rectangle(parent, x, y, 12, 12, backdrop, 6);
    rectangle(parent, x + 2, y + 2, 8, 8, presenceColor(status), 4);
    if (status == "offline" || status == "unknown" || status == "invisible")
      rectangle(parent, x + 4, y + 4, 4, 4, backdrop, 2);
    else if (status == "dnd")
      rectangle(parent, x + 3, y + 5, 6, 2, backdrop, 1);
    else if (status == "idle")
      rectangle(parent, x + 1, y + 1, 6, 6, backdrop, 3);
  }

  void ClientApplication::connect() {
    if (m_connecting)
      return;
    if (m_token.empty()) {
      m_status = "Paste your account token to connect.";
      m_window.invalidate();
      return;
    }
    m_status.clear();
    m_connecting = true;
    m_session = m_client.connect(std::move(m_token));
    m_token.clear();
    if (m_inputs.contains("token"))
      m_inputs["token"]->setValue("");
    m_selectAll = false;
    m_window.invalidate();
  }

  void ClientApplication::beginLogin() {
    if (m_connecting || m_login.active())
      return;
    m_showTokenEntry = false;
    m_fieldFocused = false;
    m_status = m_login.start() ? "Complete sign-in in your browser. Allow local access if prompted."
                               : "Could not open your browser. Check your default browser settings.";
    m_window.invalidate();
  }

  void ClientApplication::demo() {
    m_login.cancel();
    m_client.disconnect();
    m_demo = true;
    m_userDirectory = {};
    m_preferences = {};
    m_demoPrepared = false;
    m_connected = true;
    m_connecting = false;
    m_token.clear();
    m_status.clear();
    m_account = {"1", "Alex", {}, "alex", "Alex", "Building a native Discord client.", "they/them"};
    m_presence = {"online"};
    m_guilds = {{"1", "The Common Room"}, {"2", "Creative Corner"}, {"3", "After Hours"}};
    m_folders = {{"10", "Hangouts", 0x8F80D4, {"1", "2"}}, {{}, {}, 0, {"3"}}};
    m_openFolders = {"10"};
    selectGuild("1");
  }

  bool ClientApplication::readingChannel(const std::string& channel) const {
    return m_connected
        && channel == m_channelId
        && m_window.active()
        && !m_settingsOpen
        && m_viewImage.empty()
        && m_messageScroll <= 1;
  }
  void ClientApplication::cacheChannel() {
    if (m_channelId.empty())
      return;
    m_channelMessages[m_channelId] = m_messages;
    if (!m_members.empty())
      m_channelMembers[m_channelId] = {m_members, m_memberCount};
    std::erase(m_channelCacheOrder, m_channelId);
    m_channelCacheOrder.push_back(m_channelId);
    while (m_channelCacheOrder.size() > 24) {
      m_channelMessages.erase(m_channelCacheOrder.front());
      m_channelMembers.erase(m_channelCacheOrder.front());
      m_channelCacheOrder.erase(m_channelCacheOrder.begin());
    }
  }
  void ClientApplication::selectGuild(const std::string& id) {
    if (!m_dmMode && m_guildId == id && !m_channels.empty())
      return;
    cacheChannel();
    m_restoreChannel = m_lastChannels.contains(id) ? m_lastChannels[id]
        : m_demo                                   ? ""
                                                   : m_sessionStore.channelForGuild(m_account.id, id);
    m_dmMode = false;
    m_guildId = id;
    m_channelId.clear();
    m_channels.clear();
    m_messages.clear();
    m_messageLayouts.clear();
    m_members.clear();
    m_memberCount = 0;
    m_memberScroll = 0;
    m_memberPage = 0;
    m_liveChanges.clear();
    m_channelScroll = 0;
    m_messageScroll = 0;
    m_loadingMessages = false;
    m_status.clear();
    if (m_demo) {
      m_channels = {{"101", "general"}, {"102", "introductions"}, {"103", "projects"}, {"104", "off-topic"}};
      selectChannel("101");
    } else {
      if (auto cached = m_guildChannels.find(id); cached != m_guildChannels.end()) {
        m_channels = cached->second;
        auto selected = std::ranges::find(m_channels, m_restoreChannel, &discord::Channel::id);
        if (selected == m_channels.end())
          selected = std::ranges::find_if(m_channels, [](const auto& c) { return c.type != 4; });
        if (selected != m_channels.end())
          selectChannel(selected->id);
        m_loadingChannels = false;
        m_window.invalidate();
        return;
      }
      m_loadingChannels = m_client.channels(id);
      if (!m_loadingChannels)
        m_status = "Could not load channels. Try selecting the server again.";
    }
    m_window.invalidate();
  }

  void ClientApplication::showDirectMessages() {
    cacheChannel();
    if (m_restoreChannel.empty())
      m_restoreChannel = m_lastChannels.contains("@me") ? m_lastChannels["@me"]
          : m_demo                                      ? ""
                                                        : m_sessionStore.channelForGuild(m_account.id, "");
    m_dmMode = true;
    m_guildId.clear();
    m_channelId.clear();
    m_channels = m_privateChannels;
    m_messages.clear();
    m_messageLayouts.clear();
    m_members.clear();
    m_memberCount = 0;
    m_channelScroll = 0;
    m_liveChanges.clear();
    if (!m_demo && m_privateChannels.empty() && !m_loadingPrivate)
      m_loadingPrivate = m_client.privateChannels();
    if (!m_channels.empty()) {
      auto selected = std::ranges::find(m_channels, m_restoreChannel, &discord::Channel::id);
      selectChannel(selected == m_channels.end() ? m_channels.front().id : selected->id);
      m_restoreChannel.clear();
    }
    m_window.invalidate();
  }

  void ClientApplication::selectChannel(const std::string& id) {
    m_emojiMenu = false;
    if (m_channelId == id)
      return;
    if (id != m_channelId)
      cacheChannel();
    m_controlInput.setFocus(nullptr);
    m_focusedInput = "composer";
    m_channelId = id;
    m_lastChannels[m_guildId.empty() ? "@me" : m_guildId] = id;
    m_edit.end(m_drafts[id]);
    m_attachmentScroll = 0;
    m_revealedSpoilers.clear();
    m_selectedMessage.clear();
    m_selectedText.clear();
    m_dragDraft = m_dragMessage = false;
    m_messages.clear();
    m_messageLayouts.clear();
    m_members.clear();
    m_memberCount = 0;
    m_memberScroll = 0;
    m_memberPage = 0;
    m_liveChanges.clear();
    m_messageScroll = 0;
    m_selectAll = false;
    m_fieldFocused = true;
    m_status.clear();
    m_loadingMessages = false;
    if (readingChannel(id)) {
      m_unreadDMs.erase(id);
      m_unreadChannels.erase(id);
    }
    float row = 0;
    for (const auto& channel : m_channels) {
      if (channel.type != 4 && m_collapsedCategories.contains(channel.parentId) && channel.id != id)
        continue;
      if (channel.id == id) {
        const auto view = std::max(38.0F, m_window.height() - 228.0F);
        m_channelScroll = std::clamp(m_channelScroll, std::max(0.0F, row - view + 38), row);
        break;
      }
      row += 38;
    }
    updateDmMembers();
    if (m_demo) {
      m_messages = {
          {"1", id, "Maya", "Made a little space for our ideas. Welcome in! 🌿 ✨", "2026-10-17T09:41:00Z"},
          {"2", id, "Noah", "This already feels like home. What is everyone working on?", "2026-10-17T09:43:00Z"},
          {"3", id, "Alex",
           "A **native desktop client**, with *formatted messages*.\n```cpp\nint main() {\n  return 0;\n}\n```",
           "2026-10-17T09:45:00Z"},
          {"4", id, "Maya",
           "A little __underline__, ~~a correction~~, and `inline code`.\n> A good conversation deserves a good place "
           "to live.",
           "2026-10-17T09:47:00Z"},
          {"5", id, "Noah", "The basics first: our servers, our channels, and the people in them.",
           "2026-10-17T09:49:00Z"},
      };
      discord::Embed preview;
      preview.provider = "Project notes";
      preview.title = "Native message previews";
      preview.description = "Embeds support **formatted descriptions**, fields, and images.";
      preview.fields = {{"Rendering", "Native Wayland / GLES"}, {"Language", "C++23"}};
      preview.footer = "An offline example";
      m_messages.back().embeds.push_back(std::move(preview));
      m_members = {
          {0, {}, "", "Online", 3},
          {1, {"11", "Maya"}, "online"},
          {2, {"12", "Noah"}, "idle"},
          {3, {"1", "Alex"}, "online"}
      };
      m_memberCount = 4;
    } else {
      if (auto cached = m_channelMessages.find(id); cached != m_channelMessages.end())
        m_messages = cached->second;
      if (!m_dmMode)
        if (auto cached = m_channelMembers.find(id); cached != m_channelMembers.end()) {
          m_members = cached->second.rows;
          m_memberCount = cached->second.count;
        }
      m_client.subscribe(m_guildId, id);
      refresh();
      saveSelection();
    }
    m_window.invalidate();
  }

  void ClientApplication::refresh() {
    if (m_demo || !m_connected || m_channelId.empty() || m_loadingMessages || m_sending)
      return;
    m_loadingMessages = m_client.messages(m_channelId);
    m_window.invalidate();
  }

  void ClientApplication::send() {
    if (m_demo || !m_connected || m_channelId.empty() || m_sending)
      return;
    const auto& draft = m_drafts[m_channelId];
    std::vector<discord::Upload> uploads;
    for (const auto& attachment : m_attachments[m_channelId])
      uploads.push_back(attachment.upload);
    if (draft.empty() && uploads.empty())
      return;
    if (!m_client.send(m_channelId, draft, std::move(uploads))) {
      m_status = "Messages can contain up to 2,000 characters. Please try again.";
    } else {
      m_pendingDraft = draft;
      m_pendingAttachments.clear();
      for (const auto& image : m_attachments[m_channelId])
        m_pendingAttachments.push_back(image.key);
      m_sending = true;
      m_status = "Sending…";
    }
    m_window.invalidate();
  }

  void ClientApplication::tick() {
    DeferredCall::drainWakeFd();
    for (auto& callback : DeferredCall::takePending())
      callback();
    TimerManager::instance().tick();
    bool imagesChanged = false;
    for (auto& asset : m_assets.drain()) {
      m_pendingImages.erase(asset.path);
      auto& cached = m_images[asset.path];
      if (!asset.bytes.empty() && m_window.context().makeCurrent(m_window.target()))
        cached.texture =
            m_window.context().textureManager().loadFromEncodedBytes(asset.bytes.data(), asset.bytes.size());
      cached.bytes = std::move(asset.bytes);
      cached.retry = std::chrono::steady_clock::now() + std::chrono::seconds(60);
      imagesChanged = true;
    }
    std::size_t encodedBytes = 0;
    for (const auto& [_, cached] : m_images)
      encodedBytes += cached.bytes.size();
    if (encodedBytes > 64 * 1024 * 1024) {
      std::vector<std::pair<std::uint64_t, std::string>> byAge;
      for (const auto& [path, cached] : m_images)
        if (!cached.bytes.empty())
          byAge.emplace_back(cached.touched, path);
      std::ranges::sort(byAge);
      for (const auto& [_, path] : byAge) {
        encodedBytes -= m_images[path].bytes.size();
        m_images[path].bytes.clear();
        if (encodedBytes <= 64 * 1024 * 1024)
          break;
      }
    }
    while (m_images.size() > 384) {
      auto oldest = std::ranges::min_element(m_images, {}, [](const auto& item) { return item.second.touched; });
      if (oldest->second.texture.valid() && m_window.context().makeCurrent(m_window.target()))
        m_window.context().textureManager().unload(oldest->second.texture);
      m_images.erase(oldest);
    }
    if (imagesChanged)
      m_window.invalidate();
    if (auto login = m_login.poll()) {
      if (!login->handoffToken.empty() && !m_connected && !m_demo) {
        m_connecting = true;
        m_token.clear();
        m_status = "Connecting to your account…";
        m_session = m_client.connectHandoff(std::move(login->key), std::move(login->handoffToken));
        m_window.invalidate();
      } else if (!login->error.empty()) {
        m_status = std::move(login->error);
        m_window.invalidate();
      }
    }
    for (auto& event : m_client.drain()) {
      if (event.session != m_session || m_demo)
        continue;
      if (!event.unauthorized
          && ((event.operation == discord::Operation::Channels && event.resource != m_guildId)
              || (event.operation == discord::Operation::Messages && event.resource != m_channelId)))
        continue;
      if (event.unauthorized) {
        m_userDirectory = {};
        clearAccountSettings();
        m_emojiMenu = false;
        m_settingsOpen = m_presenceMenu = m_savingProfile = m_loadingProfile = false;
        m_desiredPresence.clear();
        m_settingsFields.clear();
        m_settingsStatus.clear();
        m_viewImage.clear();
        m_client.disconnect();
        m_live = false;
        m_connected = false;
        m_connecting = false;
        m_sending = false;
        m_guildChannels.clear();
        m_channelCacheOrder.clear();
        m_channelMessages.clear();
        m_channelMembers.clear();
        m_lastChannels.clear();
        m_unreadDMs.clear();
        m_unreadChannels.clear();
        m_unreadGuilds.clear();
        m_localRead.clear();
        m_guilds.clear();
        m_channels.clear();
        m_messages.clear();
        m_messageLayouts.clear();
        for (auto& [_, images] : m_attachments)
          for (auto& image : images)
            m_window.context().textureManager().unload(image.texture);
        m_attachments.clear();
        m_pendingAttachments.clear();
        m_edit = {};
        m_selectedText.clear();
        m_selectedMessage.clear();
        m_drafts.clear();
        m_fieldFocused = true;
      }
      if (event.operation == discord::Operation::Connect) {
        m_connecting = false;
        if (event.error.empty()) {
          m_account = std::move(event.account);
          m_savingProfile = m_loadingProfile = false;
          m_desiredPresence.clear();
          m_preferences = m_sessionStore.preferences(m_account.id);
          m_presence = event.presence.value_or(discord::Presence{});
          m_guilds = std::move(event.guilds);
          m_folders = discord::arrangeGuilds(m_guilds, event.layout);
          m_connected = true;
          if (!m_live)
            m_gatewayStatus = "Connecting…";
          m_loadingPrivate = m_privateChannels.empty() && m_client.privateChannels();
          const auto last = m_sessionStore.load(m_account.id);
          m_restoreChannel = last ? last->channel : "";
          if (last)
            m_window.setUiScale(last->scale);
          if (last
              && !last->guild.empty()
              && std::ranges::find(m_guilds, last->guild, &discord::Guild::id) != m_guilds.end()) {
            for (const auto& folder : m_folders)
              if (!folder.id.empty() && std::ranges::find(folder.guildIds, last->guild) != folder.guildIds.end())
                m_openFolders.insert(folder.id);
            selectGuild(last->guild);
          } else
            showDirectMessages();
        }
      } else if (event.operation == discord::Operation::UserState) {
        m_userDirectory.apply(event.users);
        if (event.users.reset || std::ranges::any_of(event.users.users, [](const auto& item) {
              return item.second.nickname.has_value();
            }))
          m_messageLayouts.clear();
      } else if (event.operation == discord::Operation::ChannelSnapshot) {
        m_guildChannels[event.resource] = event.channels;
        if (!m_dmMode && event.resource == m_guildId && m_channels.empty()) {
          m_channels = std::move(event.channels);
          auto selected = std::ranges::find(m_channels, m_restoreChannel, &discord::Channel::id);
          if (selected == m_channels.end())
            selected = std::ranges::find_if(m_channels, [](const auto& c) { return c.type != 4; });
          if (selected != m_channels.end())
            selectChannel(selected->id);
          m_restoreChannel.clear();
        }
      } else if (event.operation == discord::Operation::InvalidateChannels) {
        m_guildChannels.erase(event.resource);
        if (!m_dmMode && event.resource == m_guildId) {
          m_restoreChannel = m_channelId;
          m_loadingChannels = m_client.channels(m_guildId);
        }
      } else if (event.operation == discord::Operation::ReadState) {
        m_unreadChannels.clear();
        m_unreadDMs.clear();
        m_unreadGuilds = std::move(event.unread);
        for (const auto& [id, guild] : m_unreadGuilds) {
          if (readingChannel(id))
            continue;
          m_unreadChannels.insert(id);
          if (guild.empty()
              && std::ranges::find(m_privateChannels, id, &discord::Channel::id) != m_privateChannels.end())
            m_unreadDMs.insert(id);
        }
      } else if (
          event.operation == discord::Operation::AccountSettings
          || event.operation == discord::Operation::SaveAccountSettings
      ) {
        if (event.resource == "changed") {
          if (m_settingsOpen && !m_loadingSettings && !m_savingSettings && m_settingsDraft == m_accountSettings)
            m_loadingSettings = m_client.accountSettings();
        } else {
          m_loadingSettings = false;
          const bool saved = event.operation == discord::Operation::SaveAccountSettings;
          if (saved)
            m_savingSettings = false;
          if (event.error.empty() && event.settings) {
            const bool dirty = m_settingsDraft != m_accountSettings;
            m_accountSettings = *event.settings;
            if (!dirty || saved || !m_settingsLoaded)
              m_settingsDraft = m_accountSettings;
            m_settingsLoaded = true;
            if (saved)
              m_settingsStatus = "Account settings saved.";
          } else if (!event.error.empty())
            m_settingsStatus = event.error;
        }
      } else if (event.operation == discord::Operation::SettingsList) {
        m_loadingSettingsLists.erase(event.resource);
        if (event.error.empty()) {
          m_settingsLists[event.resource] = std::move(event.settingsEntries);
          m_settingsListErrors.erase(event.resource);
        } else
          m_settingsListErrors[event.resource] = event.error;
      } else if (
          event.operation == discord::Operation::Profile
          || event.operation == discord::Operation::SaveProfile
          || event.operation == discord::Operation::AccountUpdate
      ) {
        if (event.operation == discord::Operation::Profile)
          m_loadingProfile = false;
        if (event.operation == discord::Operation::SaveProfile)
          m_savingProfile = false;
        if (event.error.empty() && event.account.id == m_account.id) {
          const bool editing = m_settingsOpen && m_profileDirty;
          m_account = std::move(event.account);
          if (event.operation == discord::Operation::SaveProfile || !editing)
            resetProfile();
          if (event.operation == discord::Operation::SaveProfile)
            m_settingsStatus = "Profile saved.";
        } else if (!event.error.empty())
          m_settingsStatus = event.error;
      } else if (
          event.operation == discord::Operation::PresenceChanged || event.operation == discord::Operation::SavePresence
      ) {
        if (event.presence && (m_desiredPresence.empty() || event.presence->status == m_desiredPresence))
          m_presence = *event.presence;
        if (event.operation == discord::Operation::SavePresence && event.resource == m_desiredPresence) {
          m_desiredPresence.clear();
          if (event.error.empty())
            m_settingsStatus = "Status saved.";
          else
            m_settingsStatus = "Status changed for this session; could not save it to your account. " + event.error;
        }
      } else if (event.operation == discord::Operation::Channels && event.resource == m_guildId) {
        m_loadingChannels = false;
        if (event.error.empty()) {
          m_channels = std::move(event.channels);
          m_guildChannels[m_guildId] = m_channels;
          auto first = std::ranges::find(m_channels, m_restoreChannel, &discord::Channel::id);
          if (first == m_channels.end())
            first = std::ranges::find_if(m_channels, [](const auto& channel) { return channel.type != 4; });
          m_restoreChannel.clear();
          if (first != m_channels.end())
            selectChannel(first->id);
        }
      } else if (event.operation == discord::Operation::Messages && event.resource == m_channelId) {
        m_loadingMessages = false;
        if (event.error.empty()) {
          m_messageLayouts.clear();
          m_messages = std::move(event.messages);
          for (const auto& [id, change] : m_liveChanges) {
            if (change)
              upsertMessage(*change);
            else
              std::erase_if(m_messages, [&](const auto& message) { return message.id == id; });
          }
        }
      } else if (
          event.operation == discord::Operation::PrivateChannels
          || event.operation == discord::Operation::PrivateChannelUpsert
      ) {
        m_loadingPrivate = false;
        if (event.error.empty()) {
          if (event.operation == discord::Operation::PrivateChannels)
            m_privateChannels = std::move(event.channels);
          else
            for (auto& channel : event.channels) {
              auto existing = std::ranges::find(m_privateChannels, channel.id, &discord::Channel::id);
              if (existing == m_privateChannels.end())
                m_privateChannels.insert(m_privateChannels.begin(), std::move(channel));
              else
                *existing = std::move(channel);
            }
          for (const auto& channel : m_privateChannels)
            if (m_unreadGuilds.contains(channel.id) && !readingChannel(channel.id))
              m_unreadDMs.insert(channel.id);
          if (m_dmMode) {
            m_channels = m_privateChannels;
            if (m_channelId.empty() && !m_channels.empty()) {
              auto selected = std::ranges::find(m_channels, m_restoreChannel, &discord::Channel::id);
              selectChannel(selected == m_channels.end() ? m_channels.front().id : selected->id);
              m_restoreChannel.clear();
            }
            updateDmMembers();
          }
        }
      } else if (event.operation == discord::Operation::PrivateChannelDelete) {
        std::erase_if(m_privateChannels, [&](const auto& channel) { return channel.id == event.resource; });
        if (m_dmMode) {
          m_channels = m_privateChannels;
          if (m_channelId == event.resource) {
            m_channelId.clear();
            m_messages.clear();
            m_messageLayouts.clear();
          }
        }
      } else if (event.operation == discord::Operation::Layout) {
        m_folders = discord::arrangeGuilds(m_guilds, event.layout);
      } else if (event.operation == discord::Operation::Members && event.resource == m_channelId) {
        m_members = std::move(event.members);
        m_memberCount = event.memberCount;
        m_messageLayouts.clear();
        m_memberScroll =
            std::clamp(m_memberScroll, 0.0F, std::max(0.0F, m_memberCount * 44.0F - (m_window.height() - 158)));
      } else if (event.operation == discord::Operation::Gateway) {
        m_live = event.live;
        m_gatewayStatus = m_live ? "Connected" : "Reconnecting…";
        if (m_live) {
          m_status.clear();
          refresh();
        }
      } else if (event.operation == discord::Operation::MessageCreate) {
        auto dm = std::ranges::find(m_privateChannels, event.resource, &discord::Channel::id);
        if (dm != m_privateChannels.end()) {
          auto channel = std::move(*dm);
          m_privateChannels.erase(dm);
          if (!event.messages.empty())
            channel.lastMessageId = event.messages.back().id;
          m_privateChannels.insert(m_privateChannels.begin(), std::move(channel));
          if (m_dmMode)
            m_channels = m_privateChannels;
          if (!readingChannel(event.resource)
              && !event.messages.empty()
              && event.messages.back().authorId != m_account.id)
            m_unreadDMs.insert(event.resource);
        }
        const bool fromOther = !event.messages.empty() && event.messages.back().authorId != m_account.id;
        if (!readingChannel(event.resource) && fromOther)
          m_unreadChannels.insert(event.resource);
        if (event.resource != m_channelId) {
          if (auto cached = m_channelMessages.find(event.resource); cached != m_channelMessages.end()) {
            for (const auto& message : event.messages) {
              std::erase_if(cached->second, [&](const auto& old) { return old.id == message.id; });
              cached->second.push_back(message);
            }
            if (cached->second.size() > 200)
              cached->second.erase(cached->second.begin(), cached->second.end() - 200);
          }
        }
        if (event.resource == m_channelId)
          for (auto& message : event.messages) {
            m_liveChanges[message.id] = message;
            upsertMessage(std::move(message));
          }
      } else if (event.operation == discord::Operation::MessageUpdate) {
        if (auto cached = m_channelMessages.find(event.resource); cached != m_channelMessages.end())
          for (auto& message : cached->second)
            if (message.id == event.messageId)
              event.patch.apply(message);
        if (event.resource != m_channelId)
          continue;
        for (auto& message : m_messages)
          if (message.id == event.messageId) {
            event.patch.apply(message);
            m_messageLayouts.erase(message.id);
            m_liveChanges[message.id] = message;
          }
      } else if (event.operation == discord::Operation::MessageDelete) {
        if (auto cached = m_channelMessages.find(event.resource); cached != m_channelMessages.end())
          std::erase_if(cached->second, [&](const auto& message) {
            return std::ranges::find(event.removed, message.id) != event.removed.end();
          });
        if (event.resource != m_channelId)
          continue;
        for (const auto& id : event.removed) {
          m_liveChanges[id] = std::nullopt;
          m_messageLayouts.erase(id);
          std::erase_if(m_messages, [&](const auto& message) { return message.id == id; });
        }
      } else if (event.operation == discord::Operation::Send) {
        m_sending = false;
        if (event.error.empty()) {
          if (m_drafts[event.resource] == m_pendingDraft)
            m_drafts[event.resource].clear();
          if (event.resource == m_channelId)
            m_edit.clamp(m_drafts[event.resource]);
          std::erase_if(m_attachments[event.resource], [&](auto& image) {
            if (std::ranges::find(m_pendingAttachments, image.key) == m_pendingAttachments.end())
              return false;
            m_window.context().textureManager().unload(image.texture);
            return true;
          });
          if (event.resource == m_channelId) {
            m_messageScroll = 0;
            for (auto& message : event.messages) {
              m_liveChanges[message.id] = message;
              upsertMessage(std::move(message));
            }
          }
          m_status.clear();
        }
        m_pendingDraft.clear();
        m_pendingAttachments.clear();
      }
      if (!event.error.empty())
        m_status = event.error;
      while (m_liveChanges.size() > 256)
        m_liveChanges.erase(m_liveChanges.begin());
      m_window.invalidate();
    }
  }

  void ClientApplication::upsertMessage(discord::Message message) {
    m_messageLayouts.erase(message.id);
    auto existing = std::ranges::find(m_messages, message.id, &discord::Message::id);
    if (existing == m_messages.end())
      m_messages.push_back(std::move(message));
    else
      *existing = std::move(message);
    std::ranges::sort(m_messages, [](const auto& a, const auto& b) {
      return a.id.size() != b.id.size() ? a.id.size() < b.id.size() : a.id < b.id;
    });
    if (m_messages.size() > 200) {
      for (auto it = m_messages.begin(); it != m_messages.end() - 200; ++it)
        m_messageLayouts.erase(it->id);
      m_messages.erase(m_messages.begin(), m_messages.end() - 200);
    }
  }

  void ClientApplication::saveSelection() {
    if (!m_demo && m_connected && !m_channelId.empty())
      m_sessionStore.save(m_account.id, {m_guildId, m_channelId, m_window.uiScale()});
  }

  void ClientApplication::positionCaret(float x, float y, bool extend) {
    if (m_draftStops.empty())
      return;
    float nearest = std::numeric_limits<float>::max();
    std::size_t position = 0;
    for (std::size_t i = 0; i < m_draftStops.size(); ++i) {
      const auto& stop = m_draftStops[i];
      const auto dx = x - m_draftX - stop.x;
      const auto dy = std::abs(y - m_draftY - (stop.y + stop.height / 2));
      const auto distance = dx * dx + std::pow(std::max(0.0F, dy - stop.height / 2), 2) * 10000;
      if (distance < nearest) {
        nearest = distance;
        position = m_draftOffsets[i];
      }
    }
    m_edit.caret = position;
    if (!extend)
      m_edit.anchor = position;
    m_window.invalidate();
  }

  void ClientApplication::positionChatSelection(float x, float y) {
    float nearest = std::numeric_limits<float>::max();
    const TextArea* area = nullptr;
    for (const auto& item : m_textAreas)
      if (item.message == m_selectedMessage) {
        const auto dy = std::max(0.0F, std::abs(y - item.y - 11) - 11);
        const auto dx = x < item.x ? item.x - x : std::max(0.0F, x - item.x - item.run.width);
        const auto distance = dx * dx + dy * dy * 10000;
        if (distance < nearest) {
          nearest = distance;
          area = &item;
        }
      }
    if (!area)
      return;
    const auto& run = area->run;
    auto positions = TextEdit::stops(run.text);
    float distance = std::numeric_limits<float>::max();
    for (auto offset : positions) {
      const float width = run.image.empty()
          ? m_window.target()
                .renderer()
                .measureText(
                    messageMarkup(std::string_view(run.text).substr(0, offset), run.style), run.style.size,
                    FontWeight::Normal, 0, 1, TextAlign::Start, {}, TextEllipsize::End, true
                )
                .width
          : (offset ? run.width : 0);
      const float delta = std::abs(x - area->x - width);
      if (delta < distance) {
        distance = delta;
        m_chatEdit.caret = run.offset + offset;
      }
    }
    m_window.invalidate();
  }

  void ClientApplication::insert(std::string text) {
    if (!m_connected && !m_showTokenEntry)
      return;
    if (!m_fieldFocused || m_connecting || (m_connected && (m_demo || m_sending || m_channelId.empty())))
      return;
    if (!g_utf8_validate(text.data(), text.size(), nullptr))
      return;
    // Preserve pasted line breaks and indentation in messages; login stays single-line.
    std::string clean;
    for (std::size_t i = 0; i < text.size(); ++i) {
      const auto ch = static_cast<unsigned char>(text[i]);
      if (ch == '\r' && m_connected) {
        clean += '\n';
        if (i + 1 < text.size() && text[i + 1] == '\n')
          ++i;
      } else if (ch >= 32 && ch != 127)
        clean += text[i];
      else if (m_connected && (ch == '\n' || ch == '\t'))
        clean += text[i];
    }
    auto& field = m_connected ? m_drafts[m_channelId] : m_token;
    m_edit.clamp(field);
    if (field.size() - (std::max(m_edit.caret, m_edit.anchor) - std::min(m_edit.caret, m_edit.anchor)) + clean.size()
        > (m_connected ? 8000U : 4096U))
      return;
    m_edit.replace(field, clean);
    m_selectAll = false;
    m_window.invalidate();
  }

  void ClientApplication::onKey(const Key& key) {
    if (!m_viewImage.empty()) {
      if (key.control && (key.symbol == XKB_KEY_q || key.symbol == XKB_KEY_Q)) {
        m_window.close();
        return;
      }
      if (key.control && (key.symbol == XKB_KEY_c || key.symbol == XKB_KEY_C)) {
        const auto found = m_images.find(m_viewImage);
        if (found != m_images.end() && !found->second.bytes.empty()) {
          const auto& bytes = found->second.bytes;
          std::string mime;
          if (bytes.size() >= 8 && bytes[0] == 0x89 && bytes[1] == 'P')
            mime = "image/png";
          else if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8)
            mime = "image/jpeg";
          else if (bytes.size() >= 12 && std::string_view(reinterpret_cast<const char*>(bytes.data() + 8), 4) == "WEBP")
            mime = "image/webp";
          if (!mime.empty())
            m_window.copy({bytes.begin(), bytes.end()}, mime);
        }
      } else if (key.symbol == XKB_KEY_plus || key.symbol == XKB_KEY_equal || key.symbol == XKB_KEY_KP_Add)
        m_viewZoom = std::min(4.0F, m_viewZoom * 1.2F);
      else if (key.symbol == XKB_KEY_minus || key.symbol == XKB_KEY_KP_Subtract)
        m_viewZoom = std::max(0.25F, m_viewZoom / 1.2F);
      else if (key.symbol == XKB_KEY_Escape)
        m_viewImage.clear();
      else if (key.symbol == XKB_KEY_Left)
        changeImage(-1);
      else if (key.symbol == XKB_KEY_Right)
        changeImage(1);
      else if (key.symbol == XKB_KEY_0)
        m_viewZoom = 1;
      m_window.invalidate();
      return;
    }
    if (key.control
        && (key.symbol == XKB_KEY_plus
            || key.symbol == XKB_KEY_equal
            || key.symbol == XKB_KEY_KP_Add
            || key.symbol == XKB_KEY_minus
            || key.symbol == XKB_KEY_KP_Subtract
            || key.symbol == XKB_KEY_0
            || key.symbol == XKB_KEY_KP_0)) {
      const bool reset = key.symbol == XKB_KEY_0 || key.symbol == XKB_KEY_KP_0;
      const bool smaller = key.symbol == XKB_KEY_minus || key.symbol == XKB_KEY_KP_Subtract;
      m_window.setUiScale(reset ? 1 : m_window.uiScale() * (smaller ? 1 / 1.1F : 1.1F));
      saveSelection();
      return;
    }
    if (key.control && (key.symbol == XKB_KEY_q || key.symbol == XKB_KEY_Q)) {
      m_window.close();
      return;
    }
    if (key.control && key.symbol == XKB_KEY_comma && m_connected) {
      openSettings();
      m_window.invalidate();
      return;
    }
    if (key.symbol == XKB_KEY_Tab && m_viewImage.empty()) {
      (void)m_controlInput.cycleTabFocus(key.shift);
      m_window.invalidate();
      return;
    }
    if (key.symbol != XKB_KEY_Escape && m_controlInput.focusedArea()) {
      const auto unicode = key.text.empty() ? 0U : static_cast<std::uint32_t>(g_utf8_get_char(key.text.c_str()));
      m_controlInput.keyEvent(
          key.symbol, unicode, (key.control ? KeyMod::Ctrl : 0) | (key.shift ? KeyMod::Shift : 0), true
      );
      m_window.invalidate();
      return;
    }
    if (m_settingsOpen) {
      settingsKey(key);
      return;
    }
    if (key.symbol == XKB_KEY_Escape && m_emojiMenu) {
      m_emojiMenu = false;
      m_window.invalidate();
      return;
    }
    if (key.symbol == XKB_KEY_Escape && m_presenceMenu) {
      m_presenceMenu = false;
      m_window.invalidate();
      return;
    }
    if (key.control && (key.symbol == XKB_KEY_r || key.symbol == XKB_KEY_R)) {
      refresh();
      return;
    }
    if (key.control
        && (key.symbol == XKB_KEY_c || key.symbol == XKB_KEY_C || key.symbol == XKB_KEY_x || key.symbol == XKB_KEY_X)) {
      if (m_connected && m_fieldFocused) {
        auto& field = m_drafts[m_channelId];
        m_edit.clamp(field);
        if (m_edit.selected()) {
          m_window.copy(m_edit.selection(field));
          if ((key.symbol == XKB_KEY_x || key.symbol == XKB_KEY_X) && !m_sending)
            m_edit.replace(field, {});
        }
      } else if (!m_fieldFocused && m_chatEdit.selected() && !m_selectedText.empty())
        m_window.copy(m_chatEdit.selection(m_selectedText));
      m_window.invalidate();
      return;
    }
    if (key.control && (key.symbol == XKB_KEY_v || key.symbol == XKB_KEY_V)) {
      m_pasteConnected = m_connected;
      m_pasteSession = m_session;
      m_pasteChannel = m_channelId;
      if (m_fieldFocused)
        m_window.paste(m_connected && !key.shift);
      return;
    }
    if (key.symbol == XKB_KEY_Tab) {
      if (m_connected && m_fieldFocused)
        insert("    ");
      else
        m_fieldFocused = true;
      m_window.invalidate();
      return;
    }
    if (key.symbol == XKB_KEY_Return || key.symbol == XKB_KEY_KP_Enter) {
      if (m_connected) {
        if (key.shift)
          insert("\n");
        else
          send();
      } else if (m_showTokenEntry)
        connect();
      else
        beginLogin();
      return;
    }
    if (!m_fieldFocused || m_connecting || (m_connected && (m_demo || m_sending)))
      return;
    auto& field = m_connected ? m_drafts[m_channelId] : m_token;
    m_edit.clamp(field);
    if (key.control && (key.symbol == XKB_KEY_a || key.symbol == XKB_KEY_A)) {
      m_edit.anchor = 0;
      m_edit.caret = field.size();
      m_selectAll = true;
    } else if (key.symbol == XKB_KEY_Escape) {
      m_edit.anchor = m_edit.caret;
      m_selectAll = false;
    } else if (key.symbol == XKB_KEY_BackSpace || key.symbol == XKB_KEY_Delete)
      m_edit.erase(field, key.symbol == XKB_KEY_Delete);
    else if (key.symbol == XKB_KEY_Left || key.symbol == XKB_KEY_Right)
      m_edit.move(field, key.symbol == XKB_KEY_Right, key.shift);
    else if (key.symbol == XKB_KEY_Home || key.symbol == XKB_KEY_End) {
      if (key.symbol == XKB_KEY_Home) {
        const auto at = field.rfind('\n', m_edit.caret ? m_edit.caret - 1 : 0);
        m_edit.caret = key.control || at == std::string::npos ? 0 : at + 1;
      } else {
        const auto at = field.find('\n', m_edit.caret);
        m_edit.caret = key.control || at == std::string::npos ? field.size() : at;
      }
      if (!key.shift)
        m_edit.anchor = m_edit.caret;
    } else if ((key.symbol == XKB_KEY_Up || key.symbol == XKB_KEY_Down) && m_connected && !m_draftStops.empty()) {
      const auto at = std::ranges::find(m_draftOffsets, m_edit.caret);
      if (at != m_draftOffsets.end()) {
        const auto& stop = m_draftStops[at - m_draftOffsets.begin()];
        positionCaret(
            m_draftX + stop.x, m_draftY + stop.y + stop.height * (key.symbol == XKB_KEY_Up ? -0.5F : 1.5F), key.shift
        );
      }
    } else if (!key.control)
      insert(key.text);
    m_window.invalidate();
  }

  void ClientApplication::activate(const Hit& hit) {
    switch (hit.action) {
    case Action::Settings:
      openSettings();
      break;
    case Action::CloseSettings:
      m_settingsOpen = false;
      m_settingsField.clear();
      break;
    case Action::SettingsPage:
      if (!m_settingsOpen)
        openSettings();
      selectSettingsPage(hit.id);
      break;
    case Action::SaveProfile: {
      if (m_demo) {
        m_settingsStatus = "Demo mode is read-only.";
        break;
      }
      if (m_savingProfile || m_loadingProfile)
        break;
      auto edited = m_account;
      edited.displayName = m_settingsFields["display_name"];
      edited.pronouns = m_settingsFields["pronouns"];
      edited.bio = m_settingsFields["bio"];
      m_savingProfile = m_client.saveProfile(edited);
      m_settingsStatus = m_savingProfile ? "Saving profile…" : "Check the field lengths and your connection.";
      break;
    }
    case Action::ResetProfile:
      if (!m_savingProfile)
        resetProfile();
      break;
    case Action::SetPresence:
      setPresence(hit.id);
      break;
    case Action::SaveCustomStatus:
      setPresence(discord::validStatus(m_presence.status) ? m_presence.status : "online", true);
      break;
    case Action::TogglePresence:
      m_presenceMenu = !m_presenceMenu;
      m_fieldFocused = false;
      break;
    case Action::AccountPreference: {
      if (!m_settingsLoaded || m_loadingSettings || m_savingSettings)
        break;
      auto key = hit.id;
      const auto colon = key.find(':');
      const auto field = key.substr(0, colon);
      if (!m_settingsDraft.contains(field))
        break;
      auto value = m_settingsDraft[field];
      if (colon == std::string::npos)
        value = !value;
      else {
        const auto number = std::stoi(key.substr(colon + 1));
        if (field == "friend_sources") {
          if (number == 8)
            value = value & 8 ? value & ~8 : 14;
          else {
            value ^= number;
            if ((value & 6) != 6)
              value &= ~8;
          }
        } else
          value = number;
      }
      if (discord::validSettingChanges({{field, value}}))
        m_settingsDraft[field] = value;
      break;
    }
    case Action::SaveAccountSettings: {
      if (m_savingSettings || m_loadingSettings || !m_settingsLoaded)
        break;
      if (m_demo) {
        m_settingsStatus = "Demo mode is read-only.";
        break;
      }
      discord::SettingValues changes;
      for (const auto& [key, value] : m_settingsDraft)
        if (!m_accountSettings.contains(key) || m_accountSettings.at(key) != value)
          changes[key] = value;
      m_savingSettings = m_client.saveAccountSettings(std::move(changes));
      m_settingsStatus =
          m_savingSettings ? "Saving account settings…" : "No changes to save, or the connection is unavailable.";
      break;
    }
    case Action::ResetAccountSettings:
      if (!m_savingSettings) {
        m_settingsDraft = m_accountSettings;
        m_settingsStatus.clear();
      }
      break;
    case Action::Preference:
      if (hit.id.starts_with("theme:"))
        m_preferences.theme = hit.id.substr(6);
      else if (hit.id == "font+")
        m_preferences.messageSize = std::min(20.0F, m_preferences.messageSize + 1);
      else if (hit.id == "font-")
        m_preferences.messageSize = std::max(12.0F, m_preferences.messageSize - 1);
      else if (hit.id == "compact")
        m_preferences.compact = !m_preferences.compact;
      else if (hit.id == "embeds")
        m_preferences.showEmbeds = !m_preferences.showEmbeds;
      else if (hit.id == "images")
        m_preferences.showImages = !m_preferences.showImages;
      else if (hit.id.starts_with("zoom")) {
        m_window.setUiScale(hit.id == "zoom0" ? 1 : m_window.uiScale() * (hit.id == "zoom+" ? 1.1F : 1 / 1.1F));
        saveSelection();
      }
      savePreferences();
      break;
    case Action::None:
      break;
    case Action::ImagePan:
      m_dragImage = true;
      m_imageDragX = m_pointerX;
      m_imageDragY = m_pointerY;
      break;
    case Action::ImageZoom:
      m_viewZoom = hit.id == "fit" ? 1 : std::clamp(m_viewZoom * (hit.id == "in" ? 1.2F : 1 / 1.2F), 0.25F, 4.0F);
      break;
    case Action::Minimize:
      m_window.minimize();
      break;
    case Action::Maximize:
      m_window.toggleMaximized();
      break;
    case Action::ViewImage:
      m_viewImage = hit.id;
      m_viewZoom = 1;
      m_viewPanX = m_viewPanY = 0;
      for (const auto& image : m_viewerImages)
        if (image.path == hit.id)
          m_viewUrl = image.url;
      m_fieldFocused = false;
      m_dragDraft = m_dragMessage = false;
      break;
    case Action::CloseImage:
      m_viewImage.clear();
      break;
    case Action::ImageNext:
      changeImage(1);
      break;
    case Action::ImagePrevious:
      changeImage(-1);
      break;
    case Action::Close:
      m_window.close();
      break;
    case Action::Connect:
      connect();
      break;
    case Action::DiscordLogin:
      beginLogin();
      break;
    case Action::ToggleToken:
      m_login.cancel();
      m_showTokenEntry = !m_showTokenEntry;
      m_fieldFocused = m_showTokenEntry;
      m_status.clear();
      break;
    case Action::CancelLogin:
      m_login.cancel();
      m_status.clear();
      break;
    case Action::Demo:
      demo();
      break;
    case Action::Guild:
      m_restoreChannel.clear();
      selectGuild(hit.id);
      break;
    case Action::DirectMessage: {
      m_restoreChannel = hit.id;
      showDirectMessages();
      if (m_channelId != hit.id && std::ranges::find(m_channels, hit.id, &discord::Channel::id) != m_channels.end())
        selectChannel(hit.id);
      break;
    }
    case Action::Home:
      m_restoreChannel.clear();
      showDirectMessages();
      break;
    case Action::Category:
      if (!m_collapsedCategories.erase(hit.id))
        m_collapsedCategories.insert(hit.id);
      break;
    case Action::Channel:
      selectChannel(hit.id);
      break;
    case Action::Folder:
      if (!m_openFolders.erase(hit.id))
        m_openFolders.insert(hit.id);
      break;
    case Action::ToggleMembers:
      m_showMembers = !m_showMembers;
      break;
    case Action::PasteClipboard:
      if (!m_demo && !m_channelId.empty()) {
        focusControl("composer");
        m_pasteInput = "composer";
        m_pasteConnected = m_connected;
        m_pasteSession = m_session;
        m_pasteChannel = m_channelId;
        m_window.paste(true);
      }
      break;
    case Action::ToggleEmoji:
      m_emojiMenu = !m_emojiMenu;
      break;
    case Action::InsertEmoji:
      m_emojiMenu = false;
      focusControl("composer");
      if (!m_demo)
        pasteControl("composer", hit.id);
      break;
    case Action::Send:
      send();
      break;
    case Action::Token:
      m_fieldFocused = true;
      m_selectAll = false;
      m_edit.end(m_token);
      break;
    case Action::Composer:
      m_fieldFocused = true;
      m_selectAll = false;
      m_dragDraft = true;
      positionCaret(m_pointerX, m_pointerY, false);
      break;
    case Action::SelectMessage:
      m_fieldFocused = false;
      m_selectedMessage = hit.id;
      m_selectedText = m_messageText[hit.id];
      m_dragMessage = true;
      positionChatSelection(m_pointerX, m_pointerY);
      m_chatEdit.anchor = m_chatEdit.caret;
      break;
    case Action::CopyMessage:
      m_window.copy(m_messageText[hit.id]);
      break;
    case Action::CopyCode:
      m_window.copy(hit.id);
      break;
    case Action::OpenUrl:
      auth::openExternalUrl(hit.id);
      break;
    case Action::RevealSpoiler:
      m_revealedSpoilers.insert(hit.id);
      break;
    case Action::RemoveAttachment:
      if (!m_sending)
        std::erase_if(m_attachments[m_channelId], [&](auto& image) {
          if (image.key != hit.id)
            return false;
          m_window.context().textureManager().unload(image.texture);
          return true;
        });
      break;
    case Action::SignOut:
      clearAccountSettings();
      m_controlInput.setFocus(nullptr);
      m_focusedInput.clear();
      m_emojiMenu = false;
      m_settingsOpen = m_presenceMenu = m_savingProfile = m_loadingProfile = false;
      m_desiredPresence.clear();
      m_settingsStatus.clear();
      m_settingsField.clear();
      m_settingsFields.clear();
      m_viewImage.clear();
      m_presence = {};
      m_login.cancel();
      m_client.disconnect();
      m_connected = false;
      m_demo = false;
      m_connecting = false;
      m_sending = false;
      m_live = false;
      m_dmMode = false;
      m_privateChannels.clear();
      m_userDirectory = {};
      m_unreadDMs.clear();
      m_unreadChannels.clear();
      m_unreadGuilds.clear();
      m_localRead.clear();
      m_guildChannels.clear();
      m_channelCacheOrder.clear();
      m_channelMessages.clear();
      m_channelMembers.clear();
      m_lastChannels.clear();
      m_collapsedCategories.clear();
      m_token.clear();
      m_account = {};
      m_guilds.clear();
      m_channels.clear();
      m_messages.clear();
      m_messageLayouts.clear();
      m_members.clear();
      m_folders.clear();
      m_openFolders.clear();
      m_liveChanges.clear();
      for (auto& [_, images] : m_attachments)
        for (auto& image : images)
          m_window.context().textureManager().unload(image.texture);
      m_attachments.clear();
      m_pendingAttachments.clear();
      m_edit = {};
      m_selectedText.clear();
      m_selectedMessage.clear();
      m_drafts.clear();
      m_guildId.clear();
      m_channelId.clear();
      m_pendingDraft.clear();
      m_status.clear();
      m_fieldFocused = true;
      m_showTokenEntry = false;
      break;
    }
    m_window.invalidate();
  }
} // namespace app
