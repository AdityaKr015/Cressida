#include "discord/models.h"

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
    // Independent fixture encoded with Python struct.pack('<Q') and base64.
    const auto layout = discord::parseGuildLayout(
        json{
            {"settings",
             "ck8KJwoQLAEAAAAAAABkAAAAAAAAABICCGQaCQoHRnJpZW5kcyIECLPERAoKCgjIAAAAAAAAABIYyAAAAAAAAABkAAAAAAAAACwBAAAAA"
             "AAA8gEHaWdub3JlZA=="}
        }
    );
    require(
        layout.folders.size() == 2 && layout.folders[0].id == "100" && layout.folders[0].name == "Friends",
        "Decode folder wrappers and names"
    );
    require(
        layout.folders[0].color == 0x112233 && layout.folders[0].guildIds == std::vector<std::string>{"300", "100"},
        "Decode packed fixed64 IDs without numeric precision loss"
    );
    require(
        layout.positions == std::vector<std::string>{"200", "100", "300"}, "Decode legacy ordering alongside folders"
    );
    auto order = discord::arrangeGuilds({{"100", "A"}, {"200", "B"}, {"300", "C"}, {"400", "D"}}, layout);
    require(
        order.size() == 3
            && order[0].guildIds == std::vector<std::string>{"300", "100"}
            && order[1].guildIds[0] == "200"
            && order[2].guildIds[0] == "400",
        "Follow saved folder order and append newly joined servers"
    );
    auto duplicate = layout;
    duplicate.folders.push_back({"101", "Duplicates", 0, {"100", "999"}});
    require(
        discord::arrangeGuilds({{"100", "A"}, {"200", "B"}, {"300", "C"}}, duplicate).size() == 2,
        "Ignore duplicate and departed server references"
    );
    for (const auto* invalid : {"!!!!", "cgUK", "//8=", "cgEK"}) {
      bool rejected = false;
      try {
        (void)discord::parseGuildLayout(json{{"proto", invalid}});
      } catch (...) {
        rejected = true;
      }
      require(rejected, "Reject malformed or truncated settings");
    }
    auto legacy = discord::parseGuildLayout(
        json{
            {"guild_folders", json::array({{{"id", nullptr}, {"guild_ids", json::array({"200"})}}})},
            {"guild_positions", json::array({"300", "100"})}
        }
    );
    require(
        discord::arrangeGuilds({{"100", "A"}, {"200", "B"}, {"300", "C"}}, legacy)[0].guildIds[0] == "200",
        "Accept legacy settings without inventing folders"
    );
    require(
        discord::avatarPath(json{{"id", "123"}, {"avatar", "a_abc"}}) == "/avatars/123/a_abc.png?size=64",
        "Request the static frame of animated avatars"
    );
    require(
        discord::avatarPath(json{{"id", "123"}, {"avatar", nullptr}}) == "/embed/avatars/0.png",
        "Use Discord default avatars for accounts without custom images"
    );
    require(discord::emojiPath("1/../2").empty(), "CDN resources must use validated identifiers");
    discord::MemberList members;
    const json alice = {
        {"member",
         {{"user", {{"id", "1"}, {"username", "alice"}}}, {"nick", "Alice"}, {"presence", {{"status", "online"}}}}}
    };
    const json bob = {{"member", {{"user_id", "2"}, {"presence", {{"status", "idle"}}}}}};
    members.apply(
        json{
            {"member_count", 100000},
            {"groups", json::array({{{"id", "9"}, {"count", 2}}})},
            {"ops",
             json::array(
                 {{{"op", "SYNC"},
                   {"range", json::array({0, 99})},
                   {"items", json::array({{{"group", {{"id", "9"}, {"count", 2}}}}, alice, bob})}}}
             )}
        },
        {{"9", "Team"}}, {{"2", {"2", "Bob", ""}}}
    );
    auto rows = members.rows();
    require(
        rows.size() == 3 && rows[0].group == "Team" && rows[1].user.name == "Alice" && rows[2].user.name == "Bob",
        "Resolve role headers, nicknames and deduplicated users"
    );
    require(
        rows[1].status == "online" && rows[2].status == "idle" && members.total == 3,
        "Preserve member presence and list indices"
    );
    members.apply(
        json{
            {"ops", json::array({{{"op", "DELETE"}, {"index", 1}}, {{"op", "INSERT"}, {"index", 2}, {"item", alice}}})}
        },
        {}
    );
    rows = members.rows();
    require(rows[1].user.name == "Bob" && rows[2].user.name == "Alice", "Apply member list insert/delete index shifts");
    members.apply(json{{"ops", json::array({{{"op", "INVALIDATE"}, {"range", json::array({1, 2})}}})}}, {});
    require(members.rows().size() == 1, "Invalidate stale subscribed ranges");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
