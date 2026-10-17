#include "app/client_application.h"

#include "app/message_layout.h"
#include "app/reply_preview.h"
#include "app/theme.h"
#include "core/deferred_call.h"
#include "core/environment.h"
#include "core/files/resource_paths.h"
#include "core/input/key_modifiers.h"
#include "core/timer_manager.h"
#include "render/core/texture_manager.h"
#include "render/scene/image_node.h"
#include "render/scene/input_area.h"
#include "render/scene/rect_node.h"
#include "render/scene/text_node.h"
#include "ui/controls/scrollbar.h"
#include "ui/palette.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iostream>
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
    const auto* trace = std::getenv("CRESSIDA_TRACE_SCROLL");
    m_traceScroll = trace && std::string_view(trace) == "1";
    const auto* animation = std::getenv("CRESSIDA_SCROLL_ANIMATION");
    m_animateWheel = !animation || std::string_view(animation) != "0";
    m_emojiCatalog.load(paths::assetPath("emoji.json"), paths::assetPath("emoji-aliases.json"));
    m_edit.end(m_token);
    m_showTokenEntry = !m_token.empty();
    m_fieldFocused = m_showTokenEntry;
    m_window.draw = [this] {
      const auto drawStarted = std::chrono::steady_clock::now();
      prepareDemo();
      draw();
      if (m_demo && m_demoOverlay == "history-cycle")
        demoHistoryStep();
      if (m_demo && m_demoOverlay == "interaction-cycle")
        demoInteractionStep();
      if (m_demo && m_demoOverlay == "gif-cycle")
        demoGifStep();
      if (m_demo && m_demoOverlay == "unread-cycle") {
        const auto step = m_demoInteractionStep++;
        const auto first = m_unreadTimeline.first(m_messages);
        const bool banner = std::ranges::any_of(m_hits, [](const auto& hit) { return hit.action == Action::MarkRead; });
        const float chrome = m_window.titlebarHeight() - 36;
        auto clickAction = [&](Action action) {
          const auto at = std::ranges::find(m_hits, action, &Hit::action);
          if (at == m_hits.end())
            throw std::runtime_error("Unread banner action is missing");
          const float x = at->x + at->width / 2, y = at->y + at->height / 2 + chrome;
          m_window.motion(x, y);
          m_window.click(x, y, 272);
          m_window.released();
        };
        auto newMessage = [&](const std::string& id) {
          const auto previous = m_messages.back().id;
          m_messages.push_back({id, m_channelId, "Maya", "Another new message.", "2026-10-17T11:19:00Z"});
          m_unreadTimeline.snapshot({{m_channelId, {previous, id}}});
          m_scrolling.stop(m_messageScroll);
          m_messageScroll = m_messageMaxScroll / 2;
        };
        if (step == 0) {
          if (!first
              || !banner
              || m_messageTops.at(m_messages[*first].id) < m_messageViewportTop
              || m_messageTops.at(m_messages[*first].id) > m_messageViewportTop + 80)
            throw std::runtime_error("Opening unread history must show its boundary and top banner");
          const auto* bar = static_cast<Scrollbar*>(m_widgets.at("chat-scrollbar"));
          const auto* thumb = bar->children()[1].get();
          if (!bar->visible())
            throw std::runtime_error("Overflowing chat must have a native scrollbar");
          const float x = bar->x() + bar->width() / 2;
          const float y = bar->y() + thumb->y() + thumb->height() / 2 + chrome;
          m_window.motion(x, y);
          m_window.click(x, y, 272);
          m_window.motion(x, y + 100);
        } else if (step == 1) {
          if (!banner || m_messageScroll >= m_messageMaxScroll - 300)
            throw std::runtime_error("Dragging the scrollbar must move the chat");
          // Continue the same pointer capture after the scene has been rebuilt.
          m_window.motion(m_pointerX, m_window.height() * 3);
          m_window.released();
        } else if (step == 2) {
          if (first || banner || m_messageScroll > 1)
            throw std::runtime_error("Reaching the newest message must clear both unread indicators");
          m_window.motion(450, 300 + chrome);
          m_window.scroll(-120);
        } else if (step == 3) {
          if (first || banner)
            throw std::runtime_error("Scrolling back up must not resurrect read indicators");
          newMessage("1030");
        } else if (step == 4) {
          if (!first || *first != m_messages.size() - 1 || !banner)
            throw std::runtime_error("A later unread batch must start at its own boundary");
          clickAction(Action::MarkRead);
        } else if (step == 5) {
          if (first || banner)
            throw std::runtime_error("Mark as Read must clear the divider and banner while scrolled up");
          m_unreadTimeline.snapshot({{m_channelId, {"1029", "1030"}}});
          if (m_unreadTimeline.unread())
            throw std::runtime_error("A delayed Gateway snapshot must not undo a local read");
          newMessage("1031");
        } else if (step == 6) {
          if (!first || !banner)
            throw std::runtime_error("New arrivals after an acknowledgement must remain visible");
          clickAction(Action::JumpUnread);
        } else if (step == 7) {
          if (first || banner)
            throw std::runtime_error("Jumping to the newest unread message must catch up");
          m_unreadTimeline.snapshot({{m_channelId, {"1001", "1031"}}}, m_channelId);
          m_localRead.erase(m_channelId);
          m_messageScroll = m_messageMaxScroll / 2;
        } else if (step == 8) {
          if (!first || !banner)
            throw std::runtime_error("An explicit manual rewind must restore unread indicators");
          m_unreadTimeline.snapshot({});
        } else if (first || banner)
          throw std::runtime_error("Another device reading the channel must clear both indicators");
        std::clog << "Offline unread replay step " << step + 1 << " passed\n";
        m_window.invalidate();
      }
      if (m_demo
          && (m_demoOverlay == "scroll-cycle"
              || m_demoOverlay == "emoji-scroll-cycle"
              || m_demoOverlay == "gif-scroll-cycle")) {
        const auto step = m_demoInteractionStep++ % 16;
        const bool emoji = m_demoOverlay == "emoji-scroll-cycle", gif = m_demoOverlay == "gif-scroll-cycle";
        auto& offset = emoji ? m_emojiScroll : gif ? m_gifScroll : m_messageScroll;
        if (step == 0 || step == 4 || step == 8 || step == 12) {
          const float x = emoji ? m_emojiBounds.x + 100 : gif ? m_gifBounds.x + 100 : 450;
          const float y = emoji ? m_emojiBounds.y + 200 : gif ? m_gifBounds.y + 200 : 200;
          m_window.motion(x, y + m_window.titlebarHeight() - 36);
          const float amount = step == 8 ? -48 : 120;
          m_window.scroll(emoji || gif ? amount : -amount);
          if (step == 8 && m_scrolling.target(offset) >= offset)
            throw std::runtime_error("Scroll reversal must change direction immediately");
        }
        if (emoji) {
          const float top = m_emojiBounds.y + 104, bottom = m_emojiBounds.y + m_emojiBounds.height - 54;
          for (const auto& hit : m_hits)
            if ((hit.action == Action::PickEmoji || hit.action == Action::FavoriteEmoji)
                && (hit.y < top || hit.y + hit.height > bottom + 0.01F))
              throw std::runtime_error("Emoji hit regions must be clipped to the scrolling grid");
        }
        std::clog
            << "Offline "
            << m_demoOverlay
            << " frame "
            << m_demoInteractionStep
            << " offset "
            << offset
            << " draw_ms "
            << std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - drawStarted).count()
            << '\n';
        m_window.invalidate();
      }
    };
    m_window.motion = [this](float x, float y) {
      m_pointerX = x;
      y += 36 - m_window.titlebarHeight();
      m_pointerY = y;
      m_controlInput.pointerMotion(x, y + m_window.titlebarHeight() - 36, 0);
      contextMotion(x, y);
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
      if (overlayClick(x, y))
        return;
      if (m_emojiMenu
          && m_emojiBounds.contains(x, y)
          && y >= m_emojiBounds.y + 52
          && y < m_emojiBounds.y + 90
          && m_controlInput.pointerButton(x, y + m_window.titlebarHeight() - 36, 272, true, 0, 0, false)) {
        m_window.invalidate();
        return;
      }
      if (m_gifMenu
          && m_gifBounds.contains(x, y)
          && y >= m_gifBounds.y + 52
          && y < m_gifBounds.y + 90
          && m_controlInput.pointerButton(x, y + m_window.titlebarHeight() - 36, 272, true, 0, 0, false)) {
        m_window.invalidate();
        return;
      }
      if (m_viewImage.empty()
          && !m_presenceMenu
          && !m_emojiMenu
          && !m_gifMenu
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
    m_window.contextClick = [this](float x, float y) {
      y += 36 - m_window.titlebarHeight();
      m_pointerX = x;
      m_pointerY = y;
      m_dragDraft = m_dragMessage = false;
      openContext(x, y);
    };
    m_window.key = [this](const Key& key) { onKey(key); };
    m_window.animate = [this] {
      const bool active = m_scrolling.tick();
      if (m_showMembers && !m_dmMode && !m_demo && !m_guildId.empty() && !m_channelId.empty()) {
        const int page = static_cast<int>(m_memberScroll / 44) / 100 * 100;
        if (page != m_memberPage) {
          m_memberPage = page;
          m_client.subscribe(m_guildId, m_channelId, page);
        }
      }
      return active;
    };
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
        if (id == "dialog" && m_pasteDialogGeneration != m_dialogGeneration)
          return;
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
      auto scroll = [&](float& offset, float amount) {
        const bool precise = m_window.preciseScroll();
        m_scrolling.scroll(offset, ScrollMotion::distance(amount, precise), precise || !m_animateWheel);
        if (m_traceScroll)
          std::clog
              << "scroll input surface="
              << (&offset == &m_messageScroll     ? "chat"
                      : &offset == &m_gifScroll   ? "gif"
                      : &offset == &m_emojiScroll ? "emoji"
                                                  : "other")
              << " delta="
              << amount
              << " offset="
              << offset
              << " target="
              << m_scrolling.target(offset)
              << " precise="
              << precise
              << " animation="
              << m_animateWheel
              << '\n';
      };
      if (m_emojiMenu) {
        if (m_emojiBounds.contains(m_pointerX, m_pointerY))
          scroll(m_pointerX < m_emojiBounds.x + 56 ? m_emojiRailScroll : m_emojiScroll, delta);
        m_window.invalidate();
        return;
      }
      if (m_gifMenu) {
        if (m_gifBounds.contains(m_pointerX, m_pointerY))
          scroll(m_gifScroll, delta);
        m_window.invalidate();
        return;
      }
      if (m_contextOpen) {
        scrollContext(delta);
        return;
      }
      if (!m_dialogKind.empty())
        return;
      if (m_settingsOpen) {
        scroll(m_pointerX < m_settingsNavRight ? m_settingsNavScroll : m_settingsScroll, delta);
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
        scroll(m_attachmentScroll, delta);
      else if (m_pointerX < 78)
        scroll(m_guildScroll, delta);
      else if (m_pointerX < 326)
        scroll(m_channelScroll, delta);
      else if (m_showMembers && m_window.width() >= 1000 && m_pointerX >= m_window.width() - 240) {
        scroll(m_memberScroll, delta);
      } else
        scroll(m_messageScroll, -delta);
      m_window.invalidate();
    };
    if (demoMode)
      demo();
  }

  int ClientApplication::run(bool smokeTest) {
    if (ContextMenuControl::compiledEntrySize() != sizeof(ContextMenuControlEntry))
      throw std::runtime_error(
          "The renderer library is stale: its menu layout differs from the client. Run just rebuild."
      );
    m_window.initialize();
    if (!m_demo) {
      if (!m_token.empty())
        connect();
      else
        restoreLogin();
    }
    const auto* view = cressida::environment("CRESSIDA_DEMO_VIEW", "DISCORD_CPP_DEMO_VIEW");
    const bool interactionCycle = m_demo
        && view
        && (std::string_view(view) == "interaction-cycle"
            || std::string_view(view) == "animation-cycle"
            || std::string_view(view) == "gif-cycle"
            || std::string_view(view) == "scroll-cycle"
            || std::string_view(view) == "emoji-scroll-cycle"
            || std::string_view(view) == "gif-scroll-cycle"
            || std::string_view(view) == "history-cycle");
    m_window.run(
        m_client.eventFd(), [this] { tick(); },
        smokeTest ? (m_demo && view && std::string_view(view) == "unread-cycle" ? 10
                         : interactionCycle                                     ? 64
                                                                                : 1)
                  : 0,
        m_assets.eventFd()
    );
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
        if (view == "message-menu"
            || view == "interaction-cycle"
            || view == "reaction-submenu"
            || view == "reaction-people"
            || view == "reaction-loading"
            || view == "user-menu"
            || view == "reply"
            || view == "edit-message"
            || view == "block-user"
            || view == "user-profile"
            || view == "reactions"
            || view == "tooltip-long"
            || view == "blocked") {
          m_demoOverlay = view;
          // Keep interaction previews focused on the conversation, rather than
          // scrolling it above the large media fixture used by the regular demo.
          m_messages.resize(5);
          m_messages.back().embeds.clear();
          const std::vector<std::string> authors{"11", "12", "1", "11", "12", "1"};
          for (std::size_t i = 0; i < m_messages.size() && i < authors.size(); ++i)
            m_messages[i].authorId = authors[i];
          m_messages[2].reactions = {{"", "👍", 3, true}, {"", "🎉", 2}, {"", "❤️", 5}};
          m_messages[1].reply = discord::MessageReference{"1", m_channelId, "11", "Maya", m_messages.front().content};
          discord::UserUpdates users;
          users.users["11"].relationship = view == "blocked" ? 2 : 1;
          m_userDirectory.apply(users);
          if (view == "reply")
            m_replies[m_channelId] = {"1", m_channelId, "11", "Maya", m_messages.front().content};
        }
        if (view == "login-restoring" || view == "login-retry") {
          m_connected = false;
          m_connecting = m_restoringLogin = view == "login-restoring";
          m_retrySavedLogin = view == "login-retry";
          m_status = m_restoringLogin ? "Your desktop keyring may ask you to unlock it."
                                      : "Could not reach Discord. Your saved sign-in is still available.";
        } else if (
            view == "gif-picker"
            || view == "gif-favorites"
            || view == "animated-media"
            || view == "animation-cycle"
            || view == "gif-cycle"
            || view == "gif-scroll-cycle"
            || view == "animated-image"
        ) {
          for (int i = 0; i < 8; ++i) {
            const auto path = "https://static.klipy.com/ii/cressida-demo-" + std::to_string(i) + ".gif";
            auto animation = std::make_shared<media::Animation>();
            animation->width = 96;
            animation->height = 64 + i * 8;
            animation->duration = std::chrono::milliseconds(240);
            for (int f = 0; f < 3; ++f) {
              media::Frame frame;
              frame.duration = std::chrono::milliseconds(80);
              frame.rgba.resize(animation->width * animation->height * 4);
              for (int py = 0; py < animation->height; ++py)
                for (int px = 0; px < animation->width; ++px) {
                  const auto index = (py * animation->width + px) * 4;
                  frame.rgba[index] = 60 + (px + i * 17 + f * 24) % 160;
                  frame.rgba[index + 1] = 60 + (py + i * 12) % 150;
                  frame.rgba[index + 2] = 180 + f * 20;
                  frame.rgba[index + 3] = 255;
                }
              animation->frames.push_back(std::move(frame));
            }
            auto& cached = m_images[path];
            cached.animation = animation;
            cached.texture = m_window.context().textureManager().loadFromRgba(
                animation->frames[0].rgba.data(), animation->width, animation->height
            );
            discord::Gif gif;
            gif.url = "https://klipy.com/gifs/cressida-demo-" + std::to_string(i);
            gif.src = path;
            gif.width = animation->width;
            gif.height = animation->height;
            gif.title = "Offline animation " + std::to_string(i + 1);
            m_gifs.push_back(gif);
            if (i % 2 == 0)
              m_gifFavorites.push_back(gif);
          }
          m_gifFavoritesLoaded = true;
          if (view == "gif-cycle" || view == "gif-scroll-cycle")
            m_demoOverlay = view;
          if (view == "animated-media" || view == "animation-cycle" || view == "animated-image") {
            m_messages.resize(1);
            m_messages[0].content = "Native looping media";
            discord::Embed gif;
            gif.type = "gifv";
            gif.url = m_gifs[0].url;
            gif.video = {m_gifs[0].src, 480, 320};
            m_messages[0].embeds = {gif};
            m_messageLayouts.clear();
            if (view == "animated-image") {
              m_viewImage = m_gifs[0].src;
              m_viewUrl = m_gifs[0].url;
            }
          } else {
            m_gifMenu = true;
            m_gifFavoritesTab = view == "gif-favorites";
            m_focusedInput = "gif-search";
          }
        } else if (
            view == "unread-divider"
            || view == "unread-divider-light"
            || view == "unread-jump"
            || view == "unread-cycle"
        ) {
          if (view == "unread-divider-light")
            m_preferences.theme = "light";
          m_messages.clear();
          const bool longHistory = view == "unread-jump" || view == "unread-cycle";
          for (int i = 0; i < (longHistory ? 30 : 6); ++i)
            m_messages.push_back(
                {std::to_string(1000 + i), m_channelId, i % 2 ? "Maya" : "Alex",
                 i < 2 ? "A message from before your last visit."
                       : "Something new in the conversation.\nA little room to catch up with everyone.",
                 "2026-10-17T09:41:00Z"}
            );
          m_messageLayouts.clear();
          m_unreadTimeline.snapshot({{m_channelId, {"1001", m_messages.back().id}}});
          m_unreadTimeline.open(m_channelId);
          if (view == "unread-jump")
            m_unreadTimeline.positioned();
          if (view == "unread-cycle")
            m_demoOverlay = view;
        } else if (
            view == "images-loading"
            || view == "images-loading-light"
            || view == "images-failed"
            || view == "image-loading"
            || view == "image-failed"
            || view == "images-ready"
        ) {
          m_messages = {m_messages.back()};
          m_messageLayouts.clear();
          if (view == "images-loading-light")
            m_preferences.theme = "light";
          if (view != "images-ready")
            m_images["demo-preview"].texture = {};
          if (view == "images-failed" || view == "image-failed")
            m_images["demo-preview"].retry = std::chrono::steady_clock::now() + std::chrono::seconds(60);
          if (view == "image-loading" || view == "image-failed") {
            m_viewImage = "demo-preview";
            m_viewUrl = "https://example.com/preview.png";
            m_viewerImages = {{m_viewImage, m_viewUrl, 1280, 720}};
          }
        } else if (
            view == "chat-loading"
            || view == "chat-loading-light"
            || view == "chat-loading-compact"
            || view == "chat-loading-cached"
            || view == "chat-empty"
            || view == "channels-loading"
        ) {
          m_preferences.compact = view == "chat-loading-compact";
          if (view == "chat-loading-light")
            m_preferences.theme = "light";
          m_loadingMessages = view != "chat-empty" && view != "channels-loading";
          if (view != "chat-loading-cached") {
            m_messages.clear();
            m_messageLayouts.clear();
          }
          if (view == "channels-loading") {
            m_channelId.clear();
            m_channels.clear();
            m_loadingChannels = true;
          }
        } else if (view == "reply-overlap") {
          m_messages.clear();
          for (int i = 0; i < 6; ++i) {
            discord::Message message{
                std::to_string(4000 + i), m_channelId, "Maya", "This reply body stays below its preview.",
                "2026-10-17T09:41:00Z"
            };
            message.reply = discord::MessageReference{
                "1", m_channelId, "11", "Alex",
                "The Linux kernel got open-sourced btw\nLike, the entire thing is just open to download on GitHub "
                "now\nhttps://github.com/torvalds/linux"
            };
            m_messages.push_back(std::move(message));
          }
          m_messageLayouts.clear();
        } else if (view == "history-cycle") {
          m_demoOverlay = view;
          m_messages.clear();
          m_messageLayouts.clear();
          m_unreadTimeline.snapshot({});
          m_unreadTimeline.positioned();
          loadHistory(discord::HistoryDirection::Latest);
        } else if (view == "scroll-cycle") {
          m_demoOverlay = view;
          m_messages.clear();
          for (int i = 0; i < 100; ++i)
            m_messages.push_back(
                {std::to_string(3000 + i), m_channelId, "Maya",
                 "Smooth native scrolling through a conversation.\nA second line keeps the timeline long enough to "
                 "scroll.",
                 "2026-10-17T09:41:00Z"}
            );
          m_messageLayouts.clear();
        } else if (view == "emoji-sizing" || view == "emoji-sizing-compact") {
          m_preferences.compact = view == "emoji-sizing-compact";
          prepareEmojiPicker();
          m_showMembers = false;
          m_messages.clear();
          const auto* emoji = m_emojiCatalog.find("800000000000000000");
          const auto custom = emoji ? emoji->token() : "<:wave:123>";
          const std::vector<std::string> samples = {
              "Inline emoji: 🙂 ❤️ 👩🏽‍💻 " + custom + " with normal text.", "🙂 ❤️ " + custom,
              "👨‍👩‍👧‍👦 🇩🇪 1️⃣ 👩🏽‍💻", "`🙂 " + custom + "` stays literal in code."
          };
          for (std::size_t i = 0; i < samples.size(); ++i)
            m_messages.push_back({std::to_string(8000 + i), m_channelId, "Maya", samples[i], "2026-10-17T09:41:00Z"});
          m_messageLayouts.clear();
        } else if (view == "media-only") {
          prepareEmojiPicker();
          const auto* emoji = m_emojiCatalog.find("800000000000000000");
          if (emoji) {
            const auto url = "https://cdn.discordapp.com" + discord::emojiPath(emoji->id, emoji->animated);
            m_messages.resize(1);
            m_messages[0].content = "[woe](" + url + ")";
            discord::Embed preview;
            preview.type = "image";
            preview.url = url;
            preview.thumbnail = {discord::emojiPath(emoji->id, emoji->animated), 160, 160};
            m_messages[0].embeds = {preview};
            m_messageLayouts.clear();
          }
        } else if (view == "composer") {
          m_drafts[m_channelId] = "A centered message";
        } else if (view == "emoji-picker" || view == "emoji-scrolled" || view == "emoji-scroll-cycle") {
          m_emojiMenu = true;
          if (view == "emoji-scrolled")
            m_emojiScroll = 59;
          if (view == "emoji-scroll-cycle")
            m_demoOverlay = view;
        } else if (view == "unread") {
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
            || view == "expressions"
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
      Node& parent, float x, float y, float width, float height, const std::string& path, float radius, bool placeholder
  ) {
    if (path.empty() || (m_demo && !m_images.contains(path)))
      return false;
    auto global = [&](const Node* node) {
      std::pair<float, float> pos{};
      for (; node && node != m_scene.get(); node = node->parent()) {
        pos.first += node->x();
        pos.second += node->y();
      }
      return pos;
    };
    const auto [ox, oy] = global(&parent);
    const float gx = x + ox, gy = y + oy;
    float clipLeft = 0, clipTop = 36, clipRight = m_window.width(),
          clipBottom = m_window.height() + 36 - m_window.titlebarHeight();
    for (auto* node = &parent; node && node != m_scene.get(); node = node->parent())
      if (node->clipChildren()) {
        const auto [nx, ny] = global(node);
        clipLeft = std::max(clipLeft, nx);
        clipTop = std::max(clipTop, ny);
        clipRight = std::min(clipRight, nx + node->width());
        clipBottom = std::min(clipBottom, ny + node->height());
      }
    if (gx + width <= clipLeft || gx >= clipRight || gy + height <= clipTop || gy >= clipBottom)
      return false;
    auto& cached = m_images[path];
    cached.touched = ++m_imageSerial;
    if (!cached.texture.valid()) {
      if (!m_demo
          && !m_pendingImages.contains(path)
          && std::chrono::steady_clock::now() >= cached.retry
          && m_assets.request(path))
        m_pendingImages.insert(path);
      if (placeholder) {
        const bool failed = cached.retry != std::chrono::steady_clock::time_point{} && !m_pendingImages.contains(path);
        rectangle(parent, x, y, width, height, lerpColor(background, muted, 0.13F), radius);
        if (width >= 64 && height >= 64) {
          const float size = std::min(32.0F, std::min(width, height) * 0.25F);
          const bool caption = failed && width >= 160 && height >= 100;
          const float iconY = y + (height - size) / 2;
          glyph(
              parent, x + (width - size) / 2, iconY, size, failed ? "photo-off" : "photo",
              lerpColor(background, muted, 0.38F)
          );
          if (caption) {
            const auto textWidth = m_window.target().renderer().measureText("Image unavailable", 12).width;
            label(
                parent, x + (width - textWidth) / 2, iconY + size + 10, textWidth + 2, "Image unavailable", 12, muted
            );
          }
        }
      }
      return false;
    }
    if (cached.animation && cached.animation->frames.size() > 1) {
      const auto now = std::chrono::steady_clock::now();
      if (cached.visibleFrame + 1 < m_animationDraw || cached.animationStart == std::chrono::steady_clock::time_point{})
        cached.animationStart = now;
      cached.visibleFrame = m_animationDraw;
      const bool playing = (m_demo || m_window.active())
          && (m_preferences.autoplayGifs || hovered(gx, gy, width, height))
          && (m_viewImage.empty() || path == m_viewImage);
      if (playing) {
        if (cached.playingDraw + 1 < m_animationDraw)
          cached.animationStart = now;
        cached.playingDraw = m_animationDraw;
      }
      const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - cached.animationStart);
      const auto& animation = *cached.animation;
      const auto frame = playing || cached.playingDraw == m_animationDraw ? animation.frameAt(elapsed) : 0;
      if (frame != cached.animationFrame) {
        m_window.context().textureManager().updateSubImage(
            cached.texture, animation.frames[frame].rgba.data(), 0, 0, animation.width, animation.height,
            TextureDataFormat::Rgba
        );
        cached.animationFrame = frame;
      }
      if (playing) {
        auto offset = elapsed % animation.duration;
        for (std::size_t i = 0; i < frame; ++i)
          offset -= animation.frames[i].duration;
        m_animationDelay = std::min(
            m_animationDelay, std::max(std::chrono::milliseconds(10), animation.frames[frame].duration - offset)
        );
      }
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
    ++m_animationDraw;
    m_animationDelay = std::chrono::milliseconds::max();
    theme::select(m_preferences.theme);
    const float w = m_window.width(), h = m_window.height() + 36 - m_window.titlebarHeight();
    m_tooltip.clear();
    if (!m_emojiMenu && m_emojiOpened)
      closeEmojiPicker();
    m_gifTargets.clear();
    m_reactionHoveredThisFrame = false;
    m_contextTargets.clear();
    m_messageTops.clear();
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

    if (!m_connected && m_restoringLogin) {
      const float center = h / 2;
      const std::string title = "Opening Cressida…";
      const std::string subtitle = "Restoring your saved sign-in";
      auto& renderer = m_window.target().renderer();
      const auto titleWidth = renderer.measureText(title, 24, FontWeight::SemiBold).width;
      const auto subtitleWidth = renderer.measureText(subtitle, 13, FontWeight::Normal).width;
      glyph(root, (w - 28) / 2, center - 80, 28, "loader", accent);
      label(root, (w - titleWidth) / 2, center - 24, titleWidth + 2, title, 24, foreground, FontWeight::SemiBold);
      label(root, (w - subtitleWidth) / 2, center + 18, subtitleWidth + 2, subtitle, 13, muted);
      button(root, {(w - 96) / 2, center + 62, 96, 32, Action::CancelLogin, {}}, "Cancel");
    } else if (!m_connected) {
      const float extra = m_retrySavedLogin && !m_connecting ? 42 : 0;
      const float x = (w - 460) / 2, y = std::max(62.0F, (h - 360 - extra) / 2);
      rectangle(root, x, y, 460, 360 + extra, sidebar, 22);
      label(root, x + 32, y + 28, 390, "A LITTLE CLOSER TO YOUR PEOPLE", 10, accent, FontWeight::SemiBold);
      label(root, x + 32, y + 58, 390, "Welcome back.", 32, foreground, FontWeight::Bold);
      label(root, x + 32, y + 106, 390, "Connect your Discord account and settle in.", 14, muted);
      if (m_connecting) {
        label(root, x + 32, y + 163, 396, "Connecting to Discord…", 16, foreground);
        button(root, {x + 32, y + 222, 190, 38, Action::CancelLogin, {}}, "Cancel");
        button(root, {x + 238, y + 222, 190, 38, Action::Demo, {}}, "Explore demo");
        label(root, x + 32, y + 291, 396, m_status, 12, muted, FontWeight::Normal, 3);
      } else if (!m_showTokenEntry) {
        label(
            root, x + 32, y + 151, 390, "Sign in using Discord in your default browser.", 13, muted, FontWeight::Normal,
            2
        );
        button(
            root, {x + 32, y + 200, 396, 46, Action::DiscordLogin, {}},
            m_login.active() ? "Waiting for your browser…" : "Sign in with Discord", true
        );
        if (m_retrySavedLogin)
          button(root, {x + 32, y + 252, 396, 32, Action::RetryLogin, {}}, "Retry saved sign-in");
        button(root, {x + 32, y + 260 + extra, 190, 38, Action::Demo, {}}, "Explore demo");
        button(
            root, {x + 238, y + 260 + extra, 190, 38, m_login.active() ? Action::CancelLogin : Action::ToggleToken, {}},
            m_login.active() ? "Cancel sign-in" : "Use a token instead"
        );
        label(
            root, x + 32, y + 316 + extra, 396,
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
            root, x + 32, y + 306, 396, m_status.empty() ? "Sign-in is remembered in your desktop keyring." : m_status,
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
        if (m_unreadDMs.contains(dm.id) && !m_userDirectory.muted(dm.id))
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
      m_scrolling.limit(m_guildScroll, std::max(0.0F, rail.size() * 60.0F - (h - 164)));
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
          return state.second == guild.id
              && m_unreadChannels.contains(state.first)
              && !m_userDirectory.muted(state.first);
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
      m_scrolling.limit(m_channelScroll, std::max(0.0F, visibleChannels.size() * 38.0F - (h - 228)));
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
        const bool unread = (m_unreadChannels.contains(channel.id) || m_unreadDMs.contains(channel.id))
            && !m_userDirectory.muted(channel.id);
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
        if (m_dmMode)
          m_contextTargets.push_back({90, y, 224, 34, Action::ConversationMenu, channel.id});
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
      m_draftWidth = std::max(32.0F, bodyWidth - 186);
      m_draftOffsets = TextEdit::stops(draft);
      m_window.target().renderer().measureTextCursorStopsWrapped(draft, 14, m_draftOffsets, m_draftWidth, m_draftStops);
      const float fullDraftHeight =
          m_draftStops.empty() ? 22 : m_draftStops.back().y + std::max(22.0F, m_draftStops.back().height);
      m_draftHeight = std::clamp(fullDraftHeight, 22.0F, std::min(160.0F, h * 0.3F));
      const float attachmentHeight = m_attachments[m_channelId].empty() ? 0 : 106;
      const float replyHeight = m_replies.contains(m_channelId) ? 34 : 0;
      const float composerHeight = m_draftHeight + 24 + attachmentHeight + replyHeight;
      const float composerY = h - 30 - composerHeight;
      float top = m_unreadTimeline.unread() ? 166 : 122;
      const float bottom = composerY - 16, textWidth = std::max(32.0F, bodyWidth - 104);
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
      auto unreadIndex = m_unreadTimeline.first(m_messages);
      constexpr float unreadHeight = 36;
      float totalHeight = 24 + (unreadIndex ? unreadHeight : 0);
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
          cached.layout = layoutRichMessage(
              displayed, textWidth, measure, m_preferences.messageSize,
              {.fakeNitroEmojis = m_preferences.fakeNitroEmojis && m_preferences.showImages,
               .compact = m_preferences.compact,
               .unicodeEmoji = [this](std::string_view text) { return m_emojiCatalog.isUnicode(text); }}
          );
          cached.width = textWidth;
          cached.scale = renderer.renderScale();
          cached.fonts = renderer.textMetricsGeneration();
        }
        const float height = blockedMessage(message) ? 36
                                                     : std::max(22.0F, cached.layout.height)
                + (m_preferences.compact ? 26 : 34)
                + (message.reply ? 24 : 0)
                + reactionStrip(nullptr, message, 0, 0, textWidth);
        layouts.push_back(&cached.layout);
        heights.push_back(height);
        totalHeight += height;
      }
      m_messageMaxScroll = std::max(0.0F, totalHeight - (bottom - top));
      if ((m_messageScroll > 1
           || m_scrolling.target(m_messageScroll) > 1
           || !m_history.atLatest()
           || m_keepHistoryAnchor)
          && !m_unreadTimeline.positionPending()) {
        std::vector<TimelineAnchor::Row> positions;
        float rowY = bottom - totalHeight + m_messageScroll + 12;
        for (std::size_t i = 0; i < m_messages.size(); ++i) {
          if (unreadIndex && i == *unreadIndex)
            rowY += unreadHeight;
          positions.push_back({m_messages[i].id, rowY});
          rowY += heights[i];
        }
        if (const auto correction = m_timelineAnchor.correction(positions, m_messageScroll)) {
          if (m_traceScroll && *correction != 0)
            std::clog << "scroll geometry correction=" << *correction << " rows=" << m_messages.size() << '\n';
          m_scrolling.rebase(m_messageScroll, *correction);
        }
      }
      m_scrolling.limit(m_messageScroll, m_messageMaxScroll);
      m_keepHistoryAnchor = false;
      if (m_unreadTimeline.positionPending() && !m_unreadTimeline.waiting() && !m_loadingMessages) {
        if (unreadIndex) {
          float preceding = 12;
          for (std::size_t i = 0; i < *unreadIndex; ++i)
            preceding += heights[i];
          m_scrolling.stop(m_messageScroll);
          m_messageScroll = std::clamp(m_messageMaxScroll - preceding, 0.0F, m_messageMaxScroll);
        }
        m_unreadTimeline.positioned();
      }
      // Clear the banner and divider in this frame, after opening unread history
      // at its boundary. Do not acknowledge while a wheel gesture is leaving bottom.
      if ((!m_demo || m_demoOverlay == "unread-cycle")
          && !m_unreadTimeline.waiting()
          && !m_unreadTimeline.positionPending()
          && !m_loadingMessages
          && readingChannel(m_channelId)
          && m_scrolling.target(m_messageScroll) <= 1
          && markChannelRead()) {
        if (unreadIndex && !m_unreadTimeline.unread()) {
          totalHeight -= unreadHeight;
          unreadIndex.reset();
        }
        if (!m_unreadTimeline.unread())
          top = 122;
        m_messageMaxScroll = std::max(0.0F, totalHeight - (bottom - top));
        m_scrolling.limit(m_messageScroll, m_messageMaxScroll);
      }
      m_messageViewportTop = top;
      // Keep the same transform at rest: switching snapping back on can move
      // glyphs against the scroll direction in the final frame.
      content->setTextPixelSnap(false);
      if (m_traceScroll)
        std::clog
            << "scroll frame surface=chat offset="
            << m_messageScroll
            << " target="
            << m_scrolling.target(m_messageScroll)
            << " maximum="
            << m_messageMaxScroll
            << " active="
            << m_scrolling.active(m_messageScroll)
            << '\n';
      content->setPosition(left + 24, top);
      content->setSize(bodyWidth - 48, std::max(0.0F, bottom - top));
      float y = std::min(0.0F, bottom - top - totalHeight) + m_messageScroll + 12;
      for (std::size_t i = 0; i < m_messages.size(); ++i) {
        const auto& message = m_messages[i];
        if (unreadIndex && i == *unreadIndex) {
          if (y + unreadHeight >= 0 && y < bottom - top) {
            const float width = bodyWidth - 48;
            const float caption =
                m_window.target().renderer().measureText("New messages", 12, FontWeight::SemiBold).width;
            const float chipWidth = caption + 44, chipX = (width - chipWidth) / 2;
            const float sideWidth = std::max(0.0F, chipX - 12);
            for (int segment = 0; segment < 8; ++segment) {
              const float length = sideWidth / 8;
              const auto color = lerpColor(background, accent, 0.12F + segment * 0.065F);
              rectangle(*content, segment * length, y + 17, length + 0.5F, 1, color);
              rectangle(*content, width - (segment + 1) * length, y + 17, length + 0.5F, 1, color);
            }
            rectangle(*content, chipX, y + 5, chipWidth, 25, lerpColor(background, accent, 0.28F), 12.5F);
            rectangle(*content, chipX + 1, y + 6, chipWidth - 2, 23, lerpColor(background, accent, 0.08F), 11.5F);
            glyph(*content, chipX + 11, y + 10, 14, "sparkles", accent);
            label(*content, chipX + 32, y + 10, caption + 2, "New messages", 12, accent, FontWeight::SemiBold);
          }
          y += unreadHeight;
        }
        const float originalY = y;
        m_messageTops[message.id] = top + y;
        if (y + heights[i] >= 0 && y <= bottom - top) {
          const auto firstHit = m_hits.size();
          const auto rowTop = std::max(top, top + y), rowBottom = std::min(bottom, top + y + heights[i]);
          m_contextTargets.push_back(
              {left + 24, rowTop, bodyWidth - 48, std::max(0.0F, rowBottom - rowTop), Action::MessageMenu, message.id}
          );
          if (blockedMessage(message)) {
            label(*content, 50, y + 7, textWidth, "Blocked message — click to show", 12, muted);
            m_hits.push_back(
                {left + 24, rowTop, bodyWidth - 48, std::max(0.0F, rowBottom - rowTop), Action::RevealBlocked,
                 message.id}
            );
            y += heights[i];
            continue;
          }
          if (message.reply) {
            const auto& reply = *message.reply;
            const auto name = userName(reply.authorId, reply.author.empty() ? "message" : reply.author);
            auto preview = std::make_unique<Node>();
            preview->setPosition(50, y);
            preview->setSize(textWidth, 20);
            preview->setClipChildren(true);
            label(*preview, 0, 0, textWidth, replyPreview(reply, name), 12, muted, FontWeight::Normal, 1);
            content->addChild(std::move(preview));
            const float previewTop = std::max(top, top + y);
            const float previewBottom = std::min(bottom, top + y + 20);
            if (previewBottom > previewTop)
              m_hits.push_back(
                  {left + 74, previewTop, textWidth, previewBottom - previewTop, Action::JumpReply, reply.message}
              );
            y += 24;
          }
          const auto author = userName(message.authorId, message.author);
          avatar(*content, 0, y + 1, 34, message.avatar, author);
          label(*content, 50, y, std::max(24.0F, textWidth - 64), author, 13, foreground, FontWeight::SemiBold);
          if (!message.authorId.empty()) {
            const auto avatarY = std::max(top, top + y);
            const auto avatarH = std::max(0.0F, std::min(bottom, top + y + 35) - avatarY);
            const auto nameWidth = std::min(
                std::max(24.0F, textWidth - 64),
                m_window.target().renderer().measureText(author, 13, FontWeight::SemiBold).width + 4
            );
            m_contextTargets.push_back({left + 24, avatarY, 35, avatarH, Action::UserMenu, message.authorId});
            m_contextTargets.push_back(
                {left + 74, avatarY, nameWidth, std::min(18.0F, avatarH), Action::UserMenu, message.authorId}
            );
            m_hits.push_back({left + 24, avatarY, 35, avatarH, Action::UserProfile, message.authorId});
            m_hits.push_back(
                {left + 74, avatarY, nameWidth, std::min(18.0F, avatarH), Action::UserProfile, message.authorId}
            );
          }
          const std::string timestamp = message.timestamp.size() >= 16 ? message.timestamp.substr(11, 5) : "";
          label(*content, bodyWidth - 132, y + 1, 56, timestamp, 10, muted);
          const auto& layout = *layouts[i];
          const float bodyY = y + 18;
          reactionStrip(content.get(), message, 50, bodyY + layout.height + 6, textWidth);
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
              if (m_pointerX >= left && m_pointerY >= rowTop && m_pointerY < rowBottom) {
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
              rectangle(*content, rx, ry, run.width, run.height, raised, 3);
              m_hits.push_back({left + 24 + rx, top + ry, run.width, run.height, Action::RevealSpoiler, message.id});
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
                rectangle(*content, rx + offset, ry, length, run.height, rgbHex(0x574D70), 2);
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
              m_hits.push_back({left + 24 + rx, top + ry, run.width, run.height, Action::OpenUrl, run.style.link});
          }
          for (const auto& pic : layout.pictures) {
            if (pic.video)
              rectangle(*content, 50 + pic.x, bodyY + pic.y, pic.width, pic.height, panel, 6);
            picture(*content, 50 + pic.x, bodyY + pic.y, pic.width, pic.height, pic.media.url, 6, true);
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
                m_viewerImages.push_back(
                    {pic.media.url, pic.link.empty() ? pic.media.url : pic.link, static_cast<float>(pic.media.width),
                     static_cast<float>(pic.media.height)}
                );
              m_hits.push_back({px, py, pic.width, pic.height, Action::ViewImage, pic.media.url});
              const auto cachedImage = m_images.find(pic.media.url);
              const bool animated = pic.animated
                  || pic.media.url.contains(".gif")
                  || (cachedImage != m_images.end()
                      && cachedImage->second.animation
                      && cachedImage->second.animation->frames.size() > 1);
              if (animated && hovered(px, py, pic.width, pic.height) && py >= top && py + 36 <= bottom) {
                if (!m_demo && !m_gifFavoritesLoaded && !m_loadingGifFavorites)
                  m_loadingGifFavorites = m_client.gifFavorites();
                discord::Gif gif;
                gif.url = pic.link.empty() ? discord::mediaUrl(pic.media.url) : pic.link;
                gif.src = discord::mediaUrl(pic.media.url);
                gif.width = pic.media.width;
                gif.height = pic.media.height;
                gif.format = pic.media.url.contains(".mp4") || pic.media.url.contains(".webm") ? 2 : 1;
                const bool favorite =
                    std::ranges::find(m_gifFavorites, gif.url, &discord::Gif::url) != m_gifFavorites.end();
                m_gifTargets[gif.url] = gif;
                rectangle(*content, 50 + pic.x + pic.width - 36, bodyY + pic.y + 4, 30, 30, sidebar, 7);
                iconButton(
                    *content,
                    {50 + pic.x + pic.width - 36, bodyY + pic.y + 4, 30, 30,
                     m_pendingGifFavorites.contains(gif.url) ? Action::None : Action::FavoriteGif, gif.url},
                    favorite ? "star-filled" : "star", favorite ? "Remove GIF from favorites" : "Favorite GIF", favorite
                );
              }
            }
          }
          if (m_pointerX >= left && m_pointerY >= rowTop && m_pointerY < rowBottom) {
            rectangle(*content, bodyWidth - 190, y - 8, 124, 34, panel, 7);
            iconButton(
                *content, {bodyWidth - 186, y - 4, 28, 26, Action::CopyMessage, message.id}, "copy", "Copy text"
            );
            iconButton(
                *content, {bodyWidth - 156, y - 4, 28, 26, Action::Reply, message.id}, "corner-up-left", "Reply"
            );
            iconButton(
                *content, {bodyWidth - 126, y - 4, 28, 26, Action::AddReaction, message.id}, "mood-smile",
                "Add reaction"
            );
            iconButton(
                *content, {bodyWidth - 96, y - 4, 28, 26, Action::MessageMenu, message.id}, "dots", "More actions"
            );
          }
          for (auto index = firstHit; index < m_hits.size(); ++index) {
            auto& hit = m_hits[index];
            const float end = std::min(bottom, hit.y + hit.height);
            hit.y = std::max(top, hit.y);
            hit.height = std::max(0.0F, end - hit.y);
          }
        }
        y = originalY + heights[i];
      }
      const bool loadingConversation =
          m_loadingMessages || (m_channelId.empty() && (m_dmMode ? m_loadingPrivate : m_loadingChannels));
      if (m_messages.empty() && loadingConversation) {
        // Match the timeline's geometry without presenting fake text or actions.
        // Stable widths avoid reshuffling the skeleton on unrelated repaints.
        const auto nameColor = lerpColor(background, muted, 0.23F);
        const auto textColor = lerpColor(background, muted, 0.13F);
        const auto avatarColor = lerpColor(background, muted, 0.17F);
        const float viewport = std::max(0.0F, bottom - top);
        const float lineWidth = std::min(textWidth, 620.0F);
        constexpr int lines[] = {2, 1, 3, 2, 1, 2, 4};
        float rowY = 12;
        for (int row = 0; row < 24 && rowY < viewport; ++row) {
          if (!m_preferences.compact)
            rectangle(*content, 0, rowY + 1, 34, 34, avatarColor, 17);
          rectangle(*content, 50, rowY + 3, std::min(lineWidth, 68.0F + (row * 23 % 69)), 9, nameColor, 4.5F);
          const int count = lines[row % 7];
          for (int line = 0; line < count; ++line) {
            const float lineY = rowY + 25 + line * 20;
            if (lineY >= viewport)
              break;
            const float target = lineWidth * (0.42F + ((row * 7 + line * 3) % 10) * 0.055F);
            float wordX = 0;
            for (int word = 0; wordX < target; ++word) {
              const float length = std::min(24.0F + ((row * 19 + line * 13 + word * 23) % 65), target - wordX);
              rectangle(*content, 50 + wordX, lineY, length, 10, textColor, 5);
              wordX += length + 6;
            }
          }
          rowY += 25 + count * 20 + (m_preferences.compact ? 12 : 22);
        }
      } else if (m_messages.empty()) {
        label(
            *content, 18, 36, bodyWidth - 88,
            m_channelId.empty() ? "Make yourself at home." : "The conversation starts here.", 25, foreground,
            FontWeight::Bold
        );
        label(
            *content, 18, 80, bodyWidth - 88,
            m_channelId.empty() ? "Choose a server and a text channel to get started." : "Send the first message.", 14,
            muted, FontWeight::Normal, 2
        );
      }
      std::vector<TimelineAnchor::Row> visibleAnchors;
      for (std::size_t i = 0; i < m_messages.size(); ++i) {
        const float rowY = m_messageTops.at(m_messages[i].id);
        if (rowY + heights[i] > top && rowY < bottom)
          visibleAnchors.push_back({m_messages[i].id, rowY});
      }
      m_timelineAnchor.remember(std::move(visibleAnchors), m_messageScroll);
      root.addChild(std::move(content));
      if ((!m_demo || m_demoOverlay == "history-cycle")
          && !m_loadingMessages
          && m_history.initialized()
          && !m_history.busy()
          && !m_settingsOpen
          && m_viewImage.empty()
          && m_dialogKind.empty()) {
        const float threshold = std::max(240.0F, (bottom - top) * 0.6F);
        if (!m_history.olderEnd() && m_messageMaxScroll - m_messageScroll < threshold)
          loadHistory(discord::HistoryDirection::Older);
        else if (!m_history.atLatest() && m_messageScroll < threshold)
          loadHistory(discord::HistoryDirection::Newer);
      }
      const auto pendingHistory = m_history.pending();
      if ((pendingHistory && pendingHistory->direction != discord::HistoryDirection::Latest)
          || !m_history.error().empty()) {
        const auto direction = pendingHistory ? pendingHistory->direction : m_history.failedDirection();
        const bool older = direction == discord::HistoryDirection::Older;
        const float statusY = older ? top + 4 : bottom - 32;
        const float width = std::min(360.0F, bodyWidth - 48), statusX = left + (bodyWidth - width) / 2;
        rectangle(root, statusX, statusY, width, 28, sidebar, 8);
        label(
            root, statusX + 12, statusY + 7, width - 92,
            m_history.error().empty() ? (older ? "Loading earlier messages…" : "Loading newer messages…")
                                      : "Could not load message history.",
            11, muted
        );
        if (!m_history.error().empty()) {
          button(root, {statusX + width - 76, statusY, 76, 28, Action::RetryHistory, {}}, "Retry");
          if (hovered(statusX, statusY, width, 28)) {
            m_tooltip = m_history.error();
            m_tooltipX = statusX + width / 2;
            m_tooltipY = statusY;
          }
        }
      }
      if (!m_history.atLatest()) {
        button(root, {left + (bodyWidth - 160) / 2, bottom - 66, 160, 30, Action::JumpLatest, {}}, "Jump to latest");
      }
      if (m_unreadTimeline.unread()) {
        std::string caption = "New messages";
        if (unreadIndex) {
          const auto count = m_messages.size() - *unreadIndex;
          caption = std::to_string(count)
              + ((!m_history.atLatest() || m_unreadTimeline.estimated(m_messages)) ? "+" : "")
              + (count == 1 ? " new message" : " new messages");
          const auto& timestamp = m_messages[*unreadIndex].timestamp;
          if (timestamp.size() >= 16)
            caption += " since " + timestamp.substr(11, 5);
        }
        const float x = left + 16, y = 110, width = bodyWidth - 32;
        const float readWidth = 144;
        const bool over = hovered(x, y, width - readWidth, 36);
        rectangle(root, x, y, width, 36, lerpColor(background, accent, 0.3F), 8);
        if (over)
          rectangle(root, x, y, width - readWidth, 36, lerpColor(background, accent, 0.4F), 8);
        glyph(root, x + 12, y + 10, 16, "arrow-down", foreground);
        label(
            root, x + 36, y + 10, std::max(1.0F, width - readWidth - 48), caption, 12, foreground, FontWeight::Medium
        );
        m_hits.push_back({x, y, width - readWidth, 36, Action::JumpUnread, {}});
        const float readX = x + width - readWidth;
        if (hovered(readX, y, readWidth, 36))
          rectangle(root, readX, y, readWidth, 36, lerpColor(background, accent, 0.5F), 8);
        label(root, readX + 12, y + 10, readWidth - 40, "Mark as Read", 12, foreground, FontWeight::SemiBold);
        glyph(root, x + width - 28, y + 10, 16, "checks", foreground);
        m_hits.push_back({readX, y, readWidth, 36, Action::MarkRead, {}});
      }
      if (!m_settingsOpen && m_viewImage.empty()) {
        const std::string id = "chat-scrollbar";
        std::unique_ptr<Node> owned;
        if (auto stored = m_detachedInputs.find(id); stored != m_detachedInputs.end()) {
          owned = std::move(stored->second);
          m_detachedInputs.erase(stored);
        } else {
          auto bar = std::make_unique<Scrollbar>();
          bar->setTrackInset(4);
          bar->setOnScrollChanged([this](float offset) {
            m_scrolling.stop(m_messageScroll);
            m_messageScroll = std::clamp(m_messageMaxScroll - offset, 0.0F, m_messageMaxScroll);
            m_window.invalidate();
          });
          owned = std::move(bar);
        }
        auto* bar = static_cast<Scrollbar*>(owned.get());
        m_widgets[id] = bar;
        bar->setPosition(left + bodyWidth - 12, top);
        bar->setSize(bar->reservedThickness(), std::max(0.0F, bottom - top));
        bar->update(std::max(0.0F, bottom - top), totalHeight, m_messageMaxScroll - m_messageScroll);
        root.addChild(std::move(owned));
      }
      rectangle(root, left + 22, composerY, bodyWidth - 44, composerHeight, m_fieldFocused ? raised : panel, 14);
      if (replyHeight) {
        const auto& reply = m_replies.at(m_channelId);
        label(
            root, left + 38, composerY + 9, bodyWidth - 220, "Replying to " + userName(reply.authorId, reply.author),
            12, foreground
        );
        button(
            root, {left + bodyWidth - 174, composerY + 3, 94, 28, Action::ToggleReplyMention, {}},
            reply.mention ? "@ ON" : "@ OFF"
        );
        iconButton(root, {left + bodyWidth - 70, composerY + 3, 28, 28, Action::CancelReply, {}}, "x", "Cancel reply");
      }
      m_attachmentX = left + 36;
      m_attachmentY = composerY + replyHeight + 10;
      m_attachmentWidth = bodyWidth - 72;
      m_scrolling.limit(
          m_attachmentScroll, std::max(0.0F, m_attachments[m_channelId].size() * 96.0F - m_attachmentWidth)
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
      m_draftY = composerY + replyHeight + attachmentHeight + 4;
      inputControl(
          root, left + 68, m_draftY, m_draftWidth + 12, composerHeight - attachmentHeight - replyHeight - 8, "composer",
          draft, m_demo ? "Demo mode is read-only" : "Message this channel…", true
      );
      const auto actionY = composerY + composerHeight - 39;
      iconButton(
          root, {left + 30, actionY, 32, 32, m_sending ? Action::None : Action::PasteClipboard, {}},
          m_sending ? "loader" : "clipboard-plus", m_sending ? "Sending…" : "Paste from clipboard"
      );
      iconButton(
          root, {left + bodyWidth - 62, actionY, 32, 32, Action::ToggleEmoji, {}}, "mood-smile", "Emoji", m_emojiMenu
      );
      iconButton(root, {left + bodyWidth - 98, actionY, 32, 32, Action::ToggleGif, {}}, "gif", "GIFs", m_gifMenu);
      if (!m_status.empty())
        label(root, left + 28, 105, bodyWidth - 56, m_status, 11, muted);
      if (memberWidth > 0) {
        m_scrolling.limit(m_memberScroll, std::max(0.0F, m_memberCount * 44.0F - (h - 158)));
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
            const auto rowTop = std::max(148.0F, 148 + rowY), rowBottom = std::min(h - 10, 148 + rowY + 42);
            m_contextTargets.push_back(
                {memberX + 8, rowTop, memberWidth - 16, std::max(0.0F, rowBottom - rowTop), Action::UserMenu,
                 member.user.id}
            );
            m_hits.push_back(
                {memberX + 8, rowTop, memberWidth - 16, std::max(0.0F, rowBottom - rowTop), Action::UserProfile,
                 member.user.id}
            );
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
    if (m_gifMenu && !m_settingsOpen && m_viewImage.empty())
      drawGifPicker(root, w, h);
    if (!m_viewImage.empty())
      drawImageViewer(root, w, h);
    if (m_demo && !m_demoOverlayApplied && !m_demoOverlay.empty()) {
      m_demoOverlayApplied = true;
      if (m_demoOverlay == "message-menu" || m_demoOverlay == "reaction-submenu" || m_demoOverlay == "user-menu") {
        const auto target = std::ranges::find_if(m_contextTargets, [&](const auto& hit) {
          return m_demoOverlay != "user-menu" ? hit.action == Action::MessageMenu && hit.id == "3"
                                              : hit.action == Action::UserMenu && hit.id == "11";
        });
        if (target != m_contextTargets.end()) {
          openContext(target->x + (m_demoOverlay == "user-menu" ? 8 : 110), target->y + 8);
          if (m_demoOverlay == "reaction-submenu")
            openReactionSubmenu(22);
        }
      } else if (m_demoOverlay == "edit-message" || m_demoOverlay == "block-user") {
        m_contextMessage = "3";
        m_contextUser = "11";
        m_contextChannel = m_channelId;
        contextAction(m_demoOverlay == "edit-message" ? Action::EditMessage : Action::BlockUser);
      } else if (m_demoOverlay == "user-profile")
        showProfile("11");
    }
    if (m_demoOverlay == "tooltip-long") {
      m_tooltip = "A tooltip sizes itself to its text, wraps long descriptions, and stays inside the window.\nA second "
                  "line is measured too.";
      m_tooltipX = w - 40;
      m_tooltipY = 58;
    }
    if (m_demoOverlay == "reaction-people" || m_demoOverlay == "reaction-loading") {
      const auto chip = std::ranges::find_if(m_hits, [](const auto& hit) {
        return hit.action == Action::ToggleReaction && hit.id.starts_with("3\n");
      });
      if (chip != m_hits.end()) {
        m_pointerX = chip->x + 10;
        m_pointerY = chip->y + 12;
        auto message = std::ranges::find(m_messages, "3", &discord::Message::id);
        if (message != m_messages.end()) {
          if (m_demoOverlay == "reaction-people") {
            // Synthetic users only; demo never contacts the network.
            discord::Interaction request{
                .kind = discord::InteractionKind::ListReactions,
                .channel = m_channelId,
                .message = "3",
                .emoji = "👍",
                .revision = 1
            };
            const auto key = std::to_string(static_cast<int>(request.kind)) + ":" + m_channelId + ":3::👍:normal";
            auto& page = m_reactorPages[key];
            page.revision = 1;
            discord::Event result;
            result.interaction = request;
            result.reactors = {{"1", "Alex"}, {"11", "Maya"}, {"12", "Noah"}};
            interactionResult(result);
            discord::UserUpdates alias;
            alias.users["11"].nickname = "May";
            m_userDirectory.apply(alias);
          }
          hoverReaction(*message, message->reactions.front(), *chip);
        }
      }
    }
    if (!m_reactionHoveredThisFrame) {
      m_hoverReactionKey.clear();
      m_reactorHoverTimer.stop();
    }
    drawDialog(root, w, h);
    drawContext(root, w, h);
    drawTooltip(root, w, h);
    finishControls();
    updateCursor();
    m_window.context().renderScene(m_window.target(), m_scene.get());
    if (m_animationDelay != std::chrono::milliseconds::max())
      m_animationTimer.start(m_animationDelay, [this] { m_window.invalidate(); });
    else
      m_animationTimer.stop();
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
    m_restoringLogin = m_retrySavedLogin = false;
    m_connecting = true;
    m_session = m_client.connect(std::move(m_token));
    m_token.clear();
    if (m_inputs.contains("token"))
      m_inputs["token"]->setValue("");
    m_selectAll = false;
    m_window.invalidate();
  }

  void ClientApplication::restoreLogin() {
    if (m_demo || m_connecting)
      return;
    m_login.cancel();
    m_showTokenEntry = false;
    m_fieldFocused = false;
    m_connecting = m_restoringLogin = true;
    m_retrySavedLogin = false;
    m_status = "Your desktop keyring may ask you to unlock it.";
    m_session = m_client.restoreLogin();
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
    clearEmojiState();
    m_scrolling.stopAll();
    clearGifs();
    clearInteractions();
    m_login.cancel();
    m_client.disconnect();
    m_demo = true;
    m_connecting = m_restoringLogin = m_retrySavedLogin = false;
    m_loginStorageStatus.clear();
    m_userDirectory = {};
    m_preferences = {};
    m_demoPrepared = false;
    m_demoOverlay.clear();
    m_demoOverlayApplied = false;
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
        && (m_window.active() || (m_demo && m_demoOverlay == "unread-cycle"))
        && !m_settingsOpen
        && m_dialogKind.empty()
        && m_viewImage.empty()
        && m_history.atLatest()
        && (m_demo || m_history.initialized())
        && !m_loadingMessages
        && m_messageScroll <= 1;
  }
  bool ClientApplication::markChannelRead(bool explicitAction) {
    if (m_channelId.empty() || m_messages.empty() || !m_history.atLatest() || (!m_demo && !m_history.initialized()))
      return false;
    const auto& latest = m_messages.back().id;
    if (!explicitAction && m_localRead[m_channelId] == latest)
      return false;
    if (!m_demo && !m_client.acknowledge(m_channelId, latest))
      return false;
    m_localRead[m_channelId] = latest;
    m_unreadTimeline.acknowledged(m_channelId, latest);
    m_unreadChannels.erase(m_channelId);
    m_unreadDMs.erase(m_channelId);
    m_unreadGuilds.erase(m_channelId);
    return true;
  }
  void ClientApplication::cacheChannel() {
    if (m_channelId.empty())
      return;
    if (m_history.atLatest()) {
      const auto first = m_messages.size() > 200 ? m_messages.size() - 200 : 0;
      m_channelMessages[m_channelId] = {m_messages.begin() + first, m_messages.end()};
    } else
      m_channelMessages.erase(m_channelId);
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
    m_scrolling.stopAll();
    if (!m_dmMode && m_guildId == id && !m_channels.empty())
      return;
    cacheChannel();
    m_restoreChannel = m_lastChannels.contains(id) ? m_lastChannels[id]
        : m_demo                                   ? ""
                                                   : m_sessionStore.channelForGuild(m_account.id, id);
    m_dmMode = false;
    m_guildId = id;
    m_channelId.clear();
    m_unreadTimeline.open({});
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
    m_history.reset();
    m_timelineAnchor.reset();
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
    m_scrolling.stopAll();
    cacheChannel();
    if (m_restoreChannel.empty())
      m_restoreChannel = m_lastChannels.contains("@me") ? m_lastChannels["@me"]
          : m_demo                                      ? ""
                                                        : m_sessionStore.channelForGuild(m_account.id, "");
    m_dmMode = true;
    m_guildId.clear();
    m_channelId.clear();
    m_unreadTimeline.open({});
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
    m_scrolling.stopAll();
    closeGifPicker();
    closeContext();
    closeDialog();
    m_reactionMessage.clear();
    m_reactionChannel.clear();
    m_emojiMenu = false;
    if (m_channelId == id)
      return;
    if (id != m_channelId)
      cacheChannel();
    m_controlInput.setFocus(nullptr);
    m_focusedInput = "composer";
    m_channelId = id;
    m_unreadTimeline.open(id);
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
    m_history.reset();
    m_timelineAnchor.reset();
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
    if (m_demo || !m_connected || m_channelId.empty() || m_loadingMessages || m_sending || !m_history.atLatest())
      return;
    loadHistory(discord::HistoryDirection::Latest);
  }

  void ClientApplication::loadHistory(discord::HistoryDirection direction, bool retry) {
    if ((m_demo && m_demoOverlay != "history-cycle") || !m_connected || m_channelId.empty())
      return;
    const auto query = m_history.begin(direction, m_messages, retry);
    if (!query)
      return;
    if (m_demo || m_client.messages(m_channelId, *query)) {
      if (m_demo)
        m_demoHistoryQuery = *query;
      m_liveChanges.clear();
      if (direction == discord::HistoryDirection::Latest)
        m_loadingMessages = true;
    } else
      m_history.fail(*query, "Could not load message history. Try again.");
    m_window.invalidate();
  }

  void ClientApplication::applyHistory(discord::Event& event) {
    if (!event.history || event.resource != m_channelId || !m_history.accepts(*event.history))
      return;
    if (event.history->direction == discord::HistoryDirection::Latest)
      m_loadingMessages = false;
    if (!event.error.empty()) {
      m_history.fail(*event.history, event.error);
      return;
    }
    for (const auto& message : event.messages)
      m_messageLayouts.erase(message.id);
    if (m_history.apply(*event.history, m_messages, std::move(event.messages), m_liveChanges)) {
      m_keepHistoryAnchor = event.history->direction == discord::HistoryDirection::Newer;
      std::set<std::string> retained;
      for (const auto& message : m_messages)
        retained.insert(message.id);
      std::erase_if(m_messageLayouts, [&](const auto& item) { return !retained.contains(item.first); });
    }
  }

  void ClientApplication::demoHistoryStep() {
    const auto step = ++m_demoInteractionStep;
    auto require = [](bool value, const char* error) {
      if (!value)
        throw std::runtime_error(error);
    };
    require(
        std::ranges::any_of(
            m_scene->children(),
            [&](const auto& node) {
              return node->clipChildren()
                  && node->x() == 350
                  && node->y() == m_messageViewportTop
                  && !node->textPixelSnap();
            }
        ),
        "Scrollable text must keep fractional positioning even when animation has stopped"
    );
    auto visibleRow = [&]() -> TimelineAnchor::Row {
      for (const auto& message : m_messages) {
        const float y = m_messageTops.at(message.id);
        if (y >= m_messageViewportTop && y < m_messageViewportTop + 120)
          return {message.id, y};
      }
      throw std::runtime_error("History replay needs a visible message anchor");
    };
    if (m_demoHistoryAnchor) {
      const auto at = m_messageTops.find(m_demoHistoryAnchor->id);
      require(
          at != m_messageTops.end() && std::abs(at->second - m_demoHistoryAnchor->y) < 0.1F,
          "A page or live message arrival moved the visible message"
      );
      m_demoHistoryAnchor.reset();
    }
    if (m_demoHistoryQuery) {
      const auto query = *std::exchange(m_demoHistoryQuery, {});
      if (query.direction != discord::HistoryDirection::Latest)
        m_demoHistoryAnchor = visibleRow();
      const int cursor = query.cursor.empty() ? 12000 : std::stoi(query.cursor);
      const int first = query.direction == discord::HistoryDirection::Newer ? cursor + 1 : std::max(2000, cursor - 100);
      const int count = query.direction == discord::HistoryDirection::Newer ? std::clamp(12000 - first, 0, 100)
          : cursor <= 2000                                                  ? 0
                                                                            : cursor - first;
      discord::Event event;
      event.operation = discord::Operation::Messages;
      event.resource = m_channelId;
      event.history = query;
      for (int i = 0; i < count; ++i)
        event.messages.push_back(
            {std::to_string(first + i), m_channelId, "Maya",
             "A paginated conversation.\nVisible messages keep their place as history arrives.", "2026-10-17T09:41:00Z"}
        );
      applyHistory(event);
      if (query.direction == discord::HistoryDirection::Older)
        ++m_demoHistoryPages;
      require(m_messages.size() <= MessageHistory::windowLimit, "History memory window must remain bounded");
    } else if (m_demoHistoryPhase < 2) {
      if (m_demoHistoryPages >= 12)
        m_demoHistoryPhase = 1;
      if (m_demoHistoryPhase == 1 && m_history.atLatest())
        m_demoHistoryPhase = 2;
      if (m_demoHistoryPhase < 2) {
        const auto* bar = static_cast<Scrollbar*>(m_widgets.at("chat-scrollbar"));
        require(bar->visible(), "History replay needs the native chat scrollbar");
        const auto* thumb = bar->children()[1].get();
        const float x = bar->x() + bar->width() / 2;
        const float chrome = m_window.titlebarHeight() - 36;
        const float y = bar->y() + thumb->y() + thumb->height() / 2 + chrome;
        m_window.motion(x, y);
        m_window.click(x, y, 272);
        m_window.motion(x, m_demoHistoryPhase == 0 ? bar->y() - 100 + chrome : m_window.height() * 2);
        m_window.released();
      } else {
        m_scrolling.stop(m_messageScroll);
        m_messageScroll = m_messageMaxScroll / 2;
        m_timelineAnchor.reset();
      }
    } else if (m_demoHistoryPhase == 2) {
      m_demoHistoryAnchor = visibleRow();
      upsertMessage({"12000", m_channelId, "Alex", "A live message while reading history.", "2026-10-17T09:42:00Z"});
      m_demoHistoryPhase = 3;
    } else if (m_demoHistoryPhase == 3) {
      // Exercise both directions through the actual wheel callback, checking
      // a stable reference row rather than an offset that reflow can rebase.
      const bool down = m_demoHistoryMotion >= 8;
      if (m_demoHistoryMotion % 8 == 0) {
        m_demoMotionAnchor = visibleRow();
        m_window.motion(450, m_messageViewportTop + 100 + m_window.titlebarHeight() - 36);
        m_window.scroll(down ? 120 : -120);
      } else if (m_demoMotionAnchor) {
        const float y = m_messageTops.at(m_demoMotionAnchor->id);
        require(
            down ? y <= m_demoMotionAnchor->y + 0.05F : y >= m_demoMotionAnchor->y - 0.05F,
            "Visible chat rows moved against the requested wheel direction"
        );
        m_demoMotionAnchor->y = y;
      }
      ++m_demoHistoryMotion;
      if (m_demoHistoryMotion >= 16)
        m_demoHistoryPhase = 4;
    }
    if (step == 64)
      require(
          m_demoHistoryPhase == 4 && m_demoHistoryPages >= 12,
          "History replay did not finish older/newer paging and wheel motion"
      );
    std::clog
        << "Offline history frame "
        << step
        << " older_pages "
        << m_demoHistoryPages
        << " rows "
        << m_messages.size()
        << " phase "
        << m_demoHistoryPhase
        << '\n';
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
    const auto reply = m_replies.contains(m_channelId) ? std::optional(m_replies.at(m_channelId)) : std::nullopt;
    const auto content = m_emojiCatalog.messageContent(
        draft, m_dmMode ? "" : m_guildId, m_account.premiumType.value_or(0), m_preferences.fakeNitroEmojis
    );
    if (!m_client.send(m_channelId, content, std::move(uploads), reply)) {
      m_status = "Messages can contain up to 2,000 characters. Please try again.";
    } else {
      m_pendingDraft = draft;
      m_pendingReply = reply;
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
      if (!asset.bytes.empty() && m_window.context().makeCurrent(m_window.target())) {
        cached.animation = std::move(asset.animation);
        cached.animationFrame = 0;
        cached.animationStart = {};
        cached.texture = cached.animation && !cached.animation->frames.empty()
            ? m_window.context().textureManager().loadFromRgba(
                  cached.animation->frames[0].rgba.data(), cached.animation->width, cached.animation->height
              )
            : m_window.context().textureManager().loadFromEncodedBytes(asset.bytes.data(), asset.bytes.size());
      }
      cached.bytes = std::move(asset.bytes);
      cached.retry = std::chrono::steady_clock::now() + std::chrono::seconds(60);
      imagesChanged = true;
    }
    std::size_t animationBytes = 0;
    std::vector<std::pair<std::uint64_t, std::string>> animationAge;
    for (const auto& [path, cached] : m_images)
      if (cached.animation) {
        animationBytes += cached.animation->byteSize();
        animationAge.emplace_back(cached.touched, path);
      }
    std::ranges::sort(animationAge);
    for (const auto& [_, path] : animationAge) {
      if (animationBytes <= 192 * 1024 * 1024)
        break;
      animationBytes -= m_images[path].animation->byteSize();
      m_images[path].animation.reset();
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
        m_restoringLogin = m_retrySavedLogin = false;
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
        clearEmojiState();
        m_scrolling.stopAll();
        clearGifs();
        clearInteractions();
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
        m_unreadTimeline.reset();
        m_history.reset();
        m_timelineAnchor.reset();
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
        m_connecting = m_restoringLogin = false;
        m_retrySavedLogin = event.restoredLogin && !event.noSavedLogin && !event.unauthorized && !event.error.empty();
        if (event.noSavedLogin)
          m_status.clear();
        if (event.error.empty() && !event.noSavedLogin) {
          m_status.clear();
          m_loginStorageStatus = event.restoredLogin ? "Sign-in restored from your desktop keyring."
                                                     : "Saving sign-in to your desktop keyring…";
          m_account = std::move(event.account);
          m_savingProfile = m_loadingProfile = false;
          m_desiredPresence.clear();
          m_preferences = m_sessionStore.preferences(m_account.id);
          m_frequentReactions = m_sessionStore.frequentReactions(m_account.id);
          m_recentEmojis = m_sessionStore.frequentEmojis(m_account.id);
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
      } else if (
          event.operation == discord::Operation::GifSearch
          || event.operation == discord::Operation::GifFavorites
          || event.operation == discord::Operation::SaveGifFavorite
      ) {
        gifEvent(event);
      } else if (
          event.operation == discord::Operation::GuildEmojis
          || event.operation == discord::Operation::EmojiPreferences
          || event.operation == discord::Operation::SaveEmojiFavorite
      ) {
        emojiEvent(event);
      } else if (event.operation == discord::Operation::Credentials) {
        m_loginStorageStatus = !event.error.empty()                        ? event.error
            : event.credentialOperation == auth::CredentialOperation::Save ? "Sign-in is saved in your desktop keyring."
                                                                           : "Saved sign-in has been removed.";
      } else if (event.operation == discord::Operation::Interaction) {
        interactionResult(event);
      } else if (event.operation == discord::Operation::Reaction && event.reaction) {
        const auto& change = *event.reaction;
        invalidateReactors(change.channel, change.message);
        if (const auto cached = m_channelMessages.find(change.channel); cached != m_channelMessages.end())
          for (auto& message : cached->second)
            discord::applyReaction(message, change, m_account.id);
        if (change.channel == m_channelId)
          for (auto& message : m_messages)
            if (message.id == change.message) {
              discord::applyReaction(message, change, m_account.id);
              m_liveChanges[message.id] = message;
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
        m_unreadTimeline.snapshot(std::move(event.unreadCursors), event.resource);
        if (event.resource == "reset")
          m_localRead.clear();
        else if (!event.resource.empty())
          m_localRead.erase(event.resource);
        m_unreadChannels.clear();
        m_unreadDMs.clear();
        m_unreadGuilds = std::move(event.unread);
        for (const auto& [id, guild] : m_unreadGuilds) {
          if (!m_unreadTimeline.cursor(id))
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
          if (!event.account.premiumType)
            event.account.premiumType = m_account.premiumType;
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
        applyHistory(event);
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
            if (m_history.atLatest())
              upsertMessage(std::move(message));
          }
      } else if (event.operation == discord::Operation::MessageUpdate) {
        if (event.resource == m_channelId) {
          m_history.changed(event.messageId, event.patch);
          if (!m_history.busy())
            m_loadingMessages = false;
        }
        if (event.patch.reactions)
          invalidateReactors(event.resource, event.messageId);
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
        for (const auto& id : event.removed)
          invalidateReactors(event.resource, id);
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
          if (!m_sentGif && m_drafts[event.resource] == m_pendingDraft) {
            m_drafts[event.resource].clear();
            if (m_pendingReply
                && m_replies.contains(event.resource)
                && m_replies[event.resource].message == m_pendingReply->message)
              m_replies.erase(event.resource);
          }
          if (event.resource == m_channelId)
            m_edit.clamp(m_drafts[event.resource]);
          if (m_sentGif
              && m_pendingReply
              && m_replies.contains(event.resource)
              && m_replies[event.resource].message == m_pendingReply->message)
            m_replies.erase(event.resource);
          std::erase_if(m_attachments[event.resource], [&](auto& image) {
            if (std::ranges::find(m_pendingAttachments, image.key) == m_pendingAttachments.end())
              return false;
            m_window.context().textureManager().unload(image.texture);
            return true;
          });
          if (event.resource == m_channelId) {
            m_scrolling.stop(m_messageScroll);
            m_messageScroll = 0;
            m_timelineAnchor.reset();
            if (!m_history.atLatest()) {
              m_history.reset();
              m_messages.clear();
              m_messageLayouts.clear();
              loadHistory(discord::HistoryDirection::Latest);
            }
            for (auto& message : event.messages) {
              m_liveChanges[message.id] = message;
              upsertMessage(std::move(message));
            }
          }
          m_status.clear();
        }
        m_pendingDraft.clear();
        m_sentGif = false;
        m_pendingReply.reset();
        m_pendingAttachments.clear();
      }
      if (event.operation == discord::Operation::Acknowledge && !event.error.empty()) {
        m_unreadTimeline.failed(event.resource, event.messageId);
        if (m_unreadTimeline.cursor(event.resource)) {
          m_unreadChannels.insert(event.resource);
          if (std::ranges::find(m_privateChannels, event.resource, &discord::Channel::id) != m_privateChannels.end())
            m_unreadDMs.insert(event.resource);
        }
      }
      if (!event.error.empty())
        m_status = event.error;
      while (m_liveChanges.size() > (m_history.busy() ? 4096U : 256U))
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
    m_history.trim(m_messages);
    std::set<std::string> retained;
    for (const auto& item : m_messages)
      retained.insert(item.id);
    std::erase_if(m_messageLayouts, [&](const auto& item) { return !retained.contains(item.first); });
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
        const auto dy = std::max(0.0F, std::abs(y - item.y - item.run.height / 2) - item.run.height / 2);
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
    if (key.symbol == XKB_KEY_Escape && m_gifMenu) {
      closeGifPicker();
      return;
    }
    if (overlayKey(key))
      return;
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
      closeEmojiPicker();
      m_reactionMessage.clear();
      m_reactionChannel.clear();
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
    if (hit.action == Action::PickEmoji) {
      pickEmoji(hit.id);
      m_window.invalidate();
      return;
    }
    if (interactionAction(hit))
      return;
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
    case Action::JumpUnread:
      if (const auto first = m_unreadTimeline.first(m_messages)) {
        const auto at = m_messageTops.find(m_messages[*first].id);
        if (at != m_messageTops.end()) {
          m_scrolling.stop(m_messageScroll);
          m_messageScroll =
              std::clamp(m_messageScroll + m_messageViewportTop + 36 - at->second, 0.0F, m_messageMaxScroll);
        }
      }
      break;
    case Action::RetryHistory:
      loadHistory(m_history.failedDirection(), true);
      break;
    case Action::JumpLatest:
      m_scrolling.stop(m_messageScroll);
      m_messageScroll = 0;
      m_timelineAnchor.reset();
      m_unreadTimeline.positioned();
      m_history.reset();
      m_messages.clear();
      m_messageLayouts.clear();
      if (!m_demo)
        loadHistory(discord::HistoryDirection::Latest);
      break;
    case Action::MarkRead:
      markChannelRead(true);
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
      else if (hit.id == "autoplay_gifs")
        m_preferences.autoplayGifs = !m_preferences.autoplayGifs;
      else if (hit.id == "fake_nitro_emojis")
        m_preferences.fakeNitroEmojis = !m_preferences.fakeNitroEmojis;
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
      m_scrolling.stopAll();
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
      if (m_connecting)
        m_session = m_client.disconnect();
      m_connecting = m_restoringLogin = m_retrySavedLogin = false;
      m_status.clear();
      break;
    case Action::RetryLogin:
      restoreLogin();
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
      closeGifPicker();
      m_scrolling.stopAll();
      if (m_emojiMenu)
        closeEmojiPicker();
      else
        m_emojiMenu = true;
      break;
    case Action::CloseEmoji:
      closeEmojiPicker();
      m_reactionMessage.clear();
      m_reactionChannel.clear();
      break;
    case Action::PickEmoji:
      break;
    case Action::FavoriteEmoji:
      favoriteEmoji(hit.id);
      break;
    case Action::EmojiSection:
      m_scrolling.stop(m_emojiScroll);
      m_emojiQuery.clear();
      m_emojiSection = hit.id;
      if (hit.id == "all") {
        m_emojiScroll = 0;
        m_emojiJump.clear();
      } else
        m_emojiJump = hit.id;
      prepareEmojiPicker();
      break;
    case Action::EmojiRetry:
      m_emojiError.clear();
      if (!hit.id.empty())
        m_emojiGuildErrors.erase(hit.id);
      prepareEmojiPicker();
      break;
    case Action::ToggleGif:
      m_scrolling.stopAll();
      if (m_gifMenu)
        closeGifPicker();
      else {
        closeContext();
        m_emojiMenu = m_presenceMenu = false;
        m_gifMenu = true;
        m_gifScroll = 0;
        m_focusedInput = "gif-search";
        if (!m_demo && !m_loadingGifFavorites)
          m_loadingGifFavorites = m_client.gifFavorites();
        if (!m_gifFavoritesTab)
          searchGifs();
      }
      break;
    case Action::CloseGif:
      closeGifPicker();
      break;
    case Action::GifTab:
      m_scrolling.stop(m_gifScroll);
      m_gifFavoritesTab = hit.id == "favorites";
      m_gifScroll = 0;
      m_gifSearchTimer.stop();
      if (!m_gifFavoritesTab)
        searchGifs();
      else if (!m_demo && !m_gifFavoritesLoaded && !m_loadingGifFavorites)
        m_loadingGifFavorites = m_client.gifFavorites();
      break;
    case Action::GifRetry:
      if (m_gifFavoritesTab) {
        m_gifFavoriteError.clear();
        if (!m_demo)
          m_loadingGifFavorites = m_client.gifFavorites();
      } else
        searchGifs();
      break;
    case Action::FavoriteGif:
      if (const auto gif = m_gifTargets.find(hit.id); gif != m_gifTargets.end())
        toggleGifFavorite(gif->second);
      break;
    case Action::SendGif:
      if (const auto gif = m_gifTargets.find(hit.id);
          gif != m_gifTargets.end() && !m_demo && !m_sending && !m_channelId.empty()) {
        const auto reply = m_replies.contains(m_channelId) ? std::optional(m_replies.at(m_channelId)) : std::nullopt;
        if (m_client.send(m_channelId, gif->second.url, {}, reply)) {
          m_sending = m_sentGif = true;
          m_pendingDraft.clear();
          m_pendingAttachments.clear();
          m_pendingReply = reply;
          m_status = "Sending GIF…";
          closeGifPicker();
        } else
          m_gifError = "Could not send this GIF. Check the connection.";
      }
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
      clearEmojiState();
      m_scrolling.stopAll();
      clearGifs();
      clearInteractions();
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
      m_session = m_demo ? m_client.disconnect() : m_client.logout();
      m_restoringLogin = m_retrySavedLogin = false;
      m_loginStorageStatus.clear();
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
      m_unreadTimeline.reset();
      m_history.reset();
      m_timelineAnchor.reset();
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
    default:
      // Interaction actions are handled before this switch.
      break;
    }
    m_window.invalidate();
  }
} // namespace app
