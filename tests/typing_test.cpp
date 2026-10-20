#include "api/typing.h"
#include "app/typing_indicator.h"

#include <chrono>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
  void require(bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  }
} // namespace
int main() {
  using namespace std::chrono_literals;
  using Json = nlohmann::json;
  using Indicator = app::TypingIndicator;
  const auto now = Indicator::Clock::time_point{};
  const auto wall = std::chrono::system_clock::time_point{1000s};
  const auto names = [](const auto& user) -> std::optional<std::string> { return user.name; };
  try {
    auto wire = Json{
        {"channel_id", "10"},
        {"guild_id", "1"},
        {"user_id", "7"},
        {"timestamp", 1000},
        {"member", {{"user", {{"id", "7"}, {"username", "Maya"}}}, {"nick", "Moon 🌙"}}}
    };
    const auto parsed = api::parseTypingStart(wire);
    require(parsed && parsed->name == "Moon 🌙" && parsed->guild == "1", "Typing events retain the guild nickname");
    wire.erase("guild_id");
    wire.erase("member");
    require(api::parseTypingStart(wire)->guild.empty(), "DM typing works without guild/member metadata");
    for (const auto& invalid : std::vector<Json>{
             Json{},
             Json::array(),
             {{"channel_id", "../10"}, {"user_id", "7"}, {"timestamp", 1000}},
             {{"channel_id", "10"}, {"user_id", "7"}, {"timestamp", -1}},
             {{"channel_id", "10"}, {"user_id", "7"}, {"timestamp", "1000"}},
             {{"channel_id", "10"}, {"user_id", "7"}, {"timestamp", UINT64_MAX}},
             {{"channel_id", "10"}, {"user_id", "7"}, {"timestamp", 1000}, {"member", {{"user", {{"id", "8"}}}}}}
         })
      require(!api::parseTypingStart(invalid), "Malformed typing events cannot enter transient state");
    Indicator indicator;
    require(
        indicator.start(*parsed, "10", "1", "99", now, wall), "A remote member starts typing in the selected channel"
    );
    require(indicator.text(names) == "Moon 🌙 is typing…", "One member uses singular wording");
    require(
        indicator.contains("10", "7", now)
            && !indicator.contains("11", "7", now)
            && !indicator.contains("10", "8", now),
        "Avatar badges query the typing person only within the selected conversation"
    );
    require(
        !indicator.start({"10", "1", "99", "Self", 1000}, "10", "1", "99", now, wall)
            && !indicator.start(*parsed, "11", "1", "99", now, wall)
            && !indicator.start(*parsed, "10", "2", "99", now, wall),
        "Own, other-channel and other-guild events stay hidden"
    );
    indicator.start({"10", "1", "8", "Noah", 1000}, "10", "1", "99", now, wall);
    require(indicator.text(names) == "Moon 🌙 and Noah are typing…", "Two members use a conjunction");
    indicator.start({"10", "1", "9", "Alex", 1000}, "10", "1", "99", now, wall);
    require(indicator.text(names) == "Moon 🌙, Noah, and Alex are typing…", "Three names are displayed");
    indicator.start({"10", "1", "11", "Hidden fourth name", 1000}, "10", "1", "99", now, wall);
    require(indicator.text(names) == "Moon 🌙, Noah, Alex, and others are typing…", "Additional members become others");
    const auto filtered = indicator.text([](const auto& user) -> std::optional<std::string> {
      return user.id == "7" ? std::make_optional(user.name) : std::nullopt;
    });
    require(filtered == "Moon 🌙 is typing…", "Blocked or ignored users do not enter counts or wording");
    const auto original = indicator.deadline();
    require(indicator.contains("10", "11", now), "Avatar badges include typists beyond the three displayed names");
    require(
        !indicator.start(*parsed, "10", "1", "99", now + 5s, wall + 5s) && indicator.deadline() == original,
        "Duplicate Gateway events never extend typing lifetime"
    );
    require(
        !indicator.start({"10", "1", "7", "Old", 999}, "10", "1", "99", now + 5s, wall + 5s),
        "Out-of-order events cannot replace newer typing state"
    );
    require(
        indicator.start({"10", "1", "7", "Refreshed", 1005}, "10", "1", "99", now + 5s, wall + 5s),
        "A new typing timestamp refreshes the same person without duplicates"
    );
    require(
        indicator.expire(now + 10s) && indicator.text(names) == "Refreshed is typing…",
        "Each user expires independently"
    );
    require(
        indicator.contains("10", "7", now + 10s) && !indicator.contains("10", "8", now + 10s),
        "Refreshed avatar badges survive while other people expire"
    );
    require(
        !indicator.message("11", "7")
            && indicator.message("10", "7")
            && !indicator.contains("10", "7", now + 10s)
            && indicator.text(names).empty()
            && !indicator.deadline(),
        "Sending a message removes typing immediately in that channel only"
    );
    require(!indicator.start(*parsed, "10", "1", "99", now + 11s, wall + 11s), "Expired replayed events remain absent");
    indicator.start({"10", "", "7", "", 1000}, "10", "", "99", now, wall);
    require(
        indicator.text(names) == "Someone is typing…",
        "Unknown DM users get a readable fallback without a metadata fetch"
    );
    indicator.clear();
    require(
        !indicator.deadline() && indicator.text(names).empty(), "Navigation and account resets release all typing state"
    );
    require(!indicator.contains("10", "7", now), "Clearing state also removes every avatar badge");
    indicator.start({"10", "", "7", "<b>Literal</b>\nname", 999}, "10", "", "99", now, wall);
    require(
        indicator.deadline() == now + 9s && indicator.text(names) == "<b>Literal</b> name is typing…",
        "Delayed events retain only their remaining lifetime and names stay single-line plain text"
    );
    require(
        indicator.contains("10", "7", now + 8s) && !indicator.contains("10", "7", now + 9s),
        "Avatar queries never display expired typing while the owning-loop timer is pending"
    );
    indicator.expire(now + 10s);
    require(!indicator.deadline(), "Expiry leaves no timer deadline or stale users");
    indicator.start({"10", "", "7", "Future clock", INT64_MAX}, "10", "", "99", now, wall);
    require(
        indicator.deadline() == now + Indicator::lifetime,
        "Future timestamps never pin an indicator beyond its lifetime"
    );
    indicator.clear();
    for (std::size_t i = 0; i <= Indicator::maximumUsers; ++i)
      indicator.start({"10", "", std::to_string(i + 1), "Member", 1000}, "10", "", "9999", now, wall);
    std::size_t tracked = 0;
    indicator.text([&](const auto& user) -> std::optional<std::string> {
      ++tracked;
      return user.name;
    });
    require(tracked == Indicator::maximumUsers, "Typing floods remain bounded without evicting visible participants");

  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
