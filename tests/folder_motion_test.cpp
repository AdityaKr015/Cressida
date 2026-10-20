#include "app/folder_motion.h"
#include "app/layout_metrics.h"

#include <iostream>
#include <stdexcept>

int main() {
  try {
    const auto require = [](bool ok, const char* reason) {
      if (!ok)
        throw std::runtime_error(reason);
    };
    using M = app::FolderMotion;
    using R = app::layout::ServerRail;
    M motion;
    const auto start = M::Clock::time_point{};
    motion.begin();
    require(
        motion.set("10", true, true, start) == 1 && !motion.active(), "Restored folders start in their saved state"
    );
    motion.end();
    require(motion.set("10", false, true, start) == 1, "Closing begins at the last displayed position");
    require(motion.tick(start + M::duration / 4), "A quarter-duration frame is still moving");
    const auto closing = motion.set("10", false, true, start + M::duration / 4);
    require(closing > 0 && closing < 1, "Closing publishes an intermediate fraction");
    const auto reversal = start + M::duration / 4;
    require(
        motion.set("10", true, true, reversal) == closing, "Reversals preserve the last displayed fraction exactly"
    );
    motion.tick(reversal + M::duration / 4);
    require(motion.set("10", true, true, reversal) > closing, "Reversal moves toward the new target");
    require(!motion.tick(reversal + M::duration * 2), "A settled folder stops requesting animation frames");
    require(motion.set("10", false, false, reversal) == 0 && !motion.active(), "Reduced motion settles immediately");
    motion.set("10", true, true, reversal);
    motion.finish();
    require(
        !motion.active() && motion.set("10", true, true, reversal) == 1,
        "Covered/inactive views can finish without timers"
    );
    motion.begin();
    motion.end();
    require(motion.size() == 0, "Deleted folder state is pruned");
    const auto closed = R::folderFrame(3, 0), open = R::folderFrame(3, 1);
    require(
        closed.childrenHeight == 0 && closed.headerHeight == R::folderSize,
        "Closed folders allocate no hidden server rows"
    );
    require(
        open.headerHeight < closed.headerHeight && open.childrenHeight >= app::layout::Avatar::rail * 3,
        "Compact headers leave room for all expanded children"
    );
    float previous = closed.height;
    for (const float progress : {0.25F, 0.5F, 0.75F, 1.0F}) {
      const auto frame = R::folderFrame(3, progress);
      require(
          frame.height >= previous && frame.height <= open.height,
          "The measured group and following rows slide monotonically without overshooting"
      );
      previous = frame.height;
    }
    require(R::folderFrame(0, 1).height > 0, "Empty folders retain a usable header");
    std::cout << "Folder animation lifecycle, reversal, reduced motion and shared geometry passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
