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
    const auto split =
        api::parseAccountSettings(json{{"settings", "MgzqAQIIAfIBBAoCCAEyE+oBAhAD8gELCgkiB3Vua25vd24="}});
    require(
        split.at("explicit_guild_media") == 1
            && split.at("explicit_friend_media") == 3
            && split.at("filter_profanity") == 1,
        "Duplicate protobuf messages merge recursively rather than clearing earlier values"
    );
    const json nestedSnapshot{
        {"settings",
         "CgIYEzJV6gEVCAEQApoGDnVua25vd24tZmlsdGVy8gErCgIIARITGg91bmtub3duLXdyYXBwZXIIAcIFD3Vua25vd24ta2V5d29yZKIGDHVua"
         "25vd24tdGV4dGIwChciDnVua25vd24tbG9jYWxlCgVlbi1VU+oEFHVua25vd24tbG9jYWxpemF0aW9u"}
    };
    const auto nestedValues = api::parseAccountSettings(nestedSnapshot);
    require(
        nestedValues.at("explicit_friend_media") == 2
            && nestedValues.at("filter_sexual") == 1
            && nestedValues.at("locale") == 0,
        "Read nested media filters, wrappers and string locale"
    );
    const auto nestedEdit =
        api::accountSettingsPatch(nestedSnapshot, {{"explicit_friend_media", 3}, {"filter_sexual", 0}, {"locale", 11}});
    require(
        nestedEdit["settings"]
                == "MlWiBgx1bmtub3duLXRleHTqARUIAZoGDnVua25vd24tZmlsdGVyEAPyASsKAggBwgUPdW5rbm93bi1rZXl3b3JkEhMaD3Vua25"
                   "vd24td3JhcHBlcggAYi3qBBR1bmtub3duLWxvY2FsaXphdGlvbgoUIg51bmtub3duLWxvY2FsZQoCZGU="
            && nestedEdit["required_data_version"] == 19,
        "Nested settings saves preserve opaque siblings and wrapper fields at every level"
    );
    const auto editedNested = api::parseAccountSettings(nestedEdit);
    require(
        editedNested.at("explicit_friend_media") == 3
            && editedNested.at("explicit_guild_media") == 1
            && editedNested.at("filter_profanity") == 1
            && editedNested.at("filter_sexual") == 0
            && editedNested.at("locale") == 11,
        "Nested updates change only selected values"
    );
    require(
        !api::validSettingChanges({{"explicit_friend_media", 4}}) && !api::validSettingChanges({{"locale", 32}}),
        "Reject invalid filter and locale enums"
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
