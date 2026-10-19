#include "app/surface_scale.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

int main() {
  auto require = [](bool value, const char* text) {
    if (!value)
      throw std::runtime_error(text);
  };
  auto near = [](float first, float second) { return std::abs(first - second) < 0.0001F; };
  try {
    app::SurfaceScale scale;
    auto geometry = scale.geometry(1001, 601, 1);
    require(
        scale.factor() == 1
            && !scale.usesViewport()
            && geometry.bufferWidth == 1001
            && geometry.bufferHeight == 601
            && geometry.logicalWidth == 1001
            && geometry.logicalHeight == 601
            && geometry.bufferScale == 1
            && geometry.contentScale == 1,
        "A surface without density information uses one physical pixel per logical pixel"
    );
    scale.output(10, 1);
    scale.output(20, 2);
    scale.enter(10);
    require(scale.factor() == 1, "An output outside the surface does not raise its density");
    scale.enter(20);
    require(scale.factor() == 2, "A surface spanning mixed-density outputs uses the larger integer scale");
    scale.leave(10);
    require(scale.factor() == 2, "Leaving the lower-density output preserves the higher density");
    scale.leave(20);
    scale.enter(10);
    require(scale.factor() == 1, "Moving back to the lower-density output restores its native density");
    scale.output(10, 2);
    require(scale.factor() == 2, "Updating an entered output preserves its membership");
    scale.remove(10);
    require(scale.factor() == 1, "Removing an entered output removes its density contribution");
    scale.enter(30);
    scale.output(30, 3);
    require(scale.factor() == 1, "An unknown enter event cannot activate an output registered later");
    scale.enter(30);
    require(scale.factor() == 3, "A newly registered output participates after its enter event");
    scale.remove(30);
    scale.leave(999);
    scale.remove(999);
    require(scale.factor() == 1, "Unknown leave and removal events preserve the fallback density");

    app::SurfaceScale preferred;
    preferred.preferredInteger(2);
    geometry = preferred.geometry(1001, 601, 1);
    require(
        geometry.bufferWidth == 2002 && geometry.bufferHeight == 1202 && geometry.bufferScale == 2,
        "A compositor preference received before the first output enter sizes the initial buffer correctly"
    );
    preferred.output(10, 3);
    preferred.enter(10);
    require(preferred.factor() == 2, "The compositor's surface preference overrides output-derived density");
    preferred.setFractionalAvailable(true);
    geometry = preferred.geometry(1001, 601, 1);
    require(
        geometry.bufferWidth == 2002 && geometry.bufferScale == 1 && preferred.usesViewport(),
        "An available fractional viewport uses buffer scale one while waiting for the initial preference"
    );
    preferred.preferredFractional(150);
    geometry = preferred.geometry(1001, 601, 1);
    require(
        near(preferred.factor(), 1.25F)
            && geometry.bufferWidth == 1251
            && geometry.bufferHeight == 751
            && geometry.bufferScale == 1
            && near(geometry.contentScale, 1.25F),
        "A fractional preference replaces the rounded-up integer preference without upscaling the surface"
    );
    preferred.preferredInteger(3);
    require(near(preferred.factor(), 1.25F), "Later integer hints cannot overwrite an active fractional preference");
    preferred.setFractionalAvailable(false);
    geometry = preferred.geometry(1001, 601, 1);
    require(
        preferred.factor() == 3
            && !preferred.usesViewport()
            && geometry.bufferScale == 3
            && geometry.bufferWidth == 3003,
        "Partial protocol availability falls back to integer buffer scaling"
    );
    preferred.setFractionalAvailable(true);
    require(near(preferred.factor(), 1.25F), "Restoring both protocols restores the retained fractional preference");

    app::SurfaceScale earlyFractional;
    earlyFractional.setFractionalAvailable(true);
    earlyFractional.preferredFractional(180);
    geometry = earlyFractional.geometry(1001, 601, 1);
    require(
        geometry.bufferWidth == 1502 && geometry.bufferHeight == 902 && geometry.bufferScale == 1,
        "Odd surface dimensions round positive half pixels upward for the native fractional buffer"
    );
    earlyFractional.preferredFractional(121);
    require(
        near(earlyFractional.factor(), 121.0F / 120.0F),
        "Valid fractional preferences retain all protocol precision rather than snapping to common scales"
    );

    for (const uint32_t numerator : {1U, 120U, 150U, 180U, 210U, 240U, 960U}) {
      app::SurfaceScale native;
      native.setFractionalAvailable(true);
      native.preferredFractional(numerator);
      const double density = static_cast<double>(numerator) / 120.0;
      for (const float zoom : {0.67F, 0.8F, 1.0F, 1.1F, 1.25F, 1.33F, 1.5F}) {
        geometry = native.geometry(1001, 601, zoom);
        require(
            geometry.bufferWidth == static_cast<int>(std::round(1001 * density))
                && geometry.bufferHeight == static_cast<int>(std::round(601 * density))
                && geometry.bufferScale == 1,
            "Application zoom must never change the monitor-native buffer dimensions"
        );
        require(
            geometry.logicalWidth == static_cast<int>(std::floor(1001.0 / zoom))
                && geometry.logicalHeight == static_cast<int>(std::floor(601.0 / zoom))
                && near(geometry.contentScale, static_cast<float>(density * zoom)),
            "Zoom changes logical layout and glyph density together within the existing native buffer"
        );
      }
    }
    for (const int density : {1, 2, 3, 8}) {
      app::SurfaceScale integer;
      integer.output(10, density);
      integer.enter(10);
      for (const float zoom : {0.67F, 1.0F, 1.33F, 1.5F}) {
        geometry = integer.geometry(1001, 601, zoom);
        require(
            geometry.bufferWidth == 1001 * density
                && geometry.bufferHeight == 601 * density
                && geometry.bufferScale == density
                && near(geometry.contentScale, density * zoom),
            "Integer fallback also separates native density from application zoom"
        );
      }
    }

    app::SurfaceScale invalid;
    invalid.preferredInteger(2);
    invalid.preferredInteger(0);
    invalid.preferredInteger(-1);
    invalid.preferredInteger(9);
    invalid.setFractionalAvailable(true);
    invalid.preferredFractional(0);
    invalid.preferredFractional(961);
    require(invalid.factor() == 2, "Invalid initial scale preferences cannot replace the valid integer fallback");
    invalid.preferredFractional(150);
    invalid.preferredFractional(0);
    invalid.preferredFractional(std::numeric_limits<uint32_t>::max());
    require(near(invalid.factor(), 1.25F), "Invalid subsequent fractional hints retain the last valid preference");
    app::SurfaceScale invalidOutput;
    invalidOutput.output(1, 0);
    invalidOutput.enter(1);
    invalidOutput.output(2, 9);
    invalidOutput.enter(2);
    require(invalidOutput.factor() == 1, "Invalid output scales cannot create entered output state");
    invalidOutput.output(3, 2);
    invalidOutput.enter(3);
    invalidOutput.output(3, -1);
    require(invalidOutput.factor() == 2, "An invalid output update retains the last valid density and membership");

    geometry = invalidOutput.geometry(0, -10, std::numeric_limits<float>::quiet_NaN());
    require(
        geometry.bufferWidth == 2
            && geometry.bufferHeight == 2
            && geometry.logicalWidth == 1
            && geometry.logicalHeight == 1
            && geometry.contentScale == 2,
        "Invalid surface dimensions and nonfinite zoom produce safe native geometry"
    );
    geometry = invalidOutput.geometry(1001, 601, std::numeric_limits<float>::infinity());
    require(geometry.logicalWidth == 1001, "Infinite zoom falls back to the default zoom");
    geometry = invalidOutput.geometry(1001, 601, -1);
    require(near(geometry.contentScale, 2 * 0.67F), "Finite zoom below the supported range clamps to its minimum");
    geometry = invalidOutput.geometry(1001, 601, 10);
    require(geometry.contentScale == 3, "Finite zoom above the supported range clamps to its maximum");
    geometry = invalidOutput.geometry(std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 0.67F);
    require(
        geometry.bufferWidth == std::numeric_limits<int>::max()
            && geometry.logicalWidth == std::numeric_limits<int>::max(),
        "Oversized geometry saturates before integer conversion instead of overflowing"
    );
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
