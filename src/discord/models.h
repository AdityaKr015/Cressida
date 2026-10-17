#pragma once

#include <cstdint>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace discord {

  struct Account {
    std::string id;
    std::string name;
    std::string avatar;
    std::string username, displayName, bio, pronouns;
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
    int count = 0;
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
  };
  struct Upload {
    std::string filename, mime;
    std::vector<unsigned char> bytes;
  };
  constexpr std::size_t maxUploadBytes = 20 * 1024 * 1024;
  bool validUploads(const std::vector<Upload>& uploads);
  // Only these Discord HTTPS origins may be fetched automatically, without credentials.
  std::string mediaUrl(std::string_view value);

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
  };

  struct MessagePatch {
    std::optional<std::string> content;
    std::optional<std::vector<Attachment>> attachments;
    std::optional<std::vector<Embed>> embeds;
    std::optional<std::map<std::string, std::string>> mentions;
    void apply(Message& message) const;
  };
  MessagePatch parseMessagePatch(const nlohmann::json& value);

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
  std::string emojiPath(const std::string& id);

} // namespace discord
