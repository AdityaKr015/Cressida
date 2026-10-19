#include "api/rest_transfer.h"
#include "api/rest_transport_cache.h"

#include <arpa/inet.h>
#include <array>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
  using namespace std::chrono_literals;
  void require(bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  }

  class Server {
  public:
    Server() {
      m_listener = ::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
      require(m_listener >= 0, "Create an offline loopback listener");
      sockaddr_in address{};
      address.sin_family = AF_INET;
      address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
      require(::bind(m_listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0, "Bind loopback");
      require(::listen(m_listener, 8) == 0, "Listen on loopback");
      socklen_t size = sizeof(address);
      require(::getsockname(m_listener, reinterpret_cast<sockaddr*>(&address), &size) == 0, "Read loopback port");
      m_origin = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port));
      m_worker = std::jthread([this](std::stop_token stop) { run(stop); });
    }
    ~Server() {
      m_worker.request_stop();
      (void)m_wake.signal();
      if (m_worker.joinable())
        m_worker.join();
      ::close(m_listener);
    }
    std::string url(const std::string& path) const { return m_origin + path; }
    bool awaitSlow() {
      std::unique_lock lock(m_mutex);
      return m_ready.wait_for(lock, 3s, [&] { return m_slow; });
    }
    unsigned connections() const { return m_connections.load(); }

  private:
    void run(std::stop_token stop) {
      std::vector<pollfd> descriptors{{m_listener, POLLIN, 0}, {m_wake.fd(), POLLIN, 0}};
      std::vector<std::string> buffers(2);
      while (!stop.stop_requested()) {
        if (::poll(descriptors.data(), descriptors.size(), 3000) <= 0)
          continue;
        if (descriptors[1].revents)
          m_wake.drain();
        if (descriptors[0].revents & POLLIN) {
          const auto socket = ::accept(m_listener, nullptr, nullptr);
          if (socket >= 0) {
            ++m_connections;
            descriptors.push_back({socket, POLLIN, 0});
            buffers.emplace_back();
          }
        }
        for (std::size_t i = 2; i < descriptors.size();) {
          auto& descriptor = descriptors[i];
          if (!(descriptor.revents & (POLLIN | POLLHUP | POLLERR))) {
            ++i;
            continue;
          }
          std::array<char, 4096> bytes{};
          const auto count = ::recv(descriptor.fd, bytes.data(), bytes.size(), 0);
          if (count <= 0) {
            ::close(descriptor.fd);
            descriptors.erase(descriptors.begin() + static_cast<std::ptrdiff_t>(i));
            buffers.erase(buffers.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
          }
          buffers[i].append(bytes.data(), static_cast<std::size_t>(count));
          const auto end = buffers[i].find("\r\n\r\n");
          if (end != std::string::npos) {
            const auto begin = buffers[i].find(' ') + 1;
            const auto path = buffers[i].substr(begin, buffers[i].find(' ', begin) - begin);
            buffers[i].erase(0, end + 4);
            if (path == "/slow") {
              std::lock_guard lock(m_mutex);
              m_slow = true;
              m_ready.notify_all();
            } else {
              const auto response = "HTTP/1.1 200 OK\r\nContent-Length: "
                  + std::to_string(path.size())
                  + "\r\nConnection: keep-alive\r\n\r\n"
                  + path;
              (void)::send(descriptor.fd, response.data(), response.size(), MSG_NOSIGNAL);
            }
          }
          ++i;
        }
      }
      for (std::size_t i = 2; i < descriptors.size(); ++i)
        ::close(descriptors[i].fd);
    }
    int m_listener = -1;
    std::string m_origin;
    process::WakeEvent m_wake;
    std::atomic<unsigned> m_connections{0};
    std::mutex m_mutex;
    std::condition_variable m_ready;
    bool m_slow = false;
    std::jthread m_worker;
  };

  std::size_t receive(char* data, std::size_t size, std::size_t count, void* context) {
    static_cast<std::string*>(context)->append(data, size * count);
    return size * count;
  }
} // namespace

int main() {
  try {
    require(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK, "Initialize libcurl");
    Server server;
    api::RestTransportCache transportCache;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> easy(curl_easy_init(), curl_easy_cleanup);
    transportCache.attach(easy.get());
    std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)> multi(curl_multi_init(), curl_multi_cleanup);
    process::WakeEvent wake;
    std::atomic<std::uint64_t> session{1}, foreground{0};
    api::RestRequestScope active{{}, &session, 1};
    api::RestCancellation interrupted = api::RestCancellation::None;
    std::string body;
    const auto prepare = [&](const std::string& path) {
      curl_easy_reset(easy.get());
      const auto url = server.url(path);
      curl_easy_setopt(easy.get(), CURLOPT_URL, url.c_str());
      curl_easy_setopt(easy.get(), CURLOPT_PROXY, "");
      curl_easy_setopt(easy.get(), CURLOPT_NOSIGNAL, 1L);
      curl_easy_setopt(easy.get(), CURLOPT_TIMEOUT_MS, 3000L);
      curl_easy_setopt(easy.get(), CURLOPT_WRITEFUNCTION, receive);
      curl_easy_setopt(easy.get(), CURLOPT_WRITEDATA, &body);
      body.clear();
      interrupted = api::RestCancellation::None;
    };
    for (const auto* path : {"/first", "/second"}) {
      prepare(path);
      require(
          api::performRestTransfer(easy.get(), multi.get(), wake, active, interrupted) == CURLE_OK && body == path,
          "Transfers return their full body through the production multi path"
      );
    }
    require(server.connections() == 1, "Sequential reads reuse the same keep-alive connection across easy resets");
    {
      // Seed a fake host without DNS, then use it on a fresh worker handle.
      // Abort any resolver invocation so this test can never issue DNS traffic.
      auto url = server.url("/seed");
      const auto port = url.substr(url.find(':', 7) + 1);
      const auto mapping = "cressida.test:" + port.substr(0, port.find('/')) + ":127.0.0.1";
      std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> resolve(
          curl_slist_append(nullptr, mapping.c_str()), curl_slist_free_all
      );
      const auto rejectDns = +[](void*, void*, void*) { return 1; };
      prepare("/seed");
      url.replace(url.find("127.0.0.1"), 9, "cressida.test");
      curl_easy_setopt(easy.get(), CURLOPT_URL, url.c_str());
      curl_easy_setopt(easy.get(), CURLOPT_RESOLVE, resolve.get());
      curl_easy_setopt(easy.get(), CURLOPT_RESOLVER_START_FUNCTION, rejectDns);
      require(
          api::performRestTransfer(easy.get(), multi.get(), wake, active, interrupted) == CURLE_OK,
          "Seed a bounded offline host entry"
      );
      std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> fresh(curl_easy_init(), curl_easy_cleanup);
      std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)> freshMulti(curl_multi_init(), curl_multi_cleanup);
      transportCache.attach(fresh.get());
      curl_easy_setopt(fresh.get(), CURLOPT_URL, url.c_str());
      curl_easy_setopt(fresh.get(), CURLOPT_PROXY, "");
      curl_easy_setopt(fresh.get(), CURLOPT_TIMEOUT_MS, 3000L);
      curl_easy_setopt(fresh.get(), CURLOPT_RESOLVER_START_FUNCTION, rejectDns);
      curl_easy_setopt(fresh.get(), CURLOPT_WRITEFUNCTION, receive);
      curl_easy_setopt(fresh.get(), CURLOPT_WRITEDATA, &body);
      body.clear();
      require(
          api::performRestTransfer(fresh.get(), freshMulti.get(), wake, active, interrupted) == CURLE_OK
              && body == "/seed",
          "Fresh read lanes reuse DNS answers without resolving the host again"
      );
    }
    {
      std::atomic<std::uint64_t> navigation{1};
      api::RestReadIntent intent;
      process::WakeEvent slowWake;
      std::atomic<bool> slowDone{false};
      CURLcode slowResult = CURLE_OK;
      api::RestCancellation slowInterrupted = api::RestCancellation::None;
      std::jthread slowRead([&](std::stop_token stop) {
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> slow(curl_easy_init(), curl_easy_cleanup);
        transportCache.attach(slow.get());
        std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)> slowMulti(curl_multi_init(), curl_multi_cleanup);
        const auto url = server.url("/slow");
        curl_easy_setopt(slow.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(slow.get(), CURLOPT_PROXY, "");
        curl_easy_setopt(slow.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(slow.get(), CURLOPT_TIMEOUT_MS, 3000L);
        api::RestRequestScope scope{stop, &session, 1};
        scope.foregroundRevision = &foreground;
        scope.expectedRevision = foreground;
        scope.intent = &intent;
        scope.promotedNavigation = &navigation;
        slowResult = api::performRestTransfer(slow.get(), slowMulti.get(), slowWake, scope, slowInterrupted);
        slowDone = true;
      });
      require(server.awaitSlow(), "A read lane reaches an intentionally stalled response");
      intent.promote(navigation);
      ++foreground;
      (void)slowWake.signal();
      prepare("/independent");
      require(
          api::performRestTransfer(easy.get(), multi.get(), wake, active, interrupted) == CURLE_OK
              && body == "/independent"
              && !slowDone,
          "A promoted preload keeps its stalled transfer while independent foreground work completes"
      );
      ++navigation;
      (void)slowWake.signal();
      slowRead.join();
      require(
          slowResult == CURLE_ABORTED_BY_CALLBACK && slowInterrupted == api::RestCancellation::Superseded,
          "Navigation cancels superseded reads over the wake pipe without waiting for a timeout"
      );
    }
    prepare("/slow");
    api::RestRequestScope background{{}, &session, 1, &foreground, foreground.load()};
    CURLcode result = CURLE_OK;
    std::jthread transfer([&] {
      result = api::performRestTransfer(easy.get(), multi.get(), wake, background, interrupted);
    });
    const bool started = server.awaitSlow();
    const auto start = std::chrono::steady_clock::now();
    ++foreground;
    (void)wake.signal();
    transfer.join();
    require(started, "The fixture must reach a response that is intentionally held open");
    require(
        result == CURLE_ABORTED_BY_CALLBACK
            && interrupted == api::RestCancellation::ForegroundReady
            && std::chrono::steady_clock::now() - start < 1s,
        "Queued user work interrupts an idle background response without waiting for its HTTP timeout"
    );
    prepare("/visible-history");
    require(
        api::performRestTransfer(easy.get(), multi.get(), wake, active, interrupted) == CURLE_OK
            && body == "/visible-history",
        "An aborted metadata read leaves the reusable handles ready for visible history"
    );
    prepare("/cancelled-account");
    session = 2;
    require(
        api::performRestTransfer(easy.get(), multi.get(), wake, active, interrupted) == CURLE_ABORTED_BY_CALLBACK
            && interrupted == api::RestCancellation::SessionChanged
            && body.empty(),
        "A changed account cancels before the old request is submitted"
    );
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
