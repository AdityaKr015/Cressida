#include "discord/models.h"

#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

int main() {
  using nlohmann::json;
  try {
    auto require = [](bool value, const char* message) {
      if (!value)
        throw std::runtime_error(message);
    };
    // Independent protobuf fixture: wrappers, fixed64s, unknown fields, version,
    // and organization data. A presence PATCH must preserve every status subfield.
    const json snapshot{
        {"settings",
         "CgQIARgqWlIKCAoGb25saW5lEicKB3dvcmtpbmcRTQAAAAAAAAAaBHdhdmVKDWZ1dHVyZS1jdXN0b20aAggAIUDiAQAAAAAAmgYPa2VlcC10a"
         "GlzLWZpZWxkcgoSCHsAAAAAAAAA"}
    };
    const auto current = discord::parsePresenceSettings(snapshot);
    require(
        current
            && current->status == "online"
            && current->text == "working"
            && current->emojiId == "77"
            && current->emojiName == "wave",
        "Load existing status and emoji"
    );
    auto status = discord::presenceSettingsPatch(snapshot, "dnd", {});
    require(
        status["settings"]
                == "Wk8SJwoHd29ya2luZxFNAAAAAAAAABoEd2F2ZUoNZnV0dXJlLWN1c3RvbRoCCAAhQOIBAAAAAACaBg9rZWVwLXRoaXMtZmllbGQ"
                   "KBQoDZG5k"
            && status["required_data_version"] == 42,
        "Change only status while preserving the complete raw status message and optimistic version"
    );
    require(
        discord::parseGuildLayout(status).folders.empty() && discord::parseGuildLayout(status).positions.empty(),
        "Presence patches must omit unrelated top-level settings"
    );
    auto custom = discord::presenceSettingsPatch(snapshot, "dnd", "new text");
    require(
        custom["settings"] == "WjIaAggAIUDiAQAAAAAAmgYPa2VlcC10aGlzLWZpZWxkCgUKA2RuZBIKCghuZXcgdGV4dA==",
        "Explicit custom-status edits preserve other status subfields"
    );
    auto cleared = discord::presenceSettingsPatch(snapshot, "online", "");
    require(
        cleared["settings"] == "WisaAggAIUDiAQAAAAAAmgYPa2VlcC10aGlzLWZpZWxkCggKBm9ubGluZRIA"
            && discord::parsePresenceSettings(cleared)->text.empty(),
        "Clear custom status with an explicit empty submessage"
    );
    require(
        !discord::validStatus("busy") && !discord::validStatus("offline") && discord::validStatus("invisible"),
        "Only outbound Discord status values are accepted"
    );
    bool rejected = false;
    try {
      (void)discord::presenceSettingsPatch(snapshot, "invalid", {});
    } catch (...) {
      rejected = true;
    }
    require(rejected, "Malformed statuses cannot become account writes");

    const json accountSnapshot{
        {"settings",
         "CgIYOUIc2gECCABaAggOmgYQcHJlc2VydmUgcHJpdmFjeTodCgIIAboFFnByZXNlcnZlIG5vdGlmaWNhdGlvbnNaIAoICgZvbmxpbmUaAggB2"
         "gUPcHJlc2VydmUgc3RhdHVzcgxvcmdhbml6YXRpb26iBgdvcGFxdWUA"}
    };
    const auto values = discord::parseAccountSettings(accountSnapshot);
    require(
        values.at("restrict_new_guild_dms") == 0
            && values.at("friend_sources") == 14
            && values.at("show_activity") == 1,
        "Decode account preferences and skip unknown opaque groups"
    );
    const auto edit = discord::accountSettingsPatch(
        accountSnapshot, {{"restrict_new_guild_dms", 1}, {"in_app_notifications", 0}, {"show_activity", 0}}
    );
    require(
        edit["settings"]
                == "Oh26BRZwcmVzZXJ2ZSBub3RpZmljYXRpb25zCgIIAEIcWgIIDpoGEHByZXNlcnZlIHByaXZhY3naAQIIAVogCggKBm9ubGluZdo"
                   "FD3ByZXNlcnZlIHN0YXR1cxoCCAA="
            && edit["required_data_version"] == 57,
        "Preserve complete raw modified groups, status and version; omit unrelated settings"
    );
    const auto changedValues = discord::parseAccountSettings(edit);
    require(
        changedValues.at("in_app_notifications") == 0 && changedValues.at("show_activity") == 0,
        "Read explicit false wrappers"
    );
    require(
        !discord::validSettingChanges({})
            && !discord::validSettingChanges({{"token", 1}})
            && !discord::validSettingChanges({{"show_activity", 2}})
            && !discord::validSettingChanges({{"friend_sources", 8}})
            && discord::validSettingChanges({{"friend_sources", 6}}),
        "Only bounded, coherent whitelisted changes may be saved"
    );
    rejected = false;
    try {
      (void)discord::accountSettingsPatch(json::object(), {{"show_activity", 0}});
    } catch (...) {
      rejected = true;
    }
    require(rejected, "Never overwrite account settings without a fresh data version");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
