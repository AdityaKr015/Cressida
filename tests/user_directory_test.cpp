#include "api/user_directory.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
  void require(bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  }
} // namespace

int main() {
  using nlohmann::json;
  try {
    api::UserDirectory gateway, view;
    auto dispatch = [&](const auto& type, const auto& data) { view.apply(gateway.consume(type, data)); };
    dispatch(
        "READY",
        json{
            {"relationships",
             {{"entries", json::array({{{"id", "7"}, {"type", 1}, {"nickname", "My friend 🌿"}}})}, {"version", 1}}},
            {"guilds", json::array({{{"id", "10"}}})},
            {"merged_presences", {{"guilds", json::array({json::array({{{"user_id", "8"}, {"status", "online"}}})})}}}
        }
    );
    require(view.name("7", "Server name") == "My friend 🌿", "Personal aliases take priority over guild/global names");
    require(view.name("8", "Original") == "Original", "Unaliased users preserve their original name");
    require(
        view.status("8") == "online" && view.status("7") == "offline",
        "Merged guild presence and absent offline presence initialize DMs"
    );
    dispatch(
        "READY_SUPPLEMENTAL",
        json{{"merged_presences", {{"friends", json::array({{{"user_id", "7"}, {"status", "dnd"}}})}}}}
    );
    require(
        view.status("7") == "dnd" && view.name("7", "Original") == "My friend 🌿",
        "Supplemental presences merge without clearing aliases"
    );
    dispatch("PRESENCE_UPDATE", json{{"user", {{"id", "7"}}}, {"status", "idle"}});
    require(view.status("7") == "idle", "Live presence updates work with ID-only partial user objects");
    dispatch("PRESENCE_UPDATE", json{{"user_id", "7"}, {"guild_id", "10"}, {"status", "offline"}});
    require(view.status("7") == "idle", "Guild-specific presence must not override overall friend presence in DMs");
    dispatch("PRESENCE_UPDATE", json{{"user_id", "8"}, {"guild_id", "11"}, {"status", "offline"}});
    require(view.status("8") == "online", "One guild going offline must not hide another known online presence");
    dispatch("PRESENCE_UPDATE", json{{"user_id", "8"}, {"status", "offline"}});
    require(view.status("8") == "offline", "An explicit global offline update overrides guild fallbacks");
    dispatch("PRESENCE_UPDATE", json{{"user_id", "7"}, {"status", nullptr}});
    dispatch("RELATIONSHIP_UPDATE", json{{"id", "7"}, {"type", 1}});
    require(
        view.status("7") == "idle" && view.name("7", "Original") == "My friend 🌿",
        "Absent or malformed optional fields do not clear prior state"
    );
    dispatch("RELATIONSHIP_UPDATE", json{{"id", "7"}, {"nickname", nullptr}});
    require(
        view.name("7", "Server name") == "Server name", "Clearing an alias immediately restores the current fallback"
    );
    dispatch("RELATIONSHIP_ADD", json{{"id", "7"}, {"nickname", "Changed"}});
    require(view.name("7", "Original") == "Changed", "Relationship additions update existing conversations");
    dispatch("RELATIONSHIP_REMOVE", json{{"id", "7"}});
    require(view.name("7", "Original") == "Original", "Relationship removal drops aliases without losing presence");
    require(view.status("7") == "idle", "Nickname changes must preserve presence state");
    dispatch("RELATIONSHIP_UPDATE", json{{"id", "7"}, {"nickname", "<b>literal</b> **name**"}});
    require(
        view.name("7", "Original") == "<b>literal</b> **name**", "Display aliases remain raw data for the UI to escape"
    );
    dispatch("RESUMED", json::object());
    require(view.status("7") == "idle", "Resume retains state until changed by a live event");
    dispatch("READY", json{{"relationships", json::array()}});
    require(
        view.name("7", "Original") == "Original" && view.status("7") == "offline",
        "A fresh READY clears stale or previous-account state"
    );
    dispatch(
        "READY",
        json{
            {"relationships", json::array({{{"id", "9"}, {"nickname", "Legacy"}}})},
            {"presences", json::array({{{"user", {{"id", "9"}}}, {"status", "online"}}})}
        }
    );
    require(
        view.name("9", "Original") == "Legacy" && view.status("9") == "online",
        "Support non-versioned relationships and legacy bootstrap presences"
    );
    dispatch("PRESENCE_UPDATE", json{{"user_id", "9"}, {"status", "invisible"}});
    require(view.status("9") == "offline", "Invisible remote presence is displayed as offline");
    dispatch(
        "READY",
        json{
            {"relationships", json::array({{{"id", "7"}, {"type", 2}, {"nickname", nullptr}}})},
            {"user_guild_settings",
             {{"entries",
               json::array(
                   {{{"guild_id", nullptr},
                     {"channel_overrides", json::array({{{"channel_id", "10"}, {"muted", true}}})}}}
               )}}}
        }
    );
    require(
        view.relationship("7") == 2 && view.muted("10"), "Import account block state and private conversation mutes"
    );
    dispatch(
        "USER_GUILD_SETTINGS_UPDATE",
        json{{"channel_overrides", json::array({{{"channel_id", "10"}, {"muted", false}}})}}
    );
    dispatch("RELATIONSHIP_REMOVE", json{{"id", "7"}});
    require(!view.muted("10") && view.relationship("7") == 0, "Live unblock and unmute updates clear menu state");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
