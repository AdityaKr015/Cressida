#pragma once

#include <atomic>
#include <cstdint>
#include <optional>

namespace api {
  enum class BootstrapSource { Rest, Gateway };

  struct BootstrapResolution {
    bool authenticated = false;
    bool unauthorized = false;
    bool restored = false;
  };

  // The client mutex guards the pending attempt. Only the resolved generation
  // is observed without that mutex, to cancel the losing REST verification.
  class ConnectionBootstrap {
  public:
    void begin(std::uint64_t session, bool restored) {
      m_pending = session;
      m_restored = restored;
      m_restFailed = m_gatewayFailed = false;
      m_resolved = 0;
    }
    void cancel() {
      m_pending = 0;
      m_resolved = 0;
    }
    bool pending(std::uint64_t session) const { return session && m_pending == session; }
    const std::atomic<std::uint64_t>& resolved() const { return m_resolved; }

    std::optional<BootstrapResolution> succeed(std::uint64_t session) { return finish(session, true, false); }
    std::optional<BootstrapResolution> fail(std::uint64_t session, BootstrapSource source, bool unauthorized) {
      if (!pending(session))
        return {};
      (source == BootstrapSource::Rest ? m_restFailed : m_gatewayFailed) = true;
      if (unauthorized || (m_restFailed && m_gatewayFailed))
        return finish(session, false, unauthorized);
      return {};
    }

  private:
    std::optional<BootstrapResolution> finish(std::uint64_t session, bool authenticated, bool unauthorized) {
      if (!pending(session))
        return {};
      m_pending = 0;
      m_resolved = session;
      return BootstrapResolution{authenticated, unauthorized, m_restored};
    }
    std::uint64_t m_pending = 0;
    bool m_restored = false, m_restFailed = false, m_gatewayFailed = false;
    std::atomic<std::uint64_t> m_resolved{0};
  };
} // namespace api
