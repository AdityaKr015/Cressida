#include "api/actions.h"
#include "api/user_directory.h"
#include "app/notification_preview.h"

#include <iostream>
#include <stdexcept>

namespace {
  void require(bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  }
} // namespace
int main() {
  using Json = nlohmann::json;
  try {
    auto channels = api::parsePrivateChannels(Json::parse(R"([
      {"id":"100","type":1,"is_message_request":true,"is_spam":false,"is_message_request_timestamp":"2026-10-18T10:30:00Z","recipients":[{"id":"42","username":"alice"}]},
      {"id":"101","type":1,"is_spam":true},
      {"id":"102","type":1,"is_message_request":false}
    ])"));
    auto& request = channels[0];
    require(
        request.id == "100" && request.isMessageRequest() && request.messageRequestTimestamp == "2026-10-18T10:30:00Z",
        "Request status and timestamp survive private channel parsing"
    );
    require(
        channels[1].isMessageRequest() && !channels[2].isMessageRequest(),
        "Spam is separate from ordinary accepted conversations"
    );
    auto partial = api::parsePrivateChannels(Json::array({{{"id", "100"}, {"last_message_id", "999"}}})).front();
    api::mergePrivateChannel(request, std::move(partial));
    require(
        request.isMessageRequest()
            && request.recipients.size() == 1
            && request.name == "alice"
            && request.lastMessageId == "999",
        "Partial live updates preserve consent and sender identity"
    );
    auto accepted = api::parsePrivateChannels(
                        Json::array(
                            {{{"id", "100"},
                              {"is_message_request", false},
                              {"is_spam", false},
                              {"is_message_request_timestamp", nullptr}}}
                        )
    ).front();
    api::mergePrivateChannel(request, std::move(accepted));
    require(
        !request.isMessageRequest() && request.messageRequestTimestamp == "" && request.recipients.front().id == "42",
        "Explicit acceptance clears request state without losing the recipient"
    );
    api::Interaction action;
    action.kind = api::InteractionKind::AcceptMessageRequest;
    action.channel = "100";
    auto route = api::interactionRequest(action);
    require(
        route
            && route->method == "PUT"
            && route->path == "/channels/100/recipients/@me"
            && route->body == Json{{"consent_status", 2}},
        "Accept uses the supported consent status and fixed recipient route"
    );
    action.kind = api::InteractionKind::IgnoreMessageRequest;
    route = api::interactionRequest(action);
    require(
        route && route->method == "DELETE" && route->path == "/channels/100/recipients/@me" && route->body.is_null(),
        "Ignore uses the dedicated request deletion route"
    );
    action.channel = "100/other";
    require(!api::interactionRequest(action), "Message request actions reject injected identifiers");

    api::UserDirectory directory;
    const auto ready = directory.consume("READY", Json::parse(R"({
      "users":[{"id":"42","username":"alice","global_name":"Alice"}],
      "relationships":[{"id":"42","type":1,"nickname":"My friend"}]
    })"));
    require(
        directory.relatedUsers().size() == 1
            && directory.relatedUsers().front().name == "Alice"
            && directory.name("42", "Alice") == "My friend",
        "Friends can render immediately from READY users and relationships"
    );
    api::UserDirectory owningLoop;
    owningLoop.apply(ready);
    require(
        owningLoop.relatedUsers().front().username == "alice",
        "Typed updates deliver friend identities to the owning UI loop"
    );
    owningLoop.apply(directory.consume(
        "RELATIONSHIP_ADD", Json::parse(R"({"id":"43","type":3,"user":{"id":"43","username":"bob"}})")
    ));
    require(
        owningLoop.relatedUsers().size() == 2 && owningLoop.relationship("43") == 3,
        "Incoming friend requests appear from live relationship events"
    );
    owningLoop.apply(directory.consume("RELATIONSHIP_REMOVE", Json{{"id", "43"}}));
    require(owningLoop.relatedUsers().size() == 1, "Removed requests leave the friend list immediately");
    owningLoop.apply(directory.consume("READY", Json::object()));
    require(owningLoop.relatedUsers().empty(), "Account reset does not retain friend identities");

    api::Message message;
    message.authorId = "42";
    message.author = "Alice";
    message.mentions = {{"43", "Bob"}};
    const app::NotificationName names = [](char kind, const std::string& id) -> std::string {
      if (kind == '&' && id == "87")
        return "Event alerts";
      if (kind == '#' && id == "88")
        return "general";
      if (kind == '@' && id == "42")
        return "Alice";
      if (kind == '@' && id == "43")
        return "Bob <Admin>";
      return "";
    };
    message.content = "<@&87> Hi <@43> and <@!43>, visit <#88>.";
    require(
        app::desktopMessagePreview(message, names) == "@Event alerts Hi @Bob <Admin> and @Bob <Admin>, visit #general.",
        "Desktop previews resolve role, user, nickname and channel tokens as plain text"
    );
    message.content = "<@&99> <@99> <#99>";
    require(
        app::desktopMessagePreview(message, names) == "@unknown-role @unknown-user #unknown-channel",
        "Unknown notification mentions never expose raw protocol IDs"
    );
    message.content = "`<@43>` \\<@43> ||<@43>|| <@43>";
    require(
        app::desktopMessagePreview(message, names) == "`<@43>` \\<@43> [Spoiler] @Bob <Admin>",
        "Notification mentions preserve code, escapes and spoiler privacy"
    );
    message.type = 1;
    require(
        app::desktopMessagePreview(message, names) == "Alice added Bob <Admin>.",
        "Member-added notifications use readable usernames instead of raw mention bodies"
    );
    message.type = 2;
    require(
        app::desktopMessagePreview(message, names) == "Alice removed Bob <Admin>.",
        "Member removal notifications use readable names"
    );
    message.type = 7;
    message.content.clear();
    require(
        app::desktopMessagePreview(message, names).contains("Alice"), "Welcome notifications name the joining member"
    );
    std::cout << "Direct message requests, live friend identities and notification names passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
