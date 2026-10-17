#pragma once

#include "discord/models.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <thread>

namespace discord {
  // One persistent TLS WebSocket worker, independent of REST/media requests.
  class Gateway {
  public:
    using Dispatch = std::function<void(std::uint64_t, const std::string&, const nlohmann::json&)>;
    Gateway(
        const std::atomic<std::uint64_t>& generation, Dispatch dispatch,
        std::string endpoint = "wss://gateway.discord.gg/?v=9&encoding=json"
    );
    ~Gateway();
    void authenticate(std::uint64_t session, std::string token, Presence presence = {});
    void reset();
    bool setPresence(std::uint64_t session, std::string status, std::optional<std::string> customText = {});
    void subscribe(std::string guild, std::string channel, int first, bool force = false);

  private:
    void run(std::stop_token stop);
    const std::atomic<std::uint64_t>& m_generation;
    Dispatch m_dispatch;
    std::string m_endpoint;
    std::mutex m_mutex;
    std::condition_variable_any m_ready;
    std::uint64_t m_session = 0, m_subscriptionVersion = 0;
    std::string m_token, m_guild, m_channel;
    Presence m_presence;
    std::uint64_t m_presenceVersion = 0;
    int m_first = 0;
    std::jthread m_worker;
  };
} // namespace discord
