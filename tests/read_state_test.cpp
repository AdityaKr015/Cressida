#include "discord/models.h"
#include "discord/read_state.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
int main() {
  using nlohmann::json;
  auto require = [](bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  };
  try {
    discord::ReadState state;
    state.ready(
        json{
            {"guilds",
             json::array(
                 {{{"id", "1"},
                   {"channels",
                    json::array(
                        {{{"id", "10"}, {"last_message_id", "100"}}, {{"id", "11"}, {"last_message_id", "99"}}}
                    )}}}
             )},
            {"private_channels", json::array({{{"id", "20"}, {"last_message_id", "900"}}})},
            {"read_state",
             {{"entries",
               json::array(
                   {{{"id", "10"}, {"last_message_id", "99"}},
                    {{"id", "11"}, {"last_message_id", "99"}},
                    {{"id", "20"}, {"last_message_id", "899"}},
                    {{"id", "77"}, {"read_state_type", 2}, {"mention_count", 1}}}
               )}}}
        }
    );
    require(
        state.unread() == std::map<std::string, std::string>{{"10", "1"}, {"20", ""}},
        "Initial unread channels compare numeric snowflakes; ignore non-channel states"
    );
    state.acknowledge(json{{"channel_id", "10"}, {"message_id", "100"}});
    require(!state.unread().contains("10"), "Another client's read acknowledgement clears channel state");
    state.channels(json::array({{{"id", "10"}, {"last_message_id", "101"}}}), "1");
    require(state.unread().contains("10"), "Passive channel updates mark new messages unread");
    state.message(json{{"channel_id", "20"}, {"id", "901"}, {"author", {{"id", "2"}}}}, "2");
    require(!state.unread().contains("20"), "Your own DM message acknowledges prior unread messages");
    state.acknowledge(json{{"channel_id", "20"}, {"message_id", "899"}});
    require(!state.unread().contains("20"), "Late acknowledgements cannot rewind normal read state");
    state.acknowledge(json{{"channel_id", "20"}, {"message_id", "899"}, {"manual", true}});
    require(state.unread().contains("20"), "Explicit mark-unread can rewind read state");
    state.ready(json::object());
    require(state.unread().empty(), "Fresh login replaces previous account read state");
    auto entries = discord::parseSettingsEntries(
        "devices",
        json{
            {"user_sessions",
             json::array(
                 {{{"id_hash", "secret"},
                   {"client_info", {{"os", "Linux"}, {"platform", "Discord Client"}, {"location", "private"}}},
                   {"approx_last_used_time", "2026-10-17"}}}
             )}
        }
    );
    require(
        entries.size() == 1
            && entries[0].name == "Linux · Discord Client"
            && entries[0].id.empty()
            && entries[0].detail == "2026-10-17",
        "Device list projects display fields without session credentials or location"
    );
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
