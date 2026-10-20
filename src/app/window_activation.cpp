#include "app/window_activation.h"

#include "xdg-activation-v1-client-protocol.h"

namespace app {
  WindowActivation::~WindowActivation() { cancelRequest(); }
  void WindowActivation::bind(xdg_activation_v1* manager, wl_surface* surface) {
    unmap();
    m_manager = manager;
    m_surface = surface;
  }
  void WindowActivation::cancelRequest() {
    m_deadline.stop();
    if (m_request) {
      xdg_activation_token_v1_destroy(m_request);
      m_request = nullptr;
    }
  }
  void WindowActivation::activateMapped() {
    if (m_manager && m_presented) {
      const auto token = m_token.take();
      if (!token.empty())
        xdg_activation_v1_activate(m_manager, token.c_str(), m_surface);
    }
  }
  void WindowActivation::presented() {
    m_presented = true;
    activateMapped();
  }
  void WindowActivation::unmap() {
    m_presented = false;
    cancelRequest();
    m_token.clear();
  }
  void WindowActivation::request(std::string_view token) {
    if (!m_manager || !m_surface)
      return;
    if (!token.empty() && m_token.assign(token)) {
      cancelRequest();
      activateMapped();
      return;
    }
    if (m_request || m_token.pending())
      return;
    m_request = xdg_activation_v1_get_activation_token(m_manager);
    static const xdg_activation_token_v1_listener listener{
        .done = [](void* data, xdg_activation_token_v1* request, const char* value) {
          auto& self = *static_cast<WindowActivation*>(data);
          if (request != self.m_request)
            return;
          self.m_token.assign(value);
          self.cancelRequest();
          self.activateMapped();
        }
    };
    xdg_activation_token_v1_add_listener(m_request, &listener, this);
    xdg_activation_token_v1_set_app_id(m_request, desktopApplicationId);
    xdg_activation_token_v1_set_surface(m_request, m_surface);
    // The external desktop click has no input serial on our seat. Never reuse
    // an unrelated old event as evidence of that interaction.
    m_deadline.start(std::chrono::duration_cast<std::chrono::milliseconds>(DesktopActivationToken::lifetime), [this] {
      cancelRequest();
    });
    xdg_activation_token_v1_commit(m_request);
  }
} // namespace app
