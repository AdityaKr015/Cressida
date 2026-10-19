#include "api/embed_components.h"
#include "app/message_layout.h"
#include "app/message_log.h"

#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

int main() {
  using Json = nlohmann::json;
  const auto require = [](bool value, const char* error) {
    if (!value)
      throw std::runtime_error(error);
  };
  const app::MessageMeasure measure = [](std::string_view value, const app::MessageStyle& style) {
    return value.size() * style.size * .5F;
  };
  try {
    const std::string url = "https://en.wikipedia.org/wiki/D_(programming_language)";
    const std::string proxy = "https://images-ext-1.discordapp.net/external/hash/https/upload.wikimedia.org/D.png";
    Json embed = Json::parse(
        R"EMBED({"type":"components","url":"https://en.wikipedia.org/wiki/D_(programming_language)","components":[{"type":17,"components":[
      {"type":10,"content":"## [D (programming language)](https://en.wikipedia.org/wiki/D_(programming_language))"},
      {"type":10,"content":"D, also known as dlang, is a multi-paradigm system programming language."},
      {"type":12,"items":[{"media":{"url":"https://upload.wikimedia.org/D.png","proxy_url":"https://images-ext-1.discordapp.net/external/hash/https/upload.wikimedia.org/D.png","width":640,"height":480,"content_type":"image/png"}}]}
    ]}]})EMBED"
    );
    embed["url"] = url;
    api::Message message{"100", "10", "Maya", url + " 😂", "2026-10-19T12:00:00Z", "11"};
    api::parseMessagePatch(Json{{"embeds", Json::array({embed})}}).apply(message);
    require(
        message.embeds.size() == 1
            && message.embeds[0].components.size() == 1
            && message.embeds[0].components[0].children.size() == 3,
        "Parse component link previews instead of discarding all their fields"
    );
    const auto layout = app::layoutRichMessage(message, 500, measure);
    require(
        layout.plainText.contains("D (programming language)")
            && layout.plainText.contains("multi-paradigm")
            && layout.pictures.size() == 1
            && layout.pictures[0].media.url == proxy,
        "Render article text and its proxied image while retaining the message caption"
    );
    require(
        layout.boxes.size() == 1
            && layout.boxes[0].embed
            && !layout.boxes[0].embedAccent
            && layout.boxes[0].height > 300,
        "A component container has measured content and no invented accent stripe"
    );
    require(
        std::ranges::any_of(layout.runs, [&](const auto& run) { return run.style.link == url && run.style.bold; }),
        "Markdown titles retain their clickable URL, including balanced parentheses"
    );
    for (const auto& run : layout.runs)
      require(
          layout.plainText.substr(run.offset, run.text.size()) == run.text, "Embed text selection offsets stay valid"
      );
    require(
        layout.pictures[0].y > layout.runs.back().y
            && layout.pictures[0].y + layout.pictures[0].height <= layout.height,
        "The article hero follows its text and stays inside the measured card"
    );
    for (float width : {80.0F, 160.0F, 320.0F}) {
      const auto narrow = app::layoutRichMessage(message, width, measure);
      for (const auto& picture : narrow.pictures)
        require(
            picture.x >= 0 && picture.x + picture.width <= width + .01F && picture.y + picture.height <= narrow.height,
            "Media stays inside narrow preview columns"
        );
    }
    auto updated = message;
    api::parseMessagePatch(Json{{"content", "Edited caption"}}).apply(updated);
    require(updated.embeds == message.embeds, "Partial content edits preserve component previews");
    api::parseMessagePatch(Json{{"embeds", Json::array()}}).apply(updated);
    require(
        updated.embeds.empty() && updated.content == "Edited caption",
        "An explicit empty embed array clears modern previews"
    );
    api::Message placeholder = message;
    placeholder.embeds = {api::Embed{.url = url, .type = "components"}};
    const auto empty = app::layoutRichMessage(placeholder, 500, measure);
    require(
        empty.boxes.empty() && empty.plainText.contains(url),
        "Unavailable/unknown previews keep their URL without painting an empty card"
    );
    api::Message legacy = message;
    legacy.embeds = {api::Embed{
        .title = "D (programming language)",
        .description = "An article description",
        .url = url,
        .type = "article",
        .thumbnail = {proxy, 640, 480}
    }};
    const auto article = app::layoutRichMessage(legacy, 500, measure);
    require(
        article.pictures.size() == 1
            && article.pictures[0].width > 300
            && article.pictures[0].y > article.runs.back().y,
        "Legacy article thumbnails render as large media below the summary"
    );
    legacy.embeds[0].type = "rich";
    require(
        app::layoutRichMessage(legacy, 500, measure).pictures[0].width <= 72,
        "Ordinary rich-card accessory thumbnails retain their side-column sizing"
    );
    api::parseMessagePatch(
        Json{
            {"embeds",
             Json::array(
                 {{{"type", "image"},
                   {"image",
                    {{"proxy_url", ""},
                     {"url", "https://cdn.discordapp.com/attachments/1/2/image.png"},
                     {"width", 400},
                     {"height", 300}}}}}
             )}
        }
    ).apply(legacy);
    require(!legacy.embeds[0].image.url.empty(), "An empty proxy field does not discard an allowed original media URL");
    Json controls = Json::parse(R"EMBED([{"type":17,"accent_color":0,"spoiler":true,"components":[
      {"type":9,"components":[{"type":10,"content":"A section with an accessory"}],"accessory":{"type":11,"spoiler":true,"media":{"proxy_url":"https://media.discordapp.net/external/hash/image.png","width":100,"height":100}}},
      {"type":14,"spacing":2},
      {"type":1,"components":[{"type":2,"style":5,"url":"https://example.com/open","label":"Open [literal]","disabled":false},{"type":2,"style":5,"url":"https://example.com/disabled","label":"Disabled","disabled":true},{"type":2,"style":1,"custom_id":"must-not-run","label":"App action"},{"type":2,"style":5,"url":"javascript:alert(1)","label":"Unsafe"}]},
      {"type":12,"items":[{"spoiler":true,"media":{"proxy_url":"https://media.discordapp.net/external/hash/video.mp4","content_type":"video/mp4","width":640,"height":360}}]}
    ]}])EMBED");
    api::Message controlled;
    controlled.embeds = {api::Embed{.components = api::parseEmbedComponents(controls)}};
    const auto control = app::layoutRichMessage(controlled, 320, measure);
    require(
        control.boxes.front().embedAccent && control.boxes.front().color == 0,
        "An explicit black accent is retained independently from an absent accent"
    );
    require(
        control.pictures.size() == 2
            && control.pictures[0].spoiler
            && control.pictures[1].spoiler
            && control.pictures[1].video
            && !control.pictures[1].videoSource.empty(),
        "Spoiler media and native video sources survive component projection"
    );
    require(
        std::ranges::all_of(control.runs, [](const auto& run) { return run.style.spoiler; }),
        "Container spoilers apply to all descendant text and button labels"
    );
    require(
        std::ranges::count_if(control.boxes, [](const auto& box) { return !box.link.empty(); }) == 2
            && std::ranges::any_of(control.boxes, [](const auto& box) { return box.disabled && !box.link.empty(); })
            && !control.plainText.contains("App action")
            && !control.plainText.contains("Unsafe"),
        "Only validated link buttons are rendered; disabled state and plain labels are retained"
    );
    require(control.plainText.contains("Open [literal]"), "Button labels remain literal instead of becoming Markdown");
    auto bounded = controls;
    auto& items = bounded[0]["components"][3]["items"];
    for (int i = 0; i < 20; ++i)
      items.push_back(items[0]);
    controlled.embeds[0].components = api::parseEmbedComponents(bounded);
    require(
        app::layoutRichMessage(controlled, 320, measure).pictures.size() <= 11,
        "Gallery media is bounded across an embed independently of section thumbnails"
    );
    Json many = Json::array();
    for (int i = 0; i < 100; ++i)
      many.push_back({{"type", 10}, {"content", "visible"}});
    require(
        api::parseEmbedComponents(many).size() == 40, "Component traversal stops at the documented per-embed bound"
    );
    require(
        api::parseEmbedComponents(
            Json::array({{{"type", 12}, {"items", Json::array({{{"media", {{"url", "file:///etc/passwd"}}}}})}}})
        ).empty(),
        "Preview media never enables arbitrary filesystem or unproxied origin reads"
    );
    app::MessageLog log;
    require(
        !log.recordEdit(message.channelId, placeholder, message),
        "Automatic link preview population does not create a logged message edit"
    );
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
