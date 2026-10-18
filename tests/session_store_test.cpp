#include "app/session_store.h"
#include "core/environment.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace {
  struct EnvironmentOverride {
    std::string name;
    std::optional<std::string> previous;
    EnvironmentOverride(const char* key, const char* value) : name(key) {
      if (const auto* old = std::getenv(key))
        previous = old;
      setenv(key, value, 1);
    }
    ~EnvironmentOverride() {
      if (previous)
        setenv(name.c_str(), previous->c_str(), 1);
      else
        unsetenv(name.c_str());
    }
  };
} // namespace

int main() {
  char directory[] = "/tmp/cressida-state-XXXXXX";
  if (!mkdtemp(directory))
    return 1;
  const auto path = std::filesystem::path(directory) / "session.json";
  auto require = [](bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  };
  try {
    {
      app::SessionStore store(path);
      require(!store.load("123"), "First login must not invent a previous server");
      store.save("123", {"10", "20", 1.2F});
      store.save("123", {"11", "21", 1.2F});
      store.save("456", {"", "30", 0.8F});
      store.savePreferences("123", {"midnight", 18, true, false, true});
      store.save("123", {"10", "20", 1.2F});
      store.savePreferences("789", {"light", 16});
    }
    app::SessionStore restored(path);
    require(
        restored.frequentReactions("123") == std::vector<std::string>{"❤️", "👍", "😂", "🔥"},
        "New accounts get familiar quick reaction defaults"
    );
    restored.recordReaction("123", "🎉");
    restored.recordEmoji("123", "heart");
    restored.recordEmoji("123", "123456789012345678");
    restored.recordEmoji("123", "heart");
    require(
        app::SessionStore(path).frequentEmojis("123").front() == "heart"
            && app::SessionStore(path).frequentEmojis("456").empty(),
        "Emoji usage survives restart and remains account-scoped"
    );
    restored.recordReaction("123", "🎉");
    restored.recordReaction("123", "wave:123");
    restored.recordReaction("123", "../invalid");
    require(restored.frequentReactions("123").front() == "🎉", "Quick reactions rank successful local usage");
    require(restored.frequentReactions("456").front() == "❤️", "Reaction history stays account-scoped");
    restored.savePreferences("123", {"midnight", 18, true, false, true});
    restored.save("123", {"10", "20", 1.2F});
    require(
        app::SessionStore(path).frequentReactions("123").front() == "🎉",
        "Channel and preference saves preserve reaction usage across restarts"
    );
    const auto first = restored.load("123"), second = restored.load("456");
    require(
        first && first->guild == "10" && first->channel == "20" && std::abs(first->scale - 1.2F) < 0.01F,
        "Restore selected server/channel and zoom after restarting"
    );
    require(second && second->guild.empty() && second->channel == "30", "Remember DMs independently for each account");
    const auto preferences = restored.preferences("123");
    require(preferences.autoplayGifs, "GIF autoplay defaults on for existing accounts");
    auto pausedGifs = preferences;
    require(preferences.fontFamily.empty(), "Existing accounts use the system font by default");
    pausedGifs.fontFamily = "École Mono";
    require(!preferences.emojiImageLinks, "Emoji image links default off for existing accounts");
    require(
        !preferences.logDeletedMessages && !preferences.logEditHistory,
        "Both logger options default off for existing accounts"
    );
    pausedGifs.logDeletedMessages = true;
    pausedGifs.emojiImageLinks = true;
    pausedGifs.autoplayGifs = false;
    require(preferences.roleColors && preferences.animateRoleColors, "Role colors and hover animation default on");
    pausedGifs.roleColors = false;
    pausedGifs.animateRoleColors = false;
    require(preferences.showGuildTags, "Chat server tags default on for existing accounts");
    pausedGifs.showGuildTags = false;
    pausedGifs.channelSidebarWidth = 372;
    restored.savePreferences("123", pausedGifs);
    restored.save("123", {"10", "20", 1.2F});
    require(
        app::SessionStore(path).preferences("123").fontFamily == "École Mono"
            && app::SessionStore(path).preferences("456").fontFamily.empty(),
        "Installed font preference survives navigation saves and remains account-scoped"
    );
    require(
        app::SessionStore(path).preferences("123").channelSidebarWidth == 372
            && app::SessionStore(path).preferences("456").channelSidebarWidth == 248,
        "Sidebar width survives navigation saves and stays account-scoped"
    );
    require(
        app::SessionStore(path).preferences("123").logDeletedMessages
            && !app::SessionStore(path).preferences("123").logEditHistory
            && !app::SessionStore(path).preferences("456").logDeletedMessages
            && !app::SessionStore(path).preferences("456").logEditHistory,
        "Deletion-only logging persists independently for each account"
    );
    auto historyOnly = pausedGifs;
    historyOnly.logDeletedMessages = false;
    historyOnly.logEditHistory = true;
    restored.savePreferences("123", historyOnly);
    require(
        !app::SessionStore(path).preferences("123").logDeletedMessages
            && app::SessionStore(path).preferences("123").logEditHistory,
        "History-only logging persists independently of deletion logging"
    );
    pausedGifs.logEditHistory = true;
    restored.savePreferences("123", pausedGifs);
    require(
        app::SessionStore(path).preferences("123").logDeletedMessages
            && app::SessionStore(path).preferences("123").logEditHistory,
        "Both logger options can be enabled together"
    );
    auto invalidWidth = pausedGifs;
    invalidWidth.channelSidebarWidth = 900;
    restored.savePreferences("789", invalidWidth);
    require(app::SessionStore(path).preferences("789").channelSidebarWidth == 480, "Sidebar width is bounded on disk");
    restored.savePreferences("789", {"light", 16});
    require(
        !app::SessionStore(path).preferences("123").showGuildTags
            && app::SessionStore(path).preferences("456").showGuildTags,
        "Chat tag visibility persists independently for each account"
    );
    require(
        app::SessionStore(path).preferences("123").emojiImageLinks
            && !app::SessionStore(path).preferences("456").emojiImageLinks,
        "Emoji image links persist only for the account that enabled them"
    );
    require(
        !app::SessionStore(path).preferences("123").autoplayGifs
            && app::SessionStore(path).preferences("456").autoplayGifs,
        "GIF autoplay persists per account"
    );
    require(
        !app::SessionStore(path).preferences("123").roleColors
            && !app::SessionStore(path).preferences("123").animateRoleColors
            && app::SessionStore(path).preferences("456").roleColors
            && app::SessionStore(path).preferences("456").animateRoleColors,
        "Role colors and animation preferences persist independently per account"
    );
    require(
        preferences.theme == "midnight"
            && preferences.messageSize == 18
            && preferences.compact
            && !preferences.showEmbeds
            && preferences.showImages,
        "Selection saves preserve account appearance preferences"
    );
    require(
        restored.preferences("456").theme == "dark" && restored.preferences("789").theme == "light",
        "Store preferences independently, including accounts without a previous channel"
    );
    require(
        restored.channelForGuild("123", "10") == "20" && restored.channelForGuild("123", "11") == "21",
        "Keep the last channel independently for every server"
    );
    require(
        restored.channelForGuild("456", "") == "30" && restored.channelForGuild("123", "99").empty(),
        "DM history and account histories do not leak across servers"
    );
    struct stat state{};
    for (int i = 1; i <= 75; ++i)
      restored.recordReaction("123", "sample:" + std::to_string(i));
    require(
        restored.frequentReactions("123").size() <= 68, "Reaction usage is bounded to 64 records plus missing defaults"
    );
    require(
        restored.load("456")->channel == "30" && restored.preferences("123").theme == "midnight",
        "Pruning emoji history preserves other account data"
    );
    require(
        stat(path.c_str(), &state) == 0 && (state.st_mode & 0777) == 0600,
        "Private UI state must be owner-readable only"
    );
    auto invalidFont = pausedGifs;
    invalidFont.fontFamily = "Bad\nFamily";
    restored.savePreferences("789", invalidFont);
    require(
        restored.preferences("789").fontFamily.empty() && restored.preferences("789").theme == "midnight",
        "Invalid font names fall back without dropping other preferences"
    );
    invalidFont.fontFamily.assign(513, 'a');
    restored.savePreferences("789", invalidFont);
    require(restored.preferences("789").fontFamily.empty(), "Oversized font names fall back to the system default");
    pausedGifs.fontFamily.clear();
    restored.savePreferences("123", pausedGifs);
    require(restored.preferences("123").fontFamily.empty(), "System default font selection persists");
    std::ofstream(path) << "broken JSON";
    require(!restored.load("123"), "Ignore damaged UI preferences");
    {
      const auto root = std::filesystem::path(directory) / "migration";
      EnvironmentOverride stateHome("XDG_STATE_HOME", root.c_str());
      const auto oldPath = root / "discord-cpp/session.json";
      const auto newPath = root / "cressida/session.json";
      app::SessionStore legacy(oldPath);
      legacy.save("123", {"10", "20", 1.2F});
      legacy.save("123", {"11", "21", 1.2F});
      legacy.savePreferences("123", {"midnight", 18, true, false, true});
      legacy.save("456", {"", "30", 0.8F});
      app::SessionStore migrated;
      require(migrated.load("123")->channel == "21", "Read the old name's last channel before migration");
      require(migrated.preferences("123").theme == "midnight", "Read old appearance preferences");
      require(!std::filesystem::exists(newPath), "Reading old state must not create new files");
      migrated.save("123", {"10", "22", 1.1F});
      require(std::filesystem::exists(newPath), "Saving migrates to the Cressida directory");
      require(legacy.load("123")->channel == "21", "Migration leaves the original file intact");
      require(migrated.channelForGuild("123", "11") == "21", "Migration preserves other servers");
      require(migrated.load("456")->channel == "30", "Migration preserves other accounts");
      require(migrated.preferences("123").theme == "midnight", "Migration preserves display preferences");
      require(stat(newPath.c_str(), &state) == 0 && (state.st_mode & 0777) == 0600, "Migrated state remains private");
      legacy.save("456", {"", "31", 0.8F});
      require(migrated.load("456")->channel == "30", "The new state takes precedence over legacy changes");
      std::ofstream(newPath) << "broken JSON";
      require(!migrated.load("123"), "Damaged new state must not resurrect legacy selections");
    }
    {
      EnvironmentOverride oldName("DISCORD_CPP_TEST_OVERRIDE", "old");
      EnvironmentOverride newName("CRESSIDA_TEST_OVERRIDE", "new");
      require(
          std::string(cressida::environment("CRESSIDA_TEST_OVERRIDE", "DISCORD_CPP_TEST_OVERRIDE")) == "new",
          "Cressida overrides take precedence"
      );
      unsetenv("CRESSIDA_TEST_OVERRIDE");
      require(
          std::string(cressida::environment("CRESSIDA_TEST_OVERRIDE", "DISCORD_CPP_TEST_OVERRIDE")) == "old",
          "Legacy overrides remain supported"
      );
      setenv("CRESSIDA_TEST_OVERRIDE", "", 1);
      require(
          *cressida::environment("CRESSIDA_TEST_OVERRIDE", "DISCORD_CPP_TEST_OVERRIDE") == '\0',
          "An empty override must not resurrect the legacy value"
      );
    }
    std::filesystem::remove_all(directory);
  } catch (const std::exception& error) {
    std::filesystem::remove_all(directory);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
