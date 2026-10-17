#include "app/message_history.h"
#include "app/message_mentions.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

int main() {
  using nlohmann::json;
  const auto require = [](bool ok, const char* message) {
    if (!ok)
      throw std::runtime_error(message);
  };
  try {
    const std::vector<std::string> roles{"50", "51"};
    auto message = discord::parseMessages(json::parse(R"JSON([
      {"id":"100","channel_id":"10","author":{"id":"2","username":"Maya"},
       "content":"<@1> please check this","mentions":[{"id":"1","username":"Alex"}]}])JSON"))
                       .front();
    require(app::messageMentions(message, "1", roles), "A resolved direct mention highlights its message");
    require(
        !app::messageMentions(message, "3", roles) && !app::messageMentions(message, "", roles),
        "Mentions of other people and unknown accounts do not highlight"
    );
    discord::parseMessagePatch({{"content", "Updated text"}}).apply(message);
    require(app::messageMentions(message, "1"), "Partial edits retain resolved mentions");
    discord::parseMessagePatch({{"mentions", json::array()}}).apply(message);
    require(!app::messageMentions(message, "1"), "Explicit empty mentions remove the highlight");
    message.content = "`<@1> @everyone <@&50>`";
    require(!app::messageMentions(message, "1", roles), "Literal mention-looking text never creates a highlight");
    discord::parseMessagePatch({{"mention_roles", json::array({"50", "99", "../bad", 5})}}).apply(message);
    require(
        message.mentionRoles.size() == 2
            && app::messageMentions(message, "1", roles)
            && !app::messageMentions(message, "1", std::vector<std::string>{"90"})
            && !app::messageMentions(message, "1"),
        "Role mentions highlight only members of the resolved roles; malformed IDs and unknown membership do not match"
    );
    discord::parseMessagePatch({{"mention_roles", json::array()}, {"mention_everyone", true}}).apply(message);
    require(app::messageMentions(message, "1"), "The server's everyone/here flag highlights guild-wide mentions");
    discord::parseMessagePatch({{"embeds", json::array()}}).apply(message);
    require(message.mentionEveryone && app::messageMentions(message, "1"), "Embed-only updates retain mention state");
    discord::parseMessagePatch({{"mention_everyone", false}}).apply(message);
    require(
        !message.mentionEveryone && !app::messageMentions(message, "1", roles),
        "An explicit false clears a guild-wide highlight"
    );
    app::MessageHistory history;
    std::vector<discord::Message> messages;
    const auto request = history.begin(discord::HistoryDirection::Latest, messages);
    history.changed(
        "100", discord::parseMessagePatch({{"mention_everyone", true}, {"mention_roles", json::array({"51"})}})
    );
    history.changed(
        "100",
        discord::parseMessagePatch({{"content", "A later partial edit"}, {"edited_timestamp", "2026-10-17T22:00:00Z"}})
    );
    history.apply(*request, messages, {message}, {});
    require(
        messages.size() == 1
            && messages[0].mentionEveryone
            && messages[0].mentionRoles == std::vector<std::string>{"51"},
        "Live mention updates win over in-flight history without being lost to later partial edits"
    );
    require(messages[0].editedTimestamp == "2026-10-17T22:00:00Z", "In-flight history retains a live edit's timestamp");
    std::cout << "Mention projection, matching and live history patches passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
