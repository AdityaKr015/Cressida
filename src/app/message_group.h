#pragma once

#include "api/models.h"
#include "app/layout_metrics.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string_view>

namespace app {
  inline float messageFooterHeight(float components, float reactions, float thread) {
    if (components <= 0 && reactions <= 0 && thread <= 0)
      return 0;
    // Footers are drawn six pixels below the body. Reaction strips reserve four
    // pixels after their final chip; another four keeps the next text clear.
    return layout::Chat::footerGap
        + components
        + reactions
        + thread
        + (reactions > 0 && thread <= 0 ? layout::Chat::reactionTailGap : 0);
  }
  inline float messageRowPadding(
      bool continuation, bool nextContinuation, bool compact,
      int spacing = static_cast<int>(layout::Chat::defaultGroupGap), bool last = false
  ) {
    // Rich text already reserves a full line height. A second row gap would
    // make separate messages looser than wrapping/newlines within one message.
    return (continuation ? 0.0F : layout::Chat::headerHeight)
        + (nextContinuation || last
               ? 0.0F
               : (compact ? std::min(layout::Chat::maximumCompactGroupGap, static_cast<float>(spacing)) : spacing));
  }
  inline std::optional<std::chrono::sys_time<std::chrono::milliseconds>> messageTime(std::string_view value) {
    using namespace std::chrono;
    if (value.size() < 20
        || value.size() > 40
        || value[4] != '-'
        || value[7] != '-'
        || value[10] != 'T'
        || value[13] != ':'
        || value[16] != ':')
      return {};
    const auto number = [&](std::size_t at, std::size_t count) {
      int result = 0;
      for (std::size_t i = at; i < at + count; ++i) {
        if (value[i] < '0' || value[i] > '9')
          return -1;
        result = result * 10 + value[i] - '0';
      }
      return result;
    };
    const int y = number(0, 4), m = number(5, 2), d = number(8, 2), h = number(11, 2), minute = number(14, 2),
              second = number(17, 2);
    const year_month_day date{year{y}, month{static_cast<unsigned>(m)}, day{static_cast<unsigned>(d)}};
    if (y < 1 || !date.ok() || h < 0 || h > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
      return {};
    std::size_t at = 19;
    int fraction = 0, digits = 0;
    if (value[at] == '.') {
      ++at;
      while (at < value.size() && value[at] >= '0' && value[at] <= '9') {
        if (digits < 3)
          fraction = fraction * 10 + value[at] - '0';
        ++digits;
        ++at;
      }
      if (!digits || digits > 9)
        return {};
      for (int i = digits; i < 3; ++i)
        fraction *= 10;
    }
    int zone = 0;
    if (at + 1 == value.size() && value[at] == 'Z') {
    } else if (at + 6 == value.size() && (value[at] == '+' || value[at] == '-') && value[at + 3] == ':') {
      const int zh = number(at + 1, 2), zm = number(at + 4, 2);
      if (zh < 0 || zh > 23 || zm < 0 || zm > 59)
        return {};
      zone = (zh * 60 + zm) * (value[at] == '+' ? 1 : -1);
    } else
      return {};
    return sys_days{date} + hours{h} + minutes{minute - zone} + seconds{second} + milliseconds{fraction};
  }
  // Each message retains its own content, actions and selection. Only the author
  // header and inter-message spacing are shared by a short consecutive run.
  inline bool continuesMessageGroup(const api::Message& previous, const api::Message& current) {
    if (previous.deleted
        || current.deleted
        || previous.authorId.empty()
        || previous.authorId != current.authorId
        || previous.channelId != current.channelId
        || previous.author != current.author
        || previous.avatar != current.avatar
        || previous.webhookId != current.webhookId
        || previous.applicationId != current.applicationId
        || (previous.type != api::defaultMessageType && previous.type != api::replyMessageType)
        || current.type != api::defaultMessageType
        || current.reply
        || previous.forwarded
        || current.forwarded
        || ((previous.flags | current.flags) & 64))
      return false;
    const auto before = messageTime(previous.timestamp), after = messageTime(current.timestamp);
    return before
        && after
        && *after >= *before
        && *after - *before < std::chrono::minutes{7}
    && std::chrono::floor<std::chrono::days>(*before) == std::chrono::floor<std::chrono::days>(*after);
  }
} // namespace app
