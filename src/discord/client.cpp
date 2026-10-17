#include "discord/client.h"

#include "discord/uploads.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <curl/curl.h>
#include <expected>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

namespace discord {
  namespace {
    using namespace std::chrono_literals;
    using Json = nlohmann::json;
    struct Failure {
      std::string message;
      bool unauthorized = false;
    };
    struct Transfer {
      std::string body;
      std::stop_token stop;
      const std::atomic<std::uint64_t>* session;
      std::uint64_t expected;
      CURL* connection = nullptr;
    };

    size_t receive(char* bytes, size_t size, size_t count, void* context) noexcept {
      auto& transfer = *static_cast<Transfer*>(context);
      const auto length = size * count;
      if (length > 8 * 1024 * 1024 - transfer.body.size())
        return 0;
      try {
        transfer.body.append(bytes, length);
      } catch (...) {
        return 0;
      }
      return length;
    }

    int progress(void* context, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
      const auto& transfer = *static_cast<Transfer*>(context);
      return transfer.stop.stop_requested() || transfer.session->load() != transfer.expected;
    }

    // All requests are HTTPS to a fixed origin. Never log authorization or response bodies.
    std::expected<Json, Failure> request(
        const std::string& token, const std::string& path, const Json* payload, Transfer& transfer,
        std::chrono::steady_clock::time_point& cooldown, const std::vector<Upload>& uploads = {},
        const char* method = nullptr
    ) {
      for (int attempt = 0; attempt < 3; ++attempt) {
        while (std::chrono::steady_clock::now() < cooldown) {
          if (progress(&transfer, 0, 0, 0, 0))
            return std::unexpected(Failure{"Request cancelled"});
          std::this_thread::sleep_for(50ms);
        }
        // Reusing the easy handle retains its TLS/HTTP connection and DNS cache.
        auto* curlHandle = transfer.connection;
        if (curlHandle)
          curl_easy_reset(curlHandle);
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> owned(
            curlHandle ? nullptr : curl_easy_init(), curl_easy_cleanup
        );
        if (!curlHandle)
          curlHandle = owned.get();
        struct Handle {
          CURL* value;
          CURL* get() const { return value; }
          explicit operator bool() const { return value; }
        } curl{curlHandle};
        if (!curl)
          return std::unexpected(Failure{"Could not initialize the connection"});
        curl_slist* raw =
            curl_slist_append(nullptr, uploads.empty() ? "Content-Type: application/json" : "Accept: application/json");
        if (!raw)
          return std::unexpected(Failure{"Could not allocate request headers"});
        auto* extended = token.empty() ? raw : curl_slist_append(raw, ("Authorization: " + token).c_str());
        if (!extended) {
          curl_slist_free_all(raw);
          return std::unexpected(Failure{"Could not allocate request headers"});
        }
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers(extended, curl_slist_free_all);
        const std::string url = "https://discord.com/api/v9" + path;
        const std::string body = payload ? payload->dump() : std::string{};
        transfer.body.clear();
        curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_HTTPHEADER, headers.get());
        curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "cressida/0.1.0");
        curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 10000L);
        curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 30000L);
        curl_easy_setopt(curl.get(), CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, receive);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &transfer);
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFOFUNCTION, progress);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &transfer);
        std::unique_ptr<curl_mime, decltype(&curl_mime_free)> mime(nullptr, curl_mime_free);
        if (!uploads.empty()) {
          mime = makeMessageMime(curl.get(), body, uploads);
          curl_easy_setopt(curl.get(), CURLOPT_MIMEPOST, mime.get());
        } else if (payload) {
          curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDS, body.c_str());
          curl_easy_setopt(curl.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
        }
        if (method)
          curl_easy_setopt(curl.get(), CURLOPT_CUSTOMREQUEST, method);
        const auto code = curl_easy_perform(curl.get());
        if (code != CURLE_OK)
          return std::unexpected(
              Failure{
                  payload && !token.empty()
                      ? (method ? "The update could not be confirmed. Check your settings before retrying."
                                : "Message delivery could not be confirmed. Check the conversation before retrying.")
                      : "Could not reach Discord. Try again."
              }
          );
        long status = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
        auto json = decodeRestBody(status, transfer.body);
        // Only Discord rate limits impose a delay; successful reads need no fixed pause.
        if (status == 429) {
          double seconds = 1.0;
          if (!json.is_discarded() && json.contains("retry_after") && json["retry_after"].is_number())
            seconds = json["retry_after"].get<double>();
          // Respect long limits too; return to the user rather than retrying prematurely.
          if (!std::isfinite(seconds) || seconds < 0)
            seconds = 60;
          cooldown = std::chrono::steady_clock::now()
              + std::chrono::milliseconds(static_cast<long long>(std::min(seconds, 86400.0) * 1000) + 100);
          if (seconds > 30)
            return std::unexpected(Failure{"Discord is rate limiting this connection. Try again later."});
          continue;
        }
        if (status == 401 && !token.empty())
          return std::unexpected(Failure{"The account token was rejected. Reconnect with a valid token.", true});
        if ((status < 200 || status >= 300) && token.empty())
          return std::unexpected(Failure{"Discord could not complete browser sign-in. Please try again."});
        if (status == 403)
          return std::unexpected(Failure{"You do not have permission to access this channel or perform this action."});
        if (status < 200 || status >= 300)
          return std::unexpected(Failure{"Discord returned an error (HTTP " + std::to_string(status) + ")."});
        if (json.is_discarded())
          return std::unexpected(Failure{"Discord returned an unreadable response."});
        return json;
      }
      return std::unexpected(Failure{"Discord is rate limiting this connection. Try again later."});
    }
  } // namespace

  Client::Client(std::unique_ptr<auth::CredentialBackend> credentials, std::filesystem::path disabledLogin) {
    m_credentials = std::make_unique<auth::CredentialStore>(std::move(credentials), std::move(disabledLogin));
    // libcurl initialization is process-wide; this static lives beyond every client worker.
    struct CurlRuntime {
      CurlRuntime() {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
          throw std::runtime_error("libcurl initialization failed");
      }
      ~CurlRuntime() { curl_global_cleanup(); }
    };
    static CurlRuntime runtime;
    if (!m_wake.valid())
      throw std::runtime_error("Could not create the network event pipe");
    m_gateway = std::make_unique<Gateway>(m_session, [this](auto session, const auto& type, const auto& data) {
      gatewayEvent(session, type, data);
    });
    m_worker = std::jthread([this](std::stop_token stop) { run(stop); });
  }

  Client::~Client() {
    ++m_session;
    m_worker.request_stop();
    m_credentials->shutdown();
    m_ready.notify_all();
    if (m_worker.joinable())
      m_worker.join();
    m_gateway.reset();
  }

  std::uint64_t Client::connect(std::string token) { return beginConnect(std::move(token), {}); }

  std::uint64_t Client::connectHandoff(std::string key, std::string handoffToken) {
    return beginConnect(std::move(handoffToken), std::move(key));
  }

  std::uint64_t Client::beginConnect(std::string credential, std::optional<std::string> handoffKey) {
    m_credentials->cancelLoad();
    const auto session = ++m_session;
    m_authenticatedSession = 0;
    m_restoringSession = 0;
    m_gateway->reset();
    {
      std::lock_guard lock(m_mutex);
      m_commands.clear();
      m_events.clear();
      m_commands.push_back({Operation::Connect, session, {}, std::move(credential), std::move(handoffKey)});
    }
    m_ready.notify_all();
    return session;
  }

  std::uint64_t Client::disconnect() {
    m_credentials->cancelLoad();
    const auto session = ++m_session;
    m_authenticatedSession = 0;
    m_restoringSession = 0;
    m_gateway->reset();
    std::lock_guard lock(m_mutex);
    m_commands.clear();
    m_events.clear();
    // Wake the worker to clear its credential even when no further commands arrive.
    m_ready.notify_all();
    return session;
  }

  std::uint64_t Client::restoreLogin() {
    const auto session = disconnect();
    m_restoringSession = session;
    m_credentials->load(session, [this](auth::CredentialResult result) {
      if (!result.found || !result.error.empty()) {
        Event event{.operation = Operation::Connect, .session = result.request};
        event.error = std::move(result.error);
        event.noSavedLogin = event.error.empty();
        event.restoredLogin = true;
        publish(std::move(event));
        return;
      }
      {
        std::lock_guard lock(m_mutex);
        if (result.request != m_session)
          return;
        Command command{Operation::Connect, result.request, {}, std::move(result.token)};
        command.restoredLogin = true;
        m_commands.push_back(std::move(command));
      }
      m_ready.notify_all();
    });
    return session;
  }
  void Client::saveLogin(std::uint64_t session, const std::string& token) {
    std::lock_guard lock(m_mutex);
    if (session != m_session)
      return;
    m_credentials->save(session, token, [this](auth::CredentialResult result) {
      Event event{.operation = Operation::Credentials, .session = result.request};
      event.credentialOperation = result.operation;
      event.error = std::move(result.error);
      publish(std::move(event));
    });
  }
  void Client::forgetLogin(std::uint64_t session) {
    std::lock_guard lock(m_mutex);
    if (session != m_session)
      return;
    m_credentials->forget(session, [this](auth::CredentialResult result) {
      Event event{.operation = Operation::Credentials, .session = result.request};
      event.credentialOperation = result.operation;
      event.error = std::move(result.error);
      publish(std::move(event));
    });
  }
  std::uint64_t Client::logout() {
    const auto session = disconnect();
    forgetLogin(session);
    return session;
  }

  bool Client::enqueue(Command command) {
    if (command.session == 0 || m_authenticatedSession != command.session)
      return false;
    if ((command.operation == Operation::Channels
         || command.operation == Operation::Messages
         || command.operation == Operation::Send)
        && !validSnowflake(command.resource))
      return false;
    std::lock_guard lock(m_mutex);
    if (m_commands.size() >= 32)
      return false;
    if (command.operation == Operation::Acknowledge) {
      // A burst of live messages only needs the newest acknowledgement per channel.
      for (auto& pending : m_commands)
        if (pending.operation == command.operation && pending.resource == command.resource) {
          pending.content = std::move(command.content);
          return true;
        }
    }
    if (command.operation == Operation::SavePresence) {
      for (const auto& pending : m_commands)
        if (pending.operation == Operation::SavePresence && !command.customStatus && pending.customStatus)
          command.customStatus = pending.customStatus;
      std::erase_if(m_commands, [](const auto& pending) { return pending.operation == Operation::SavePresence; });
    }
    if (command.operation == Operation::GifSearch)
      std::erase_if(m_commands, [](const auto& pending) { return pending.operation == Operation::GifSearch; });
    if (command.operation != Operation::Send
        && command.operation != Operation::Interaction
        && command.operation != Operation::SaveGifFavorite
        && command.operation != Operation::SaveEmojiFavorite
        && std::ranges::any_of(m_commands, [&](const auto& pending) {
             return pending.operation == command.operation && pending.resource == command.resource;
           }))
      return true;
    m_commands.push_back(std::move(command));
    m_ready.notify_all();
    return true;
  }

  bool Client::channels(const std::string& id) { return enqueue({Operation::Channels, m_session, id, {}}); }
  bool Client::privateChannels() { return enqueue({Operation::PrivateChannels, m_session, {}, {}}); }
  bool Client::messages(const std::string& id) { return enqueue({Operation::Messages, m_session, id, {}}); }
  bool Client::send(
      const std::string& id, std::string content, std::vector<Upload> uploads, std::optional<MessageReference> reply
  ) {
    if ((content.empty() && uploads.empty())
        || content.size() > 8000
        || utf8Length(content) > 2000
        || !validUploads(uploads)
        || (reply && (!validSnowflake(reply->message) || reply->channel != id)))
      return false;
    Command command{Operation::Send, m_session, id, std::move(content), {}, std::move(uploads)};
    command.reply = std::move(reply);
    return enqueue(std::move(command));
  }
  bool Client::interact(discord::Interaction action) {
    if (!interactionRequest(action))
      return false;
    Command command{Operation::Interaction, m_session, action.channel, {}};
    command.interaction = std::move(action);
    return enqueue(std::move(command));
  }

  bool Client::accountSettings() { return enqueue({Operation::AccountSettings, m_session, {}, {}}); }
  bool Client::searchGifs(const std::string& query) {
    if (query.size() > 256)
      return false;
    return enqueue({Operation::GifSearch, m_session, query, {}});
  }
  bool Client::gifFavorites() { return enqueue({Operation::GifFavorites, m_session, {}, {}}); }
  bool Client::guildEmojis(const std::string& guild) {
    return validSnowflake(guild) && enqueue({Operation::GuildEmojis, m_session, guild, {}});
  }
  bool Client::emojiPreferences() { return enqueue({Operation::EmojiPreferences, m_session, {}, {}}); }
  bool Client::saveEmojiFavorite(const std::string& key, bool add) {
    return validEmojiKey(key) && enqueue({Operation::SaveEmojiFavorite, m_session, key, add ? "add" : "remove"});
  }
  bool Client::saveGifFavorite(const Gif& gif, bool add) {
    if (!validGif(gif))
      return false;
    auto value = gifJson(gif);
    value["add"] = add;
    return enqueue({Operation::SaveGifFavorite, m_session, gif.url, value.dump()});
  }
  bool Client::saveAccountSettings(SettingValues changes) {
    if (!validSettingChanges(changes))
      return false;
    return enqueue({Operation::SaveAccountSettings, m_session, {}, Json(changes).dump()});
  }
  bool Client::settingsList(const std::string& section) {
    if (section != "connections" && section != "authorized" && section != "devices")
      return false;
    return enqueue({Operation::SettingsList, m_session, section, {}});
  }
  bool Client::acknowledge(const std::string& channel, const std::string& message) {
    if (!validSnowflake(channel) || !validSnowflake(message))
      return false;
    return enqueue({Operation::Acknowledge, m_session, channel, message});
  }
  bool Client::profile() { return enqueue({Operation::Profile, m_session, {}, {}}); }
  bool Client::saveProfile(const Account& profile) {
    if (utf8Length(profile.displayName) > 32 || utf8Length(profile.bio) > 190 || utf8Length(profile.pronouns) > 40)
      return false;
    Json body{
        {"global_name", profile.displayName.empty() ? Json(nullptr) : Json(profile.displayName)},
        {"bio", profile.bio},
        {"pronouns", profile.pronouns}
    };
    return enqueue({Operation::SaveProfile, m_session, {}, body.dump()});
  }
  bool Client::setPresence(std::string status, std::optional<std::string> customText) {
    if (!validStatus(status) || (customText && utf8Length(*customText) > 128))
      return false;
    Command command{Operation::SavePresence, m_session, status, {}};
    command.customStatus = customText;
    command.presenceRevision = ++m_presenceRevision;
    if (!enqueue(std::move(command)))
      return false;
    m_gateway->setPresence(m_session, std::move(status), std::move(customText));
    return true;
  }

  void Client::subscribe(const std::string& guild, const std::string& channel, int first) {
    if (m_authenticatedSession != m_session || (!guild.empty() && !validSnowflake(guild)) || !validSnowflake(channel))
      return;
    {
      std::lock_guard lock(m_mutex);
      m_selectedGuild = guild;
      m_selectedChannel = channel;
      m_selectedMemberFirst = first;
    }
    m_gateway->subscribe(guild, channel, first);
  }

  void Client::gatewayEvent(std::uint64_t session, const std::string& type, const Json& data) {
    if (session != m_session)
      return;
    try {
      if (m_memberSession != session) {
        m_memberSession = session;
        m_memberChannel.clear();
        m_memberList = {};
        m_roleNames.clear();
        m_users.clear();
        m_readState = {};
        m_userDirectory = {};
        m_self.clear();
      }
      auto users = m_userDirectory.consume(type, data);
      if (users.reset || !users.users.empty() || !users.mutedChannels.empty()) {
        Event event{.operation = Operation::UserState, .session = session};
        event.users = std::move(users);
        publish(std::move(event));
      }
      auto publishReads = [&](std::string rewind = {}) {
        Event state{.operation = Operation::ReadState, .session = session};
        state.resource = std::move(rewind);
        state.unread = m_readState.unread();
        state.unreadCursors = m_readState.unreadCursors();
        publish(std::move(state));
      };
      if (type == "CHANNEL_UNREAD_UPDATE") {
        if (data.contains("channel_unread_updates"))
          m_readState.channels(data["channel_unread_updates"], data.value("guild_id", ""));
        publishReads();
      } else if (type == "MESSAGE_ACK") {
        m_readState.acknowledge(data);
        publishReads(data.value("manual", false) ? data.value("channel_id", "") : "");
      } else if (type == "PRESENCE_SENT") {
        Event event{.operation = Operation::PresenceChanged, .session = session};
        event.presence = Presence{data.value("status", "unknown"), data.value("text", "")};
        publish(std::move(event));
      } else if (type == "USER_UPDATE") {
        Event event{.operation = Operation::AccountUpdate, .session = session};
        event.account = parseAccount(data);
        publish(std::move(event));
      } else if (type == "READY" || type == "RESUMED") {
        if (type == "READY") {
          if (data.contains("user"))
            m_self = data["user"].value("id", "");
          m_readState.ready(data);
          publishReads("reset");
        }
        publish(Event{.operation = Operation::Gateway, .session = session, .live = true});
        if (data.contains("users"))
          for (const auto& user : data["users"]) {
            auto account = parseAccount(user);
            m_users[account.id] = std::move(account);
          }
        if (data.contains("guilds"))
          for (const auto& guild : data["guilds"]) {
            if (guild.contains("emojis") && guild["emojis"].is_array()) {
              Event event{.operation = Operation::GuildEmojis, .session = session, .resource = guild.value("id", "")};
              event.emojis = parseGuildEmojis(guild["emojis"], event.resource);
              publish(std::move(event));
            }
            if (guild.contains("channels") && guild["channels"].is_array()) {
              Event snapshot{
                  .operation = Operation::ChannelSnapshot, .session = session, .resource = guild.value("id", "")
              };
              snapshot.channels = parseChannels(guild["channels"]);
              publish(std::move(snapshot));
            }
            if (guild.contains("roles"))
              for (const auto& role : guild["roles"])
                m_roleNames[role.at("id").get<std::string>()] = role.value("name", "Members");
          }
        if (data.contains("private_channels")) {
          Event event{.operation = Operation::PrivateChannels, .session = session};
          event.channels = parsePrivateChannels(data["private_channels"], m_users);
          publish(std::move(event));
        }
        if (data.contains("user_settings_proto")) {
          Event event{.operation = Operation::Layout, .session = session};
          event.layout = parseGuildLayout(Json{{"proto", data["user_settings_proto"]}});
          if (!event.layout.folders.empty() || !event.layout.positions.empty())
            publish(std::move(event));
        } else if (data.contains("user_settings")) {
          Event event{.operation = Operation::Layout, .session = session};
          event.layout = parseGuildLayout(data["user_settings"]);
          if (!event.layout.folders.empty() || !event.layout.positions.empty())
            publish(std::move(event));
        }
      } else if (type == "READY_SUPPLEMENTAL") {
        if (data.contains("lazy_private_channels") && data["lazy_private_channels"].is_array()) {
          Event event{.operation = Operation::PrivateChannelUpsert, .session = session};
          event.channels = parsePrivateChannels(data["lazy_private_channels"], m_users);
          publish(std::move(event));
        }
      } else if (type == "USER_SETTINGS_PROTO_UPDATE") {
        if (data.at("settings").value("type", 0) == 2) {
          Event changed{.operation = Operation::GifFavorites, .session = session, .resource = "changed"};
          publish(std::move(changed));
          publish(Event{.operation = Operation::EmojiPreferences, .session = session, .resource = "changed"});
        }
        if (data.at("settings").value("type", 0) == 1) {
          Event changed{.operation = Operation::AccountSettings, .session = session, .resource = "changed"};
          publish(std::move(changed));
          if (auto presence = parsePresenceSettings(data["settings"])) {
            Event event{.operation = Operation::PresenceChanged, .session = session};
            event.presence = presence;
            publish(std::move(event));
          }
          auto layout = parseGuildLayout(data["settings"]);
          // Partial updates unrelated to guild organization must not reset it.
          if (!layout.folders.empty() || !layout.positions.empty() || !data.value("partial", true)) {
            Event event{.operation = Operation::Layout, .session = session};
            event.layout = std::move(layout);
            publish(std::move(event));
          }
        }
      } else if (type == "RECONNECTING" || type == "CONNECTION_FAILED") {
        Event event{.operation = Operation::Gateway, .session = session};
        if (type == "CONNECTION_FAILED" && data.value("authentication_failed", false)) {
          event.unauthorized = true;
          if (m_authenticatedSession == session || m_restoringSession == session)
            forgetLogin(session);
          m_authenticatedSession = 0;
        }
        event.error = type == "CONNECTION_FAILED" ? "Live connection was rejected. Sign out and reconnect."
                                                  : "Reconnecting… " + data.value("reason", std::string{});
        publish(std::move(event));
      } else if (type == "GUILD_MEMBER_LIST_UPDATE") {
        std::string guild, channel;
        int first;
        {
          std::lock_guard lock(m_mutex);
          guild = m_selectedGuild;
          channel = m_selectedChannel;
          first = m_selectedMemberFirst;
        }
        if (data.value("guild_id", "") != guild || channel.empty())
          return;
        if (m_memberChannel != channel) {
          m_memberChannel = channel;
          m_memberList = {};
        }
        m_memberList.apply(data, m_roleNames, m_users);
        Event event{.operation = Operation::Members, .session = session, .resource = channel};
        event.members = m_memberList.rows();
        event.memberCount = m_memberList.total;
        publish(std::move(event));
        if (data.contains("ops"))
          for (const auto& op : data["ops"]) {
            if (op.value("op", "") == "INVALIDATE"
                && op.contains("range")
                && op["range"].is_array()
                && op["range"].size() == 2
                && op["range"][0].get<int>() <= first + 199
                && op["range"][1].get<int>() >= first) {
              m_gateway->subscribe(guild, channel, first, true);
              break;
            }
          }
      } else if (
          type == "GUILD_EMOJIS_UPDATE" || type == "GUILD_CREATE" || type == "GUILD_UPDATE" || type == "GUILD_DELETE"
      ) {
        const auto guild = data.value("guild_id", data.value("id", std::string{}));
        if (data.contains("emojis") || type == "GUILD_DELETE") {
          Event event{.operation = Operation::GuildEmojis, .session = session, .resource = guild};
          event.emojis = parseGuildEmojis(data.value("emojis", Json::array()), guild);
          publish(std::move(event));
        }
      } else if (
          (type == "CHANNEL_CREATE" || type == "CHANNEL_UPDATE" || type == "CHANNEL_DELETE")
          && data.contains("guild_id")
      ) {
        publish(
            Event{
                .operation = Operation::InvalidateChannels, .session = session, .resource = data.value("guild_id", "")
            }
        );
      } else if (type == "CHANNEL_CREATE" || type == "CHANNEL_UPDATE") {
        const auto channelType = data.value("type", -1);
        if (channelType == 1 || channelType == 3) {
          Event event{.operation = Operation::PrivateChannelUpsert, .session = session};
          event.channels = parsePrivateChannels(Json::array({data}), m_users);
          publish(std::move(event));
        }
      } else if (type == "CHANNEL_DELETE" && !data.contains("guild_id")) {
        Event event{.operation = Operation::PrivateChannelDelete, .session = session};
        event.resource = data.at("id");
        publish(std::move(event));
      } else if (auto reaction = parseReactionUpdate(type, data)) {
        Event event{.operation = Operation::Reaction, .session = session, .resource = reaction->channel};
        event.reaction = std::move(reaction);
        publish(std::move(event));
      } else if (type == "MESSAGE_CREATE") {
        m_readState.message(data, m_self);
        publishReads();
        Event event{
            .operation = Operation::MessageCreate, .session = session, .resource = data.value("channel_id", "")
        };
        event.messages = parseMessages(Json::array({data}));
        publish(std::move(event));
      } else if (type == "MESSAGE_UPDATE") {
        Event event{
            .operation = Operation::MessageUpdate, .session = session, .resource = data.value("channel_id", "")
        };
        event.messageId = data.at("id");
        if (data.contains("content") && data["content"].is_string())
          event.content = data["content"];
        event.patch = parseMessagePatch(data);
        publish(std::move(event));
      } else if (type == "MESSAGE_DELETE" || type == "MESSAGE_DELETE_BULK") {
        Event event{
            .operation = Operation::MessageDelete, .session = session, .resource = data.value("channel_id", "")
        };
        if (type == "MESSAGE_DELETE")
          event.removed.push_back(data.at("id"));
        else
          event.removed = data.at("ids").get<std::vector<std::string>>();
        publish(std::move(event));
      }
    } catch (...) { /* Ignore malformed optional events without exposing account data. */
    }
  }

  void Client::publish(Event event) {
    std::lock_guard lock(m_mutex);
    if (event.session != m_session)
      return;
    if (m_events.size() >= 2048) {
      // Keep bounded UI delivery. REST history can resynchronize the selected channel.
      m_events.erase(m_events.begin());
    }
    m_events.push_back(std::move(event));
    (void)m_wake.signal();
  }

  std::vector<Event> Client::drain() {
    m_wake.drain();
    std::lock_guard lock(m_mutex);
    return std::exchange(m_events, {});
  }

  void Client::run(std::stop_token stop) {
    std::string token;
    std::uint64_t activeSession = 0;
    auto cooldown = std::chrono::steady_clock::now();
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> connection(curl_easy_init(), curl_easy_cleanup);
    while (!stop.stop_requested()) {
      Command command;
      {
        std::unique_lock lock(m_mutex);
        m_ready.wait(lock, stop, [&] { return !m_commands.empty() || activeSession != m_session; });
        if (stop.stop_requested())
          break;
        if (activeSession != m_session) {
          token.clear();
          activeSession = m_session;
        }
        if (m_commands.empty())
          continue;
        command = std::move(m_commands.front());
        m_commands.pop_front();
      }
      if (command.session != m_session
          || (command.operation == Operation::SavePresence && command.presenceRevision != m_presenceRevision))
        continue;
      Event event{.operation = command.operation, .session = command.session, .resource = command.resource};
      if (command.operation == Operation::Acknowledge)
        event.messageId = command.content;
      Transfer transfer{{}, stop, &m_session, command.session, connection.get()};
      event.restoredLogin = command.restoredLogin;
      bool saveCredential = false;
      try {
        if (command.operation == Operation::Connect) {
          token = std::move(command.content);
          if (!auth::validCredential(token))
            throw std::runtime_error("Enter a valid account token.");
          if (command.handoffKey) {
            if (command.handoffKey->size() != 64 || !std::ranges::all_of(*command.handoffKey, [](unsigned char ch) {
                  return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
                }))
              throw std::runtime_error("Invalid browser handoff key");
            const Json handoff{{"key", *command.handoffKey}, {"handoff_token", std::move(token)}};
            auto exchange = request({}, "/auth/handoff/exchange", &handoff, transfer, cooldown);
            if (!exchange) {
              event.error = exchange.error().message;
              publish(std::move(event));
              continue;
            }
            token = exchange->at("token").get<std::string>();
            if (!auth::validCredential(token))
              throw std::runtime_error("Invalid browser session");
          }
        }
        std::string path;
        Json body;
        const char* method = nullptr;
        std::optional<InteractionRequest> interaction;
        switch (command.operation) {
        case Operation::GuildEmojis:
          path = "/guilds/" + command.resource + "/emojis";
          break;
        case Operation::EmojiPreferences:
          path = "/users/@me/settings-proto/2";
          break;
        case Operation::SaveEmojiFavorite: {
          path = "/users/@me/settings-proto/2";
          auto snapshot = request(token, path, nullptr, transfer, cooldown);
          if (!snapshot) {
            event.error = snapshot.error().message;
            event.unauthorized = snapshot.error().unauthorized;
            break;
          }
          body = emojiFavoritePatch(*snapshot, command.resource, command.content == "add");
          method = "PATCH";
          break;
        }
        case Operation::GifSearch:
          path = gifSearchPath(command.resource);
          break;
        case Operation::GifFavorites:
          path = "/users/@me/settings-proto/2";
          break;
        case Operation::SaveGifFavorite: {
          path = "/users/@me/settings-proto/2";
          auto snapshot = request(token, path, nullptr, transfer, cooldown);
          if (!snapshot) {
            event.error = snapshot.error().message;
            event.unauthorized = snapshot.error().unauthorized;
            break;
          }
          const auto value = Json::parse(command.content);
          body = gifFavoritePatch(*snapshot, gifFromJson(value), value.at("add").get<bool>());
          method = "PATCH";
          break;
        }
        case Operation::Interaction:
          if (!command.interaction || !(interaction = interactionRequest(*command.interaction)))
            continue;
          event.interaction = command.interaction;
          path = interaction->path;
          body = interaction->body;
          method = interaction->method.c_str();
          break;
        case Operation::Acknowledge:
          path = "/channels/" + command.resource + "/messages/" + command.content + "/ack";
          body = {{"token", nullptr}, {"manual", false}};
          method = "POST";
          break;
        case Operation::Profile:
        case Operation::Connect:
          path = "/users/@me";
          break;
        case Operation::AccountSettings:
          path = "/users/@me/settings-proto/1";
          break;
        case Operation::SettingsList:
          path = command.resource == "connections" ? "/users/@me/connections"
              : command.resource == "devices"      ? "/auth/sessions"
                                                   : "/oauth2/tokens";
          break;
        case Operation::SaveAccountSettings: {
          path = "/users/@me/settings-proto/1";
          auto snapshot = request(token, path, nullptr, transfer, cooldown);
          if (!snapshot) {
            event.error = snapshot.error().message;
            event.unauthorized = snapshot.error().unauthorized;
            publish(std::move(event));
            continue;
          }
          body = accountSettingsPatch(*snapshot, Json::parse(command.content).get<SettingValues>());
          method = "PATCH";
          break;
        }
        case Operation::SaveProfile:
          path = "/users/@me";
          body = Json::parse(command.content);
          method = "PATCH";
          break;
        case Operation::SavePresence: {
          path = "/users/@me/settings-proto/1";
          auto current = request(token, path, nullptr, transfer, cooldown);
          if (!current) {
            event.error = current.error().message;
            event.unauthorized = current.error().unauthorized;
            publish(std::move(event));
            continue;
          }
          body = presenceSettingsPatch(*current, command.resource, command.customStatus);
          method = "PATCH";
          break;
        }
        case Operation::Channels:
          path = "/guilds/" + command.resource + "/channels";
          break;
        case Operation::PrivateChannels:
          path = "/users/@me/channels";
          break;
        case Operation::Messages:
          path = "/channels/" + command.resource + "/messages?limit=50";
          break;
        case Operation::Send:
          path = "/channels/" + command.resource + "/messages";
          body = messageBody(command.content, command.reply);
          if (!command.uploads.empty()) {
            body["attachments"] = Json::array();
            for (std::size_t i = 0; i < command.uploads.size(); ++i)
              body["attachments"].push_back({{"id", i}, {"filename", command.uploads[i].filename}});
          }
          break;
        default:
          continue;
        }
        auto response = event.error.empty()
            ? request(
                  command.operation == Operation::GifSearch ? std::string{} : token, path,
                  (interaction ? !body.is_null() : command.operation == Operation::Send || method) ? &body : nullptr,
                  transfer, cooldown, command.uploads, method
              )
            : std::expected<Json, Failure>(std::unexpected(Failure{event.error, event.unauthorized}));
        if (!response) {
          event.error = response.error().message;
          event.unauthorized = response.error().unauthorized;
        } else
          switch (command.operation) {
          case Operation::GuildEmojis:
            event.emojis = parseGuildEmojis(*response, command.resource);
            break;
          case Operation::EmojiPreferences:
          case Operation::SaveEmojiFavorite:
            if (response->value("out_of_date", false))
              event.error = "Emoji favorites changed on another device. Please try again.";
            else
              event.emojiPreferences = parseEmojiPreferences(*response);
            break;
          case Operation::GifSearch:
            event.gifs = parseGifs(*response);
            break;
          case Operation::GifFavorites:
          case Operation::SaveGifFavorite:
            if (response->value("out_of_date", false))
              event.error = "Favorite GIFs changed on another device. Please try again.";
            else
              event.gifs = parseGifFavorites(*response);
            break;
          case Operation::Connect: {
            event.account = parseAccount(*response);
            // Establish the live socket while REST bootstraps organization/settings.
            m_gateway->authenticate(command.session, token);
            // Page through servers instead of silently omitting accounts with over 200 guilds.
            std::string after;
            for (;;) {
              auto guilds = request(
                  token, "/users/@me/guilds?limit=200" + (after.empty() ? "" : "&after=" + after), nullptr, transfer,
                  cooldown
              );
              if (!guilds) {
                event.error = guilds.error().message;
                event.unauthorized = guilds.error().unauthorized;
                break;
              }
              auto page = parseGuilds(*guilds);
              const auto count = page.size();
              if (count == 0)
                break;
              const auto next = page.back().id;
              event.guilds.insert(event.guilds.end(), page.begin(), page.end());
              if (count < 200)
                break;
              if (next == after || event.guilds.size() > 10000)
                throw std::runtime_error("Could not finish loading the server list.");
              after = next;
            }
            if (event.error.empty()) {
              // User settings, rather than snowflake order, define the server rail.
              auto settings = request(token, "/users/@me/settings-proto/1", nullptr, transfer, cooldown);
              if (settings) {
                try {
                  event.layout = parseGuildLayout(*settings);
                  event.presence = parsePresenceSettings(*settings);
                } catch (...) {
                }
              }
            }
            break;
          }
          case Operation::Interaction:
            if (command.interaction->kind == InteractionKind::ListReactions)
              event.reactors = parseReactors(*response);
            else if (command.interaction->kind == InteractionKind::EditMessage)
              event.messages = parseMessages(Json::array({*response}));
            else if (command.interaction->kind == InteractionKind::OpenDm)
              event.channels = parsePrivateChannels(Json::array({*response}));
            else if (command.interaction->kind == InteractionKind::UserProfile) {
              event.account = parseAccount(response->at("user"));
              if (response->contains("user_profile") && (*response)["user_profile"].is_object()) {
                const auto& profile = (*response)["user_profile"];
                if (profile.contains("bio") && profile["bio"].is_string())
                  event.account.bio = profile["bio"];
                if (profile.contains("pronouns") && profile["pronouns"].is_string())
                  event.account.pronouns = profile["pronouns"];
              }
            }
            break;
          case Operation::AccountSettings:
          case Operation::SaveAccountSettings:
            if (response->value("out_of_date", false))
              event.error = "Settings changed on another device. Reload and try again.";
            else
              event.settings = parseAccountSettings(*response);
            break;
          case Operation::SettingsList:
            event.settingsEntries = parseSettingsEntries(command.resource, *response);
            break;
          case Operation::Profile:
          case Operation::SaveProfile:
            event.account = parseAccount(*response);
            if (command.operation == Operation::SaveProfile
                && response->contains("token")
                && (*response)["token"].is_string()) {
              const auto replacement = (*response)["token"].get<std::string>();
              if (auth::validCredential(replacement)) {
                token = replacement;
                saveCredential = true;
                m_gateway->authenticate(command.session, token);
              }
            }
            break;
          case Operation::SavePresence:
            if (response->value("out_of_date", false))
              event.error = "Status changed on another device. Please try again.";
            else
              event.presence = parsePresenceSettings(*response);
            break;
          case Operation::Channels:
            event.channels = parseChannels(*response);
            break;
          case Operation::PrivateChannels:
            event.channels = parsePrivateChannels(*response);
            break;
          case Operation::Messages:
            event.messages = parseMessages(*response);
            break;
          case Operation::Send:
            event.messages = parseMessages(Json::array({*response}));
            break;
          default:
            break;
          }
      } catch (const std::exception&) {
        // Do not expose arbitrary JSON/transport exceptions, which can contain account data.
        event.error = "Could not read Discord's response. Reconnect or try again.";
      }
      if (event.unauthorized
          && command.session == m_session
          && (command.restoredLogin || m_authenticatedSession == command.session))
        forgetLogin(command.session);
      if (event.unauthorized || (command.operation == Operation::Connect && !event.error.empty())) {
        token.clear();
        m_authenticatedSession = 0;
        m_gateway->reset();
      } else if (command.operation == Operation::Connect && command.session == m_session) {
        m_authenticatedSession = command.session;
      }
      const bool startGateway =
          command.operation == Operation::Connect && event.error.empty() && command.session == m_session;
      if (command.operation == Operation::SavePresence
          && command.presenceRevision != m_presenceRevision
          && !event.unauthorized)
        continue;
      const auto initialPresence = event.presence.value_or(Presence{});
      const bool remember = (startGateway && !command.restoredLogin) || (saveCredential && event.error.empty());
      publish(std::move(event));
      if (remember)
        saveLogin(command.session, token);
      if (startGateway)
        m_gateway->authenticate(command.session, token, initialPresence);
    }
  }
} // namespace discord
