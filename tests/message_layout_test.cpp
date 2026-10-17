#include "app/emoji_catalog.h"
#include "app/message_layout.h"
#include "app/text_edit.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  auto require = [](bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  };
  try {
    require(argc == 2, "Emoji assets directory required");
    app::EmojiCatalog catalog;
    catalog.load(std::filesystem::path(argv[1]) / "emoji.json", std::filesystem::path(argv[1]) / "emoji-aliases.json");
    const app::MessageOptions emojiOptions{.unicodeEmoji = [&](std::string_view text) {
      return catalog.isUnicode(text);
    }};
    const app::MessageMeasure emojiMeasure = [](std::string_view text, const app::MessageStyle& style) {
      return style.emoji ? style.size : static_cast<float>(text.size() * 8);
    };
    discord::Message emojiMessage;
    emojiMessage.content = "🙂 ❤️ 👩🏽‍💻 🇩🇪 1️⃣ <:wave:123>";
    auto sized = app::layoutRichMessage(emojiMessage, 600, emojiMeasure, 14, emojiOptions);
    require(
        sized.runs.size() == 6
            && std::ranges::all_of(
                sized.runs,
                [](const auto& run) {
                  return run.style.emoji && (run.image.empty() ? run.style.size == 48 : run.width == 48);
                }
            ),
        "Unicode, skin tones, ZWJ, flags, keycaps and custom emoji share jumbo sizing"
    );
    for (const auto& run : sized.runs)
      require(run.y + run.height <= sized.height, "Jumbo rows reserve height for every emoji");
    auto compact = emojiOptions;
    compact.compact = true;
    sized = app::layoutRichMessage(emojiMessage, 600, emojiMeasure, 14, compact);
    require(
        std::ranges::all_of(
            sized.runs,
            [](const auto& run) {
              return run.style.emoji && (run.image.empty() ? run.style.size == 22 : run.width == 22);
            }
        ),
        "Compact messages retain consistent inline sizing"
    );
    emojiMessage.content = "Hello 🙂 <:wave:123>!";
    sized = app::layoutRichMessage(emojiMessage, 600, emojiMeasure, 14, emojiOptions);
    require(
        std::ranges::count_if(sized.runs, [](const auto& run) { return run.style.emoji; }) == 2
            && std::ranges::all_of(
                sized.runs,
                [](const auto& run) {
                  return !run.style.emoji || (run.image.empty() ? run.style.size == 22 : run.width == 22);
                }
            ),
        "Mixed text uses larger inline Unicode emoji without enlarging the message"
    );
    emojiMessage.content.clear();
    for (int i = 0; i < 27; ++i)
      emojiMessage.content += "🙂";
    sized = app::layoutRichMessage(emojiMessage, 120, emojiMeasure, 14, emojiOptions);
    require(
        sized.runs.size() == 27 && sized.runs.front().style.size == 48 && sized.height > 100,
        "Up to 27 jumbo emoji wrap across lines"
    );
    emojiMessage.content += "🙂";
    sized = app::layoutRichMessage(emojiMessage, 120, emojiMeasure, 14, emojiOptions);
    require(sized.runs.size() == 28 && sized.runs.front().style.size == 22, "28 emoji return to inline size");
    emojiMessage.content = "`🙂 <:wave:123>`";
    sized = app::layoutRichMessage(emojiMessage, 600, emojiMeasure, 14, emojiOptions);
    require(
        std::ranges::none_of(sized.runs, [](const auto& run) { return run.style.emoji; }),
        "Code keeps literal text sizing"
    );
    emojiMessage.content = "<:wave:123>";
    sized = app::layoutRichMessage(emojiMessage, 32, emojiMeasure, 14, emojiOptions);
    require(sized.runs.front().width <= 32, "Jumbo custom emoji fit narrow columns");
    auto measure = [](std::string_view value) { return static_cast<float>(value.size() * 8); };
    auto layout = app::layoutMessage("hi <:wave:123> there\n<a:dance:456>!", 200, measure);
    require(
        layout.runs.size() == 5
            && layout.runs[1].image == "/emojis/123.png?size=128"
            && layout.runs[3].image == "/emojis/456.gif?size=128",
        "Render static and animated custom emoji inline"
    );
    require(
        layout.runs[1].x > layout.runs[0].x && layout.runs[3].y > layout.runs[1].y,
        "Preserve emoji positions and explicit line breaks"
    );
    discord::Message mentionMessage;
    mentionMessage.content = "Hi <@123> and <@!123>, see <#456>. `literal <@123>`";
    mentionMessage.mentions = {{"123", "Maya **literal**"}, {"456", "general"}};
    const auto mentions =
        app::layoutRichMessage(mentionMessage, 1800, [&](std::string_view text, const app::MessageStyle&) {
          return measure(text);
        });
    require(
        mentions.plainText.contains("@Maya **literal**")
            && mentions.plainText.contains("#general")
            && mentions.plainText.contains("literal <@123>"),
        "Resolve mentions outside code, preserving literal display names and code"
    );
    require(
        std::ranges::any_of(mentions.runs, [](const auto& run) { return run.style.mention && !run.style.bold; }),
        "Mention names cannot inject Markdown and receive mention styling"
    );
    auto malformed = app::layoutMessage("<:broken:abc>", 400, measure);
    require(
        malformed.runs.size() == 1 && malformed.runs[0].image.empty() && malformed.runs[0].text == "<:broken:abc>",
        "Malformed emoji must remain readable text"
    );
    auto wrapped = app::layoutMessage("abcdefghij <:ok:1>", 32, measure);
    require(wrapped.height > 24, "Wrap long words and emoji rather than overflowing");
    for (const auto& run : wrapped.runs)
      require(run.x + run.width <= 32, "Every fragment fits within the message column");
    auto unicode = app::layoutMessage("hello 👩‍💻 🌿", 500, measure);
    std::string text;
    for (const auto& run : unicode.runs)
      text += run.text;
    require(
        text.contains("👩‍💻") && text.contains("🌿"),
        "Keep Unicode emoji and ZWJ sequences for Pango color-font rendering"
    );
    auto markdown =
        app::layoutMessage("**bold** *italic* _also italic_ __underlined__ ~~gone~~ `a < b` ||secret||", 1200, measure);
    auto styled = [&](std::string_view text, auto test) {
      return std::ranges::any_of(markdown.runs, [&](const auto& run) { return run.text == text && test(run.style); });
    };
    require(styled("bold", [](const auto& s) { return s.bold; }), "Render strong emphasis");
    require(styled("italic", [](const auto& s) { return s.italic; }), "Render emphasis");
    require(styled("underlined", [](const auto& s) { return s.underline; }), "Discord double-underscore underline");
    require(styled("gone", [](const auto& s) { return s.strike; }), "Render strikethrough");
    require(styled("secret", [](const auto& s) { return s.spoiler; }), "Spoilers remain hidden until interaction");
    auto unmatched = app::layoutMessage("literal __marker and || unfinished", 600, measure);
    require(
        unmatched.plainText.contains("__marker") && unmatched.plainText.contains("||"),
        "Unpaired delimiters remain visible"
    );
    auto escaped = app::messageMarkup("<span>& \"bad\"", {.bold = true});
    require(
        escaped.find("&lt;span&gt;") != std::string::npos && escaped.find("&amp;") != std::string::npos,
        "Untrusted message text cannot inject Pango markup"
    );
    auto code = app::layoutMessage("```cpp\n  if (a < b) {\n    __literal__;\n  }\n```", 240, measure);
    require(
        !code.boxes.empty() && code.boxes.front().code == "  if (a < b) {\n    __literal__;\n  }\n",
        "Code preserves indentation, newlines, and literal delimiters for copying"
    );
    require(
        std::ranges::all_of(code.runs, [](const auto& run) { return run.style.code && !run.style.underline; }),
        "Fenced code uses monospace without interpreting formatting"
    );
    auto link = app::layoutMessage("[docs](https://example.com)\n> quote\n- list item", 300, measure);
    require(link.runs.front().style.link == "https://example.com", "Masked links retain their actual URL");
    require(
        std::ranges::any_of(link.boxes, [](const auto& box) { return box.quote; }), "Blockquotes have a native rule"
    );
    discord::Message rich;
    rich.content = "**An image**";
    rich.attachments.push_back(
        {"1",
         "photo.png",
         "https://cdn.discordapp.com/attachments/1/2/photo.png",
         "image/png",
         {"https://cdn.discordapp.com/attachments/1/2/photo.png", 1920, 1080}}
    );
    discord::Embed embed;
    embed.title = "Preview";
    embed.description = "**Formatted** description";
    embed.fields.push_back({"Field", "value"});
    rich.embeds.push_back(embed);
    auto richLayout = app::layoutRichMessage(rich, 400, [&](std::string_view text, const app::MessageStyle&) {
      return measure(text);
    });
    require(
        richLayout.pictures.size() == 1
            && richLayout.pictures.front().width <= 400
            && richLayout.pictures.front().height <= 300,
        "Image previews preserve aspect ratio within the message column"
    );
    require(
        std::ranges::any_of(richLayout.boxes, [](const auto& box) { return box.embed; })
            && richLayout.plainText.contains("Preview")
            && richLayout.plainText.contains("value"),
        "Embeds lay out titles, descriptions, and fields"
    );
    for (const auto& run : richLayout.runs)
      require(
          richLayout.plainText.substr(run.offset, run.text.size()) == run.text,
          "Selection offsets refer to rendered plain text"
      );
    discord::Message video;
    discord::Message animated;
    discord::Embed gif;
    gif.type = "gifv";
    gif.url = "https://klipy.com/gifs/example";
    gif.video = {"https://static.klipy.com/ii/example.mp4", 320, 180};
    gif.thumbnail = {"https://static.klipy.com/ii/still.png", 320, 180};
    animated.embeds.push_back(gif);
    animated.content = gif.url;
    const auto animationLayout =
        app::layoutRichMessage(animated, 400, [&](std::string_view text, const app::MessageStyle&) {
          return measure(text);
        });
    require(
        animationLayout.pictures.size() == 1
            && !animationLayout.pictures[0].video
            && animationLayout.pictures[0].animated
            && animationLayout.pictures[0].media.url == gif.video.url
            && animationLayout.boxes.empty(),
        "GIF videos use their looping media, without a video link card or play overlay"
    );
    require(
        animationLayout.runs.empty() && animationLayout.plainText.empty(),
        "A standalone GIF link disappears when the media preview is rendered"
    );
    auto mixed = animated;
    auto masked = animated;
    masked.content = "[woe](" + gif.url + ")";
    require(
        app::layoutRichMessage(
            masked, 400, [&](std::string_view text, const app::MessageStyle&) { return measure(text); }
        ).runs.empty(),
        "Hide a masked media-only link without a duplicate label"
    );
    mixed.content = "Look at this " + gif.url;
    auto mixedLayout = app::layoutRichMessage(mixed, 400, [&](std::string_view text, const app::MessageStyle&) {
      return measure(text);
    });
    require(
        mixedLayout.plainText.contains("Look at this") && mixedLayout.plainText.contains(gif.url),
        "Keep captions and mixed message text"
    );
    auto hidden = animated;
    hidden.embeds.clear();
    require(
        !app::layoutRichMessage(
             hidden, 400, [&](std::string_view text, const app::MessageStyle&) { return measure(text); }
        ).runs.empty(),
        "Hidden or missing GIF previews retain their source link"
    );
    auto image = animated;
    image.embeds.front().type = "image";
    image.embeds.front().video = {};
    image.embeds.front().image = {};
    const auto imageLayout = app::layoutRichMessage(image, 400, [&](std::string_view text, const app::MessageStyle&) {
      return measure(text);
    });
    require(
        imageLayout.pictures.size() == 1
            && imageLayout.boxes.empty()
            && imageLayout.runs.empty()
            && imageLayout.pictures[0].media.url == gif.thumbnail.url,
        "Image embeds with thumbnail media display as an unframed image, not a link card"
    );
    discord::Message fake;
    const std::string fakeUrl = "https://cdn.discordapp.com/emojis/123456789.gif?size=48&name=wave&lossless=true";
    fake.content = "[wave](" + fakeUrl + ")";
    auto fakeLayout = app::layoutRichMessage(
        fake, 400, [&](std::string_view text, const app::MessageStyle&) { return measure(text); }, 14,
        {.fakeNitroEmojis = true}
    );
    require(
        fakeLayout.runs.size() == 1 && !fakeLayout.runs[0].image.empty() && fakeLayout.pictures.empty(),
        "Standalone FakeNitro links render inline without requiring a server embed"
    );
    discord::Embed duplicate;
    duplicate.type = "image";
    duplicate.url = fakeUrl;
    duplicate.thumbnail = {fakeUrl, 48, 48};
    fake.embeds.push_back(duplicate);
    fakeLayout = app::layoutRichMessage(
        fake, 400, [&](std::string_view text, const app::MessageStyle&) { return measure(text); }, 14,
        {.fakeNitroEmojis = true}
    );
    require(fakeLayout.runs.size() == 1 && fakeLayout.pictures.empty(), "Suppress duplicate FakeNitro image previews");
    fake.content = "`" + fake.content + "`";
    fake.embeds.clear();
    fakeLayout = app::layoutRichMessage(
        fake, 400, [&](std::string_view text, const app::MessageStyle&) { return measure(text); }, 14,
        {.fakeNitroEmojis = true}
    );
    require(
        std::ranges::none_of(fakeLayout.runs, [](const auto& run) { return !run.image.empty(); }),
        "Code containing emoji CDN links remains literal"
    );
    discord::Embed youtube;
    youtube.type = "video";
    youtube.provider = "YouTube";
    youtube.title = "A video";
    youtube.description = "Long descriptions should not replace the video thumbnail.";
    youtube.thumbnail = {
        "https://images-ext-1.discordapp.net/external/hash/https/i.ytimg.com/vi/test/hqdefault.jpg", 1280, 720
    };
    video.embeds.push_back(youtube);
    auto videoLayout = app::layoutRichMessage(video, 400, [&](std::string_view text, const app::MessageStyle&) {
      return measure(text);
    });
    require(
        videoLayout.pictures.size() == 1
            && videoLayout.pictures[0].video
            && videoLayout.pictures[0].width > 300
            && !videoLayout.plainText.contains("Long descriptions"),
        "YouTube embeds use a full-width video thumbnail and play action"
    );
    auto larger = app::layoutRichMessage(
        rich, 400,
        [&](std::string_view text, const app::MessageStyle& style) { return measure(text) * style.size / 14; }, 20
    );
    require(
        larger.height > richLayout.height && larger.runs.front().style.size == 20,
        "Message font settings affect both measured wrapping and drawn glyph sizes"
    );
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
