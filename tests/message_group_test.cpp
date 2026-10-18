#include "app/message_group.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

int main() {
  const auto require = [](bool ok, const char* message) {
    if (!ok)
      throw std::runtime_error(message);
  };
  try {
    for (const bool compact : {false, true}) {
      const float lineHeight = 22;
      const float firstBodyTop = 18;
      const float firstRowHeight = lineHeight + app::messageRowPadding(false, true, compact);
      const float nextRowHeight = lineHeight + app::messageRowPadding(true, true, compact);
      const float firstGap = firstRowHeight - firstBodyTop - lineHeight;
      const float nextGap = nextRowHeight - lineHeight;
      require(firstGap == nextGap, "The first-to-second message gap matches all subsequent gaps in the group");
      require(nextGap == 0, "Consecutive messages use the same line spacing as wrapped lines without an extra row gap");
      require(
          app::messageRowPadding(true, false, compact) > nextGap,
          "Separate groups retain a larger gap than consecutive messages"
      );
    }
    api::Message first{"100", "10", "Lemmy", "First", "2026-10-17T09:41:00.000000+00:00", "11"};
    auto next = first;
    next.id = "101";
    next.content = "Second";
    next.timestamp = "2026-10-17T09:42:00Z";
    require(app::continuesMessageGroup(first, next), "Consecutive ordinary messages share the author header");
    next.timestamp = "2026-10-17T09:47:59.999Z";
    require(app::continuesMessageGroup(first, next), "Short runs can continue just below the seven-minute boundary");
    next.timestamp = "2026-10-17T09:48:00Z";
    require(!app::continuesMessageGroup(first, next), "A seven-minute gap starts a new group");
    next.timestamp = "2026-10-17T11:42:00+02:00";
    require(app::continuesMessageGroup(first, next), "Timestamp offsets normalize before comparing message gaps");
    next.timestamp = "2026-10-17T09:40:59Z";
    require(!app::continuesMessageGroup(first, next), "Out-of-order timestamps never hide an author header");
    for (const auto* value :
         {"", "invalid", "2026-02-30T09:41:00Z", "2026-10-17T24:00:00Z", "2026-10-17T09:41:00.",
          "2026-10-17T09:41:00+99:00", "2026-10-17T09:41:00Zjunk"}) {
      next.timestamp = value;
      require(!app::continuesMessageGroup(first, next), "Invalid or missing timestamps retain a separate header");
    }
    next = first;
    next.timestamp = "2026-10-18T00:00:10Z";
    first.timestamp = "2026-10-17T23:59:50Z";
    require(!app::continuesMessageGroup(first, next), "A calendar-day boundary starts a new group");
    next = first;
    next.authorId = "12";
    require(!app::continuesMessageGroup(first, next), "Identical display names never merge different accounts");
    next = first;
    next.authorId.clear();
    first.authorId.clear();
    require(!app::continuesMessageGroup(first, next), "Unknown authors cannot be grouped by display name alone");
    first.authorId = "11";
    for (int reason = 0; reason < 8; ++reason) {
      next = first;
      switch (reason) {
      case 0:
        next.reply = api::MessageReference{};
        break;
      case 1:
        next.flags = 64;
        break;
      case 2:
        next.type = 7;
        break;
      case 3:
        next.webhookId = "123";
        break;
      case 4:
        next.author = "Another webhook name";
        break;
      case 5:
        next.avatar = "different-avatar";
        break;
      case 6:
        next.channelId = "20";
        break;
      case 7:
        next.applicationId = "123";
        break;
      }
      require(
          !app::continuesMessageGroup(first, next) && !app::continuesMessageGroup(next, first),
          "Replies, ephemeral/system messages and changed identities delimit groups in both directions"
      );
    }
    auto parsed = api::parseMessages(nlohmann::json::parse(R"JSON([
      {"id":"100","channel_id":"10","type":7,"webhook_id":"123","author":{"id":"11","username":"Lemmy"},"content":"Hello"}
    ])JSON"))
                      .front();
    api::parseMessagePatch({{"content", "Edited"}}).apply(parsed);
    require(parsed.type == 7 && parsed.webhookId == "123", "Partial edits retain message type and webhook identity");
    api::parseMessagePatch({{"type", 0}, {"webhook_id", nullptr}}).apply(parsed);
    require(parsed.type == 0 && parsed.webhookId.empty(), "Explicit fields can reset grouping metadata");
    std::cout << "Message grouping boundaries and identity projection passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
