#include "api/assets.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
  constexpr auto timeout = std::chrono::seconds(5);

  void require(bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  }

  std::string path(int value) { return "/media/v1/collectibles-shop/" + std::to_string(value) + "/static"; }

  struct Call {
    std::string path;
    bool videoPoster = false;
    std::stop_token stop;
    bool released = false;
    unsigned char value = 0;
    std::weak_ptr<const media::Animation> animation;
  };

  class LoaderFixture {
  public:
    explicit LoaderFixture(bool ignoreCancellation = false) : m_ignoreCancellation(ignoreCancellation) {}

    api::Asset load(const std::string& path, bool videoPoster, std::stop_token stop) {
      auto call = std::make_shared<Call>();
      call->path = path;
      call->videoPoster = videoPoster;
      call->stop = stop;
      std::stop_callback canceled(stop, [&] {
        std::lock_guard lock(m_mutex);
        m_ready.notify_all();
      });
      std::unique_lock lock(m_mutex);
      call->value = m_calls.size() + 1;
      m_calls.push_back(call);
      m_ready.notify_all();
      m_ready.wait(lock, [&] {
        return m_releaseAll || call->released || (!m_ignoreCancellation && stop.stop_requested());
      });
      auto animation = std::make_shared<media::Animation>();
      animation->frames.resize(2);
      animation->frames.front().rgba.resize(64);
      call->animation = animation;
      return {path, {call->value}, std::move(animation)};
    }

    std::shared_ptr<Call> wait(const std::string& path, std::size_t occurrence = 1) {
      std::unique_lock lock(m_mutex);
      const auto find = [&]() -> std::shared_ptr<Call> {
        std::size_t count = 0;
        for (const auto& call : m_calls)
          if (call->path == path && ++count == occurrence)
            return call;
        return {};
      };
      require(m_ready.wait_for(lock, timeout, [&] { return bool(find()); }), "The requested worker call starts");
      return find();
    }

    std::size_t count(const std::string& path) {
      std::lock_guard lock(m_mutex);
      return std::ranges::count_if(m_calls, [&](const auto& call) { return call->path == path; });
    }

    bool animationAlive(const std::shared_ptr<Call>& call) {
      std::lock_guard lock(m_mutex);
      return !call->animation.expired();
    }

    void release(const std::shared_ptr<Call>& call) {
      std::lock_guard lock(m_mutex);
      call->released = true;
      m_ready.notify_all();
    }

    void releaseAll() {
      std::lock_guard lock(m_mutex);
      m_releaseAll = true;
      m_ready.notify_all();
    }

  private:
    std::mutex m_mutex;
    std::condition_variable m_ready;
    std::vector<std::shared_ptr<Call>> m_calls;
    bool m_ignoreCancellation = false, m_releaseAll = false;
  };

  struct ReleaseOnExit {
    LoaderFixture& fixture;
    ~ReleaseOnExit() { fixture.releaseAll(); }
  };

  void waitResult(api::Assets& assets) {
    pollfd ready{assets.eventFd(), POLLIN, 0};
    require(
        poll(&ready, 1, std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count()) == 1,
        "A worker publishes its completed result"
    );
  }

  void testQueuedCancellation() {
    LoaderFixture fixture;
    api::Assets assets([&](const auto& path, bool poster, auto stop) { return fixture.load(path, poster, stop); });
    ReleaseOnExit release{fixture};
    require(!assets.request("/unapproved/media.png"), "Media validation is retained for injected loaders");
    require(assets.request(path(1)) && assets.request(path(2)), "Both existing workers accept work");
    const auto first = fixture.wait(path(1));
    const auto second = fixture.wait(path(2));
    require(assets.request(path(3)), "Queue a third request while both workers are occupied");
    assets.cancel(path(3));
    assets.cancel(path(3));
    require(assets.request(path(4)), "Cancellation leaves the queue usable");
    fixture.release(first);
    const auto barrier = fixture.wait(path(4));
    require(fixture.count(path(3)) == 0, "Canceled queued work is never passed to the loader");
    waitResult(assets);
    auto results = assets.drain();
    require(results.size() == 1 && results.front().path == first->path, "Only completed uncanceled work drains");
    fixture.release(barrier);
    waitResult(assets);
    results = assets.drain();
    require(results.size() == 1 && results.front().path == barrier->path, "Work after a canceled queue item completes");
    fixture.release(second);
    waitResult(assets);
    assets.drain();
    require(assets.request(path(3), true), "A canceled queued path can be requested again");
    const auto replacement = fixture.wait(path(3));
    require(replacement->videoPoster, "A replacement retains its independently requested decode mode");
    fixture.release(replacement);
    waitResult(assets);
    results = assets.drain();
    require(
        results.size() == 1 && results.front().bytes == std::vector<unsigned char>{replacement->value},
        "Only the replacement request publishes a canceled queued path"
    );
  }

  void testCompletedCancellation() {
    LoaderFixture fixture;
    api::Assets assets([&](const auto& path, bool poster, auto stop) { return fixture.load(path, poster, stop); });
    ReleaseOnExit release{fixture};
    require(assets.request(path(1)), "Request a completed cancellation fixture");
    const auto completed = fixture.wait(path(1));
    fixture.release(completed);
    waitResult(assets);
    require(fixture.animationAlive(completed), "The completed queue owns its decoded animation");
    assets.cancel(path(1));
    require(
        assets.drain().empty() && !fixture.animationAlive(completed),
        "Cancel removes completed results and releases their decoded storage immediately"
    );
    require(assets.request(path(1)), "A canceled completed path can be requested again");
    const auto replacement = fixture.wait(path(1), 2);
    fixture.release(replacement);
    waitResult(assets);
    const auto results = assets.drain();
    require(
        results.size() == 1 && results.front().bytes == std::vector<unsigned char>{replacement->value},
        "A replacement publishes fresh data after its completed predecessor was discarded"
    );
  }

  void testInFlightGenerationRace() {
    LoaderFixture fixture(true);
    api::Assets assets([&](const auto& path, bool poster, auto stop) { return fixture.load(path, poster, stop); });
    ReleaseOnExit release{fixture};
    require(assets.request(path(1)), "Request the first generation");
    const auto old = fixture.wait(path(1));
    assets.cancel(path(1));
    require(old->stop.stop_requested(), "Cancel requests stop on the actual in-flight loader token");
    require(assets.request(path(1), true), "Immediately reenable the same path while its old loader is still running");
    const auto replacement = fixture.wait(path(1), 2);
    require(
        !replacement->stop.stop_requested() && replacement->videoPoster,
        "The new generation has independent cancellation state and decode mode"
    );
    fixture.release(old);
    require(assets.request(path(2)), "Queue a barrier while the replacement occupies the other worker");
    const auto barrier = fixture.wait(path(2));
    fixture.release(barrier);
    waitResult(assets);
    auto results = assets.drain();
    require(
        results.size() == 1 && results.front().path == barrier->path,
        "A late old loader cannot publish over or remove the replacement's pending identity"
    );
    require(assets.request(path(1)), "A still-pending replacement remains coalesced after the old loader finishes");
    require(fixture.count(path(1)) == 2, "Old completion cannot accidentally enable a duplicate request");
    fixture.release(replacement);
    waitResult(assets);
    results = assets.drain();
    require(
        results.size() == 1 && results.front().bytes == std::vector<unsigned char>{replacement->value},
        "Only the newest in-flight generation reaches the application"
    );
  }

  void testQueueLimits() {
    LoaderFixture fixture;
    api::Assets assets([&](const auto& path, bool poster, auto stop) { return fixture.load(path, poster, stop); });
    ReleaseOnExit release{fixture};
    require(assets.request(path(1)) && assets.request(path(2)), "Occupy both workers for bounded queue checks");
    fixture.wait(path(1));
    fixture.wait(path(2));
    for (int index = 0; index < 128; ++index)
      require(assets.request(path(1000 + index)), "Fill the existing bounded request queue");
    require(!assets.request(path(3000)), "Cancellation support retains the existing queue capacity");
    assets.cancel(path(1000));
    require(assets.request(path(3000)), "Canceling queued work immediately releases its queue slot");
  }

  void testShutdownCancellation() {
    LoaderFixture fixture;
    std::shared_ptr<Call> first, second;
    {
      api::Assets assets([&](const auto& path, bool poster, auto stop) { return fixture.load(path, poster, stop); });
      require(assets.request(path(1)) && assets.request(path(2)), "Start both worker shutdown fixtures");
      first = fixture.wait(path(1));
      second = fixture.wait(path(2));
    }
    require(
        first->stop.stop_requested() && second->stop.stop_requested(),
        "Destruction stops both in-flight requests before joining their workers"
    );
  }
} // namespace

int main() {
  try {
    testQueuedCancellation();
    testCompletedCancellation();
    testInFlightGenerationRace();
    testQueueLimits();
    testShutdownCancellation();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
