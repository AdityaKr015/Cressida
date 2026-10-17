#include "auth/credentials.h"

#include <iostream>

int main() {
  // Meson supplies an invalid private bus address. Never touch the user's keyring.
  auto backend = auth::secretServiceBackend();
  const auto load = backend->load({});
  const auto save = backend->save("synthetic.test.only", {});
  const auto erase = backend->erase({});
  std::stop_source cancelled;
  cancelled.request_stop();
  const auto interrupted = backend->load(cancelled.get_token());
  if (load || save || erase || interrupted || interrupted.error() != auth::CredentialFailure::Cancelled) {
    std::cerr << "An unavailable/cancelled keyring must fail without a fallback\n";
    return 1;
  }
}
