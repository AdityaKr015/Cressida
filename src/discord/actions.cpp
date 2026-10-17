#include "discord/actions.h"

#include <algorithm>
#include <cctype>

namespace discord {
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
        request.path = "/users/" + action.user + "/profile";
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
} // namespace discord
