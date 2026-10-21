#pragma once
#include "api/models.h"

#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>

namespace api {
  inline constexpr std::size_t inboxPageSize = 25, inboxMaximumSaved = 200, inboxPreviewMessages = 3;
  enum class InboxKind { Mentions, Bookmarks, Reminders, Unread, Save, Remove, Dismiss };
  struct InboxQuery {
    InboxKind kind = InboxKind::Mentions;
    std::string channel, message, guild, before, due;
    bool roles = true, everyone = true;
    std::uint64_t request = 0;
    bool operator==(const InboxQuery&) const = default;
  };
  struct SavedMessage {
    std::string channel, id, guild, savedAt, due, authorSummary, channelSummary, summary;
    std::optional<Message> message;
  };
  struct InboxPage {
    std::vector<Message> messages;
    std::map<std::string, std::string> guilds;
    std::vector<SavedMessage> saved;
    bool more = false;
    std::string before;
  };
  struct InboxHttpRequest {
    std::string path, method = "GET";
    nlohmann::json body;
  };
  std::optional<std::chrono::system_clock::time_point> inboxTime(std::string_view value);
  std::string inboxTimestamp(std::chrono::system_clock::time_point time);
  std::optional<InboxHttpRequest> inboxHttpRequest(const InboxQuery& query);
  bool inboxMissingCollection(const InboxQuery& query, long httpStatus, int serviceCode);
  InboxPage parseInboxPage(const nlohmann::json& value, const InboxQuery& query);
  SavedMessage parseSavedMessage(const nlohmann::json& value);
} // namespace api
