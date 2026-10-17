#include "discord/models.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
  void require(bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  }
} // namespace

int main() {
  using nlohmann::json;
  try {
    auto account = discord::parseAccount(json{{"id", "123"}, {"username", "alex"}, {"global_name", nullptr}});
    require(account.name == "alex", "A null global name must fall back to the username");
    require(
        !account.premiumType
            && discord::parseAccount(json{{"id", "123"}, {"premium_type", 2}}).premiumType == 2
            && discord::parseAccount(json{{"id", "123"}, {"premium_type", 0}}).premiumType == 0,
        "Distinguish omitted premium state in partial account updates from explicit non-premium"
    );
    auto channels = discord::parseChannels(
        json::array({
            {{"id", "4"}, {"name", "voice"}, {"type", 2}},
            {{"id", "3"}, {"name", "news"}, {"type", 5}, {"position", 2}},
            {{"id", "2"}, {"name", "general"}, {"type", 0}, {"position", 1}},
            {{"id", "1"}, {"name", "category"}, {"type", 4}},
        })
    );
    require(
        channels.size() == 3 && channels.front().id == "2" && channels.back().type == 4,
        "Filter unsupported channel types and preserve position order"
    );
    auto messages = discord::parseMessages(
        json::array({
            {{"id", "100"},
             {"channel_id", "1"},
             {"author", {{"username", "maya"}, {"global_name", nullptr}}},
             {"content", "hello"},
             {"application_id", nullptr}},
            {{"id", "99"}, {"channel_id", "1"}, {"content", ""}, {"attachments", json::array({{{"id", "1"}}})}},
        })
    );
    require(messages.front().id == "99", "Sort snowflakes numerically across digit boundaries");
    require(
        messages.front().content.empty() && messages.front().attachments.size() == 1,
        "Attachment-only messages retain typed attachments instead of placeholder text"
    );
    require(messages.back().author == "maya", "Optional client fields must not prevent message decoding");
    const auto guildMessages = discord::parseMessages(
        json::array(
            {{{"id", "101"}, {"author", {{"id", "7"}, {"global_name", "Maya"}}}, {"member", {{"nick", "Server Maya"}}}},
             {{"id", "102"}, {"author", {{"id", "7"}, {"global_name", "Maya"}}}, {"member", {{"nick", nullptr}}}}}
        )
    );
    require(
        guildMessages[0].author == "Server Maya" && guildMessages[1].author == "Maya",
        "Message names honor guild nicknames and null fallbacks"
    );
    const auto categorized = discord::parseChannels(
        json::array(
            {{{"id", "10"}, {"type", 4}, {"name", "Chat"}, {"position", 2}},
             {{"id", "11"}, {"type", 0}, {"parent_id", "10"}, {"position", 0}},
             {{"id", "20"}, {"type", 4}, {"name", "Projects"}, {"position", 1}},
             {{"id", "21"}, {"type", 0}, {"parent_id", "20"}, {"position", 9}}}
        )
    );
    require(
        categorized.size() == 4
            && categorized[0].id == "20"
            && categorized[1].id == "21"
            && categorized[2].id == "10"
            && categorized[3].id == "11",
        "Keep channels under their ordered parent categories"
    );
    const auto conversations = discord::parsePrivateChannels(
        json::array(
            {{{"id", "30"},
              {"type", 1},
              {"last_message_id", "99"},
              {"recipients", json::array({{{"id", "7"}, {"username", "maya"}, {"global_name", nullptr}}})}},
             {{"id", "31"},
              {"type", 3},
              {"name", "Studio"},
              {"last_message_id", "100"},
              {"recipients", json::array({{{"id", "8"}, {"username", "alex"}}})}}}
        )
    );
    require(
        conversations.size() == 2
            && conversations[0].name == "Studio"
            && conversations[1].name == "maya"
            && !conversations[1].icon.empty(),
        "Name/avatar DMs and sort by the latest message snowflake"
    );
    const auto deduped = discord::parsePrivateChannels(
        json::array({{{"id", "32"}, {"type", 1}, {"recipient_ids", json::array({"7"})}}}),
        {{"7", {"7", "Maya", "/embed/avatars/0.png"}}}
    );
    require(deduped.front().name == "Maya", "Resolve normalized Gateway DM recipient IDs");
    require(
        conversations[0].named && !conversations[1].named, "Distinguish named groups from recipient-generated titles"
    );
    require(
        discord::validSnowflake("18446744073709551615") && !discord::validSnowflake("1/../../users/@me"),
        "Resource IDs must not become arbitrary REST paths"
    );
    require(!discord::validSnowflake("") && !discord::validSnowflake("123x"), "Reject malformed IDs");
    require(discord::utf8Length("hello 🌿") == 7, "Message lengths must count Unicode characters rather than bytes");
    const auto rich = discord::parseMessages(
        json::array(
            {{{"id", "101"},
              {"content", ""},
              {"attachments",
               json::array(
                   {{{"id", "1"},
                     {"filename", "photo.png"},
                     {"url", "https://cdn.discordapp.com/attachments/1/2/photo.png?ex=a&hm=b"},
                     {"content_type", "image/png"},
                     {"width", 640},
                     {"height", 480}}}
               )},
              {"embeds",
               json::array(
                   {{{"title", "Preview"},
                     {"description", "**Hello**"},
                     {"color", 123},
                     {"fields", json::array({{{"name", "field"}, {"value", "value"}}})},
                     {"thumbnail",
                      {{"proxy_url", "https://media.discordapp.net/external/hash/https/example.com/image.png"}}}}}
               )}}}
        )
    );
    require(
        rich.front().attachments.front().image.width == 640
            && rich.front().embeds.front().fields.front().name == "field",
        "Decode attachments, signed CDN URLs, and rich embed fields"
    );
    require(
        !discord::mediaUrl(
             "https://images-ext-1.discordapp.net/external/hash/https/i.ytimg.com/vi/test/hqdefault.jpg?width=640"
        )
             .empty(),
        "Accept Discord's external image proxy for YouTube thumbnails"
    );
    require(
        discord::mediaUrl("https://images-ext-1.discordapp.net.attacker.test/external/hash").empty(),
        "External proxy origins must match exactly"
    );
    const auto profile = discord::parseAccount(
        json{{"id", "123"}, {"username", "alex"}, {"global_name", "Alex"}, {"bio", "hello"}, {"pronouns", "they/them"}}
    );
    require(
        profile.username == "alex"
            && profile.displayName == "Alex"
            && profile.bio == "hello"
            && profile.pronouns == "they/them",
        "Load editable account/profile fields separately from display labels"
    );
    auto updated = rich.front();
    discord::parseMessagePatch(json{{"embeds", json::array()}}).apply(updated);
    require(
        updated.embeds.empty() && updated.attachments.size() == 1 && updated.content.empty(),
        "Embed-only Gateway updates preserve unrelated fields and allow clearing previews"
    );
    discord::parseMessagePatch(json{{"content", "edited"}}).apply(updated);
    require(
        updated.content == "edited" && updated.attachments.size() == 1, "Content-only updates preserve attachments"
    );
    require(
        discord::mediaUrl("https://cdn.discordapp.com.attacker.test/attachments/a").empty()
            && discord::mediaUrl("https://cdn.discordapp.com@attacker.test/attachments/a").empty()
            && discord::mediaUrl("file:///tmp/image.png").empty(),
        "Automatic media requests stay on fixed credential-free Discord origins"
    );
    discord::Upload image{"paste.png", "image/png", {1, 2, 3}};
    require(discord::validUploads({image}), "Accept bounded pasted images");
    image.filename = "../secret";
    require(!discord::validUploads({image}), "Reject path-like upload names");
    bool rejected = false;
    try {
      (void)discord::parseAccount(json{{"id", "bad"}});
    } catch (const std::exception&) {
      rejected = true;
    }
    require(rejected, "Reject malformed account identifiers");
    rejected = false;
    try {
      (void)discord::parseMessages(json::object());
    } catch (const std::exception&) {
      rejected = true;
    }
    require(rejected, "Reject malformed message responses");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
