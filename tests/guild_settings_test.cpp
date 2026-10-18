#include "api/guild_settings.h"

#include <array>
#include <iostream>
#include <stdexcept>

using namespace api;
using nlohmann::json;
namespace {
  void require(bool condition, const char* message) {
    if (!condition)
      throw std::runtime_error(message);
  }
  template <class F> void rejects(F function, const char* message) {
    try {
      function();
    } catch (const std::exception&) {
      return;
    }
    throw std::runtime_error(message);
  }
} // namespace

int main() {
  try {
    using S = GuildSettingsSection;
    using V = GuildSettingsVerb;
    GuildSettingsRequest request{.guildId = "1"};
    require(
        guildSettingsHttpRequest(request)->path == "/guilds/1?with_counts=true",
        "Overview reads the exact selected server"
    );
    request.verb = V::Update;
    request.values = {{"name", "New name"}, {"description", "Only changed fields"}};
    auto http = guildSettingsHttpRequest(request);
    require(
        http && http->method == "PATCH" && http->body == request.values, "Overview patches only explicit changed fields"
    );
    request.values["token"] = "unwanted";
    require(!guildSettingsHttpRequest(request), "Unknown or credential-like writable fields are rejected");
    request.values = {{"mfa_level", 1}};
    http = guildSettingsHttpRequest(request);
    require(
        http && http->path == "/guilds/1/mfa" && http->method == "POST" && http->body == json{{"level", 1}},
        "Moderator 2FA uses Discord's client MFA endpoint"
    );
    request.values = {{"mfa_level", 1}, {"name", "Cannot combine"}};
    require(!guildSettingsHttpRequest(request), "MFA cannot silently drop unrelated guild changes");
    {
      constexpr auto rawBytes = 10U * 1024 * 1024;
      const auto tenMiBImage = std::string("data:image/png;base64,") + std::string(((rawBytes + 2) / 3) * 4, 'A');
      const auto oversizedImage = std::string("data:image/png;base64,") + std::string(14U * 1024 * 1024, 'A');
      const std::array imageSections{S::Overview, S::Profile, S::Events, S::Webhooks};
      for (const auto section : imageSections) {
        const auto field = section == S::Overview ? "icon"
            : section == S::Profile               ? "custom_banner"
            : section == S::Events                ? "image"
                                                  : "avatar";
        GuildSettingsRequest upload{
            .section = section,
            .verb = V::Update,
            .guildId = "1",
            .resourceId = section == S::Events || section == S::Webhooks ? "2" : "",
            .values = {{field, tenMiBImage}}
        };
        require(
            guildSettingsHttpRequest(upload).has_value(),
            "General server images accept the base64 expansion of a valid 10 MiB raw upload"
        );
        upload.values[field] = oversizedImage;
        require(
            !guildSettingsHttpRequest(upload), "General server image data URIs remain bounded after base64 expansion"
        );
      }
      GuildSettingsRequest aggregate{
          .verb = V::Update, .guildId = "1", .values = {{"icon", tenMiBImage}, {"banner", tenMiBImage}}
      };
      require(
          !guildSettingsHttpRequest(aggregate),
          "Multiple valid images still respect the aggregate 16 MiB mutation limit"
      );
    }
    request = {
        .section = S::Profile,
        .verb = V::Update,
        .guildId = "1",
        .values = {
            {"traits", json::array({{{"label", "Cozy"}, {"position", 0}, {"emoji_name", "🌿"}}})},
            {"game_application_ids", json::array({"20", "30"})},
            {"brand_color_primary", "#123456"}
        }
    };
    require(
        guildSettingsHttpRequest(request)->path == "/guilds/1/profile",
        "Server profiles have their own traits, games and brand-color route"
    );
    request.values = {{"visibility", 2}};
    require(
        guildSettingsHttpRequest(request)->path == "/guilds/1/profile/visibility",
        "Server profile privacy uses its dedicated visibility route"
    );
    request = {.section = S::OwnershipCode, .verb = V::Create, .guildId = "1"};
    http = guildSettingsHttpRequest(request);
    require(
        http && http->method == "PUT" && http->path == "/guilds/1/pincode",
        "Ownership email verification only sends after an explicit action"
    );
    request = {.verb = V::Update, .guildId = "1", .values = {{"owner_id", "2"}, {"code", "123456"}}};
    require(
        guildSettingsHttpRequest(request)->body["code"] == "123456",
        "Ownership transfer retains the explicitly entered verification code"
    );
    request = {.section = S::Applications, .guildId = "1", .query = "Deep Rock"};
    require(
        guildSettingsHttpRequest(request)->path == "/games/autocomplete?q=Deep%20Rock&filter_group=guild_profile_games",
        "Server game choices use Discord's own profile autocomplete filter"
    );
    const auto games =
        parseGuildSettings(
            json::array(
                {{{"id", "20"}, {"name", "Game"}, {"unrelated", "not retained"}}, {{"id", 30}, {"name", "Other"}}}
            ),
            request
        )
            .data;
    require(
        games.size() == 2 && games[1]["id"] == "30" && !games[0].contains("unrelated"),
        "Game suggestions are projected to bounded display records"
    );
    request = {
        .section = S::Discovery, .verb = V::Update, .guildId = "1", .values = {{"about", "An updated description"}}
    };
    require(
        guildSettingsHttpRequest(request)->path == "/guilds/1/discovery-metadata",
        "Discovery editing has a fixed metadata route"
    );
    const auto discovery = guildDiscoveryPatch(
        {{"guild_id", "1"},
         {"keywords", json::array({"keep"})},
         {"primary_category_id", 49},
         {"about", "Old"},
         {"partner_actioned_timestamp", nullptr}},
        request.values
    );
    require(
        discovery["keywords"] == json::array({"keep"})
            && discovery["about"] == "An updated description"
            && !discovery.contains("guild_id")
            && !discovery.contains("partner_actioned_timestamp"),
        "Fresh discovery patches retain every untouched writable field and omit server-owned metadata"
    );
    request.values = {{"partner_actioned_timestamp", nullptr}};
    require(!guildSettingsHttpRequest(request), "Clients cannot overwrite server-owned discovery audit fields");
    request = {
        .section = S::Insights,
        .guildId = "1",
        .resourceId = "growth",
        .values = {{"start", "2026-09-01T00:00:00.000Z"}, {"end", "2026-10-01T00:00:00.000Z"}, {"interval", 1}}
    };
    http = guildSettingsHttpRequest(request);
    require(
        http
            && http->path
                == "/guilds/1/analytics/growth-activation/"
                   "overview?start=2026-09-01T00%3A00%3A00.000Z&end=2026-10-01T00%3A00%3A00.000Z&interval=1",
        "Guild Insights uses the verified ISO date query and guild-specific daily interval"
    );
    request.values["interval"] = 0;
    require(!guildSettingsHttpRequest(request), "The generic chart interval zero is not a guild analytics interval");
    request.values = {{"days", 28}, {"interval", 2}};
    require(
        guildSettingsHttpRequest(request).has_value(), "Insights supports bounded relative periods without page polling"
    );
    request.values["days"] = 3650;
    require(!guildSettingsHttpRequest(request), "Insights cannot request unbounded analytics ranges");
    const auto intervals = parseGuildSettings(json::array({{{"day_pt", "2026-10-01"}, {"members", 2}}}), request).data;
    require(
        intervals[0]["interval_start_timestamp"] == "2026-10-01",
        "Insights preserves legacy interval timestamps through the same normalization as Discord"
    );
    request = {
        .section = S::Roles,
        .verb = V::Update,
        .guildId = "1",
        .resourceId = "2",
        .values = {
            {"permissions", "1267650600228229401496703205377"},
            {"colors", {{"primary_color", 0xABCDEF}, {"secondary_color", nullptr}, {"tertiary_color", nullptr}}}
        }
    };
    http = guildSettingsHttpRequest(request);
    require(
        http && http->path == "/guilds/1/roles/2" && http->body["permissions"] == "1267650600228229401496703205377",
        "Permission strings preserve unknown high bits without integer conversions"
    );
    auto permission = guildRolePermission("1267650600228229401496703205377", 28, true);
    require(
        guildRolePermission(permission, 28, false) == "1267650600228229401496703205377",
        "Permission toggles change one bit and preserve distant bits"
    );
    require(guildRolePermission("000000000", 0, false) == "0", "Permission arithmetic normalizes leading zeroes");
    rejects([] { guildRolePermission("3x", 0, true); }, "Malformed decimal permissions cannot be toggled");
    request.values["permissions"] = 9007199254740992ULL;
    require(!guildSettingsHttpRequest(request), "Permissions must never be rounded through JSON numbers");
    request = {
        .section = S::Roles, .verb = V::Reorder, .guildId = "1", .values = json::array({{{"id", "2"}, {"position", 3}}})
    };
    http = guildSettingsHttpRequest(request);
    require(
        http && http->method == "PATCH" && http->path == "/guilds/1/roles", "Role ordering uses the bulk position route"
    );
    request.resourceId = "2";
    require(
        guildSettingsHttpRequest(request).has_value(),
        "Role ordering can retain the selected role identity for hierarchy checks"
    );
    request.resourceId = "3";
    require(
        !guildSettingsHttpRequest(request), "Role ordering cannot validate one role and reorder an unrelated identity"
    );
    request.resourceId.clear();
    request.values[0]["id"] = "1";
    require(!guildSettingsHttpRequest(request), "The everyone role cannot be reordered");
    request = {.section = S::Members, .guildId = "1", .query = "a&b/é", .limit = 50};
    http = guildSettingsHttpRequest(request);
    require(
        http
            && http->path == "/guilds/1/members-search"
            && http->method == "POST"
            && http->body["and_query"] == json{{"usernames", {{"or_query", json::array({"a&b/é"})}}}},
        "Personal-account member searches use the indexed management endpoint with data, not URL fragments"
    );
    request.query.clear();
    request.cursor = json{{"guild_joined_at", 1700000000000LL}, {"user_id", "90"}}.dump();
    require(
        guildSettingsHttpRequest(request)->body["after"]
            == json{{"guild_joined_at", 1700000000000LL}, {"user_id", "90"}},
        "Member pages retain the complete indexed search cursor"
    );
    request = {
        .section = S::Members, .verb = V::Update, .guildId = "1", .resourceId = "4", .values = {{"nick", nullptr}}
    };
    require(
        guildSettingsHttpRequest(request)->body == json{{"nick", nullptr}},
        "Clearing a nickname does not rewrite roles or timeout state"
    );
    request.values = {{"roles", json::array({"2", "2"})}};
    require(!guildSettingsHttpRequest(request), "Duplicate member role identities are rejected");
    request = {
        .section = S::Bans,
        .verb = V::Create,
        .guildId = "1",
        .resourceId = "4",
        .values = {{"delete_message_seconds", 86400}}
    };
    http = guildSettingsHttpRequest(request);
    require(
        http && http->method == "PUT" && http->path == "/guilds/1/bans/4",
        "Bans target the exact member and explicit history window"
    );
    request.values["delete_message_seconds"] = 604801;
    require(!guildSettingsHttpRequest(request), "Ban history deletion is bounded to seven days");
    request = {.section = S::Invites, .verb = V::Delete, .guildId = "1", .resourceId = "safe_code-2"};
    require(
        guildSettingsHttpRequest(request)->path == "/invites/safe_code-2",
        "Invite revocation uses an encoded invite identity"
    );
    request.resourceId = "../9";
    require(!guildSettingsHttpRequest(request), "Invite routes reject traversal");
    request = {
        .section = S::AuditLog,
        .guildId = "1",
        .values = {{"user_id", "2"}, {"action_type", 22}},
        .cursor = "90",
        .limit = 20
    };
    require(
        guildSettingsHttpRequest(request)->path == "/guilds/1/audit-logs?limit=20&before=90&user_id=2&action_type=22",
        "Audit logs retain all chosen filters while paging"
    );
    request = {
        .section = S::Emoji,
        .verb = V::Create,
        .guildId = "1",
        .values = {{"name", "test_emoji"}, {"image", "data:image/png;base64,AQIDBA=="}, {"roles", json::array({"2"})}}
    };
    require(
        guildSettingsHttpRequest(request).has_value(), "Emoji uploads accept bounded image data and role restrictions"
    );
    request.values["image"] = "https://untrusted.example/image.png";
    require(!guildSettingsHttpRequest(request), "Uploads never ask the worker to fetch arbitrary URLs");
    request = {
        .section = S::Stickers,
        .verb = V::Create,
        .guildId = "1",
        .values = {
            {"name", "Sticker"},
            {"tags", "smile"},
            {"description", "A sticker"},
            {"file", json::binary({1, 2, 3})},
            {"filename", "sticker.png"},
            {"content_type", "image/png"}
        }
    };
    require(guildSettingsHttpRequest(request)->stickerUpload, "Sticker creation uses the multipart upload path");
    request.values["filename"] = "../sticker.png";
    require(!guildSettingsHttpRequest(request), "Multipart file names cannot be paths");
    request = {
        .section = S::Soundboard,
        .verb = V::Create,
        .guildId = "1",
        .values = {{"name", "Sound"}, {"sound", "data:audio/ogg;base64,AQIDBA=="}, {"volume", 0.5}}
    };
    require(guildSettingsHttpRequest(request).has_value(), "Sound creation accepts supported audio data");
    request.values["volume"] = 1.5;
    require(!guildSettingsHttpRequest(request), "Sound volume remains in range");
    request = {
        .section = S::Webhooks, .verb = V::Create, .guildId = "1", .resourceId = "6", .values = {{"name", "Incoming"}}
    };
    require(
        guildSettingsHttpRequest(request)->path == "/channels/6/webhooks",
        "Webhook creation uses an explicit channel, not the currently open chat"
    );
    request.values["token"] = "not-writable";
    require(!guildSettingsHttpRequest(request), "A webhook token cannot be injected into creation");
    request = {.section = S::Onboarding, .verb = V::Update, .guildId = "1", .values = {{"enabled", false}}};
    require(
        guildSettingsHttpRequest(request)->method == "PUT",
        "Onboarding uses the correct PUT operation without replacing absent configuration fields"
    );
    request = {
        .section = S::Welcome,
        .verb = V::Update,
        .guildId = "1",
        .values = {
            {"welcome_channels",
             json::array(
                 {{{"channel_id", "3"}, {"description", "Chat here"}, {"emoji_id", nullptr}, {"emoji_name", "👋"}}}
             )}
        }
    };
    require(
        guildSettingsHttpRequest(request).has_value(), "Welcome channels validate each destination and description"
    );
    request = {
        .section = S::AutoModeration,
        .verb = V::Create,
        .guildId = "1",
        .values = {
            {"name", "Keyword rule"},
            {"event_type", 1},
            {"trigger_type", 1},
            {"trigger_metadata", {{"keyword_filter", json::array({"bad*"})}}},
            {"actions", json::array({{{"type", 1}, {"metadata", {{"custom_message", "Please follow the rules"}}}}})}
        }
    };
    require(
        guildSettingsHttpRequest(request).has_value(), "AutoMod validates nested rule metadata and explicit actions"
    );
    request.values["actions"][0] = {{"type", 3}, {"metadata", {{"duration_seconds", 2419201}}}};
    require(!guildSettingsHttpRequest(request), "AutoMod timeouts cannot exceed Discord's maximum");
    request = {
        .section = S::Events,
        .verb = V::Create,
        .guildId = "1",
        .values = {
            {"name", "Meetup"},
            {"privacy_level", 2},
            {"entity_type", 3},
            {"entity_metadata", {{"location", "Somewhere"}}},
            {"scheduled_start_time", "2030-01-01T12:00:00Z"},
            {"scheduled_end_time", "2030-01-01T13:00:00Z"}
        }
    };
    require(guildSettingsHttpRequest(request).has_value(), "External scheduled events require a location and end time");
    request.values.erase("scheduled_end_time");
    require(!guildSettingsHttpRequest(request), "Incomplete external events are rejected before sending");
    request = {.section = S::Templates, .verb = V::Sync, .guildId = "1", .resourceId = "safe-code"};
    http = guildSettingsHttpRequest(request);
    require(
        http && http->method == "PUT" && http->path == "/guilds/1/templates/safe-code",
        "Template synchronization is explicit and uses the correct route"
    );
    request = {.section = S::DeleteGuild, .verb = V::Delete, .guildId = "1"};
    http = guildSettingsHttpRequest(request);
    require(
        http && http->method == "POST" && http->path == "/guilds/1/delete",
        "Server deletion uses Discord's personal-account route"
    );
    request.mfaChallenge = 7;
    request.mfaType = "totp";
    request.mfaCode = "123456";
    require(
        guildSettingsHttpRequest(request).has_value(), "Explicit TOTP verification can resume the same validated action"
    );
    request.mfaType = "arbitrary/endpoint";
    require(!guildSettingsHttpRequest(request), "Verification methods cannot alter verification routes");
    request.mfaType = "password";
    request.mfaCode = std::string(1025, 'x');
    require(!guildSettingsHttpRequest(request), "Verification credentials remain bounded");
    request.mfaChallenge = 0;
    request.mfaType.clear();
    request.mfaCode.clear();
    GuildSettingsAccess access{
        .self = "2", .owner = "9", .permissions = (1ULL << 3), .selfPosition = 10, .targetPosition = 1
    };
    require(!canManageGuildSettings(request, access), "Administrators cannot delete another owner's server");
    access.self = "9";
    require(canManageGuildSettings(request, access), "Only the owner can delete a server");
    request = {.section = S::Roles, .verb = V::Update, .guildId = "1", .resourceId = "4", .values = {{"name", "Role"}}};
    access.self = "2";
    access.targetPosition = 10;
    require(!canManageGuildSettings(request, access), "Administrator permission does not bypass equal role hierarchy");
    access.targetPosition = 5;
    require(canManageGuildSettings(request, access), "Lower non-managed roles remain editable");
    access.targetManaged = true;
    require(!canManageGuildSettings(request, access), "Managed roles cannot be edited");
    request = {.section = S::Members, .verb = V::Delete, .guildId = "1", .resourceId = "9"};
    access.targetManaged = false;
    require(!canManageGuildSettings(request, access), "The owner can never be kicked");
    request.resourceId = "3";
    access.permissions = 0;
    require(!canManageGuildSettings(request, access), "Hierarchy alone does not grant moderation permissions");
    request = {.section = S::Bans, .verb = V::Delete, .guildId = "1", .resourceId = "3"};
    access.permissions = 1ULL << 2;
    access.targetPosition = -1;
    require(
        canManageGuildSettings(request, access), "Unbanning does not require a current membership or role position"
    );
    request = {
        .section = S::Members, .verb = V::Update, .guildId = "1", .resourceId = "2", .values = {{"nick", "Nickname"}}
    };
    access.permissions = 1ULL << 26;
    require(
        canManageGuildSettings(request, access), "Members can change their own nickname with Change Nickname permission"
    );
    request.values["roles"] = json::array({"5"});
    require(!canManageGuildSettings(request, access), "A self nickname update cannot smuggle an own role change");
    request.values = {{"nick", nullptr}};
    request.resourceId = "@me";
    require(
        guildSettingsHttpRequest(request)->path == "/guilds/1/members/@me",
        "Own nickname changes use the current-member route"
    );
    access.permissions = 0;
    require(!canManageGuildSettings(request, access), "Nickname changes still require the own-nickname permission");
    request = {.section = S::Emoji, .verb = V::Create, .guildId = "1"};
    access.permissions = 1ULL << 30;
    require(canManageGuildSettings(request, access), "Manage Expressions includes creating expressions");
    request = {
        .section = S::AutoModeration,
        .verb = V::Update,
        .guildId = "1",
        .resourceId = "5",
        .values = {{"actions", json::array({{{"type", 3}, {"metadata", {{"duration_seconds", 60}}}}})}}
    };
    access.permissions = 1ULL << 5;
    require(!canManageGuildSettings(request, access), "AutoMod timeout rules also require Moderate Members");
    access.permissions = (1ULL << 5) | (1ULL << 40);
    require(
        canManageGuildSettings(request, access),
        "Moderators with both permissions can explicitly configure AutoMod timeouts"
    );
    request.values = {{"actions", json::array({{{"type", 1}}})}};
    access.permissions = 1ULL << 5;
    require(
        canManageGuildSettings(request, access),
        "AutoMod block-only rules retain Manage Server access without timeout permission"
    );
    request = {.section = S::Members, .guildId = "1", .limit = 2};
    const json members{
        {"guild_id", "1"},
        {"members",
         json::array(
             {{{"member", {{"user", {{"id", "20"}}}, {"joined_at", "2023-11-14T22:13:20Z"}}}},
              {{"member", {{"user", {{"id", "21"}}}, {"joined_at", "2023-11-14T23:13:20.123+01:00"}}}}}
         )}
    };
    auto page = parseGuildSettings(members, request);
    require(
        page.more && json::parse(page.cursor) == json{{"guild_joined_at", 1700000000123LL}, {"user_id", "21"}},
        "Member pagination derives the complete cursor from the returned member and timezone"
    );
    require(
        page.data.is_array() && page.data.size() == 2 && page.data[1]["user"]["id"] == "21",
        "Indexed wrappers are projected into regular member records"
    );
    request.cursor = page.cursor;
    require(!parseGuildSettings(members, request).more, "Non-progressing pages cannot cause a pagination loop");
    request = {.guildId = "1"};
    rejects([&] { parseGuildSettings({{"id", "2"}}, request); }, "Foreign guild snapshots are rejected");
    rejects(
        [&] { parseGuildSettings({{"id", "1"}, {"description", std::string(65537, 'a')}}, request); },
        "Oversized server text is bounded"
    );
    request = {
        .section = S::AuditLog,
        .guildId = "1",
        .values = {{"user_id", "20"}, {"action_type", 193}},
        .cursor = "90",
        .limit = 2
    };
    http = guildSettingsHttpRequest(request);
    require(
        http
            && http->path.find("user_id=20") != std::string::npos
            && http->path.find("action_type=193") != std::string::npos
            && http->path.find("before=90") != std::string::npos,
        "Audit filters compose with the exact pagination cursor"
    );
    const json audit{
        {"audit_log_entries", json::array({{{"id", "88"}}, {{"id", "87"}}})},
        {"users", json::array({{{"id", "20"}}})},
        {"application_commands", json::array({{{"id", "42"}}})},
        {"threads", json::array({{{"id", "43"}}})},
        {"auto_moderation_rules", json::array({{{"id", "44"}}})}
    };
    page = parseGuildSettings(audit, request);
    require(
        page.data == audit && page.more && page.cursor == "87",
        "Audit pages retain all supplemental identities and options"
    );
    require(
        guildAuditActions().size() > 60
            && guildAuditActionLabel(213) != "Unknown action"
            && guildAuditActionLabel(99999) == "Unknown action",
        "The action catalog includes current client actions and preserves unknown-action fallbacks"
    );
    request = {
        .section = S::Roles,
        .verb = V::Update,
        .guildId = "1",
        .resourceId = "2",
        .values = {
            {"colors", {{"primary_color", 11127295}, {"secondary_color", 16759788}, {"tertiary_color", 16761760}}}
        }
    };
    require(guildSettingsHttpRequest(request).has_value(), "Official holographic colors are accepted");
    request.values["colors"]["tertiary_color"] = 1;
    require(!guildSettingsHttpRequest(request), "Custom holographic triples cannot be submitted");
    access = {.self = "10", .owner = "11", .targetCreator = "10", .permissions = 1ULL << 43};
    request = {.section = S::Emoji, .verb = V::Update, .guildId = "1", .resourceId = "2", .values = {{"name", "mine"}}};
    require(canManageGuildSettings(request, access), "Create Expressions permits updating one's own expressions");
    access.targetCreator = "12";
    require(
        !canManageGuildSettings(request, access), "Create Expressions does not grant management of others' expressions"
    );
    access.targetCreator = "10";
    access.targetManaged = true;
    access.permissions = 1ULL << 3;
    require(
        !canManageGuildSettings(request, access), "Managed integration expressions stay immutable for administrators"
    );
    access.targetManaged = false;
    access.permissions = 1ULL << 44;
    request.section = S::Events;
    require(canManageGuildSettings(request, access), "Create Events includes editing one's own event");
    require(
        guildExpressionAsset(S::Emoji, {{"id", "2"}, {"animated", true}})
            == "https://cdn.discordapp.com/emojis/2.gif?size=128",
        "Animated emoji assets retain GIF media"
    );
    require(
        guildExpressionAsset(S::Stickers, {{"id", "2"}, {"format_type", 2}})
            == "https://cdn.discordapp.com/stickers/2.png",
        "APNG stickers use their native PNG route"
    );
    require(
        guildExpressionAsset(S::Stickers, {{"id", "2"}, {"format_type", 3}}).ends_with("2.json"),
        "Lottie assets remain explicit JSON rather than a broken PNG"
    );
    require(
        guildExpressionAsset(S::Stickers, {{"id", "2"}, {"format_type", 4}})
            == "https://media.discordapp.net/stickers/2.gif",
        "GIF stickers use Discord's media CDN"
    );
    require(
        guildExpressionAsset(S::Soundboard, {{"sound_id", "2"}}) == "https://cdn.discordapp.com/soundboard-sounds/2",
        "Soundboard previews use the fixed public sound route"
    );
    const json guideSnapshot{
        {"enabled", false},
        {"welcome_message", {{"author_ids", json::array({"10"})}, {"message", "Welcome aboard"}}},
        {"new_member_actions", json::array({{{"channel_id", "2"}, {"action_type", 0}, {"title", "Read rules"}}})},
        {"resource_channels", json::array()}
    };
    auto mergedGuide = guildGuidePatch(
        guideSnapshot, {{"welcome_message", {{"author_ids", json::array({"10"})}, {"message", "A new welcome"}}}}
    );
    require(
        mergedGuide["new_member_actions"] == guideSnapshot["new_member_actions"],
        "Guide saves retain untouched channel actions from a fresh snapshot"
    );
    rejects([&] { guildGuidePatch(guideSnapshot, {{"enabled", true}}); }, "A live guide requires three actions");
    request = {.section = S::ServerGuide, .verb = V::Update, .guildId = "1", .values = {{"enabled", false}}};
    require(
        guildSettingsHttpRequest(request)->path == "/guilds/1/new-member-welcome",
        "Server Guide uses the actual personal-client resource"
    );
    request = {
        .section = S::IncidentActions,
        .verb = V::Update,
        .guildId = "1",
        .values = {{"invites_disabled_until", nullptr}, {"dms_disabled_until", nullptr}}
    };
    require(guildSettingsHttpRequest(request)->method == "PUT", "Incident pauses can be explicitly cleared");
    request.values["dms_disabled_until"] = "2099-01-01T00:00:00Z";
    require(!guildSettingsHttpRequest(request), "Incident pauses cannot exceed the verified 24-hour window");
    request = {
        .section = S::BulkBans,
        .verb = V::Create,
        .guildId = "1",
        .values = {{"user_ids", json::array({"20", "21"})}, {"delete_message_seconds", 86400}}
    };
    require(
        guildSettingsHttpRequest(request)->path == "/guilds/1/bulk-ban/v2",
        "Personal bulk bans use asynchronous v2 moderation"
    );
    access = {.self = "10", .owner = "11", .permissions = (1ULL << 5) | (1ULL << 2)};
    require(canManageGuildSettings(request, access), "Bulk moderation requires both server and ban management");
    request.values["user_ids"].push_back("11");
    require(!canManageGuildSettings(request, access), "Bulk moderation can never include the server owner");
    request = {
        .section = S::Integrations,
        .verb = V::Update,
        .guildId = "1",
        .resourceId = "2",
        .values = {{"expire_behavior", 1}, {"expire_grace_period", 7}, {"enable_emoticons", false}}
    };
    require(
        guildSettingsHttpRequest(request)->method == "PATCH", "Subscription integrations support actual expiry settings"
    );
    request.verb = V::Sync;
    request.values = json::object();
    require(
        guildSettingsHttpRequest(request)->path == "/guilds/1/integrations/2/sync",
        "Integration synchronization is an explicit native action"
    );
    request = {.section = S::Webhooks, .guildId = "1", .resourceId = "2"};
    require(
        guildSettingsHttpRequest(request)->path == "/webhooks/2",
        "Webhook URL copies can fetch a fresh authorized record"
    );
    request = {
        .section = S::Members,
        .guildId = "1",
        .values =
            {{"role_ids", json::array({"2", "3"})},
             {"sort", 2},
             {"joined_after", "2023-11-14T22:13:20Z"},
             {"account_before", "2024-01-01T00:00:00Z"},
             {"require_timeout", true}},
        .query = "alice,20"
    };
    http = guildSettingsHttpRequest(request);
    require(
        http
            && http->body["and_query"]["role_ids"]["and_query"].size() == 2
            && http->body["and_query"]["usernames"]["or_query"][0] == "alice"
            && http->body["and_query"]["user_id"]["or_query"][0] == "20"
            && http->body["or_query"]["safety_signals"].contains("communication_disabled_until"),
        "Member search combines names, IDs, roles, dates, ordering and live safety filters"
    );
    request.values["account_before"] = "9999-01-01T00:00:00Z";
    require(!guildSettingsHttpRequest(request), "Snowflake account-date filters reject overflow");
    request = {
        .section = S::JoinRequests, .guildId = "1", .values = {{"status", "REJECTED"}}, .cursor = "99", .limit = 1
    };
    require(
        guildSettingsHttpRequest(request)->path == "/guilds/1/requests?status=REJECTED&limit=1&before=99",
        "Membership applications filter status and paginate with the actual client route"
    );
    page = parseGuildSettings(
        {{"guild_join_requests",
          json::array({{{"join_request_id", "98"}, {"guild_id", "1"}, {"form_responses", json::array()}}})}},
        request
    );
    require(page.more && page.cursor == "98", "Membership reviews retain answers and next-page identity");
    request = {
        .section = S::JoinRequests,
        .verb = V::Update,
        .guildId = "1",
        .resourceId = "98",
        .values = {{"action", "REJECTED"}, {"rejection_reason", "Please answer the questions"}}
    };
    require(
        guildSettingsHttpRequest(request)->method == "PATCH", "Membership review sends explicit approval or rejection"
    );
    request = {.section = S::JoinInterview, .verb = V::Create, .guildId = "1", .resourceId = "98"};
    require(
        guildSettingsHttpRequest(request)->path == "/join-requests/98/interview",
        "Membership interviews use the actual group DM endpoint"
    );
    request = {
        .section = S::Boosts,
        .verb = V::Create,
        .guildId = "1",
        .values = {
            {"user_premium_guild_subscription_slot_ids", json::array({"2"})}, {"disable_powerup_auto_apply", true}
        }
    };
    require(
        guildSettingsHttpRequest(request)->method == "PUT",
        "Existing boost slots can be allocated without a billing purchase"
    );
    access = {.self = "10", .owner = "11", .permissions = 0};
    require(
        canManageGuildSettings(request, access),
        "Boost allocation uses personal slot ownership rather than server management"
    );
    request.verb = V::Delete;
    request.resourceId = "2";
    request.values = json::object();
    require(!canManageGuildSettings(request, access), "Boost withdrawal cannot target another account's slot");
    access.targetCreator = "10";
    require(canManageGuildSettings(request, access), "Own applied boost slots can be withdrawn");
    request = {
        .section = S::CommandPermissions,
        .verb = V::Update,
        .guildId = "1",
        .resourceId = "3",
        .values = {
            {"application_id", "2"},
            {"permissions",
             json::array(
                 {{{"id", "4"}, {"type", 1}, {"permission", true}}, {{"id", "5"}, {"type", 3}, {"permission", false}}}
             )}
        }
    };
    http = guildSettingsHttpRequest(request);
    require(
        http
            && http->method == "PUT"
            && http->path == "/applications/2/guilds/1/commands/3/permissions"
            && !http->body.contains("application_id"),
        "Command overrides use the verified personal client PUT route"
    );
    request.values["permissions"].push_back(request.values["permissions"][0]);
    require(!guildSettingsHttpRequest(request), "Duplicate command overrides are rejected before submission");
    request = {
        .section = S::Events,
        .verb = V::Update,
        .guildId = "1",
        .resourceId = "2",
        .values = {
            {"recurrence_rule",
             {{"start", "2026-10-20T12:00:00Z"}, {"frequency", 2}, {"interval", 2}, {"by_weekday", json::array({1})}}}
        }
    };
    require(
        guildSettingsHttpRequest(request).has_value(),
        "Every-other-week event recurrence preserves the official numeric frequency"
    );
    request.values["recurrence_rule"]["by_weekday"].push_back(2);
    require(!guildSettingsHttpRequest(request), "Weekly recurrence is limited to one weekday by the current API");
    request.values["recurrence_rule"] = {
        {"start", "2026-10-20T12:00:00Z"},
        {"frequency", 3},
        {"interval", 1},
        {"by_weekday", json::array({0, 1, 2, 3, 4})}
    };
    require(guildSettingsHttpRequest(request).has_value(), "Known weekday daily recurrence is supported");
    request.values["recurrence_rule"]["count"] = 10;
    require(!guildSettingsHttpRequest(request), "The documented read-only recurrence count cannot be written");
    request.values["recurrence_rule"] = {
        {"start", "2026-10-20T12:00:00Z"},
        {"frequency", 1},
        {"interval", 1},
        {"by_n_weekday", json::array({{{"n", 4}, {"day", 2}}})}
    };
    require(guildSettingsHttpRequest(request).has_value(), "Monthly recurrence uses a bounded nth weekday");
    request.values["recurrence_rule"] = nullptr;
    require(
        guildSettingsHttpRequest(request).has_value(), "Recurring events can be returned to a single event explicitly"
    );
    request = {.section = S::InstalledApplications, .guildId = "1"};
    require(
        guildSettingsHttpRequest(request)->path == "/guilds/1/applications",
        "Installed app metadata comes from the actual guild catalog"
    );
    request.section = S::ApplicationCommands;
    require(
        guildSettingsHttpRequest(request)->path == "/guilds/1/application-command-index",
        "Personal command metadata uses the current client index"
    );
    rejects(
        [&] { parseGuildSettings(json::object(), request); },
        "Pending command indexes remain visible errors with explicit retry"
    );
    const json commandIndex{
        {"applications", json::array({{{"id", "2"}, {"name", "Example"}}})}, {"application_commands", json::array()}
    };
    require(
        parseGuildSettings(commandIndex, request).data == commandIndex,
        "Command metadata and indexed permission fields are preserved"
    );
    request = {.section = S::CommandPermissions, .guildId = "1", .values = {{"application_id", "2"}}};
    rejects(
        [&] { parseGuildSettings(json::array({{{"id", "3"}, {"guild_id", "9"}, {"application_id", "2"}}}), request); },
        "Foreign command permissions cannot enter another server's draft"
    );
    request.verb = V::Update;
    access = {.self = "10", .owner = "11", .permissions = 1ULL << 5};
    require(!canManageGuildSettings(request, access), "Manage Guild alone does not grant command override editing");
    access.permissions = (1ULL << 5) | (1ULL << 28);
    require(canManageGuildSettings(request, access), "Command override editing requires both management permissions");
    request = {.section = S::ServerGuide, .verb = V::Update, .guildId = "1", .values = {{"enabled", false}}};
    require(
        guildSettingsHttpRequest(request)->method == "PUT", "The current personal client replaces Server Guide with PUT"
    );
    request = {
        .section = S::GuideAction, .verb = V::Update, .guildId = "1", .resourceId = "2", .values = {{"icon", nullptr}}
    };
    require(
        guildSettingsHttpRequest(request)->method == "PATCH"
            && guildSettingsHttpRequest(request)->path == "/guilds/1/new-member-actions/2",
        "Guide action icons have their dedicated PATCH route"
    );
    request.section = S::GuideResource;
    require(guildSettingsHttpRequest(request)->method == "PUT", "Guide resource icons use their distinct PUT route");
    request = {.section = S::DiscoveryCategory, .verb = V::Create, .guildId = "1", .resourceId = "2"};
    require(
        guildSettingsHttpRequest(request)->method == "PUT"
            && guildSettingsHttpRequest(request)->path == "/guilds/1/discovery-categories/2",
        "Secondary discovery categories use the verified dedicated resource"
    );
    request = {
        .section = S::Access,
        .verb = V::Update,
        .guildId = "1",
        .values = {
            {"mode", "apply"},
            {"enabled", true},
            {"form_fields",
             json::array({{{"field_type", "TEXT_INPUT"}, {"label", "Why do you want to join?"}, {"required", true}}})}
        }
    };
    const json originalAccessGuild{
        {"id", "1"}, {"features", json::array({"COMMUNITY", "DISCOVERABLE", "UNKNOWN_FUTURE_FEATURE"})}
    };
    const json originalScreening{
        {"enabled", false}, {"form_fields", json::array()}, {"description", "A preserved introduction"}
    };
    auto accessPlan = guildAccessPlan(originalAccessGuild, originalScreening, request);
    require(
        accessPlan.guildPatch["features"] == json::array({"COMMUNITY", "UNKNOWN_FUTURE_FEATURE"})
            && accessPlan.verificationPatch["description"] == "A preserved introduction"
            && accessPlan.verificationPatch["enabled"] == true,
        "Application mode preserves unknown guild features and untouched form metadata"
    );
    require(
        !accessPlan.verificationPatch.contains("mode") && !accessPlan.guildPatch.contains("manual_approval_enabled"),
        "UI access modes never become invented API fields"
    );
    request.values = {{"mode", "apply"}, {"enabled", true}, {"form_fields", json::array()}};
    rejects(
        [&] { guildAccessPlan(originalAccessGuild, originalScreening, request); },
        "Application mode requires at least one application question"
    );
    request.values = {{"mode", "discoverable"}, {"enabled", false}, {"form_fields", json::array()}};
    const json privateGuild{{"id", "1"}, {"features", json::array({"UNKNOWN_FUTURE_FEATURE"})}};
    rejects(
        [&] { guildAccessPlan(privateGuild, originalScreening, request); }, "Discovery requires the Community feature"
    );
    const json applicationGuild{
        {"id", "1"},
        {"features",
         json::array(
             {"COMMUNITY", "MEMBER_VERIFICATION_GATE_ENABLED", "MEMBER_VERIFICATION_MANUAL_APPROVAL",
              "UNKNOWN_FUTURE_FEATURE"}
         )}
    };
    accessPlan = guildAccessPlan(applicationGuild, originalScreening, request);
    require(
        accessPlan.guildPatch["features"]
            == json::array({"COMMUNITY", "MEMBER_VERIFICATION_GATE_ENABLED", "UNKNOWN_FUTURE_FEATURE", "DISCOVERABLE"}),
        "Discovery replaces only the primary-client documented access flags"
    );
    require(
        guildAccessMode(applicationGuild) == "apply"
            && guildAccessMode(originalAccessGuild) == "discoverable"
            && guildAccessMode(privateGuild) == "invite",
        "Access read modes use actual returned guild features"
    );
    request.values = {
        {"mode", "invite"}, {"enabled", false}, {"form_fields", json::array()}, {"bulk_action", "REJECTED"}
    };
    access.permissions = 1ULL << 5;
    require(!canManageGuildSettings(request, access), "Resolving pending applications also requires Kick Members");
    access.permissions = (1ULL << 5) | (1ULL << 1);
    require(
        canManageGuildSettings(request, access), "Explicit application resolution requires both relevant permissions"
    );
    request = {.section = S::MonetizationRequirements, .guildId = "1"};
    require(
        guildSettingsHttpRequest(request)->path == "/guilds/1/creator-monetization/requirements",
        "Native monetization eligibility uses the verified read resource"
    );
    request.section = S::MonetizationRestrictions;
    require(
        guildSettingsHttpRequest(request)->path == "/guilds/1/creator-monetization/restrictions",
        "Monetization restrictions are a separate native read"
    );
    request = {
        .section = S::ServerGuide, .verb = V::Update, .guildId = "1", .values = {{"new_member_actions", json::array()}}
    };
    for (unsigned i = 0; i < 5; ++i)
      request.values["new_member_actions"].push_back(
          {{"channel_id", std::to_string(i + 2)}, {"action_type", 0}, {"title", "Read an introduction"}}
      );
    require(guildSettingsHttpRequest(request).has_value(), "The current client allows five Server Guide tasks");
    request.values["new_member_actions"].push_back(
        {{"channel_id", "9"}, {"action_type", 0}, {"title", "One too many"}}
    );
    require(!guildSettingsHttpRequest(request), "A sixth Server Guide task exceeds the source-verified limit");
    request.values = {{"resource_channels", json::array()}};
    for (unsigned i = 0; i < 7; ++i)
      request.values["resource_channels"].push_back({{"channel_id", std::to_string(i + 2)}, {"title", "A resource"}});
    require(guildSettingsHttpRequest(request).has_value(), "The current client allows seven Server Guide resources");
    request = {
        .section = S::Access,
        .verb = V::Update,
        .guildId = "1",
        .values = {{"mode", "invite"}, {"description", "Only the description changes"}}
    };
    accessPlan = guildAccessPlan(privateGuild, originalScreening, request);
    require(
        accessPlan.verificationPatch["description"] == "Only the description changes"
            && accessPlan.verificationPatch["form_fields"] == originalScreening["form_fields"],
        "Access description-only saves preserve freshly loaded forms"
    );
    std::cout << "Guild settings protocol and hierarchy tests passed\n";
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
