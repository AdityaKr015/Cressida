#include "api/inbox.h"

#include <algorithm>
#include <glib.h>
#include <set>
#include <stdexcept>

namespace api {
  namespace {
    using Json = nlohmann::json;
    constexpr std::size_t maximumTimestampBytes = 40;
    constexpr int unknownSavedMessages = 10133;
    std::string text(const Json& value, const char* key) {
      const auto at = value.find(key);
      return at != value.end() && at->is_string() ? at->get<std::string>() : "";
    }
    std::string encoded(std::string_view value) {
      auto* escaped = g_uri_escape_string(std::string(value).c_str(), nullptr, false);
      std::string result = escaped ? escaped : "";
      g_free(escaped);
      return result;
    }
  } // namespace
  std::optional<std::chrono::system_clock::time_point> inboxTime(std::string_view value) {
    if (value.empty() || value.size() > maximumTimestampBytes || value.find('\0') != std::string_view::npos)
      return {};
    auto* date = g_date_time_new_from_iso8601(std::string(value).c_str(), nullptr);
    if (!date)
      return {};
    const auto seconds = g_date_time_to_unix(date);
    g_date_time_unref(date);
    return std::chrono::system_clock::time_point{std::chrono::seconds{seconds}};
  }
  std::string inboxTimestamp(std::chrono::system_clock::time_point time) {
    auto* date = g_date_time_new_from_unix_utc(
        std::chrono::duration_cast<std::chrono::seconds>(time.time_since_epoch()).count()
    );
    auto* formatted = date ? g_date_time_format_iso8601(date) : nullptr;
    std::string result = formatted ? formatted : "";
    g_free(formatted);
    if (date)
      g_date_time_unref(date);
    return result;
  }
  std::optional<InboxHttpRequest> inboxHttpRequest(const InboxQuery& q) {
    const auto saved = "/users/@me/saved-messages";
    switch (q.kind) {
    case InboxKind::Mentions: {
      if ((!q.guild.empty() && !validSnowflake(q.guild)) || (!q.before.empty() && !validSnowflake(q.before)))
        return {};
      auto path = "/users/@me/mentions?limit="
          + std::to_string(inboxPageSize)
          + "&roles="
          + (q.roles ? "true" : "false")
          + "&everyone="
          + (q.everyone ? "true" : "false");
      if (!q.guild.empty())
        path += "&guild_id=" + q.guild;
      if (!q.before.empty())
        path += "&before=" + q.before;
      return InboxHttpRequest{path};
    }
    case InboxKind::Bookmarks:
      if (!q.before.empty() && !inboxTime(q.before))
        return {};
      return InboxHttpRequest{
          std::string(saved)
          + "/bookmarks?limit="
          + std::to_string(inboxPageSize)
          + (q.before.empty() ? "" : "&before=" + encoded(q.before))
      };
    case InboxKind::Reminders:
      return InboxHttpRequest{std::string(saved) + "?bookmark_ids=true"};
    case InboxKind::Unread:
      if (!validSnowflake(q.channel) || (!q.before.empty() && !validSnowflake(q.before)))
        return {};
      return InboxHttpRequest{
          "/channels/"
          + q.channel
          + "/messages?limit="
          + std::to_string(inboxPreviewMessages)
          + (q.before.empty() ? "" : "&after=" + q.before)
      };
    case InboxKind::Save:
    case InboxKind::Remove:
      if (!validSnowflake(q.channel) || !validSnowflake(q.message) || (!q.due.empty() && !inboxTime(q.due)))
        return {};
      return InboxHttpRequest{
          std::string(saved) + "/" + q.channel + "/" + q.message, q.kind == InboxKind::Save ? "PUT" : "DELETE",
          q.kind == InboxKind::Save ? Json{{"due_at", q.due.empty() ? Json(nullptr) : Json(q.due)}} : Json{}
      };
    case InboxKind::Dismiss:
      if (!validSnowflake(q.message))
        return {};
      return InboxHttpRequest{"/users/@me/mentions/" + q.message, "DELETE"};
    }
    return {};
  }

  bool inboxMissingCollection(const InboxQuery& query, long httpStatus, int serviceCode) {
    return httpStatus == 404
        && serviceCode == unknownSavedMessages
        && query.before.empty()
        && (query.kind == InboxKind::Bookmarks || query.kind == InboxKind::Reminders);
  }
  SavedMessage parseSavedMessage(const Json& value) {
    if (!value.is_object() || !value.contains("save_data") || !value["save_data"].is_object())
      throw std::runtime_error("The saved message response is invalid.");
    const auto& data = value["save_data"];
    SavedMessage result{text(data, "channel_id"),      text(data, "message_id"),     text(data, "guild_id"),
                        text(data, "saved_at"),        text(data, "due_at"),         text(data, "author_summary"),
                        text(data, "channel_summary"), text(data, "message_summary")};
    if (!validSnowflake(result.channel)
        || !validSnowflake(result.id)
        || (!result.guild.empty() && result.guild != "0" && !validSnowflake(result.guild))
        || (!result.due.empty() && !inboxTime(result.due)))
      throw std::runtime_error("The saved message identity is invalid.");
    if (result.guild == "0")
      result.guild.clear();
    if (const auto at = value.find("message"); at != value.end() && at->is_object()) {
      auto parsed = parseMessages(Json::array({*at}));
      if (parsed.size() != 1 || parsed.front().id != result.id || parsed.front().channelId != result.channel)
        throw std::runtime_error("The saved message identity does not match its content.");
      result.message = std::move(parsed.front());
    }
    return result;
  }
  InboxPage parseInboxPage(const Json& value, const InboxQuery& q) {
    InboxPage result;
    if (q.kind == InboxKind::Save) {
      result.saved.push_back(parseSavedMessage(value));
      if (result.saved.front().channel != q.channel || result.saved.front().id != q.message)
        throw std::runtime_error("The saved message response targets another message.");
      return result;
    }
    if (q.kind == InboxKind::Remove || q.kind == InboxKind::Dismiss)
      return result;
    if (q.kind == InboxKind::Mentions || q.kind == InboxKind::Unread) {
      const auto limit = q.kind == InboxKind::Mentions ? inboxPageSize : inboxPreviewMessages;
      if (!value.is_array() || value.size() > limit)
        throw std::runtime_error("The inbox message page is invalid.");
      result.messages = parseMessages(value);
      for (const auto& message : result.messages)
        if (!validSnowflake(message.id)
            || !validSnowflake(message.channelId)
            || (q.kind == InboxKind::Unread && message.channelId != q.channel))
          throw std::runtime_error("The inbox message identity is invalid.");
      result.more = q.kind == InboxKind::Mentions && result.messages.size() == inboxPageSize;
      if (!result.messages.empty())

        result.before =
            std::ranges::min(
                result.messages,
                [](const auto& a, const auto& b) { return a.size() != b.size() ? a.size() < b.size() : a < b; },
                &Message::id
            )
                .id;
      for (const auto& message : value) {
        const auto guild = text(message, "guild_id");
        if (validSnowflake(guild))
          result.guilds[text(message, "channel_id")] = guild;
      }
      if (q.kind == InboxKind::Mentions)
        std::ranges::reverse(result.messages);
      return result;
    }
    const char* key = q.kind == InboxKind::Reminders && value.contains("reminders") ? "reminders" : "results";
    if (!value.is_object() || !value.contains(key) || !value[key].is_array() || value[key].size() > inboxMaximumSaved)
      throw std::runtime_error("The saved message page is invalid.");
    std::set<std::pair<std::string, std::string>> seen;
    for (const auto& item : value[key]) {
      auto saved = parseSavedMessage(item);
      if (seen.emplace(saved.channel, saved.id).second && ((q.kind == InboxKind::Reminders) == !saved.due.empty()))
        result.saved.push_back(std::move(saved));
    }
    result.more = q.kind == InboxKind::Bookmarks && value.value("has_more", false);
    if (!result.saved.empty())
      result.before = result.saved.back().savedAt;
    if (result.more && (!inboxTime(result.before) || result.before == q.before))
      throw std::runtime_error("The bookmark page has no usable continuation.");
    return result;
  }
} // namespace api
