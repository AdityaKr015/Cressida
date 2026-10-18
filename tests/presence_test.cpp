#include "api/models.h"

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
    const auto current = api::parsePresenceSettings(snapshot);
    require(
        current
            && current->status == "online"
            && current->text == "working"
            && current->emojiId == "77"
            && current->emojiName == "wave",
        "Load existing status and emoji"
    );
    auto status = api::presenceSettingsPatch(snapshot, "dnd", {});
    require(
        status["settings"]
                == "Wk8SJwoHd29ya2luZxFNAAAAAAAAABoEd2F2ZUoNZnV0dXJlLWN1c3RvbRoCCAAhQOIBAAAAAACaBg9rZWVwLXRoaXMtZmllbGQ"
                   "KBQoDZG5k"
            && status["required_data_version"] == 42,
        "Change only status while preserving the complete raw status message and optimistic version"
    );
    require(
        api::parseGuildLayout(status).folders.empty() && api::parseGuildLayout(status).positions.empty(),
        "Presence patches must omit unrelated top-level settings"
    );
    auto custom = api::presenceSettingsPatch(snapshot, "dnd", "new text");
    require(
        custom["settings"] == "WjIaAggAIUDiAQAAAAAAmgYPa2VlcC10aGlzLWZpZWxkCgUKA2RuZBIKCghuZXcgdGV4dA==",
        "Explicit custom-status edits preserve other status subfields"
    );
    auto cleared = api::presenceSettingsPatch(snapshot, "online", "");
    require(
        cleared["settings"] == "WisaAggAIUDiAQAAAAAAmgYPa2VlcC10aGlzLWZpZWxkCggKBm9ubGluZRIA"
            && api::parsePresenceSettings(cleared)->text.empty(),
        "Clear custom status with an explicit empty submessage"
    );
    require(
        !api::validStatus("busy") && !api::validStatus("offline") && api::validStatus("invisible"),
        "Only outbound Discord status values are accepted"
    );
    bool rejected = false;
    try {
      (void)api::presenceSettingsPatch(snapshot, "invalid", {});
    } catch (...) {
      rejected = true;
    }
    require(rejected, "Malformed statuses cannot become account writes");

    const json accountSnapshot{
        {"settings",
         "CgIYOUIc2gECCABaAggOmgYQcHJlc2VydmUgcHJpdmFjeTodCgIIAboFFnByZXNlcnZlIG5vdGlmaWNhdGlvbnNaIAoICgZvbmxpbmUaAggB2"
         "gUPcHJlc2VydmUgc3RhdHVzcgxvcmdhbml6YXRpb26iBgdvcGFxdWUA"}
    };
    const auto values = api::parseAccountSettings(accountSnapshot);
    require(
        values.at("restrict_new_guild_dms") == 0
            && values.at("friend_sources") == 14
            && values.at("show_activity") == 1,
        "Decode account preferences and skip unknown opaque groups"
    );
    const auto edit = api::accountSettingsPatch(
        accountSnapshot, {{"restrict_new_guild_dms", 1}, {"in_app_notifications", 0}, {"show_activity", 0}}
    );
    require(
        edit["settings"]
                == "Oh26BRZwcmVzZXJ2ZSBub3RpZmljYXRpb25zCgIIAEIcWgIIDpoGEHByZXNlcnZlIHByaXZhY3naAQIIAVogCggKBm9ubGluZdo"
                   "FD3ByZXNlcnZlIHN0YXR1cxoCCAA="
            && edit["required_data_version"] == 57,
        "Preserve complete raw modified groups, status and version; omit unrelated settings"
    );
    const auto changedValues = api::parseAccountSettings(edit);
    require(
        changedValues.at("in_app_notifications") == 0 && changedValues.at("show_activity") == 0,
        "Read explicit false wrappers"
    );
    require(
        !api::validSettingChanges({})
            && !api::validSettingChanges({{"token", 1}})
            && !api::validSettingChanges({{"show_activity", 2}})
            && !api::validSettingChanges({{"friend_sources", 8}})
            && api::validSettingChanges({{"friend_sources", 6}}),
        "Only bounded, coherent whitelisted changes may be saved"
    );
    rejected = false;
    try {
      (void)api::accountSettingsPatch(json::object(), {{"show_activity", 0}});
    } catch (...) {
      rejected = true;
    }
    require(rejected, "Never overwrite account settings without a fresh data version");
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
