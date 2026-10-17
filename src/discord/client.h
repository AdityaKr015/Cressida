#pragma once

#include "auth/credentials.h"
#include "core/process/wake_event.h"
#include "discord/actions.h"
#include "discord/gateway.h"
#include "discord/models.h"
#include "discord/read_state.h"
#include "discord/user_directory.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>

namespace discord {

  enum class Operation {
    Connect,
    ReadState,
    ChannelSnapshot,
    InvalidateChannels,
    Acknowledge,
    AccountSettings,
    SaveAccountSettings,
    SettingsList,
    Profile,
    SaveProfile,
    SavePresence,
    PresenceChanged,
    UserState,
    Interaction,
    Reaction,
    AccountUpdate,
    Channels,
    Messages,
    Send,
    Layout,
    Members,
    MessageCreate,
    MessageUpdate,
    MessageDelete,
    Gateway,
    PrivateChannels,
    PrivateChannelUpsert,
    PrivateChannelDelete,
    Credentials
  };

  struct Event {
    Operation operation = Operation::Connect;
    std::uint64_t session = 0;
    std::string resource;
    std::string error;
    bool unauthorized = false;
    Account account;
    std::vector<Guild> guilds;
    std::vector<Channel> channels;
    std::vector<Message> messages;
    GuildLayout layout;
    std::vector<MemberRow> members;
    int memberCount = 0;
    std::vector<std::string> removed;
    std::string messageId;
    std::optional<std::string> content;
    bool live = false;
    MessagePatch patch;
    std::optional<Presence> presence;
    std::optional<SettingValues> settings;
    std::vector<SettingsEntry> settingsEntries;
    std::map<std::string, std::string> unread;
    UserUpdates users;
    std::optional<discord::Interaction> interaction;
    std::optional<ReactionUpdate> reaction;
    std::vector<Account> reactors;
    bool noSavedLogin = false, restoredLogin = false;
    std::optional<auth::CredentialOperation> credentialOperation;
  };

  // One cancellable network worker. No rendering or window-system dependency.
  class Client {
  public:
    explicit Client(
        std::unique_ptr<auth::CredentialBackend> credentials = {}, std::filesystem::path disabledLogin = {}
    );
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    std::uint64_t connect(std::string token);
    std::uint64_t connectHandoff(std::string key, std::string handoffToken);
    std::uint64_t restoreLogin();
    std::uint64_t logout();
    std::uint64_t disconnect();
    bool channels(const std::string& guildId);
    bool privateChannels();
    bool acknowledge(const std::string& channel, const std::string& message);
    bool messages(const std::string& channelId);
    bool send(
        const std::string& channelId, std::string content, std::vector<Upload> uploads = {},
        std::optional<MessageReference> reply = {}
    );
    bool interact(discord::Interaction action);
    bool profile();
    bool accountSettings();
    bool saveAccountSettings(SettingValues changes);
    bool settingsList(const std::string& section);
    bool saveProfile(const Account& profile);
    bool setPresence(std::string status, std::optional<std::string> customText = {});
    void subscribe(const std::string& guild, const std::string& channel, int first = 0);
    std::vector<Event> drain();
    int eventFd() const noexcept { return m_wake.fd(); }

  private:
    struct Command {
      Operation operation;
      std::uint64_t session;
      std::string resource;
      std::string content;
      std::optional<std::string> handoffKey;
      std::vector<Upload> uploads;
      std::optional<std::string> customStatus;
      std::uint64_t presenceRevision = 0;
      std::optional<MessageReference> reply;
      std::optional<discord::Interaction> interaction;
      bool restoredLogin = false;
    };
    std::uint64_t beginConnect(std::string credential, std::optional<std::string> handoffKey);
    bool enqueue(Command command);
    void run(std::stop_token stop);
    void publish(Event event);
    void saveLogin(std::uint64_t session, const std::string& token);
    void forgetLogin(std::uint64_t session);
    void gatewayEvent(std::uint64_t session, const std::string& type, const nlohmann::json& data);

    process::WakeEvent m_wake;
    std::atomic<std::uint64_t> m_session{0};
    std::atomic<std::uint64_t> m_authenticatedSession{0};
    std::atomic<std::uint64_t> m_restoringSession{0};
    std::atomic<std::uint64_t> m_presenceRevision{0};
    std::mutex m_mutex;
    std::condition_variable_any m_ready;
    std::deque<Command> m_commands;
    std::vector<Event> m_events;
    std::unique_ptr<Gateway> m_gateway;
    std::unique_ptr<auth::CredentialStore> m_credentials;
    std::string m_selectedGuild, m_selectedChannel;
    int m_selectedMemberFirst = 0;
    // These caches belong exclusively to the Gateway worker.
    std::uint64_t m_memberSession = 0;
    std::string m_memberChannel;
    MemberList m_memberList;
    discord::ReadState m_readState;
    UserDirectory m_userDirectory;
    std::string m_self;
    std::map<std::string, std::string> m_roleNames;
    std::map<std::string, Account> m_users;
    // Declared last so the worker joins before any state it uses is destroyed.
    std::jthread m_worker;
  };

} // namespace discord
