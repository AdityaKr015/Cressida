#include "app/system_message.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

int main() {
  const auto require = [](bool ok, const char* reason) {
    if (!ok)
      throw std::runtime_error(reason);
  };
  try {
    const auto messages = api::parseMessages(
        nlohmann::json::array(
            {{{"id", "100"},
              {"channel_id", "10"},
              {"type", 7},
              {"content", ""},
              {"timestamp", "2026-10-17T15:22:00.000Z"},
              {"author", {{"id", "11"}, {"username", "Maya"}}},
              {"reactions", nlohmann::json::array({{{"count", 1}, {"emoji", {{"name", "❤️"}}}}})}}}
        )
    );
    auto message = messages.front();
    require(message.type == 7 && message.content.empty(), "REST/Gateway joins retain empty original content");
    require(message.reactions.size() == 1, "Join messages retain reaction metadata");
    const auto text = app::joinNoticeText(message, "Maya");
    message.timestamp = "2026-10-17T17:22:00.000+02:00";
    require(app::joinNoticeText(message, "Maya") == text, "Equivalent timestamp zones select the same welcome variant");
    std::set<std::string> variants;
    for (int i = 0; i < 13; ++i) {
      message.timestamp = "2026-10-17T15:22:00." + std::string(i < 10 ? "00" : "0") + std::to_string(i) + "Z";
      variants.insert(app::joinNoticeText(message, "Maya"));
      require(
          app::joinNoticeText(message, "Maya") == app::joinNoticeText(message, "Maya"),
          "Variants never randomize on redraw"
      );
    }
    require(variants.size() == 13, "Timestamp milliseconds select all thirteen stable welcome variants");
    const auto time = app::messageTime(message.timestamp)->time_since_epoch().count();
    message.id = std::to_string((static_cast<std::uint64_t>(time) - 1420070400000ULL) << 22);
    const auto fromTime = app::joinNoticeText(message, "Maya");
    message.timestamp.clear();
    require(
        app::joinNoticeText(message, "Maya") == fromTime, "Missing timestamps fall back to the snowflake creation time"
    );
    require(
        app::joinNoticeText(message, "<b>Literal 🦊</b>").find("<b>Literal 🦊</b>") != std::string::npos,
        "Welcome formatting preserves names verbatim before escaped application rendering"
    );
    api::parseMessagePatch({{"reactions", nlohmann::json::array()}}).apply(message);
    require(
        message.type == 7 && message.content.empty() && message.reactions.empty(),
        "Partial join edits retain type/content and allow reaction clearing"
    );
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
