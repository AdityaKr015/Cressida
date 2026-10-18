#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace app {
  struct Preferences {
    std::string theme = "dark";
    float messageSize = 14;
    bool compact = false, showEmbeds = true, showImages = true;
    bool autoplayGifs = true;
    bool emojiImageLinks = false;
    bool roleColors = true, animateRoleColors = true;
    bool showGuildTags = true;
    float channelSidebarWidth = 248;
    bool logDeletedMessages = false, logEditHistory = false;
    std::string fontFamily;
    std::string colorTheme = "cressida";
    bool reducedMotion = false, animateEmoji = true, animateAvatars = true;
    int saturation = 100, spoilerMode = 0, roleColorMode = 0;
    bool developerMode = false, convertEmoticons = true, showReactions = true, showRoleIcons = true;
    int messageGroupSpacing = 16;
    bool desktopNotifications = true, notificationSounds = true;
    int notificationPrivacy = 0;
    std::map<std::string, std::string> keybinds;
  };
  struct LastChannel {
    std::string guild, channel;
    float scale = 1;
  };
  struct ChannelListPreferences {
    bool hideMuted = false;
    std::vector<std::string> pinned;
  };
  // UI state only, keyed by account ID. Never stores tokens or passwords.
  class SessionStore {
  public:
    explicit SessionStore(std::filesystem::path path = {});
    std::optional<LastChannel> load(const std::string& account) const;
    void save(const std::string& account, const LastChannel& channel) const;
    std::string channelForGuild(const std::string& account, const std::string& guild) const;
    Preferences preferences(const std::string& account) const;
    // Only color mode/palette are shared with the signed-out UI.
    Preferences startupPreferences() const;
    void saveAppearance(const Preferences& preferences) const;
    ChannelListPreferences channelList(const std::string& account, const std::string& guild) const;
    void saveChannelList(
        const std::string& account, const std::string& guild, const ChannelListPreferences& preferences
    ) const;
    void savePreferences(const std::string& account, const Preferences& preferences) const;
    std::vector<std::string> frequentReactions(const std::string& account) const;
    void recordReaction(const std::string& account, const std::string& emoji) const;
    std::vector<std::string> frequentEmojis(const std::string& account) const;
    void recordEmoji(const std::string& account, const std::string& key) const;

  private:
    std::filesystem::path m_path, m_legacyPath;
  };
} // namespace app
