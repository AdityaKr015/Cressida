#include "auth/credentials.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace {
  using namespace std::chrono_literals;
  void require(bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  }
  struct Memory {
    std::mutex mutex;
    std::condition_variable_any ready;
    std::string token;
    bool unavailable = false, failErase = false, blockLoad = false, blockSave = false, commitAfterCancel = false;
    std::atomic<unsigned> loads{0}, saves{0}, erases{0};
  };
  class FakeKeyring final : public auth::CredentialBackend {
    std::shared_ptr<Memory> m;

  public:
    explicit FakeKeyring(std::shared_ptr<Memory> memory) : m(std::move(memory)) {}
    std::expected<std::string, auth::CredentialFailure> load(std::stop_token stop) override {
      std::unique_lock lock(m->mutex);
      ++m->loads;
      m->ready.notify_all();
      m->ready.wait(lock, stop, [&] { return !m->blockLoad; });
      if (stop.stop_requested())
        return std::unexpected(auth::CredentialFailure::Cancelled);
      if (m->unavailable)
        return std::unexpected(auth::CredentialFailure::Unavailable);
      return m->token;
    }
    std::expected<void, auth::CredentialFailure> save(const std::string& token, std::stop_token stop) override {
      std::unique_lock lock(m->mutex);
      ++m->saves;
      m->ready.notify_all();
      m->ready.wait(lock, stop, [&] { return !m->blockSave; });
      if (stop.stop_requested() && !m->commitAfterCancel)
        return std::unexpected(auth::CredentialFailure::Cancelled);
      if (m->unavailable)
        return std::unexpected(auth::CredentialFailure::Unavailable);
      m->token = token;
      return {};
    }
    std::expected<void, auth::CredentialFailure> erase(std::stop_token) override {
      std::lock_guard lock(m->mutex);
      ++m->erases;
      if (m->unavailable || m->failErase)
        return std::unexpected(auth::CredentialFailure::Unavailable);
      m->token.clear();
      return {};
    }
  };
  struct Reply {
    std::shared_ptr<std::promise<auth::CredentialResult>> promise =
        std::make_shared<std::promise<auth::CredentialResult>>();
    std::future<auth::CredentialResult> future = promise->get_future();
    auth::CredentialStore::Completion callback() {
      return [promise = promise](auto value) { promise->set_value(std::move(value)); };
    }
    auth::CredentialResult get() {
      require(
          future.wait_for(3s) == std::future_status::ready, "Credential worker must complete and notify its caller"
      );
      return future.get();
    }
  };
  void waitStarted(const std::shared_ptr<Memory>& memory, const std::atomic<unsigned>& count, unsigned target) {
    std::unique_lock lock(memory->mutex);
    require(memory->ready.wait_for(lock, 3s, [&] { return count >= target; }), "Fake keyring operation must start");
  }
} // namespace
int main() {
  char directory[] = "/tmp/cressida-credentials-XXXXXX";
  if (!mkdtemp(directory))
    return 1;
  const auto marker = std::filesystem::path(directory) / "state/login-disabled";
  auto memory = std::make_shared<Memory>();
  try {
    for (const auto& token :
         {std::string{}, std::string("line\nbreak"), std::string("nul\0byte", 8), std::string(4097, 'a')})
      require(!auth::validCredential(token), "Reject malformed credentials without platform access");
    {
      auth::CredentialStore store(std::make_unique<FakeKeyring>(memory), marker);
      require(
          memory->loads == 0 && !std::filesystem::exists(marker.parent_path()),
          "Construction is inert, including for demo"
      );
      Reply first;
      store.load(1, first.callback());
      const auto missing = first.get();
      require(!missing.found && missing.error.empty(), "First launch without a saved token is normal");
      Reply save;
      store.save(2, "synthetic.first.token", save.callback());
      require(
          save.get().error.empty() && !std::filesystem::exists(marker), "Successful storage enables automatic login"
      );
      require(memory->token == "synthetic.first.token", "The secret goes to the backend only");
    }
    {
      auth::CredentialStore restarted(std::make_unique<FakeKeyring>(memory), marker);
      Reply load;
      restarted.load(3, load.callback());
      const auto restored = load.get();
      require(
          restored.found && restored.token == "synthetic.first.token" && restored.request == 3,
          "A new client restores the last stored login with its request generation"
      );
      Reply rotate;
      restarted.save(4, "synthetic.rotated.token", rotate.callback());
      require(
          rotate.get().error.empty() && memory->token == "synthetic.rotated.token",
          "Token rotation replaces the saved credential"
      );
      memory->failErase = true;
      Reply forget;
      restarted.forget(5, forget.callback());
      require(std::filesystem::exists(marker), "Logout writes its barrier before asynchronous keyring deletion");
      require(!forget.get().error.empty(), "Keyring deletion failures are reported without secret contents");
      struct stat info{};
      require(stat(marker.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600, "Logout state is owner-only");
      std::ifstream file(marker);
      std::string contents((std::istreambuf_iterator<char>(file)), {});
      require(contents == "disabled\n", "The disk marker contains no credentials");
    }
    {
      const auto before = memory->loads.load();
      auth::CredentialStore restarted(std::make_unique<FakeKeyring>(memory), marker);
      Reply load;
      restarted.load(6, load.callback());
      require(
          !load.get().found && memory->loads == before, "Failed deletion must not silently log back in after restart"
      );
      memory->failErase = false;
      Reply save;
      restarted.save(7, "synthetic.new.token", save.callback());
      require(save.get().error.empty(), "An explicit new login can replace an old disabled record");
      memory->unavailable = true;
      Reply failure;
      restarted.save(8, "synthetic.failed.token", failure.callback());
      const auto failed = failure.get();
      require(
          !failed.error.empty() && failed.token.empty() && failed.error.find("synthetic") == std::string::npos,
          "Unavailable keyrings produce safe errors without exposing tokens"
      );
      require(std::filesystem::exists(marker), "A failed save must not restore another account's older token");
      memory->unavailable = false;
      Reply erase;
      restarted.forget(9, erase.callback());
      require(erase.get().error.empty() && memory->token.empty(), "Successful logout removes the saved token");
    }
    {
      auth::CredentialStore store(std::make_unique<FakeKeyring>(memory), marker);
      memory->blockSave = memory->commitAfterCancel = true;
      const auto count = memory->saves.load();
      Reply cancelledSave;
      store.save(10, "synthetic.racing.token", cancelledSave.callback());
      waitStarted(memory, memory->saves, count + 1);
      Reply forget;
      store.forget(11, forget.callback());
      require(
          forget.get().error.empty() && memory->token.empty() && std::filesystem::exists(marker),
          "Logout wins even if an in-flight save commits after cancellation"
      );
      require(
          cancelledSave.future.wait_for(10ms) == std::future_status::timeout,
          "Stale save completion must not re-enable login"
      );
      memory->blockSave = memory->commitAfterCancel = false;
    }
    {
      std::filesystem::remove(marker);
      memory->blockLoad = true;
      const auto count = memory->loads.load();
      auth::CredentialStore store(std::make_unique<FakeKeyring>(memory), marker);
      Reply cancelled;
      store.load(12, cancelled.callback());
      waitStarted(memory, memory->loads, count + 1);
      const auto start = std::chrono::steady_clock::now();
      store.shutdown();
      require(std::chrono::steady_clock::now() - start < 1s, "Shutdown cancels an outstanding keyring prompt");
      require(
          cancelled.future.wait_for(10ms) == std::future_status::timeout,
          "Cancelled restoration must not deliver a token"
      );
      memory->blockLoad = false;
    }
    {
      memory->token = "malformed\ncredential";
      auth::CredentialStore store(std::make_unique<FakeKeyring>(memory), marker);
      Reply invalid;
      store.load(13, invalid.callback());
      require(
          !invalid.get().error.empty() && memory->token.empty() && std::filesystem::exists(marker),
          "Malformed saved credentials are forgotten, never passed to networking"
      );
    }
    {
      const auto notDirectory = std::filesystem::path(directory) / "file";
      std::ofstream(notDirectory) << "file";
      auth::CredentialStore store(std::make_unique<FakeKeyring>(memory), notDirectory / "marker");
      Reply failed;
      const auto writes = memory->saves.load();
      store.save(14, "synthetic.token", failed.callback());
      require(
          !failed.get().error.empty() && memory->saves == writes,
          "Unwritable state must not fall back to unsafe persistence"
      );
    }
    std::filesystem::remove_all(directory);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    std::filesystem::remove_all(directory);
    return 1;
  }
}
