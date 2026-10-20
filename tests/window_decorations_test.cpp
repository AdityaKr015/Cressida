#include "app/window_decorations.h"

#include <iostream>
#include <limits>
#include <stdexcept>

int main() {
  using Decorations = app::WindowDecorations;
  using Mode = Decorations::Mode;
  const auto require = [](bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  };
  const auto configure = [](Decorations& decorations, Mode mode) {
    decorations.configure(static_cast<std::uint32_t>(mode));
  };
  try {
    Decorations decorations;
    require(
        decorations.desktopPreferred() && decorations.requested() == Mode::Server,
        "Desktop decorations are the default preference"
    );
    require(
        decorations.clientSide() && decorations.needsTitlebar(false) && !decorations.needsTitlebar(true),
        "Unconfigured surfaces retain a client title bar outside fullscreen"
    );
    require(!decorations.preferDesktop(true), "Repeated preferences do not trigger decoration configure loops");

    configure(decorations, Mode::Server);
    require(decorations.clientSide(), "A decoration event alone cannot change the current surface layout");
    decorations.surfaceConfigured();
    require(
        !decorations.clientSide() && !decorations.needsTitlebar(false),
        "The surface configure applies the desktop decoration choice"
    );

    require(
        decorations.preferDesktop(false) && decorations.requested() == Mode::Client,
        "Disabling desktop decorations requests client decorations"
    );
    require(
        !decorations.preferDesktop(false) && !decorations.clientSide(),
        "Changing the request preserves the last confirmed desktop decoration mode"
    );
    configure(decorations, Mode::Client);
    require(!decorations.clientSide(), "A pending client decoration still waits for surface configuration");
    decorations.surfaceConfigured();
    require(
        decorations.needsTitlebar(false) && !decorations.needsTitlebar(true),
        "Confirmed client decorations restore the title bar while fullscreen still hides it"
    );

    configure(decorations, Mode::Server);
    decorations.surfaceConfigured();
    require(
        decorations.requested() == Mode::Client && !decorations.clientSide(),
        "A compositor may override the requested client mode and remains authoritative"
    );
    decorations.preferDesktop(true);
    decorations.preferDesktop(false);
    decorations.preferDesktop(true);
    require(!decorations.clientSide(), "Rapid preference changes cannot prematurely change the surface layout");
    configure(decorations, Mode::Client);
    decorations.configure(0);
    decorations.configure(std::numeric_limits<std::uint32_t>::max());
    decorations.surfaceConfigured();
    require(
        decorations.requested() == Mode::Server && decorations.clientSide(),
        "Invalid events preserve the last valid pending choice, including compositor overrides"
    );

    Decorations unavailable;
    unavailable.preferDesktop(false);
    unavailable.surfaceConfigured();
    unavailable.preferDesktop(true);
    unavailable.surfaceConfigured();
    require(
        unavailable.desktopPreferred() && unavailable.needsTitlebar(false),
        "Without the decoration protocol, either preference retains usable client controls"
    );
    std::cout << "Window decoration negotiation passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
