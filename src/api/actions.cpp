#include "api/actions.h"

#include "api/channel_features.h"

#include <algorithm>
#include <cctype>

namespace api {
  nlohmann::json decodeRestBody(long status, std::string_view bytes) {
    if (status == 204 || status == 205)
      return nlohmann::json::object();
    return nlohmann::json::parse(bytes, nullptr, false);
  }
  namespace {
    std::string encode(std::string_view value) {
      constexpr char hex[] = "0123456789ABCDEF";
      std::string result;
      for (unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z')
            || (ch >= 'A' && ch <= 'Z')
            || (ch >= '0' && ch <= '9')
            || ch == '-'
            || ch == '_'
            || ch == '.'
            || ch == '~')
          result += ch;
        else {
          result += '%';
          result += hex[ch >> 4];
          result += hex[ch & 15];
        }
      }
      return result;
    }
    bool validEmoji(const std::string& value) {
      if (value.empty() || value.size() > 160)
        return false;
      if (const auto colon = value.find(':'); colon != std::string::npos)
        return colon > 0
            && colon <= 64
            && validSnowflake(value.substr(colon + 1))
            && std::ranges::all_of(value.substr(0, colon), [](unsigned char c) { return std::isalnum(c) || c == '_'; });
      return std::ranges::any_of(value, [](unsigned char c) { return c >= 128; })
          && std::ranges::all_of(value, [](unsigned char c) {
               return c >= 128 || (c >= '0' && c <= '9') || c == '#' || c == '*';
             });
    }
  } // namespace
  std::string reactionKey(const Reaction& reaction) {
    return reaction.id.empty() ? reaction.name : reaction.name + ":" + reaction.id;
  }
  std::vector<Account> parseReactors(const nlohmann::json& value) {
    std::vector<Account> result;
    if (!value.is_array())
      return result;
    for (const auto& item : value) {
      if (result.size() == 100)
        break;
      if (!item.is_object()
          || !item.contains("id")
          || !item["id"].is_string()
          || !validSnowflake(item["id"].get<std::string>()))
        continue;
      auto account = parseAccount(item);
      if (validSnowflake(account.id)
          && std::ranges::none_of(result, [&](const auto& old) { return old.id == account.id; }))
        result.push_back(std::move(account));
    }
    return result;
  }
  nlohmann::json messageBody(const std::string& content, const std::optional<MessageReference>& reply) {
    nlohmann::json result{
        {"content", content},
        {"allowed_mentions", {{"parse", nlohmann::json::array()}, {"replied_user", reply && reply->mention}}}
    };
    if (reply && validSnowflake(reply->message) && validSnowflake(reply->channel))
      result["message_reference"] = {
          {"message_id", reply->message}, {"channel_id", reply->channel}, {"fail_if_not_exists", true}
      };
    return result;
  }
  std::optional<InteractionRequest> interactionRequest(const Interaction& action) {
    using Kind = InteractionKind;
    InteractionRequest request;
    switch (action.kind) {
    case Kind::SubmitModal:
      if (!action.modal
          || action.application != action.modal->application
          || action.channel != action.modal->channel
          || !validSnowflake(action.nonce)
          || (!action.guild.empty() && !validSnowflake(action.guild)))
        return {};
      try {
        request.body = {
            {"type", 5},
            {"nonce", action.nonce},
            {"channel_id", action.channel},
            {"application_id", action.application},
            {"data", modalSubmission(*action.modal, action.values)}
        };
      } catch (const std::exception&) {
        return {};
      }
      request.path = "/interactions";
      request.method = "POST";
      if (!action.guild.empty())
        request.body["guild_id"] = action.guild;
      break;
    case Kind::MessageButton:
      if (!validSnowflake(action.channel)
          || !validSnowflake(action.message)
          || !validSnowflake(action.application)
          || !validSnowflake(action.nonce)
          || (!action.guild.empty() && !validSnowflake(action.guild))
          || action.customId.empty()
          || action.customId.size() > 400
          || utf8Length(action.customId) > 100)
        return {};
      request.path = "/interactions";
      request.method = "POST";
      request.body = {
          {"type", 3},
          {"nonce", action.nonce},
          {"channel_id", action.channel},
          {"message_id", action.message},
          {"message_flags", action.messageFlags},
          {"application_id", action.application},
          {"data", {{"component_type", 2}, {"custom_id", action.customId}}}
      };
      if (!action.guild.empty())
        request.body["guild_id"] = action.guild;
      break;
    case Kind::EditChannel:
      if (!validSnowflake(action.channel) || (!action.channelName && !action.channelTopic))
        return {};
      if (action.channelName
          && (action.channelName->empty()
              || utf8Length(*action.channelName) > 100
              || action.channelName->size() > 400
              || std::ranges::any_of(*action.channelName, [](unsigned char c) { return c < 32 || c == 127; })))
        return {};
      if (action.channelTopic && (utf8Length(*action.channelTopic) > 4096 || action.channelTopic->size() > 16384))
        return {};
      request.path = "/channels/" + action.channel;
      request.method = "PATCH";
      request.body = nlohmann::json::object();
      if (action.channelName)
        request.body["name"] = *action.channelName;
      if (action.channelTopic)
        request.body["topic"] = *action.channelTopic;
      break;
    case Kind::GrantChannelAccess:
      if (!validSnowflake(action.channel)
          || !validSnowflake(action.user)
          || (action.overwriteType != 0 && action.overwriteType != 1))
        return {};
      request.path = "/channels/" + action.channel + "/permissions/" + action.user;
      request.method = "PUT";
      request.body = {{"type", action.overwriteType}}; // Fresh overwrite bits are merged on the REST worker.
      break;
    case Kind::CreateThread:
    case Kind::CreateForumPost:
      if (!validSnowflake(action.channel)
          || (!action.message.empty() && !validSnowflake(action.message))
          || action.text.empty()
          || utf8Length(action.text) > 100
          || action.text.size() > 400
          || std::ranges::all_of(action.text, [](unsigned char c) { return c <= 32; })
          || std::ranges::any_of(action.text, [](unsigned char c) { return c < 32 || c == 127; }))
        return {};
      request.path =
          "/channels/" + action.channel + (action.message.empty() ? "" : "/messages/" + action.message) + "/threads";
      request.method = "POST";
      request.body = {{"name", action.text}, {"auto_archive_duration", 1440}};
      if (action.message.empty())
        request.body["type"] = 11;
      if (action.kind == Kind::CreateForumPost) {
        if (!action.message.empty()
            || action.content.empty()
            || action.content.size() > 8000
            || utf8Length(action.content) > 2000
            || action.tags.size() > 5
            || std::ranges::any_of(action.tags, [](const auto& tag) { return !validSnowflake(tag); }))
          return {};
        request.body["message"] = messageBody(action.content);
        request.body["applied_tags"] = action.tags;
      }
      break;
    case Kind::JoinThread:
    case Kind::LeaveThread:
    case Kind::UnarchiveThread:
      if (!validSnowflake(action.channel))
        return {};
      request.path = "/channels/" + action.channel;
      if (action.kind == Kind::UnarchiveThread) {
        request.method = "PATCH";
        request.body = {{"archived", false}};
      } else {
        request.path += "/thread-members/@me";
        request.method = action.kind == Kind::JoinThread ? "PUT" : "DELETE";
      }
      break;
    case Kind::GuildProfile:
    case Kind::JoinGuild:
      if (!validSnowflake(action.guild))
        return {};
      request.path = "/guilds/" + action.guild + (action.kind == Kind::GuildProfile ? "/profile" : "/members/@me");
      request.method = action.kind == Kind::GuildProfile ? "GET" : "PUT";
      break;
    case Kind::AddReaction:
    case Kind::RemoveReaction:
    case Kind::ListReactions:
    case Kind::EditMessage:
    case Kind::DeleteMessage:
      if (!validSnowflake(action.channel) || !validSnowflake(action.message))
        return {};
      request.path = "/channels/" + action.channel + "/messages/" + action.message;
      if (action.kind == Kind::AddReaction
          || action.kind == Kind::RemoveReaction
          || action.kind == Kind::ListReactions) {
        if (!validEmoji(action.emoji))
          return {};
        request.path += "/reactions/" + encode(action.emoji);
        if (action.kind == Kind::ListReactions) {
          request.path += std::string("?limit=100&type=") + (action.burst ? "1" : "0");
          request.method = "GET";
        } else {
          request.path += "/@me";
          request.method = action.kind == Kind::AddReaction ? "PUT" : "DELETE";
        }
      } else if (action.kind == Kind::EditMessage) {
        if (action.text.size() > 8000 || utf8Length(action.text) > 2000)
          return {};
        request.method = "PATCH";
        request.body = messageBody(action.text);
      } else
        request.method = "DELETE";
      break;
    case Kind::Block:
    case Kind::Unblock:
    case Kind::AddFriend:
    case Kind::RemoveFriend:
    case Kind::Nickname:
    case Kind::OpenDm:
    case Kind::UserProfile:
      if (!validSnowflake(action.user))
        return {};
      if (action.kind == Kind::OpenDm) {
        request.path = "/users/@me/channels";
        request.method = "POST";
        request.body = {{"recipient_id", action.user}};
      } else if (action.kind == Kind::UserProfile) {
        if (!action.guild.empty() && !validSnowflake(action.guild))
          return {};
        request.path = "/users/"
            + action.user
            + "/profile?with_mutual_guilds=true&with_mutual_friends=true&with_mutual_friends_count=true&type=popout";
        if (!action.guild.empty())
          request.path += "&guild_id=" + action.guild;
        request.method = "GET";
      } else {
        request.path = "/users/@me/relationships/" + action.user;
        if (action.kind == Kind::Nickname) {
          if (utf8Length(action.text) > 32 || action.text.size() > 128)
            return {};
          request.method = "PATCH";
          request.body = {{"nickname", action.text.empty() ? nlohmann::json(nullptr) : nlohmann::json(action.text)}};
        } else if (action.kind == Kind::Block || action.kind == Kind::AddFriend) {
          request.method = "PUT";
          request.body = {{"type", action.kind == Kind::Block ? 2 : 1}};
        } else
          request.method = "DELETE";
      }
      break;
    case Kind::Mute:
    case Kind::Unmute:
      if (!validSnowflake(action.channel) || (!action.guild.empty() && !validSnowflake(action.guild)))
        return {};
      request.path = "/users/@me/guilds/" + (action.guild.empty() ? "@me" : action.guild) + "/settings";
      request.method = "PATCH";
      request.body = {
          {"channel_overrides",
           {{action.channel,
             {{"muted", action.kind == Kind::Mute},
              {"mute_config", {{"end_time", nullptr}, {"selected_time_window", -1}}}}}}}
      };
      break;
    default:
      return {};
    }
    return request;
  }
} // namespace api
