#include "api/models.h"
#include "api/server_organization.h"
#include "api/server_privacy.h"

#include <algorithm>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
  using Json = nlohmann::json;
  void require(bool value, const char* message) {
    if (!value)
      throw std::runtime_error(message);
  }
  template <class Action> void rejects(Action action, const char* message) {
    bool rejected = false;
    try {
      action();
    } catch (const std::exception&) {
      rejected = true;
    }
    require(rejected, message);
  }
  // Independently encoded protobuf fixtures include packed/unpacked IDs, a zero
  // folder ID, split messages, and opaque wrapper/folder/group/root metadata.
  const std::map<std::string, std::string> fixtures{
      {"snapshot",
       "CgQIARgqcsMBCj8KEGUAAAAAAAAA5wMAAAAAAAASEQgAsgQMemVybyB3cmFwcGVyGggKBlNvdXJjZZIDDXNvdXJjZSBvcGFxdWXaBAxncm91"
       "cCBvcGFxdWUKLQnKAAAAAAAAABICCAcaCQoHRnJpZW5kcyITCNboSLoEDGNvbG9yIG9wYXF1ZRIoZQAAAAAAAADnAwAAAAAAAMoAAAAAAAAA"
       "LwEAAAAAAACUAQAAAAAAAAoYCS8BAAAAAAAA0gUMbG9vc2Ugb3BhcXVlQk0aEGUAAAAAAAAACQMAAAAAAABpeAMAAAAAAAB6EGUAAAAAAAAA"
       "5wMAAAAAAACBAQkDAAAAAAAAkgMOcHJpdmFjeSBvcGFxdWXaAQIIAVoKCggKBm9ubGluZZoGDnVudG91Y2hlZCByb290"},
      {"move",
       "csQBCjcKCOcDAAAAAAAAEhEIALIEDHplcm8gd3JhcHBlchoICgZTb3VyY2WSAw1zb3VyY2Ugb3BhcXVl2gQMZ3JvdXAgb3BhcXVlCjYJygAA"
       "AAAAAAASAggHGgkKB0ZyaWVuZHMiEwjW6Ei6BAxjb2xvciBvcGFxdWUJZQAAAAAAAAASKGUAAAAAAAAA5wMAAAAAAADKAAAAAAAAAC8BAAAA"
       "AAAAlAEAAAAAAAAKGAkvAQAAAAAAANIFDGxvb3NlIG9wYXF1ZQ=="},
      {"ungroup",
       "csYBCjcKCOcDAAAAAAAAEhEIALIEDHplcm8gd3JhcHBlchoICgZTb3VyY2WSAw1zb3VyY2Ugb3BhcXVlCgkJZQAAAAAAAADaBAxncm91cCBv"
       "cGFxdWUKLQnKAAAAAAAAABICCAcaCQoHRnJpZW5kcyITCNboSLoEDGNvbG9yIG9wYXF1ZRIoZQAAAAAAAADnAwAAAAAAAMoAAAAAAAAALwEA"
       "AAAAAACUAQAAAAAAAAoYCS8BAAAAAAAA0gUMbG9vc2Ugb3BhcXVl"},
      {"create",
       "ctMBCjcKCOcDAAAAAAAAEhEIALIEDHplcm8gd3JhcHBlchoICgZTb3VyY2WSAw1zb3VyY2Ugb3BhcXVlChYJZQAAAAAAAAASAggBGgcKBU9y"
       "Yml02gQMZ3JvdXAgb3BhcXVlCi0JygAAAAAAAAASAggHGgkKB0ZyaWVuZHMiEwjW6Ei6BAxjb2xvciBvcGFxdWUSKGUAAAAAAAAA5wMAAAAA"
       "AADKAAAAAAAAAC8BAAAAAAAAlAEAAAAAAAAKGAkvAQAAAAAAANIFDGxvb3NlIG9wYXF1ZQ=="},
      {"remove_empty",
       "crIBCj8KEGUAAAAAAAAA5wMAAAAAAAASEQgAsgQMemVybyB3cmFwcGVyGggKBlNvdXJjZZIDDXNvdXJjZSBvcGFxdWXaBAxncm91cCBvcGFx"
       "dWUKNgnKAAAAAAAAABICCAcaCQoHRnJpZW5kcyITCNboSLoEDGNvbG9yIG9wYXF1ZQkvAQAAAAAAABIoZQAAAAAAAADnAwAAAAAAAMoAAAAA"
       "AAAALwEAAAAAAACUAQAAAAAAAA=="},
      {"position_only",
       "cswBCj8KEGUAAAAAAAAA5wMAAAAAAAASEQgAsgQMemVybyB3cmFwcGVyGggKBlNvdXJjZZIDDXNvdXJjZSBvcGFxdWXaBAxncm91cCBvcGFx"
       "dWUKNgnKAAAAAAAAABICCAcaCQoHRnJpZW5kcyITCNboSLoEDGNvbG9yIG9wYXF1ZQmUAQAAAAAAABIoZQAAAAAAAADnAwAAAAAAAMoAAAAA"
       "AAAALwEAAAAAAACUAQAAAAAAAAoYCS8BAAAAAAAA0gUMbG9vc2Ugb3BhcXVl"},
      {"noop",
       "csMBCj8KEGUAAAAAAAAA5wMAAAAAAAASEQgAsgQMemVybyB3cmFwcGVyGggKBlNvdXJjZZIDDXNvdXJjZSBvcGFxdWXaBAxncm91cCBvcGFx"
       "dWUKLQnKAAAAAAAAABICCAcaCQoHRnJpZW5kcyITCNboSLoEDGNvbG9yIG9wYXF1ZRIoZQAAAAAAAADnAwAAAAAAAMoAAAAAAAAALwEAAAAA"
       "AACUAQAAAAAAAAoYCS8BAAAAAAAA0gUMbG9vc2Ugb3BhcXVl"},
      {"privacy_patch",
       "Qk4aCAkDAAAAAAAAaXgDAAAAAAAAehBlAAAAAAAAAOcDAAAAAAAAgQEJAwAAAAAAAJIDDnByaXZhY3kgb3BhcXVl2gECCAFpZQAAAAAAAAA="},
      {"privacy_noop",
       "Qk0aEGUAAAAAAAAACQMAAAAAAABpeAMAAAAAAAB6EGUAAAAAAAAA5wMAAAAAAACBAQkDAAAAAAAAkgMOcHJpdmFjeSBvcGFxdWXaAQIIAQ=="},
      {"missing_version",
       "csMBCj8KEGUAAAAAAAAA5wMAAAAAAAASEQgAsgQMemVybyB3cmFwcGVyGggKBlNvdXJjZZIDDXNvdXJjZSBvcGFxdWXaBAxncm91cCBvcGFx"
       "dWUKLQnKAAAAAAAAABICCAcaCQoHRnJpZW5kcyITCNboSLoEDGNvbG9yIG9wYXF1ZRIoZQAAAAAAAADnAwAAAAAAAMoAAAAAAAAALwEAAAAA"
       "AACUAQAAAAAAAAoYCS8BAAAAAAAA0gUMbG9vc2Ugb3BhcXVlQk0aEGUAAAAAAAAACQMAAAAAAABpeAMAAAAAAAB6EGUAAAAAAAAA5wMAAAAA"
       "AACBAQkDAAAAAAAAkgMOcHJpdmFjeSBvcGFxdWXaAQIIAQ=="},
      {"unknown_folder", "CgQIARgqcg8KDQllAAAAAAAAABICCCo="},
      {"duplicate_guild",
       "CgQIARgqclAKPwoQZQAAAAAAAADnAwAAAAAAABIRCACyBAx6ZXJvIHdyYXBwZXIaCAoGU291cmNlkgMNc291cmNlIG9wYXF1ZQoNCWUAAAAA"
       "AAAAEgIIBw=="},
      {"duplicate_folder",
       "CgQIARgqclAKPwoQZQAAAAAAAADnAwAAAAAAABIRCACyBAx6ZXJvIHdyYXBwZXIaCAoGU291cmNlkgMNc291cmNlIG9wYXF1ZQoNCcoAAAAA"
       "AAAAEgIIAA=="},
      {"full_folder",
       "CgQIARgqcusGCj8KEGUAAAAAAAAA5wMAAAAAAAASEQgAsgQMemVybyB3cmFwcGVyGggKBlNvdXJjZZIDDXNvdXJjZSBvcGFxdWUKpwYKoAbo"
       "AwAAAAAAAOkDAAAAAAAA6gMAAAAAAADrAwAAAAAAAOwDAAAAAAAA7QMAAAAAAADuAwAAAAAAAO8DAAAAAAAA8AMAAAAAAADxAwAAAAAAAPID"
       "AAAAAAAA8wMAAAAAAAD0AwAAAAAAAPUDAAAAAAAA9gMAAAAAAAD3AwAAAAAAAPgDAAAAAAAA+QMAAAAAAAD6AwAAAAAAAPsDAAAAAAAA/AMA"
       "AAAAAAD9AwAAAAAAAP4DAAAAAAAA/wMAAAAAAAAABAAAAAAAAAEEAAAAAAAAAgQAAAAAAAADBAAAAAAAAAQEAAAAAAAABQQAAAAAAAAGBAAA"
       "AAAAAAcEAAAAAAAACAQAAAAAAAAJBAAAAAAAAAoEAAAAAAAACwQAAAAAAAAMBAAAAAAAAA0EAAAAAAAADgQAAAAAAAAPBAAAAAAAABAEAAAA"
       "AAAAEQQAAAAAAAASBAAAAAAAABMEAAAAAAAAFAQAAAAAAAAVBAAAAAAAABYEAAAAAAAAFwQAAAAAAAAYBAAAAAAAABkEAAAAAAAAGgQAAAAA"
       "AAAbBAAAAAAAABwEAAAAAAAAHQQAAAAAAAAeBAAAAAAAAB8EAAAAAAAAIAQAAAAAAAAhBAAAAAAAACIEAAAAAAAAIwQAAAAAAAAkBAAAAAAA"
       "ACUEAAAAAAAAJgQAAAAAAAAnBAAAAAAAACgEAAAAAAAAKQQAAAAAAAAqBAAAAAAAACsEAAAAAAAALAQAAAAAAAAtBAAAAAAAAC4EAAAAAAAA"
       "LwQAAAAAAAAwBAAAAAAAADEEAAAAAAAAMgQAAAAAAAAzBAAAAAAAADQEAAAAAAAANQQAAAAAAAA2BAAAAAAAADcEAAAAAAAAOAQAAAAAAAA5"
       "BAAAAAAAADoEAAAAAAAAOwQAAAAAAAA8BAAAAAAAAD0EAAAAAAAAPgQAAAAAAAA/BAAAAAAAAEAEAAAAAAAAQQQAAAAAAABCBAAAAAAAAEME"
       "AAAAAAAARAQAAAAAAABFBAAAAAAAAEYEAAAAAAAARwQAAAAAAABIBAAAAAAAAEkEAAAAAAAASgQAAAAAAABLBAAAAAAAABICCAc="},
      {"malformed_fixed", "CgQIARgqcgsKCQoDYmFkEgIIAA=="},
      {"malformed_privacy", "CgQIARgqQgUaA2JhZA=="},
      {"split_snapshot",
       "CgIIAXJBCj8KEGUAAAAAAAAA5wMAAAAAAAASEQgAsgQMemVybyB3cmFwcGVyGggKBlNvdXJjZZIDDXNvdXJjZSBvcGFxdWVCChoIZQAAAAAA"
       "AAAKAhgqcoIB2gQMZ3JvdXAgb3BhcXVlCi0JygAAAAAAAAASAggHGgkKB0ZyaWVuZHMiEwjW6Ei6BAxjb2xvciBvcGFxdWUSKGUAAAAAAAAA"
       "5wMAAAAAAADKAAAAAAAAAC8BAAAAAAAAlAEAAAAAAAAKGAkvAQAAAAAAANIFDGxvb3NlIG9wYXF1ZUIJaXgDAAAAAAAA"},
      {"split_privacy_patch", "Qh0aCGUAAAAAAAAAaXgDAAAAAAAAgQFlAAAAAAAAAA=="}
  };
  Json fixture(const std::string& name) { return {{"settings", fixtures.at(name)}}; }
} // namespace

int main() {
  try {
    const auto snapshot = fixture("snapshot");
    const auto original = api::parseGuildLayout(snapshot);
    require(
        original.folders.size() == 3
            && original.folders[0].id == "0"
            && original.folders[0].guildIds == std::vector<std::string>{"101", "999"}
            && original.folders[1].id == "7"
            && original.folders[1].color == 0x123456
            && original.positions == std::vector<std::string>{"101", "999", "202", "303", "404"},
        "Fixtures carry unloaded guilds, a zero-ID folder, color metadata and independent server positions"
    );
    const auto editedFolder = api::guildFolderPatch(snapshot, {.folder = "7", .name = "月の庭", .color = 0xABCDEF});
    const auto editedLayout = api::parseGuildLayout(editedFolder);
    require(
        editedFolder["settings"]
            == "csYBCj8KEGUAAAAAAAAA5wMAAAAAAAASEQgAsgQMemVybyB3cmFwcGVyGggKBlNvdXJjZZIDDXNvdXJjZSBvcGFxdWXaBAxncm91"
               "cCBvcGFxdWUKMAnKAAAAAAAAABICCAcaCwoJ5pyI44Gu5bqtIhS6BAxjb2xvciBvcGFxdWUI75uvBRIoZQAAAAAAAADnAwAAAAAA"
               "AMoAAAAAAAAALwEAAAAAAACUAQAAAAAAAAoYCS8BAAAAAAAA0gUMbG9vc2Ugb3BhcXVl",
        "Folder edits preserve opaque group, folder and wrapper fields byte-for-byte"
    );
    require(
        editedFolder["required_data_version"] == 42
            && editedLayout.folders[1].name == "月の庭"
            && editedLayout.folders[1].color == 0xABCDEF
            && editedLayout.folders[1].guildIds == original.folders[1].guildIds
            && editedLayout.folders[0].name == original.folders[0].name
            && editedLayout.positions == original.positions,
        "Folder settings retain membership, other folders, ordering and required account version"
    );
    const auto cleared =
        api::parseGuildLayout(api::guildFolderPatch(snapshot, {.folder = "0", .name = "", .color = 0}));
    require(
        cleared.folders[0].name.empty() && cleared.folders[0].color == 0,
        "Zero-ID folders support unnamed folders and black as an actual color"
    );
    for (const auto position : {api::FolderPosition::Top, api::FolderPosition::Up}) {
      const auto changed =
          api::parseGuildLayout(api::guildFolderPatch(snapshot, {.folder = "7", .position = position}));
      require(
          changed.folders[0].id == "7"
              && changed.folders[1].id == "0"
              && changed.folders[0].guildIds == original.folders[1].guildIds
              && changed.positions == original.positions,
          "Moving a folder toward the top moves its complete server group"
      );
    }
    for (const auto position : {api::FolderPosition::Bottom, api::FolderPosition::Down}) {
      const auto changed =
          api::parseGuildLayout(api::guildFolderPatch(snapshot, {.folder = "7", .position = position}));
      require(
          changed.folders[2].id == "7" && changed.folders[1].id.empty(),
          "Moving a folder toward the bottom preserves ungrouped rail entries"
      );
    }
    require(
        api::guildFolderPatch(snapshot, {.folder = "0", .position = api::FolderPosition::Up})["settings"]
            == fixtures.at("noop"),
        "Moving the first folder up is byte-identical"
    );
    rejects(
        [&] { api::guildFolderPatch(snapshot, {.folder = "99", .name = "Missing"}); }, "Deleted folders reject edits"
    );
    for (const auto& change : std::vector<api::GuildFolderChange>{
             {.folder = "7"},
             {.folder = "01", .name = "Name"},
             {.folder = "7", .color = 0x1000000},
             {.folder = "7", .name = std::string(101, 'x')},
             {.folder = "7", .name = "line\nbreak"},
             {.folder = "7", .position = static_cast<api::FolderPosition>(99)}
         })
      require(!api::validFolderChange(change), "Invalid folder edits cannot produce account requests");
    for (const auto name : {"duplicate_guild", "duplicate_folder", "malformed_fixed"})
      rejects(
          [&] { api::guildFolderPatch(fixture(name), {.folder = "0", .name = "Changed"}); },
          "Folder edits reject ambiguous snapshots through the shared organization validator"
      );
    const auto moved = api::guildMovePatch(snapshot, "101", "7");
    require(
        moved["settings"] == fixtures.at("move") && moved["required_data_version"] == 42,
        "A move preserves raw group/folder/wrapper metadata, unloaded IDs and optimistic version"
    );
    const auto movedLayout = api::parseGuildLayout(moved);
    require(
        movedLayout.folders[0].guildIds == std::vector<std::string>{"999"}
            && movedLayout.folders[1].guildIds == std::vector<std::string>{"202", "101"}
            && movedLayout.positions == original.positions,
        "Only source and destination memberships change; unloaded guilds keep their positions"
    );
    const auto ungrouped = api::guildMovePatch(snapshot, "101", "");
    const auto looseLayout = api::parseGuildLayout(ungrouped);
    require(
        ungrouped["settings"] == fixtures.at("ungroup")
            && looseLayout.folders.size() == 4
            && looseLayout.folders[1].id.empty()
            && looseLayout.folders[1].guildIds == std::vector<std::string>{"101"},
        "Ungrouping creates an adjacent top-level server instead of moving it to an arbitrary rail position"
    );
    const auto created = api::guildMovePatch(snapshot, "101", "", "Orbit");
    const auto createdLayout = api::parseGuildLayout(created);
    require(
        created["settings"] == fixtures.at("create")
            && createdLayout.folders.size() == 4
            && createdLayout.folders[1].id == "1"
            && createdLayout.folders[1].name == "Orbit"
            && createdLayout.folders[1].guildIds == std::vector<std::string>{"101"},
        "Create New Folder allocates an unused folder ID and retains other folders"
    );
    require(
        api::guildMovePatch(snapshot, "303", "7")["settings"] == fixtures.at("remove_empty")
            && api::parseGuildLayout(api::guildMovePatch(snapshot, "303", "7")).folders.size() == 2,
        "Moving the only server out of a folder removes the empty source record"
    );
    require(
        api::guildMovePatch(snapshot, "404", "7")["settings"] == fixtures.at("position_only"),
        "A server known only through guild_positions can join an existing folder without losing other positions"
    );
    require(
        api::guildMovePatch(snapshot, "101", "0")["settings"] == fixtures.at("noop"),
        "An existing destination membership is an unchanged versioned organization patch"
    );
    require(
        api::guildMovePatch(fixture("split_snapshot"), "101", "7")["settings"] == fixtures.at("move"),
        "Split protobuf organization and version messages merge before an explicit move"
    );
    const auto named = api::parseGuildLayout(api::guildMovePatch(snapshot, "101", "", "月の庭"));
    require(named.folders[1].name == "月の庭", "Folder names support valid Unicode");

    rejects([&] { api::guildMovePatch(snapshot, "101", "99"); }, "Missing destinations cannot silently become folders");
    rejects([&] { api::guildMovePatch(snapshot, "505", "7"); }, "Unknown servers cannot be added to organization");
    rejects(
        [&] { api::guildMovePatch(snapshot, "101", "7", "Orbit"); }, "Create and existing-folder targets cannot mix"
    );
    rejects([&] { api::guildMovePatch(snapshot, "101", "", ""); }, "An explicitly new folder needs a name");
    rejects([&] { api::guildMovePatch(snapshot, "101", "", "   "); }, "A blank folder name is not accepted");
    rejects([&] { api::guildMovePatch(snapshot, "101", "", std::string(101, 'x')); }, "Folder names are bounded");
    rejects([&] { api::guildMovePatch(snapshot, "101", "", "line\nbreak"); }, "Folder names cannot contain controls");
    rejects([&] { api::guildMovePatch(snapshot, "101", "", std::string("\xff", 1)); }, "Invalid UTF-8 is rejected");
    for (const auto id : {"0", "01", "../101", "18446744073709551616"})
      rejects([&] { api::guildMovePatch(snapshot, id, "7"); }, "Guild identifiers must be canonical nonzero fixed64s");
    for (const auto id : {"01", "-1", "9223372036854775808", "../7"})
      rejects([&] { api::guildMovePatch(snapshot, "101", id); }, "Folder identifiers must fit their signed wrapper");
    for (const auto name : {"duplicate_guild", "duplicate_folder", "malformed_fixed"})
      rejects(
          [&] { api::guildMovePatch(fixture(name), "101", "0"); }, "Ambiguous or malformed server lists reject writes"
      );
    rejects(
        [&] { api::guildMovePatch(fixture("full_folder"), "101", "7"); }, "A destination cannot exceed 100 servers"
    );
    rejects(
        [&] { api::guildMovePatch(fixture("missing_version"), "101", "7"); },
        "Organization writes require a fresh account data version"
    );
    rejects([&] { api::guildMovePatch(Json::object(), "101", "7"); }, "Missing snapshots cannot become account writes");
    rejects([&] { api::guildMovePatch({{"settings", "not base64!"}}, "101", "7"); }, "Corrupt snapshots fail closed");

    const auto privacy = api::parseGuildPrivacy(snapshot, "101");
    require(
        privacy.directMessages == false
            && privacy.activity == true
            && privacy.activityJoining == false
            && privacy.messageRequests == true
            && !privacy.empty(),
        "Privacy reads resolve allowed values from the four guild-specific restriction arrays"
    );
    const auto unrestricted = api::parseGuildPrivacy(snapshot, "404");
    require(
        unrestricted.directMessages == true
            && unrestricted.activity == true
            && unrestricted.activityJoining == true
            && unrestricted.messageRequests == true,
        "No restriction-array membership means allowed without inheriting new-server defaults"
    );
    const auto privacyPatch = api::guildPrivacyPatch(snapshot, "101", {.directMessages = true, .activity = false});
    require(
        privacyPatch["settings"] == fixtures.at("privacy_patch") && privacyPatch["required_data_version"] == 42,
        "Partial privacy edits preserve unrelated guild IDs, untouched lists, scalar preferences and opaque metadata"
    );
    const auto edited = api::parseGuildPrivacy(privacyPatch, "101");
    const auto unrelated = api::parseGuildPrivacy(privacyPatch, "777");
    require(
        edited.directMessages == true
            && edited.activity == false
            && edited.activityJoining == false
            && edited.messageRequests == true
            && unrelated.directMessages == false
            && unrelated.messageRequests == false,
        "Allowing one server and restricting another activity leaves other guild restrictions unchanged"
    );
    require(
        api::guildPrivacyPatch(snapshot, "101", {.directMessages = false})["settings"] == fixtures.at("privacy_noop"),
        "An already restricted guild does not gain duplicate entries or normalize untouched wire data"
    );
    require(
        api::guildPrivacyPatch(fixture("split_snapshot"), "101", {.messageRequests = false})["settings"]
            == fixtures.at("split_privacy_patch"),
        "Split privacy messages preserve earlier restrictions during a later partial save"
    );
    rejects([&] { api::guildPrivacyPatch(snapshot, "101", {}); }, "Empty privacy writes are rejected");
    rejects(
        [&] { api::guildPrivacyPatch(fixture("missing_version"), "101", {.directMessages = true}); },
        "Privacy writes require a fresh account data version"
    );
    rejects([&] { api::parseGuildPrivacy(snapshot, "../101"); }, "Privacy reads validate the selected guild");
    rejects(
        [&] { api::guildPrivacyPatch(fixture("malformed_privacy"), "101", {.directMessages = true}); },
        "Malformed packed privacy IDs must not be overwritten"
    );
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
