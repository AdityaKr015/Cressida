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
    require(
        app::desktopMessageTitle("Maya", "#🏡-home", "Community") == "Maya (#🏡-home, Community)",
        "Guild notification titles include sender, channel and server"
    );
    require(
        app::desktopMessageTitle("Maya", "Weekend plans") == "Maya (Weekend plans)",
        "Group DM notifications identify the conversation"
    );
    require(app::desktopMessageTitle("Maya") == "Maya", "Individual DMs keep the sender as their title");
    require(
        app::desktopMessageTitle("Maya", {}, "Community") == "Maya (Community)",
        "Unavailable channel metadata does not create an empty channel label"
    );
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

    action = {};
    action.kind = api::InteractionKind::CreateDm;
    action.recipients = {"42", "43"};
    route = api::interactionRequest(action);
    require(
        route
            && route->path == "/users/@me/channels"
            && route->method == "POST"
            && route->body == Json{{"recipients", {"42", "43"}}},
        "Group creation uses exact recipient IDs"
    );
    for (const auto& invalid : std::vector<std::vector<std::string>>{
             {}, {"42", "42"}, {"42/other"}, {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10"}
         }) {
      action.recipients = invalid;
      require(!api::interactionRequest(action), "Invalid, duplicate or excess recipients cannot be submitted");
    }
    const auto group = Json::parse(
        R"({"id":"200","type":3,"recipients":[{"id":"43","username":"bob"},{"id":"42","username":"alice"},{"id":"99","username":"self"}]})"
    );
    require(
        api::createdDm(group, {"42", "43"}, "99").id == "200",
        "Group response validates exact membership, allowing current account"
    );
    const auto rejected = [&](const Json& response, const std::vector<std::string>& wanted) {
      try {
        (void)api::createdDm(response, wanted, "99");
      } catch (const std::exception&) {
        return true;
      }
      return false;
    };
    require(
        rejected(group, {"42"}) && rejected(group, {"42", "44"}) && rejected(group, {"99"}),
        "Wrong channel type, mismatched membership and self recipients are rejected"
    );
    auto duplicate = group;
    duplicate["recipients"].push_back(duplicate["recipients"][0]);
    require(rejected(duplicate, {"42", "43"}), "Duplicate response recipients are rejected");
    action = {};
    action.channel = "100";
    action.notificationFlags = (1ULL << 40) | 512;
    action.kind = api::InteractionKind::PinDm;
    route = api::interactionRequest(action);
    require(
        route
            && route->path == "/users/@me/guilds/@me/settings"
            && route->method == "PATCH"
            && route->body["channel_overrides"]["100"]["flags"] == (action.notificationFlags | 2048ULL),
        "Pin preserves unrelated and unknown notification flags"
    );
    action.kind = api::InteractionKind::UnpinDm;
    action.notificationFlags |= 2048;
    route = api::interactionRequest(action);
    require(
        route && route->body["channel_overrides"]["100"]["flags"] == ((1ULL << 40) | 512),
        "Unpin clears only the private conversation favorite flag"
    );
    action.guild = "200";
    require(!api::interactionRequest(action), "DM pin actions cannot modify a guild override");

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
    auto pins = directory.consume(
        "USER_GUILD_SETTINGS_UPDATE",
        Json::parse(R"({"guild_id":null,"channel_overrides":[{"channel_id":"100","flags":2560,"muted":true}]})")
    );
    require(
        directory.pinnedDm("100") && directory.muted("100"),
        "Remote private-channel pins are projected with notification preferences"
    );
    directory.consume(
        "USER_GUILD_SETTINGS_UPDATE",
        Json::parse(R"({"guild_id":null,"channel_overrides":[{"channel_id":"100","muted":false}]})")
    );
    require(directory.pinnedDm("100") && !directory.muted("100"), "Mute patches preserve pin flags");
    directory.consume(
        "USER_GUILD_SETTINGS_UPDATE",
        Json::parse(R"({"guild_id":null,"channel_overrides":[{"channel_id":"100","flags":0}]})")
    );
    require(!directory.pinnedDm("100"), "Explicit zero flags remove a remote pin");
    api::UserDirectory pinnedLoop;
    pinnedLoop.consume(
        "READY",
        Json::parse(
            R"({"user_guild_settings":{"entries":[{"guild_id":null,"channel_overrides":[{"channel_id":"101","flags":2048}]}]}})"
        )
    );
    require(pinnedLoop.pinnedDm("101"), "Initial READY private override flags restore existing remote pins");
    pinnedLoop.apply(pins);
    require(pinnedLoop.pinnedDm("100"), "Pin flags reach the UI through immutable typed updates");
    pinnedLoop.apply({.reset = true});
    require(!pinnedLoop.pinnedDm("100"), "Account reset clears pin state");
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
    message.content = "<:LUL:1407474360729468989> <a:dance:123> 🎉";
    require(
        app::desktopMessagePreview(message, names) == ":LUL: :dance: 🎉",
        "Desktop custom emoji use readable colon names while Unicode stays intact"
    );
    message.content = "`<:LUL:123>` \\<:LUL:123> ||<a:secret:456>|| <:bad-name:123> <:bad:0>";
    require(
        app::desktopMessagePreview(message, names) == "`<:LUL:123>` \\<:LUL:123> [Spoiler] <:bad-name:123> <:bad:0>",
        "Emoji previews preserve code, escapes, malformed tokens and spoiler privacy"
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
