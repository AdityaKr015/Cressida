#include "api/rest_schedule.h"

#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
  void require(bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  }

  struct Work {
    std::string name;
    bool background = false;
  };
} // namespace

int main() {
  try {
    std::atomic<std::uint64_t> session{7}, revision{0}, ready{0};
    std::stop_source stop;
    api::RestRequestScope guilds{stop.get_token(), &session, 7, &revision, 0, &ready};
    std::deque<Work> pending{{"guilds-page", true}, {"initial-settings", true}, {"channel-details", true}};
    const auto next = [&] {
      const auto at = api::nextRestWork(pending, [](const auto& command) { return command.background; });
      auto result = std::move(*at);
      pending.erase(at);
      return result;
    };
    auto suspended = next();
    require(suspended.name == "guilds-page", "Bootstrap reads retain their order while no user work is waiting");
    pending.push_back({"channels"});
    pending.push_back({"messages"});
    pending.push_back({"send"});
    ++revision;
    require(
        guilds.cancellation() == api::RestCancellation::ForegroundReady,
        "A slow bootstrap page must yield when visible channel/history work is accepted"
    );
    pending.push_back(std::move(suspended));
    require(
        next().name == "channels" && next().name == "messages" && next().name == "send",
        "Selected-channel work runs before suspended metadata without reordering accepted foreground operations"
    );
    require(
        next().name == "initial-settings"
            && next().name == "channel-details"
            && next().name == "guilds-page"
            && pending.empty(),
        "Suspending a GET must keep exactly one continuation, including optional channel details"
    );
    api::RestRequestScope resumed{stop.get_token(), &session, 7, &revision, revision.load(), &ready};
    require(resumed.cancellation() == api::RestCancellation::None, "A resumed read uses the new queue revision");
    api::RestRequestScope mutation{stop.get_token(), &session, 7};
    ++revision;
    require(
        mutation.cancellation() == api::RestCancellation::None,
        "New visible work must never abort an accepted mutation or cause an uncertain write to be retried"
    );
    ready = 7;
    require(
        resumed.cancellation() == api::RestCancellation::Superseded,
        "A complete Gateway READY supersedes even an already-running fallback guild page"
    );
    session = 8;
    require(
        mutation.cancellation() == api::RestCancellation::SessionChanged
            && resumed.cancellation() == api::RestCancellation::SessionChanged,
        "Changing accounts cancels both reads and writes before another session can consume them"
    );
    stop.request_stop();
    require(mutation.cancellation() == api::RestCancellation::Stopped, "Shutdown terminates any remaining request");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
