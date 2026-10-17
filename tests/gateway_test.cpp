#include "discord/client.h"
#include "discord/gateway.h"

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <nlohmann/json.hpp>
#include <set>

int main(int argc, char** argv) {
  if (argc != 2)
    return 2;
  // Initialize libcurl through the same process runtime as the actual client.
  discord::Client runtime;
  std::atomic<std::uint64_t> generation{1};
  std::mutex mutex;
  std::condition_variable changed;
  std::set<std::string> events;
  bool valid = true;
  discord::UserDirectory users;
  {
    discord::Gateway gateway(
        generation,
        [&](auto session, const auto& type, const auto& data) {
          std::lock_guard lock(mutex);
          if (session != 1)
            valid = false;
          users.consume(type, data);
          if (type == "READY_SUPPLEMENTAL" && users.status("7") != "online")
            valid = false;
          if (type == "MESSAGE_CREATE" && (users.status("7") != "idle" || users.name("7", "Original") != "My friend"))
            valid = false;
          if (type == "MESSAGE_DELETE" && (users.status("7") != "idle" || users.name("7", "Original") != "Original"))
            valid = false;
          if (type == "MESSAGE_CREATE" && data.value("content", "") != "synthetic 🌿")
            valid = false;
          events.insert(type);
          changed.notify_all();
        },
        argv[1]
    );
    gateway.authenticate(1, "synthetic-token", {"online", "working", "77", "wave"});
    if (!gateway.setPresence(1, "dnd") || gateway.setPresence(2, "online") || gateway.setPresence(1, "busy"))
      valid = false;
    gateway.subscribe("1", "2", 100);
    std::unique_lock lock(mutex);
    if (!changed.wait_for(lock, std::chrono::seconds(7), [&] {
          return events.contains("READY")
              && events.contains("RESUMED")
              && events.contains("MESSAGE_CREATE")
              && events.contains("MESSAGE_UPDATE")
              && events.contains("MESSAGE_DELETE")
              && events.contains("RELATIONSHIP_REMOVE")
              && events.contains("PRESENCE_UPDATE")
              && events.contains("GUILD_MEMBER_LIST_UPDATE");
        })) {
      std::cerr << "Gateway did not complete identify, fragmented dispatch and resume\n";
      for (const auto& event : events)
        std::cerr << "Observed: " << event << '\n';
      valid = false;
    }
    lock.unlock();
    ++generation;
    gateway.reset();
  }
  return valid ? 0 : 1;
}
