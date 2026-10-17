#include "app/message_history.h"
#include "discord/actions.h"
#include "discord/channel_features.h"
#include "discord/history.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

using namespace discord;
using nlohmann::json;
namespace {
  void require(bool ok, const char* message) {
    if (!ok)
      throw std::runtime_error(message);
  }
  json msg(const char* id, const char* channel) {
    return {
        {"id", id},
        {"channel_id", channel},
        {"author", {{"id", "9"}, {"username", "Support"}, {"bot", true}}},
        {"content", "Help"}
    };
  }
} // namespace
int main() {
  try {
    require(
        permissionBits("18446744073709551616") == 0
            && permissionBits("18446744073709552640") == viewChannel
            && !permissionBits("1024x"),
        "Large permission integers retain their known low bits without truncating raw snapshots"
    );
    require(
        withViewPermission("18446744073709551616", true) == "18446744073709552640"
            && withViewPermission("18446744073709552640", false) == "18446744073709551616",
        "Editing View Channel preserves unknown high permission bits"
    );
    for (unsigned value = 0; value < 4096; ++value) {
      const auto raw = std::to_string(value);
      require(
          withViewPermission(raw, true) == std::to_string(value | 1024)
              && withViewPermission(raw, false) == std::to_string(value & ~1024U),
          "Decimal permission editing must handle carries and borrows"
      );
    }
    Channel channel;
    channel.id = "10";
    parseChannelDetails(
        channel,
        {{"topic", "**Need help?**"},
         {"permission_overwrites",
          json::array(
              {{{"id", "1"}, {"type", 0}, {"allow", "0"}, {"deny", "1024"}},
               {{"id", "2"}, {"type", 0}, {"allow", "1024"}, {"deny", "0"}},
               {{"id", "3"}, {"type", 0}, {"allow", "0"}, {"deny", "1024"}},
               {{"id", "7"}, {"type", 1}, {"allow", "0"}, {"deny", "1024"}}}
          )}}
    );
    require(
        channel.topic == "**Need help?**" && channel.overwrites->size() == 4,
        "Channel topics and permission overrides are projected"
    );
    parseChannelDetails(channel, {{"name", "partial"}});
    require(
        channel.topic == "**Need help?**" && channel.overwrites->size() == 4,
        "Partial channel fields retain prior metadata"
    );
    std::vector<ColoredRole> roles{
        {"1", 0, {}, "@everyone", {}, {}, "1024"},
        {"2", 1, {}, "Members", {}, {}, "0"},
        {"3", 2, {}, "Restricted", {}, {}, "0"},
        {"4", 3, {}, "Admin", {}, {}, "8"}
    };
    require(
        (channelPermissions("1", "8", "", roles, std::vector<std::string>{"2", "3"}, channel).value_or(0) & viewChannel)
            != 0,
        "Role overwrite allows aggregate after role denies"
    );
    require(
        (channelPermissions("1", "7", "", roles, std::vector<std::string>{"2"}, channel).value_or(0) & viewChannel)
            == 0,
        "Member overrides apply after role overrides"
    );
    require(
        channelPermissions("1", "7", "", roles, std::vector<std::string>{"4"}, channel) == ~std::uint64_t{0}
            && channelPermissions("1", "7", "7", {}, {}, channel) == ~std::uint64_t{0},
        "Administrator and owner permissions bypass channel overrides"
    );
    require(
        !channelPermissions("1", "7", "", roles, {}, channel),
        "Unknown member roles cannot invent management permissions"
    );
    const auto access = channelAccessRoles("1", roles, channel);
    require(
        access.size() == 2 && access[0].id == "4" && access[1].id == "2",
        "Private-channel access roles include allowed roles and administrators"
    );
    Channel malformed;
    parseChannelDetails(
        malformed,
        {{"permission_overwrites", json::array({{{"id", "2"}, {"type", 0}, {"allow", "bad"}, {"deny", "0"}}})}}
    );
    require(!malformed.overwrites, "Malformed permissions are unknown, never an empty public overwrite list");
    parseChannelDetails(channel, {{"topic", nullptr}, {"permission_overwrites", json::array()}});
    require(
        channel.topic == "" && channel.overwrites->empty(), "Explicit null and empty channel metadata can clear fields"
    );
    RoleDirectory directory;
    directory.apply(parseRoleUpdates(
        "READY",
        {{"user", {{"id", "7"}}},
         {"guilds",
          json::array(
              {{{"id", "1"},
                {"properties", {{"owner_id", "7"}}},
                {"roles", json::array({{{"id", "1"}, {"permissions", "1024"}}, {{"id", "2"}, {"permissions", "16"}}})},
                {"member", {{"roles", json::array({"2"})}}}}}
          )}}
    ));
    require(
        directory.owner("1") == "7"
            && directory.memberships("1", "7")->at(0) == "2"
            && directory.allRoles("1").size() == 2,
        "READY supplies owner, own memberships and full role permissions"
    );
    auto raw = msg("100", "10");
    raw["components"] = json::array(
        {{{"type", 1},
          {"components",
           json::array(
               {{{"type", 2},
                 {"style", 2},
                 {"label", "Get help"},
                 {"custom_id", "support:open"},
                 {"emoji", {{"name", "🌙"}}}},
                {{"type", 2}, {"style", 5}, {"label", "Docs"}, {"url", "https://example.com"}},
                {{"type", 2}, {"style", 4}, {"label", "Disabled"}, {"custom_id", "disabled"}, {"disabled", true}},
                {{"type", 2}, {"style", 5}, {"label", "Invalid"}, {"url", "javascript:alert(1)"}}}
           )}}}
    );
    raw["flags"] = 64;
    auto message = parseMessages(json::array({raw})).front();
    require(
        message.buttons.size() == 1
            && message.buttons[0].size() == 3
            && message.buttons[0][0].emojiName == "🌙"
            && message.buttons[0][2].disabled
            && message.applicationId == "9"
            && message.flags == 64,
        "Messages retain real component actions, URLs, disabled state, bot identity and ephemeral flags"
    );
    parseMessagePatch({{"content", "Edited"}}).apply(message);
    require(
        message.buttons[0][0].customId == "support:open" && message.applicationId == "9",
        "Partial edits retain buttons and application identity"
    );
    parseMessagePatch({{"components", json::array()}}).apply(message);
    require(message.buttons.empty(), "Explicit empty components clear message buttons");
    raw["content"] = "";
    raw.erase("embeds");
    require(
        parseMessages(json::array({raw})).front().content.empty(),
        "Button-only messages must not gain fake system-message text"
    );
    Interaction button{
        .kind = InteractionKind::MessageButton,
        .channel = "10",
        .message = "100",
        .guild = "1",
        .application = "9",
        .customId = "support:open",
        .nonce = "123",
        .messageFlags = 64
    };
    auto request = interactionRequest(button);
    require(
        request
            && request->path == "/interactions"
            && request->method == "POST"
            && request->body["type"] == 3
            && request->body["data"] == json{{"component_type", 2}, {"custom_id", "support:open"}}
            && !request->body.contains("session_id"),
        "Button requests contain validated public fields; private session identity stays on the REST worker"
    );
    button.application = "../bad";
    require(!interactionRequest(button), "Component actions validate application identity");
    const auto formJson = json::parse(R"JSON({
      "id":"600","channel_id":"10","application":{"id":"9"},"custom_id":"support:details","title":"Support details",
      "components":[
        {"type":1,"id":1,"components":[{"type":4,"id":2,"custom_id":"system","label":"System","style":1,"min_length":2,"max_length":20}]},
        {"type":18,"id":3,"label":"Details","description":"What happened?","component":{"type":4,"id":4,"custom_id":"details","style":2,"required":false}}
      ]})JSON");
    const auto modal = parseApplicationModal(formJson);
    require(
        modal.fields.size() == 2 && modal.fields[1].multiline && !modal.fields[1].required,
        "Native forms project both legacy action rows and current labels without exposing response credentials"
    );
    const std::vector<std::string> values{"Linux", "Line one\nLine two 🌙"};
    const auto submitted = modalSubmission(modal, values);
    require(
        submitted["id"] == "600"
            && submitted["custom_id"] == "support:details"
            && submitted["components"][0]["components"][0]
                == json{{"type", 4}, {"id", 2}, {"custom_id", "system"}, {"value", "Linux"}}
            && submitted["components"][1]["component"]["value"] == values[1],
        "Form submission preserves server IDs, wrapper types, custom identifiers and multiline Unicode values"
    );
    bool rejected = false;
    try {
      (void)modalSubmission(modal, std::vector<std::string>{"", ""});
    } catch (const std::exception&) {
      rejected = true;
    }
    require(rejected, "Required fields validate before sending");
    auto invalidForm = formJson;
    invalidForm["components"][1]["component"]["type"] = 19;
    rejected = false;
    try {
      (void)parseApplicationModal(invalidForm);
    } catch (const std::exception&) {
      rejected = true;
    }
    require(rejected, "Unsupported form controls cannot be silently dropped from a submission");
    auto modalRequest = interactionRequest(
        {.kind = InteractionKind::SubmitModal,
         .channel = "10",
         .guild = "1",
         .application = "9",
         .nonce = "777",
         .modal = modal,
         .values = values}
    );
    require(
        modalRequest
            && modalRequest->path == "/interactions"
            && modalRequest->body["type"] == 5
            && modalRequest->body["data"] == submitted
            && !modalRequest->body.contains("session_id"),
        "Explicit form submissions use validated type-5 interactions; session identity stays backend-owned"
    );
    auto edit =
        interactionRequest({.kind = InteractionKind::EditChannel, .channel = "10", .channelTopic = "New topic"});
    require(
        edit && edit->body == json{{"topic", "New topic"}} && edit->method == "PATCH",
        "Channel edits send only explicitly changed fields"
    );
    require(
        !interactionRequest({.kind = InteractionKind::EditChannel, .channel = "10", .channelName = ""})
            && !interactionRequest({.kind = InteractionKind::GrantChannelAccess, .channel = "10", .user = "../bad"}),
        "Channel mutation routes and names are validated"
    );
    auto grant = interactionRequest({.kind = InteractionKind::GrantChannelAccess, .channel = "10", .user = "2"});
    require(
        grant && grant->path == "/channels/10/permissions/2" && grant->body == json{{"type", 0}},
        "Granting access uses a fixed route and defers merging fresh overwrite bits to the worker"
    );
    ChannelPageQuery search{ChannelPage::Search, "1", "10", "help & 🌙"};
    require(
        channelPagePath(search) == "/guilds/1/messages/search?content=help%20%26%20%F0%9F%8C%99&offset=0",
        "Server search safely encodes content"
    );
    require(
        channelPagePath({ChannelPage::Search, {}, "10", "help"})
            == "/channels/10/messages/search?content=help&offset=0",
        "DM search uses the channel route"
    );
    ChannelPageQuery pins{ChannelPage::Pins, "1", "10", {}, "2026-10-17T09:00:00+00:00"};
    require(
        channelPagePath(pins) == "/channels/10/messages/pins?limit=50&before=2026-10-17T09%3A00%3A00%2B00%3A00",
        "Pinned messages use Discord's current paginated endpoint"
    );
    auto hit = msg("100", "10");
    hit["hit"] = true;
    auto context = msg("99", "10");
    context["hit"] = false;
    auto page =
        parseChannelPage({{"messages", json::array({json::array({context, hit})})}, {"total_results", 80}}, search);
    require(
        page.messages.size() == 1 && page.messages[0].id == "100" && page.more && page.total == 80,
        "Search results project hits instead of duplicate context messages"
    );
    auto pinPage = parseChannelPage(
        {{"items", json::array({{{"pinned_at", "2026-10-16T09:00:00Z"}, {"message", msg("100", "10")}}})},
         {"has_more", true}},
        pins
    );
    require(
        pinPage.messages.size() == 1 && pinPage.more && pinPage.before == "2026-10-16T09:00:00Z",
        "Pin pages project messages and advancing timestamps"
    );
    require(
        messageHistoryPath("10", {HistoryDirection::Around, "100", 1}).ends_with("&around=100"),
        "Message navigation loads native history around the selected result"
    );
    app::MessageHistory history;
    const auto failedAround = history.beginAround("100");
    history.fail(*failedAround, "Temporary failure");
    const auto retryAround = history.begin(HistoryDirection::Around, {}, true);
    require(
        retryAround && retryAround->cursor == "100" && retryAround->request != failedAround->request,
        "Retrying a result jump preserves its exact message cursor and uses a fresh request identity"
    );
    const auto around = history.beginAround("100");
    std::vector<Message> messages{{"1000", "10", "Old cache", "old"}};
    history.apply(
        *around, messages, parseMessages(json::array({msg("99", "10"), msg("100", "10"), msg("101", "10")})), {}
    );
    require(
        messages.size() == 3 && messages[1].id == "100" && !history.atLatest() && !history.olderEnd(),
        "Around-history replaces disconnected cached tails and defers read acknowledgement until reaching the present"
    );
    std::cout << "Channel metadata, permissions, components, routes, search, pins and message navigation passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
