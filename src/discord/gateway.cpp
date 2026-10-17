#include "discord/gateway.h"

#include <algorithm>
#include <chrono>
#include <curl/curl.h>
#include <curl/websockets.h>
#include <deque>
#include <memory>
#include <nlohmann/json.hpp>
#include <poll.h>
#include <stdexcept>

namespace discord {
  namespace {
    using namespace std::chrono_literals;
    using Json = nlohmann::json;
    using Clock = std::chrono::steady_clock;
    struct GatewayFailure : std::runtime_error {
      using std::runtime_error::runtime_error;
    };
    struct Connection {
      std::stop_token stop;
      const std::atomic<std::uint64_t>& generation;
      std::uint64_t session;
      bool cancelled() const { return stop.stop_requested() || generation != session; }
    };
    int progress(void* data, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
      return static_cast<Connection*>(data)->cancelled();
    }
    void wait(CURL* curl, short events) {
      curl_socket_t socket = CURL_SOCKET_BAD;
      curl_easy_getinfo(curl, CURLINFO_ACTIVESOCKET, &socket);
      pollfd fd{socket, events, 0};
      ::poll(&fd, 1, 50);
    }
    void send(CURL* curl, const Json& payload, const Connection& connection) {
      const auto bytes = payload.dump();
      std::size_t offset = 0;
      const auto deadline = Clock::now() + 5s;
      do {
        if (connection.cancelled() || Clock::now() > deadline)
          throw GatewayFailure("Gateway send cancelled");
        size_t sent = 0;
        const auto status = curl_ws_send(curl, bytes.data() + offset, bytes.size() - offset, &sent, 0, CURLWS_TEXT);
        offset += sent;
        if (status == CURLE_AGAIN)
          wait(curl, POLLOUT);
        else if (status != CURLE_OK)
          throw GatewayFailure(std::string("Gateway send failed: ") + curl_easy_strerror(status));
      } while (offset < bytes.size());
    }
  } // namespace

  Gateway::Gateway(const std::atomic<std::uint64_t>& generation, Dispatch dispatch, std::string endpoint)
      : m_generation(generation), m_dispatch(std::move(dispatch)), m_endpoint(std::move(endpoint)),
        m_worker([this](std::stop_token stop) { run(stop); }) {}
  Gateway::~Gateway() {
    m_worker.request_stop();
    m_ready.notify_all();
    m_worker.join();
  }
  void Gateway::authenticate(std::uint64_t session, std::string token, Presence presence) {
    std::lock_guard lock(m_mutex);
    if (session != m_generation)
      return;
    const bool changed = m_session != session;
    m_session = session;
    m_token = std::move(token);
    if (changed || m_presenceVersion == 0 || !validStatus(m_presence.status))
      m_presence = std::move(presence);
    ++m_presenceVersion;
    ++m_subscriptionVersion;
    m_ready.notify_all();
  }
  void Gateway::reset() {
    std::lock_guard lock(m_mutex);
    m_token.clear();
    m_presence = {};
    m_presenceVersion = 0;
    m_guild.clear();
    m_channel.clear();
    ++m_subscriptionVersion;
    m_ready.notify_all();
  }
  void Gateway::subscribe(std::string guild, std::string channel, int first, bool force) {
    std::lock_guard lock(m_mutex);
    first = std::clamp(first / 100 * 100, 0, 10000000);
    if (!force && guild == m_guild && channel == m_channel && first == m_first)
      return;
    m_guild = std::move(guild);
    m_channel = std::move(channel);
    m_first = first;
    ++m_subscriptionVersion;
  }

  bool Gateway::setPresence(std::uint64_t session, std::string status, std::optional<std::string> customText) {
    if (!validStatus(status) || (customText && utf8Length(*customText) > 128))
      return false;
    std::lock_guard lock(m_mutex);
    if (session != m_generation || session != m_session || m_token.empty())
      return false;
    if (m_presence.status == status
        && (!customText
            || (m_presence.text == *customText && m_presence.emojiId.empty() && m_presence.emojiName.empty())))
      return true;
    m_presence.status = std::move(status);
    if (customText) {
      m_presence.text = std::move(*customText);
      m_presence.emojiId.clear();
      m_presence.emojiName.clear();
    }
    ++m_presenceVersion;
    return true;
  }

  void Gateway::run(std::stop_token stop) {
    int backoff = 1;
    std::string resumeId;
    std::int64_t sequence = -1;
    std::uint64_t activeSession = 0;
    while (!stop.stop_requested()) {
      std::string token;
      std::uint64_t session;
      {
        std::unique_lock lock(m_mutex);
        m_ready.wait(lock, stop, [&] { return !m_token.empty() && m_session == m_generation; });
        if (stop.stop_requested())
          return;
        token = m_token;
        session = m_session;
      }
      if (activeSession != session) {
        activeSession = session;
        resumeId.clear();
        sequence = -1;
        backoff = 1;
      }
      Connection connection{stop, m_generation, session};
      bool fatal = false;
      std::string failure = "Live connection interrupted";
      try {
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
        if (!curl)
          throw std::runtime_error("Gateway unavailable");
        // The only alternative origin is an explicitly injected loopback test server.
        const bool loopback = m_endpoint.starts_with("ws://127.0.0.1:");
        if (!loopback && m_endpoint != "wss://gateway.discord.gg/?v=9&encoding=json")
          throw std::runtime_error("Invalid Gateway origin");
        curl_easy_setopt(curl.get(), CURLOPT_URL, m_endpoint.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_CONNECT_ONLY, 2L);
        curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, loopback ? "ws" : "wss");
        if (loopback)
          curl_easy_setopt(curl.get(), CURLOPT_NOPROXY, "127.0.0.1");
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 10000L);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 20000L);
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, progress);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &connection);
        const auto connected = curl_easy_perform(curl.get());
        if (connected != CURLE_OK)
          throw GatewayFailure(std::string("Gateway connection failed: ") + curl_easy_strerror(connected));
        std::string message, subscribedGuild, subscribedChannel;
        std::uint64_t subscription = 0, presenceVersion = 0;
        std::deque<Clock::time_point> presenceTimes;
        auto subscribedAt = Clock::time_point{};
        auto heartbeatAt = Clock::now() + 20s;
        std::chrono::milliseconds interval{30000};
        bool hello = false, ready = false, awaitingAck = false;
        while (!connection.cancelled()) {
          const auto now = Clock::now();
          if (now >= heartbeatAt) {
            if (!hello || awaitingAck)
              throw GatewayFailure("Gateway heartbeat timed out");
            send(curl.get(), Json{{"op", 1}, {"d", sequence < 0 ? Json(nullptr) : Json(sequence)}}, connection);
            awaitingAck = true;
            heartbeatAt = now + interval;
          }
          if (ready) {
            Presence presence;
            std::uint64_t desiredVersion;
            {
              std::lock_guard lock(m_mutex);
              presence = m_presence;
              desiredVersion = m_presenceVersion;
            }
            while (!presenceTimes.empty() && now - presenceTimes.front() >= 20s)
              presenceTimes.pop_front();
            if (presenceVersion != desiredVersion && presenceTimes.size() < 5) {
              if (validStatus(presence.status)) {
                Json activities = Json::array();
                if (!presence.text.empty() || !presence.emojiId.empty() || !presence.emojiName.empty()) {
                  Json custom{{"name", "Custom Status"}, {"type", 4}, {"state", presence.text}};
                  if (!presence.emojiId.empty())
                    custom["emoji"] = {{"id", presence.emojiId}, {"name", presence.emojiName}};
                  else if (!presence.emojiName.empty())
                    custom["emoji"] = {{"name", presence.emojiName}};
                  activities.push_back(std::move(custom));
                }
                send(
                    curl.get(),
                    Json{
                        {"op", 3},
                        {"d",
                         {{"status", presence.status},
                          {"since",
                           presence.status == "idle" ? std::chrono::duration_cast<std::chrono::milliseconds>(
                                                           std::chrono::system_clock::now().time_since_epoch()
                                                       )
                                                           .count()
                                                     : 0},
                          {"afk", false},
                          {"activities", activities}}}
                    },
                    connection
                );
                presenceTimes.push_back(now);
                m_dispatch(session, "PRESENCE_SENT", Json{{"status", presence.status}, {"text", presence.text}});
              }
              presenceVersion = desiredVersion;
            }
            std::string guild, channel;
            int first;
            std::uint64_t version;
            {
              std::lock_guard lock(m_mutex);
              guild = m_guild;
              channel = m_channel;
              first = m_first;
              version = m_subscriptionVersion;
            }
            if (version != subscription
                && !channel.empty()
                && (channel != subscribedChannel || guild != subscribedGuild || now - subscribedAt >= 500ms)) {
              Json subscriptions = Json::object();
              if (!subscribedGuild.empty() && subscribedGuild != guild)
                subscriptions[subscribedGuild] = {
                    {"typing", false}, {"activities", false}, {"threads", false}, {"channels", Json::object()}
                };
              Json ranges = Json::array({Json::array({first, first + 99}), Json::array({first + 100, first + 199})});
              Json channels = {{channel, ranges}};
              if (subscribedGuild == guild && !subscribedChannel.empty() && subscribedChannel != channel)
                channels[subscribedChannel] = Json::array();
              if (!guild.empty())
                subscriptions[guild] = {
                    {"typing", true},
                    {"activities", true},
                    {"threads", true},
                    {"member_updates", true},
                    {"channels", channels}
                };
              if (!subscriptions.empty())
                send(curl.get(), Json{{"op", 37}, {"d", {{"subscriptions", subscriptions}}}}, connection);
              subscription = version;
              subscribedGuild = guild;
              subscribedChannel = channel;
              subscribedAt = now;
            }
          }
          char buffer[65536];
          size_t count = 0;
          const curl_ws_frame* frame = nullptr;
          const auto code = curl_ws_recv(curl.get(), buffer, sizeof(buffer), &count, &frame);
          if (code == CURLE_AGAIN) {
            wait(curl.get(), POLLIN);
            continue;
          }
          if (code != CURLE_OK || !frame)
            throw GatewayFailure(std::string("Gateway disconnected: ") + curl_easy_strerror(code));
          if (frame->flags & CURLWS_CLOSE) {
            const unsigned closeCode = count >= 2
                ? (static_cast<unsigned char>(buffer[0]) << 8) | static_cast<unsigned char>(buffer[1])
                : 1000;
            if (closeCode == 4004
                || closeCode == 4010
                || closeCode == 4011
                || closeCode == 4012
                || closeCode == 4013
                || closeCode == 4014)
              fatal = true;
            if (closeCode == 4007 || closeCode == 4009) {
              resumeId.clear();
              sequence = -1;
            }
            throw GatewayFailure("Gateway closed (" + std::to_string(closeCode) + ")");
          }
          if (!(frame->flags & CURLWS_TEXT))
            continue;
          if (message.size() + count > 32 * 1024 * 1024)
            throw std::runtime_error("Gateway payload too large");
          message.append(buffer, count);
          if (frame->bytesleft || (frame->flags & CURLWS_CONT))
            continue;
          auto packet = Json::parse(message, nullptr, false);
          message.clear();
          if (!packet.is_object())
            throw std::runtime_error("Malformed Gateway payload");
          const int op = packet.value("op", -1);
          const auto& data = packet["d"];
          if (packet.contains("s") && packet["s"].is_number_integer())
            sequence = packet["s"].get<std::int64_t>();
          if (op == 10) {
            if (hello)
              throw std::runtime_error("Duplicate Gateway hello");
            hello = true;
            interval = std::chrono::milliseconds(std::clamp(data.value("heartbeat_interval", 30000), 1000, 120000));
            heartbeatAt = Clock::now();
            if (!resumeId.empty() && sequence >= 0)
              send(
                  curl.get(), Json{{"op", 6}, {"d", {{"token", token}, {"session_id", resumeId}, {"seq", sequence}}}},
                  connection
              );
            else
              send(
                  curl.get(),
                  Json{
                      {"op", 2},
                      {"d",
                       {{"token", token},
                        {"compress", false},
                        {"capabilities", 16381},
                        {"properties",
                         {{"os", "Linux"},
                          {"browser", "cressida"},
                          {"device", "cressida"},
                          {"system_locale", "en-US"},
                          {"release_channel", "stable"}}},
                        {"presence",
                         {{"status", "unknown"}, {"since", 0}, {"activities", Json::array()}, {"afk", false}}}}}
                  },
                  connection
              );
          } else if (op == 11)
            awaitingAck = false;
          else if (op == 1) {
            send(curl.get(), Json{{"op", 1}, {"d", sequence < 0 ? Json(nullptr) : Json(sequence)}}, connection);
            awaitingAck = true;
          } else if (op == 7)
            throw GatewayFailure("Gateway requested reconnect");
          else if (op == 9) {
            if (!data.is_boolean() || !data.get<bool>()) {
              resumeId.clear();
              sequence = -1;
            }
            throw GatewayFailure("Gateway session invalidated");
          } else if (op == 0) {
            const auto type = packet.value("t", std::string{});
            if (type == "READY") {
              resumeId = data.value("session_id", std::string{});
              {
                std::lock_guard lock(m_mutex);
                if (m_presence.status == "unknown" && data.contains("sessions") && data["sessions"].is_array())
                  for (const auto& own : data["sessions"])
                    if (own.value("session_id", "") == resumeId) {
                      const auto state = own.value("status", "unknown");
                      if (validStatus(state)) {
                        m_presence.status = state;
                        ++m_presenceVersion;
                      }
                    }
              }
              ready = true;
              backoff = 1;
            }
            if (type == "RESUMED") {
              ready = true;
              backoff = 1;
            }
            m_dispatch(session, type, data);
          }
        }
      } catch (const GatewayFailure& error) {
        failure = error.what();
      } catch (...) { /* Payloads and transport diagnostics may contain credentials. */
      }
      if (!connection.cancelled())
        m_dispatch(session, fatal ? "CONNECTION_FAILED" : "RECONNECTING", Json{{"reason", failure}});
      if (fatal) {
        std::lock_guard lock(m_mutex);
        if (m_session == session)
          m_token.clear();
        continue;
      }
      const auto retryAt = Clock::now() + std::chrono::seconds(backoff);
      while (!connection.cancelled() && Clock::now() < retryAt)
        std::this_thread::sleep_for(50ms);
      backoff = std::min(backoff * 2, 60);
    }
  }
} // namespace discord
