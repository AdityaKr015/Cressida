#include "app/unread_timeline.h"

#include <iostream>
#include <stdexcept>

int main() {
  auto require = [](bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  };
  try {
    app::UnreadTimeline timeline;
    std::vector<discord::Message> messages(4);
    for (std::size_t i = 0; i < messages.size(); ++i)
      messages[i].id = std::to_string(98 + i);
    timeline.open("10");
    require(timeline.waiting() && timeline.positionPending(), "Wait for READY before positioning or acknowledging");
    timeline.snapshot({{"10", {"99", "101"}}, {"20", {"100", "101"}}});
    require(!timeline.waiting() && timeline.first(messages) == 2, "Import numeric read cursors across digit lengths");
    require(!timeline.estimated(messages), "A loaded read boundary gives an exact loaded unread count");
    timeline.positioned();
    timeline.acknowledged("10", "100");
    require(timeline.first(messages) == 3, "A partial acknowledgement advances the divider");
    timeline.snapshot({{"10", {"99", "101"}}});
    require(timeline.first(messages) == 3, "A stale server snapshot cannot undo a local read");
    timeline.acknowledged("10", "101");
    require(!timeline.unread() && !timeline.first(messages), "Catching up clears both banner and divider immediately");
    timeline.snapshot({{"10", {"99", "101"}}});
    timeline.open("10");
    require(!timeline.unread(), "Reopening or scrolling back cannot resurrect read messages");
    messages.push_back({.id = "102"});
    timeline.snapshot({{"10", {"99", "102"}}});
    require(timeline.first(messages) == 4, "A newer arrival starts a fresh unread batch after the local read cursor");
    timeline.snapshot({{"10", {"100", "102"}}}, "10");
    require(timeline.first(messages) == 3, "Explicit manual mark-unread can rewind local state");
    timeline.snapshot({{"10", {"101", "102"}}});
    require(timeline.first(messages) == 4, "Another device's partial read moves the boundary forward");
    messages.pop_back();
    require(timeline.unread() && !timeline.first(messages), "An unloaded new message still retains its unread banner");
    timeline.snapshot({});
    require(
        !timeline.unread() && !timeline.first(messages),
        "A complete other-device acknowledgement clears both indicators"
    );
    timeline.snapshot({{"20", {"99", "101"}}});
    timeline.open("20");
    require(timeline.first(messages) == 2, "Navigation uses the selected channel's current cursor");
    messages.erase(messages.begin() + 2);
    require(timeline.first(messages) == 2, "Deleting the first unread row advances to the next loaded row");
    timeline.snapshot({{"20", {"2", "101"}}});
    require(timeline.first(messages) == 0 && timeline.estimated(messages), "Truncated history reports a lower bound");
    timeline.snapshot({{"20", {"", "101"}}});
    require(
        timeline.first(messages) == 0 && timeline.estimated(messages),
        "Never-read history starts at the first loaded row"
    );
    timeline.acknowledged("20", "101");
    timeline.failed("20", "100");
    require(!timeline.unread(), "A stale failed request cannot undo a newer acknowledgement");
    timeline.failed("20", "101");
    require(timeline.unread(), "A failed acknowledgement restores unread indicators for explicit retry");
    timeline.acknowledged("20", "101");
    timeline.snapshot({{"20", {"99", "101"}}}, "reset");
    require(timeline.unread(), "A fresh READY is authoritative after reconnect");
    timeline.snapshot({{"20", {"99", "101", 12}}}, "reset");
    require(timeline.mentions("20") == 12, "Channel badge imports the authoritative mention count");
    timeline.acknowledged("20", "101");
    require(timeline.mentions("20") == 0, "Local reads clear badge counts immediately");
    timeline.snapshot({{"20", {"99", "102", 13, {"102"}}}});
    require(timeline.mentions("20") == 1, "New live ping cannot resurrect locally read imported mentions");
    timeline.snapshot({{"20", {"99", "103", 13, {"102"}}}});
    require(timeline.mentions("20") == 1, "Ordinary arrivals do not change the mention badge");
    timeline.snapshot({{"20", {"101", "103", 1, {"102"}}}});
    require(timeline.mentions("20") == 1, "Server confirmation reconciles a pending local ACK");
    timeline.acknowledged("20", "103");
    timeline.failed("20", "103");
    require(timeline.mentions("20") == 1, "Failed ACK restores the count along with the unread indicators");
    timeline.snapshot({{"20", {"99", "103", 4}}}, "20");
    require(timeline.mentions("20") == 4, "Manual unread rewind restores authoritative badge counts");
    timeline.reset();
    require(!timeline.unread() && !timeline.first(messages), "Account reset drops all read state");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
