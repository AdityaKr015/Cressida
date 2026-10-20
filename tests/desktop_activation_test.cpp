#include "app/desktop_activation.h"

#include <iostream>
#include <stdexcept>

int main() {
  const auto require = [](bool ok, const char* reason) {
    if (!ok)
      throw std::runtime_error(reason);
  };
  try {
    using Token = app::DesktopActivationToken;
    const auto now = Token::Clock::now();
    Token token;
    require(!token.pending(now) && token.take(now).empty(), "Empty activation handles never create requests");
    require(
        token.assign("test-token", now) && token.pending(now), "A fresh desktop token can be queued before mapping"
    );
    require(token.take(now) == "test-token" && token.take(now).empty(), "Tokens are consumed once");
    token.assign("old-token", now);
    require(
        !token.pending(now + Token::lifetime + std::chrono::milliseconds(1))
            && token.take(now + Token::lifetime + std::chrono::milliseconds(1)).empty(),
        "Delayed tokens expire without a polling timer"
    );
    token.assign("newer-token", now);
    require(token.take(now - std::chrono::milliseconds(1)).empty(), "Invalid future timestamps cannot revive a token");
    token.assign("previous-token", now);
    require(
        !token.assign(std::string(Token::maximumBytes + 1, 'x'), now) && token.take(now).empty(),
        "An oversized replacement clears its previous activation handle"
    );
    require(
        !token.assign(std::string_view("token\0suffix", 12), now), "Embedded NUL cannot truncate an activation token"
    );
    token.assign("cancelled-token", now);
    token.clear();
    require(!token.pending(now), "Hiding/resetting cancels any pending activation");
    std::cout << "Desktop activation token lifetime and single-use checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
