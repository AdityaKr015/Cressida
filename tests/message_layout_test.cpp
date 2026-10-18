#include "app/discord_timestamp.h"
#include "app/emoji_catalog.h"
#include "app/message_layout.h"
#include "app/message_log_layout.h"
#include "app/reply_preview.h"
#include "app/text_edit.h"

#include <algorithm>
#include <iostream>
#include <ranges>
#include <stdexcept>

int main(int argc, char** argv) {
  auto require = [](bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  };
  try {
    const app::MessageMeasure editMeasure = [](std::string_view text, const app::MessageStyle& style) {
      return static_cast<float>(text.size()) * style.size * .5F;
    };
    api::Message editedMessage{"900", "10", "Maya", "Hello", "2026-10-17T09:00:00Z"};
    const auto originalLayout = app::layoutRichMessage(editedMessage, 400, editMeasure);
    require(!originalLayout.edited, "Unedited messages have no marker");
    editedMessage.editedTimestamp = "2026-10-17T09:01:00Z";
    auto editedLayout = app::layoutRichMessage(editedMessage, 400, editMeasure);
    require(
        editedLayout.edited
            && editedLayout.edited->x > 0
            && editedLayout.edited->y < 22
            && editedLayout.plainText == originalLayout.plainText,
        "Edited markers follow the last text line without becoming copied message text"
    );
    editedLayout = app::layoutRichMessage(editedMessage, 42, editMeasure);
    require(
        editedLayout.edited
            && editedLayout.edited->x == 0
            && editedLayout.height >= editedLayout.edited->y + editedLayout.edited->height,
        "Narrow messages reserve a new line for the marker without overflowing"
    );
    api::Message oldVersion = editedMessage;
    oldVersion.content = "Earlier **text** with <@1>";
    oldVersion.mentions["1"] = "Alex";
    oldVersion.editedTimestamp.clear();
    const auto liveLayout = app::layoutRichMessage(editedMessage, 400, editMeasure);
    const auto loggedLayout = app::withEditHistory(liveLayout, std::vector{oldVersion}, 400, editMeasure, 14);
    require(
        loggedLayout.historyHeight > 0
            && loggedLayout.edited
            && loggedLayout.edited->y == liveLayout.edited->y + loggedLayout.historyHeight
            && loggedLayout.height == liveLayout.height + loggedLayout.historyHeight,
        "Edit history reserves height and shifts the current edited marker together"
    );
    require(
        loggedLayout.plainText.contains("Earlier text with @Alex")
            && loggedLayout.plainText.ends_with(liveLayout.plainText),
        "Historical text remains selectable and current content stays last"
    );
    for (const auto& run : loggedLayout.runs) {
      require(
          run.style.historical == (run.y < loggedLayout.historyHeight),
          "Only earlier versions use muted history styling"
      );
      require(
          run.offset + run.text.size() <= loggedLayout.plainText.size(), "History text selection offsets stay valid"
      );
      if (run.image.empty())
        require(
            loggedLayout.plainText.substr(run.offset, run.text.size()) == run.text,
            "Selection offsets identify the exact historical/current text"
        );
    }
    require(
        app::withEditHistory(liveLayout, {}, 400, editMeasure, 14).height == liveLayout.height,
        "Without retained edits the normal message layout is unchanged"
    );
    editedMessage.content = "```cpp\nint x = 1;\n```";
    editedLayout = app::layoutRichMessage(editedMessage, 400, editMeasure);
    require(
        editedLayout.edited
            && editedLayout.edited->x == 0
            && !editedLayout.boxes.empty()
            && editedLayout.edited->y >= editedLayout.boxes.back().y + editedLayout.boxes.back().height,
        "Edited labels sit outside fenced code blocks"
    );
    editedMessage.content.clear();
    editedMessage.attachments.push_back(
        {"1",
         "image.png",
         "https://cdn.discordapp.com/attachments/1/2/image.png",
         "image/png",
         {"https://cdn.discordapp.com/attachments/1/2/image.png", 200, 100}}
    );
    editedLayout = app::layoutRichMessage(editedMessage, 400, editMeasure);
    require(
        editedLayout.edited
            && !editedLayout.pictures.empty()
            && editedLayout.edited->y >= editedLayout.pictures.back().y + editedLayout.pictures.back().height,
        "Media-only edited messages keep their marker clear of the image"
    );
    api::MessageReference reference{
        "1", "2", "3", "Maya",
        "First line\r\nSecond\tline\xE2\x80\xA8"
        "Third\xE2\x80\xA9"
        "Fourth"
    };
    require(
        app::replyPreview(reference, "Maya\nname") == "↳ Maya name: First line Second line Third Fourth",
        "Reply previews flatten body and author paragraph breaks into a single line"
    );
    reference.content = std::string(4095, 'a') + "🙂";
    require(
        app::replyPreview(reference, "Maya").ends_with(std::string(4095, 'a')),
        "Bounding long reply previews never splits a UTF-8 character"
    );
    reference.deleted = true;
    require(
        app::replyPreview(reference, "Maya") == "↳ Maya: Original message deleted",
        "Deleted reply previews retain a readable fallback"
    );
    require(argc == 2, "Emoji assets directory required");
    app::EmojiCatalog catalog;
    catalog.load(std::filesystem::path(argv[1]) / "emoji.json", std::filesystem::path(argv[1]) / "emoji-aliases.json");
    const app::MessageOptions emojiOptions{.unicodeEmoji = [&](std::string_view text) {
      return catalog.isUnicode(text);
    }};
    const app::MessageMeasure emojiMeasure = [](std::string_view text, const app::MessageStyle& style) {
      return style.emoji ? style.size : static_cast<float>(text.size() * 8);
    };
    auto profileOptions = emojiOptions;
    profileOptions.compact = true;
    profileOptions.discordTimestamps = true;
    auto profileBio = app::layoutMessage(
        "**My midnight** is <t:1754172000:t> <:wave:123>", 900, emojiMeasure, 13, {}, profileOptions
    );
    require(
        profileBio.plainText.find(*app::discordTimestamp("t:1754172000:t")) != std::string::npos
            && profileBio.plainText.find("<t:") == std::string::npos
            && std::ranges::any_of(profileBio.runs, [](const auto& run) { return run.style.bold; })
            && std::ranges::any_of(profileBio.runs, [](const auto& run) { return !run.image.empty(); }),
        "Profile bios format Discord timestamps alongside escaped Markdown and custom emoji"
    );
    profileBio = app::layoutMessage(
        "`<t:1754172000:t>` <t:invalid:t> <span color='red'>bio</span>", 900, emojiMeasure, 13, {}, profileOptions
    );
    require(
        std::ranges::any_of(
            profileBio.runs, [](const auto& run) { return run.style.code && run.text == "<t:1754172000:t>"; }
        )
            && profileBio.plainText.find("<t:invalid:t>") != std::string::npos
            && profileBio.plainText.find("<span color='red'>bio</span>") != std::string::npos,
        "Timestamps in code, malformed tokens and raw HTML stay literal in profiles"
    );
    reference.deleted = false;
    reference.content = "my beautiful pink role <:woe:1407464763293831269> <a:dance:123> 🙂";
    auto reply = app::layoutReplyPreview(reference, "Yocraft", 1000, emojiMeasure, emojiOptions);
    auto replyImages = std::views::filter(reply.runs, [](const auto& run) { return !run.image.empty(); });
    require(
        std::ranges::distance(replyImages) == 2, "Reply previews render static and animated custom emoji as images"
    );
    require(replyImages.front().image == api::emojiPath("1407464763293831269", false), "Static reply emoji uses PNG");
    require(std::next(replyImages.begin())->image == api::emojiPath("123", true), "Animated reply emoji uses GIF");
    require(
        std::ranges::all_of(
            reply.runs,
            [](const auto& run) { return run.y == 0 && run.height == 20 && (!run.style.emoji || run.style.size == 16); }
        ) && reply.height == 20,
        "Reply emoji stay inline inside one reserved row; never jumbo"
    );
    reference.content = "<:wave:123><a:dance:456>";
    reply = app::layoutReplyPreview(reference, "Maya", 500, emojiMeasure, emojiOptions);
    require(
        std::ranges::count_if(reply.runs, [](const auto& run) { return !run.image.empty(); }) == 2,
        "Adjacent reply emoji are separate native images"
    );
    reference.content = "`<:wave:123>` <:bad:not-a-snowflake> <:wave:123>";
    reply = app::layoutReplyPreview(reference, "**Maya** <:name:789>", 2000, emojiMeasure, emojiOptions);
    require(
        std::ranges::count_if(reply.runs, [](const auto& run) { return !run.image.empty(); }) == 1
            && reply.runs.front().text == "↳ **Maya** <:name:789>: "
            && std::ranges::any_of(
                reply.runs, [](const auto& run) { return run.style.code && run.text == "<:wave:123>"; }
            ),
        "Reply author names, code and malformed custom tags are never interpreted as emoji images"
    );
    reference.content = "first\nsecond\n\nthird <:wave:123>";
    reply = app::layoutReplyPreview(reference, "Maya", 1000, emojiMeasure, emojiOptions);
    require(
        reply.plainText.find('\n') == std::string::npos
            && std::ranges::all_of(reply.runs, [](const auto& run) { return run.y == 0; }),
        "Rich reply previews flatten every paragraph onto one line"
    );
    reference.content = "lots of words <:wave:123> and more words";
    reply = app::layoutReplyPreview(reference, "Maya", 140, emojiMeasure, emojiOptions);
    require(
        reply.plainText.ends_with("…")
            && std::ranges::all_of(reply.runs, [](const auto& run) { return run.x >= 0 && run.x + run.width <= 140; }),
        "Truncated reply runs and ellipsis fit the viewport"
    );
    reference.content = "<:wave:123>";
    const auto prefix = emojiMeasure("↳ Maya: ", {.size = 12});
    reply = app::layoutReplyPreview(reference, "Maya", prefix + 15, emojiMeasure, emojiOptions);
    require(
        std::ranges::none_of(reply.runs, [](const auto& run) { return !run.image.empty(); }),
        "A clipped emoji is omitted whole, without requesting its asset"
    );
    reference.content = "https://cdn.discordapp.com/emojis/123.png?size=48&name=wave";
    reply = app::layoutReplyPreview(reference, "Maya", 1000, emojiMeasure, {.emojiImageLinks = true});
    require(
        std::ranges::count_if(reply.runs, [](const auto& run) { return !run.image.empty(); }) == 1,
        "Opt-in emoji image links render consistently in reply previews"
    );
    reference.content = "||<:hidden:123>||";
    reply = app::layoutReplyPreview(reference, "Maya", 500, emojiMeasure, emojiOptions);
    require(
        std::ranges::any_of(reply.runs, [](const auto& run) { return run.style.spoiler; }),
        "Reply previews preserve spoiler masking instead of revealing hidden emoji"
    );
    reference.deleted = true;
    reply = app::layoutReplyPreview(reference, "Maya", 1000, emojiMeasure, emojiOptions);
    require(
        reply.plainText == "↳ Maya: Original message deleted"
            && std::ranges::none_of(reply.runs, [](const auto& run) { return !run.image.empty(); }),
        "Deleted reply previews cannot load media from old content"
    );
    api::Message emojiMessage;
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
    auto noticeOptions = emojiOptions;
    noticeOptions.mentionPrefix = false;
    const auto notice = app::layoutMessage(
        "Welcome <@123>!", 1800, emojiMeasure, 14, {{"123", "Maya **literal** <b>name</b>"}}, noticeOptions
    );
    require(
        notice.plainText.starts_with("Welcome Maya **literal** <b>name</b>!")
            && !notice.plainText.contains("@")
            && std::ranges::any_of(notice.runs, [](const auto& run) { return run.style.mention && !run.style.bold; }),
        "Inline system names omit the mention prefix while remaining literal and styled"
    );
    api::Message mentionMessage;
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
    api::Message rich;
    rich.content = "**An image**";
    rich.attachments.push_back(
        {"1",
         "photo.png",
         "https://cdn.discordapp.com/attachments/1/2/photo.png",
         "image/png",
         {"https://cdn.discordapp.com/attachments/1/2/photo.png", 1920, 1080}}
    );
    api::Embed embed;
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
    api::Message video;
    api::Message attachedVideo;
    const std::string videoUrl = "https://cdn.discordapp.com/attachments/1/2/clip.mp4?ex=a&hm=b";
    attachedVideo.attachments.push_back({"2", "clip.mp4", videoUrl, "video/mp4", {videoUrl, 1920, 1080}});
    const auto attachedLayout =
        app::layoutRichMessage(attachedVideo, 400, [&](std::string_view text, const app::MessageStyle&) {
          return measure(text);
        });
    require(
        attachedLayout.pictures.size() == 1
            && attachedLayout.pictures[0].videoSource == videoUrl
            && !attachedLayout.pictures[0].animated
            && attachedLayout.pictures[0].media.width == 1920,
        "Video attachments retain the original signed source and use the native player, never GIF playback"
    );
    attachedVideo.attachments[0].contentType.clear();
    attachedVideo.attachments[0].image = {};
    const auto missingMetadata =
        app::layoutRichMessage(attachedVideo, 400, [&](std::string_view text, const app::MessageStyle&) {
          return measure(text);
        });
    require(
        missingMetadata.pictures.size() == 1 && missingMetadata.pictures[0].videoSource == videoUrl,
        "Known video extensions remain playable without optional MIME/thumbnail dimensions"
    );
    api::Message animated;
    api::Embed gif;
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
            && animationLayout.pictures[0].videoSource.empty()
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
    api::Message fake;
    const std::string fakeUrl = "https://cdn.discordapp.com/emojis/123456789.gif?size=48&name=wave&lossless=true";
    fake.content = "[wave](" + fakeUrl + ")";
    auto fakeLayout = app::layoutRichMessage(
        fake, 400, [&](std::string_view text, const app::MessageStyle&) { return measure(text); }, 14,
        {.emojiImageLinks = true}
    );
    require(
        fakeLayout.runs.size() == 1 && !fakeLayout.runs[0].image.empty() && fakeLayout.pictures.empty(),
        "Standalone emoji image links render inline without requiring a server embed"
    );
    api::Embed duplicate;
    duplicate.type = "image";
    duplicate.url = fakeUrl;
    duplicate.thumbnail = {fakeUrl, 48, 48};
    fake.embeds.push_back(duplicate);
    fakeLayout = app::layoutRichMessage(
        fake, 400, [&](std::string_view text, const app::MessageStyle&) { return measure(text); }, 14,
        {.emojiImageLinks = true}
    );
    require(fakeLayout.runs.size() == 1 && fakeLayout.pictures.empty(), "Suppress duplicate emoji image previews");
    fake.content = "`" + fake.content + "`";
    fake.embeds.clear();
    fakeLayout = app::layoutRichMessage(
        fake, 400, [&](std::string_view text, const app::MessageStyle&) { return measure(text); }, 14,
        {.emojiImageLinks = true}
    );
    require(
        std::ranges::none_of(fakeLayout.runs, [](const auto& run) { return !run.image.empty(); }),
        "Code containing emoji CDN links remains literal"
    );
    api::Embed youtube;
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
