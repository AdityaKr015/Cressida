#include "discord/assets.h"

#include "discord/models.h"

#include <algorithm>
#include <cctype>
#include <curl/curl.h>
#include <memory>
#include <utility>

namespace discord {
  namespace {
    struct Download {
      std::vector<unsigned char> bytes;
      std::stop_token stop;
    };
    size_t receive(char* bytes, size_t size, size_t count, void* context) noexcept {
      auto& download = *static_cast<Download*>(context);
      const auto length = size * count;
      if (length > maxUploadBytes - download.bytes.size())
        return 0;
      try {
        download.bytes.insert(download.bytes.end(), bytes, bytes + length);
      } catch (...) {
        return 0;
      }
      return length;
    }
    int progress(void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
      return static_cast<Download*>(context)->stop.stop_requested();
    }
  } // namespace
  Assets::Assets() {
    for (int i = 0; i < 2; ++i)
      m_workers.emplace_back([this](std::stop_token stop) { run(stop); });
  }
  Assets::~Assets() {
    for (auto& worker : m_workers)
      worker.request_stop();
    m_ready.notify_all();
    m_workers.clear();
  }
  bool Assets::request(const std::string& path) {
    if (mediaUrl(path).empty())
      return false;
    std::lock_guard lock(m_mutex);
    if (m_pending.contains(path))
      return true;
    if (m_queue.size() >= 128 || m_results.size() >= 128)
      return false;
    m_pending.insert(path);
    auto priority = [](const std::string& resource) {
      return resource.contains("/avatars/") ? 3
          : resource.contains("/emojis/")   ? 2
          : resource.contains("/icons/")    ? 0
                                            : 1;
    };
    m_queue.insert(
        std::ranges::find_if(m_queue, [&](const auto& pending) { return priority(pending) < priority(path); }), path
    );
    m_ready.notify_one();
    return true;
  }
  std::vector<Asset> Assets::drain() {
    m_wake.drain();
    std::lock_guard lock(m_mutex);
    for (const auto& result : m_results)
      m_pending.erase(result.path);
    return std::exchange(m_results, {});
  }
  void Assets::run(std::stop_token stop) {
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    while (!stop.stop_requested()) {
      std::string path;
      {
        std::unique_lock lock(m_mutex);
        if (!m_ready.wait(lock, stop, [&] { return !m_queue.empty(); }))
          return;
        path = std::move(m_queue.front());
        m_queue.pop_front();
      }
      Download data{{}, stop};
      if (curl) {
        curl_easy_reset(curl.get());
        const std::string url = mediaUrl(path);
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 3000L);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 8000L);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &data);
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, progress);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &data);
        const auto status = curl_easy_perform(curl.get());
        long code = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &code);
        if (status != CURLE_OK || code != 200)
          data.bytes.clear();
      }
      auto animation = media::decodeAnimation(data.bytes, stop);
      if (!stop.stop_requested()) {
        std::lock_guard lock(m_mutex);
        std::size_t resident = 0;
        for (const auto& result : m_results)
          resident += result.bytes.size() + (result.animation ? result.animation->byteSize() : 0);
        if (resident + data.bytes.size() > 128 * 1024 * 1024)
          data.bytes.clear();
        if (data.bytes.empty()
            || resident + data.bytes.size() + (animation ? animation->byteSize() : 0) > 128 * 1024 * 1024)
          animation.reset();
        if (m_results.size() < 128)
          m_results.push_back({std::move(path), std::move(data.bytes), std::move(animation)});
        else
          m_pending.erase(path);
        (void)m_wake.signal();
      }
    }
  }
} // namespace discord
