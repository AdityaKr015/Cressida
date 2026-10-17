#pragma once

#include <cstdint>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace discord {

  struct GuildTag {
    std::string guild, tag, badge;
    bool operator==(const GuildTag&) const = default;
  };
  bool validAssetHash(std::string_view value);
  std::optional<GuildTag> parseGuildTag(const nlohmann::json& user);

  struct Account {
    std::string id;
    std::string name;
    std::string avatar;
    std::string username, displayName, bio, pronouns;
    std::optional<int> premiumType;
    std::optional<GuildTag> guildTag;
  };

  struct Presence {
    std::string status = "unknown", text, emojiId, emojiName;
  };
  bool validStatus(std::string_view status);
  std::optional<Presence> parsePresenceSettings(const nlohmann::json& value);
  nlohmann::json presenceSettingsPatch(
      const nlohmann::json& current, const std::string& status, const std::optional<std::string>& customText
  );

  using SettingValues = std::map<std::string, int>;
  SettingValues parseAccountSettings(const nlohmann::json& value);
  bool validSettingChanges(const SettingValues& changes);
  nlohmann::json accountSettingsPatch(const nlohmann::json& snapshot, const SettingValues& changes);
  struct SettingsEntry {
    std::string id, name, detail;
  };
  std::vector<SettingsEntry> parseSettingsEntries(const std::string& section, const nlohmann::json& value);

  struct Guild {
    std::string id;
    std::string name;
    std::string icon;
  };

  struct GuildFolder {
    std::string id;
    std::string name;
    std::uint32_t color = 0x5865F2;
    std::vector<std::string> guildIds;
  };
  struct GuildLayout {
    std::vector<GuildFolder> folders;
    std::vector<std::string> positions;
  };

  struct MemberRow {
    int index = 0;
    Account user;
    std::string status = "offline";
    std::string group;
    int count = -1;
    std::string groupId;
  };
  class MemberList {
  public:
    void apply(
        const nlohmann::json& update, const std::map<std::string, std::string>& roles,
        const std::map<std::string, Account>& users = {}
    );
    std::vector<MemberRow> rows() const;
    int total = 0;

  private:
    std::map<int, MemberRow> m_rows;
    std::map<std::string, int> m_groupCounts;
  };

  struct ForumTag {
    std::string id, name, emojiId, emojiName;
  };
  struct Channel {
    std::string id;
    std::string name;
    int type = 0;
    int position = 0;
    std::string parentId;
    std::string icon;
    std::vector<Account> recipients;
    std::string lastMessageId;
    bool named = false;
    // Optional fields preserve thread metadata across partial Gateway updates.
    std::optional<bool> archived, locked, joined;
    std::string archiveTimestamp;
    std::optional<int> messageCount, threadMemberCount;
    std::vector<ForumTag> availableTags;
    std::optional<std::vector<std::string>> appliedTags;
    bool requireTag = false;
    bool isThread() const { return type == 10 || type == 11 || type == 12; }
    bool isForum() const { return type == 15 || type == 16; }
  };

  struct Media {
    std::string url;
    int width = 0, height = 0;
  };
  struct Attachment {
    std::string id, filename, url, contentType;
    Media image;
  };
  struct EmbedField {
    std::string name, value;
  };
  struct Embed {
    std::string title, description, url, provider, author, footer, type;
    std::uint32_t color = 0x5865F2;
    Media image, thumbnail;
    std::vector<EmbedField> fields;
    Media video;
  };
  struct Upload {
    std::string filename, mime;
    std::vector<unsigned char> bytes;
  };
  constexpr std::size_t maxUploadBytes = 20 * 1024 * 1024;
  bool validUploads(const std::vector<Upload>& uploads);
  // Fixed Discord CDN/proxy and public GIF provider origins, without credentials.
  std::string mediaUrl(std::string_view value);

  struct Reaction {
    std::string id, name;
    int count = 0;
    bool me = false;
    int burstCount = 0;
    bool meBurst = false;
  };
  struct MessageReference {
    std::string message, channel, authorId, author, content;
    bool deleted = false;
    bool mention = false;
  };
  struct ReactionUpdate {
    enum class Kind { Add, Remove, RemoveEmoji, RemoveAll };
    Kind kind = Kind::Add;
    std::string channel, message, user;
    Reaction emoji;
    bool burst = false;
  };
  std::optional<ReactionUpdate> parseReactionUpdate(std::string_view type, const nlohmann::json& data);
  struct Message {
    std::string id;
    std::string channelId;
    std::string author;
    std::string content;
    std::string timestamp;
    std::string authorId;
    std::string avatar;
    std::vector<Attachment> attachments;
    std::vector<Embed> embeds;
    std::map<std::string, std::string> mentions;
    std::optional<MessageReference> reply;
    std::vector<Reaction> reactions;
    std::optional<std::vector<std::string>> memberRoles;
    std::optional<GuildTag> authorTag;
    std::optional<Channel> thread;
    bool hasThread = false;
  };

  struct MessagePatch {
    std::optional<std::string> content;
    std::optional<std::vector<Attachment>> attachments;
    std::optional<std::vector<Embed>> embeds;
    std::optional<std::map<std::string, std::string>> mentions;
    std::optional<std::optional<MessageReference>> reply;
    std::optional<std::vector<Reaction>> reactions;
    std::optional<std::vector<std::string>> memberRoles;
    std::optional<std::optional<GuildTag>> authorTag;
    std::optional<std::optional<Channel>> thread;
    std::optional<bool> hasThread;
    void apply(Message& message) const;
  };
  MessagePatch parseMessagePatch(const nlohmann::json& value);
  void applyReaction(Message& message, const ReactionUpdate& update, const std::string& self);

  // Parse only protocol data; presentation and account lookups stay outside these records.
  Account parseAccount(const nlohmann::json& value);
  std::vector<Guild> parseGuilds(const nlohmann::json& value);
  std::vector<Channel> parseChannels(const nlohmann::json& value);
  std::vector<Channel>
  parsePrivateChannels(const nlohmann::json& value, const std::map<std::string, Account>& users = {});
  std::vector<Message> parseMessages(const nlohmann::json& value);
  bool validSnowflake(const std::string& value);
  std::size_t utf8Length(const std::string& value);
  GuildLayout parseGuildLayout(const nlohmann::json& value);
  std::vector<GuildFolder> arrangeGuilds(const std::vector<Guild>& guilds, const GuildLayout& layout);
  std::string avatarPath(const nlohmann::json& user);
  std::string emojiPath(const std::string& id, bool animated = false);

} // namespace discord
