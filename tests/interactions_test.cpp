#include "api/actions.h"

#include <iostream>
#include <stdexcept>

namespace {
  void require(bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  }
} // namespace
int main() {
  using namespace api;
  using Kind = InteractionKind;
  using nlohmann::json;
  try {
    const auto reaction =
        interactionRequest({.kind = Kind::AddReaction, .channel = "10", .message = "20", .emoji = "❤️"});
    require(
        reaction
            && reaction->method == "PUT"
            && reaction->path == "/channels/10/messages/20/reactions/%E2%9D%A4%EF%B8%8F/@me"
            && reaction->body.is_null(),
        "Unicode reactions are byte-encoded as a single path component and have no body"
    );
    const auto remove =
        interactionRequest({.kind = Kind::RemoveReaction, .channel = "10", .message = "20", .emoji = "wave:123"});
    require(
        remove && remove->method == "DELETE" && remove->path.ends_with("/wave%3A123/@me"),
        "Custom emoji use the validated name:ID route"
    );
    const auto people = interactionRequest(
        {.kind = Kind::ListReactions, .channel = "10", .message = "20", .emoji = "wave:123", .burst = true}
    );
    require(
        people
            && people->method == "GET"
            && people->body.is_null()
            && people->path == "/channels/10/messages/20/reactions/wave%3A123?limit=100&type=1",
        "Reactor tooltips use a bounded read-only list, with normal/super reactions distinguished"
    );
    require(
        !interactionRequest({.kind = Kind::ListReactions, .channel = "10", .message = "20", .emoji = "../evil"}),
        "Read routes also reject emoji path injection"
    );
    auto reactors = parseReactors(
        json::array(
            {{{"id", "1"}, {"username", "alex"}, {"global_name", "Alex"}},
             {{"id", "2"}, {"username", "maya"}},
             {{"id", "2"}},
             {{"id", "../invalid"}},
             nullptr}
        )
    );
    require(
        reactors.size() == 2 && reactors[0].name == "Alex" && reactors[1].id == "2",
        "Reactor projection deduplicates valid users and ignores malformed entries"
    );
    json many = json::array();
    for (int i = 1; i <= 150; ++i)
      many.push_back({{"id", std::to_string(i)}, {"username", "person"}});
    require(
        parseReactors(many).size() == 100 && parseReactors(json::object()).empty(),
        "Reactor pages stay bounded even with an oversized or malformed response"
    );
    for (const auto* emoji : {"", "x/../../@me", "x:1/2", "x:123:456", "\n", "wave", "x%2f:123"})
      require(
          !interactionRequest({.kind = Kind::AddReaction, .channel = "10", .message = "20", .emoji = emoji}),
          "Reject invalid emoji paths"
      );
    require(
        !interactionRequest({.kind = Kind::DeleteMessage, .channel = "../users", .message = "20"}),
        "Reject malformed resource IDs"
    );
    require(!interactionRequest({.kind = static_cast<Kind>(999)}), "Unrecognized actions must never become requests");
    auto edit =
        interactionRequest({.kind = Kind::EditMessage, .channel = "10", .message = "20", .text = "line one\nline two"});
    require(
        edit
            && edit->method == "PATCH"
            && edit->body["content"] == "line one\nline two"
            && !edit->body.contains("attachments"),
        "Text edits preserve attachments and multiline content"
    );
    require(edit->body["allowed_mentions"]["parse"].empty(), "Editing must not unexpectedly ping users or roles");
    require(
        interactionRequest({.kind = Kind::EditMessage, .channel = "10", .message = "20", .text = ""}).has_value(),
        "Allow clearing the caption of an attached image"
    );
    require(
        !interactionRequest(
            {.kind = Kind::EditMessage, .channel = "10", .message = "20", .text = std::string(2001, 'x')}
        ),
        "Apply message length limits to edits"
    );
    auto block = interactionRequest({.kind = Kind::Block, .user = "7"});
    require(block && block->method == "PUT" && block->body == json{{"type", 2}}, "Blocking uses the relationship API");
    auto unblock = interactionRequest({.kind = Kind::Unblock, .user = "7"});
    require(unblock && unblock->method == "DELETE" && unblock->body.is_null(), "Unblocking uses a bodyless DELETE");
    auto nickname = interactionRequest({.kind = Kind::Nickname, .user = "7", .text = ""});
    require(nickname && nickname->body["nickname"].is_null(), "Clearing a personal nickname sends null");
    require(
        !interactionRequest({.kind = Kind::Nickname, .user = "7", .text = std::string(33, 'x')}),
        "Validate nickname length"
    );
    auto dm = interactionRequest({.kind = Kind::OpenDm, .user = "7"});
    require(
        dm && dm->path == "/users/@me/channels" && dm->body == json{{"recipient_id", "7"}},
        "Opening a DM does not send a message"
    );
    auto profile = interactionRequest({.kind = Kind::UserProfile, .user = "7"});
    require(profile && profile->method == "GET" && profile->body.is_null(), "Profile requests are read-only");
    require(
        profile->path.find("with_mutual_friends_count=true") != std::string::npos
            && profile->path.ends_with("type=popout")
            && profile->path.find("guild_id") == std::string::npos,
        "Global popouts request mutual counts without a server context"
    );
    auto guildProfile = interactionRequest({.kind = Kind::UserProfile, .user = "7", .guild = "10"});
    require(
        guildProfile && guildProfile->path.ends_with("&guild_id=10"),
        "Server popouts request the selected guild profile"
    );
    require(
        !interactionRequest({.kind = Kind::UserProfile, .user = "7", .guild = "1&other=2"}),
        "Profile queries validate guild IDs"
    );
    const auto server = interactionRequest({.kind = Kind::GuildProfile, .guild = "99"});
    const auto join = interactionRequest({.kind = Kind::JoinGuild, .guild = "99"});
    require(
        server && server->method == "GET" && server->path == "/guilds/99/profile" && server->body.is_null(),
        "Opening a tag preview is a read-only profile request"
    );
    require(
        join && join->method == "PUT" && join->path == "/guilds/99/members/@me" && join->body.is_null(),
        "Joining is a separate explicit action, without lurking or unrelated account fields"
    );
    require(
        !interactionRequest({.kind = Kind::GuildProfile, .guild = "../users"})
            && !interactionRequest({.kind = Kind::JoinGuild, .guild = "1?lurker=true"}),
        "Server preview/join routes reject path and query injection"
    );
    auto mute = interactionRequest({.kind = Kind::Mute, .channel = "10"});
    require(
        mute
            && mute->path == "/users/@me/guilds/@me/settings"
            && mute->body.size() == 1
            && mute->body["channel_overrides"].size() == 1
            && mute->body["channel_overrides"]["10"]["muted"] == true,
        "DM mute patches only the selected conversation"
    );
    auto unmute = interactionRequest({.kind = Kind::Unmute, .channel = "10", .guild = "30"});
    require(
        unmute
            && unmute->path == "/users/@me/guilds/30/settings"
            && unmute->body["channel_overrides"]["10"]["muted"] == false,
        "Unmute retains the proper guild scope"
    );
    MessageReference reply{"20", "10", "7", "Friend", "Original"};
    auto body = messageBody("reply", reply);
    require(
        body["message_reference"]["message_id"] == "20"
            && body["message_reference"]["channel_id"] == "10"
            && body["message_reference"]["fail_if_not_exists"] == true
            && body["allowed_mentions"]["replied_user"] == false,
        "Replies target the correct message and channel without an implicit ping"
    );
    reply.mention = true;
    require(
        messageBody("reply", reply)["allowed_mentions"]["replied_user"] == true,
        "Reply mentions require explicit opt-in"
    );
    require(!messageBody("normal").contains("message_reference"), "Ordinary messages have no reference");
    require(
        decodeRestBody(204, "").is_object() && decodeRestBody(205, "").is_object(),
        "Empty successful mutation responses are valid"
    );
    require(
        decodeRestBody(200, "").is_discarded() && decodeRestBody(200, "invalid").is_discarded(),
        "Malformed JSON reads still fail"
    );
    require(
        decodeRestBody(429, R"({"retry_after":1.5})")["retry_after"] == 1.5,
        "Rate-limit JSON remains available to the scheduler"
    );

    auto messages = parseMessages(
        json::array(
            {{{"id", "21"},
              {"channel_id", "10"},
              {"content", "response"},
              {"message_reference", {{"message_id", "20"}, {"channel_id", "10"}}},
              {"referenced_message", {{"content", "Original"}, {"author", {{"id", "7"}, {"global_name", "Friend"}}}}},
              {"reactions", json::array({{{"emoji", {{"id", nullptr}, {"name", "👍"}}}, {"count", 2}, {"me", true}}})}}}
        )
    );
    auto& message = messages.front();
    require(
        message.reply
            && message.reply->authorId == "7"
            && message.reply->content == "Original"
            && message.reactions.size() == 1
            && message.reactions[0].me,
        "Decode received reply previews and own reactions"
    );
    parseMessagePatch(json{{"content", "edited"}}).apply(message);
    require(message.reply && message.reactions.size() == 1, "Unrelated partial edits preserve replies and reactions");
    auto update = parseReactionUpdate(
        "MESSAGE_REACTION_ADD",
        json{{"channel_id", "10"}, {"message_id", "21"}, {"user_id", "9"}, {"emoji", {{"id", nullptr}, {"name", "👍"}}}}
    );
    require(update.has_value(), "Parse live reaction additions");
    const auto super = parseReactionUpdate(
        "MESSAGE_REACTION_ADD",
        json{
            {"channel_id", "10"},
            {"message_id", "21"},
            {"user_id", "9"},
            {"type", 1},
            {"emoji", {{"id", nullptr}, {"name", "👍"}}}
        }
    );
    require(super && super->burst, "Reaction type identifies super reactions when the older burst flag is absent");
    applyReaction(message, *update, "1");
    require(
        message.reactions[0].count == 3 && message.reactions[0].me,
        "Other users' reactions update counts without changing own selection"
    );
    update->kind = ReactionUpdate::Kind::Remove;
    update->user = "1";
    applyReaction(message, *update, "1");
    require(message.reactions[0].count == 2 && !message.reactions[0].me, "Own reaction removals clear selection");
    update->kind = ReactionUpdate::Kind::Add;
    update->burst = true;
    applyReaction(message, *update, "1");
    require(
        message.reactions[0].count == 3
            && message.reactions[0].burstCount == 1
            && message.reactions[0].meBurst
            && !message.reactions[0].me,
        "Super reaction updates do not masquerade as own regular reactions"
    );
    update->kind = ReactionUpdate::Kind::RemoveEmoji;
    applyReaction(message, *update, "1");
    require(message.reactions.empty(), "Removing an emoji clears its entire reaction group");
    parseMessagePatch(json{{"reactions", json::array()}}).apply(message);
    parseMessagePatch(
        json{{"referenced_message", nullptr}, {"message_reference", {{"message_id", "20"}, {"channel_id", "10"}}}}
    ).apply(message);
    require(
        message.reply && message.reply->deleted && message.reactions.empty(),
        "Deleted references and empty reaction arrays are explicit updates"
    );
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
