#pragma once

#include <array>
#include <curl/curl.h>
#include <memory>
#include <mutex>

namespace api {
  // Worker connections stay private. Only DNS answers and TLS sessions are
  // shared, so a newly used read lane can reuse the account owner's setup.
  // This object must outlive every attached easy handle.
  class RestTransportCache {
  public:
    RestTransportCache() : m_share(curl_share_init(), curl_share_cleanup) {
      if (!m_share)
        return;
      if (curl_share_setopt(m_share.get(), CURLSHOPT_USERDATA, this) != CURLSHE_OK
          || curl_share_setopt(m_share.get(), CURLSHOPT_LOCKFUNC, lock) != CURLSHE_OK
          || curl_share_setopt(m_share.get(), CURLSHOPT_UNLOCKFUNC, unlock) != CURLSHE_OK
          || curl_share_setopt(m_share.get(), CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS) != CURLSHE_OK
          || curl_share_setopt(m_share.get(), CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION) != CURLSHE_OK)
        m_share.reset();
    }
    void attach(CURL* easy) const {
      if (easy && m_share)
        (void)curl_easy_setopt(easy, CURLOPT_SHARE, m_share.get());
    }

  private:
    static void lock(CURL*, curl_lock_data data, curl_lock_access, void* context) {
      static_cast<RestTransportCache*>(context)->m_locks[data].lock();
    }
    static void unlock(CURL*, curl_lock_data data, void* context) {
      static_cast<RestTransportCache*>(context)->m_locks[data].unlock();
    }
    std::array<std::mutex, CURL_LOCK_DATA_LAST> m_locks;
    std::unique_ptr<CURLSH, decltype(&curl_share_cleanup)> m_share;
  };
} // namespace api
