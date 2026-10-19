#include "app/message_layout.h"

#include "api/emoji.h"
#include "api/models.h"
#include "app/discord_link.h"
#include "app/discord_timestamp.h"
#include "app/layout_metrics.h"
#include "app/reply_preview.h"
#include "ui/style.h"

#include <algorithm>
#include <cmath>
#include <md4c.h>
#include <pango/pango.h>

namespace app {
  std::string messageMarkup(std::string_view text, const MessageStyle& style) {
    auto* escaped = g_markup_escape_text(text.data(), text.size());
    std::string result = escaped;
    g_free(escaped);
    if (style.bold)
      result = "<b>" + result + "</b>";
    if (style.italic)
      result = "<i>" + result + "</i>";
    if (style.underline || !style.link.empty())
      result = "<u>" + result + "</u>";
    if (style.strike)
      result = "<s>" + result + "</s>";
    if (style.code)
      result = "<tt>" + result + "</tt>";
    return result;
  }
  namespace {
    // MD4C handles Markdown nesting/escaping; these tokens preserve Discord's
    // double-underscore underline and spoilers without interpreting raw HTML.
    constexpr std::string_view underline = "\uE000", spoiler = "\uE001";
    std::string discordSyntax(std::string_view text, bool timestamps) {
      std::vector<std::size_t> underscores, spoilers;
      std::string fence;
      for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '\\') {
          i += std::min<std::size_t>(2, text.size() - i);
          continue;
        }
        if (text[i] == '`') {
          auto end = i + 1;
          while (end < text.size() && text[end] == '`')
            ++end;
          auto delimiter = std::string(text.substr(i, end - i));
          if (fence.empty()) {
            if (text.find(delimiter, end) != std::string_view::npos)
              fence = delimiter;
          } else if (delimiter == fence)
            fence.clear();
          i = end;
          continue;
        }
        if (fence.empty() && text.substr(i, 2) == "__") {
          underscores.push_back(i);
          i += 2;
        } else if (fence.empty() && text.substr(i, 2) == "||") {
          spoilers.push_back(i);
          i += 2;
        } else
          ++i;
      }
      if (underscores.size() % 2)
        underscores.pop_back();
      if (spoilers.size() % 2)
        spoilers.pop_back();
      std::string result;
      for (std::size_t i = 0; i < text.size();) {
        if (std::binary_search(underscores.begin(), underscores.end(), i)) {
          result += underline;
          i += 2;
        } else if (std::binary_search(spoilers.begin(), spoilers.end(), i)) {
          result += spoiler;
          i += 2;
        } else if (text[i] == '<') {
          const auto end = text.find('>', i);
          const auto token = end == std::string_view::npos ? std::string_view{} : text.substr(i + 1, end - i - 1);
          const auto colon = token.rfind(':');
          if (((token.starts_with(':') || token.starts_with("a:")) && colon != std::string_view::npos && colon > 1)
              || token.starts_with('@')
              || token.starts_with('#')
              || (timestamps && token.starts_with("t:"))) {
            result += "\uE002";
            result += token;
            result += "\uE003";
            i = end + 1;
          } else
            result += text[i++];
        } else
          result += text[i++];
      }
      return result;
    }
    std::string restoreEmoji(std::string text) {
      for (auto [marker, original] :
           {std::pair{std::string_view("\uE002"), std::string_view("<")},
            {std::string_view("\uE003"), std::string_view(">")}})
        for (auto pos = text.find(marker); pos != std::string::npos; pos = text.find(marker, pos + 1))
          text.replace(pos, marker.size(), original);
      return text;
    }
    std::string restore(std::string text) {
      for (auto [marker, original] : {std::pair{underline, std::string_view("__")}, {spoiler, std::string_view("||")}})
        for (auto pos = text.find(marker); pos != std::string::npos; pos = text.find(marker, pos + 2))
          text.replace(pos, marker.size(), original);
      return restoreEmoji(std::move(text));
    }
    struct Layout {
      MessageLayout output;
      float width, x = 0, y = 0, indent = 0, lineHeight = 22, fontSize = 14;
      MessageMeasure measure;
      MessageStyle style;
      std::vector<MessageStyle> styles;
      std::vector<unsigned> lists;
      std::vector<std::size_t> quotes;
      bool codeBlock = false;
      std::size_t codeBox = 0;
      std::string codeLanguage;
      std::optional<std::size_t> inlineBox;
      bool underlineOn = false, spoilerOn = false;
      float emojiSize = 22;
      std::size_t lineStart = 0;
      std::size_t lineBoxStart = 0;
      const MessageOptions* options = nullptr;
      const std::map<std::string, std::string>* mentions = nullptr;
      std::string pending;
      void flush() {
        auto value = std::move(pending);
        pending.clear();
        if (options && options->channelMention && !style.code && value == style.link)
          if (const auto target = discordLink(value)) {
            const auto resolved = options->channelMention(target->guild, target->channel);
            channel(resolved, resolved.link.empty() ? std::string{} : value, true, value);
            return;
          }
        if (!value.empty())
          text(restoreEmoji(std::move(value)));
      }
      void alignLine() {
        const bool hasEmoji = std::any_of(output.runs.begin() + lineStart, output.runs.end(), [](const auto& run) {
          return run.style.emoji;
        });
        if (hasEmoji) {
          for (auto i = lineStart; i < output.runs.size(); ++i)
            output.runs[i].y += std::max(0.0F, (lineHeight - output.runs[i].height) / 2);
          for (auto i = lineBoxStart; i < output.boxes.size(); ++i)
            if (auto& box = output.boxes[i]; box.inlineCode)
              box.y += std::max(0.0F, (lineHeight - (box.height + 4)) / 2);
        }
        lineStart = output.runs.size();
        lineBoxStart = output.boxes.size();
      }
      void newline(bool semantic = false) {
        alignLine();
        inlineBox.reset();
        x = indent + (style.code && !codeBlock ? 3 : 0);
        y += lineHeight;
        lineHeight = fontSize + 8;
        if (semantic)
          output.plainText += "\n";
      }
      void boundary() {
        if (x > indent)
          newline(true);
      }
      void channel(
          const MessageOptions::ChannelMention& target, std::string link, bool breadcrumb, std::string_view source = {}
      ) {
        using Part = MessageRun::ChannelChip::Part;
        const float available = std::max(0.0F, width - indent);
        auto current = style;
        current.link = std::move(link);
        current.userMention.clear();
        current.mention = true;
        current.spoiler |= spoilerOn;
        current.underline = false;
        auto textStyle = current;
        textStyle.link.clear();
        const float padding = layout::ChannelLink::padding;
        const float gap = layout::ChannelLink::gap;
        const float iconSize = std::min(
            current.size + layout::ChannelLink::iconExtra,
            std::max(0.0F, available - padding * 2 - gap - Style::borderWidth)
        );
        MessageRun::ChannelChip chip;
        const auto part = [&](Part::Kind kind, std::string value, bool locked = false) {
          const float measured = kind == Part::Text ? measure(value, textStyle) : iconSize;
          chip.parts.push_back({kind, std::move(value), 0, measured, locked});
        };
        const bool full = available >= layout::ChannelLink::breadcrumbMinimumWidth;
        if (breadcrumb && full && !target.guildName.empty()) {
          part(Part::GuildIcon, target.guildIcon);
          part(Part::Text, target.guildName);
          part(Part::Glyph, "chevron-right");
        }
        if (full && !target.parentName.empty()) {
          part(Part::Glyph, target.parentIcon);
          part(Part::Text, target.parentName);
          part(Part::Glyph, "chevron-right");
        }
        part(Part::Glyph, target.icon, target.locked);
        part(Part::Text, target.channelName.empty() ? target.name : target.channelName);
        float fixed = padding * 2 + (chip.parts.size() - 1) * gap, text = 0;
        for (const auto& item : chip.parts)
          if (item.kind == Part::Text)
            text += item.width;
          else
            fixed += item.width;
        const float fraction = text > 0 ? std::min(1.0F, std::max(0.0F, available - fixed) / text) : 1;
        float chipWidth = padding;
        for (auto& item : chip.parts) {
          if (item.kind == Part::Text)
            item.width *= fraction;
          item.x = chipWidth;
          chipWidth += item.width + gap;
        }
        chipWidth += padding - gap;
        chipWidth = std::min(chipWidth, available);
        for (auto& item : chip.parts) {
          item.x = std::min(item.x, chipWidth);
          item.width = std::min(item.width, std::max(0.0F, chipWidth - item.x - padding));
        }
        if (x > indent && x + chipWidth > width)
          newline();
        const std::string plain = source.empty() ? "#" + target.name : std::string(source);
        const float height = current.size + layout::ChannelLink::lineExtra;
        output.runs.push_back({plain, {}, x, y, chipWidth, current, output.plainText.size(), height, std::move(chip)});
        output.plainText += plain;
        x += chipWidth;
        lineHeight = std::max(lineHeight, height);
      }
      void add(std::string text, std::string image = {}, bool unicodeEmoji = false) {
        auto current = style;
        current.underline |= underlineOn;
        current.spoiler |= spoilerOn;
        current.emoji = !image.empty() || unicodeEmoji;
        if (unicodeEmoji)
          current.size = emojiSize;
        const float size = image.empty() ? measure(text, current) : emojiSize;
        const float height = image.empty() ? current.size + 8 : emojiSize;
        const bool inlineCode = current.code && !codeBlock;
        const float padding = inlineCode ? 3 : 0;
        if (x > indent + padding && x + size > width - (codeBlock ? 10 : padding))
          newline();
        const float runY = y + (inlineCode ? std::max(0.0F, fontSize - current.size) : 0);
        if (inlineCode) {
          if (!inlineBox) {
            inlineBox = output.boxes.size();
            output.boxes.push_back(
                {x - padding, runY - 1, size + padding * 2, current.size + 4, false, {}, false, 0, true}
            );
          } else {
            auto& box = output.boxes[*inlineBox];
            box.width = x + size + padding - box.x;
          }
        }
        output.runs.push_back(
            {std::move(text), std::move(image), x, runY, size, current, output.plainText.size(), height}
        );
        output.plainText += output.runs.back().text;
        x += size;
        lineHeight = std::max(lineHeight, current.emoji ? emojiSize + 8 : height);
      }
      void plainWord(std::string_view word) {
        const float available = std::max(22.0F, width - indent - (codeBlock ? 10 : style.code ? 6 : 0));
        auto current = style;
        current.underline |= underlineOn;
        if (measure(word, current) <= available) {
          add(std::string(word));
          return;
        }
        // Break oversized words at grapheme boundaries, including combined emoji.
        const auto count = g_utf8_strlen(word.data(), word.size());
        std::vector<PangoLogAttr> attrs(count + 1);
        pango_get_log_attrs(word.data(), word.size(), -1, pango_language_get_default(), attrs.data(), attrs.size());
        const char* start = word.data();
        const char* end = start;
        std::string part;
        for (glong i = 1; i <= count; ++i) {
          end = g_utf8_next_char(end);
          if (!attrs[i].is_cursor_position)
            continue;
          const std::string cluster(start, end);
          start = end;
          if (!part.empty() && measure(part + cluster, current) > available) {
            add(std::move(part));
            part.clear();
            newline();
          }
          part += cluster;
        }
        if (!part.empty())
          add(std::move(part));
      }
      void word(std::string_view value) {
        if (style.code || !options || !options->unicodeEmoji || std::ranges::none_of(value, [](unsigned char ch) {
              return ch >= 0x80;
            })) {
          plainWord(value);
          return;
        }
        const auto count = g_utf8_strlen(value.data(), value.size());
        std::vector<PangoLogAttr> attrs(count + 1);
        pango_get_log_attrs(value.data(), value.size(), -1, pango_language_get_default(), attrs.data(), attrs.size());
        const char* start = value.data();
        const char* end = start;
        const char* plain = start;
        for (glong i = 1; i <= count; ++i) {
          end = g_utf8_next_char(end);
          if (!attrs[i].is_cursor_position)
            continue;
          const std::string_view cluster(start, end - start);
          if (options->unicodeEmoji(cluster)) {
            if (plain < start)
              plainWord(std::string_view(plain, start - plain));
            add(std::string(cluster), {}, true);
            plain = end;
          }
          start = end;
        }
        if (plain < end)
          plainWord(std::string_view(plain, end - plain));
      }
      void text(std::string_view text, bool literal = false) {
        for (std::size_t pos = 0; pos < text.size();) {
          if (!literal && text.substr(pos).starts_with(underline)) {
            underlineOn = !underlineOn;
            pos += underline.size();
            continue;
          }
          if (!literal && text.substr(pos).starts_with(spoiler)) {
            spoilerOn = !spoilerOn;
            pos += spoiler.size();
            continue;
          }
          if (text[pos] == '\n') {
            newline(true);
            ++pos;
            continue;
          }
          if (text[pos] == '\r') {
            ++pos;
            continue;
          }
          if (text[pos] == '\t') {
            add("    ");
            ++pos;
            continue;
          }
          if (!literal && text[pos] == '<') {
            const auto end = text.find('>', pos);
            if (end != std::string_view::npos && end - pos <= 128) {
              const auto token = text.substr(pos + 1, end - pos - 1);
              if (options && options->discordTimestamps)
                if (const auto timestamp = discordTimestamp(token)) {
                  word(*timestamp);
                  pos = end + 1;
                  continue;
                }
              const auto colon = token.rfind(':');
              if (token.starts_with('@') || token.starts_with('#')) {
                auto id = token.substr(1);
                if (id.starts_with('!') || id.starts_with('&'))
                  id.remove_prefix(1);
                if (api::validSnowflake(std::string(id))) {
                  const auto found =
                      mentions ? mentions->find(std::string(id)) : std::map<std::string, std::string>::const_iterator{};
                  auto name = mentions && found != mentions->end() ? found->second
                      : token.starts_with('#')                     ? "unknown-channel"
                      : token.starts_with("@&")                    ? "unknown-role"
                                                                   : "unknown-user";
                  const auto previous = style;
                  style.mention = true;
                  style.link.clear();
                  style.userMention.clear();
                  if (token.starts_with('#') && options && options->channelMention) {
                    const auto resolved = options->channelMention({}, id);
                    channel(resolved, resolved.link, false);
                    style = previous;
                    pos = end + 1;
                    continue;
                  } else if (token.starts_with('@') && !token.starts_with("@&"))
                    style.userMention = id;
                  word(
                      std::string((!options || options->mentionPrefix) ? (token.starts_with('#') ? "#" : "@") : "")
                      + name
                  );
                  style = previous;
                  pos = end + 1;
                  continue;
                }
              }
              if ((token.starts_with(':') || token.starts_with("a:")) && colon != std::string_view::npos && colon > 1) {
                auto path = api::emojiPath(std::string(token.substr(colon + 1)), token.starts_with("a:"));
                if (!path.empty()) {
                  add(std::string(text.substr(pos, end - pos + 1)), std::move(path));
                  pos = end + 1;
                  continue;
                }
              }
            }
          }
          const bool space = text[pos] == ' ';
          auto end = pos + 1;
          while (end < text.size()
                 && text[end] != '\n'
                 && text[end] != '\r'
                 && text[end] != '\t'
                 && text[end] != '<'
                 && (text[end] == ' ') == space
                 && (literal || (!text.substr(end).starts_with(underline) && !text.substr(end).starts_with(spoiler))))
            ++end;
          if (space) {
            const auto length = measure(text.substr(pos, end - pos), style);
            output.plainText += text.substr(pos, end - pos);
            if (x + length > width)
              newline();
            else
              x += length;
          } else
            word(text.substr(pos, end - pos));
          pos = end;
        }
      }
    };
    int enterBlock(MD_BLOCKTYPE type, void* detail, void* context) {
      auto& l = *static_cast<Layout*>(context);
      l.flush();
      switch (type) {
      case MD_BLOCK_P:
        l.boundary();
        break;
      case MD_BLOCK_H:
        l.boundary();
        l.style.bold = true;
        l.style.size = std::max(16.0F, 28.0F - static_cast<MD_BLOCK_H_DETAIL*>(detail)->level * 3) * l.fontSize / 14;
        break;
      case MD_BLOCK_UL:
        l.lists.push_back(0);
        break;
      case MD_BLOCK_OL:
        l.lists.push_back(static_cast<MD_BLOCK_OL_DETAIL*>(detail)->start);
        break;
      case MD_BLOCK_LI:
        l.boundary();
        l.add(l.lists.back() ? std::to_string(l.lists.back()++) + ". " : "• ");
        l.indent += 18;
        l.x = l.indent;
        break;
      case MD_BLOCK_QUOTE:
        l.boundary();
        l.quotes.push_back(l.output.boxes.size());
        l.output.boxes.push_back({l.indent, l.y, 3, 0, true, {}});
        l.indent += 14;
        l.x = l.indent;
        break;
      case MD_BLOCK_CODE: {
        l.boundary();
        if (l.y > 0)
          l.y += 4;
        l.codeBox = l.output.boxes.size();
        l.output.boxes.push_back({l.indent, l.y, l.width - l.indent, 0, false, {}});
        l.indent += 10;
        l.x = l.indent;
        l.y += 10;
        l.codeBlock = true;
        l.style.code = true;
        const auto& lang = static_cast<MD_BLOCK_CODE_DETAIL*>(detail)->lang;
        l.codeLanguage.assign(lang.text ? lang.text : "", lang.size);
        l.style.size = l.fontSize * 11 / 14;
        l.text(lang.size ? std::string_view(lang.text, lang.size) : std::string_view("code"), true);
        l.newline();
        l.style.size = l.fontSize;
        break;
      }
      case MD_BLOCK_HR:
        l.boundary();
        l.output.boxes.push_back({l.indent, l.y + 7, l.width - l.indent, 1, true, {}});
        l.y += 18;
        break;
      default:
        break;
      }
      return 0;
    }
    int leaveBlock(MD_BLOCKTYPE type, void*, void* context) {
      auto& l = *static_cast<Layout*>(context);
      l.flush();
      switch (type) {
      case MD_BLOCK_H:
        l.boundary();
        l.style.bold = false;
        l.style.size = l.fontSize;
        l.y += 4;
        break;
      case MD_BLOCK_P:
        l.boundary();
        break;
      case MD_BLOCK_LI:
        l.boundary();
        l.indent = std::max(0.0F, l.indent - 18);
        l.x = l.indent;
        break;
      case MD_BLOCK_UL:
      case MD_BLOCK_OL:
        l.lists.pop_back();
        break;
      case MD_BLOCK_QUOTE:
        l.boundary();
        l.output.boxes[l.quotes.back()].height = l.y - l.output.boxes[l.quotes.back()].y;
        l.quotes.pop_back();
        l.indent -= 14;
        l.x = l.indent;
        break;
      case MD_BLOCK_CODE: {
        const auto source = l.output.boxes[l.codeBox].code;
        for (const auto& span : highlightCode(source, l.codeLanguage)) {
          l.style.syntax = span.token;
          l.text(std::string_view(source).substr(span.offset, span.length), true);
        }
        l.style.syntax = CodeToken::Plain;
        l.boundary();
        l.y += 10;
        l.output.boxes[l.codeBox].height = l.y - l.output.boxes[l.codeBox].y;
        l.indent -= 10;
        l.x = l.indent;
        l.y += 6;
        l.style.code = false;
        l.codeBlock = false;
        break;
      }
      default:
        break;
      }
      return 0;
    }
    int enterSpan(MD_SPANTYPE type, void* detail, void* context) {
      auto& l = *static_cast<Layout*>(context);
      l.flush();
      l.styles.push_back(l.style);
      switch (type) {
      case MD_SPAN_STRONG:
        l.style.bold = true;
        break;
      case MD_SPAN_EM:
        l.style.italic = true;
        break;
      case MD_SPAN_DEL:
        l.style.strike = true;
        break;
      case MD_SPAN_CODE:
        l.style.code = true;
        l.style.size *= 0.9F;
        l.inlineBox.reset();
        l.x += 3;
        break;
      case MD_SPAN_A: {
        const auto& href = static_cast<MD_SPAN_A_DETAIL*>(detail)->href;
        l.style.link.assign(href.text, href.size);
        break;
      }
      default:
        break;
      }
      return 0;
    }
    int leaveSpan(MD_SPANTYPE type, void*, void* context) {
      auto& l = *static_cast<Layout*>(context);
      l.flush();
      if (type == MD_SPAN_CODE) {
        l.x += 3;
        l.inlineBox.reset();
      }
      l.style = l.styles.back();
      l.styles.pop_back();
      return 0;
    }
    int textCallback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* context) {
      auto& l = *static_cast<Layout*>(context);
      if (type == MD_TEXT_BR || type == MD_TEXT_SOFTBR) {
        l.flush();
        l.newline(true);
      } else if (type == MD_TEXT_NULLCHAR)
        l.pending += "�";
      else if (type == MD_TEXT_CODE) {
        l.flush();
        auto raw = restore(std::string(text, size));
        if (l.codeBlock)
          l.output.boxes[l.codeBox].code += raw;
        else
          l.text(raw, true);
      } else
        l.pending.append(text, size);
      return 0;
    }
  } // namespace
  namespace {
    MessageLayout parseMessage(
        std::string_view content, float width, const MessageMeasure& measure, float fontSize,
        const std::map<std::string, std::string>& mentions, const MessageOptions& options, float emojiSize
    ) {
      if (content.empty())
        return {};
      Layout layout{
          .width = std::max(width, 32.0F), .lineHeight = fontSize + 8, .fontSize = fontSize, .measure = measure
      };
      layout.style.size = fontSize;
      layout.emojiSize = std::min(emojiSize, layout.width);
      layout.options = &options;
      layout.mentions = &mentions;
      const MD_PARSER parser{
          .abi_version = 0,
          .flags =
              MD_FLAG_NOHTML | MD_FLAG_NOINDENTEDCODEBLOCKS | MD_FLAG_STRIKETHROUGH | MD_FLAG_PERMISSIVEURLAUTOLINKS,
          .enter_block = enterBlock,
          .leave_block = leaveBlock,
          .enter_span = enterSpan,
          .leave_span = leaveSpan,
          .text = textCallback
      };
      const auto input = discordSyntax(content, options.discordTimestamps);
      md_parse(input.data(), input.size(), &parser, &layout);
      layout.alignLine();
      layout.output.height = layout.y + (layout.x > layout.indent ? layout.lineHeight : 0);
      return std::move(layout.output);
    }
  } // namespace
  MessageLayout layoutMessage(
      std::string_view content, float width, const MessageMeasure& measure, float fontSize,
      const std::map<std::string, std::string>& mentions, const MessageOptions& options
  ) {
    return parseMessage(content, width, measure, fontSize, mentions, options, 22 * fontSize / 14);
  }

  MessageLayout layoutReplyPreview(
      const api::MessageReference& reply, std::string_view author, float width, const MessageMeasure& measure,
      const MessageOptions& options
  ) {
    MessageLayout result;
    result.height = 20;
    if (width <= 0)
      return result;
    MessageStyle base;
    base.size = 12;
    std::vector<MessageRun> runs;
    auto append = [&](std::string text, MessageStyle style, std::string image = {}) {
      if (text.empty())
        return;
      style.size = style.emoji || !image.empty() ? 16 : 12;
      style.link.clear(); // The entire preview jumps to its referenced message.
      style.userMention.clear();
      const auto size = image.empty() ? measure(text, style) : 16;
      runs.push_back({std::move(text), std::move(image), 0, 0, size, style, 0, 20});
    };
    // Names are plain text, never interpreted as Markdown or emoji tokens.
    append("↳ " + oneLineReplyText(author) + ": ", base);
    auto bodyOptions = options;
    bodyOptions.compact = true;
    auto body = parseMessage(replyContent(reply), 32768, measure, 12, {}, bodyOptions, 16);
    if (options.emojiImageLinks && !body.runs.empty() && body.boxes.empty()) {
      const auto& link = body.runs.front().style.link;
      if (const auto emoji = api::emojiFromImageLink(link);
          emoji && std::ranges::all_of(body.runs, [&](const auto& run) {
            return run.image.empty() && !run.style.code && !run.style.spoiler && run.style.link == link;
          }))
        body = parseMessage(emoji->token(), 32768, measure, 12, {}, bodyOptions, 16);
    }
    std::size_t previous = 0;
    for (const auto& run : body.runs) {
      if (run.offset > previous)
        append(oneLineReplyText(std::string_view(body.plainText).substr(previous, run.offset - previous)), base);
      append(oneLineReplyText(run.text), run.style, run.image);
      previous = run.offset + run.text.size();
    }
    float total = 0;
    for (const auto& run : runs)
      total += run.width;
    const bool truncated = total > width;
    const float ellipsis = measure("…", base);
    const float limit = truncated ? std::max(0.0F, width - ellipsis) : width;
    float x = 0;
    for (auto run : runs) {
      if (run.width > limit - x) {
        if (run.image.empty() && !run.style.emoji) {
          const auto count = g_utf8_strlen(run.text.data(), run.text.size());
          std::vector<PangoLogAttr> attrs(count + 1);
          pango_get_log_attrs(
              run.text.data(), run.text.size(), -1, pango_language_get_default(), attrs.data(), attrs.size()
          );
          std::vector<std::size_t> offsets{0};
          const char* end = run.text.data();
          for (glong i = 1; i <= count; ++i) {
            end = g_utf8_next_char(end);
            if (attrs[i].is_cursor_position)
              offsets.push_back(end - run.text.data());
          }
          std::size_t low = 0, high = offsets.size();
          while (low + 1 < high) {
            const auto mid = low + (high - low) / 2;
            if (measure(std::string_view(run.text).substr(0, offsets[mid]), run.style) <= limit - x)
              low = mid;
            else
              high = mid;
          }
          run.text.resize(offsets[low]);
          run.width = measure(run.text, run.style);
        } else
          run.text.clear(); // Never draw a partial image/Unicode grapheme.
        if (!run.text.empty()) {
          run.x = x;
          run.offset = result.plainText.size();
          x += run.width;
          result.plainText += run.text;
          result.runs.push_back(std::move(run));
        }
        break;
      }
      run.x = x;
      run.offset = result.plainText.size();
      x += run.width;
      result.plainText += run.text;
      result.runs.push_back(std::move(run));
    }
    if (truncated && ellipsis <= width) {
      result.runs.push_back({"…", {}, x, 0, ellipsis, base, result.plainText.size(), 20});
      result.plainText += "…";
    }
    return result;
  }
  MessageLayout
  layoutMessage(std::string_view content, float width, const std::function<float(std::string_view)>& measure) {
    return layoutMessage(content, width, [&](std::string_view text, const MessageStyle&) { return measure(text); });
  }
  MessageLayout layoutRichMessage(
      const api::Message& message, float width, const MessageMeasure& measure, float fontSize,
      const MessageOptions& options
  ) {
    auto content = std::string_view(message.content);
    const auto first = content.find_first_not_of(" \r\n\t"), last = content.find_last_not_of(" \r\n\t");
    const auto trimmed = first == std::string_view::npos ? std::string_view{} : content.substr(first, last - first + 1);
    // Transform only exact CDN links or trailing sticker-link lines. Code and
    // spoilers retain their source presentation, and captions stay above the artwork.
    if (options.emojiImageLinks) {
      auto stickerPreview = [&](api::Sticker sticker, const std::string& link, std::string plainText) {
        auto shown = message;
        shown.content.clear();
        shown.stickers = {std::move(sticker)};
        std::erase_if(shown.embeds, [&](const auto& embed) {
          return (embed.type == "image" || embed.type == "gifv")
              && (embed.url == link || embed.image.url == link || embed.thumbnail.url == link);
        });
        auto transformed = options;
        transformed.emojiImageLinks = false;
        auto result = layoutRichMessage(shown, width, measure, fontSize, transformed);
        result.plainText = std::move(plainText);
        return result;
      };
      if (auto sticker = api::stickerFromImageLink(trimmed))
        return stickerPreview(std::move(*sticker), std::string(trimmed), message.content);
      const auto probe = layoutMessage(message.content, width, measure, fontSize, message.mentions);
      if (probe.boxes.empty()) {
        auto shown = message;
        std::vector<std::string> links;
        std::vector<api::Sticker> stickers;
        while (stickers.size() + message.stickers.size() < 3) {
          const auto end = shown.content.find_last_not_of(" \r\n\t");
          if (end == std::string::npos)
            break;
          const auto newline = shown.content.rfind('\n', end);
          const auto begin = newline == std::string::npos ? 0 : newline + 1;
          const auto candidate = shown.content.substr(begin, end - begin + 1);
          auto sticker = api::stickerFromImageLink(candidate);
          if (!sticker)
            break;
          const auto offset = probe.plainText.rfind(candidate);
          if (offset == std::string::npos || std::ranges::any_of(probe.runs, [&](const auto& run) {
                return run.offset < offset + candidate.size()
                    && run.offset + run.text.size() > offset
                    && (run.style.code || run.style.spoiler);
              }))
            break;
          links.push_back(candidate);
          stickers.push_back(std::move(*sticker));
          shown.content.erase(newline == std::string::npos ? 0 : newline);
        }
        if (!stickers.empty()) {
          for (auto it = stickers.rbegin(); it != stickers.rend(); ++it)
            shown.stickers.push_back(std::move(*it));
          std::erase_if(shown.embeds, [&](const auto& embed) {
            return (embed.type == "image" || embed.type == "gifv") && std::ranges::any_of(links, [&](const auto& link) {
                     return embed.url == link || embed.image.url == link || embed.thumbnail.url == link;
                   });
          });
          auto transformed = options;
          transformed.emojiImageLinks = false;
          auto result = layoutRichMessage(shown, width, measure, fontSize, transformed);
          result.plainText = probe.plainText;
          return result;
        }
      }
      if (!probe.runs.empty() && probe.boxes.empty()) {
        const auto& link = probe.runs.front().style.link;
        const bool standalone = std::ranges::all_of(probe.runs, [&](const auto& run) {
          return run.style.link == link && !run.style.code && !run.style.spoiler && run.image.empty();
        });
        if (auto sticker = api::stickerFromImageLink(link); sticker && standalone)
          return stickerPreview(std::move(*sticker), link, probe.plainText);
        if (const auto emoji = api::emojiFromImageLink(link); emoji && standalone) {
          auto shown = message;
          shown.content = emoji->token();
          std::erase_if(shown.embeds, [&](const auto& embed) {
            return (embed.type == "image" || embed.type == "gifv")
                && (embed.url == link || embed.image.url == link || embed.thumbnail.url == link);
          });
          auto transformed = options;
          transformed.emojiImageLinks = false;
          return layoutRichMessage(shown, width, measure, fontSize, transformed);
        }
      }
    }
    // Only replace an exact standalone media link whose preview is present.
    // Keep captions, mixed messages and unexpanded/hidden links readable.
    for (const auto& embed : message.embeds)
      if ((embed.type == "gifv" || embed.type == "image")
          && trimmed == embed.url
          && (!embed.image.url.empty() || !embed.thumbnail.url.empty() || !embed.video.url.empty())) {
        content = {};
        break;
      }
    auto result = layoutMessage(content, width, measure, fontSize, message.mentions, options);
    // Discord also emits masked links such as [woe](https://.../emoji.gif).
    // A media-only hyperlink may wrap into several runs; every visible run
    // must refer to the same rendered image, with no caption or code mixed in.
    if (!result.runs.empty() && result.boxes.empty()) {
      for (const auto& embed : message.embeds) {
        if ((embed.type != "image" && embed.type != "gifv")
            || (embed.image.url.empty() && embed.thumbnail.url.empty() && embed.video.url.empty()))
          continue;
        const bool onlyLink = std::ranges::all_of(result.runs, [&](const auto& run) {
          return !run.style.code
              && !run.style.spoiler
              && run.image.empty()
              && !run.style.link.empty()
              && (run.style.link == embed.url
                  || run.style.link == embed.image.url
                  || run.style.link == embed.thumbnail.url
                  || run.style.link == embed.video.url);
        });
        if (onlyLink) {
          result = {};
          break;
        }
      }
    }
    if (!options.compact
        && !result.runs.empty()
        && result.runs.size() <= 27
        && result.boxes.empty()
        && std::ranges::all_of(result.runs, [](const auto& run) {
             return run.style.emoji && !run.style.code && !run.style.spoiler && run.style.link.empty();
           }))
      result = parseMessage(content, width, measure, fontSize, message.mentions, options, 48 * fontSize / 14);
    const MessageStyle editedStyle{.size = std::clamp(fontSize * .75F, 9.0F, 12.0F)};
    const float editedWidth = message.editedTimestamp.empty() ? 0 : std::min(width, measure("(edited)", editedStyle));
    const float editedHeight = editedStyle.size + 8;
    if (!message.editedTimestamp.empty() && !result.runs.empty()) {
      const auto& last = result.runs.back();
      float x = last.x + last.width + 6, atY = last.y + std::max(0.0F, last.height - editedHeight);
      const bool codeBlock = std::ranges::any_of(result.boxes, [&](const auto& box) {
        return !box.code.empty() && last.y >= box.y && last.y < box.y + box.height;
      });
      if (x + editedWidth > width || codeBlock) {
        x = 0;
        atY = result.height + 2;
      }
      result.edited = MessageLayout::EditedMarker{x, atY, editedWidth, editedHeight, editedStyle.size};
      result.height = std::max(result.height, atY + editedHeight);
    }
    float y = result.height;
    auto append = [&](std::string_view text, float x, float atY, float available, MessageStyle style = {}) {
      auto block = layoutMessage(
          text, available,
          [&](std::string_view part, const MessageStyle& original) {
            auto merged = original;
            merged.bold |= style.bold;
            merged.size = original.size;
            return measure(part, merged);
          },
          style.size * fontSize / 14, {}, options
      );
      const auto offset = result.plainText.size();
      result.plainText += block.plainText;
      for (auto& run : block.runs) {
        run.x += x;
        run.y += atY;
        run.offset += offset;
        run.style.bold |= style.bold;
        run.style.spoiler |= style.spoiler;
        if (run.style.link.empty())
          run.style.link = style.link;
        result.runs.push_back(std::move(run));
      }
      for (auto& box : block.boxes) {
        box.x += x;
        box.y += atY;
        result.boxes.push_back(std::move(box));
      }
      return block.height;
    };
    auto picture = [&](const api::Media& media, float x, float atY, float available, float maxHeight,
                       std::string link) {
      float w = std::min(available, 420.0F), h = std::min(maxHeight, 220.0F);
      if (media.width > 0 && media.height > 0) {
        const float scale = std::min({w / media.width, maxHeight / media.height, 1.0F});
        w = media.width * scale;
        h = media.height * scale;
      }
      result.pictures.push_back({media, x, atY, w, h, std::move(link)});
      return h;
    };
    const auto galleryCount = options.attachmentGallery
        ? std::ranges::count_if(
              message.attachments,
              [](const auto& attachment) {
                return !attachment.image.url.empty() && !attachment.contentType.starts_with("video/");
              }
          )
        : 0;
    if (galleryCount > 1) {
      const int columns = width >= 240 ? 2 : 1;
      const float galleryWidth = std::min(width, 600.0F), gap = 4;
      const float cellWidth = (galleryWidth - gap * (columns - 1)) / columns;
      const float cellHeight = std::min(190.0F, cellWidth * .75F);
      int index = 0;
      y += y > 0 ? 8 : 0;
      for (const auto& attachment : message.attachments)
        if (!attachment.image.url.empty() && !attachment.contentType.starts_with("video/")) {
          result.pictures.push_back(
              {attachment.image, (index % columns) * (cellWidth + gap), y + (index / columns) * (cellHeight + gap),
               cellWidth, cellHeight, attachment.url}
          );
          ++index;
        }
      y += ((index + columns - 1) / columns) * (cellHeight + gap) - gap;
    }
    for (const auto& attachment : message.attachments) {
      if (galleryCount > 1 && !attachment.image.url.empty() && !attachment.contentType.starts_with("video/"))
        continue;
      y += 8;
      const bool videoAttachment = attachment.contentType.starts_with("video/")
          || (attachment.contentType.empty()
              && (attachment.filename.ends_with(".mp4") || attachment.filename.ends_with(".webm")));
      auto source = api::mediaUrl(attachment.url);
      if (source.empty())
        source = attachment.image.url;
      if (videoAttachment && !source.empty()) {
        auto poster = attachment.image;
        if (poster.url.empty())
          poster.url = source;
        if (poster.width <= 0 || poster.height <= 0) {
          poster.width = 640;
          poster.height = 360;
        }
        y += picture(poster, 0, y, width, 300, attachment.url);
        result.pictures.back().videoSource = std::move(source);
      } else if (!attachment.image.url.empty()) {
        y += picture(attachment.image, 0, y, width, 300, attachment.url);
      } else {
        result.boxes.push_back({0, y, width, 42, false, {}, true, 0x5865F2});
        append(attachment.filename, 14, y + 12, width - 28, {.link = attachment.url});
        y += 42;
      }
    }
    const auto componentButton = [&](const api::EmbedComponentLink& link, float x, float atY, float available,
                                     bool spoiler) {
      const MessageStyle style{.spoiler = spoiler, .size = 13 * fontSize / 14};
      const float textWidth = std::min(std::max(1.0F, available - 20), measure(link.label, style));
      const float buttonWidth = std::min(available, textWidth + 20), height = std::max(28.0F, style.size + 12);
      MessageBox box{x, atY, buttonWidth, height};
      box.link = link.url;
      box.disabled = link.disabled;
      box.spoiler = spoiler;
      result.boxes.push_back(std::move(box));
      const auto offset = result.plainText.size();
      result.plainText += link.label;
      result.runs.push_back(
          {link.label, {}, x + 10, atY + (height - style.size - 8) / 2, textWidth, style, offset, style.size + 8}
      );
      result.plainText += '\n';
      return height;
    };
    const auto componentImage = [&](const api::EmbedComponentMedia& item, float x, float atY, float available,
                                    float maxHeight, bool spoiler, const std::string& url) {
      const auto height = picture(item.media, x, atY, available, maxHeight, url);
      result.pictures.back().spoiler = spoiler || item.spoiler;
      if (item.video) {
        result.pictures.back().videoSource = item.media.url;
        result.pictures.back().video = true;
      }
      return height;
    };
    const auto componentLayout = [&](auto&& self, const api::EmbedComponent& component, float x, float atY,
                                     float available, bool inheritedSpoiler, const std::string& url) -> float {
      const bool spoiler = inheritedSpoiler || component.spoiler;
      const auto children = [&](float childX, float childY, float childWidth) {
        float height = 0;
        for (const auto& child : component.children) {
          const float childHeight = self(self, child, childX, childY + height, childWidth, spoiler, url);
          if (childHeight > 0)
            height += childHeight + 8;
        }
        return height > 0 ? height - 8 : 0;
      };
      switch (component.type) {
      case 17: {
        const auto index = result.boxes.size();
        result.boxes.push_back({x, atY, available, 0, false, {}, true, component.accent.value_or(0)});
        result.boxes[index].embedAccent = component.accent.has_value();
        const float inset = std::min(16.0F, available / 4);
        const auto height = children(x + inset, atY + 12, available - 2 * inset) + 24;
        result.boxes[index].height = height;
        return height;
      }
      case 1: {
        float dx = 0, dy = 0, rowHeight = 0;
        for (const auto& child : component.children) {
          if (!child.link)
            continue;
          const float size = std::min(available, measure(child.link->label, {.size = 13 * fontSize / 14}) + 20);
          if (dx > 0 && dx + size > available) {
            dy += rowHeight + 6;
            dx = 0;
            rowHeight = 0;
          }
          rowHeight =
              std::max(rowHeight, componentButton(*child.link, x + dx, atY + dy, size, spoiler || child.spoiler));
          dx += size + 6;
        }
        return dy + rowHeight;
      }
      case 9: {
        const bool aside = (component.thumbnail || component.link) && available >= 200;
        const float accessory = component.thumbnail ? 72 : std::min(120.0F, available / 3);
        float height = children(x, atY, available - (aside ? accessory + 12 : 0));
        if (component.thumbnail) {
          const auto mediaHeight = componentImage(
              *component.thumbnail, aside ? x + available - 72 : x, aside ? atY : atY + height + 8, 72, 72, spoiler, url
          );
          height = aside ? std::max(height, mediaHeight) : height + 8 + mediaHeight;
        } else if (component.link) {
          const auto linkHeight = componentButton(
              *component.link, aside ? x + available - accessory : x, aside ? atY : atY + height + 8,
              aside ? accessory : available, spoiler
          );
          height = aside ? std::max(height, linkHeight) : height + 8 + linkHeight;
        }
        return height;
      }
      case 10:
        return append(component.content, x, atY, available, {.spoiler = spoiler});
      case 11:
        return component.thumbnail
            ? componentImage(*component.thumbnail, x, atY, std::min(72.0F, available), 72, spoiler, url)
            : 0;
      case 12: {
        const int columns = component.gallery.size() > 1 && available >= 280 ? 2 : 1;
        const float cell = (available - 4 * (columns - 1)) / columns;
        float height = 0, rowHeight = 0;
        for (std::size_t i = 0; i < component.gallery.size(); ++i) {
          rowHeight = std::max(
              rowHeight,
              componentImage(
                  component.gallery[i], x + (i % columns) * (cell + 4), atY + height, cell, columns == 1 ? 320 : 200,
                  spoiler, url
              )
          );
          if ((i + 1) % columns == 0 || i + 1 == component.gallery.size()) {
            height += rowHeight + 4;
            rowHeight = 0;
          }
        }
        return height > 0 ? height - 4 : 0;
      }
      case 14: {
        const float height = component.spacing == 2 ? 16 : 8;
        if (component.divider) {
          MessageBox box{x, atY + height / 2, available, 1};
          box.divider = true;
          result.boxes.push_back(std::move(box));
        }
        return height;
      }
      case 2:
        return component.link ? componentButton(*component.link, x, atY, available, spoiler) : 0;
      default:
        return 0;
      }
    };
    for (const auto& embed : message.embeds) {
      if (!embed.components.empty()) {
        for (const auto& component : embed.components) {
          const auto height =
              componentLayout(componentLayout, component, 0, y + 8, std::min(width, 480.0F), false, embed.url);
          if (height > 0)
            y += 8 + height;
        }
        continue;
      }
      if (embed.title.empty()
          && embed.description.empty()
          && embed.provider.empty()
          && embed.author.empty()
          && embed.footer.empty()
          && embed.fields.empty()
          && embed.image.url.empty()
          && embed.thumbnail.url.empty()
          && embed.video.url.empty())
        continue;
      y += 8;
      if (embed.type == "video" && !embed.video.url.empty() && embed.provider != "YouTube") {
        const auto& poster = !embed.thumbnail.url.empty() ? embed.thumbnail : embed.video;
        y += picture(poster, 0, y, width, 300, embed.url);
        result.pictures.back().videoSource = embed.video.url;
        continue;
      }
      if (embed.type == "gifv" || embed.type == "image") {
        const auto& media = !embed.video.url.empty() ? embed.video
            : !embed.image.url.empty()               ? embed.image
                                                     : embed.thumbnail;
        if (!media.url.empty()) {
          y += picture(media, 0, y, width, 300, embed.url);
          result.pictures.back().animated = embed.type == "gifv";
          continue;
        }
      }
      const float start = y, cardWidth = std::min(width, 480.0F);
      const auto box = result.boxes.size();
      result.boxes.push_back({0, start, cardWidth, 0, false, {}, true, embed.color});
      y += 12;
      const bool video = embed.type == "video" || embed.provider == "YouTube";
      const bool thumbnail = !video
          && embed.type != "article"
          && !embed.thumbnail.url.empty()
          && embed.image.url.empty()
          && cardWidth >= 200;
      const float contentWidth = std::max(32.0F, cardWidth - 32 - (thumbnail ? 88 : 0));
      if (!embed.provider.empty())
        y += append(embed.provider, 16, y, contentWidth, {.size = 11}) + 4;
      if (!embed.author.empty())
        y += append(embed.author, 16, y, contentWidth, {.bold = true, .size = 12}) + 4;
      if (!embed.title.empty())
        y += append(embed.title, 16, y, contentWidth, {.bold = true, .size = 16, .link = embed.url}) + 6;
      if (!embed.description.empty() && !video)
        y += append(embed.description, 16, y, contentWidth) + 6;
      for (const auto& field : embed.fields) {
        y += append(field.name, 16, y, contentWidth, {.bold = true}) + 2;
        y += append(field.value, 16, y, contentWidth) + 8;
      }
      if (thumbnail) {
        picture(embed.thumbnail, cardWidth - 88, start + Style::spaceMd, 72, 72, embed.url);
        y = std::max(y, start + 84);
      }
      if (!embed.image.url.empty() || (!thumbnail && !embed.thumbnail.url.empty())) {
        y += 4;
        y += picture(
            embed.image.url.empty() ? embed.thumbnail : embed.image, 16, y, cardWidth - Style::space2Xl, 280, embed.url
        );
        result.pictures.back().video = video;
      }
      if (!embed.footer.empty()) {
        y += 8;
        y += append(embed.footer, 16, y, cardWidth - 32, {.size = 11});
      }
      y += 12;
      result.boxes[box].height = y - start;
    }
    if (!message.stickers.empty()) {
      if (y > 0)
        y += 4;
      const float size = std::min(160.0F, width), gap = 8;
      const int columns = std::clamp(static_cast<int>((width + gap) / (size + gap)), 1, 3);
      for (std::size_t i = 0; i < message.stickers.size(); ++i) {
        const auto& sticker = message.stickers[i];
        result.pictures.push_back(
            {{api::stickerAsset(sticker), 320, 320},
             (i % columns) * (size + gap),
             y + (i / columns) * (size + gap),
             size,
             size,
             {},
             false,
             sticker.format != 1,
             {},
             sticker}
        );
      }
      y += std::ceil(static_cast<float>(message.stickers.size()) / columns) * (size + gap) - gap;
    }
    if (!message.editedTimestamp.empty() && !result.edited) {
      result.edited = MessageLayout::EditedMarker{0, y + 2, editedWidth, editedHeight, editedStyle.size};
      y += 2 + editedHeight;
    }
    if (message.forwarded) {
      const auto& forward = *message.forwarded;
      api::Message captured;
      captured.content = forward.available ? forward.content : "Forwarded message unavailable";
      captured.attachments = forward.attachments;
      captured.embeds = forward.embeds;
      captured.stickers = forward.stickers;
      captured.mentions = forward.mentions;
      if (forward.available
          && captured.content.empty()
          && captured.attachments.empty()
          && captured.embeds.empty()
          && captured.stickers.empty())
        captured.content = "Forwarded message";
      auto settings = options;
      settings.attachmentGallery = true;
      auto part = layoutRichMessage(captured, std::max(32.0F, width - 12), measure, fontSize, settings);
      if (y > 0)
        y += 8;
      const float start = y;
      y += 22;
      if (!result.plainText.empty() && !result.plainText.ends_with('\n'))
        result.plainText += '\n';
      const auto offset = result.plainText.size();
      for (auto& run : part.runs) {
        run.x += 12;
        run.y += y;
        run.offset += offset;
        result.runs.push_back(std::move(run));
      }
      for (auto& box : part.boxes) {
        box.x += 12;
        box.y += y;
        result.boxes.push_back(std::move(box));
      }
      for (auto& image : part.pictures) {
        image.x += 12;
        image.y += y;
        result.pictures.push_back(std::move(image));
      }
      result.plainText += part.plainText;
      y += part.height + 26;
      result.forwards.push_back({start, y - start, forward.guild, forward.channel, forward.message, forward.timestamp});
    }
    result.height = y;
    return result;
  }
} // namespace app
