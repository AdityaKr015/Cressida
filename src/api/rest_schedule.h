#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <stop_token>

namespace api {

  enum class RestCancellation { None, Stopped, SessionChanged, ForegroundReady, Superseded };

  // A background GET may yield to accepted user work, but a mutation must only
  // be cancelled by shutdown or an account/session change.
  struct RestRequestScope {
    std::stop_token stop;
    const std::atomic<std::uint64_t>* session = nullptr;
    std::uint64_t expectedSession = 0;
    const std::atomic<std::uint64_t>* foregroundRevision = nullptr;
    std::uint64_t expectedRevision = 0;
    const std::atomic<std::uint64_t>* authoritativeSession = nullptr;

    RestCancellation cancellation() const noexcept {
      if (stop.stop_requested())
        return RestCancellation::Stopped;
      if (session && session->load() != expectedSession)
        return RestCancellation::SessionChanged;
      if (authoritativeSession && authoritativeSession->load() == expectedSession)
        return RestCancellation::Superseded;
      if (foregroundRevision && foregroundRevision->load() != expectedRevision)
        return RestCancellation::ForegroundReady;
      return RestCancellation::None;
    }
  };

  // Preserve FIFO ordering within each class. In particular, accepted writes
  // never overtake one another when optional bootstrap reads are deferred.
  template <typename Queue, typename Background> auto nextRestWork(Queue& queue, Background background) {
    const auto foreground =
        std::find_if(queue.begin(), queue.end(), [&](const auto& command) { return !background(command); });
    return foreground != queue.end() ? foreground : queue.begin();
  }

} // namespace api
