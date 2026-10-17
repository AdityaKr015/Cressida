#pragma once

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>

namespace auth {
  enum class CredentialFailure { Unavailable, Cancelled, Invalid, StateFile };
  bool validCredential(const std::string& token);
  std::string credentialError(CredentialFailure failure);

  // Implementations never expose platform objects or raw keyring errors.
  class CredentialBackend {
  public:
    virtual ~CredentialBackend() = default;
    virtual std::expected<std::string, CredentialFailure> load(std::stop_token stop) = 0;
    virtual std::expected<void, CredentialFailure> save(const std::string& token, std::stop_token stop) = 0;
    virtual std::expected<void, CredentialFailure> erase(std::stop_token stop) = 0;
  };
  std::unique_ptr<CredentialBackend> secretServiceBackend();

  enum class CredentialOperation { Load, Save, Forget };
  struct CredentialResult {
    CredentialOperation operation;
    std::uint64_t request;
    std::string token, error;
    bool found = false;
  };

  // A dedicated worker keeps keyring prompts off both UI and Discord workers.
  // Completion callbacks run here; only the Discord client may receive tokens.
  class CredentialStore {
  public:
    using Completion = std::function<void(CredentialResult)>;
    explicit CredentialStore(std::unique_ptr<CredentialBackend> backend = {}, std::filesystem::path disabled = {});
    ~CredentialStore();
    void load(std::uint64_t request, Completion complete);
    void save(std::uint64_t request, std::string token, Completion complete);
    void forget(std::uint64_t request, Completion complete);
    void cancelLoad();
    void shutdown();

  private:
    struct Task {
      CredentialOperation operation;
      std::uint64_t request, revision;
      std::string token;
      Completion complete;
      bool markerWritten = true;
    };
    void run(std::stop_token stop);
    bool disableRestore(); // Marker contains no credentials, only a logout barrier.
    std::unique_ptr<CredentialBackend> m_backend;
    std::filesystem::path m_disabled;
    std::mutex m_mutex;
    std::condition_variable_any m_ready;
    std::deque<Task> m_tasks;
    std::stop_source m_activeStop;
    CredentialOperation m_active = CredentialOperation::Load;
    bool m_busy = false, m_stopping = false;
    std::uint64_t m_revision = 0;
    std::jthread m_worker;
  };
} // namespace auth
