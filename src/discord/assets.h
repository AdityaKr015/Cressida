#pragma once
#include "core/process/wake_event.h"
#include "media/animation.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace discord {
  struct Asset {
    std::string path;
    std::vector<unsigned char> bytes;
    std::shared_ptr<const media::Animation> animation;
  };
  // Public Discord CDN only. Authentication is never attached to media requests.
  class Assets {
  public:
    Assets();
    ~Assets();
    bool request(const std::string& path, bool videoPoster = false);
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
    };
    std::deque<Request> m_queue;
    std::set<std::string> m_pending;
    std::vector<Asset> m_results;
    std::vector<std::jthread> m_workers;
  };
} // namespace discord
