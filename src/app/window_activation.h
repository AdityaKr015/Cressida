#pragma once

#include "app/desktop_activation.h"
#include "core/timer_manager.h"

struct wl_surface;
struct xdg_activation_v1;
struct xdg_activation_token_v1;

namespace app {
  // Owning Wayland loop only. Activation must not depend on a frame callback
  // (minimized windows stop receiving them), nor change native window identity.
  class WindowActivation {
  public:
    ~WindowActivation();
    void bind(xdg_activation_v1* manager, wl_surface* surface);
    void request(std::string_view token = {});
    void presented();
    void unmap();

  private:
    void cancelRequest();
    void activateMapped();
    xdg_activation_v1* m_manager = nullptr;
    wl_surface* m_surface = nullptr;
    xdg_activation_token_v1* m_request = nullptr;
    DesktopActivationToken m_token;
    Timer m_deadline;
    bool m_presented = false;
  };
} // namespace app
