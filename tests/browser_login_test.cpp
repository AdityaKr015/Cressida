#include "auth/browser_login.h"

#include <chrono>
#include <iostream>
#include <netinet/in.h>
#include <stdexcept>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {
  using namespace std::chrono_literals;
  void require(bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  }

  struct Socket {
    int fd;
    ~Socket() { close(fd); }
  };

  int connectTo(int port) {
    const auto fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
      throw std::runtime_error("Could not create test socket");
    timeval timeout{3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
      close(fd);
      throw std::runtime_error("Could not connect to test listener");
    }
    return fd;
  }

  std::string request(
      int port, std::string_view method, std::string_view target, std::string_view headers, std::string_view body = {}
  ) {
    Socket socket{connectTo(port)};
    std::string bytes = std::string(method)
        + " "
        + std::string(target)
        + " HTTP/1.1\r\nHost: 127.0.0.1:"
        + std::to_string(port)
        + "\r\n"
        + std::string(headers)
        + "\r\n"
        + std::string(body);
    std::size_t sent = 0;
    while (sent < bytes.size()) {
      const auto count = send(socket.fd, bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
      require(count > 0, "Could not send synthetic request");
      sent += count;
    }
    std::string response;
    char buffer[2048];
    for (;;) {
      const auto count = recv(socket.fd, buffer, sizeof(buffer), 0);
      if (count == 0)
        break;
      require(count > 0, "Listener did not reply in time");
      response.append(buffer, count);
    }
    return response;
  }

  std::string encode(std::string_view input) {
    std::string output;
    for (unsigned char ch : input) {
      output += '%';
      output += "0123456789ABCDEF"[ch >> 4];
      output += "0123456789ABCDEF"[ch & 15];
    }
    return output;
  }

  auth::BrowserResult result(auth::BrowserLogin& login) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
      if (auto value = login.poll())
        return std::move(*value);
      std::this_thread::sleep_for(5ms);
    }
    throw std::runtime_error("Listener did not publish a handoff");
  }
} // namespace

int main() {
  try {
    std::string url;
    // Inject an opener: these tests never launch a browser or contact Discord.
    auth::BrowserLogin login([&](const std::string& value) {
      url = value;
      return true;
    });
    require(login.start(), "Could not start browser handoff");
    require(
        url.starts_with("https://discord.com/login?redirect_to=%2Fhandoff%3Frpc%3D"),
        "Login must remain on Discord's origin"
    );
    const auto port = std::stoi(url.substr(url.rfind("&rpc=") + 5));
    const auto keyOffset = url.find("%26key%3D") + std::string_view("%26key%3D").size();
    const auto key = url.substr(keyOffset, 64);
    require(key.size() == 64, "A handoff needs an unpredictable session key");

    constexpr std::string_view payload =
        R"({"cmd":"BROWSER_HANDOFF","args":{"handoffToken":"synthetic-ticket","fingerprint":"test"},"nonce":"test-nonce"})";
    const auto headers = "Origin: https://discord.com\r\nContent-Type: application/json\r\nContent-Length: "
        + std::to_string(payload.size())
        + "\r\n";
    require(
        request(port, "OPTIONS", "/rpc?v=1", "Origin: https://discord.com\r\n").starts_with("HTTP/1.1 200"),
        "Allow Discord browser preflight"
    );
    require(!login.poll(), "Preflight must not complete sign-in");
    require(
        request(port, "POST", "/rpc?v=1", "Origin: https://attacker.example\r\nContent-Length: 0\r\n")
            .starts_with("HTTP/1.1 400"),
        "Reject cross-origin callbacks"
    );
    require(
        request(port, "POST", "/rpc", "Content-Length: 0\r\n").starts_with("HTTP/1.1 400"),
        "POST callbacks require Discord origin"
    );
    require(
        request(port, "POST", "/rpc", headers + "Content-Length: 0\r\n", payload).starts_with("HTTP/1.1 400"),
        "Reject ambiguous request lengths"
    );
    require(
        request(port, "POST", "/rpc", headers + "Transfer-Encoding: chunked\r\n", payload).starts_with("HTTP/1.1 400"),
        "Reject conflicting HTTP framing"
    );
    require(
        request(port, "POST", "/rpc", "Origin: https://discord.com\r\nContent-Length: 99999\r\n")
            .starts_with("HTTP/1.1 400"),
        "Bound callback bodies"
    );
    require(
        request(port, "POST", "/rpc", "Origin: https://discord.com\r\nContent-Length: 2\r\n", "{}")
            .starts_with("HTTP/1.1 400"),
        "Reject malformed RPC payloads"
    );
    require(login.active() && !login.poll(), "Invalid requests must not consume the handoff");
    const auto response = request(port, "POST", "/rpc?v=1", headers, payload);
    require(
        response.starts_with("HTTP/1.1 200") && response.contains("\"nonce\":\"test-nonce\""),
        "Reply to Chrome RPC with its nonce"
    );
    auto handoff = result(login);
    require(
        handoff.key == key && handoff.handoffToken == "synthetic-ticket" && handoff.error.empty(),
        "Bind the browser ticket to this session's key"
    );
    require(!login.poll(), "A handoff can only be consumed once");

    require(login.start(), "Could not restart handoff");
    const auto nextPort = std::stoi(url.substr(url.rfind("&rpc=") + 5));
    const auto query =
        "/rpc?v=1&payload=" + encode(payload) + "&callback=" + encode("https://discord.com/handoff?done=true");
    require(
        request(nextPort, "GET", "/rpc?payload=%GG&callback=bad", {}).starts_with("HTTP/1.1 400"),
        "Reject malformed percent encoding and redirects"
    );
    require(request(nextPort, "GET", query, {}).starts_with("HTTP/1.1 302"), "Support Firefox's navigation callback");
    handoff = result(login);
    require(handoff.key != key && handoff.handoffToken == "synthetic-ticket", "Generate a fresh key on each attempt");

    require(login.start(), "Could not start cancellable handoff");
    Socket unfinished{connectTo(std::stoi(url.substr(url.rfind("&rpc=") + 5)))};
    const auto before = std::chrono::steady_clock::now();
    login.cancel();
    require(
        std::chrono::steady_clock::now() - before < 500ms && !login.active() && !login.poll(),
        "Cancellation must promptly discard pending callbacks"
    );
    auth::BrowserLogin failed([](const std::string&) { return false; });
    require(!failed.start() && !failed.active(), "Report browser launch failure");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
