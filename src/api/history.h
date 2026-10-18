#pragma once

#include "api/models.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace api {
  inline constexpr std::size_t messagePageSize = 100;
  enum class HistoryDirection { Latest, Older, Newer, Around };
  struct HistoryQuery {
    HistoryDirection direction = HistoryDirection::Latest;
    std::string cursor;
    std::uint64_t request = 0;
    bool operator==(const HistoryQuery&) const = default;
  };
  inline std::string messageHistoryPath(const std::string& channel, const HistoryQuery& query) {
    if (!validSnowflake(channel)
        || (query.direction != HistoryDirection::Latest && !validSnowflake(query.cursor))
        || (query.direction == HistoryDirection::Latest && !query.cursor.empty()))
      return {};
    auto path = "/channels/" + channel + "/messages?limit=" + std::to_string(messagePageSize);
    if (query.direction != HistoryDirection::Latest)
      path += (query.direction == HistoryDirection::Older        ? "&before="
                   : query.direction == HistoryDirection::Around ? "&around="
                                                                 : "&after=")
          + query.cursor;
    return path;
  }
} // namespace api
