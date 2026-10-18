#pragma once

#include "api/models.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace api {
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
    void requestMembers(std::uint64_t session, const std::string& guild, const std::vector<std::string>& users);
    bool
    searchMembers(std::uint64_t session, const std::string& guild, const std::string& query, const std::string& nonce);
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
    std::set<std::pair<std::string, std::string>> m_memberRequested;
    std::map<std::string, std::set<std::string>> m_memberPending;
    std::size_t m_memberPendingCount = 0;
    struct MemberSearch {
      std::string guild, query, nonce;
    };
    std::optional<MemberSearch> m_memberSearch;
    std::jthread m_worker;
  };
} // namespace api
