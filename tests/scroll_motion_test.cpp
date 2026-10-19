#include "app/scroll_motion.h"
#include "render/text/text_position.h"

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
    require(
        app::ScrollMotion::distance(56, false) == 70 && app::ScrollMotion::distance(12, true) == 12,
        "Wheel notches travel farther while precise scrolling retains its supplied distance"
    );
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
    app::ScrollMotion fastFrames, slowFrames;
    float fast = 0, slow = 0;
    fastFrames.limit(fast, 1000);
    slowFrames.limit(slow, 1000);
    fastFrames.scroll(fast, 56, false, start);
    slowFrames.scroll(slow, 56, false, start);
    for (int ms = 5; ms <= 80; ms += 5)
      fastFrames.tick(start + std::chrono::milliseconds(ms));
    slowFrames.tick(start + 80ms);
    require(
        std::abs(fast - slow) < 0.001F && fast > 0 && fast < 56,
        "Frame pacing changes do not alter the wheel trajectory or collapse a notch into a few frames"
    );
    require(fast > 40 && fastFrames.active(fast), "Wheel response makes most of its progress early without snapping");
    const float before = fast;
    fastFrames.tick(start + 81ms);
    const float oldSpeed = fast - before;
    fastFrames.scroll(fast, 56, false, start + 81ms);
    fastFrames.tick(start + 82ms);
    require(
        std::abs((fast - before - oldSpeed) - oldSpeed) < 0.08F,
        "A new wheel notch preserves velocity instead of restarting with a visible lurch"
    );
    fastFrames.tick(start + 2s);
    require(
        fast == 112 && !fastFrames.tick(start + 3s) && !fastFrames.active(fast),
        "Repeated wheel input eventually stops scheduling frames"
    );
    for (const float scale : {0.67F, 1.0F, 1.25F, 1.33F, 2.0F}) {
      const auto beforeStop = textQuadTransform(Mat3::translation(0, 100.39F), scale, false);
      const auto stopped = textQuadTransform(Mat3::translation(0, 100.4F), scale, false);
      require(
          stopped.m[7] >= beforeStop.m[7],
          "The final text quad must not recoil when wheel animation stops at a fractional position"
      );
      // A whole-pixel label top still produces a fractional raster origin when
      // Pango's measured baseline differs from its integer raster baseline.
      // Snap that stationary phase, then add scrolling independently.
      const auto baseline = Mat3::translation(10.37F, 20.43F);
      const auto staticText = textQuadTransform(baseline, scale, true);
      const auto unscrolled = textQuadTransform(baseline, scale, false, 0.0F);
      require(
          staticText.m == unscrolled.m
              && std::abs(unscrolled.m[6] * scale - std::round(unscrolled.m[6] * scale)) < 0.0001F
              && std::abs(unscrolled.m[7] * scale - std::round(unscrolled.m[7] * scale)) < 0.0001F,
          "An unscrolled pane must retain the same sharp raster origin as static text, including zoom below 100%"
      );
      for (const float direction : {-1.0F, 1.0F}) {
        float previous = unscrolled.m[7];
        for (const float distance : {0.02F, 0.17F, 0.39F, 0.4F}) {
          const float offset = direction * distance;
          auto moved = baseline;
          moved.m[7] += offset;
          const auto rendered = textQuadTransform(moved, scale, false, offset);
          require(
              std::abs(rendered.m[6] - unscrolled.m[6]) < 0.0001F
                  && std::abs(rendered.m[7] - unscrolled.m[7] - offset) < 0.0001F
                  && direction * (rendered.m[7] - previous) >= 0,
              "Scroll-origin snapping must preserve fractional travel in both directions without a settling recoil"
          );
          previous = rendered.m[7];
        }
      }
      const auto rotated = baseline * Mat3::rotation(0.2F);
      require(
          textQuadTransform(rotated, scale, false, 0.0F).m == rotated.m,
          "Scrolling text must preserve rotated transforms instead of snapping either axis"
      );
      for (const float sign : {-1.0F, 1.0F}) {
        const float halfPixel = sign * 20.5F / scale;
        const auto stationary = textQuadTransform(Mat3::translation(0, halfPixel), scale, false, 0.0F);
        for (const float offset : {0.02F, 0.39F, 2.1F, 100.23F, 1000.25F}) {
          const auto scrolled = textQuadTransform(Mat3::translation(0, halfPixel + offset), scale, false, offset);
          require(
              std::abs(scrolled.m[7] - stationary.m[7] - offset) < 0.0002F,
              "Subtracting scroll offsets must not flip a stationary half-pixel tie and jump a whole pixel"
          );
        }
      }
    }
    app::ScrollMotion betweenFrames;
    float displayed = 500;
    betweenFrames.limit(displayed, 2000);
    betweenFrames.scroll(displayed, 200, false, start);
    betweenFrames.tick(start + 20ms);
    const float shown = displayed;
    betweenFrames.scroll(displayed, -10, false, start + 39ms);
    require(displayed == shown, "A wheel callback must not advance the displayed position between frames");
    betweenFrames.tick(start + 40ms);
    require(displayed < shown, "Reversing near a frame boundary must immediately move from the last visible position");
    const auto shownDown = displayed;
    betweenFrames.scroll(displayed, 10, false, start + 59ms);
    require(displayed == shownDown, "The opposite reversal also preserves the last displayed position");
    betweenFrames.tick(start + 60ms);
    require(displayed > shownDown, "Both directions start from the visible position rather than old unseen motion");
    for (const float direction : {-1.0F, 1.0F}) {
      app::ScrollMotion oneWay;
      float offset = direction > 0 ? 1000000 : 8000000;
      oneWay.limit(offset, 10000000);
      for (int ms = 0; ms < 2000; ++ms) {
        const float previous = offset;
        if (ms < 400 && ms % 17 == 0)
          oneWay.scroll(offset, direction * 120, false, start + std::chrono::milliseconds(ms));
        oneWay.tick(start + std::chrono::milliseconds(ms));
        require(
            direction * (offset - previous) >= 0,
            "Repeated wheel input and floating-point settling must never bounce in the opposite direction"
        );
      }
    }
    app::ScrollMotion anchoredMotion;
    float anchored = 100;
    anchoredMotion.limit(anchored, 1000);
    anchoredMotion.scroll(anchored, 120, false, start);
    anchoredMotion.tick(start + 30ms);
    const float beforeAppend = anchored;
    const float beforeTarget = anchoredMotion.target(anchored);
    anchoredMotion.rebase(anchored, 80);
    anchoredMotion.limit(anchored, 1080);
    require(
        anchored == beforeAppend + 80 && anchoredMotion.target(anchored) == beforeTarget + 80,
        "Timeline growth shifts the displayed offset and pending target together"
    );
    anchoredMotion.tick(start + 40ms);
    require(anchored >= beforeAppend + 80, "Content anchoring preserves ongoing wheel direction");

  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
