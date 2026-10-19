#pragma once

#include "ui/style.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>

// Application geometry extends the shared control style. Measurements and hit
// regions must use these same values; changing density must not move them apart.
namespace app::layout {
  struct Sidebar {
    static constexpr float railWidth = 78;
    static constexpr float preferredWidth = 248;
    static constexpr float minimumWidth = 200;
    static constexpr float maximumWidth = 480;
    static constexpr float minimumConversation = 360;
    static constexpr float resizeSlop = Style::spaceXs;
    static constexpr float categoryHeight = 24;
    static constexpr float rowHeight = 34;
    static constexpr float rowGap = Style::spaceXs;
    static constexpr float rowStep = rowHeight + rowGap;
    static constexpr float rowInset = Style::spaceMd;
    static constexpr float iconSize = 20;
    static constexpr float threadIconSize = Style::baseGlyphSize;
    static constexpr float lockSize = 8;
    static constexpr float lockOffset = iconSize - lockSize;
    static constexpr float panelPreferredWidth = 380;
    static constexpr float panelMinimumWidth = 250;
  };
  struct Window {
    static constexpr float titlebarHeight = 36;
  };
  inline float sidebarWidth(float preferred, float windowWidth) {
    if (!std::isfinite(preferred))
      preferred = Sidebar::preferredWidth;
    const float maximum = std::max(
        Sidebar::minimumWidth,
        std::min(Sidebar::maximumWidth, windowWidth - Sidebar::railWidth - Sidebar::minimumConversation)
    );
    return std::clamp(preferred, Sidebar::minimumWidth, maximum);
  }
  struct Chat {
    static constexpr auto jumpHighlightDuration = std::chrono::seconds{2};
    static constexpr float jumpHighlightOpacity = .18F;
    static constexpr float inset = 24;
    static constexpr float avatarSize = 40;
    static constexpr float authorGutter = 50;
    static constexpr float topPadding = Style::spaceMd;
    static constexpr float bottomPadding = Style::spaceXs;
    static constexpr float composerGap = Style::spaceSm;
    static constexpr float headerHeight = 18;
    static constexpr float footerGap = 6;
    static constexpr float reactionTailGap = Style::spaceXs;
    static constexpr float maximumCompactGroupGap = Style::spaceSm;
    static constexpr float defaultGroupGap = Style::spaceLg;
    static constexpr float timelineTop = 122;
    static constexpr float unreadTimelineTop = 166;
    static constexpr float minimumHistoryFetchDistance = 240;
  };
  struct ChannelHeader {
    static constexpr float height = 66;
    static constexpr float iconTop = 58;
    static constexpr float searchBreakpoint = 690;
    static constexpr float searchMinimumWidth = 180;
    static constexpr float searchMaximumWidth = 260;
    static constexpr float searchWidthFraction = .28F;
    static constexpr float controlTop = 52;
    static constexpr float controlHeight = 32;
    static constexpr float actionWidth = 30;
    static constexpr float actionStep = 35;
    static constexpr float actionsReserve = 146;
  };
  struct DraftAttachment {
    static constexpr float size = 82;
    static constexpr float topInset = Style::spaceXxs;
    static constexpr float step = 96;
    static constexpr float removeSize = 28;
    static constexpr float removeLeft = size - removeSize + Style::spaceXs;
    static constexpr float height = size + topInset * 2;
    static constexpr float composerReserve = height + Style::spaceLg + Style::spaceXs;
  };
  struct ChannelIntro {
    static constexpr float badgeSize = 58;
    static constexpr float badgeTop = Style::spaceXs;
    static constexpr float iconInset = Style::spaceMd;
    static constexpr float iconSize = badgeSize - iconInset * 2;
  };
  struct Tooltip {
    static constexpr float maximumWidth = 480;
    static constexpr float horizontalPadding = Style::spaceDense;
    static constexpr float verticalPadding = Style::spaceSm;
    static constexpr float top = Window::titlebarHeight + Style::spaceXs;
    static constexpr float belowAnchor = 38;
  };
  struct Profile {
    static constexpr float width = 370;
    static constexpr float inset = 18;
    static constexpr float maximumBioHeight = 63;
    static constexpr float compactHeight = 510;
    static constexpr float compactBannerHeight = 76;
    static constexpr float bannerHeight = 108;
    static constexpr float identityHeight = 112;
    static constexpr float pronounIdentityHeight = 136;
    static constexpr float footerHeight = 70;
    static constexpr float viewportReserve = 64;
    static constexpr float anchorOffset = 96;
    static constexpr float roleChipHeight = 26;
    static constexpr float roleChipStep = 28;
    static constexpr float collapsedRoles = 6;
  };
  struct Settings {
    static constexpr float frameWidth = 1100;
    static constexpr float narrowWidth = 760;
    static constexpr float navigationWidth = 216;
    static constexpr float narrowNavigationWidth = 180;
    static constexpr float navigationTop = 124;
    static constexpr float navigationBottom = 78;
    static constexpr float navigationRowHeight = 30;
    static constexpr float navigationHeadingHeight = 26;
    static constexpr float contentGutter = 28;
    static constexpr float contentInset = Style::cardPadding;
    static constexpr float contentMaximumWidth = 730;
    static constexpr float rightReserve = 68;
    static constexpr float viewportTop = 108;
    static constexpr float actionHeight = 26;
    static constexpr float actionRadius = Style::controlRadius;
    static constexpr float stackThreshold = 420;
    static constexpr float toggleWidth = Style::toggleThumbSizeSm + Style::toggleInsetSm * 2 + Style::toggleTravelSm;
    static constexpr float toggleHeight = Style::toggleThumbSizeSm + Style::toggleInsetSm * 2;
    static constexpr float saveHeight = 48;
    static constexpr float stackedSaveHeight = 82;
  };
  struct SettingsFrame {
    float x, navigationWidth, contentLeft, contentWidth, contentX, innerWidth;
  };
  inline SettingsFrame settingsFrame(float windowWidth) {
    const float x = std::max(Style::spaceLg, (windowWidth - Settings::frameWidth) / 2);
    const float navigation =
        windowWidth < Settings::narrowWidth ? Settings::narrowNavigationWidth : Settings::navigationWidth;
    const float left = x + navigation + Settings::contentGutter;
    const float width = std::max(
        Settings::narrowNavigationWidth,
        std::min(windowWidth - left - Settings::rightReserve, Settings::contentMaximumWidth)
    );
    return {x, navigation, left, width, left + Settings::contentInset, width - Settings::contentInset * 2};
  }
  struct Picker {
    static constexpr float emojiWidth = 552;
    static constexpr float gifWidth = 440;
    static constexpr float maximumHeight = 520;
    static constexpr float minimumHeight = 240;
    static constexpr float emojiMinimumHeight = 250;
    static constexpr float stickerInfoHeight = 430;
    static constexpr float composerReserve = 64;
    static constexpr float memberBreakpoint = 1000;
    static constexpr float memberWidth = 240;
    static constexpr float searchTop = 52;
    static constexpr float contentTop = 104;
    static constexpr float railWidth = 64;
    static constexpr float railItemSize = 36;
    static constexpr float railStep = 44;
    static constexpr float emojiRowStep = 48;
    static constexpr float emojiCellHeight = 44;
    static constexpr float emojiImageSize = 36;
    static constexpr float favoriteSize = 17;
    static constexpr float gifMinimumTileHeight = 80;
    static constexpr float gifMaximumTileHeight = 220;
    static constexpr float gifFallbackTileHeight = 140;
  };
  struct Frame {
    float x, y, width, height;
  };
  inline Frame pickerFrame(
      float windowWidth, float composerTop, bool members, float desiredWidth,
      float minimumHeight = Picker::minimumHeight, float maximumHeight = Picker::maximumHeight
  ) {
    const float width = std::min(desiredWidth, windowWidth - Style::spaceLg * 2);
    const float height = std::min(maximumHeight, std::max(minimumHeight, composerTop - Picker::composerReserve));
    const float memberReserve = members && windowWidth >= Picker::memberBreakpoint ? Picker::memberWidth : 0;
    const float x = std::clamp(
        windowWidth - memberReserve - width - Style::spaceXl, Style::spaceLg,
        std::max(Style::spaceLg, windowWidth - width - Style::spaceLg)
    );
    const float y = std::max(Window::titlebarHeight + Style::spaceSm, composerTop - height - Style::spaceSm);
    return {x, y, width, height};
  }
  struct Avatar {
    static constexpr float tiny = 20, compact = 24, dm = 28, member = 30, small = 32, account = 34;
    static constexpr float chat = Chat::avatarSize, rail = 48, preview = 56, large = 64;
    static constexpr float request = 72, profile = 84, hero = 96;
  };
  struct Emoji {
    static constexpr float badge = Style::baseGlyphSize, inlineSize = 22, picker = 36, preview = 32, jumbo = 48;
    static constexpr float pickerFont = 30, previewFont = 27;
  };
  struct Completion {
    static constexpr float rowHeight = 36;
    static constexpr float headingHeight = 32;
    static constexpr float inset = 6;
    static constexpr float footerInset = 6;
    static constexpr float chromeHeight = headingHeight + footerInset;
    static constexpr float maximumHeight = 326;
    static constexpr float minimumHeight = chromeHeight + rowHeight;
    static constexpr float preferredRoom = 180;
    static constexpr float topReserve = 110;
    static constexpr float composerLeadingInset = 46;
    static constexpr float composerExtraWidth = 142;
    static constexpr std::size_t maximumResults = 50;
  };
  struct FontPicker {
    static constexpr float preferredWidth = 420;
    static constexpr float maximumViewportHeight = 600;
    static constexpr float chromeHeight = 88;
    static constexpr float rowStep = 30;
    static constexpr float rowHeight = 26;
    static constexpr float listTop = 80;
    static constexpr std::size_t maximumPreparedRows = 25;
    static constexpr std::size_t replayRows = maximumPreparedRows * 2;
  };
  struct ChannelLink {
    static constexpr float padding = Style::spaceXs;
    static constexpr float gap = Style::spaceXs;
    static constexpr float iconExtra = Style::spaceXxs;
    static constexpr float lineExtra = Style::spaceSm;
    static constexpr float breadcrumbMinimumWidth = 180;
    static constexpr float tint = .10F;
    static constexpr float hoverTint = .20F;
    static constexpr float selectionTint = .30F;
  };
} // namespace app::layout
