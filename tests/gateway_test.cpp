#include "discord/client.h"
#include "discord/gateway.h"
#include "discord/role_styles.h"

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
  discord::RoleDirectory roles;
  {
    discord::Gateway gateway(
        generation,
        [&](auto session, const auto& type, const auto& data) {
          std::lock_guard lock(mutex);
          if (session != 1)
            valid = false;
          users.consume(type, data);
          roles.apply(discord::parseRoleUpdates(type, data));
          if (type == "GUILD_MEMBERS_CHUNK" && roles.memberships("1", "7") != std::vector<std::string>{"50"})
            valid = false;
          if (type == "READY_SUPPLEMENTAL" && users.status("7") != "online")
            valid = false;
          if (type == "MESSAGE_CREATE" && (users.status("7") != "idle" || users.name("7", "Original") != "My friend"))
            valid = false;
          if (type == "MESSAGE_DELETE" && (users.status("7") != "idle" || users.name("7", "Original") != "Original"))
            valid = false;
          if (type == "CONNECTION_FAILED" && !data.value("authentication_failed", false))
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
    gateway.requestMembers(2, "1", {"9"});
    gateway.requestMembers(1, "../invalid", {"9"});
    gateway.requestMembers(1, "1", {"7", "7", "bad", "8"});
    gateway.requestMembers(1, "1", {"7", "8"});
    std::unique_lock lock(mutex);
    if (!changed.wait_for(lock, std::chrono::seconds(7), [&] {
          return events.contains("READY")
              && events.contains("RESUMED")
              && events.contains("MESSAGE_CREATE")
              && events.contains("MESSAGE_UPDATE")
              && events.contains("MESSAGE_DELETE")
              && events.contains("CONNECTION_FAILED")
              && events.contains("RELATIONSHIP_REMOVE")
              && events.contains("PRESENCE_UPDATE")
              && events.contains("GUILD_MEMBER_LIST_UPDATE")
              && events.contains("GUILD_MEMBERS_CHUNK");
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
