#include "api/client.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <stdexcept>
#include <unistd.h>

namespace {
  using namespace std::chrono_literals;
  void require(bool value, const char* text) {
    if (!value)
      throw std::runtime_error(text);
  }
  struct Memory {
    std::atomic<unsigned> loads{0}, writes{0}, erases{0};
    bool fail = false;
    std::string token;
  };
  class Fake final : public auth::CredentialBackend {
    std::shared_ptr<Memory> m;

  public:
    explicit Fake(std::shared_ptr<Memory> memory) : m(std::move(memory)) {}
    std::expected<std::string, auth::CredentialFailure> load(std::stop_token) override {
      ++m->loads;
      if (m->fail)
        return std::unexpected(auth::CredentialFailure::Unavailable);
      return m->token;
    }
    std::expected<void, auth::CredentialFailure> save(const std::string&, std::stop_token) override {
      ++m->writes;
      return {};
    }
    std::expected<void, auth::CredentialFailure> erase(std::stop_token) override {
      ++m->erases;
      m->token.clear();
      return {};
    }
  };
  api::Event wait(api::Client& client, std::uint64_t session) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
      pollfd fd{client.eventFd(), POLLIN, 0};
      poll(&fd, 1, 100);
      for (auto& event : client.drain()) {
        require(event.session == session, "Discard stale credential events after replacing a session");
        return std::move(event);
      }
    }
    throw std::runtime_error("Credential events must wake the client");
  }
} // namespace
int main() {
  char directory[] = "/tmp/cressida-credential-client-XXXXXX";
  if (!mkdtemp(directory))
    return 1;
  const auto marker = std::filesystem::path(directory) / "disabled";
  auto memory = std::make_shared<Memory>();
  try {
    {
      api::Client client(std::make_unique<Fake>(memory), marker);
      const auto restore = client.restoreLogin();
      auto empty = wait(client, restore);
      require(
          empty.operation == api::Operation::Connect
              && empty.noSavedLogin
              && empty.restoredLogin
              && empty.account.id.empty()
              && empty.error.empty()
              && !client.messages("1"),
          "An empty keyring returns to sign-in without inventing a connected account or issuing REST reads"
      );
      memory->fail = true;
      const auto retry = client.restoreLogin();
      auto unavailable = wait(client, retry);
      require(
          !unavailable.error.empty() && !unavailable.noSavedLogin && !client.channels("1"),
          "Keyring failures are recoverable login errors, not network authentication"
      );
      memory->fail = false;
      memory->token = "invalid\ncredential";
      const auto invalid = client.restoreLogin();
      auto malformed = wait(client, invalid);
      require(
          !malformed.error.empty() && memory->erases == 1 && std::filesystem::exists(marker),
          "Malformed saved credentials are rejected locally before HTTPS"
      );
      const auto logout = client.logout();
      auto cleared = wait(client, logout);
      require(
          cleared.operation == api::Operation::Credentials
              && cleared.error.empty()
              && cleared.credentialOperation == auth::CredentialOperation::Forget
              && memory->writes == 0,
          "Logout forgets credentials without saving unvalidated tokens"
      );
    }
    {
      const auto reads = memory->loads.load();
      api::Client restarted(std::make_unique<Fake>(memory), marker);
      const auto request = restarted.restoreLogin();
      require(
          wait(restarted, request).noSavedLogin && memory->loads == reads,
          "A fresh client honors explicit logout without contacting the keyring"
      );
    }
    std::filesystem::remove_all(directory);
  } catch (const std::exception& error) {
    std::filesystem::remove_all(directory);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
