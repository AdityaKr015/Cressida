#include "api/client.h"

#include <chrono>
#include <iostream>
#include <poll.h>
#include <stdexcept>

namespace {
  void require(bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  }
} // namespace

int main() {
  try {
    api::Client client;
    require(!client.messages("123"), "Disconnected clients must not issue HTTP requests");
    require(
        !client.messages("123", {api::HistoryDirection::Older, "456", 1})
            && !client.messages("123", {api::HistoryDirection::Newer, "../invalid", 2}),
        "History pages require authentication and validated cursors"
    );
    require(
        !client.threads({api::ThreadList::Active, "1", "10"}),
        "Disconnected thread browsing never performs requests"
    );
    require(!client.channels("123"), "Channels require an authenticated session");
    require(!client.send("123", "hello"), "Sending requires an authenticated session");
    require(
        !client.interact({.kind = api::InteractionKind::Block, .user = "7"})
            && !client.interact({.kind = api::InteractionKind::DeleteMessage, .channel = "10", .message = "20"})
            && !client.send("10", "reply", {}, api::MessageReference{"20", "99"}),
        "Disconnected actions and cross-channel reply references never perform requests"
    );
    require(
        !client.profile() && !client.saveProfile({"123", "Alex"}), "Account settings require an authenticated session"
    );
    require(
        !client.setPresence("dnd") && !client.setPresence("busy"),
        "Disconnected or malformed presence changes do not send requests"
    );
    require(
        !client.accountSettings()
            && !client.saveAccountSettings({{"show_activity", 0}})
            && !client.settingsList("devices")
            && !client.settingsList("arbitrary")
            && !client.acknowledge("123", "456")
            && !client.acknowledge("123", "../token"),
        "Disconnected settings and acknowledgements never perform requests"
    );
    // Empty credentials are rejected locally: no network or account is used.
    const auto obsolete = client.connect("");
    client.disconnect();
    const auto current = client.connect("");
    require(current > obsolete, "Reconnects need distinct session generations");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    bool received = false;
    while (!received && std::chrono::steady_clock::now() < deadline) {
      pollfd descriptor{client.eventFd(), POLLIN, 0};
      poll(&descriptor, 1, 100);
      for (const auto& event : client.drain()) {
        require(event.session == current, "Discard late events from disconnected sessions");
        require(
            event.operation == api::Operation::Connect && !event.error.empty(),
            "Invalid credentials must produce a connection failure"
        );
        received = true;
      }
    }
    require(received, "The network worker must wake the UI on failure");
    require(!client.messages("123"), "Failed connections must not allow follow-up requests");
    client.disconnect();
    require(client.drain().empty(), "Disconnect must discard queued account events");
    // Malformed handoffs must be rejected before any HTTP exchange is attempted.
    const auto handoffSession = client.connectHandoff("", "synthetic-ticket");
    received = false;
    const auto handoffDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!received && std::chrono::steady_clock::now() < handoffDeadline) {
      pollfd descriptor{client.eventFd(), POLLIN, 0};
      poll(&descriptor, 1, 100);
      for (const auto& event : client.drain()) {
        require(event.session == handoffSession && !event.error.empty(), "Reject handoffs with missing session keys");
        received = true;
      }
    }
    require(received && !client.messages("123"), "Failed handoff must leave the account disconnected");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
