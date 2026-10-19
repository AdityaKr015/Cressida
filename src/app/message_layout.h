#pragma once

#include "api/models.h"
#include "app/syntax_highlight.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace app {
  struct MessageStyle {
    bool bold = false, italic = false, underline = false, strike = false, code = false, spoiler = false,
         mention = false, emoji = false;
    bool historical = false;
    float size = 14;
    std::string link;
    CodeToken syntax = CodeToken::Plain;
  };
  struct MessageRun {
    std::string text, image;
    float x = 0, y = 0, width = 0;
    MessageStyle style;
    std::size_t offset = 0;
    float height = 22;
  };
  struct MessageBox {
    float x, y, width, height;
    bool quote = false;
    std::string code;
    bool embed = false;
    std::uint32_t color = 0;
    bool inlineCode = false;
    bool embedAccent = true, divider = false, disabled = false, spoiler = false;
    std::string link;
  };
  struct MessagePicture {
    api::Media media;
    float x, y, width, height;
    std::string link;
    bool video = false;
    bool animated = false;
    std::string videoSource;
    std::optional<api::Sticker> sticker;
    bool spoiler = false;
  };
  struct MessageLayout {
    struct EditedMarker {
      float x, y, width, height, size;
    };
    struct ForwardedCard {
      float y, height;
      std::string guild, channel, message, timestamp;
    };
    std::vector<ForwardedCard> forwards;
    std::vector<MessageRun> runs;
    std::vector<MessageBox> boxes;
    std::vector<MessagePicture> pictures;
    std::string plainText;
    float height = 0, historyHeight = 0;
    std::optional<EditedMarker> edited;
  };
  struct MessageOptions {
    bool emojiImageLinks = false, compact = false;
    std::function<bool(std::string_view)> unicodeEmoji;
    bool discordTimestamps = false;
    bool mentionPrefix = true;
    bool attachmentGallery = false;
  };
  MessageLayout layoutRichMessage(
      const api::Message& message, float width,
      const std::function<float(std::string_view, const MessageStyle&)>& measure, float fontSize = 14,
      const MessageOptions& options = {}
  );
  using MessageMeasure = std::function<float(std::string_view, const MessageStyle&)>;
  MessageLayout layoutReplyPreview(
      const api::MessageReference& reply, std::string_view author, float width, const MessageMeasure& measure,
      const MessageOptions& options = {}
  );
  std::string messageMarkup(std::string_view text, const MessageStyle& style);
  MessageLayout layoutMessage(
      std::string_view content, float width, const MessageMeasure& measure, float fontSize = 14,
      const std::map<std::string, std::string>& mentions = {}, const MessageOptions& options = {}
  );
  MessageLayout
  layoutMessage(std::string_view content, float width, const std::function<float(std::string_view)>& measure);
} // namespace app
