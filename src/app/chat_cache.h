#pragma once

#include "api/models.h"

#include <chrono>
#include <cstdint>
#include <span>
#include <string_view>

namespace app {
  // A complete latest page can be reused while the same live session keeps its
  // messages current. Reconnects, a newer known tail and age require a catch-up.
  struct ChatCacheState {
    using Clock = std::chrono::steady_clock;
    Clock::time_point fetched{};
    std::uint64_t epoch = 0;
    bool olderEnd = false;

    bool fresh(
        bool live, std::uint64_t currentEpoch, std::string_view knownLatest, std::span<const api::Message> messages,
        Clock::time_point now = Clock::now()
    ) const {
      if (!live
          || epoch != currentEpoch
          || fetched == Clock::time_point{}
          || now < fetched
          || now - fetched >= std::chrono::minutes(5))
        return false;
      std::string_view tail;
      for (const auto& message : messages)
        if (!(message.flags & 64)
            && (tail.size() < message.id.size() || (tail.size() == message.id.size() && tail < message.id)))
          tail = message.id;
      return knownLatest.empty()
          || (!tail.empty()
              && (knownLatest.size() < tail.size() || (knownLatest.size() == tail.size() && knownLatest <= tail)));
    }
  };
} // namespace app
