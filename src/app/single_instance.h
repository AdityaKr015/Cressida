#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace app {
  // The first process owns a private activation socket; later launches only send Open.
  class SingleInstance {
  public:
    enum class Launch { Owner, Forwarded };
    explicit SingleInstance(std::filesystem::path runtime = {});
    ~SingleInstance();
    SingleInstance(const SingleInstance&) = delete;
    SingleInstance& operator=(const SingleInstance&) = delete;
    Launch claim(std::string_view activationToken = {});
    int eventFd() const { return m_owner ? m_socket : -1; }
    const std::filesystem::path& runtimeDirectory() const { return m_runtime; }
    std::optional<std::string> poll();

  private:
    std::filesystem::path m_runtime;
    int m_directory = -1, m_lock = -1, m_socket = -1;
    bool m_owner = false;
  };
} // namespace app
