#pragma once

#include "discord/models.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace app {
  struct MessageStyle {
    bool bold = false, italic = false, underline = false, strike = false, code = false, spoiler = false,
         mention = false;
    float size = 14;
    std::string link;
  };
  struct MessageRun {
    std::string text, image;
    float x = 0, y = 0, width = 0;
    MessageStyle style;
    std::size_t offset = 0;
  };
  struct MessageBox {
    float x, y, width, height;
    bool quote = false;
    std::string code;
    bool embed = false;
    std::uint32_t color = 0;
  };
  struct MessagePicture {
    discord::Media media;
    float x, y, width, height;
    std::string link;
    bool video = false;
  };
  struct MessageLayout {
    std::vector<MessageRun> runs;
    std::vector<MessageBox> boxes;
    std::vector<MessagePicture> pictures;
    std::string plainText;
    float height = 0;
  };
  MessageLayout layoutRichMessage(
      const discord::Message& message, float width,
      const std::function<float(std::string_view, const MessageStyle&)>& measure, float fontSize = 14
  );
  using MessageMeasure = std::function<float(std::string_view, const MessageStyle&)>;
  std::string messageMarkup(std::string_view text, const MessageStyle& style);
  MessageLayout layoutMessage(
      std::string_view content, float width, const MessageMeasure& measure, float fontSize = 14,
      const std::map<std::string, std::string>& mentions = {}
  );
  MessageLayout
  layoutMessage(std::string_view content, float width, const std::function<float(std::string_view)>& measure);
} // namespace app
