#pragma once
#include "core/process/wake_event.h"
#include "media/animation.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace api {
  struct Asset {
    std::string path;
    std::vector<unsigned char> bytes;
    std::shared_ptr<const media::Animation> animation;
  };
  // Public media only. Authentication is never attached to media requests.
  class Assets {
  public:
    using Loader = std::function<Asset(const std::string&, bool, std::stop_token)>;
    Assets();
    explicit Assets(Loader loader);
    ~Assets();
    bool request(const std::string& path, bool videoPoster = false);
    void cancel(const std::string& path);
    std::vector<Asset> drain();
    int eventFd() const noexcept { return m_wake.fd(); }

  private:
    void run(std::stop_token stop);
    process::WakeEvent m_wake;
    std::mutex m_mutex;
    std::condition_variable_any m_ready;
    struct Request {
      std::string path;
      bool videoPoster = false;
      std::shared_ptr<std::stop_source> cancellation;
    };
    std::deque<Request> m_queue;
    std::map<std::string, std::shared_ptr<std::stop_source>> m_pending;
    std::vector<Asset> m_results;
    Loader m_loader;
    std::vector<std::jthread> m_workers;
  };
} // namespace api
