#include "app/scroll_motion.h"

#include <iostream>
#include <stdexcept>

int main() {
  using namespace std::chrono_literals;
  using Clock = app::ScrollMotion::Clock;
  auto require = [](bool value, const char* text) {
    if (!value)
      throw std::runtime_error(text);
  };
  try {
    app::ScrollMotion motion;
    float chat = 0, picker = 0;
    const auto start = Clock::time_point{};
    motion.limit(chat, 1000);
    motion.limit(picker, 200);
    motion.scroll(chat, 100, false, start);
    require(chat == 0 && motion.target(chat) == 100, "Wheel input animates toward a target instead of jumping");
    require(motion.tick(start + 10ms) && chat > 0 && chat < 100, "Intermediate frames move smoothly");
    const auto early = chat;
    motion.scroll(chat, 100, false, start + 10ms);
    require(motion.target(chat) == 200 && chat >= early, "Rapid notches accumulate their distance");
    motion.tick(start + 20ms);
    const auto beforeReverse = chat;
    motion.scroll(chat, -10, false, start + 20ms);
    require(
        motion.target(chat) < beforeReverse, "Direction changes respond immediately without unwinding old momentum"
    );
    motion.tick(start + 1s);
    require(chat == motion.target(chat), "Final offset settles exactly");
    motion.scroll(chat, 5000, false, start + 1s);
    motion.limit(chat, 40);
    motion.tick(start + 2s);
    require(chat == 40 && !motion.tick(start + 3s), "Shrinking content clamps the target and stops redraws");
    motion.scroll(chat, -10, true, start + 3s);
    require(
        chat == 30 && !motion.tick(start + 3s), "Touchpad input stays direct, without a second interpolation delay"
    );
    motion.scroll(picker, 50, false, start + 3s);
    motion.stop(picker);
    motion.tick(start + 4s);
    require(picker == 0 && chat == 30, "Closing an overlay cancels its motion without moving another pane");
    motion.scroll(chat, -25, false, start + 4s);
    chat = 0;
    motion.tick(start + 5s);
    require(chat == 0, "Explicit navigation jumps replace old scroll motion");
    motion.scroll(picker, 80, false, start + 5s);
    motion.stopAll();
    require(!motion.tick(start + 6s) && picker == 0, "Account/channel changes cancel every pending scroll");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
